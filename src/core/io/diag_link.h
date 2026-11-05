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
// and sets a volatile flag. Everything else happens in the main loop.
//
// Requires daisy.h included before this file (DIAG builds).

#include <stdint.h>

#ifdef CT3_DIAG_BUILD

#include <stdarg.h>
#include <stdio.h>

#ifndef DIAG_GIT_HASH
#define DIAG_GIT_HASH "unknown"
#endif
#ifndef DIAG_BUILD_TIME
#define DIAG_BUILD_TIME "unknown"
#endif

namespace diag_link {

static constexpr uint32_t LINK_DEAD_MS = 50;   // failing this long -> link dead
static constexpr int      LINE_BUF     = 256;  // bytes per line incl. "\r\n"

// C++14 (no inline variables): shared state via function-local statics.
struct State {
  bool     dead         = false;
  bool     failing      = false;   // last transmit failed
  uint32_t fail_since   = 0;       // ms of the first failure in this run
  uint32_t drops        = 0;
  int      next_buf     = 0;
  char     buf[2][LINE_BUF];
};
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
  for (uint32_t i = 0; i < *len; i++)
    if (buf[i] == '?') Query() = true;
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

// Main loop: on `?` re-arm a dead link and answer with the BUILD line.
template <typename Seed>
inline void Poll(Seed& /*seed*/, const char* pedal) {
  if (!Query()) return;
  Query() = false;
  State& s = St();
  s.dead = false;
  s.failing = false;
  PrintLine("BUILD %s %s DIAG %s", pedal, DIAG_GIT_HASH, DIAG_BUILD_TIME);
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
