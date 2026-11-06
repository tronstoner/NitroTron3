#pragma once
//
// core/io/diag_link.h
//
// DIAG builds only: the USB-serial side of the DIAG firmware.
//
// 1. Non-blocking print. libDaisy's Logger (hw.seed.Print/PrintLine) switches
//    itself to blocking transfers after two packets and then spins in
//    `while (!Transmit())`. When the USB cable is pulled (or nobody reads) the
//    CDC TxState stays busy forever and the main loop hangs there - controls
//    freeze while audio keeps running. So DIAG code never calls the Logger:
//      diag_link::PrintLine(fmt, ...)   // one line, "\r\n" appended
//      diag_link::Print(fmt, ...)       // no line end (first half of a line)
//    Each formats into its own static buffer (vsnprintf, so libDaisy's
//    FLT_FMT/FLT_VAR and plain integer formats work as before) and calls the
//    USB CDC transmit (UsbHandle::TransmitInternal -> CDC_Transmit_FS) exactly
//    ONCE. Busy / failed -> the line is dropped and counted (Drops()), never
//    retried. CDC_Transmit_FS hands the whole buffer to the USB stack in one
//    call (the stack splits it into 64-byte packets), so lines up to the
//    buffer size go out in one call.
//    Two alternating buffers: a new line is never formatted into the buffer
//    the USB stack may still be sending.
//
// 2. Link-dead detection. When transmits keep failing for LINK_DEAD_MS while
//    lines are attempted, the link is marked dead and every print is skipped
//    without touching USB (counted as a drop) until the host sends `?` again
//    (tools/diag/log.py sends "?\n" on every connect / reconnect).
//
// 3. `?` -> BUILD line:  BUILD <pedal> <git hash[-dirty]> DIAG <build time>
//    so the host logger can tell which firmware is talking. The Makefile
//    defines DIAG_GIT_HASH / DIAG_BUILD_TIME only when DIAG=1 (together with
//    CT3_DIAG_BUILD).
//
// 4. Line commands (newline-terminated, '\r' ignored, unknown lines ignored):
//      dfu   -> prints `DFU entering bootloader`, waits DFU_DELAY_MS (checked
//               on later Polls, never a busy wait) so the line can go out,
//               then stops ADC + audio and jumps to the Daisy bootloader the
//               same way the shells' both-footswitch gesture does
//               (System::ResetToBootloader(DAISY_INFINITE_TIMEOUT); no LED
//               blink). Used by tools/devtool for gesture-free flashing.
//    `?` keeps working as before (any '?' byte, no newline needed).
//    Live constants (src/core/util/tunable.h; LIVE_TUNABLES = firmware DIAG
//    builds; docs/DEV_INTERFACE.md step 2):
//      list                 -> `T <name> <f|i|b> <value>` per tunable, then `T END <count>`
//      get <name>           -> `T <name> <type> <value>`
//      set <name> <value>   -> stored (one 32-bit / bool store), echoed as above
//      reset <name>|all     -> back to the compiled default, echoed as above
//      errors               -> `T ERR <reason>`
//    Values: float = shortest decimal that reads back as the same float
//    (own formatter: newlib-nano printf has no %g), int decimal, bool 0 / 1.
//    Parsing: float strtof (non-finite rejected), int strtol, bool 0/1/true/false;
//    the whole token must parse. Replies are queued (a per-tunable `echo` flag,
//    a `list` cursor, one error slot) and sent from Poll, packed several lines
//    per USB transmit; a busy USB keeps them queued for the next Poll (never
//    dropped like log lines). They are never sent while a shell line is half
//    printed (Print without PrintLine), so they cannot split a log line.
//
// Usage in a pedal shell:
//   hw.seed.StartLog(false);             // USB CDC init (unchanged)
//   DIAG_LINK_INIT();                    // after StartLog
//   DIAG_LINK_POLL(hw.seed, "nitrolab"); // in the main loop (never the ISR)
//   diag_link::PrintLine(...)            // main loop only
// In non-DIAG builds the macros expand to nothing and Print/PrintLine are
// empty inline functions (only ever called behind a constexpr-false DIAG
// flag), so those binaries are byte-identical.
//
// The USB receive callback runs in interrupt context: it only scans for '?'
// (volatile flag) and copies the bytes into a small single-producer /
// single-consumer ring. Line assembly and command parsing happen in Poll()
// (main loop).
//
// Requires daisy.h included before this file (DIAG builds).

#include <stdint.h>

#ifdef CT3_DIAG_BUILD

#include <stdarg.h>
#include <stdio.h>

#ifdef LIVE_TUNABLES
#include <stdlib.h>
#include <string.h>
#include "tunable.h"   // src/core/util
#endif

#ifndef DIAG_GIT_HASH
#define DIAG_GIT_HASH "unknown"
#endif
#ifndef DIAG_BUILD_TIME
#define DIAG_BUILD_TIME "unknown"
#endif

namespace diag_link {

static constexpr uint32_t LINK_DEAD_MS = 50;   // failing this long -> link dead
static constexpr int      LINE_BUF     = 256;  // bytes per line incl. "\r\n"
static constexpr uint32_t DFU_DELAY_MS = 20;   // let the DFU line go out before the jump
#ifdef LIVE_TUNABLES
static constexpr int      RX_RING      = 512;  // ISR -> main loop bytes (power of 2; slider `set` bursts)
static constexpr int      CMD_BUF      = 80;   // longest accepted command line (`set <name> <value>`)
#else
static constexpr int      RX_RING      = 64;   // ISR -> main loop bytes (power of 2)
static constexpr int      CMD_BUF      = 16;   // longest accepted command line
#endif

// C++14 (no inline variables): shared state via function-local statics.
struct State {
  bool     dead         = false;
  bool     failing      = false;   // last transmit failed
  uint32_t fail_since   = 0;       // ms of the first failure in this run
  uint32_t drops        = 0;
  int      next_buf     = 0;
  char     buf[2][LINE_BUF];
  // command line assembly (main loop only)
  char     cmd[CMD_BUF];
  int      cmd_len      = 0;
  bool     cmd_overflow = false;     // line too long -> ignore until newline
  bool     dfu_pending  = false;
  uint32_t dfu_at       = 0;
  bool     mid_line     = false;     // last sent text was a Print without line end
#ifdef LIVE_TUNABLES
  tunable::Node* list_cur = nullptr; // `list`: next node to send
  bool     list_on      = false;     // `list` running (incl. the END line)
  char     err[64];                  // pending `T ERR` text ("" = none; latest wins)
#endif
};
// ISR -> main loop byte ring. The ISR only writes `head`, the main loop only
// writes `tail`; both are 32-bit (atomic on the M7). Full ring -> bytes dropped.
struct Rx {
  volatile uint32_t head = 0;
  volatile uint32_t tail = 0;
  uint8_t           data[RX_RING];
};
inline Rx& RxRing() {
  static Rx r;
  return r;
}
inline State& St() {
  static State s;
  return s;
}
inline volatile bool& Query() {
  static volatile bool q = false;
  return q;
}
inline daisy::UsbHandle& Usb() {
  static daisy::UsbHandle usb;   // stateless handle, shared with the logger
  return usb;
}

inline void OnRx(uint8_t* buf, uint32_t* len) {
  Rx& r = RxRing();
  uint32_t h = r.head;
  for (uint32_t i = 0; i < *len; i++) {
    if (buf[i] == '?') Query() = true;
    if (h - r.tail < (uint32_t)RX_RING) {
      r.data[h & (RX_RING - 1)] = buf[i];
      h++;
    }
  }
  __asm__ volatile("" ::: "memory");   // data before head (same core: no DMB needed)
  r.head = h;
}

inline void Init() {
  Usb().SetReceiveCallback(OnRx, daisy::UsbHandle::FS_INTERNAL);
}

inline uint32_t Drops() { return St().drops; }
inline bool     LinkDead() { return St().dead; }

// Format + one transmit attempt. Never blocks, never retries.
inline void SendV(bool newline, const char* fmt, va_list va) {
  State& s = St();
  if (s.dead) { s.drops++; return; }
  char* b = s.buf[s.next_buf];
  int len = vsnprintf(b, LINE_BUF - 2, fmt, va);
  if (len < 0) { s.drops++; return; }
  if (len > LINE_BUF - 3) len = LINE_BUF - 3;   // truncated by vsnprintf
  if (newline) { b[len++] = '\r'; b[len++] = '\n'; }
  if (len == 0) return;
  const uint32_t now = daisy::System::GetNow();
  if (Usb().TransmitInternal((uint8_t*)b, (size_t)len) == daisy::UsbHandle::Result::OK) {
    s.failing = false;
    s.next_buf ^= 1;               // the stack owns this buffer until sent
    s.mid_line = !newline;
  } else {
    s.drops++;
    if (!s.failing) { s.failing = true; s.fail_since = now; }
    else if (now - s.fail_since >= LINK_DEAD_MS) s.dead = true;
  }
}

inline void PrintLine(const char* fmt, ...) {
  va_list va; va_start(va, fmt);
  SendV(true, fmt, va);
  va_end(va);
}

inline void Print(const char* fmt, ...) {
  va_list va; va_start(va, fmt);
  SendV(false, fmt, va);
  va_end(va);
}

#ifdef LIVE_TUNABLES
// --- live constants --------------------------------------------------------
// Float -> shortest decimal text that strtof reads back as the same float
// (precision 1..9 significant digits, plain or exponent notation). Main loop.
inline int FmtSig(char* out, double a, int prec) {   // a > 0, finite; returns length
  // decimal exponent by scaling (no log10 / pow: keeps libm's double tables out of the DIAG binary)
  int e = 0;
  double x = a;
  while (x >= 10.0) { x /= 10.0; e++; }
  while (x < 1.0) { x *= 10.0; e--; }
  for (int i = 1; i < prec; i++) x *= 10.0;
  uint64_t m = (uint64_t)(x + 0.5);                    // prec digits
  uint64_t lim = 1;
  for (int i = 0; i < prec; i++) lim *= 10u;
  if (m >= lim) { m = (m + 5u) / 10u; e++; }
  char dg[12];
  for (int i = prec - 1; i >= 0; i--) { dg[i] = (char)('0' + (int)(m % 10u)); m /= 10u; }
  int nd = prec;
  while (nd > 1 && dg[nd - 1] == '0') nd--;            // trailing zeros
  int n = 0;
  if (e < -5 || e >= 10) {                             // d.ddde+-x
    out[n++] = dg[0];
    if (nd > 1) { out[n++] = '.'; for (int i = 1; i < nd; i++) out[n++] = dg[i]; }
    n += snprintf(out + n, 8, "e%d", e);
  } else if (e < 0) {                                  // 0.000ddd
    out[n++] = '0'; out[n++] = '.';
    for (int i = 0; i < -e - 1; i++) out[n++] = '0';
    for (int i = 0; i < nd; i++) out[n++] = dg[i];
  } else {                                             // ddd.ddd
    for (int i = 0; i <= e; i++) out[n++] = (i < nd) ? dg[i] : '0';
    if (nd > e + 1) { out[n++] = '.'; for (int i = e + 1; i < nd; i++) out[n++] = dg[i]; }
  }
  out[n] = '\0';
  return n;
}
inline int FmtFloat(char* out, float v) {              // out: >= 24 bytes
  if (v != v) { strcpy(out, "nan"); return 3; }
  if (v == 0.f) { strcpy(out, (1.f / v < 0.f) ? "-0" : "0"); return (int)strlen(out); }
  int n = 0;
  if (v < 0.f) out[n++] = '-';
  const double a = (v < 0.f) ? -(double)v : (double)v;
  for (int prec = 1; prec <= 9; prec++) {
    FmtSig(out + n, a, prec);
    float back = strtof(out + n, nullptr);
    if (back == (float)a) break;
  }
  return (int)strlen(out);
}
// "T <name> <ty> <value>\r\n" into out (cap bytes); returns length, 0 if it does not fit.
inline int FmtTunable(char* out, int cap, const tunable::Node* t) {
  char v[24];
  if (t->ty == 'f') { float f; uint32_t b = t->Bits(); memcpy(&f, &b, 4); FmtFloat(v, f); }
  else if (t->ty == 'i') { int i; uint32_t b = t->Bits(); memcpy(&i, &b, 4); snprintf(v, sizeof v, "%d", i); }
  else snprintf(v, sizeof v, "%d", (int)t->Bits());
  int n = snprintf(out, (size_t)cap, "T %s %c %s\r\n", t->name, t->ty, v);
  return (n > 0 && n < cap) ? n : 0;
}
inline void TunErr(const char* fmt, const char* arg) {
  snprintf(St().err, sizeof St().err, fmt, arg);
}
// Value text -> raw bits for t. Returns false (and sets the error) if it does not parse.
inline bool ParseValue(const tunable::Node* t, const char* txt, uint32_t& bits) {
  char* end = nullptr;
  if (t->ty == 'f') {
    const float f = strtof(txt, &end);
    if (end == txt || *end) { TunErr("bad value %s", txt); return false; }
    memcpy(&bits, &f, 4);
    if ((bits & 0x7F800000u) == 0x7F800000u) { TunErr("not finite %s", txt); return false; }   // inf / nan
    return true;
  }
  if (t->ty == 'i') {
    const long l = strtol(txt, &end, 10);
    if (end == txt || *end || l < -2147483647L || l > 2147483647L) { TunErr("bad int %s", txt); return false; }
    const int i = (int)l;
    memcpy(&bits, &i, 4);
    return true;
  }
  if (!strcmp(txt, "1") || !strcmp(txt, "true"))  { bits = 1u; return true; }
  if (!strcmp(txt, "0") || !strcmp(txt, "false")) { bits = 0u; return true; }
  TunErr("bad bool %s", txt);
  return false;
}
// `list` / `get` / `set` / `reset` (line already split at spaces into a, b, c). Returns true if it was one.
inline bool TunCommand(const char* a, const char* b, const char* c) {
  State& s = St();
  if (!strcmp(a, "list")) {
    if (b[0]) { TunErr("list takes no argument%s", ""); return true; }
    s.list_cur = tunable::Head();
    s.list_on = true;
    return true;
  }
  const bool get = !strcmp(a, "get"), set = !strcmp(a, "set"), reset = !strcmp(a, "reset");
  if (!get && !set && !reset) return false;
  if (!b[0]) { TunErr("%s needs a name", a); return true; }
  if (reset && !strcmp(b, "all")) {
    for (tunable::Node* t = tunable::Head(); t; t = t->next) { t->Reset(); t->echo = true; }
    return true;
  }
  tunable::Node* t = tunable::Find(b);
  if (!t) { TunErr("unknown name %s", b); return true; }
  if (set) {
    if (!c[0]) { TunErr("set %s needs a value", b); return true; }
    uint32_t bits;
    if (!ParseValue(t, c, bits)) return true;
    t->Store(bits);
  } else if (reset) {
    t->Reset();
  }
  t->echo = true;
  return true;
}
// Main loop (Poll): send due `T` lines, packed into one transmit; nothing is lost
// when USB is busy (sent on a later Poll). Never while a shell line is half printed.
inline void TunFlush() {
  State& s = St();
  if (s.dead || s.mid_line) return;
  bool any = s.err[0] != '\0' || s.list_on;
  for (tunable::Node* t = tunable::Head(); t && !any; t = t->next) any = t->echo;
  if (!any) return;
  char* b = s.buf[s.next_buf];
  int len = 0;
  const int cap = LINE_BUF;
  tunable::Node* sent[24];
  int nsent = 0;
  tunable::Node* lcur = s.list_cur;
  bool lend = false, errsent = false;
  if (s.err[0]) {
    const int n = snprintf(b, (size_t)cap, "T ERR %s\r\n", s.err);
    if (n > 0 && n < cap) { len = n; errsent = true; }
  }
  for (tunable::Node* t = tunable::Head(); t && nsent < 24; t = t->next) {   // echoes first (set / get / reset)
    if (!t->echo) continue;
    const int n = FmtTunable(b + len, cap - len, t);
    if (!n) break;
    len += n; sent[nsent++] = t;
  }
  if (s.list_on) {
    while (lcur) {
      const int n = FmtTunable(b + len, cap - len, lcur);
      if (!n) break;
      len += n; lcur = lcur->next;
    }
    if (!lcur) {
      const int n = snprintf(b + len, (size_t)(cap - len), "T END %d\r\n", tunable::Count());
      if (n > 0 && n < cap - len) { len += n; lend = true; }
    }
  }
  if (len == 0) return;
  const uint32_t now = daisy::System::GetNow();
  if (Usb().TransmitInternal((uint8_t*)b, (size_t)len) == daisy::UsbHandle::Result::OK) {
    s.failing = false;
    s.next_buf ^= 1;
    if (errsent) s.err[0] = '\0';
    for (int i = 0; i < nsent; i++) sent[i]->echo = false;
    if (s.list_on) { s.list_cur = lcur; if (lend) s.list_on = false; }
  } else {                         // busy: everything stays queued (no drop count: nothing lost)
    if (!s.failing) { s.failing = true; s.fail_since = now; }
    else if (now - s.fail_since >= LINK_DEAD_MS) s.dead = true;
  }
}
#endif

// One complete command line (main loop). Unknown lines are ignored.
inline void OnLine(const char* line) {
  State& s = St();
#ifdef LIVE_TUNABLES
  {
    // split "a b c" at single / multiple spaces (copies: line is the command buffer)
    char a[CMD_BUF], b[CMD_BUF], c[CMD_BUF];
    a[0] = b[0] = c[0] = '\0';
    char* f[3] = { a, b, c };
    int k = 0, j = 0;
    bool extra = false;
    for (const char* p = line; ; p++) {
      const char ch = *p;
      if (ch == ' ' || ch == '\t' || ch == '\0') {
        if (j > 0) { f[k][j] = '\0'; k++; j = 0; }
        if (ch == '\0') break;
        continue;
      }
      if (k >= 3) { extra = true; continue; }
      f[k][j++] = ch;
    }
    if (TunCommand(a, b, c)) {
      if (extra) TunErr("too many arguments for %s", a);
      return;
    }
  }
#endif
  if (line[0] == 'd' && line[1] == 'f' && line[2] == 'u' && line[3] == '\0') {
    if (s.dfu_pending) return;
    s.dead = false;      // the host is listening: give the line a chance
    s.failing = false;
    PrintLine("DFU entering bootloader");
    s.dfu_pending = true;
    s.dfu_at = daisy::System::GetNow();
  }
}

// Main loop: drain the RX ring into command lines; on `?` re-arm a dead link
// and answer with the BUILD line; a pending `dfu` jumps once DFU_DELAY_MS passed.
template <typename Seed>
inline void Poll(Seed& seed, const char* pedal) {
  State& s = St();
  Rx& r = RxRing();
  const uint32_t h = r.head;
  __asm__ volatile("" ::: "memory");   // head before data
  uint32_t t = r.tail;
  while (t != h) {
    const char c = (char)r.data[t & (RX_RING - 1)];
    t++;
    if (c == '\n') {
      if (!s.cmd_overflow) {
        s.cmd[s.cmd_len] = '\0';
        OnLine(s.cmd);
      }
      s.cmd_len = 0;
      s.cmd_overflow = false;
    } else if (c == '\r') {
      // ignored (CRLF hosts)
    } else if (s.cmd_len < CMD_BUF - 1) {
      s.cmd[s.cmd_len++] = c;
    } else {
      s.cmd_overflow = true;
    }
  }
  r.tail = t;

  if (Query()) {
    Query() = false;
    s.dead = false;
    s.failing = false;
    s.mid_line = false;            // a new connection: start on a fresh line
    PrintLine("BUILD %s %s DIAG %s", pedal, DIAG_GIT_HASH, DIAG_BUILD_TIME);
  }
#ifdef LIVE_TUNABLES
  TunFlush();
#endif

  if (s.dfu_pending && daisy::System::GetNow() - s.dfu_at >= DFU_DELAY_MS) {
    // Same exit as the shells' both-footswitch gesture (Hothouse::StopAdc =
    // seed.adc.Stop(), Hothouse::StopAudio = seed.StopAudio()).
    seed.adc.Stop();
    seed.StopAudio();
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
  }
}

}  // namespace diag_link

#define DIAG_LINK_INIT() diag_link::Init()
#define DIAG_LINK_POLL(seed, pedal) diag_link::Poll(seed, pedal)

#else

namespace diag_link {
// Non-DIAG: callers sit behind a constexpr-false DIAG flag (dead code).
inline void PrintLine(const char*, ...) {}
inline void Print(const char*, ...) {}
inline uint32_t Drops() { return 0; }
inline bool LinkDead() { return false; }
}  // namespace diag_link

#define DIAG_LINK_INIT() ((void)0)
#define DIAG_LINK_POLL(seed, pedal) ((void)0)

#endif
