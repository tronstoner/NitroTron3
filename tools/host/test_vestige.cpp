// test_vestige.cpp — the REAL vestige module on the host, stubbed hardware.
//
// Drives Controls() every 10 ms and Process() every 48 samples like the shell,
// over a synthetic bass pluck, and checks the onward-rework behaviour:
//
//   stage 0  FS2 transport (playing / stopped / held-playing / held-stopped),
//            K2 length + direction, SW1 1-voice / 6-voice / freeze, K6 not owned.
//   buffers  the loop side and the freeze side are DISTINCT memory: address
//            ranges disjoint, each slot's RingBuffer as long as its own row,
//            neither side ever writes the other's slab, every capture write
//            stays inside its scratch row, and an SW1 switch between the sides
//            carries no content across (unheld: the side left is cleared;
//            held: it is kept, untouched, and comes back).
//
// Private state is inspected directly (#define private public) — this is a
// white-box test of the module, not of its interface.
//
// Build/run via tools/host/run.sh.
//
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <vector>
#include <string>
#include <algorithm>
#include "daisy.h"
#include "hothouse.h"
#include "control_surface.h"
#include "knob_map.h"
#include "constants.h"
#define private public
#include "vestige.h"
#undef private
uint32_t daisy::System::now_ms = 0;

static Vestige v;
static ControlSurface cs;
static daisy::Led led1, led2;
static const float sr = 48000.f;
static long n = 0;
static bool play_input = true;
static bool sustain_input = false;   // steady tone instead of plucks
// Deterministic noise bursts [noise_from, noise_to) (sample numbers): a sharp
// autocorrelation, so the output can be lined up against the input exactly.
static long noise_from = -1, noise_to = -1;
// Click train: 4 sharp decaying clicks per 500 ms on a low bed (keeps the gate open).
static bool click_on = false; static long click_from = 0;
static float Noise(long k) {
  uint32_t h = (uint32_t)k * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  return ((float)(h & 0xFFFF) / 32768.f - 1.f);
}
// Input / wet history (only while hist_on), indexed n - hist_n0.
static bool hist_on = false; static long hist_n0 = 0;
static std::vector<float> in_hist, wet_hist;
static float sustain_hz = 110.f;
static float peak = 0.f, maxabs = 0.f, maxd = 0.f, prevw = 0.f;
static int bad = 0;
static long rec_overrun = 0;     // blocks where rec_idx_ exceeded its scratch row
static size_t frz_rec_max = 0;   // furthest index the freeze scratch reached

static bool note_on = false;
static float NoteIn(long k);
static float Input(long k) {
  if (note_on) return NoteIn(k);
  if (k >= noise_from && k < noise_to) return 0.25f * Noise(k);
  if (click_on) {
    const long ph = (k - click_from) % 6000;             // 125 ms
    const float click = (ph < 480) ? 0.5f * expf(-(float)ph / 60.f) * Noise(k) : 0.f;
    return click + 0.03f * sinf(2.f * 3.14159265f * 110.f * (float)k / sr);
  }
  if (!play_input) return 0.f;
  if (sustain_input) return 0.25f * sinf(2.f * 3.14159265f * sustain_hz * (float)k / sr);
  const float period = 0.7f;
  float t = fmodf((float)k / sr, period);
  float env = (t < 0.35f) ? expf(-t * 3.f) * (t < 0.005f ? t / 0.005f : 1.f) : 0.f;
  float ph = 2.f * 3.14159265f * 73.4f * (float)k / sr;
  return 0.25f * env * (sinf(ph) + 0.5f * sinf(2 * ph));
}

// Footswitch state injected per tick, with the real ControlSurface's semantics
// (held_ms = 0 whenever the switch is up — so also on the falling edge).
static int fs_down_ticks[2] = {0, 0};   // >0: pressed for this many more ticks
static uint32_t fs_since[2] = {0, 0};
static bool fs_last[2] = {false, false};
// LED1 rising edges, as the sample number at the tick that lit it.
static std::vector<long> led1_rises;
static bool led1_prev = false;
static long led1_on_while_off = 0;      // ticks LED1 was lit while the effect was off

static void Tick() {
  daisy::System::now_ms += 10;
  for (int f = 0; f < 2; f++) {
    bool down = fs_down_ticks[f] > 0;
    if (fs_down_ticks[f] > 0) fs_down_ticks[f]--;
    FootswitchEvent& e = cs.fs[f];
    e.rising = down && !fs_last[f]; e.falling = !down && fs_last[f];
    if (e.rising) fs_since[f] = daisy::System::now_ms;
    e.down = down; e.held_ms = down ? daisy::System::now_ms - fs_since[f] : 0;
    fs_last[f] = down;
  }
  v.Controls(cs, led1, led2);
  const bool on = led1.v > 0.5f;
  if (on && !led1_prev) led1_rises.push_back(n);
  if (on && !v.engaged_) led1_on_while_off++;
  led1_prev = on;
}
static int& fs2_down_ticks = fs_down_ticks[1];

static int  blk = 48;                  // Process() block size (1 = per-sample audit mode)
static bool audit_on = false;
static long audit_bad = 0, audit_reads = 0;
static long audit_max_over = 0;        // deepest guard read past L, minus min(L, guard) (<= 1 expected)
static int  max_grains = 0;
static int  max_counted = 0;          // max grains counted by the cap (active, not fading out)
static void Audit();
// Per-sample C change watcher (blk must be 1): play-length changes of one slot
// and whether each landed exactly on a wrap of its clean head.
static int  watch_s = -1; static size_t watch_pl = 0; static float watch_prev = 0.f;
static long watch_changes = 0, watch_midpass = 0;
static void WatchStep();
static void RunFor(float secs) {
  float in[48], wet[48];
  long end = n + (long)(secs * sr);
  peak = 0.f;
  while (n < end) {
    if (n % 480 == 0) Tick();
    for (int i = 0; i < blk; i++) in[i] = Input(n + i);
    v.Process(in, wet, blk);
    if (audit_on) Audit();
    if (watch_s >= 0) WatchStep();
    { int a = 0, c = 0; for (int g = 0; g < VESTIGE_GRAINS; g++) if (v.grains_[g].IsActive()) { a++; if (!v.grains_[g].FadingOut()) c++; }
      if (a > max_grains) max_grains = a; if (c > max_counted) max_counted = c; }
    if (hist_on) for (int i = 0; i < blk; i++) { in_hist.push_back(in[i]); wet_hist.push_back(wet[i]); }
    if (v.recording_ && v.rec_idx_ > v.cap_[v.rec_slot_]) rec_overrun++;
    if (v.recording_ && Vestige::PoolOf(v.rec_slot_) == Vestige::kPoolFreeze && v.rec_idx_ > frz_rec_max)
      frz_rec_max = v.rec_idx_;
    for (int i = 0; i < blk; i++) {
      float a = fabsf(wet[i]);
      if (!std::isfinite(wet[i]) || a > 10.f) bad++;
      if (a > peak) peak = a;
      float d = fabsf(wet[i] - prevw); prevw = wet[i]; if (d > maxd) maxd = d;
      if (a > maxabs) maxabs = a;
    }
    n += blk;
  }
}
// Back to shell-rate blocks after a per-sample section: step single samples
// until n is on a 480-sample tick boundary again (Tick fires on n % 480 == 0).
static void Realign() { blk = 1; while (n % 480 != 0) RunFor(1.f / sr); blk = 48; }
static void NoErrors();
static void Tap()  { fs2_down_ticks = 10; RunFor(0.2f); }   // 100 ms press
static void Hold() { fs2_down_ticks = 90; RunFor(1.0f); }   // 900 ms press

static int fails = 0;
static void Check(bool ok, const char* what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

// Pool helpers over the module's own ranges.
static int LiveIn(int p) {
  int c = 0;
  for (int s = Vestige::PoolLo(p); s < Vestige::PoolHi(p); s++) if (v.active_[s] && !v.dying_[s]) c++;
  return c;
}
static int Live() { return LiveIn(Vestige::kPoolLoop); }
static bool HasIn(int p) { return v.PoolHasContent(p); }
static int GrainsIn(int p) {
  int c = 0;
  for (int g = 0; g < VESTIGE_GRAINS; g++)
    if (v.grains_[g].IsActive() && Vestige::PoolOf(v.grain_slot_[g]) == p) c++;
  return c;
}
// FNV-1a over a float range's bytes.
static uint64_t Hash(const float* p, size_t count) {
  const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < count * sizeof(float); i++) { h ^= b[i]; h *= 1099511628211ULL; }
  return h;
}
static uint64_t HashLoopSide()   { return Hash(&vestige_slab[0][0], sizeof(vestige_slab) / sizeof(float)); }
static uint64_t HashFreezeSide() { return Hash(&vestige_freeze_slab[0][0], sizeof(vestige_freeze_slab) / sizeof(float)); }
static bool AllZero(const float* p, size_t count) { for (size_t i = 0; i < count; i++) if (p[i] != 0.f) return false; return true; }
// Zero crossings per second over a slot's captured loop — identifies which
// steady tone a slot was recorded from.
static float SlotHz(int s) {
  const float* m = v.slab_[s]; const size_t L = v.loop_len_[s];
  int zc = 0;
  for (size_t i = 1; i < L; i++) if ((m[i - 1] < 0.f) != (m[i] < 0.f)) zc++;
  return (L > 1) ? 0.5f * (float)zc * sr / (float)L : 0.f;
}
static void Engage()   { if (!v.engaged_) Tap(); }
static void Unhold()   { if (v.held_) Hold(); }

// ---------------------------------------------------------------------------
static void TestMemoryLayout() {
  printf("-- buffers: distinct memory\n");
  const uintptr_t loop_lo = (uintptr_t)&vestige_slab[0][0];
  const uintptr_t loop_hi = loop_lo + sizeof(vestige_slab);
  const uintptr_t frz_lo  = (uintptr_t)&vestige_freeze_slab[0][0];
  const uintptr_t frz_hi  = frz_lo + sizeof(vestige_freeze_slab);
  Check(loop_hi <= frz_lo || frz_hi <= loop_lo, "loop slab and freeze slab do not overlap");
  bool rows_ok = true, len_ok = true, disjoint = true, side_ok = true;
  for (int s = 0; s < VESTIGE_SLOTS; s++) {
    const uintptr_t a = (uintptr_t)v.slab_[s], b = a + v.cap_[s] * sizeof(float);
    const bool frz = (s >= VESTIGE_FREEZE_SLOT0);
    if (frz ? (a < frz_lo || b > frz_hi) : (a < loop_lo || b > loop_hi)) side_ok = false;
    if (v.cap_[s] != (frz ? VESTIGE_FREEZE_CAP : VESTIGE_VOICE_CAP)) rows_ok = false;
    if (v.ring_[s].GetLength() != v.cap_[s]) len_ok = false;
    for (int t = 0; t < VESTIGE_SLOTS; t++) {
      if (t == s) continue;
      const uintptr_t c = (uintptr_t)v.slab_[t], d = c + v.cap_[t] * sizeof(float);
      if (a < d && c < b) disjoint = false;
    }
  }
  Check(side_ok, "every slot's row lies inside its own side's slab");
  Check(rows_ok, "row capacity: loop = 8 s + guard, freeze = 400 ms + overhang");
  Check(len_ok,  "every RingBuffer is exactly its row's length (grain reads wrap inside the row)");
  Check(disjoint, "no two slots share memory");
  Check(VESTIGE_FREEZE_CAP >= VESTIGE_FREEZE_SAMPLES + VESTIGE_SEAM_XFADE_MAX,
        "freeze row holds the 400 ms window + the seam overhang");
}

// ---------------------------------------------------------------------------
static void TestStage0() {
  printf("-- stage 0: transport, K2, SW1\n");
  RunFor(1.0f);
  Check(!v.engaged_ && v.Bypassed() && !v.HasContent(), "power-up: off, bypassed, empty");
  Check(peak == 0.f, "power-up: wet silent");
  Check(!v.OwnsOutput(), "OwnsOutput() == false");

  Tap(); RunFor(2.0f);
  Check(v.engaged_ && !v.held_ && !v.Bypassed(), "tap: playing");
  Check(v.HasContent() && peak > 0.01f, "playing: auto-captured a loop, wet audible");
  Check(!led1_rises.empty() && led1_rises.back() > n - (long)(2.0f * sr), "playing: LED1 flashing (clock)");

  Hold();
  Check(v.engaged_ && v.held_, "hold: held-playing");
  play_input = false; RunFor(1.0f);
  Check(!v.recording_ && peak > 0.01f, "held-playing: loop sustains, no capture");

  Tap(); RunFor(1.5f);
  Check(!v.engaged_ && v.held_ && v.Bypassed(), "tap while held: held-stopped");
  Check(v.HasContent(), "held-stopped: buffer preserved after fade-out");
  RunFor(0.5f);
  Check(peak < 1e-4f, "held-stopped: wet silent after fade");
  Check(led1.v == 0.f, "held-stopped: LED1 off");

  Tap(); RunFor(1.5f);
  Check(v.engaged_ && v.held_ && peak > 0.01f, "tap again: held-playing, old loop returns with no input");

  Hold();
  Check(v.engaged_ && !v.held_, "hold again: playing (unheld)");
  Tap(); RunFor(1.5f);
  Check(!v.engaged_ && !v.held_ && !v.HasContent(), "tap while unheld: stopped + buffers cleared");

  // Un-hold while stopped clears too.
  Tap(); play_input = true; RunFor(2.0f); Hold(); play_input = false; Tap(); RunFor(1.0f);
  Check(!v.engaged_ && v.held_ && v.HasContent(), "setup: held-stopped with content");
  Hold(); RunFor(0.3f);
  Check(!v.engaged_ && !v.held_ && !v.HasContent(), "release hold while stopped: cleared");

  // K2 noon = shortest ceiling; CCW = reverse.
  cs.knob[1] = 0.5f; RunFor(0.05f);
  Check(v.period_ == VESTIGE_T_MIN_SAMPLES && v.max_loop_len_ == VESTIGE_T_MIN_SAMPLES && !v.rev_play_,
        "K2 noon: T = T_MIN (100 ms), forward");
  cs.knob[1] = 0.0f; RunFor(0.05f);
  Check(v.max_loop_len_ == VESTIGE_LOOP_MAX_SAMPLES && v.rev_play_, "K2 CCW: longest, reverse");
  cs.knob[1] = 1.0f; RunFor(0.05f);
  Check(v.max_loop_len_ == VESTIGE_LOOP_MAX_SAMPLES && !v.rev_play_, "K2 CW: longest, forward");
  cs.knob[1] = 0.15f;  // reverse, shorter
  Tap(); play_input = true; RunFor(3.0f); play_input = false; RunFor(1.0f);
  Check(v.rev_play_ && v.HasContent() && peak > 0.01f, "reverse: loop plays (audible)");

  // MIDDLE = VESTIGE_MAX_VOICES voices.
  cs.sw[0] = 1; cs.knob[1] = 0.62f; play_input = true; RunFor(8.0f);
  Check(v.target_voices_ == VESTIGE_MAX_VOICES && Live() > 1, "MIDDLE: poly (>1 live voice)");
  printf("      live voices in MIDDLE: %d\n", Live());
  cs.sw[0] = 0; RunFor(2.0f);
  Check(Live() == 1, "back to UP: 1 live voice");

  // Freeze.
  cs.sw[0] = 2; cs.knob[1] = 0.0f; cs.knob[2] = 0.0f; RunFor(3.0f);
  size_t maxL = 0;
  for (int s = VESTIGE_FREEZE_SLOT0; s < VESTIGE_FREEZE_SLOT0 + VESTIGE_FREEZE_SLABS; s++)
    if (v.active_[s] && !v.dying_[s] && v.loop_len_[s] > maxL) maxL = v.loop_len_[s];
  Check(maxL <= VESTIGE_FREEZE_SAMPLES && maxL > 0, "freeze: captures <= 400 ms, in the freeze pool");
  Check(v.mb_nbands_ == 3 && v.k3_frozen_ && !v.rev_play_ && v.k3_focus_ == 1.f, "freeze: 3 bands, frozen, forward, K2 inert");
  float kf = v.k3_focus_; cs.knob[2] = 1.0f; cs.knob[1] = 1.0f; RunFor(0.05f);
  NoErrors();                                     // that K3 move is not a timing edit
  Check(v.k3_focus_ == kf && v.max_loop_len_ == VESTIGE_FREEZE_SAMPLES, "freeze: K2/K3 inert");
  play_input = false; RunFor(1.0f);
  Check(peak > 0.005f, "freeze: sustains after input stops");
  printf("      freeze loop len %zu, peak %.4f\n", maxL, peak);

  // Forward vs reverse on the SAME held loop: max sample-to-sample jump.
  cs.sw[0] = 0; cs.knob[1] = 0.8f; RunFor(0.1f);
  Unhold(); Engage();
  play_input = true; RunFor(3.0f); Hold(); play_input = false; RunFor(2.0f);
  maxd = 0.f; RunFor(4.0f); float dfwd = maxd, pfwd = peak;
  cs.knob[1] = 0.2f; RunFor(1.0f); maxd = 0.f; RunFor(4.0f); float drev = maxd, prev = peak;
  printf("      fwd: peak %.4f maxstep %.5f | rev: peak %.4f maxstep %.5f\n", pfwd, dfwd, prev, drev);
  Check(v.rev_play_ && prev > 0.01f, "reverse on a held loop: audible");
  Check(drev < dfwd * 2.f + 1e-3f, "reverse: no step much larger than forward's (seam)");
}

// ---------------------------------------------------------------------------
// Start each buffer scenario from a known state: effect on, not held, both
// sides empty, UP, sensible knobs.
// No error levels: K3 (the error amount, all three layers) back to CCW. A test
// that moves K3 for another reason (e.g. the stage-0 "K3 inert in freeze"
// check) would otherwise leave the errors on.
// K4 is the degradation colour; capture sensitivity is a constant. Tests that
// used K4 as the sensitivity set the threshold the old K4 map gave, and keep K4
// at noon (colour clean).
static void SetK4Thresh(float k4) {
  v.thresh_cfg_ = Mapf(RemapKnob(k4), VESTIGE_AUTO_THRESH_MIN, VESTIGE_AUTO_THRESH_MAX);
  cs.knob[3] = 0.5f;
}
// K5 is bipolar (CCW decay · noon endless, shortest fade · CW fade). The raw
// knob for a CW-half fade of u x the maximum (u in 0..1).
static float K5Fade(float u) {
  const float r = 0.5f + VESTIGE_K5_DEADZONE + u * (0.5f - VESTIGE_K5_DEADZONE);
  return r * (KNOB_MAX - KNOB_MIN) + KNOB_MIN;
}
static void NoErrors() {
  for (int i = 0; i < 3; i++) v.err_level_[i] = 0.f;
  cs.knob[2] = 0.f;
}
static void Reset() {
  NoErrors();
  play_input = false;
  cs.knob[1] = 0.85f; SetK4Thresh(0.1f); cs.knob[4] = K5Fade(0.05f);
  cs.sw[0] = 0; RunFor(0.1f);
  Unhold();
  if (v.engaged_) Tap();
  RunFor(1.0f);                          // fades finish -> off + unheld clears everything
  Engage();
}

static void TestBufferSeparation() {
  printf("-- buffers: no content crosses an SW1 side switch\n");

  // 1. Loop side never touches the freeze slab (hashed: stage 0 above already
  //    used freeze, so it is no longer all-zero).
  Reset();
  uint64_t hf = HashFreezeSide();
  play_input = true; RunFor(3.0f); play_input = false; RunFor(1.0f);
  Check(HasIn(Vestige::kPoolLoop) && !HasIn(Vestige::kPoolFreeze), "UP: capture lands in the loop pool only");
  Check(HashFreezeSide() == hf, "UP capture + playback: freeze slab byte-identical");

  // 2. Unheld switch UP -> DOWN with no input: the loop fades out and is
  //    cleared; nothing appears in freeze; the wet goes silent.
  cs.sw[0] = 2; RunFor(0.05f);
  Check(v.pool_ == Vestige::kPoolFreeze, "SW1 DOWN: freeze side active");
  Check(v.eng_[Vestige::kPoolLoop].frozen == false && v.eng_[Vestige::kPoolFreeze].frozen == true,
        "loop tail keeps loop addressing while the freeze side is active");
  RunFor(1.0f);
  Check(!HasIn(Vestige::kPoolLoop), "unheld: loop side cleared after its fade-out");
  Check(!HasIn(Vestige::kPoolFreeze), "freeze side did not inherit the loop");
  RunFor(0.5f);
  Check(peak < 1e-5f, "freeze side with no capture: wet silent");

  // 3. Freeze side never touches the loop slab.
  uint64_t hl = HashLoopSide();
  hf = HashFreezeSide();
  play_input = true; RunFor(3.0f); play_input = false; RunFor(0.5f);
  Check(HasIn(Vestige::kPoolFreeze) && !HasIn(Vestige::kPoolLoop), "DOWN: capture lands in the freeze pool only");
  Check(HashLoopSide() == hl, "DOWN capture + playback: loop slab byte-identical");
  Check(HashFreezeSide() != hf, "DOWN capture: the freeze slab was written (sanity)");
  Check(peak > 0.005f, "DOWN: freeze audible");

  // 4. Unheld switch DOWN -> UP: freeze cleared, loop side starts empty, silent.
  cs.sw[0] = 0; RunFor(1.0f);
  Check(!HasIn(Vestige::kPoolFreeze), "unheld: freeze side cleared after its fade-out");
  Check(!HasIn(Vestige::kPoolLoop), "loop side did not inherit the freeze");
  RunFor(0.5f);
  Check(peak < 1e-5f, "loop side with no capture: wet silent");

  // 5. Held switch UP -> DOWN -> UP: the loop is kept untouched, silent and
  //    unscheduled while away, and comes back.
  play_input = true; RunFor(3.0f); play_input = false; RunFor(1.0f);
  Hold();
  Check(v.held_ && HasIn(Vestige::kPoolLoop), "setup: held loop");
  int ls = -1;
  for (int s = 0; s < VESTIGE_VOICE_SLABS; s++) if (v.active_[s] && !v.dying_[s]) ls = s;
  const size_t L0 = (ls >= 0) ? v.loop_len_[ls] : 0;
  const uint64_t hrow = (ls >= 0) ? Hash(v.slab_[ls], v.cap_[ls]) : 0;
  hf = HashFreezeSide();
  cs.sw[0] = 2; play_input = true; RunFor(2.0f);   // input playing: held = no capture
  Check(!v.recording_ && !HasIn(Vestige::kPoolFreeze), "held on the freeze side: no capture, freeze empty");
  Check(HashFreezeSide() == hf, "held on the freeze side: freeze slab untouched");
  Check(HasIn(Vestige::kPoolLoop) && ls >= 0 && v.active_[ls] && v.loop_len_[ls] == L0,
        "held: loop kept while on the freeze side");
  Check(GrainsIn(Vestige::kPoolLoop) == 0, "held loop is parked: no grains scheduled while away");
  play_input = false; RunFor(0.5f);
  Check(peak < 1e-5f, "held, freeze side empty: wet silent (the loop does not play here)");
  cs.sw[0] = 0; RunFor(1.0f);
  Check(ls >= 0 && v.active_[ls] && v.loop_len_[ls] == L0 && Hash(v.slab_[ls], v.cap_[ls]) == hrow,
        "back on UP: the same loop, byte-identical");
  RunFor(0.5f);
  Check(peak > 0.01f, "back on UP: held loop fades back in");

  // 6. Held freeze is kept too, and the loop side does not hear it.
  Unhold(); cs.sw[0] = 2; RunFor(1.0f);           // unheld: loop cleared on leaving
  play_input = true; RunFor(2.0f); play_input = false; RunFor(0.3f);
  Hold();
  Check(v.held_ && HasIn(Vestige::kPoolFreeze) && !HasIn(Vestige::kPoolLoop), "setup: held freeze, empty loop side");
  cs.sw[0] = 0; RunFor(1.0f);                     // freeze fades out (K5 ~0.3 s)
  Check(HasIn(Vestige::kPoolFreeze) && !HasIn(Vestige::kPoolLoop), "held: freeze kept on the loop side, loop side still empty");
  RunFor(0.5f);
  Check(peak < 1e-5f, "loop side (held, empty): wet silent");
  cs.sw[0] = 2; RunFor(1.0f);
  Check(peak > 0.005f, "back on DOWN: held freeze fades back in");

  // 7. A capture in flight when SW1 changes side is dropped, not committed
  //    into the other side.
  Unhold(); cs.sw[0] = 0; RunFor(1.0f);
  Reset();
  cs.knob[1] = 1.0f;                               // 8 s ceiling: the capture runs long
  // Tone A (110 Hz) is played into the loop side; the instant SW1 moves the
  // tone changes to B (330 Hz). Everything the freeze side holds afterwards
  // must be B: an A slot there means the loop capture crossed over.
  sustain_hz = 110.f; sustain_input = true; play_input = true; RunFor(0.3f);
  Check(v.recording_ && Vestige::PoolOf(v.rec_slot_) == Vestige::kPoolLoop, "setup: loop capture in flight (into a loop-side slot)");
  cs.sw[0] = 2; sustain_hz = 330.f; RunFor(0.02f);
  Check(!HasIn(Vestige::kPoolLoop), "in-flight loop capture dropped (never committed)");
  Check(!v.recording_ || Vestige::PoolOf(v.rec_slot_) == Vestige::kPoolFreeze, "any new capture records into a freeze-side slot");
  RunFor(0.45f);                                   // B capture hits the 400 ms ceiling
  int nb = 0, na = 0;
  for (int s = VESTIGE_FREEZE_SLOT0; s < VESTIGE_FREEZE_SLOT0 + VESTIGE_FREEZE_SLABS; s++) {
    if (!v.active_[s]) continue;                  // incl. dying: a crossed-over slot would be fading here
    const float hz = SlotHz(s);
    printf("      freeze slot %d: %zu samples, %.1f Hz%s\n", s, v.loop_len_[s], hz, v.dying_[s] ? " (dying)" : "");
    if (fabsf(hz - 330.f) < 15.f) nb++; else na++;
  }
  Check(nb >= 1 && na == 0, "freeze side holds only audio played AFTER the switch (330 Hz, no 110 Hz)");
  RunFor(0.6f);
  sustain_input = false; play_input = false; sustain_hz = 110.f; RunFor(0.3f);
  bool frz_only = true;
  for (int s = 0; s < VESTIGE_FREEZE_SLOT0; s++) if (v.active_[s]) frz_only = false;
  Check(frz_only && HasIn(Vestige::kPoolFreeze), "after the switch only the freeze side holds content");

  // 8. A freeze capture hitting the 400 ms ceiling never writes past the
  //    scratch row. The ceiling stop and the commit are acted on by Controls()
  //    up to a tick (480 samples) late and the row only has 240 spare, so the
  //    write bound is what holds it. Sustained input so the capture runs to the
  //    ceiling instead of ending on silence.
  Unhold(); cs.sw[0] = 2; play_input = false; RunFor(1.0f);
  frz_rec_max = 0;
  sustain_input = true; play_input = true; RunFor(1.5f);
  play_input = false; sustain_input = false; RunFor(0.5f);
  printf("      freeze capture: furthest index %zu, row capacity %zu, ceiling %zu\n",
         frz_rec_max, (size_t)VESTIGE_FREEZE_CAP, (size_t)VESTIGE_FREEZE_SAMPLES);
  size_t fl = 0;
  for (int s = VESTIGE_FREEZE_SLOT0; s < VESTIGE_FREEZE_SLOT0 + VESTIGE_FREEZE_SLABS; s++)
    if (v.active_[s] && !v.dying_[s]) fl = v.loop_len_[s];
  Check(frz_rec_max >= VESTIGE_FREEZE_SAMPLES, "sustained freeze capture reached the 400 ms ceiling");
  Check(fl == VESTIGE_FREEZE_SAMPLES, "ceiling capture committed at exactly 400 ms");
  Check(frz_rec_max <= VESTIGE_FREEZE_CAP, "freeze capture never indexed past its row");
  Check(rec_overrun == 0, "no capture ever indexed past its scratch row (whole run)");
}

// ---------------------------------------------------------------------------
// Stage 1: T, the master period.
static void PressFS1(int ticks) { fs_down_ticks[0] = ticks; }
// A chain of FS1 taps: each press lasts 50 ms, successive DOWN-presses `ivs`
// ms apart (multiples of the 10 ms tick, so the intervals are exact).
// Tap tempo needs two agreeing intervals (VESTIGE_TAP_AGREE), so the last
// interval is repeated once: Taps({500}) is three taps 500 ms apart.
static void Taps(const std::vector<int>& ivs) {
  for (int iv : ivs) { PressFS1(5); RunFor((float)iv * 0.001f); }
  if (!ivs.empty()) { PressFS1(5); RunFor((float)ivs.back() * 0.001f); }
  PressFS1(5); RunFor(0.1f);                        // the closing tap
}
static size_t TapT(int ms) { return (size_t)((float)ms * 0.001f * sr + 0.5f); }

// Every LED1 rise in [from, to) must sit on anchor + k*T, lit at the first tick
// at or after the true flash start (0..480 samples late). Returns the worst
// lateness in samples, or -1 if any rise is off the anchor's beat.
static long LedWorstLate(long anchor, size_t T, long from, long to, int* count) {
  long worst = 0; *count = 0;
  for (long r : led1_rises) {
    if (r < from || r >= to) continue;
    const long ph = (r - anchor) % (long)T;
    (*count)++;
    if (ph > 480) return -1;
    if (ph > worst) worst = ph;
  }
  return worst;
}

static void TestTimeBase() {
  printf("-- stage 1: the time base (T)\n");
  Reset();
  Check(v.engaged_ && !v.held_ && !v.HasContent(), "setup: on, unheld, empty, UP");
  cs.knob[1] = 0.85f; RunFor(0.05f);
  const size_t knobT = Vestige::KnobPeriod(RemapKnob(0.85f));

  // Range.
  Check(VESTIGE_T_MIN_SAMPLES / 8 >= VESTIGE_GRAIN_MIN_LEN, "T_MIN/8 >= VESTIGE_GRAIN_MIN_LEN (quantiser floor stays playable)");
  printf("      T range %zu..%zu samples (%.1f ms..%.1f s), T_MIN/8 = %zu\n",
         (size_t)VESTIGE_T_MIN_SAMPLES, (size_t)VESTIGE_T_MAX_SAMPLES,
         VESTIGE_T_MIN_SAMPLES * 1000.f / sr, VESTIGE_T_MAX_SAMPLES / sr, (size_t)VESTIGE_T_MIN_SAMPLES / 8);
  bool mono = true; size_t prevT = 0, tmin = (size_t)-1, tmax = 0;
  for (int i = 0; i <= 200; i++) {
    const size_t t = Vestige::KnobPeriod((float)i / 200.f);
    if (t < tmin) tmin = t; if (t > tmax) tmax = t;
    if (i > 100 && t < prevT) mono = false;
    prevT = t;
  }
  Check(tmin == VESTIGE_T_MIN_SAMPLES && tmax == VESTIGE_T_MAX_SAMPLES && mono,
        "K2 sweep: T spans exactly T_MIN..T_MAX, monotonic away from noon");
  Check(v.period_ == knobT && v.max_loop_len_ == knobT, "no tap: K2 sets T, and T is the loop ceiling");

  // Tap intervals produce T.
  struct TapCase { int ms; bool accept; };
  const TapCase cases[] = { {500, true}, {250, true}, {1230, true}, {100, true}, {7990, true},
                            {90, false}, {8010, false} };
  size_t expect = v.period_;
  for (const TapCase& c : cases) {
    // Break the chain first: an interval longer than T_MAX is ignored and the
    // press that ends it starts a new chain.
    RunFor(8.2f);
    Taps({c.ms});
    if (c.accept) expect = TapT(c.ms);
    char msg[128];
    snprintf(msg, sizeof msg, "tap %d ms -> T %s (%zu samples)", c.ms, c.accept ? "set" : "ignored, unchanged", expect);
    Check(v.period_ == expect, msg);
  }
  RunFor(8.2f);
  // Multi-tap chain: the LAST interval is T (no averaging, like sprawl).
  Taps({600, 610, 450});
  Check(v.period_ == TapT(450), "tap chain 600/610/450(/450) ms -> T = the last two agreeing intervals (450 ms)");
  // A stray press after a pause must not re-time anything: its interval agrees
  // with nothing. Then two agreeing intervals at a new tempo set T.
  { RunFor(3.0f); PressFS1(5); RunFor(0.8f);                      // stray press, ~3 s after the chain
    const bool kept1 = (v.period_ == TapT(450));
    PressFS1(5); RunFor(0.8f);                                   // first 800 ms interval: vs ~3 s, disagree
    const bool kept2 = (v.period_ == TapT(450));
    PressFS1(5); RunFor(0.1f);                                   // second 800 ms interval: agree
    Check(kept1 && kept2 && v.period_ == TapT(800),
          "stray tap after a pause changes nothing; two agreeing intervals then set T"); }
  // A long press is not a tap: T unchanged, chain untouched.
  const size_t before = v.period_;
  PressFS1(40); RunFor(0.8f);                         // 400 ms press
  Check(v.period_ == before, "400 ms FS1 press: not a tap, T unchanged");
  // T drives the loop ceiling: a sustained note captures exactly T.
  Taps({500});
  Check(v.period_ == TapT(500) && v.max_loop_len_ == TapT(500), "tapped T is the loop ceiling");
  sustain_hz = 110.f; sustain_input = true; play_input = true; RunFor(1.0f);
  sustain_input = false; play_input = false; RunFor(0.5f);
  size_t ll = 0;
  for (int s = 0; s < VESTIGE_VOICE_SLABS; s++) if (v.active_[s] && !v.dying_[s]) ll = v.loop_len_[s];
  Check(ll == TapT(500), "sustained note under a 500 ms tap: loop = exactly T (24000)");

  // K2 arbitration.
  cs.knob[1] = 0.85f + 0.012f; RunFor(0.05f);         // ADC-scale jitter
  Check(v.period_ == TapT(500), "K2 jitter below epsilon: tap kept");
  cs.knob[1] = 0.85f + 0.05f; RunFor(0.05f);
  Check(v.tap_period_ == 0 && v.period_ == Vestige::KnobPeriod(RemapKnob(0.90f)), "K2 moved past epsilon: tap cancelled, knob sets T");
  cs.knob[1] = 0.20f; RunFor(0.05f);                  // CCW: reverse
  Taps({400});
  Check(v.period_ == TapT(400) && v.rev_play_, "tap overrides T only: K2 CCW still sets reverse");
  cs.knob[1] = 0.85f; RunFor(0.05f);
  Check(v.tap_period_ == 0 && !v.rev_play_, "K2 back CW: tap cancelled, forward");

  // Tap on the freeze side: T is global, the freeze window is not.
  cs.sw[0] = 2; RunFor(0.8f);
  Taps({700});
  Check(v.period_ == TapT(700) && v.max_loop_len_ == VESTIGE_FREEZE_SAMPLES, "freeze side: tap sets T, capture window stays 400 ms");
  cs.sw[0] = 0; RunFor(0.8f);
  Check(v.max_loop_len_ == TapT(700), "back on the loop side: the tapped T is the ceiling");

  // LED1 = the clock, anchored to the most recent capture start.
  Reset();
  Taps({1000});                                      // T = 1 s, buffers empty
  const size_t T = v.period_;
  Check(T == TapT(1000), "setup: T = 1000 ms");
  // (a) no capture yet: the flash runs from the tap's down-press.
  long a0 = (long)v.led_anchor_[Vestige::kPoolLoop];
  long from = n;
  RunFor(3.2f);
  int cnt = 0; long late = LedWorstLate(a0, T, from, n, &cnt);
  Check(cnt >= 3 && late >= 0, "no capture: LED1 flashes once per T from the last tap");
  // (b) a capture start re-anchors: burst of tone at an arbitrary offset.
  RunFor(0.37f);                                     // 0.37 T off the tap beat
  sustain_input = true; play_input = true; RunFor(0.02f);
  Check(v.recording_, "setup: capture started");
  const long a1 = (long)v.led_anchor_[Vestige::kPoolLoop];
  Check(labs(a1 - a0) % (long)T > 4800 && labs(a1 - a0) % (long)T < (long)T - 4800,
        "setup: the capture start is well off the old beat");
  RunFor(0.28f); sustain_input = false; play_input = false;
  from = a1;
  RunFor(4.0f);
  late = LedWorstLate(a1, T, from, n, &cnt);
  printf("      capture-anchored flashes: %d, worst lateness %ld samples (tick = 480)\n", cnt, late);
  Check(cnt >= 4 && late >= 0, "LED1 re-anchored to the capture start: every flash on start + k*T");
  int stale = 0;
  long off_old = LedWorstLate(a0, T, from, n, &stale);
  Check(off_old < 0, "no flash left on the old (tap) beat — not free-running");
  // (c) a second capture re-anchors again, per capture.
  RunFor(0.61f);
  sustain_input = true; play_input = true; RunFor(0.02f);
  const long a2 = (long)v.led_anchor_[Vestige::kPoolLoop];
  RunFor(0.2f); sustain_input = false; play_input = false; RunFor(3.0f);
  late = LedWorstLate(a2, T, a2, n, &cnt);
  Check(a2 != a1 && cnt >= 2 && late >= 0, "next capture start re-anchors LED1 again");
  // (d) a tap while a loop exists changes the period but not the anchor. (All
  // three SW2 modes now make a playing loop follow T, so where LED1 then
  // flashes — the loop's actual "one" — is checked per mode in
  // TestFollowTape / TestFollowStretch / TestFollowRecut.)
  Taps({700});
  Check(v.period_ == TapT(700) && (long)v.led_anchor_[Vestige::kPoolLoop] == a2,
        "tap with a loop playing: new T, anchor stays on the capture start");
  RunFor(1.0f);
  // (e) off = LED1 dark.
  Tap(); RunFor(1.5f);
  Check(!v.engaged_, "setup: effect off");
  Check(led1_on_while_off == 0, "LED1 never lit while the effect is off (whole run)");
  Tap(); RunFor(0.2f);

  // A held loop parked on the other side keeps running (stays on its beat).
  sustain_input = true; play_input = true; RunFor(0.3f); sustain_input = false; play_input = false; RunFor(1.0f);
  Hold();
  int ls = -1;
  for (int s = 0; s < VESTIGE_VOICE_SLABS; s++) if (v.active_[s] && !v.dying_[s]) ls = s;
  Check(ls >= 0, "setup: held loop");
  if (ls >= 0) {
    const float L = (float)v.loop_len_[ls];
    const float f0 = v.fwd_[ls]; const long c0 = n;
    cs.sw[0] = 2; RunFor(2.13f); cs.sw[0] = 0; RunFor(0.01f);
    const float expect_f = fmodf(f0 + (float)(n - c0), L);
    float d = fabsf(v.fwd_[ls] - expect_f); if (d > L * 0.5f) d = L - d;
    printf("      parked head: drift %.1f samples over %.2f s away\n", d, (n - c0) / sr);
    Check(d < 2.f, "held loop's head kept running while parked on the freeze side");
  }
  Unhold();
}

// ---------------------------------------------------------------------------
// Stage 2: quantised capture, sample-accurate.
static float InH(long k)  { long i = k - hist_n0; return (i >= 0 && i < (long)in_hist.size())  ? in_hist[i]  : 0.f; }
static float WetH(long k) { long i = k - hist_n0; return (i >= 0 && i < (long)wet_hist.size()) ? wet_hist[i] : 0.f; }
// Lag (-maxlag..maxlag) at which wet[at + lag + t] best matches ref(t), t < N.
// ref(t) = input sample the loop should be playing at `at + t` (before the
// wet path's latency). Returns the lag and the normalised correlation.
template <class F> static int BestLag(long at, int N, int maxlag, F ref, float* corr_out) {
  int best = -1; double bc = -2.0;
  for (int d = -maxlag; d <= maxlag; d++) {        // both signs: early would show too
    double xy = 0, xx = 0, yy = 0;
    for (int t = 0; t < N; t++) { const double a = WetH(at + d + t), b = ref(t); xy += a * b; xx += a * a; yy += b * b; }
    const double c = (xx > 0 && yy > 0) ? xy / sqrt(xx * yy) : 0.0;
    if (c > bc) { bc = c; best = d; }
  }
  *corr_out = (float)bc; return best;
}

struct CapRec { int s; long A; size_t Q, raw, T; long act; uint32_t phase; long dec; };
static uint32_t seen_acts = 0;
// Wait (up to `secs`) for the next activation; fills a record from the module.
static bool WaitActivation(float secs, CapRec* r) {
  const long end = n + (long)(secs * sr);
  while (n < end) {
    RunFor(0.01f);
    if (v.act_count_ != seen_acts) {
      seen_acts = v.act_count_;
      const int s = v.last_act_slot_;
      *r = CapRec{s, (long)v.cap_start_[s], v.cap_len_[s], v.cap_raw_[s], v.cap_T_[s],
                  (long)v.last_act_at_, v.act_phase_[s], (long)v.cap_decide_at_[s]};
      return true;
    }
  }
  return false;
}
// Where the replicated gate opens: the module's own envelope follower run
// over the recorded input, from the module's envelope state at hist start.
static float env_at_hist0 = 0.f;
static long GateOpen(long from) {
  float env = env_at_hist0;
  for (long k = hist_n0; k < hist_n0 + (long)in_hist.size(); k++) {
    env += VESTIGE_ENV_COEF * (fabsf(InH(k)) - env);
    if (k >= from && env > v.auto_thresh_) return k;
  }
  return -1;
}

static long worst_start_err = 0, worst_play_err = 0, worst_period_err = 0, content_bad = 0;
static int  wet_latency = -1;

// One capture: a noise burst of `burst` samples starting now. Returns its record.
static bool OneCapture(long burst, CapRec* r, const char* name, bool expect_rev = false) {
  const long b0 = n; noise_from = b0; noise_to = b0 + burst;
  if (!WaitActivation(4.0f + burst / sr, r)) { Check(false, name); return false; }
  const long A_rep = GateOpen(b0);
  const long start_err = labs(r->A - A_rep);
  if (start_err > worst_start_err) worst_start_err = start_err;
  // Recorded content: the loop body is exactly input[A .. A+Q), bit for bit.
  long bad = 0;
  for (size_t j = 0; j < r->Q; j++) if (v.slab_[r->s][j] != InH(r->A + (long)j)) bad++;
  content_bad += bad;
  // Playback on its own grid, measured at the output. The loop sample that
  // should sound at act + t: forward in[A + (p+t) mod Q], reverse the mirror.
  const long at = r->act;
  const size_t Q = r->Q; const uint32_t p = r->phase;
  auto fwd_ref = [&](int t) { return InH(r->A + (long)((p + (size_t)t) % Q)); };
  auto rev_ref = [&](int t) { return InH(r->A + (long)(Q - 1 - ((p + (size_t)t) % Q))); };
  RunFor(0.3f + (float)(2 * Q) / sr);             // let it play two passes
  float c1 = 0.f, c2 = 0.f;
  const int N = 2400;
  const int d1 = expect_rev ? BestLag(at, N, 600, rev_ref, &c1) : BestLag(at, N, 600, fwd_ref, &c1);
  // Next full pass, from its "one": loop sample 0 at A + k*Q.
  long one = r->A + (long)((( (size_t)(at - r->A) + Q - 1) / Q) * Q);
  if (one <= at) one += (long)Q;
  auto one_ref_f = [&](int t) { return InH(r->A + (long)((size_t)t % Q)); };
  auto one_ref_r = [&](int t) { return InH(r->A + (long)(Q - 1 - ((size_t)t % Q))); };
  const int d2 = expect_rev ? BestLag(one, N, 600, one_ref_r, &c2) : BestLag(one, N, 600, one_ref_f, &c2);
  const long perr = labs((long)d1 - wet_latency), qerr = labs((long)d2 - (long)d1);
  if (perr > worst_play_err) worst_play_err = perr;
  if (qerr > worst_period_err) worst_period_err = qerr;
  const char* dir = (Q > r->raw) ? "up" : (Q < r->raw) ? "DOWN (truncated)" : "exact";
  printf("      %-19s gate%+ld  raw %6zu -> Q %6zu (%.4f T) %-16s play @A+%-6ld phase %-5u lag %d (c=%.3f)  next-one lag %d (c=%.3f)  body mismatches %ld\n",
         name, r->A - A_rep, r->raw, Q, GridQuantize::DivisionOf(Q, r->T), dir, at - r->A, p, d1, c1, d2, c2, bad);
  return c1 > 0.9f && c2 > 0.9f;
}

static void TestQuantisedCapture() {
  printf("-- stage 2: quantised capture (sample-accurate)\n");
  Reset();
  cs.sw[0] = 0; SetK4Thresh(0.1f); cs.knob[4] = 0.5f;   // UP, sensitive gate, K5 CCW (3 ms fades)
  Taps({1000}); RunFor(1.0f);
  const size_t T = v.period_;
  Check(T == 48000, "setup: T = 1000 ms (48000)");
  // The wet path has no latency (the idle warble tap is bypassed): the loop
  // sample due at t must sound AT t — lag 0 against the dry.
  wet_latency = 0;
  seen_acts = v.act_count_;
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear(); env_at_hist0 = v.env_;

  struct Case { const char* name; long burst; int want; };   // want: +1 up, -1 down, 0 = Q==T ceiling
  // Raw length ~ burst + ~330 (envelope decay to the close threshold).
  const Case cases[] = {
    {"floor (tiny stab)",   1500, +1},   // raw ~1.8k -> 1/8 = 6000
    {"1/8 down",            6200, -1},   // raw ~6.5k -> 6000 (truncates ~500)
    {"1/2 up (late)",      21800, +1},   // raw ~22.1k -> 24000, decided after the boundary
    {"1/2 down",           25400, -1},   // raw ~25.7k -> 24000 (truncates)
    {"1 up (on time)",     42500, +1},   // raw ~42.8k -> 48000, decided before it
    {"ceiling (T)",        62000,  0},   // runs into T
  };
  bool dirs_ok = true, div_ok = true, all_corr = true, on_time_ok = true, phase_ok = true;
  bool saw_up = false, saw_down = false;
  for (const Case& c : cases) {
    CapRec r{};
    const bool ok = OneCapture(c.burst, &r, c.name);
    all_corr = all_corr && ok;
    const size_t q = GridQuantize::Quantize(r.raw, r.T);
    if (r.T != T || q != r.Q || GridQuantize::IndexOf(r.Q, r.T) < 0) div_ok = false;
    if (c.want > 0 && !(r.Q > r.raw)) dirs_ok = false;
    if (c.want < 0 && !(r.Q < r.raw)) dirs_ok = false;
    if (c.want == 0 && r.Q != T) dirs_ok = false;
    if (r.Q > r.raw) saw_up = true;
    if (r.Q < r.raw) saw_down = true;
    // On its grid: entered at phase (act - A) mod Q; on time = exactly A+Q, phase 0.
    if ((uint32_t)((size_t)(r.act - r.A) % r.Q) != r.phase || r.act < r.A + (long)r.Q) phase_ok = false;
    // "Known before the boundary" = the module actually decided the end at or
    // before A + Q. (The old rule, raw + 80 ms < Q, assumed the gate meter falls
    // instantly; with VESTIGE_GATE_ENV_MODE 1 the decision comes later.)
    const bool decided_early = (r.dec - r.A) <= (long)r.Q;
    if (decided_early && !(r.act == r.A + (long)r.Q && r.phase == 0)) on_time_ok = false;
    if (c.want < 0) {
      // Truncated for real: nothing past Q is readable as loop — the cells a
      // grain can reach behind Q are the crossfaded seam and the head copy.
      const float* m = v.slab_[r.s];
      const size_t xf = Vestige::SeamXfadeLen(r.Q);
      long gbad = 0;
      for (size_t k = xf; k < r.Q + 2 && k < VESTIGE_GUARD_SAMPLES; k++) if (m[r.Q + k] != m[k % r.Q]) gbad++;
      Check(gbad == 0 && v.loop_len_[r.s] == r.Q, "truncated capture: loop = Q, guard behind it = the head, not the cut material");
    }
    RunFor(0.8f);
  }
  Check(div_ok, "every captured loop length is an exact division of the T latched at its start");
  Check(dirs_ok && saw_up && saw_down, "rounding goes both ways (up, down/truncated, floor, ceiling)");
  Check(phase_ok, "every loop enters ON its own grid: phase == (start - A) mod Q");
  Check(on_time_ok, "end known before the boundary: playback starts at exactly A + Q, on its \"one\"");
  Check(all_corr, "output correlates with the expected loop samples (c > 0.9), both passes");
  Check(worst_start_err == 0, "capture start = the sample the gate opens (replicated envelope), 0 samples error");
  Check(content_bad == 0, "loop body bit-identical to input[A .. A+Q): recorded length and start exact");
  printf("      worst: start %ld, heard playback start vs grid (vs dry) %ld, period %ld samples\n",
         worst_start_err, worst_play_err, worst_period_err);
  Check(worst_play_err == 0 && worst_period_err == 0, "measured at the OUTPUT: playback start and period on the grid, 0 samples vs dry");

  // Reverse on time (ceiling), short T: the guard must be ready at A+Q.
  cs.knob[1] = 0.2f; RunFor(0.1f); Taps({150}); RunFor(0.5f);
  Check(v.rev_play_ && v.period_ == 7200, "setup: reverse, T = 150 ms");
  { CapRec r{}; const bool ok = OneCapture(12000, &r, "reverse ceiling", true);
    Check(ok && r.Q == 7200 && r.act == r.A + 7200 && r.phase == 0,
          "reverse: ceiling loop starts at exactly A+Q from its tail (guard ready in time)"); }
  cs.knob[1] = 0.85f; RunFor(0.1f); Taps({1000}); RunFor(1.0f);

  // T latched at the capture start: K2 moved mid-capture (cancels the tap).
  { const long b0 = n; noise_from = b0; noise_to = b0 + 30000;
    RunFor(0.2f);
    cs.knob[1] = 0.62f;                                  // T -> knob value (much shorter)
    CapRec r{}; WaitActivation(3.f, &r);
    printf("      K2 moved mid-capture: live T now %zu, capture T %zu, Q %zu\n", v.period_, r.T, r.Q);
    Check(v.period_ != 48000 && r.T == 48000 && GridQuantize::IndexOf(r.Q, 48000) >= 0,
          "K2 mid-capture: the capture keeps the T latched at its start");
    cs.knob[1] = 0.85f; RunFor(1.0f); Taps({1000}); RunFor(2.0f); }

  // A full poly pool: every voice keeps its own anchor.
  cs.sw[0] = 1; RunFor(0.5f);
  static_assert(VESTIGE_MAX_VOICES <= 6, "test burst tables hold at most 6 voices");
  const int NV = VESTIGE_MAX_VOICES;
  const long bursts[6] = {3000, 9000, 14000, 21000, 27000, 30500};
  const float gaps[6]  = {0.37f, 0.61f, 0.23f, 0.89f, 0.41f, 0.5f};
  int slots[6]; long As[6]; size_t Qs[6]; int got = 0;
  for (int i = 0; i < NV; i++) {
    CapRec r{};
    noise_from = n; noise_to = n + bursts[i];
    if (WaitActivation(3.f, &r)) { slots[got] = r.s; As[got] = r.A; Qs[got] = r.Q; got++; }
    RunFor(gaps[i]);
  }
  RunFor(1.0f);
  int live = 0; bool heads_ok = true, anchors_distinct = true, divs = true; size_t qset[6]; int nq = 0;
  for (int k = 0; k < 4; k++) {                          // sample the heads at a few moments
    RunFor(0.137f);
    for (int i = 0; i < got; i++) {
      const int s = slots[i];
      if (!v.active_[s] || v.dying_[s]) continue;
      const long last = n - 1;                            // the head is on the sample just played
      const float want = (float)((size_t)(last - As[i]) % Qs[i]);
      if (v.fwd_[s] != want) heads_ok = false;
    }
  }
  for (int i = 0; i < got; i++) {
    if (v.active_[slots[i]] && !v.dying_[slots[i]]) live++;
    if (GridQuantize::IndexOf(Qs[i], 48000) < 0) divs = false;
    for (int j = 0; j < i; j++) if (As[j] == As[i]) anchors_distinct = false;
    bool dup = false; for (int j = 0; j < nq; j++) if (qset[j] == Qs[i]) dup = true;
    if (!dup) qset[nq++] = Qs[i];
    printf("      voice slot %d: anchor %ld, Q %zu (%.4f of T)\n", slots[i], As[i], Qs[i], GridQuantize::DivisionOf(Qs[i], 48000));
  }
  Check(got == NV && live == NV, "MIDDLE: a full pool of captures, all live");
  Check(anchors_distinct && nq >= 3 && divs, "full pool: distinct anchors, >= 3 different divisions of one T");
  Check(heads_ok, "every voice's head sits on (now - its own anchor) mod its own Q, at every probe");
  // Freeze: nothing else sounding, one fragment. Its output must begin at its
  // activation sample (the first grain's Hann window is 0 at phase 0, so the
  // first NON-ZERO sample is activation + 1 — a window value, not a delay).
  cs.sw[0] = 2; RunFor(2.0f);                            // loop side fades out and clears
  { CapRec r{}; const long b0 = n; noise_from = b0; noise_to = b0 + 4800;
    WaitActivation(2.f, &r);
    long first = -1;
    for (long k = r.act - 2000; k < r.act + 2000; k++) if (WetH(k) != 0.f) { first = k; break; }
    printf("      freeze: activation at %ld, first non-zero output at %+ld\n", r.act, first - r.act);
    Check(Vestige::PoolOf(r.s) == Vestige::kPoolFreeze && first == r.act + 1,
          "freeze: output starts at its activation sample (0 latency vs dry)"); }
  cs.sw[0] = 0; RunFor(1.0f);
  hist_on = false; in_hist.clear(); wet_hist.clear();
  noise_from = noise_to = -1;
  Unhold();
}

// ---------------------------------------------------------------------------
// Guard-read audit: for every active loop-side grain, the cell(s) it reads next
// (and its interpolation partner when that has weight) must be real loop data:
// inside the body, or a FINISHED guard cell — the seam crossfade once recorded,
// or a head copy the background job has already written.
static void Audit() {
  for (int g = 0; g < VESTIGE_GRAINS; g++) {
    const GrainVoice& gv = v.grains_[g];
    if (!gv.IsActive()) continue;
    const int s = v.grain_slot_[g];
    if (s >= VESTIGE_FREEZE_SLOT0 || s == VESTIGE_FRIP_SLOT) continue;
    // A raw-row grain reads the STORED loop (its capture guard is for the
    // stored length), even if a C view has since changed the play length.
    const size_t L = v.loop_len_[s];
    if (L == 0) continue;
    const float pos = gv.read_pos_f_;
    if (v.grain_view_[g] >= 0) {
      // Read through a C re-cut view: every cell must lie inside the view
      // (body / silence / its built guard). The raw row is not touched.
      const GrainView& vw = v.views_[v.grain_view_[g] / 2][v.grain_view_[g] & 1];
      const size_t idx = (size_t)pos;
      audit_reads++;
      if (idx + 1 >= vw.loop_len + vw.guard_len) audit_bad++;
      const long over = (long)idx - (long)vw.loop_len - (long)((vw.loop_len < VESTIGE_GUARD_SAMPLES) ? vw.loop_len : VESTIGE_GUARD_SAMPLES);
      if (over > audit_max_over) audit_max_over = over;
      continue;
    }
    const size_t idx = (size_t)pos;
    const bool partner = (pos - (float)idx) > 0.f;
    for (int q = 0; q < (partner ? 2 : 1); q++) {
      const size_t c = idx + (size_t)q;
      audit_reads++;
      if (c < L) continue;
      const size_t k = c - L;
      const size_t span = (L < VESTIGE_GUARD_SAMPLES) ? L : VESTIGE_GUARD_SAMPLES;
      if ((long)k - (long)span > audit_max_over) audit_max_over = (long)k - (long)span;
      const size_t xf = Vestige::SeamXfadeLen(L);
      bool ok;
      if (c >= v.cap_[s]) ok = false;
      else if (k < xf) ok = !(v.recording_ && v.rec_slot_ == s) || v.rec_idx_ > c;
      else ok = (v.gfill_base_[s] == L && v.gfill_k_[s] > k);
      if (!ok) audit_bad++;
    }
  }
}

// ---------------------------------------------------------------------------
// Stage 6 (step 4): K1 playback speed crossfade.
// Expected output for the loop in slot s over the next N samples, from the
// module's own clean head / pass at the last processed sample: version rate r
// (1 = clean) in direction dir (+1 / -1).
static std::vector<float> ExpectVersion(int s, float r, int dir, int N) {
  const float L = (float)v.loop_len_[s];
  float c = v.fwd_[s]; int32_t pass = v.pass_[s];
  std::vector<float> out(N);
  const float* m = v.slab_[s];
  for (int j = 0; j < N; j++) {
    c += (float)dir;
    if (c >= L) { c -= L; pass++; }
    if (c < 0.f) { c += L; pass--; }
    float h;
    if (r == 1.f) h = c;
    else if (r < 1.f) h = (c + ((pass & 1) ? L : 0.f)) * 0.5f;
    else { h = c * 2.f; if (h >= L) h -= L; }
    // Just past the loop seam every grain in flight started BEFORE the wrap
    // (hop >> seam), so it reads on into the guard — whose first cells are the
    // designed seam crossfade, not the plain head. Model exactly that; the row
    // holds the guard, so i0 + 1 needs no wrap.
    size_t i0 = (size_t)h; const float f = h - (float)i0;
    if (i0 < Vestige::SeamXfadeLen((size_t)L)) i0 += (size_t)L;
    const size_t i1 = i0 + 1;
    out[j] = m[i0] * (1.f - f) + m[i1] * f;
  }
  return out;
}
// The same, from the GRID alone (no module state): a loop that started playing
// at `act` on its "one" plays, at time t, clean sample (t-act) mod L, half speed
// ((t-act) mod 2L)/2, double speed 2(t-act) mod L — all versions anchored at
// the playback start and realigning every two loop periods.
static std::vector<float> ExpectGrid(int s, long act, float r, long from, int N) {
  const size_t L = v.loop_len_[s];
  const float* m = v.slab_[s];
  std::vector<float> out(N);
  for (int j = 0; j < N; j++) {
    const long e = from + j - act;
    float h;
    if (r == 1.f)      h = (float)(e % (long)L);
    else if (r < 1.f)  h = (float)(e % (long)(2 * L)) * 0.5f;
    else               h = (float)((2 * e) % (long)L);
    size_t i0 = (size_t)h; const float f = h - (float)i0;
    if (i0 < Vestige::SeamXfadeLen(L)) i0 += L;
    out[j] = m[i0] * (1.f - f) + m[i0 + 1] * f;
  }
  return out;
}
static float CorrAt(long at, const std::vector<float>& ref, int lag) {
  double xy = 0, xx = 0, yy = 0;
  for (size_t t = 0; t < ref.size(); t++) { const double a = WetH(at + lag + (long)t), b = ref[t]; xy += a * b; xx += a * a; yy += b * b; }
  return (xx > 0 && yy > 0) ? (float)(xy / sqrt(xx * yy)) : 0.f;
}
static int BestLagRef(long at, const std::vector<float>& ref, int maxlag, float* c) {
  int best = 0; float bc = -2.f;
  for (int d = -maxlag; d <= maxlag; d++) { const float cc = CorrAt(at, ref, d); if (cc > bc) { bc = cc; best = d; } }
  *c = bc; return best;
}
// Loop period of the wet from its autocorrelation: the SMALLEST lag in
// [lo, hi] whose correlation is within 5% of the best one there.
static long PeriodOf(long at, int N, long lo, long hi) {
  std::vector<double> cs_(hi - lo + 1);
  double best = -2;
  for (long P = lo; P <= hi; P += 1) {
    double xy = 0, xx = 0, yy = 0;
    for (int t = 0; t < N; t += 4) { const double a = WetH(at + t), b = WetH(at + t + P); xy += a * b; xx += a * a; yy += b * b; }
    const double c = (xx > 0 && yy > 0) ? xy / sqrt(xx * yy) : 0; cs_[P - lo] = c; if (c > best) best = c;
  }
  for (long P = lo; P <= hi; P++) if (cs_[P - lo] >= best * 0.95) {
    long b = P; while (b + 1 <= hi && cs_[b + 1 - lo] > cs_[b - lo]) b++;   // refine to the local max
    return b;
  }
  return -1;
}
// Least-squares split of the wet onto two expected versions.
static void Split(long at, const std::vector<float>& a, const std::vector<float>& b, float* ga, float* gb, float* resid) {
  double aa = 0, bb = 0, ab = 0, ya = 0, yb = 0, yy = 0;
  for (size_t t = 0; t < a.size(); t++) {
    const double y = WetH(at + (long)t);
    aa += a[t] * a[t]; bb += b[t] * b[t]; ab += a[t] * b[t]; ya += y * a[t]; yb += y * b[t]; yy += y * y;
  }
  const double det = aa * bb - ab * ab;
  const double x1 = (ya * bb - yb * ab) / det, x2 = (yb * aa - ya * ab) / det;
  double e = 0;
  for (size_t t = 0; t < a.size(); t++) { const double r = WetH(at + (long)t) - x1 * a[t] - x2 * b[t]; e += r * r; }
  *ga = (float)x1; *gb = (float)x2; *resid = (float)sqrt(e / (yy > 0 ? yy : 1));
}

static void TestSpeedXfade() {
  printf("-- stage 6: K1 playback speed crossfade\n");
  Reset();
  cs.sw[0] = 0; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f;
  Taps({500}); RunFor(0.6f);
  const size_t Q = 24000;
  Check(v.period_ == Q, "setup: T = 500 ms");
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  CapRec r{};
  noise_from = n; noise_to = n + 36000;            // runs into T: ceiling loop, on time
  WaitActivation(3.f, &r);
  Check(r.Q == Q && r.phase == 0, "setup: 500 ms noise loop, started on its one");
  RunFor(0.8f);
  const int s = r.s;
  const int N = 4800;

  struct Pos { const char* name; float k1; float rate; long period; };
  const Pos ps[] = { {"noon (clean)", 0.5f, 1.f, (long)Q}, {"full CCW (half)", 0.0f, 0.5f, (long)(2 * Q)},
                     {"full CW (double)", 1.0f, 2.f, (long)(Q / 2)} };
  for (const Pos& p : ps) {
    cs.knob[0] = p.k1; RunFor(1.2f);                 // crossfade + noon swap settle
    const long at = n;
    const std::vector<float> ref = ExpectVersion(s, p.rate, +1, N);
    RunFor(0.1f + (float)(3 * Q) / sr);
    float c = 0.f; const int lag = BestLagRef(at, ref, 400, &c);
    const long P = PeriodOf(at, 12000, (long)(Q / 4), (long)(5 * Q / 2));
    const std::vector<float> gref = ExpectGrid(s, r.act, p.rate, at, N);
    float cg = 0.f; const int glag = BestLagRef(at, gref, 400, &cg);
    printf("      %-18s period %ld (expect %ld)  lag vs module head %d (c=%.3f), vs grid from playback start %d (c=%.3f)\n",
           p.name, P, p.period, lag, c, glag, cg);
    char msg[160];
    snprintf(msg, sizeof msg, "%s: loop period %ld = %.2fx; on the grid anchored at its playback start (lag 0)", p.name, P, (double)P / Q);
    Check(P == p.period && lag == 0 && c > 0.95f && glag == 0 && cg > 0.95f, msg);
  }

  struct Mid { const char* name; float k1; float rate; };
  const float half_travel = VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f;
  const Mid ms[] = { {"CCW midpoint", 0.5f - half_travel, 0.5f}, {"CW midpoint", 0.5f + half_travel, 2.f} };
  for (const Mid& m : ms) {
    cs.knob[0] = m.k1; RunFor(1.2f);
    const long at = n;
    const std::vector<float> rc = ExpectVersion(s, 1.f, +1, N), rs = ExpectVersion(s, m.rate, +1, N);
    RunFor(0.2f);
    float gc = 0, gs = 0, res = 0; Split(at, rc, rs, &gc, &gs, &res);
    // The equal-power law for the amount this knob position actually gives.
    const float c1 = RemapKnob(m.k1) - 0.5f;
    const float x = (fabsf(c1) - VESTIGE_K1_DEADZONE) / (0.5f - VESTIGE_K1_DEADZONE);
    const float ec = cosf(x * 1.5707963f), es = sinf(x * 1.5707963f);
    printf("      %-18s clean %.3f + speed %.3f (expected %.3f + %.3f; sum of squares %.3f), residual %.4f\n",
           m.name, gc, gs, ec, es, gc * gc + gs * gs, res);
    Check(fabsf(gc - ec) < 0.02f && fabsf(gs - es) < 0.02f && res < 0.02f,
          m.rate < 1.f ? "CCW midpoint: clean + half speed, both ~0.707 (equal power)"
                       : "CW midpoint: clean + double speed, both ~0.707 (equal power)");
  }

  // Half-speed restarts at arbitrary sample offsets: every half-speed grain must
  // read exactly at the half-speed head (never half a sample off).
  {
    long misaligned = 0, checked = 0;
    for (int trial = 0; trial < 8; trial++) {
      cs.knob[0] = 0.5f; RunFor(0.6f);                   // noon: speed version idle
      blk = 1; RunFor((float)(7 * trial + 3) / sr);       // shift the restart by odd sample counts
      cs.knob[0] = 0.0f;
      for (int j = 0; j < 9600; j++) {
        RunFor(1.f / sr);
        const float L = (float)v.loop_len_[s];
        // Half head at the NEXT sample (grain read_pos_f_ is the next read).
        float c = v.fwd_[s] + 1.f; int32_t pass = v.pass_[s];
        if (c >= L) { c -= L; pass++; }
        const float hn = (c + ((pass & 1) ? L : 0.f)) * 0.5f;
        for (int g = 0; g < VESTIGE_GRAINS; g++) {
          if (!v.grains_[g].IsActive() || v.grain_ver_[g] != 1 || v.grain_slot_[g] != s) continue;
          float d = fmodf(v.grains_[g].read_pos_f_ - hn, L); if (d < 0.f) d += L;
          checked++;
          if (d != 0.f && d != L) misaligned++;
        }
      }
      Realign();
    }
    printf("      half-speed restarts: %ld grain reads checked, %ld off the half-speed head\n", checked, misaligned);
    Check(checked > 0 && misaligned == 0, "half speed: every grain reads exactly on the half-speed head, whatever the restart sample");
    cs.knob[0] = 0.5f; RunFor(0.6f);
  }
  // K2 CCW for reverse also moves T; re-tap T so the loop is back at its own
  // length (in tape mode it follows T) and let the tape rate settle.
  cs.knob[1] = 0.2f; RunFor(0.3f); Taps({500}); RunFor(1.0f);
  const Pos rv[] = { {"reverse half", 0.0f, 0.5f, (long)(2 * Q)}, {"reverse double", 1.0f, 2.f, (long)(Q / 2)} };
  for (const Pos& p : rv) {
    cs.knob[0] = p.k1; RunFor(1.2f);
    const long at = n;
    const std::vector<float> ref = ExpectVersion(s, p.rate, -1, N);
    RunFor(0.1f + (float)(3 * Q) / sr);
    float c = 0.f; const int lag = BestLagRef(at, ref, 400, &c);
    const long P = PeriodOf(at, 12000, (long)(Q / 4), (long)(5 * Q / 2));
    printf("      %-18s period %ld (expect %ld)  content lag %d, c=%.3f\n", p.name, P, p.period, lag, c);
    Check(v.rev_play_ && P == p.period && lag == 0 && c > 0.95f,
          p.rate < 1.f ? "reverse at half speed: period 2x, reversed content on the grid"
                       : "reverse at double speed: period 0.5x, reversed content on the grid");
  }
  cs.knob[1] = 0.85f; cs.knob[0] = 0.5f; RunFor(1.0f);
  hist_on = false; in_hist.clear(); wet_hist.clear(); noise_from = noise_to = -1;

  // Noon crossing declick, on a sine loop (noise has no meaningful step bound).
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true;
  WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(1.0f);
  cs.knob[0] = 1.0f; RunFor(1.5f);
  maxd = 0.f; RunFor(1.0f); const float step_cw = maxd;
  cs.knob[0] = 0.0f; RunFor(1.5f);
  maxd = 0.f; RunFor(1.0f); const float step_ccw = maxd;
  blk = 1;
  int side_prev = v.sp_side_; int swaps = 0; bool swap_silent = true;
  maxd = 0.f;
  for (int k = 0; k <= 120; k++) {
    cs.knob[0] = (float)k / 120.f;
    const long e = n + 480;
    while (n < e) {
      RunFor(1.f / sr);
      if (v.sp_side_ != side_prev) { swaps++; if (v.sp_x_ != 0.f) swap_silent = false; side_prev = v.sp_side_; }
    }
  }
  RunFor(0.5f);
  Realign();
  const float step_sweep = maxd;
  const float bound = 1.5f * (step_cw > step_ccw ? step_cw : step_ccw);
  printf("      max sample step: steady half %.5f, steady double %.5f, sweep CCW->noon->CW %.5f (bound %.5f), swaps %d\n",
         step_ccw, step_cw, step_sweep, bound, swaps);
  Check(swaps == 1 && swap_silent, "noon crossing: half->double swap happened once, with the speed version at exactly 0");
  Check(step_sweep <= bound, "noon crossing + crossfade: no step above 1.5x the steady double-speed maximum");
  cs.knob[0] = 0.5f; RunFor(1.0f);

  printf("      guard-read audit (per sample):\n");
  struct Aud { const char* name; float k1; float k2; int tap; long burst; };
  const Aud au[] = {
    {"2x fwd, T=100 ms ceiling",   1.0f, 0.85f, 100, 9000},
    {"2x rev, T=100 ms ceiling",   1.0f, 0.20f, 100, 9000},
    {"0.5x rev, T=100 ms ceiling", 0.0f, 0.20f, 100, 9000},
    {"2x fwd, 1/8 late join",      1.0f, 0.85f, 100, 300},
    {"2x rev, 1/8 late join",      1.0f, 0.20f, 100, 300},
    {"2x fwd, T=1 s ceiling",      1.0f, 0.85f, 1000, 60000},
    {"2x rev, T=1 s ceiling",      1.0f, 0.20f, 1000, 60000},
  };
  for (const Aud& a : au) {
    Reset(); cs.knob[4] = 0.5f; cs.knob[1] = a.k2; cs.knob[0] = a.k1; RunFor(1.5f);
    Taps({a.tap}); RunFor(0.3f);
    const long bad0 = audit_bad, reads0 = audit_reads;
    blk = 1; audit_on = true;
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + a.burst;
    CapRec q{}; const bool got = WaitActivation(3.f, &q);
    RunFor(1.5f);
    audit_on = false; Realign();
    printf("        %-28s Q %6zu phase %5u  reads audited %ld, bad %ld\n", a.name, q.Q, q.phase,
           audit_reads - reads0, audit_bad - bad0);
    char msg[160]; snprintf(msg, sizeof msg, "%s: captured length is still a division of T", a.name);
    Check(got && GridQuantize::IndexOf(q.Q, (size_t)a.tap * 48) >= 0, msg);
  }
  Check(audit_bad == 0, "guard audit: no grain ever read an unwritten guard cell (all speeds, both directions)");
  printf("      deepest guard read: %ld cells past min(L, guard) (<= 1 = within the coverage clamp)\n", audit_max_over);
  Check(audit_max_over <= 1, "coverage clamp: no read past L + min(L, guard) (+1 interpolation partner)");
  noise_from = noise_to = -1;

  // Full poly pool + K1 in the crossfade: the grain count doubles there; the cap holds.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; cs.sw[0] = 1; Taps({1000}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[6] = {3000, 9000, 14000, 21000, 27000, 30500};
  for (int i = 0; i < VESTIGE_MAX_VOICES; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.3f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  int live6 = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) if (v.active_[q] && !v.dying_[q]) live6++;
  max_grains = 0; uint32_t d0 = v.grain_cap_drops_; RunFor(3.0f);
  const int g_noon = max_grains; const uint32_t drop_noon = v.grain_cap_drops_ - d0;
  cs.knob[0] = ms[1].k1; RunFor(1.0f);
  max_grains = 0; d0 = v.grain_cap_drops_; RunFor(3.0f);
  const int g_mid = max_grains; const uint32_t drop_mid = v.grain_cap_drops_ - d0;
  cs.knob[0] = 1.0f; RunFor(1.5f);
  max_grains = 0; d0 = v.grain_cap_drops_; RunFor(3.0f);
  const int g_end = max_grains; const uint32_t drop_end = v.grain_cap_drops_ - d0;
  printf("      %d voices: max active grains noon %d (cap drops %u) | CW midpoint %d (drops %u) | full CW %d (drops %u); cap %d\n",
         live6, g_noon, drop_noon, g_mid, drop_mid, g_end, drop_end, VESTIGE_MB_GRAIN_CAP);
  Check(live6 == VESTIGE_MAX_VOICES, "setup: a full poly pool is live");
  // The reason for the voice count: in steady state (no voice fading out) the
  // crossfade must fit the cap without refusing a single grain.
  Check(2 * 2 * VESTIGE_MAX_VOICES > VESTIGE_MB_GRAIN_CAP || drop_mid == 0,
        "steady state: K1 crossfade with a full pool refuses no grains");
  Check(g_noon <= VESTIGE_MB_GRAIN_CAP && g_mid <= VESTIGE_MB_GRAIN_CAP && g_end <= VESTIGE_MB_GRAIN_CAP,
        "grain count never exceeds VESTIGE_MB_GRAIN_CAP");
  Check(g_end <= g_noon + 1, "at the K1 end only one version runs (no doubling outside the crossfade)");
  cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f);
  Unhold();
}

// ---------------------------------------------------------------------------
// Loops follow T — A: tape (SW2 UP).
static int FirstLive() { for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) if (v.active_[q] && !v.dying_[q]) return q; return -1; }
// Samples between the next two forward wraps of slot s's clean head (the
// loop's pass length as actually played), measured per sample.
static long PassLength(int s, float max_secs) {
  blk = 1;
  long w0 = -1, w1 = -1; float prev = v.fwd_[s];
  const long end = n + (long)(max_secs * sr);
  while (n < end && w1 < 0) {
    RunFor(1.f / sr);
    const float f = v.fwd_[s];
    if (f < prev) { if (w0 < 0) w0 = n; else w1 = n; }
    prev = f;
  }
  Realign();
  return (w0 >= 0 && w1 >= 0) ? (w1 - w0) : -1;
}
// Period as the GLOBAL correlation maximum in [lo, hi] (one period in range).
static long PeriodArgmax(long at, int N, long lo, long hi) {
  long best = -1; double bc = -2;
  for (long P = lo; P <= hi; P++) {
    double xy = 0, xx = 0, yy = 0;
    for (int t = 0; t < N; t += 2) { const double a = WetH(at + t), b = WetH(at + t + P); xy += a * b; xx += a * a; yy += b * b; }
    const double c = (xx > 0 && yy > 0) ? xy / sqrt(xx * yy) : 0; if (c > bc) { bc = c; best = P; }
  }
  return best;
}
// Expected clean output over N samples from the module's state, the head
// stepping by rho per sample (forward). Seam cells modelled as in ExpectVersion.
static std::vector<float> ExpectTape(int s, double rho, int N) {
  const double L = (double)v.loop_len_[s];
  double c = v.fwd_d_[s];                          // double, like the module's head
  const float* m = v.slab_[s];
  std::vector<float> out(N);
  for (int j = 0; j < N; j++) {
    c += rho; if (c >= L) c -= L;
    size_t i0 = (size_t)c; const float f = (float)(c - (double)i0);
    if (i0 < Vestige::SeamXfadeLen((size_t)L)) i0 += (size_t)L;
    out[j] = m[i0] * (1.f - f) + m[i0 + 1] * f;
  }
  return out;
}

static void TestFollowTape() {
  printf("-- follow T, A: tape (SW2 UP)\n");
  Reset();
  cs.sw[0] = 0; v.follow_mode_cfg_ = 0; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f;
  Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  CapRec r{};
  noise_from = n; noise_to = n + 36000;
  WaitActivation(3.f, &r);
  noise_from = noise_to = -1;
  const int s = r.s;
  RunFor(0.8f);
  Check(r.Q == 24000 && v.div_[s] == 0 && v.rho_s_[s] == 1.f, "setup: 500 ms loop (division 1), tape rate exactly 1");
  const uint32_t tg0 = v.tape_grains_;
  // T unchanged, K2 jitter below the follow deadband: nothing moves.
  for (int k = 0; k < 200; k++) { cs.knob[1] = 0.85f + ((k & 1) ? 0.003f : -0.003f); RunFor(0.01f); }
  cs.knob[1] = 0.85f; RunFor(0.1f);
  Check(v.period_ == 24000 && v.rho_s_[s] == 1.f && v.tape_grains_ == tg0,
        "K2 jitter (+-0.003) below the deadband: T and the tape rate untouched, original path only");

  // Re-tap: the loop's length follows d x T_now, on its own timeline.
  struct Tc_ { int tap; size_t Lt; float rho; };
  const Tc_ tcs[] = { {1000, 48000, 0.5f}, {250, 12000, 2.f}, {700, 33600, 24000.f / 33600.f} };
  for (const Tc_& t : tcs) {
    Taps({t.tap}); RunFor(0.6f);
    const long P = PassLength(s, 3.f);
    const long at = n;
    const std::vector<float> ref = ExpectTape(s, v.rho_d_[s], 4800);
    RunFor(0.1f + (float)(t.Lt + 7000) / sr);       // one full pass + the window, all after the glide
    float c = 0.f; const int lag = BestLagRef(at, ref, 300, &c);
    const long Pwet = PeriodArgmax(at, 6000, (long)(t.Lt - 600), (long)(t.Lt + 600));
    printf("      tap %4d ms: rate %.4f (expect %.4f)  pass %ld  output period %ld (expect %zu)  content lag %d c=%.3f\n",
           t.tap, v.rho_s_[s], t.rho, P, Pwet, t.Lt, lag, c);
    char msg[160]; snprintf(msg, sizeof msg, "tap %d ms: loop period follows d x T_now = %zu, tape content on its timeline", t.tap, t.Lt);
    Check(fabsf(v.rho_s_[s] - t.rho) < 1e-5f && labs(P - (long)t.Lt) <= 1 && labs(Pwet - (long)t.Lt) <= 1 && lag == 0 && c > 0.95f, msg);
  }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // Phase continuity through a tap: the loop fraction advances by exactly the
  // (gliding) rate every sample — no jump.
  {
    Taps({500}); RunFor(0.6f);
    blk = 1;
    double worst = 0; double prevf = v.fwd_d_[s];
    const double L = (double)v.loop_len_[s];
    PressFS1(5);
    long moved = 0;
    for (int j = 0; j < 48000; j++) {
      RunFor(1.f / sr);
      double d = v.fwd_d_[s] - prevf; if (d < -L * 0.5) d += L;
      const double e = fabs(d - v.rho_d_[s]);     // this sample advanced by its own (gliding) rate
      if (e > worst) worst = e;
      if (v.rho_d_[s] != 1.0) moved++;
      prevf = v.fwd_d_[s];
    }
    Realign();
    // Close the chain: second tap 800 ms after the first.
    PressFS1(5); RunFor(0.1f); RunFor(1.0f);
    printf("      phase continuity: worst |step - rate| %.2e samples over 1 s across a re-tap (%ld samples at rate != 1)\n", worst, moved);
    Check(worst < 1e-6 && moved > 0, "tap: the head moves by its own rate every sample (continuous loop fraction, no jump)");
  }

  // No click at a rate jump or an SW2 switch, on a sine loop.
  Reset(); cs.knob[4] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true;
  WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(1.0f);
  const int ss = FirstLive();
  maxd = 0.f; RunFor(1.0f); const float st1 = maxd;
  maxd = 0.f; Taps({250}); RunFor(1.0f); const float st_tap_up = maxd;
  maxd = 0.f; RunFor(1.0f); const float st2 = maxd;
  maxd = 0.f; Taps({1000}); RunFor(1.0f); const float st_tap_dn = maxd;
  maxd = 0.f; RunFor(1.0f); const float st05 = maxd;
  maxd = 0.f; v.follow_mode_cfg_ = 2; RunFor(1.0f); const float st_sw = maxd;   // -> C (latched until its step): rate glides back to 1
  Check(v.rho_s_[ss] == 1.f, "SW2 to a latched position: the loop glides back to its own length (rate exactly 1)");
  v.follow_mode_cfg_ = 0; RunFor(1.0f);
  const float big = (st1 > st2 ? st1 : st2);
  printf("      max step: steady x1 %.5f, x2 %.5f, x0.5 %.5f | tap x1->x2 %.5f, tap x2->x0.5 %.5f, SW2 x0.5->x1 %.5f (bound 1.5 x the faster side)\n",
         st1, st2, st05, st_tap_up, st_tap_dn, st_sw);
  Check(st_tap_up <= 1.5f * big && st_tap_dn <= 1.5f * big && st_sw <= 1.5f * (st1 > st05 ? st1 : st05),
        "tape rate jumps (taps) and an SW2 switch: no step above 1.5x the faster steady rate");

  // LED1 on the loop's actual "one" after T changed (a division-1 loop: its one
  // is its wrap). Old A + k*T would be off after the rate changed.
  {
    Taps({800}); RunFor(1.0f);
    blk = 1;
    std::vector<long> wraps; float prev = v.fwd_[ss];
    const size_t r0 = led1_rises.size();
    const long from = n;
    for (int j = 0; j < 48000 * 4; j++) {
      RunFor(1.f / sr);
      if (v.fwd_[ss] < prev) wraps.push_back(n - 1);
      prev = v.fwd_[ss];
    }
    Realign();
    int ok = 0, bad = 0;
    for (size_t i = r0; i < led1_rises.size(); i++) {
      const long lr = led1_rises[i];
      if (lr < from + 480) continue;
      bool near = false;
      for (long w : wraps) if (lr >= w && lr - w <= 480) near = true;
      if (near) ok++; else bad++;
    }
    const long olda = (long)v.led_anchor_[Vestige::kPoolLoop];
    int old_ok = 0;
    for (size_t i = r0; i < led1_rises.size(); i++) { const long ph = (led1_rises[i] - olda) % (long)v.period_; if (ph <= 480) old_ok++; }
    printf("      LED1 after re-tap: %d flashes on the loop's wraps, %d elsewhere (%zu wraps); A+k*T would have matched %d\n",
           ok, bad, wraps.size(), old_ok);
    Check(ok >= 3 && bad == 0, "LED1 keeps flashing on the loop's actual one after T changed");
  }

  // K2 as the source of T: jitter below the deadband never reaches a loop, a
  // slow turn reaches it as a SMOOTH rate change (no zipper from the staircase).
  {
    cs.knob[1] = 0.80f; RunFor(0.1f); cs.knob[1] = 0.85f; RunFor(0.1f);   // moves past eps: the tap is cancelled
    Check(v.tap_period_ == 0, "setup: T from the knob (tap cancelled)");
    seen_acts = v.act_count_;
    sustain_hz = 220.f; sustain_input = true; play_input = true;
    CapRec q{}; WaitActivation(10.f, &q);
    sustain_input = false; play_input = false; RunFor(0.5f);
    const size_t T0 = v.period_; const uint32_t tg = v.tape_grains_;
    for (int k = 0; k < 300; k++) { cs.knob[1] = 0.85f + ((k & 1) ? 0.003f : -0.003f); RunFor(0.01f); }
    cs.knob[1] = 0.85f; RunFor(0.1f);
    Check(v.period_ == T0 && v.rho_s_[q.s] == 1.f && v.tape_grains_ == tg,
          "knob T: K2 jitter (+-0.003) below the deadband: T and the tape rate untouched");
    // Slow turn 0.85 -> 0.75 over 2 s: T moves in deadband steps; the rate glides.
    blk = 1;
    double worst = 0; double prev = v.rho_d_[q.s]; double moved = 0;
    for (int k = 0; k <= 200; k++) {
      cs.knob[1] = 0.85f - 0.10f * (float)k / 200.f;
      for (int j = 0; j < 480; j++) {
        RunFor(1.f / sr);
        const double d = fabs(v.rho_d_[q.s] - prev) / v.rho_d_[q.s];
        if (d > worst) worst = d;
        moved += fabs(v.rho_d_[q.s] - prev);
        prev = v.rho_d_[q.s];
      }
    }
    Realign(); RunFor(0.5f);
    printf("      slow K2 turn: tape rate %.4f -> %.4f, largest per-sample change %.2e (relative)\n",
           1.0, v.rho_d_[q.s], worst);
    Check(moved > 0.05 && worst < 1e-3, "slow K2 turn: the loop follows as a smooth glide (no per-tick rate steps)");
    cs.knob[1] = 0.85f; RunFor(1.0f);
    Unhold();
  }

  // Held loops follow too.
  Hold();
  Check(v.held_, "setup: held");
  Taps({500}); RunFor(0.6f);
  { const int q = FirstLive();
    const long P = PassLength(q, 3.f);
    const size_t Lt = GridQuantize::Boundary(v.div_[q], v.period_);
    double want = (double)v.loop_len_[q] / (double)Lt; long k = 1;
    while (want > VESTIGE_TAPE_RATE_MAX) { want *= 0.5; k *= 2; }
    while (want < VESTIGE_TAPE_RATE_MIN) { want *= 2.0; }
    printf("      held loop: material %zu, target %zu, rate %.4f, pass %ld (expect %ld = target x %ld)\n",
           v.loop_len_[q], Lt, v.rho_d_[q], P, (long)Lt * k, k);
    Check(v.held_ && labs(P - (long)Lt * k) <= 1, "held loop follows T (pass = target, folded x 2^k when out of range)"); }
  Unhold();

  // Division kept (poly, several divisions), budget, coverage.
  Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 1; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[4] = {3000, 14000, 21000, 30500};
  for (int i = 0; i < 4; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.3f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  Taps({1300}); RunFor(1.0f);
  bool divs_ok = true; int nv = 0;
  for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) {
    if (!v.active_[q] || v.dying_[q]) continue;
    nv++;
    const size_t Lt = GridQuantize::Boundary(v.div_[q], v.period_);
    float want = (float)v.loop_len_[q] / (float)Lt;
    while (want > VESTIGE_TAPE_RATE_MAX) want *= 0.5f;
    while (want < VESTIGE_TAPE_RATE_MIN) want *= 2.f;
    const long P = PassLength(q, 3.f);
    const long wantP = (long)((float)v.loop_len_[q] / want + 0.5f);
    printf("      voice %d: division %d (%.4f T), material %zu -> target %zu, rate %.4f, pass %ld\n",
           q, v.div_[q], GridQuantize::DivisionOf(Lt, v.period_), v.loop_len_[q], Lt, v.rho_s_[q], P);
    if (fabsf(v.rho_s_[q] - want) > 1e-5f || labs(P - wantP) > 1) divs_ok = false;
  }
  Check(nv == VESTIGE_MAX_VOICES && divs_ok, "poly: every voice keeps its division; pass = Boundary(d, T_now)");
  cs.knob[0] = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f); RunFor(1.5f);
  max_grains = 0; uint32_t d0 = v.grain_cap_drops_; RunFor(3.0f);
  printf("      %d voices, tape rates != 1, K1 midpoint: max active grains %d, refused %u (cap %d)\n",
         nv, max_grains, v.grain_cap_drops_ - d0, VESTIGE_MB_GRAIN_CAP);
  Check(v.grain_cap_drops_ - d0 == 0 && max_grains <= VESTIGE_MB_GRAIN_CAP,
        "tape + K1 crossfade, full pool: no grain refused (still 2 grains per stream)");
  // During the glide itself (a re-tap), measured for the report.
  { const uint32_t dg0 = v.grain_cap_drops_; max_grains = 0;
    Taps({900}); RunFor(1.0f);
    printf("      same pool, re-tap 1300 -> 900 ms (glide): max active %d, refused %u during 1.9 s\n",
           max_grains, v.grain_cap_drops_ - dg0);
    Check(v.grain_cap_drops_ - dg0 == 0, "tape glide at a full pool (re-tap): no grain refused — a stream never holds 3"); }
  cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f);

  // Guard coverage at arbitrary rates, per sample: tape across the WHOLE T range
  // (up to 80x either way) with K1 on top, both directions, short and long loops.
  printf("      guard-read audit, tape rates:\n");
  struct Aud { const char* name; float k1; float k2; int tap0; long burst; int tap1; };
  const Aud au[] = {
    {"x4 . K1x2 fwd (1/8 of 400ms->100ms)", 1.0f, 0.85f, 400, 1500, 100},
    {"x4 . K1x2 rev",                        1.0f, 0.20f, 400, 1500, 100},
    {"x0.25 . K1half fwd (100ms->400ms)",    0.0f, 0.85f, 100, 9000, 400},
    {"x0.25 . K1half rev",                   0.0f, 0.20f, 100, 9000, 400},
    {"x40 (4 s -> 100 ms) fwd",              0.5f, 0.85f, 4000, 250000, 100},
    {"x80 . K1x2 fwd (8 s -> 100 ms)",       1.0f, 0.85f, 8000, 300000, 100},
    {"x80 . K1x2 rev",                       1.0f, 0.20f, 8000, 300000, 100},
    {"x1/80 . K1half fwd (100 ms -> 8 s)",   0.0f, 0.85f, 100, 3000, 8000},
    {"x1/80 . K1half rev",                   0.0f, 0.20f, 100, 3000, 8000},
  };
  const long bad0 = audit_bad; audit_max_over = 0;
  for (const Aud& a : au) {
    Reset(); cs.knob[4] = 0.5f; cs.knob[1] = a.k2; cs.knob[0] = a.k1; RunFor(1.5f);
    Taps({a.tap0}); RunFor(0.3f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + a.burst;
    CapRec q{}; WaitActivation(8.f, &q);
    noise_from = noise_to = -1;
    const long b1 = audit_bad, r1 = audit_reads;
    blk = 1; audit_on = true;
    PressFS1(5); RunFor((float)a.tap1 / 1000.f); PressFS1(5); RunFor(0.1f);   // re-tap while it plays
    RunFor(1.5f);
    audit_on = false; Realign();
    printf("        %-38s Q %6zu -> target %6zu  rate %.4f  reads %ld, bad %ld\n", a.name, q.Q,
           GridQuantize::Boundary(v.div_[q.s], v.period_), v.rho_s_[q.s], audit_reads - r1, audit_bad - b1);
  }
  Check(audit_bad == bad0, "tape rates (x1/80..x80, with K1): no grain read an unwritten guard cell");
  Check(audit_max_over <= 1, "tape rates: no read past L + min(L, guard)");
  // No folding: an 8 s loop re-tapped to 100 ms runs at exactly material/target
  // and its pass is exactly the target (the old fold gave target x 2^k).
  { Reset(); cs.knob[4] = 0.5f; cs.knob[1] = 0.85f; cs.knob[0] = 0.5f; RunFor(1.5f);
    Taps({8000}); RunFor(0.3f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 300000;
    CapRec q0{}; WaitActivation(9.f, &q0);
    noise_from = noise_to = -1;
    PressFS1(5); RunFor(0.1f); PressFS1(5); RunFor(1.0f);
    const int q = FirstLive();
    const long P = (q >= 0) ? PassLength(q, 3.f) : -1;
    const size_t Lt = (q >= 0) ? GridQuantize::Boundary(v.div_[q], v.period_) : 0;
    const double want = (q >= 0 && Lt) ? (double)v.loop_len_[q] / (double)Lt : 0.0;
    printf("      no folding: material %zu, target %zu, rate %.4f (want %.4f), pass %ld = %.3f x target\n",
           q >= 0 ? v.loop_len_[q] : 0, Lt, q >= 0 ? v.rho_s_[q] : 0.f, want, P, Lt ? (double)P / Lt : 0.0);
    Check(q >= 0 && Lt > 0 && want > 60.0 && fabs(v.rho_s_[q] - want) < 1e-3 * want && labs(P - (long)Lt) <= 1,
          "extreme re-tap (8 s -> 100 ms): rate = material / target exactly, pass = target (no folding)"); }

  // Fresh capture at an extreme rate: T re-tapped DURING the recording, so the
  // loop plays at ~80x from its very first sample while the head copy behind the
  // loop end is still being written — the case the old 4x cap protected.
  { const long bad1 = audit_bad;
    const int dirs[2] = {0, 1};
    for (int dir : dirs) {
      Reset(); cs.knob[4] = 0.5f; cs.knob[1] = dir ? 0.20f : 0.85f; cs.knob[0] = 1.0f; RunFor(1.5f);
      Taps({8000}); RunFor(0.3f);
      seen_acts = v.act_count_;
      noise_from = n; noise_to = n + 60000;                 // ~1.25 s phrase against T = 8 s
      RunFor(0.3f); PressFS1(5); RunFor(0.1f); PressFS1(5);  // re-tap to 100 ms mid-capture
      blk = 1; audit_on = true;
      CapRec q1{}; WaitActivation(4.f, &q1);
      noise_from = noise_to = -1;
      RunFor(1.0f);
      audit_on = false; Realign();
      printf("      fresh capture at ~x80 . K1x2 %s: rate at activation %.2f, bad reads %ld\n",
             dir ? "rev" : "fwd", v.rho_s_[q1.s], audit_bad - bad1);
    }
    Check(audit_bad == bad1, "fresh capture played at ~80x from its first sample: no unwritten guard cell read"); }

  // The copy speed only rises for a loop that is read fast. Normal playing must
  // copy at exactly the base rate: a flat fast copy was a CPU burst at every new
  // loop and clicked on the pedal (1280dad).
  { Reset(); cs.knob[4] = 0.5f; cs.knob[1] = 0.85f; cs.knob[0] = 0.5f; RunFor(1.0f);
    Taps({1000}); RunFor(0.3f);
    v.fill_budget_max_ = 0;
    for (int i = 0; i < 4; i++) {
      seen_acts = v.act_count_;
      noise_from = n; noise_to = n + 9000 + 7000 * i;
      CapRec qn{}; WaitActivation(3.f, &qn);
      noise_from = noise_to = -1; RunFor(0.6f);
    }
    printf("      normal captures at rate 1: largest guard-copy speed %d cells/sample (base %u)\n",
           v.fill_budget_max_, VESTIGE_GUARD_FILL_PER_SAMPLE);
    Check(v.fill_budget_max_ == (int)VESTIGE_GUARD_FILL_PER_SAMPLE,
          "normal playing copies the guard at the base speed (no CPU burst at a new loop)"); }

  // The capture is quantised against the T at ITS start; it follows T_now once playing.
  Reset(); cs.knob[4] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 60000;               // long: runs to the 500 ms ceiling
  RunFor(0.15f);
  PressFS1(5); RunFor(0.3f); PressFS1(5); RunFor(0.05f);   // re-tap 300 ms DURING the capture
  { CapRec q{}; WaitActivation(3.f, &q);
    noise_from = noise_to = -1;
    printf("      tap during capture: capture T %zu, Q %zu, T now %zu, rate at start %.4f\n", q.T, q.Q, v.period_, v.rho_s_[q.s]);
    Check(q.T == 24000 && GridQuantize::IndexOf(q.Q, 24000) >= 0 &&
          fabsf(v.rho_s_[q.s] - (float)q.Q / (float)GridQuantize::Boundary(v.div_[q.s], v.period_)) < 1e-5f,
          "capture quantised against its own start's T, then plays at T_now from its first sample"); }
  RunFor(0.5f);
  Unhold();
}

// ---------------------------------------------------------------------------
// Loops follow T — B: stretch (SW2 MIDDLE).
// Dominant frequency of the wet over [from, from+N): Goertzel scan 100..900 Hz
// in 1 Hz steps (a spectral peak: robust to the grain overlap's phase jumps,
// which fool a zero-crossing count).
static float PitchOf(long from, int N) {
  float best_f = 0.f; double best = -1;
  for (int f = 100; f <= 900; f++) {
    const double w = 2.0 * M_PI * f / sr, c = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (int t = 0; t < N; t++) { const double s0 = WetH(from + t) + c * s1 - s2; s2 = s1; s1 = s0; }
    const double pw = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (pw > best) { best = pw; best_f = (float)f; }
  }
  return best_f;
}
// Wet power in [f0-bw, f0+bw] Hz (Goertzel, 1 Hz steps).
static double BandPower(long from, int N, float f0, float bw) {
  double sum = 0;
  for (int f = (int)(f0 - bw); f <= (int)(f0 + bw); f++) {
    const double w = 2.0 * M_PI * f / sr, c = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (int t = 0; t < N; t++) { const double s0 = WetH(from + t) + c * s1 - s2; s2 = s1; s1 = s0; }
    sum += s1 * s1 + s2 * s2 - c * s1 * s2;
  }
  return sum;
}
// Level ripple of the wet: (p90 - p10) / (p90 + p10) of 5 ms RMS frames.
static float RippleOf(long from, long N) {
  std::vector<float> fr;
  for (long t = 0; t + 240 <= N; t += 240) { double e = 0; for (int k = 0; k < 240; k++) { const double x = WetH(from + t + k); e += x * x; } fr.push_back((float)sqrt(e / 240)); }
  std::sort(fr.begin(), fr.end());
  const float lo = fr[fr.size() / 10], hi = fr[fr.size() * 9 / 10];
  return (hi + lo > 0.f) ? (hi - lo) / (hi + lo) : 0.f;
}
// Attacks in the wet over [from, from+N): 1 ms RMS frames, an onset when a frame
// jumps 4x above the mean of the previous 10 frames (and above a floor), with a
// 10 ms refractory.
static int OnsetsIn(long from, long N) {
  std::vector<float> fr;
  for (long t = 0; t + 48 <= N; t += 48) { double e = 0; for (int k = 0; k < 48; k++) { const double x = WetH(from + t + k); e += x * x; } fr.push_back((float)sqrt(e / 48)); }
  int on = 0; int refr = 0;
  for (size_t i = 10; i < fr.size(); i++) {
    if (refr > 0) { refr--; continue; }
    float m = 0; for (int k = 1; k <= 10; k++) m += fr[i - k]; m /= 10.f;
    if (fr[i] > 0.01f && fr[i] > 4.f * m) { on++; refr = 10; }
  }
  return on;
}
static size_t CleanGrainLen(int s) {
  for (int g = 0; g < VESTIGE_GRAINS; g++)
    if (v.grains_[g].IsActive() && v.grain_slot_[g] == s && v.grain_ver_[g] == 0) return v.grains_[g].grain_len_;
  return 0;
}

static void TestFollowStretch() {
  printf("-- follow T, B: stretch (SW2 MIDDLE)\n");
  const size_t gst = (size_t)((float)VESTIGE_STRETCH_GRAIN_MS * 0.001f * sr) & ~(size_t)1;
  // Sine loop: pitch must stay, time must follow.
  Reset();
  cs.sw[0] = 0; v.follow_mode_cfg_ = 1; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f;
  Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true;
  CapRec r{}; WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(0.8f);
  const int s = r.s;
  Check(r.Q == 24000 && v.div_[s] == 0, "setup: 500 ms sine loop (division 1)");
  const float p0 = PitchOf(n - 12000, 12000);
  printf("      captured sine loop: %.0f Hz, level ripple at rate 1: %.3f\n", p0, RippleOf(n - 24000, 24000));
  const uint32_t tg0 = v.tape_grains_;
  RunFor(1.0f);
  Check(v.rho_d_[s] == 1.0 && v.tape_grains_ == tg0 && CleanGrainLen(s) != gst,
        "T unchanged: rate exactly 1, original path, clean-loop grains (not stretch grains)");
  struct St { int tap; size_t Lt; double sig; float k1; float pitch; };
  const St sts[] = { {1000, 48000, 0.5, 0.5f, 220.f}, {250, 12000, 2.0, 0.5f, 220.f},
                     {700, 33600, 24000.0 / 33600.0, 0.5f, 220.f}, {1000, 48000, 0.5, 1.0f, 440.f} };
  bool grains_ok = true;
  for (const St& t : sts) {
    cs.knob[0] = t.k1; Taps({t.tap}); RunFor(0.8f);
    const long P = PassLength(s, 3.f);
    const size_t gl = CleanGrainLen(s);
    const long at = n; RunFor(0.5f);
    const float pz = PitchOf(at, 12000);
    const float rip = RippleOf(at, 24000);
    // Where the energy is: around the expected pitch vs around where TAPE
    // would have put it (pitch x rate).
    const double pin  = BandPower(at, 12000, t.pitch, 30.f);
    const double ptap = BandPower(at, 12000, t.pitch * (float)t.sig, 30.f);
    const float  frac = (float)(pin / (pin + ptap));
    printf("      tap %4d ms, K1 %.1f: rate %.4f (expect %.4f)  pass %ld (expect %zu)  peak %.0f Hz  energy at %.0f Hz vs tape's %.0f Hz: %.3f  level ripple %.2f  grain %zu\n",
           t.tap, t.k1, v.rho_d_[s], t.sig, P, t.Lt, pz, t.pitch, t.pitch * (float)t.sig, frac, rip, gl);
    char msg[160]; snprintf(msg, sizeof msg, "stretch, tap %d ms%s: pass = d x T_now = %zu, pitch stays %.0f Hz", t.tap,
                            t.k1 > 0.9f ? " + K1 x2" : "", t.Lt, t.pitch);
    Check(fabs(v.rho_d_[s] - t.sig) < 1e-9 && labs(P - (long)t.Lt) <= 1 && frac > 0.9f, msg);
    if (t.k1 < 0.9f && gl != gst) grains_ok = false;
  }
  Check(grains_ok, "while stretching, the clean stream uses the stretch grain length");
  cs.knob[0] = 0.5f; RunFor(0.8f);
  // Back to the original T: rate 1, the clean-loop reconstruction as before.
  hist_on = true;
  Taps({500}); RunFor(1.0f);
  { const long at = n;
    const std::vector<float> ref = ExpectTape(s, v.rho_d_[s], 4800);
    RunFor(0.2f);
    float c = 0.f; const int lag = BestLagRef(at, ref, 300, &c);
    const size_t gl = CleanGrainLen(s);
    printf("      back to 500 ms: rate %.6f, grain %zu, content vs the material at rate 1: lag %d c=%.4f\n", v.rho_d_[s], gl, lag, c);
    Check(v.rho_d_[s] == 1.0 && gl != gst && lag == 0 && c > 0.999f,
          "stretch rate back to 1: clean-loop grains, the loop reconstructs as before (lag 0)"); }

  // Phase continuity through a re-tap, in B.
  {
    blk = 1;
    double worst = 0; double prevf = v.fwd_d_[s]; const double L = (double)v.loop_len_[s]; long moved = 0;
    PressFS1(5);
    for (int j = 0; j < 48000; j++) {
      RunFor(1.f / sr);
      double d = v.fwd_d_[s] - prevf; if (d < -L * 0.5) d += L;
      const double e = fabs(d - v.rho_d_[s]); if (e > worst) worst = e;
      if (v.rho_d_[s] != 1.0) moved++;
      prevf = v.fwd_d_[s];
    }
    Realign(); PressFS1(5); RunFor(0.1f); RunFor(1.0f);
    printf("      stretch phase continuity: worst |step - rate| %.2e over 1 s across a re-tap (%ld samples at rate != 1)\n", worst, moved);
    Check(worst < 1e-6 && moved > 0, "stretch: the head moves by its own rate every sample (no jump)");
  }

  // Clicks: rate jumps and A/B/C switches, on the sine loop.
  Taps({500}); RunFor(1.0f);
  maxd = 0.f; RunFor(1.0f); const float st1 = maxd;
  maxd = 0.f; Taps({1000}); RunFor(1.0f); const float s_dn = maxd;
  maxd = 0.f; RunFor(1.0f); const float st05 = maxd;
  maxd = 0.f; Taps({250}); RunFor(1.0f); const float s_up = maxd;
  maxd = 0.f; RunFor(1.0f); const float st2 = maxd;
  maxd = 0.f; v.follow_mode_cfg_ = 0; RunFor(1.0f); const float s_ab = maxd;   // B -> A: same head rate, pitch follows
  maxd = 0.f; RunFor(1.0f); const float stA = maxd;
  maxd = 0.f; v.follow_mode_cfg_ = 2; RunFor(1.0f); const float s_ac = maxd;   // -> C (latched): back to its own length
  maxd = 0.f; v.follow_mode_cfg_ = 1; RunFor(1.0f); const float s_cb = maxd;   // -> B again
  float big = st1; if (st05 > big) big = st05; if (st2 > big) big = st2; if (stA > big) big = stA;
  printf("      max step: steady B x1 %.5f, x0.5 %.5f, x2 %.5f, A x2 %.5f | tap x1->x0.5 %.5f, x0.5->x2 %.5f, B->A %.5f, A->C %.5f, C->B %.5f (bound %.5f)\n",
         st1, st05, st2, stA, s_dn, s_up, s_ab, s_ac, s_cb, 1.5f * big);
  Check(s_dn <= 1.5f * big && s_up <= 1.5f * big && s_ab <= 1.5f * big && s_ac <= 1.5f * big && s_cb <= 1.5f * big,
        "stretch: rate jumps and SW2 A/B/C switches: no step above 1.5x the largest steady step");

  // LED1 on the loop's actual one in B.
  {
    Taps({800}); RunFor(1.0f);
    blk = 1;
    std::vector<long> wraps; float prev = v.fwd_[s];
    const size_t r0 = led1_rises.size(); const long from = n;
    for (int j = 0; j < 48000 * 4; j++) { RunFor(1.f / sr); if (v.fwd_[s] < prev) wraps.push_back(n - 1); prev = v.fwd_[s]; }
    Realign();
    int ok = 0, bad = 0;
    for (size_t i = r0; i < led1_rises.size(); i++) {
      const long lr = led1_rises[i]; if (lr < from + 480) continue;
      bool near = false; for (long w : wraps) if (lr >= w && lr - w <= 480) near = true;
      if (near) ok++; else bad++;
    }
    printf("      LED1 in B after re-tap: %d flashes on the loop's wraps, %d elsewhere\n", ok, bad);
    Check(ok >= 3 && bad == 0, "stretch: LED1 keeps flashing on the loop's actual one after T changed");
  }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // Attack repetition when slowing down: a loop of 4 sharp clicks per pass.
  {
    Reset(); v.follow_mode_cfg_ = 1; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
    hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
    seen_acts = v.act_count_;
    // 4 clicks, 125 ms apart, on a low bed so the gate stays open to the ceiling.
    click_from = n; click_on = true;
    CapRec q{}; WaitActivation(2.f, &q);
    click_on = false; RunFor(0.8f);
    const int qs = q.s;
    printf("      attack repetition (loop of 4 clicks per pass, stretch grains %u ms):\n", VESTIGE_STRETCH_GRAIN_MS);
    struct Sg { int tap; double sig; };
    const Sg sgs[] = { {500, 1.0}, {1000, 0.5}, {2000, 0.25}, {250, 2.0} };
    int base = 0;
    for (const Sg& g : sgs) {
      Taps({g.tap}); RunFor(1.0f);
      const size_t Lt = GridQuantize::Boundary(v.div_[qs], v.period_);
      const long at = n; RunFor((float)(2 * Lt) / sr + 0.05f);
      const int on = OnsetsIn(at, (long)(2 * Lt));
      if (g.sig == 1.0) base = on;
      printf("        rate %.2f: %d attacks per pass (source has 4)\n", g.sig, on / 2);
    }
    Check(base / 2 == 4, "click loop at rate 1: 4 attacks per pass (the detector sees the source)");
    hist_on = false; in_hist.clear(); wet_hist.clear();
  }

  // Grain budget: full pool, stretch + K1 midpoint, steady and during a glide.
  Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 1; v.follow_mode_cfg_ = 1; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[4] = {3000, 14000, 21000, 30500};
  for (int i = 0; i < 4; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.3f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  Taps({1300}); RunFor(1.0f);
  cs.knob[0] = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f); RunFor(1.5f);
  max_grains = 0; uint32_t d0 = v.grain_cap_drops_; RunFor(3.0f);
  const int gm = max_grains; const uint32_t dr = v.grain_cap_drops_ - d0;
  max_grains = 0; d0 = v.grain_cap_drops_; Taps({700}); RunFor(1.0f);
  printf("      4 voices, stretch + K1 midpoint: max active %d, refused %u steady | re-tap glide: max %d, refused %u\n",
         gm, dr, max_grains, v.grain_cap_drops_ - d0);
  Check(dr == 0 && v.grain_cap_drops_ - d0 == 0 && gm <= VESTIGE_MB_GRAIN_CAP,
        "stretch, full pool + K1 crossfade: no grain refused, steady or gliding (2 per stream)");
  cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f);

  // Guard audit, and no folding in B.
  printf("      guard-read audit, stretch rates:\n");
  struct Aud { const char* name; float k1; float k2; int tap0; long burst; int tap1; };
  const Aud au[] = {
    {"x4 . K1x2 fwd (400ms->100ms, 1/8)",   1.0f, 0.85f, 400, 1500, 100},
    {"x4 . K1x2 rev",                        1.0f, 0.20f, 400, 1500, 100},
    {"x0.25 . K1half rev (100ms->400ms)",    0.0f, 0.20f, 100, 9000, 400},
    {"x40 unfolded (4 s -> 100 ms) fwd",     0.5f, 0.85f, 4000, 250000, 100},
  };
  const long bad0 = audit_bad; audit_max_over = 0;
  for (const Aud& a : au) {
    Reset(); v.follow_mode_cfg_ = 1; cs.knob[4] = 0.5f; cs.knob[1] = a.k2; cs.knob[0] = a.k1; RunFor(1.5f);
    Taps({a.tap0}); RunFor(0.3f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + a.burst;
    CapRec q{}; WaitActivation(8.f, &q);
    noise_from = noise_to = -1;
    const long b1 = audit_bad, r1 = audit_reads;
    blk = 1; audit_on = true;
    PressFS1(5); RunFor((float)a.tap1 / 1000.f); PressFS1(5); RunFor(0.1f);
    RunFor(1.5f);
    audit_on = false; Realign();
    printf("        %-36s Q %6zu -> target %6zu  rate %.4f  reads %ld, bad %ld\n", a.name, q.Q,
           GridQuantize::Boundary(v.div_[q.s], v.period_), v.rho_s_[q.s], audit_reads - r1, audit_bad - b1);
  }
  Check(audit_bad == bad0 && audit_max_over <= 1, "stretch rates (x0.25..x40, with K1): no unwritten guard read, none past L + min(L, guard)");
  { const int q = FirstLive();
    const long P = PassLength(q, 3.f);
    printf("      no folding in B: material %zu, target %zu, rate %.2f, pass %ld\n", v.loop_len_[q],
           GridQuantize::Boundary(v.div_[q], v.period_), v.rho_d_[q], P);
    Check(labs(P - (long)GridQuantize::Boundary(v.div_[q], v.period_)) <= 1,
          "stretch, extreme re-tap (40x): pass = exactly d x T_now (no octave folding)"); }
  v.follow_mode_cfg_ = 0; RunFor(0.5f);
  Unhold();
}

// ---------------------------------------------------------------------------
// Onset detection lifts the re-arm block after a ceiling stop.
struct NoteTrain { long from = -1; long gap = 0; int count = 0; float hz = 110.f, amp = 0.3f, atk_s = 0.004f, tau_s = 0.4f; };
static NoteTrain nt;
// A BEATING string: two close partials make one ringing note's level swell and
// dip slowly (here 1 Hz, depth 0.6) — what a real bass string does, and what a
// pure sine never shows.
static bool bt_on = false; static long bt_from = 0;
static float bt_f1 = 41.2f, bt_f2 = 42.2f, bt_d = 0.6f, bt_amp = 0.3f, bt_tau = 1.5f;
static float BeatIn(long k) {
  if (k < bt_from) return 0.f;
  const float t = (float)(k - bt_from) / sr;
  const float a = (t < 0.002f ? t / 0.002f : 1.f) * expf(-t / bt_tau);
  return bt_amp * a * (sinf(2.f * 3.14159265f * bt_f1 * t) + bt_d * sinf(2.f * 3.14159265f * bt_f2 * t)) / (1.f + bt_d);
}
// A SPIKY low E: harmonics 1..16 in phase give one sharp peak per cycle, the
// way a real bass DI signal does. On the FAST meter that peak jumps ~2x over the
// onset baseline every cycle — this is the model that reproduces the hardware
// DIAG log (false onsets every 100 ms on one ringing note). Sines never do.
static bool sp_on = false; static long sp_from = 0; static float sp_amp = 0.3f, sp_hz = 41.2f, sp_tau = 2.0f;
static float SpikyIn(long k) {
  if (k < sp_from) return 0.f;
  const float t = (float)(k - sp_from) / sr;
  const float a = (t < 0.002f ? t / 0.002f : 1.f) * expf(-t / sp_tau);
  float y = 0.f;
  for (int h = 1; h <= 16; h++) y += cosf(2.f * 3.14159265f * sp_hz * h * t) / sqrtf((float)h);
  return sp_amp * a * y / 7.0f;
}
static float NoteIn(long k) {
  if (sp_on) return SpikyIn(k);
  if (bt_on) return BeatIn(k);
  if (nt.count <= 0 || k < nt.from) return 0.f;
  const long rel = k - nt.from; const long i = rel / nt.gap;
  if (i >= nt.count) {                                     // last note keeps ringing
    const float t = (float)(rel - (nt.count - 1) * nt.gap) / sr;
    return nt.amp * expf(-t / nt.tau_s) * sinf(2.f * 3.14159265f * nt.hz * (float)k / sr);
  }
  const float t = (float)(rel - i * nt.gap) / sr;
  const float a = (t < nt.atk_s ? t / nt.atk_s : 1.f) * expf(-t / nt.tau_s);
  // Each new note adds to the previous one's ring (a real re-pluck is louder
  // than the decayed string), so the envelope RISES at every attack.
  float prev = 0.f;
  if (i > 0) { const float tp = t + (float)nt.gap / sr; prev = expf(-tp / nt.tau_s); }
  return nt.amp * (a + prev * (t < nt.atk_s ? 1.f - t / nt.atk_s : 0.f)) * sinf(2.f * 3.14159265f * nt.hz * (float)k / sr);
}
// Replicated detector: the module's own envelope + onset arithmetic run over
// the recorded input from a snapshot of its state. Returns the first sample at
// or after `from` where an onset fires.
static float rep_env0 = 0.f, rep_gate0 = 0.f, rep_slow0 = 0.f; static int rep_refr0 = 0;
static long ReplicatedOnset(long from) {
  float env = rep_env0, gate = rep_gate0, slow = rep_slow0; int refr = rep_refr0;
  for (long k = hist_n0; k < hist_n0 + (long)in_hist.size(); k++) {
    env += VESTIGE_ENV_COEF * (fabsf(InH(k)) - env);
    // The gate meter (VESTIGE_GATE_ENV_MODE 1): follows env up, falls slowly.
    if (VESTIGE_GATE_ENV_MODE == 1) { if (env > gate) gate = env; else gate += v.gate_rel_coef_ * (env - gate); }
    else gate = env;
    const float oe = VESTIGE_ONSET_ON_GATE ? gate : env;     // what the detector reads
    slow += VESTIGE_ONSET_SLOW_COEF * (oe - slow);
    if (refr > 0) refr--;
    const bool on = (refr == 0 && oe > v.auto_thresh_ * VESTIGE_ONSET_FLOOR_REL && oe > slow * VESTIGE_ONSET_RISE);
    if (on) { refr = v.onset_refr_len_; if (k >= from) return k; }
  }
  return -1;
}
static void StartHist() {
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  rep_env0 = v.env_; rep_gate0 = v.env_gate_; rep_slow0 = v.onset_slow_; rep_refr0 = v.onset_refr_;
}

static void TestOnsetRearm() {
  printf("-- onset lifts the re-arm block\n");
  const uint32_t starts_before = v.onset_starts_;
  printf("      earlier sections: %u captures were started by an onset lifting the block\n", starts_before);

  struct Sc { const char* name; float hz, amp, k4, atk; };
  const Sc scs[] = { {"bass 110 Hz, 0.3", 110.f, 0.3f, 0.1f, 0.004f},
                     {"bass 41 Hz, 0.3",   41.2f, 0.3f, 0.1f, 0.004f},
                     {"guitar 330 Hz, 0.03, K4 CCW", 330.f, 0.03f, 0.0f, 0.002f} };
  for (const Sc& c : scs) {
    // 1. Ringing note hits the T ceiling; a re-pluck while it still rings above
    //    the close level starts a new capture at the onset sample.
    Reset(); cs.sw[0] = 0; v.follow_mode_cfg_ = 0; SetK4Thresh(c.k4); cs.knob[4] = 0.5f;
    Taps({300}); RunFor(0.8f);
    StartHist();
    seen_acts = v.act_count_;
    nt = NoteTrain{}; nt.from = n + 480; nt.gap = 28800; nt.count = 2; nt.hz = c.hz; nt.amp = c.amp; nt.atk_s = c.atk; nt.tau_s = 0.4f;
    note_on = true;
    // Run to just before the second note, recording whether the block is on.
    bool blocked = false; float env_at = 0.f;
    while (n < nt.from + nt.gap - 480) { RunFor(0.01f); if (v.rearm_block_) blocked = true; }
    env_at = v.env_;
    const bool still_blocked = v.rearm_block_ && env_at > v.auto_thresh_ * VESTIGE_AUTO_HYST;
    const uint32_t os0 = v.onset_starts_;
    RunFor(0.05f);
    const long det = ReplicatedOnset(nt.from + nt.gap);
    const bool started = v.onset_starts_ == os0 + 1;
    const long A = (long)v.cap_start_[v.rec_slot_];
    printf("      %-28s blocked before re-pluck %d (env %.4f > close %.4f)  re-pluck at %ld: capture start %+ld, replicated onset %+ld\n",
           c.name, (int)still_blocked, env_at, v.auto_thresh_ * VESTIGE_AUTO_HYST, nt.from + nt.gap,
           A - (nt.from + nt.gap), det - (nt.from + nt.gap));
    char msg[200];
    snprintf(msg, sizeof msg, "%s: re-pluck while the ceiling-stopped note still rings starts a capture at the onset sample", c.name);
    Check(blocked && still_blocked && started && det >= 0 && A == det, msg);
    note_on = false; nt.count = 0; RunFor(1.0f);
    hist_on = false; in_hist.clear(); wet_hist.clear();

    // 2. A decaying note alone never re-triggers: one long-ringing note, ceiling
    //    stop, nothing else played.
    Reset(); SetK4Thresh(c.k4); cs.knob[4] = 0.5f; Taps({300}); RunFor(0.8f);
    seen_acts = v.act_count_;
    nt = NoteTrain{}; nt.from = n + 480; nt.gap = 48000 * 20; nt.count = 1; nt.hz = c.hz; nt.amp = c.amp; nt.atk_s = c.atk; nt.tau_s = 1.5f;
    note_on = true;
    const uint32_t oc0 = v.onset_count_, os1 = v.onset_starts_, ac0 = v.act_count_;
    RunFor(6.0f);
    const uint32_t onsets = v.onset_count_ - oc0, caps = v.act_count_ - ac0;
    printf("      %-28s one note ringing 6 s: %u onset(s), %u capture(s), %u started by an onset\n", c.name, onsets, caps, v.onset_starts_ - os1);
    // (Extra captures of a LOW note's tail come from the level gate, not the
    // onset: its ripple crosses close/open near the thresholds. Pre-existing —
    // identical on 6ec83de — and outside this step; counted, not asserted.)
    snprintf(msg, sizeof msg, "%s: a decaying note alone never re-triggers (one onset, no onset-started capture)", c.name);
    Check(onsets == 1 && v.onset_starts_ == os1, msg);
    note_on = false; nt.count = 0; RunFor(1.0f);
  }

  // 3. One pluck = one onset, over plucks of different pitch, level and attack.
  {
    struct Pk { float hz, amp, atk, k4; };
    const Pk pks[] = { {30.9f, 0.3f, 0.001f, 0.1f}, {41.2f, 0.3f, 0.001f, 0.1f}, {110.f, 0.3f, 0.004f, 0.1f},
                       {110.f, 0.3f, 0.020f, 0.1f}, {330.f, 0.03f, 0.002f, 0.0f}, {82.4f, 0.1f, 0.010f, 0.0f},
                       {196.f, 0.01f, 0.004f, 0.0f}, {61.7f, 0.05f, 0.002f, 0.0f} };
    int worst = 0, best = 99;
    for (const Pk& p : pks) {
      Reset(); SetK4Thresh(p.k4); cs.knob[4] = 0.5f; RunFor(0.5f);
      for (int i = 0; i < 6; i++) {
        nt = NoteTrain{}; nt.from = n + 480; nt.gap = 48000 * 20; nt.count = 1; nt.hz = p.hz; nt.amp = p.amp; nt.atk_s = p.atk;
        nt.tau_s = (i & 1) ? 1.5f : 0.3f;                   // short and sustained plucks
        note_on = true;
        const uint32_t oc0 = v.onset_count_;
        RunFor(1.5f);
        const int k = (int)(v.onset_count_ - oc0);
        if (k > worst) worst = k; if (k < best) best = k;
        note_on = false; nt.count = 0; RunFor(1.0f);
      }
    }
    printf("      48 plucks (31-330 Hz, 0.01-0.3, 1-20 ms attacks, short + sustained): onsets per pluck min %d, max %d\n", best, worst);
    Check(best == 1 && worst == 1, "one pluck never triggers twice (and always once)");
  }

  // 4. Onsets never end or split a capture: attacks inside one continuous phrase.
  {
    Reset(); SetK4Thresh(0.1f); cs.knob[4] = 0.5f; Taps({2000}); RunFor(1.0f);
    seen_acts = v.act_count_;
    nt = NoteTrain{}; nt.from = n + 480; nt.gap = 14400; nt.count = 5; nt.hz = 110.f; nt.amp = 0.3f; nt.tau_s = 0.12f;
    note_on = true;
    const uint32_t oc0 = v.onset_count_, ac0 = v.act_count_;
    RunFor(1.5f);
    const uint32_t mid_onsets = v.onset_count_ - oc0;
    const bool still_rec = v.recording_ && v.act_count_ == ac0;
    RunFor(1.5f);
    printf("      one phrase with 5 plucks, T = 2 s: %u onsets inside it, still one capture recording at 1.5 s: %d\n", mid_onsets, (int)still_rec);
    Check(mid_onsets >= 4 && still_rec, "onsets inside a phrase never end or split the capture");
    note_on = false; nt.count = 0; RunFor(1.5f);
  }
  Unhold();
}

// ---------------------------------------------------------------------------
// The gate meter (VESTIGE_GATE_ENV_MODE). What it is FOR: a decaying low note is
// captured exactly once. What it COSTS: the pause two stabs need between them to
// become two captures — measured, not asserted, so the modes can be compared.
static void TestGateMeter() {
  printf("-- gate meter (VESTIGE_GATE_ENV_MODE %d)\n", VESTIGE_GATE_ENV_MODE);
  const float hzs[] = {30.9f, 41.2f, 110.f};
  for (float hz : hzs) {
    Reset(); cs.sw[0] = 0; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; Taps({300}); RunFor(0.8f);
    seen_acts = v.act_count_;
    nt = NoteTrain{}; nt.from = n + 480; nt.gap = 48000 * 20; nt.count = 1; nt.hz = hz;
    nt.amp = 0.3f; nt.atk_s = 0.002f; nt.tau_s = 1.5f;
    note_on = true;
    const uint32_t ac0 = v.act_count_;
    RunFor(6.0f);
    const uint32_t caps = v.act_count_ - ac0;
    printf("      one decaying %5.1f Hz note, T = 300 ms: %u capture(s)\n", hz, caps);
    if (VESTIGE_GATE_ENV_MODE != 0) {
      char msg[160];
      snprintf(msg, sizeof msg, "a decaying %.1f Hz note is captured exactly once (no tail re-capture)", hz);
      Check(caps == 1, msg);
    }
    note_on = false; nt.count = 0; RunFor(1.0f);
  }

  // A beating note: its slow swells must not re-open the gate on the tail
  // (VESTIGE_REARM_EVERY_END). Without it, most settings capture it twice.
  {
    int worst = 0;
    const float amps[] = {0.3f, 0.1f}; const float k4s[] = {0.0f, 0.1f, 0.3f};
    for (float amp : amps) for (float k4 : k4s) {
      Reset(); cs.sw[0] = 0; SetK4Thresh(k4); cs.knob[4] = 0.5f; Taps({300}); RunFor(0.8f);
      seen_acts = v.act_count_;
      int starts = 0; bool was = v.recording_;
      bt_amp = amp; bt_from = n + 480; bt_on = true; note_on = true;
      for (int i = 0; i < 1000; i++) { RunFor(0.01f); if (v.recording_ && !was) starts++; was = v.recording_; }
      if (starts > worst) worst = starts;
      note_on = false; bt_on = false; RunFor(1.0f);
    }
    printf("      one beating low E (1 Hz beat), 6 level/K4 settings: at most %d capture(s)\n", worst);
    if (VESTIGE_REARM_EVERY_END) Check(worst == 1, "a beating note is captured exactly once (swells do not re-open the gate)");
  }

  // A spiky ringing low E at a short T: every capture hits the ceiling, so a
  // false onset would start the next one at once (the hardware failure). With
  // the detector on the gate meter it is one capture and one onset.
  {
    int worst_caps = 0; uint32_t worst_onsets = 0, worst_os = 0;
    const float amps[] = {0.2f, 0.3f};
    for (float amp : amps) {
      Reset(); cs.sw[0] = 0; SetK4Thresh(0.45f); cs.knob[4] = 0.5f; Taps({230}); RunFor(0.8f);
      seen_acts = v.act_count_;
      const uint32_t oc0 = v.onset_count_, os0 = v.onset_starts_;
      int starts = 0; bool was = v.recording_;
      sp_amp = amp; sp_from = n + 480; sp_on = true; note_on = true;
      for (int i = 0; i < 600; i++) { RunFor(0.01f); if (v.recording_ && !was) starts++; was = v.recording_; }
      if (starts > worst_caps) worst_caps = starts;
      if (v.onset_count_ - oc0 > worst_onsets) worst_onsets = v.onset_count_ - oc0;
      if (v.onset_starts_ - os0 > worst_os) worst_os = v.onset_starts_ - os0;
      note_on = false; sp_on = false; RunFor(1.0f);
    }
    printf("      spiky ringing low E, T = 230 ms: at most %d capture(s), %u onset(s), %u onset-started\n",
           worst_caps, worst_onsets, worst_os);
    Check(worst_caps == 1 && worst_onsets == 1 && worst_os == 0,
          "a spiky ringing note at short T: one capture, no false onsets restarting it");
  }

  // Stab separation: two 60 ms noise stabs, gap g, T = 2 s (no ceiling).
  const int gaps_ms[] = {60, 80, 100, 120, 150, 200, 250, 300, 400};
  int min_gap = -1;
  printf("      two 60 ms stabs -> captures, by pause:");
  for (int g : gaps_ms) {
    Reset(); cs.sw[0] = 1; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; Taps({2000}); RunFor(1.0f);
    seen_acts = v.act_count_;
    const uint32_t ac0 = v.act_count_;
    const long a0 = n + 480, a1 = a0 + 2880;
    noise_from = a0; noise_to = a1;
    while (n < a1 + 48) RunFor(0.001f);
    noise_from = a1 + (long)g * 48; noise_to = noise_from + 2880;
    RunFor(4.0f);
    noise_from = noise_to = -1;
    const uint32_t caps = v.act_count_ - ac0;
    printf(" %d ms:%u", g, caps);
    if (caps >= 2 && min_gap < 0) min_gap = g;
    RunFor(0.5f);
  }
  printf("\n      shortest pause that gives two separate loops: %d ms\n", min_gap);
  Unhold();
}

// ---------------------------------------------------------------------------
// Loops follow T — C: re-cut (SW2 DOWN), non-destructive.
static uint64_t RowHash(int s, size_t n_) { return Hash(v.slab_[s], n_); }
// Expected clean output for the next N samples: the head at rate 1 through the
// CURRENT view (or the raw row), wrapping at the play length; just past a wrap
// the crossing grains read the seam cells (view guard / capture seam).
static std::vector<float> ExpectView(int s, int dir, int N) {
  const long L = (long)v.PlayLen(s);
  long c = (long)v.fwd_[s];
  const int cv = v.cur_view_[s];
  std::vector<float> out(N);
  for (int j = 0; j < N; j++) {
    c += dir; if (c >= L) c -= L; if (c < 0) c += L;
    const size_t xf = Vestige::SeamXfadeLen((size_t)L);
    if (cv >= 0) {
      const GrainView& vw = v.views_[s][cv];
      out[j] = ((size_t)c < xf) ? vw.guard[c] : vw.At((size_t)c);
    } else {
      out[j] = ((size_t)c < xf) ? v.slab_[s][L + c] : v.slab_[s][c];
    }
  }
  return out;
}
// Run per sample for `secs`; record every play-length change and whether it
// happened exactly on a wrap of the clean head (forward: head decreased;
// reverse: head increased).
struct Change { long at; size_t from, to; bool at_wrap; };
static void WatchStep() {
  const int s = watch_s;
  const float f = v.fwd_[s];
  const bool wrapped = v.rev_play_ ? (f > watch_prev + 1.5f) : (f < watch_prev - 1.5f);
  if (v.PlayLen(s) != watch_pl) { watch_changes++; if (!wrapped) watch_midpass++; watch_pl = v.PlayLen(s); }
  watch_prev = f;
}
static void WatchOn(int s) { watch_s = s; watch_pl = v.PlayLen(s); watch_prev = v.fwd_[s]; blk = 1; }
static void WatchOff() { watch_s = -1; Realign(); }
static std::vector<Change> WatchChanges(int s, float secs) {
  std::vector<Change> ch;
  blk = 1;
  size_t pl = v.PlayLen(s); float prev = v.fwd_[s];
  const long end = n + (long)(secs * sr);
  while (n < end) {
    RunFor(1.f / sr);
    const float f = v.fwd_[s];
    const bool wrapped = v.rev_play_ ? (f > prev + 1.5f) : (f < prev - 1.5f);
    if (v.PlayLen(s) != pl) { ch.push_back(Change{n - 1, pl, v.PlayLen(s), wrapped}); pl = v.PlayLen(s); }
    prev = f;
  }
  Realign();
  return ch;
}

static void TestFollowRecut() {
  printf("-- follow T, C: re-cut (SW2 DOWN)\n");
  Reset();
  cs.sw[0] = 0; v.follow_mode_cfg_ = 2; SetK4Thresh(0.1f); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f;
  Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  CapRec r{};
  noise_from = n; noise_to = n + 36000;
  WaitActivation(3.f, &r);
  noise_from = noise_to = -1;
  const int s = r.s;
  const size_t M = v.loop_len_[s];
  RunFor(0.8f);                                             // capture guard fully written
  const size_t rowlen = M + VESTIGE_GUARD_SAMPLES;
  const uint64_t h0 = RowHash(s, rowlen);
  Check(M == 24000 && v.cur_view_[s] < 0 && v.PlayLen(s) == M && v.rho_d_[s] == 1.0,
        "setup: 500 ms loop, C: raw row, play length = stored length, rate 1");
  const uint32_t slip_b0 = v.recut_slip_build_, slip_p0 = v.recut_slip_parity_;

  struct Rc { int tap; size_t Lt; const char* what; };
  const Rc rcs[] = { {250, 12000, "cut to 1/2"}, {500, 24000, "back to the recorded length (cut material returns)"},
                     {1000, 48000, "pad with silence"}, {750, 36000, "shorter pad (the recorded part is whole again)"},
                     {330, 15840, "cut, off any period"}, {500, 24000, "back again"} };
  bool all_at_wrap = true, hash_ok = true;
  for (const Rc& t : rcs) {
    const long c0 = watch_changes, m0 = watch_midpass;
    WatchOn(s); Taps({t.tap}); RunFor(2.2f); WatchOff();
    std::vector<Change> ch;                          // (summary only)
    for (long k = 0; k < watch_changes - c0; k++) ch.push_back(Change{0, 0, 0, true});
    if (watch_midpass != m0 && !ch.empty()) ch.back().at_wrap = false;
    const long P = PassLength(s, 3.f);
    const long at = n;
    const std::vector<float> ref = ExpectView(s, +1, 4800);
    RunFor(0.2f);
    float c = 0.f; const int lag = BestLagRef(at, ref, 300, &c);
    for (const Change& k : ch) if (!k.at_wrap) all_at_wrap = false;
    if (RowHash(s, rowlen) != h0) hash_ok = false;
    printf("      tap %4d ms: %-50s pass %6ld (expect %zu)  %zu change(s) %s  view %d  content lag %d c=%.4f\n",
           t.tap, t.what, P, t.Lt, ch.size(), ch.empty() ? "" : (ch.back().at_wrap ? "at a wrap" : "MID-PASS"),
           v.cur_view_[s], lag, c);
    char msg[200]; snprintf(msg, sizeof msg, "C, tap %d ms (%s): pass = %zu, content on its timeline", t.tap, t.what, t.Lt);
    Check(labs(P - (long)t.Lt) <= 0 && lag == 0 && c > 0.999f, msg);
  }
  Check(all_at_wrap && watch_midpass == 0 && watch_changes >= 6,
        "every C length change took effect exactly at a wrap of the loop, never mid-pass");
  Check(hash_ok, "non-destructive: material + capture guard byte-identical through every cut / pad / return");
  Check(v.cur_view_[s] < 0 && v.PlayLen(s) == M, "back at the recorded length: the raw row again (no view)");

  // Padding really is silence after the material, faded out at its end.
  {
    Taps({1000});
    WatchChanges(s, 2.2f);
    // Find the next wrap, then measure the silence region and the body.
    blk = 1; float prev = v.fwd_[s]; while (!(v.fwd_[s] < prev - 1.5f)) { prev = v.fwd_[s]; RunFor(1.f / sr); } Realign();
    const long w = n - (long)v.fwd_[s] - 1;                  // sample where the pass began
    RunFor(1.1f);
    double eb = 0, es = 0;
    for (long k = w + 1000; k < w + 22000; k++) eb += (double)WetH(k) * WetH(k);
    for (long k = w + 24000 + 600; k < w + 48000 - 600; k++) es += (double)WetH(k) * WetH(k);
    const double rb = sqrt(eb / 21000), rs = sqrt(es / (24000 - 1200));
    printf("      padded pass: body RMS %.4f, padded-silence RMS %.2e (ratio %.1e)\n", rb, rs, rs / rb);
    Check(rs < rb * 1e-3, "padding: after the material the loop is silent");
    Taps({500}); WatchChanges(s, 2.2f);
  }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // No clicks at the new seams, on a sine loop: a cut off any period, a pad
  // (fade-out at the material end, silence -> head).
  Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true;
  WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(1.0f);
  { const int q = r.s;
    maxd = 0.f; RunFor(1.0f); const float st = maxd;
    Taps({330}); WatchChanges(q, 1.0f); maxd = 0.f; RunFor(2.0f); const float s_cut = maxd;
    Taps({900}); WatchChanges(q, 1.5f); maxd = 0.f; RunFor(3.0f); const float s_pad = maxd;
    Taps({500}); WatchChanges(q, 2.5f); maxd = 0.f; RunFor(1.0f); const float s_back = maxd;
    printf("      sine loop max step: steady %.5f | cut at 15840 (mid-cycle) %.5f | padded to 43200 %.5f | back %.5f (bound %.5f)\n",
           st, s_cut, s_pad, s_back, 1.5f * st);
    Check(s_cut <= 1.5f * st && s_pad <= 1.5f * st && s_back <= 1.5f * st,
          "C seams: cut mid-cycle, pad (fade-out + silence -> head), return: no step above 1.5x steady");

    // LED1 on the loop's actual one in C.
    Taps({330}); WatchChanges(q, 1.0f);
    blk = 1;
    std::vector<long> wraps; float prev = v.fwd_[q];
    const size_t r0 = led1_rises.size(); const long from = n;
    for (int j = 0; j < 48000 * 3; j++) { RunFor(1.f / sr); if (v.fwd_[q] < prev - 1.5f) wraps.push_back(n - 1); prev = v.fwd_[q]; }
    Realign();
    int ok = 0, bad = 0;
    for (size_t i = r0; i < led1_rises.size(); i++) {
      const long lr = led1_rises[i]; if (lr < from + 480) continue;
      bool near = false; for (long wv : wraps) if (lr >= wv && lr - wv <= 480) near = true;
      if (near) ok++; else bad++;
    }
    printf("      LED1 in C after a cut: %d flashes on the loop's wraps, %d elsewhere\n", ok, bad);
    Check(ok >= 3 && bad == 0, "C: LED1 keeps flashing on the loop's actual one after T changed");
  }

  // Level through a cut: the old grains fade out as the restarted stream fades
  // in (identical material across the wrap) — no dip, no bump.
  for (float k2 : {0.85f, 0.2f}) {
    Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; cs.knob[1] = k2; RunFor(0.3f); Taps({500}); RunFor(0.6f);
    hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
    seen_acts = v.act_count_;
    sustain_hz = 220.f; sustain_input = true; play_input = true; CapRec q{}; WaitActivation(3.f, &q);
    sustain_input = false; play_input = false; RunFor(1.5f);
    RunFor(8.2f);                                   // a lone interval, not joined to the setup tap
    double e = 0; for (long k = n - 24000; k < n; k++) e += (double)WetH(k) * WetH(k); const double steady = sqrt(e / 24000);
    blk = 1; size_t pl = v.PlayLen(q.s); long chg = -1;
    PressFS1(5);
    for (long j = 0; j < 250L * 48 + 48000L * 3 && chg < 0; j++) { if (j == 250L * 48) PressFS1(5); RunFor(1.f / sr); if (v.PlayLen(q.s) != pl) chg = n - 1; }
    RunFor(0.8f); Realign();
    double lo = 10, hi = 0;
    for (long f = chg; f + 480 <= chg + 28800; f += 480) { double s2 = 0; for (long k = f; k < f + 480; k++) s2 += (double)WetH(k) * WetH(k);
      const double r = sqrt(s2 / 480) / steady; if (r < lo) lo = r; if (r > hi) hi = r; }
    printf("      %s cut %zu -> %zu (sine loop): level over the 600 ms after it %.2f .. %.2f of steady\n",
           k2 < 0.5f ? "reverse" : "forward", pl, v.PlayLen(q.s), lo, hi);
    Check(chg >= 0 && lo > 0.85 && hi < 1.25, k2 < 0.5f ? "C reverse: no level dip or bump through a cut" : "C forward: no level dip or bump through a cut");
    hist_on = false; in_hist.clear(); wet_hist.clear();
  }
  cs.knob[1] = 0.85f;

  // Reverse: cut and pad backwards.
  {
    Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[1] = 0.2f; RunFor(0.3f); Taps({500}); RunFor(0.6f);
    hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 36000; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(0.8f);
    bool ok = true, wrap_ok = true;
    const int taps[3] = {250, 1000, 500}; const size_t lts[3] = {12000, 48000, 24000};
    for (int i = 0; i < 3; i++) {
      const long m0 = watch_midpass;
      WatchOn(q.s); Taps({taps[i]}); RunFor(2.2f); WatchOff();
      if (watch_midpass != m0) wrap_ok = false;
      const long at = n;
      const std::vector<float> ref = ExpectView(q.s, -1, 4800);
      RunFor(0.2f);
      float c = 0.f; const int lag = BestLagRef(at, ref, 300, &c);
      printf("      reverse, tap %4d ms: play length %zu (expect %zu), content lag %d c=%.4f\n", taps[i], v.PlayLen(q.s), lts[i], lag, c);
      if (v.PlayLen(q.s) != lts[i] || lag != 0 || c < 0.999f) ok = false;
    }
    Check(ok && wrap_ok && v.rev_play_, "C reverse: cut / pad / return follow T, applied at a wrap, content on its timeline");
    hist_on = false; in_hist.clear(); wet_hist.clear();
    cs.knob[1] = 0.85f; RunFor(0.3f);
  }

  // K1 half speed audible: a change waits for an EVEN pass (its own wrap), so
  // the half-speed version never jumps mid-pass. Counted as parity slips.
  {
    Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[0] = 0.0f; RunFor(1.5f); Taps({500}); RunFor(0.6f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 36000; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(0.8f);
    const uint32_t p0 = v.recut_slip_parity_;
    bool even_ok = true; int changes = 0;
    for (int t : {250, 700, 500}) {
      blk = 1; size_t pl = v.PlayLen(q.s);
      PressFS1(5);
      for (int j = 0; j < t * 48 + 48000 * 3; j++) {
        if (j == t * 48) PressFS1(5);
        RunFor(1.f / sr);
        if (v.PlayLen(q.s) != pl) { changes++; if (v.pass_[q.s] & 1) even_ok = false; pl = v.PlayLen(q.s); }
      }
      Realign();
    }
    printf("      K1 half speed: %d changes, all on even passes %d, %u parity slip(s) (a change waited one pass)\n",
           changes, (int)even_ok, v.recut_slip_parity_ - p0);
    Check(changes == 3 && even_ok, "K1 half speed audible: C changes apply only where the half-speed version also wraps");
    cs.knob[0] = 0.5f; RunFor(1.5f);
  }

  // Poly: every voice follows its own division; budget; held.
  Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 1; v.follow_mode_cfg_ = 2; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[4] = {3000, 14000, 21000, 30500};
  for (int i = 0; i < 4; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.3f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  Hold();
  Taps({1300}); RunFor(3.0f);
  bool divs_ok = true; int nv = 0;
  for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) {
    if (!v.active_[q] || v.dying_[q]) continue;
    nv++;
    const size_t Lt = GridQuantize::Boundary(v.div_[q], v.period_);
    const long P = PassLength(q, 3.f);
    printf("      held voice %d: division %d, stored %zu -> play length %zu (target %zu), pass %ld\n",
           q, v.div_[q], v.loop_len_[q], v.PlayLen(q), Lt, P);
    if (v.PlayLen(q) != Lt || labs(P - (long)Lt) > 0) divs_ok = false;
  }
  Check(nv == VESTIGE_MAX_VOICES && divs_ok && v.held_, "poly, held: every voice re-cut to Boundary(its d, T_now)");
  Unhold();
  cs.knob[0] = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f); RunFor(1.5f);
  max_grains = 0; max_counted = 0; uint32_t d0 = v.grain_cap_drops_;
  Taps({800}); RunFor(3.0f);
  printf("      4 voices, C + K1 midpoint through a re-cut: max counted by the cap %d, physical max %d (incl. grains fading out over 5 ms), refused %u\n",
         max_counted, max_grains, v.grain_cap_drops_ - d0);
  Check(v.grain_cap_drops_ - d0 == 0 && max_counted <= VESTIGE_MB_GRAIN_CAP, "C, full pool + K1 crossfade, through a re-cut: no grain refused, cap held");
  cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f);

  // Guard audit: grains reading through views, fwd/rev, K1 x2, short loops.
  printf("      guard-read audit, C views:\n");
  struct Aud { const char* name; float k1; float k2; int tap0; long burst; int tap1; };
  const Aud au[] = {
    {"cut, K1x2 fwd (400ms->100ms, 1/8)", 1.0f, 0.85f, 400, 1500, 100},
    {"cut, K1x2 rev",                     1.0f, 0.20f, 400, 1500, 100},
    {"pad, K1half rev (100ms->400ms)",    0.0f, 0.20f, 100, 9000, 400},
    {"pad, K1x2 fwd (1 s -> 1.6 s)",      1.0f, 0.85f, 1000, 60000, 1600},
  };
  const long bad0 = audit_bad; audit_max_over = 0;
  for (const Aud& a : au) {
    Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[1] = a.k2; cs.knob[0] = a.k1; RunFor(1.5f);
    Taps({a.tap0}); RunFor(0.3f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + a.burst;
    CapRec q{}; WaitActivation(8.f, &q);
    noise_from = noise_to = -1;
    const long b1 = audit_bad, r1 = audit_reads;
    blk = 1; audit_on = true;
    PressFS1(5); RunFor((float)a.tap1 / 1000.f); PressFS1(5); RunFor(0.1f);
    RunFor(2.5f);
    audit_on = false; Realign();
    printf("        %-36s stored %6zu -> play %6zu  view %d  reads %ld, bad %ld\n", a.name, q.Q, v.PlayLen(q.s), v.cur_view_[q.s],
           audit_reads - r1, audit_bad - b1);
  }
  Check(audit_bad == bad0 && audit_max_over <= 1, "C views: no grain read outside its view / built guard, none past L + min(L, guard)");

  // Slips: a change requested just before a wrap cannot be built in time.
  {
    Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.6f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 60000; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(1.0f);
    // Time the closing tap to land ~20 ms before a wrap (the build needs ~56 ms).
    const uint32_t sb0 = v.recut_slip_build_;
    int landed = 0;
    for (int tries = 0; tries < 3; tries++) {
      PressFS1(5); RunFor(0.8f);                       // first tap of an 800 ms interval
      // wait until 20 ms before a wrap, then close the interval (T = 800+)
      blk = 1; while ((long)v.PlayLen(q.s) - (long)v.fwd_[q.s] > 960 + 5) RunFor(1.f / sr);
      Realign();
      PressFS1(5); RunFor(0.1f);
      const std::vector<Change> ch = WatchChanges(q.s, 3.5f);
      if (!ch.empty()) landed++;
      Taps({1000}); WatchChanges(q.s, 3.0f);
    }
    printf("      change requested ~20 ms before a wrap (build needs ~56 ms): %d applied, %u build slip(s) — each waited exactly one more pass\n",
           landed, v.recut_slip_build_ - sb0);
    Check(landed == 3, "a change that misses its wrap applies at the next one (slips measured, not lost)");
  }
  printf("      slips over the whole C section: build %u, parity %u, applied %u\n",
         v.recut_slip_build_ - slip_b0, v.recut_slip_parity_ - slip_p0, v.recut_applied_);
  v.follow_mode_cfg_ = 0; RunFor(0.5f);
  Unhold();
}

// ---------------------------------------------------------------------------
// Stage 3: the TIMING error — a Euclidean groove, one instance per
// VESTIGE_TIMING_PATTERN_PASSES passes (HALF TIME at 2).
static constexpr int kSpan = VESTIGE_TIMING_PATTERN_PASSES;
struct InstRec { long start; int pat; int rot; uint32_t mask; uint32_t base; int brot; int var; bool ret = false; float half0 = 0.f;
                 std::vector<long> passes; std::vector<long> hits_el; std::vector<long> hits_at; };
struct TrigLog { std::vector<InstRec> inst; long rh_bad = 0, half_jumps = 0, half_carry = 0, inner_wraps = 0, inner_xfades = 0; };
// Run per sample for `count` pattern instances of slot s (recording starts at
// the next instance start): per instance its pattern, its pass starts and
// every restart (elapsed from the instance start, absolute sample); whether it
// ended with a return to the one. Also, every sample: the read head in range,
// and (track_half) the K1 half-speed head continuous except at restarts.
static TrigLog RunInst(int s, int count, float max_secs = 60.f, bool track_half = false) {
  TrigLog lg; blk = 1;
  int32_t pp = v.pass_[s]; uint32_t tc = v.timing_trigs_, rc = v.timing_returns_;
  float hprev = v.SpeedHead(s, 0.5f);
  const long end = n + (long)(max_secs * sr);
  while (n < end && (int)lg.inst.size() < count + 1) {
    RunFor(1.f / sr);
    const bool trig = v.timing_trigs_ != tc, ret = v.timing_returns_ != rc;
    if (v.pass_[s] != pp) {
      pp = v.pass_[s];
      if (v.trig_start_[s] == v.pass_[s]) {                // a new instance was laid out here
        if (!lg.inst.empty()) lg.inst.back().ret = ret;
        InstRec r; r.start = n - 1; r.pat = v.cur_pat_[s]; r.rot = v.cur_rot_[s]; r.mask = v.cur_mask_[s];
        r.base = v.base_mask_[s]; r.brot = v.base_rot_[s]; r.var = v.cur_var_[s];
        r.half0 = v.SpeedHead(s, 0.5f);                      // where the K1 half-speed cycle is
        lg.inst.push_back(r);
      }
      if (!lg.inst.empty()) lg.inst.back().passes.push_back(n - 1);
      if (!lg.inst.empty() && v.trig_start_[s] != v.pass_[s] && !trig) {   // an inner wrap with no hit on it
        lg.inner_wraps++;
        bool xf = v.restart_[s][0] || v.restart_[s][1];
        for (int g = 0; g < VESTIGE_GRAINS; g++) if (v.grain_slot_[g] == s && v.grains_[g].IsActive() && v.grains_[g].FadingOut()) xf = true;
        if (xf) lg.inner_xfades++;
      }
      if (track_half && !trig && !ret && v.trig_off_[s] != 0.f) lg.half_carry++;   // a wrap the read runs through
    }
    if (trig) {
      if (v.last_trig_slot_ == s && !lg.inst.empty()) {
        lg.inst.back().hits_el.push_back((n - 1) - lg.inst.back().start);
        lg.inst.back().hits_at.push_back(n - 1);
      }
    }
    tc = v.timing_trigs_; rc = v.timing_returns_;
    const float L = (float)v.PlayLen(s), h = v.ReadHead(s);
    if (!(h >= 0.f && h < L)) lg.rh_bad++;
    if (track_half) {
      const float h2 = v.SpeedHead(s, 0.5f);
      float d = h2 - hprev; if (d > 0.5f * L) d -= L; if (d < -0.5f * L) d += L;
      if (!trig && !ret && fabsf(d - 0.5f) > 1e-3f) lg.half_jumps++;
      hprev = h2;
    }
  }
  Realign();
  if (!lg.inst.empty()) lg.inst.pop_back();                  // the last one is incomplete
  return lg;
}
static std::string MaskStr(uint32_t m, int n) { std::string r; for (int i = 0; i < n; i++) r += (m & (1u << i)) ? 'x' : '.'; return r; }
static uint32_t RotMask(uint32_t m, int n, int h) { uint32_t r = 0; for (int i = 0; i < n; i++) if (m & (1u << ((i + h) % n))) r |= 1u << i; return r; }
static int Pop(uint32_t m) { int c = 0; while (m) { c += m & 1u; m >>= 1; } return c; }
// The test's own model of the level -> pattern rule: list position round(L*9);
// short-loop guard on the SPAN's step = densest fitting at or below, else
// sparsest fitting above.
static int WantBase(float level) { return (int)(level * (float)(VESTIGE_TIMING_PATTERNS - 1) + 0.5f); }
static int WantFit(float level, double pass_out) {
  const double min_step = VESTIGE_TIMING_MIN_STEP_MS * 0.001 * sr, span = kSpan * pass_out;
  const int b = WantBase(level);
  for (int i = b; i >= 0; i--) if (span / VESTIGE_TIMING_PAT_STEPS[i] >= min_step) return i;
  for (int i = b + 1; i < VESTIGE_TIMING_PATTERNS; i++) if (span / VESTIGE_TIMING_PAT_STEPS[i] >= min_step) return i;
  return -1;
}
// The test's model of where an instance restarts (elapsed material samples
// from its start): hit i at round(span x L x i / n); a hit whose landing point
// (pos mod L) equals the read's current offset is no jump (the read is already
// at the loop's start). *last = the offset at the instance's end (!= 0: a
// return to the one follows).
static std::vector<long> WantRestarts(uint32_t mask, int nst, size_t L, long* last, std::vector<long>* all = nullptr) {
  std::vector<long> w; long off = 0;
  for (int i = 1; i < nst; i++) if (mask & (1u << i)) {
    const long pos = (long)((double)kSpan * (double)L * i / nst + 0.5);
    if (all) all->push_back(pos);
    if (pos % (long)L != off) { w.push_back(pos); off = pos % (long)L; }
  }
  *last = off; return w;
}
struct InstStats {
  int inst = 0, played = 0, var = 0, eligible = 0, eligible_var = 0, add = 0, drop = 0, rot = 0, restarts = 0, inplace = 0;
  int bad_pat = 0, bad_base = 0, bad_var = 0, bad_hits = 0, bad_step0 = 0, var_twice = 0, rot_moved = 0, bad_ret = 0, bad_grid = 0;
  bool ok() const { return bad_pat + bad_base + bad_var + bad_hits + bad_step0 + var_twice + rot_moved + bad_ret + bad_grid == 0; }
};
// Every instance of slot s (play length L) against the model: the expected
// pattern (want, -1 = none), its base = the table mask in ONE hit-starting
// rotation fixed across the log, the instance = the base or exactly one
// variation (one hit added / dropped off step 0, or another hit-starting
// rotation) — classified from the masks, the module's own label must agree —
// never two variation instances running, step 0 always a hit, span passes each
// L long (rate1), restarts exactly at the model's points (rate1; else only
// their count) and nowhere else, a return at its end iff the model says so.
static InstStats CheckInst(const TrigLog& lg, size_t L, int want, bool rate1) {
  InstStats st;
  for (size_t p = 0; p < lg.inst.size(); p++) {
    const InstRec& r = lg.inst[p]; st.inst++;
    if ((int)r.passes.size() != kSpan) st.bad_grid++;
    if (rate1) for (size_t k = 0; k < r.passes.size(); k++) if (r.passes[k] != r.start + (long)(k * L)) st.bad_grid++;
    if (r.pat != want) st.bad_pat++;
    if (r.pat < 0) { if (!r.hits_el.empty() || r.ret) st.bad_hits++; continue; }
    st.played++;
    const int nst = VESTIGE_TIMING_PAT_STEPS[r.pat]; const uint32_t tm = v.timing_mask_[r.pat];
    if (!(tm & (1u << r.brot)) || r.base != RotMask(tm, nst, r.brot)) st.bad_base++;
    if (!(r.mask & 1u)) st.bad_step0++;
    if (p > 0 && lg.inst[p - 1].pat == r.pat && lg.inst[p - 1].brot != r.brot) st.rot_moved++;
    const uint32_t d = r.mask ^ r.base; int cls = 0;
    if (d == 0) cls = 0;
    else if (Pop(d) == 1 && !(d & 1u) && (d & r.mask)) cls = 1;          // added
    else if (Pop(d) == 1 && !(d & 1u) && (d & r.base)) cls = 2;          // dropped
    else { cls = -1; for (int h = 0; h < nst; h++) if ((tm & (1u << h)) && RotMask(tm, nst, h) == r.mask) cls = 3; }
    if (cls < 0 || cls != r.var) st.bad_var++;
    if (cls == 3 && (!(tm & (1u << r.rot)) || RotMask(tm, nst, r.rot) != r.mask)) st.bad_var++;
    if (cls > 0) { st.var++; if (cls == 1) st.add++; if (cls == 2) st.drop++; if (cls == 3) st.rot++; }
    if (p > 0) {
      if (lg.inst[p - 1].var == 0) { st.eligible++; if (cls > 0) st.eligible_var++; }
      else if (cls != 0) st.var_twice++;
    }
    long last = 0; std::vector<long> all;
    const std::vector<long> w = WantRestarts(r.mask, nst, L, &last, &all);
    st.restarts += (int)r.hits_el.size(); st.inplace += (int)(all.size() - w.size());
    if (r.ret != (last != 0)) st.bad_ret++;
    if (w.size() != r.hits_el.size()) { st.bad_hits++; continue; }
    if (rate1) for (size_t k = 0; k < w.size(); k++) if (r.hits_el[k] != w[k]) st.bad_hits++;
  }
  return st;
}
static void PrintStats(const char* what, const InstStats& st) {
  printf("      %s: %d instances, %d played, %d restarts, %d hits in place; variations %d (add %d, drop %d, rotation %d); bad: pattern %d base %d variation %d step0 %d hits %d return %d grid %d twice %d rotation-moved %d\n",
         what, st.inst, st.played, st.restarts, st.inplace, st.var, st.add, st.drop, st.rot, st.bad_pat, st.bad_base, st.bad_var,
         st.bad_step0, st.bad_hits, st.bad_ret, st.bad_grid, st.var_twice, st.rot_moved);
}
// Forward, rate 1: the output around every point where the read runs on
// seamlessly — a pass wrap inside the span (read not on the timeline), or the
// read crossing the loop's seam inside a segment longer than a pass — equals
// the loop material around the read position there (lag 0, c > 0.99).
static float seam_ref = 1.f;                                  // c of a natural (level 0) wrap window
static void SeamlessWindows(const TrigLog& lg, int s, size_t L, int* wrap_ok, int* wrap_tot, int* seam_ok, int* seam_tot, float* seam_worst) {
  *wrap_ok = *wrap_tot = *seam_ok = *seam_tot = 0; *seam_worst = 1.f;
  for (const InstRec& r : lg.inst) {
    if (r.pat < 0) continue;
    const long span = (long)(kSpan * L);
    std::vector<long> rs = r.hits_el;                        // restart points; segment [a, b)
    std::vector<long> bounds; bounds.push_back(0); for (long h : rs) bounds.push_back(h); bounds.push_back(span);
    for (size_t g = 0; g + 1 < bounds.size(); g++) {
      const long a = bounds[g], b = bounds[g + 1];
      // read(E) = (E - a) mod L for a hit segment; the first segment is the timeline.
      for (long c = a + 1; c < b; c++) {
        const bool pass_wrap = (c % (long)L == 0), seam = (g > 0 && (c - a) % (long)L == 0);
        if (!(pass_wrap || seam)) continue;
        if (pass_wrap && seam) continue;                     // both at once = on the timeline's own wrap
        if (pass_wrap && g == 0) continue;                   // timeline's own wrap
        if (c - a < 1080 || b - c < 2520) continue;          // restart-free window
        const long rp = (g == 0) ? c % (long)L : (c - a) % (long)L;
        std::vector<float> ref(2400); for (int j = 0; j < 2400; j++) ref[j] = v.slab_[s][((rp - 480 + j) % (long)L + (long)L) % (long)L];
        float cc = 0.f; const int lagv = BestLagRef(r.start + c - 480, ref, 50, &cc);
        // (Across the loop's own seam the reference is the natural wrap's
        // correlation, measured at level 0: the capture seam crossfade.)
        const bool good = (lagv == 0 && cc > (pass_wrap ? 0.99f : seam_ref - 0.002f));
        if (!pass_wrap && cc < *seam_worst) *seam_worst = cc;
        if (pass_wrap) { (*wrap_tot)++; if (good) (*wrap_ok)++; } else { (*seam_tot)++; if (good) (*seam_ok)++; }
      }
    }
  }
}

static void TestTimingError() {
  printf("-- stage 3: the TIMING error (a Euclidean groove over %d pass(es))\n", kSpan);
  // The generated patterns are the table's rhythms. Bjorklund's output is a
  // ROTATION of the written form for some (the written forms follow different
  // conventions); an event draws its rotation uniformly from the hit-starting
  // ones, so what can be played is exactly the table's set.
  { const char* written[VESTIGE_TIMING_PATTERNS] = {"x...x...", "x..x..x..x..", "x..x..x.", "x..x.x..x.x.", "x.x.x.",
                                                     "x.xx.x.xx.x.", "x.xx.xx.", "x.xx.x", "x.xxxx", "x.xxxxxx"};
    bool ok = true, rot_ok = true;
    for (int p = 0; p < VESTIGE_TIMING_PATTERNS; p++) {
      const int k = VESTIGE_TIMING_PAT_HITS[p], nst = VESTIGE_TIMING_PAT_STEPS[p];
      const uint32_t m = v.timing_mask_[p];
      int hits = 0; for (int i = 0; i < nst; i++) if (m & (1u << i)) hits++;
      bool found = false, all_start = true;
      for (int h = 0; h < nst; h++) if (m & (1u << h)) {
        const uint32_t r = RotMask(m, nst, h);
        if (!(r & 1u)) all_start = false;
        if (MaskStr(r, nst) == written[p]) found = true;
      }
      printf("      E(%d,%2d) generated %-13s written %-13s %s\n", k, nst, MaskStr(m, nst).c_str(), written[p],
             MaskStr(m, nst) == written[p] ? "same" : (found ? "same rhythm, other rotation" : "DIFFERENT"));
      if (hits != k || !found || !(m & 1u) || m >> nst) ok = false;
      if (!all_start) rot_ok = false;
    }
    Check(ok, "every generated pattern is the table's rhythm: k hits in n steps, the written rotation among its hit-start rotations");
    Check(rot_ok, "every rotation an event can pick starts with a hit"); }

  // The level -> base pattern mapping.
  { bool ok = true;
    for (float lv : {0.001f, 0.05f, 0.3f, 4.f / 9.f, 0.5f, 0.6f, 0.95f, 1.f})
      if (Vestige::TimingBaseIndex(lv) != WantBase(lv)) ok = false;
    Check(ok && Vestige::TimingBaseIndex(0.001f) == 0 && Vestige::TimingBaseIndex(1.f) == VESTIGE_TIMING_PATTERNS - 1,
          "base pattern = list position round(L x (N-1)): smallest level -> E(2,8), full -> E(7,8)"); }
  // Segments over the span: every base pattern (any rotation) keeps each
  // stretch between hits within one pass; which variations exceed it.
  { bool ok = true; std::string over;
    for (int p = 0; p < VESTIGE_TIMING_PATTERNS; p++) {
      const int nst = VESTIGE_TIMING_PAT_STEPS[p]; const uint32_t m = v.timing_mask_[p];
      int gap = 0, run = 0; for (int i = 1; i <= 2 * nst; i++) { if (m & (1u << (i % nst))) { if (run + 1 > gap) gap = run + 1; run = 0; } else run++; }
      if (gap * kSpan > nst) ok = false;
      int dgap = 0;                                          // worst gap with one hit dropped
      for (int h = 1; h < nst; h++) if (m & (1u << h)) {
        const uint32_t dm = m & ~(1u << h); int g2 = 0, r2 = 0;
        for (int i = 1; i <= 2 * nst; i++) { if (dm & (1u << (i % nst))) { if (r2 + 1 > g2) g2 = r2 + 1; r2 = 0; } else r2++; }
        if (g2 > dgap) dgap = g2;
      }
      if (dgap * kSpan > nst) { char b[48]; snprintf(b, sizeof b, "E(%d,%d) %.2f  ", VESTIGE_TIMING_PAT_HITS[p], nst, (double)dgap * kSpan / nst); over += b; }
    }
    printf("      span %d: longest stretch between hits of any base <= 1 pass: %s; a dropped hit makes it > 1 pass (passes) in: %s\n",
           kSpan, ok ? "yes" : "NO", over.empty() ? "none" : over.c_str());
    Check(ok, "every base pattern over the span: no stretch between hits longer than one pass"); }

  // Level 0: nothing drawn.
  Reset(); cs.sw[0] = 0; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  CapRec r{}; noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  const int s = r.s; const size_t Q = r.Q;
  RunFor(0.8f);
  long grid0 = 0;
  { const uint32_t rng0 = v.timing_rng_, t0 = v.timing_trigs_, p0 = v.timing_patterns_;
    blk = 1; const int32_t pp = v.pass_[s]; while (v.pass_[s] == pp) RunFor(1.f / sr); grid0 = n - 1;
    RunFor(3.0f); Realign();
    // The natural wrap: the window across the loop's own seam, for reference.
    seam_ref = 1.f;
    for (int k = 1; k <= 4; k++) {
      std::vector<float> ref(2400); for (int j = 0; j < 2400; j++) ref[j] = v.slab_[s][((long)Q - 480 + j) % (long)Q];
      float cc = 0.f; const int lagv = BestLagRef(grid0 + k * (long)Q - 480, ref, 50, &cc);
      if (lagv != 0) cc = 0.f; if (cc < seam_ref) seam_ref = cc;
    }
    printf("      level 0: window across the loop's own wrap = its material at lag 0 with c = %.4f (the capture seam crossfade)\n", seam_ref);
    Check(v.timing_rng_ == rng0 && v.timing_trigs_ == t0 && v.timing_patterns_ == p0 && v.trig_off_[s] == 0.f,
          "timing level 0: nothing drawn, no pattern, no restart, the read head is the timeline head"); }

  // Level 1: E(7,8) over the span; the grid holds, restarts only at hits.
  v.err_level_[0] = 1.f;
  const TrigLog lg = RunInst(s, 15);
  const InstStats st1 = CheckInst(lg, Q, WantBase(1.f), true);
  bool grid_ok = true;
  for (const InstRec& ir : lg.inst) for (long ps : ir.passes) if ((ps - grid0) % (long)Q != 0) grid_ok = false;
  for (size_t p = 0; p < lg.inst.size(); p++) if ((lg.inst[p].start - grid0) % (long)(kSpan * Q) != 0 && ((grid0 - lg.inst[p].start) % (long)(kSpan * Q)) != 0) {}
  PrintStats("level 1 (E(7,8))", st1);
  printf("      level 1: base %s (rotation from hit %d), grid %s, read head out of range %ld\n",
         MaskStr(lg.inst[0].base, 8).c_str(), lg.inst[0].brot, grid_ok ? "unchanged" : "MOVED", lg.rh_bad);
  Check(st1.played == st1.inst && st1.ok() && st1.restarts > 0,
        "level 1: every instance plays E(7,8) (or one variation) over the span; restarts exactly at round(span x L x i / n), none elsewhere");
  Check(grid_ok && lg.rh_bad == 0, "the span never moves the grid: every pass Q long, on the no-error grid; the read head stays in the loop");
  { int al = 0; for (const InstRec& ir : lg.inst) if (ir.half0 < 1.f) al++;
    printf("      instance starts on the K1 half-speed cycle's start (half-speed head at 0): %d / %zu\n", al, lg.inst.size());
    Check(al == (int)lg.inst.size(), "forward: every instance starts where the K1 half-speed cycle does (the loop's own pass counter)"); }

  // After each restart the output is the loop's start (past the 5 ms fade);
  // after an instance the next one starts on the one.
  { int ok = 0, tot = 0; float worst = 1.f;
    for (const InstRec& ir : lg.inst) for (size_t k = 0; k < ir.hits_at.size() && tot < 40; k++) {
      const long next = (k + 1 < ir.hits_el.size()) ? ir.hits_el[k + 1] : (long)(kSpan * Q);
      const int N = (int)(next - ir.hits_el[k] - 600); if (N < 600) continue;
      const int NN = N < 2400 ? N : 2400;
      std::vector<float> ref(NN); for (int j = 0; j < NN; j++) ref[j] = v.slab_[s][480 + j];
      float c = 0.f; const int lagv = BestLagRef(ir.hits_at[k] + 480, ref, 50, &c);
      tot++; if (lagv == 0 && c > 0.99f) ok++; if (c < worst) worst = c;
    }
    printf("      after each restart: %d / %d windows = the loop's start at lag 0 (worst c = %.4f)\n", ok, tot, worst);
    Check(tot >= 20 && ok == tot, "after every restart the output is the loop's start (lag 0, c > 0.99)"); }
  { int ok = 0, tot = 0;
    for (size_t p = 1; p < lg.inst.size() && tot < 10; p++) {
      if (lg.inst[p - 1].pat < 0) continue;
      const long first = lg.inst[p].hits_el.empty() ? (long)Q : lg.inst[p].hits_el[0];
      const int N = (int)((first - 600 < 2400) ? first - 600 : 2400); if (N < 600) continue;
      std::vector<float> ref(N); for (int j = 0; j < N; j++) ref[j] = v.slab_[s][480 + j];
      float c = 0.f; const int lagv = BestLagRef(lg.inst[p].start + 480, ref, 50, &c);
      tot++; if (lagv == 0 && c > 0.99f) ok++;
    }
    Check(tot >= 5 && ok == tot, "after an instance the next one starts on the loop's one (lag 0)"); }

  // Seamless read through a pass wrap inside the span: E(3,8) (no hit on the
  // span's middle), also for the K1 half-speed head.
  { v.err_level_[0] = 2.f / 9.f;
    const TrigLog lw = RunInst(s, 20, 60.f, true);
    const InstStats sw = CheckInst(lw, Q, 2, true);
    int wo, wt, so, st_; float sw_c;
    SeamlessWindows(lw, s, Q, &wo, &wt, &so, &st_, &sw_c);
    PrintStats("level 2/9 (E(3,8))", sw);
    printf("      through an in-span pass wrap: %d / %d windows = the material the read runs on (lag 0); half-speed head: %ld wraps run through, %ld jumps outside restarts; read out of range %ld\n",
           wo, wt, lw.half_carry, lw.half_jumps, lw.rh_bad);
    Check(sw.ok() && sw.played == sw.inst, "level 2/9: every instance E(3,8) (or one variation) over the span");
    printf("      inner wraps with no hit: %ld, with a crossfade / restart in flight: %ld\n", lw.inner_wraps, lw.inner_xfades);
    Check(wt >= 15 && wo == wt && lw.inner_wraps >= 15 && lw.inner_xfades == 0,
          "a pass wrap inside the span: no restart, no crossfade, the read simply runs on (the loop material continues, lag 0)");
    Check(lw.half_carry >= 15 && lw.half_jumps == 0 && lw.rh_bad == 0, "K1 half-speed head: continuous through those wraps too (jumps only at restarts)"); }

  // E(2,8) over 2 passes: a hit exactly on every pass start = the untouched
  // loop (no restart, no return, the read stays on the timeline).
  { v.err_level_[0] = 0.05f;
    const uint32_t t0 = v.timing_trigs_, r0 = v.timing_returns_, ip0 = v.timing_inplace_;
    const TrigLog le = RunInst(s, 20);
    const InstStats se = CheckInst(le, Q, 0, true);
    int base_restarts = 0, base_n = 0; for (const InstRec& ir : le.inst) if (ir.var == 0) { base_n++; base_restarts += (int)ir.hits_el.size() + (ir.ret ? 1 : 0); }
    PrintStats("level 0.05 (E(2,8))", se);
    printf("      E(2,8): %d base instances, %d restarts/returns in them; in-place hits %u; all restarts %u, returns %u (from the variations)\n",
           base_n, base_restarts, v.timing_inplace_ - ip0, v.timing_trigs_ - t0, v.timing_returns_ - r0);
    Check(se.ok() && se.played == se.inst && base_n >= 10 && base_restarts == 0,
          kSpan == 2 ? "E(2,8) over 2 passes: its hit lands on every pass start = the untouched loop (no restart, no return)"
                     : "E(2,8): every instance as the model says"); }

  // K3 change mid-instance: the instance keeps its pattern through its inner
  // wrap; the next instance takes the new base; the rotation stays the loop's
  // drawn index (mod the hit count), so K3 back gives the same rotation again.
  { const auto hit_index = [](int pat, int h) { int k = 0; for (int i = 0; i < h; i++) if (v.timing_mask_[pat] & (1u << i)) k++; return k; };
    v.err_level_[0] = 1.f; RunInst(s, 1);
    blk = 1; while (v.trig_start_[s] != v.pass_[s] || v.cur_pat_[s] != 9) RunFor(1.f / sr);   // an E(7,8) instance began
    for (long j = 0; j < (long)Q / 2; j++) RunFor(1.f / sr);
    const int pat_a = v.cur_pat_[s], rot_a = v.base_rot_[s]; const uint32_t mask_a = v.cur_mask_[s]; const int32_t st_a = v.trig_start_[s];
    v.err_level_[0] = 6.f / 9.f;                            // -> E(5,8)
    bool kept = true;
    while (v.trig_start_[s] == st_a) { RunFor(1.f / sr); if (v.trig_start_[s] == st_a && (v.cur_pat_[s] != pat_a || v.cur_mask_[s] != mask_a)) kept = false; }
    const int pat_b = v.cur_pat_[s], rot_b = v.base_rot_[s];
    Realign();
    const TrigLog lb = RunInst(s, 4);
    v.err_level_[0] = 1.f;
    const TrigLog lc = RunInst(s, 1);
    const int rot_c = lc.inst.empty() ? -1 : lc.inst[0].brot;
    const uint32_t seed = v.rot_seed_[s];
    const int ia = hit_index(pat_a, rot_a), ib = hit_index(pat_b, rot_b);
    printf("      K3 1 -> 6/9 mid-instance: instance kept E(%d,%d) through its inner wrap = %s; next instance E(%d,%d); rotation hit index %d of 7 -> %d of 5 -> back %s (seed %% 7 = %u, %% 5 = %u)\n",
           VESTIGE_TIMING_PAT_HITS[pat_a], VESTIGE_TIMING_PAT_STEPS[pat_a], kept ? "yes" : "NO",
           VESTIGE_TIMING_PAT_HITS[pat_b], VESTIGE_TIMING_PAT_STEPS[pat_b], ia, ib, rot_c == rot_a ? "same" : "DIFFERENT", seed % 7u, seed % 5u);
    Check(kept && pat_a == 9 && pat_b == 6, "a K3 change applies at the next instance start, not mid-instance");
    const InstStats sb = CheckInst(lb, Q, 6, true);
    Check(sb.ok() && sb.played == sb.inst, "after the change every instance is the new base (or one variation of it)");
    Check(ia == (int)(seed % 7u) && ib == (int)(seed % 5u) && rot_c == rot_a,
          "rotation = the loop's drawn index modulo the hit count: kept across K3 changes, K3 back = the same rotation"); }

  // Variation rate and kinds, per INSTANCE (mid level: E(7,12)).
  { v.err_level_[0] = 0.5f;
    const TrigLog lv = RunInst(s, 200, 250.f);
    const InstStats sv = CheckInst(lv, Q, WantBase(0.5f), true);
    PrintStats("level 0.5 (E(7,12))", sv);
    const double p = VESTIGE_TIMING_VAR_PROB, rate = (double)sv.eligible_var / sv.eligible, sd = sqrt(p * (1 - p) / sv.eligible);
    const double e3 = sv.var / 3.0, sd3 = sqrt(sv.var * (1.0 / 3) * (2.0 / 3));
    printf("      variation rate on eligible instances (not right after a variation): %d / %d = %.3f (constant %.3f +- %.3f); overall %.3f\n",
           sv.eligible_var, sv.eligible, rate, p, 3 * sd, (double)sv.var / sv.inst);
    Check(sv.ok() && sv.played == sv.inst, "level 0.5: every instance = the base or exactly one variation (added / dropped / rotated), never two running");
    Check(fabs(rate - p) < 3 * sd, "variation rate per instance ~ VESTIGE_TIMING_VAR_PROB (on instances that may roll)");
    Check(fabs(sv.add - e3) < 3 * sd3 && fabs(sv.drop - e3) < 3 * sd3 && fabs(sv.rot - e3) < 3 * sd3,
          "the three variation kinds are ~uniform"); }
  // A rotation-symmetric base (E(3,6)): variations are only added / dropped
  // hits. A dropped hit stretches a segment past one pass: the read then runs
  // over the loop's seam by itself, seamlessly.
  { v.err_level_[0] = 4.f / 9.f;
    const TrigLog ly = RunInst(s, 80, 120.f, true);
    const InstStats sy = CheckInst(ly, Q, 4, true);
    int wo, wt, so, st_; float sw_c; SeamlessWindows(ly, s, Q, &wo, &wt, &so, &st_, &sw_c);
    PrintStats("level 4/9 (E(3,6), symmetric)", sy);
    printf("      E(3,6): %d / %d in-span wraps and %d / %d in-segment seam crossings (dropped hits) seamless (worst c %.4f vs natural wrap %.4f); half-speed jumps %ld; read out of range %ld\n",
           wo, wt, so, st_, sw_c, seam_ref, ly.half_jumps, ly.rh_bad);
    Check(sy.ok() && sy.var > 0 && sy.rot == 0, "symmetric base: variations are real changes (no rotation that sounds the same)");
    Check((kSpan == 1 || (sy.drop > 0 && st_ >= 1)) && so == st_ && wo == wt && ly.half_jumps == 0 && ly.rh_bad == 0,
          "a segment longer than a pass (dropped hit): the read wraps the loop by itself, seamless, in range");
    v.err_level_[0] = 0.f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // The loop's first instance: with the level already up, the pattern starts
  // with the loop (joined mid-pass: only the hits still ahead).
  for (long burst : {36000L, 25400L}) {                      // T 500: on its one / T 1 s: 1/2 round-down, joins late
    Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({burst == 36000L ? 500 : 1000}); RunFor(0.6f);
    v.err_level_[0] = 1.f;
    seen_acts = v.act_count_;
    const uint32_t p0 = v.timing_patterns_, t0 = v.timing_trigs_;
    CapRec q{}; noise_from = n; noise_to = n + burst; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    const int qs = q.s; const size_t L = q.Q;
    const int pat = v.cur_pat_[qs]; const uint32_t mask = v.cur_mask_[qs]; const int32_t st0 = v.trig_start_[qs];
    blk = 1; std::vector<long> at; uint32_t tc = v.timing_trigs_, tc_in = tc;
    while (v.trig_start_[qs] == st0) {
      tc_in = v.timing_trigs_;                                 // restarts inside this instance so far
      RunFor(1.f / sr); if (v.timing_trigs_ != tc) { tc = v.timing_trigs_; at.push_back((long)v.last_trig_at_); }
    }
    const uint32_t planned = v.timing_patterns_ - p0;          // incl. the next instance's
    if (v.timing_trigs_ != tc_in) at.pop_back();               // (a restart at the next instance's start sample)
    const uint32_t ntrig = tc_in - t0;                         // ALL restarts of this instance, incl. at activation
    long last = 0; std::vector<long> w = WantRestarts(mask, 8, L, &last);
    int want = 0, off = 0; size_t k = 0;
    for (long pos : w) {
      if (pos <= (long)q.phase) continue;
      want++;
      const long exp_at = q.act + (pos - (long)q.phase);
      if (k < at.size() && labs(at[k] - exp_at) > 1) off++; k++;
    }
    printf("      first instance (joined at phase %u of %zu): pattern %d planned at loop start, %u restarts in it (expected %d; %zu timed per sample, %d off), %u plans until the next instance\n",
           q.phase, L, pat, ntrig, want, at.size(), off, planned);
    Check(pat == 9 && (int)ntrig == want && (int)at.size() == want && off == 0 && planned == 2 && (burst == 36000L ? q.phase == 0 : q.phase > 0),
          burst == 36000L ? "the pattern starts with the loop: a loop entering on its one plays the whole instance"
                          : "a loop joining late (mid-pass) plays only the hits still ahead in its first instance, each once");
    v.err_level_[0] = 0.f; }

  // No click at any restart, on a sine loop (restarts land mid-cycle); also
  // with the K1 half-speed version up (through the in-span wraps).
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true; WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(1.0f);
  { const int q = r.s;
    maxd = 0.f; RunFor(2.0f); const float st = maxd;
    size_t nr = 0; int nv = 0;
    for (float lv : {1.f, 0.5f, 2.f / 9.f}) {
      v.err_level_[0] = lv; maxd = 0.f;
      const TrigLog lc = RunInst(q, 6);
      for (const InstRec& ir : lc.inst) { nr += ir.hits_at.size(); if (ir.var) nv++; }
    }
    const float sj = maxd;
    const float khalf = 0.5f - (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = khalf; v.err_level_[0] = 0.f; RunFor(1.5f); maxd = 0.f; RunFor(2.0f); const float sth = maxd;
    v.err_level_[0] = 2.f / 9.f; maxd = 0.f; const TrigLog lh = RunInst(q, 6); const float sh = maxd;
    printf("      sine loop max step: steady %.5f | 18 instances (%d variations), %zu restarts: %.5f (bound %.5f) | K1 half side: steady %.5f, E(3,8) %.5f (bound %.5f)\n",
           st, nv, nr, sj, 1.5f * st, sth, sh, 1.5f * sth);
    Check(nr >= 40 && sj <= 1.5f * st, "pattern restarts and returns: no step above 1.5x steady (5 ms restarts)");
    Check(lh.inst.size() >= 5 && sh <= 1.5f * sth, "K1 half-speed version through the in-span wraps: no step above 1.5x steady");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; }

  // Reverse: every restart jumps to the loop's END.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; cs.knob[1] = 0.2f; RunFor(0.3f); Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  RunFor(0.8f);
  { const int q = r.s; const long L = (long)r.Q;
    v.err_level_[0] = 1.f;
    const TrigLog lr = RunInst(q, 6);
    const InstStats sr_ = CheckInst(lr, (size_t)L, 9, true);
    int ok = 0, tot = 0;
    for (const InstRec& ir : lr.inst) for (size_t k = 0; k < ir.hits_at.size() && tot < 30; k++) {
      const long next = (k + 1 < ir.hits_el.size()) ? ir.hits_el[k + 1] : kSpan * L;
      const int N = (int)(next - ir.hits_el[k] - 600); if (N < 600) continue;
      const int NN = N < 2400 ? N : 2400;
      std::vector<float> ref(NN); for (int j = 0; j < NN; j++) ref[j] = v.slab_[q][L - 1 - 480 - j];
      float c = 0.f; const int lagv = BestLagRef(ir.hits_at[k] + 480, ref, 50, &c);
      tot++; if (lagv == 0 && c > 0.99f) ok++;
    }
    PrintStats("reverse, level 1", sr_);
    int al = 0; for (const InstRec& ir : lr.inst) if (ir.half0 > (float)L - 2.f) al++;
    printf("      reverse: %d / %d restarts play the loop's END backward (lag 0); instance starts on the half-speed cycle's start (its tail): %d / %zu; read out of range %ld\n",
           ok, tot, al, lr.inst.size(), lr.rh_bad);
    Check(v.rev_play_ && sr_.ok() && sr_.played == sr_.inst && tot >= 10 && ok == tot && lr.rh_bad == 0,
          "reverse: every restart jumps back to the loop's end");
    Check(al == (int)lr.inst.size(), "reverse: every instance starts where the reverse K1 half-speed cycle does (from the tail)");
    v.err_level_[0] = 0.f; cs.knob[1] = 0.85f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // K1 midpoint: both streams restart together.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  { const int q = r.s; const long L = (long)r.Q;
    const float kmid = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = kmid; RunFor(1.5f);
    v.err_level_[0] = 0.5f;                                   // mid pattern: steps long enough to measure
    const TrigLog lk = RunInst(q, 5);
    int ok = 0, tot = 0;
    for (const InstRec& ir : lk.inst) for (size_t k = 0; k < ir.hits_at.size() && tot < 20; k++) {
      const long next = (k + 1 < ir.hits_el.size()) ? ir.hits_el[k + 1] : kSpan * L;
      const int N = (int)(next - ir.hits_el[k] - 600); if (N < 600) continue;
      const int NN = N < 2400 ? N : 2400;
      std::vector<float> rc(NN), rs(NN);
      for (int j = 0; j < NN; j++) { rc[j] = v.slab_[q][480 + j]; rs[j] = v.slab_[q][(2 * (480 + j)) % L]; }
      float gc = 0, gs = 0, res = 0; Split(ir.hits_at[k] + 480, rc, rs, &gc, &gs, &res);
      tot++; if (fabsf(gc - v.g_c_) < 0.05f && fabsf(gs - v.g_sp_) < 0.05f && res < 0.05f) ok++;
    }
    printf("      K1 midpoint, level 0.5: %d / %d restarts = clean from the start + double speed from the start\n", ok, tot);
    Check(tot >= 6 && ok == tot, "K1 midpoint: clean and speed versions restart together");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // Stretch at a rate != 1: patterns laid out on the span, lengths unchanged.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  { const int q = r.s;
    Taps({700}); RunFor(1.0f);
    v.err_level_[0] = 1.f;
    const TrigLog ls = RunInst(q, 5);
    const long Lt = (long)GridQuantize::Boundary(v.div_[q], v.period_);
    const InstStats ss = CheckInst(ls, v.PlayLen(q), 9, false);   // restart COUNT per instance
    int len_bad = 0;
    for (const InstRec& ir : ls.inst) for (size_t k = 0; k < ir.passes.size(); k++) if (labs(ir.passes[k] - ir.start - (long)k * Lt) > 1) len_bad++;
    for (size_t p = 1; p < ls.inst.size(); p++) if (labs(ls.inst[p].start - ls.inst[p - 1].start - kSpan * Lt) > 1) len_bad++;
    // Restart times in output samples ~ the model's points / rate.
    int tbad = 0;
    for (const InstRec& ir : ls.inst) { if (ir.pat < 0) continue; long last = 0;
      const std::vector<long> w = WantRestarts(ir.mask, VESTIGE_TIMING_PAT_STEPS[ir.pat], v.PlayLen(q), &last);
      for (size_t k = 0; k < w.size() && k < ir.hits_el.size(); k++)
        if (labs(ir.hits_el[k] - (long)((double)w[k] / v.rho_d_[q])) > 2) tbad++; }
    printf("      stretch at rate %.4f: pass %ld, %d patterns in %d instances, restart counts off %d, times off %d, lengths off %d\n",
           v.rho_d_[q], Lt, ss.played, ss.inst, ss.bad_hits, tbad, len_bad);
    Check(v.rho_d_[q] != 1.0 && ss.ok() && ss.played == ss.inst && tbad == 0 && len_bad == 0,
          "stretch at rate != 1: restarts on i/n of the span, every pass stays d x T_now");
    v.err_level_[0] = 0.f; }

  // Hold: patterns continue. Freeze: never.
  { Hold(); v.err_level_[0] = 1.f;
    const int q = FirstLive(); const TrigLog lh = RunInst(q, 3);
    const InstStats sh = CheckInst(lh, v.PlayLen(q), 9, false);
    Check(v.held_ && sh.played >= 2 && sh.ok(), "held loop: patterns play too");
    v.err_level_[0] = 0.f; Unhold(); }
  { Reset(); cs.sw[0] = 2; cs.knob[4] = 0.5f; RunFor(1.0f);
    seen_acts = v.act_count_;
    v.err_level_[0] = 1.f;
    const uint32_t t0 = v.timing_trigs_, p0 = v.timing_patterns_;
    noise_from = n; noise_to = n + 9600; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(3.0f);
    Check(Vestige::PoolOf(q.s) == Vestige::kPoolFreeze && v.timing_trigs_ == t0 && v.timing_patterns_ == p0,
          "freeze: the timing error never touches it (also not at its start)");
    v.err_level_[0] = 0.f; cs.sw[0] = 0; RunFor(1.0f); }

  // C re-cut with an instance in flight: a new length landing on the span's
  // inner wrap drops the instance's remaining hits (back to the one); the read
  // head never leaves the loop.
  { Reset(); v.follow_mode_cfg_ = 2; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 36000; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(0.5f);
    v.err_level_[0] = 2.f / 9.f;
    const uint32_t ab0 = v.timing_aborts_, ra0 = v.recut_applied_; const long bad0 = bad; maxd = 0.f;
    long rh_bad = 0, inner = 0;
    for (int i = 0; i < 8; i++) {
      Taps({(i & 1) ? 500 : 330});
      const TrigLog lt = RunInst(q.s, 2, 10.f);
      rh_bad += lt.rh_bad;
    }
    inner = (long)(v.timing_aborts_ - ab0);
    printf("      C re-cuts with the pattern running: %u length changes applied, %ld landed on an inner wrap (instance dropped); read out of range %ld, bad samples %ld, max step %.4f\n",
           v.recut_applied_ - ra0, inner, rh_bad, bad - bad0, maxd);
    Check(v.recut_applied_ - ra0 >= 6 && (kSpan == 1 || inner >= 1) && rh_bad == 0 && bad == bad0,
          "C re-cut inside a span: the instance is dropped at that wrap, the read head stays in the (new) loop");
    v.err_level_[0] = 0.f; v.follow_mode_cfg_ = 0; RunFor(0.5f); }

  // Short-loop guard on the span's step: a base whose step is under
  // VESTIGE_TIMING_MIN_STEP_MS falls back to the densest pattern below it that
  // fits (nothing below: the sparsest above); nothing fits: no pattern.
  { struct G { int tap_ms; long burst; };
    for (const G& g : {G{100, 9000}, G{100, 3300}, G{100, 2100}}) {
      Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({g.tap_ms}); RunFor(0.6f);
      seen_acts = v.act_count_;
      noise_from = n; noise_to = n + g.burst; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
      RunFor(0.3f);
      std::string line; bool ok = true; int nfb = 0, nnone = 0;
      for (float lv : {0.05f, 1.f / 9.f, 5.f / 9.f, 6.f / 9.f, 1.f}) {
        v.err_level_[0] = lv;
        const int want = WantFit(lv, (double)q.Q);
        const uint32_t fb0 = v.timing_fallbacks_, sk0 = v.timing_skipped_, t0 = v.timing_trigs_;
        const TrigLog lt = RunInst(q.s, 6);
        const InstStats sg = CheckInst(lt, q.Q, want, true);
        if (!sg.ok() || lt.inst.size() < 5) ok = false;
        if (want < 0) { nnone++; if (v.timing_skipped_ - sk0 < 5 || v.timing_trigs_ != t0) ok = false; }
        else if (sg.played != sg.inst) ok = false;
        if (want >= 0 && want != WantBase(lv)) { nfb++; if (v.timing_fallbacks_ - fb0 < (uint32_t)sg.inst) ok = false; }
        char b[64]; snprintf(b, sizeof b, "E(%d,%d)->%s ", VESTIGE_TIMING_PAT_HITS[WantBase(lv)], VESTIGE_TIMING_PAT_STEPS[WantBase(lv)],
                             want >= 0 ? (std::string("E(") + std::to_string(VESTIGE_TIMING_PAT_HITS[want]) + "," + std::to_string(VESTIGE_TIMING_PAT_STEPS[want]) + ")").c_str() : "none");
        line += b;
      }
      printf("      %.0f ms loop (%zu), span step: %s\n", q.Q / 48.0, q.Q, line.c_str());
      char msg[160]; snprintf(msg, sizeof msg, "short-loop guard, %.0f ms loop (step = span x pass / n): %d fallbacks, %d levels with nothing that fits", q.Q / 48.0, nfb, nnone);
      Check(ok && (q.Q == 2400 ? nnone == 5 : nfb >= 2), msg);
      v.err_level_[0] = 0.f;
    } }

  // 3 voices, each with its own rotation and its own variation rolls; budget
  // and the PHYSICAL pool.
  Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 1; cs.knob[0] = 0.5f; Taps({400}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[3] = {9000, 12500, 14000};              // -> 200 / 267 / 300 ms: all take E(7,8) at level 1
  for (int i = 0; i < 3; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.2f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  { int slots[3]; int ns = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS && ns < 3; q++) if (v.active_[q] && !v.dying_[q]) slots[ns++] = q;
    for (int i = 0; i < ns; i++) printf("      voice %d: loop %zu (%.0f ms)\n", slots[i], v.PlayLen(slots[i]), v.PlayLen(slots[i]) / 48.0);
    v.err_level_[0] = 1.f;
    blk = 1;
    int32_t pp[3]; for (int i = 0; i < ns; i++) pp[i] = v.pass_[slots[i]];
    std::vector<std::vector<int>> vars(3); uint32_t bases[3] = {0, 0, 0}; bool base_moved = false;
    for (long j = 0; j < 48000L * 12; j++) {
      RunFor(1.f / sr);
      for (int i = 0; i < ns; i++) if (v.pass_[slots[i]] != pp[i]) {
        pp[i] = v.pass_[slots[i]];
        if (v.trig_start_[slots[i]] != v.pass_[slots[i]]) continue;   // inner wrap: same instance
        vars[i].push_back(v.cur_var_[slots[i]]);
        if (vars[i].size() > 1 && v.base_mask_[slots[i]] != bases[i]) base_moved = true;
        bases[i] = v.base_mask_[slots[i]];
      }
    }
    Realign();
    const bool rot_differ = ns == 3 && (bases[0] != bases[1] || bases[1] != bases[2]);
    int vdiff = 0, nvar[3] = {0, 0, 0}; const size_t m = std::min(vars[0].size(), std::min(vars[1].size(), vars[2].size()));
    for (size_t k = 0; k < m; k++) if ((vars[0][k] != 0) != (vars[1][k] != 0) || (vars[1][k] != 0) != (vars[2][k] != 0)) vdiff++;
    for (int i = 0; i < ns; i++) for (int x : vars[i]) if (x) nvar[i]++;
    printf("      3 voices, level 1, 12 s: bases %s / %s / %s; variations %d / %d / %d; variation instances differ in %d of %zu instance indices\n",
           MaskStr(bases[0], 8).c_str(), MaskStr(bases[1], 8).c_str(), MaskStr(bases[2], 8).c_str(), nvar[0], nvar[1], nvar[2], vdiff, m);
    Check(rot_differ && !base_moved, "3 voices: each keeps its own rotation of the same groove (they differ)");
    Check(nvar[0] > 0 && nvar[1] > 0 && nvar[2] > 0 && vdiff > 0, "3 voices roll their variations independently");
    // Step rounding on a loop whose span does not divide by n (25600 / 12).
    { int q12 = -1; for (int i = 0; i < ns; i++) if ((kSpan * v.PlayLen(slots[i])) % 12 != 0) q12 = slots[i];
      if (q12 < 0) q12 = slots[1];
      v.err_level_[0] = 0.5f;
      const TrigLog lq = RunInst(q12, 20, 60.f);
      const InstStats sq = CheckInst(lq, v.PlayLen(q12), WantFit(0.5f, (double)v.PlayLen(q12)), true);
      printf("      loop %zu (span %zu not a multiple of 12): %d instances E(%d,%d), restarts off the model: %d\n", v.PlayLen(q12), kSpan * v.PlayLen(q12), sq.played,
             VESTIGE_TIMING_PAT_HITS[lq.inst[0].pat], VESTIGE_TIMING_PAT_STEPS[lq.inst[0].pat], sq.bad_hits);
      Check((kSpan * v.PlayLen(q12)) % 12 != 0 && sq.played >= 10 && sq.ok() && VESTIGE_TIMING_PAT_STEPS[lq.inst[0].pat] == 12,
            "restarts land at round(span x L x i / n) exactly, also when n does not divide the span"); }
    const float kmid = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = kmid; RunFor(1.5f);
    v.err_level_[0] = 1.f;
    max_grains = 0; max_counted = 0; const uint32_t d0 = v.grain_cap_drops_, pf0 = v.pool_full_, t0 = v.timing_trigs_;
    blk = 1; RunFor(10.0f); Realign();
    printf("      3 voices (200-300 ms loops), K1 midpoint, level 1, 10 s: %u restarts; counted max %d (budget 12), PHYSICAL max %d of %d, cap refusals %u, pool exhausted %u\n",
           v.timing_trigs_ - t0, max_counted, max_grains, VESTIGE_GRAINS, v.grain_cap_drops_ - d0, v.pool_full_ - pf0);
    Check(max_counted <= 12 && v.grain_cap_drops_ - d0 == 0, "dense patterns, 3 voices + K1 midpoint: counted grains within 12, none refused");
    Check(v.pool_full_ - pf0 == 0 && max_grains < VESTIGE_GRAINS, "dense patterns: the physical grain pool never runs out");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f); }
  Unhold();
}

// ---------------------------------------------------------------------------
// Timing mode 1: SLICE REARRANGEMENT.
struct SlRec { long start; int n; int ord[VESTIGE_TIMING_SLICE_MAX]; int arr[VESTIGE_TIMING_SLICE_MAX]; uint32_t rmask; bool ret = false; int planned = 0;
               std::vector<long> jumps_el; std::vector<long> jumps_at; };
struct SlLog { std::vector<SlRec> p; long rh_bad = 0, half_jumps = 0; };
static SlLog RunSl(int s, int passes, float max_secs = 60.f, bool track_half = false) {
  SlLog lg; blk = 1;
  int32_t pp = v.pass_[s]; uint32_t tc = v.timing_trigs_, rc = v.timing_returns_;
  float hprev = v.SpeedHead(s, 0.5f);
  const long end = n + (long)(max_secs * sr);
  while (n < end && (int)lg.p.size() < passes + 1) {
    RunFor(1.f / sr);
    const bool trig = v.timing_trigs_ != tc, ret = v.timing_returns_ != rc;
    if (v.pass_[s] != pp) {
      pp = v.pass_[s];
      if (!lg.p.empty()) lg.p.back().ret = ret;
      SlRec r; r.start = n - 1; r.n = v.sl_n_[s]; r.rmask = v.sl_rand_mask_[s]; r.planned = v.trig_cnt_[s];
      for (int i = 0; i < VESTIGE_TIMING_SLICE_MAX; i++) { r.ord[i] = v.sl_order_[s][i]; r.arr[i] = v.sl_arr_[s][i]; }
      lg.p.push_back(r);
    } else if (trig && v.last_trig_slot_ == s && !lg.p.empty()) {
      lg.p.back().jumps_el.push_back((n - 1) - lg.p.back().start); lg.p.back().jumps_at.push_back(n - 1);
    }
    tc = v.timing_trigs_; rc = v.timing_returns_;
    const float L = (float)v.PlayLen(s), h = v.ReadHead(s);
    if (!(h >= 0.f && h < L)) lg.rh_bad++;
    if (track_half) {
      const float h2 = v.SpeedHead(s, 0.5f);
      float d = h2 - hprev; if (d > 0.5f * L) d -= L; if (d < -0.5f * L) d += L;
      if (!trig && !ret && fabsf(d - 0.5f) > 1e-3f) lg.half_jumps++;
      hprev = h2;
    }
  }
  Realign();
  if (!lg.p.empty()) lg.p.pop_back();
  return lg;
}
static long SlB(size_t L, int nsl, int i) { return (long)((double)L * i / nsl + 0.5); }
// The test's model of the slice count for a pass (output samples per pass).
static int WantN(double pass_out, int* tier) {
  const double min_step = VESTIGE_TIMING_MIN_STEP_MS * 0.001 * sr;
  int nsl = VESTIGE_TIMING_SLICES, t = 0;
  while (nsl >= 2) { if (pass_out / nsl >= min_step) { *tier = t; return nsl; } nsl /= 2; t++; }
  return 0;
}
// The model's arrangement of loop s for level / tier: the first round(L*(N-1))
// steps of its drawn priority take their drawn replacement.
static void WantArr(int s, float level, int nsl, int tier, int* arr) {
  for (int i = 0; i < nsl; i++) arr[i] = i;
  int cnt = (int)(level * (float)(nsl - 1) + 0.5f); if (cnt > nsl - 1) cnt = nsl - 1;
  for (int k = 0; k < cnt; k++) { const int st = v.sl_prio_[s][tier][k]; arr[st] = v.sl_repl_[s][tier][st]; }
}
// Model of one pass's jumps (elapsed material from the pass start) and each
// step's read start (material position; reverse: on the reversed loop).
static void WantJumps(const int* ord, int nsl, size_t L, std::vector<long>* jumps, std::vector<long>* rstart) {
  jumps->clear(); rstart->assign(nsl, 0);
  for (int i = 1; i < nsl; i++) {
    if (ord[i] == (ord[i - 1] + 1) % nsl) (*rstart)[i] = ((*rstart)[i - 1] + SlB(L, nsl, i) - SlB(L, nsl, i - 1)) % (long)L;
    else { jumps->push_back(SlB(L, nsl, i)); (*rstart)[i] = SlB(L, nsl, ord[i]); }
  }
}
struct SlStats { int passes = 0, played = 0, steps = 0, rsteps = 0, rpasses = 0, jumps = 0, naturals = 0, swapped = 0;
                 int hist[VESTIGE_TIMING_SLICE_MAX] = {0};    // random slice - planned slice (mod N)
                 int bad_n = 0, bad_arr = 0, bad_step0 = 0, bad_var = 0, bad_jumps = 0, bad_ret = 0, arr_moved = 0, bad_grid = 0;
                 bool ok() const { return bad_n + bad_arr + bad_step0 + bad_var + bad_jumps + bad_ret + arr_moved + bad_grid == 0; } };
static SlStats CheckSl(const SlLog& lg, int s, size_t L, float level, int want_n, int tier, bool rate1) {
  SlStats st; int want_arr[VESTIGE_TIMING_SLICE_MAX];
  for (size_t p = 0; p < lg.p.size(); p++) {
    const SlRec& r = lg.p[p]; st.passes++;
    if (rate1 && p > 0 && r.start - lg.p[p - 1].start != (long)L) st.bad_grid++;
    if (r.n != want_n) { st.bad_n++; continue; }
    if (r.n == 0) { if (!r.jumps_el.empty() || r.ret) st.bad_jumps++; continue; }
    st.played++;
    WantArr(s, level, r.n, tier, want_arr);
    int sw = 0; for (int i = 0; i < r.n; i++) { if (r.arr[i] != want_arr[i]) st.bad_arr++; if (r.arr[i] != i) sw++; }
    if (sw != (int)(level * (float)(r.n - 1) + 0.5f)) st.bad_arr++;
    st.swapped = sw;
    if (p > 0 && lg.p[p - 1].n == r.n) for (int i = 0; i < r.n; i++) if (lg.p[p - 1].arr[i] != r.arr[i]) { st.arr_moved++; break; }
    if (r.ord[0] != 0 || r.arr[0] != 0) st.bad_step0++;
    // Random steps, from the played vs planned slices: the module's own mask
    // must agree (a random slice equal to the planned one would show as a
    // masked step that does not differ), never step 0.
    uint32_t dm = 0; for (int i = 0; i < r.n; i++) if (r.ord[i] != r.arr[i]) { dm |= 1u << i; st.hist[((r.ord[i] - r.arr[i]) % r.n + r.n) % r.n]++; }
    if (dm != r.rmask) st.bad_var++;
    if (dm & 1u) st.bad_step0++;
    st.steps += r.n - 1; st.rsteps += Pop(dm); if (dm) st.rpasses++;
    std::vector<long> w, rs; WantJumps(r.ord, r.n, L, &w, &rs);
    st.jumps += (int)w.size(); st.naturals += r.n - 1 - (int)w.size();
    if (r.ret != (r.ord[r.n - 1] != r.n - 1)) st.bad_ret++;
    if (r.planned != (int)w.size()) st.bad_jumps++;              // the module's own plan: no jump at a continuation
    if (w.size() != r.jumps_el.size()) { st.bad_jumps++; continue; }
    if (rate1) for (size_t k = 0; k < w.size(); k++) if (r.jumps_el[k] != w[k]) st.bad_jumps++;
  }
  return st;
}
static void PrintSl(const char* what, const SlStats& st) {
  printf("      %s: %d passes, %d rearranged (%d steps swapped), %d jumps, %d steps ran on; random steps %d of %d (in %d passes); bad: slices %d arrangement %d step0 %d random %d jumps %d return %d moved %d grid %d\n",
         what, st.passes, st.played, st.swapped, st.jumps, st.naturals, st.rsteps, st.steps, st.rpasses, st.bad_n, st.bad_arr, st.bad_step0, st.bad_var,
         st.bad_jumps, st.bad_ret, st.arr_moved, st.bad_grid);
}
static std::string ArrStr(const int* a, int nsl) { std::string r; for (int i = 0; i < nsl; i++) r += (char)('A' + a[i]); return r; }
// Every step's output = its slice at lag 0 (rate 1; windows past the 5 ms fade,
// not across the loop's seam). rev: reversed-loop slices.
static void StepWindows(const SlLog& lg, int s, size_t L, bool rev, int* ok, int* tot, float* worst) {
  *ok = *tot = 0; *worst = 1.f;
  for (const SlRec& r : lg.p) {
    if (r.n == 0) continue;
    std::vector<long> w, rs; WantJumps(r.ord, r.n, L, &w, &rs);
    for (int i = 0; i < r.n; i++) {
      const long len = SlB(L, r.n, i + 1) - SlB(L, r.n, i);
      const int N = (int)std::min(2400L, len - 600); if (N < 400) continue;
      if (rs[i] + 480 + N >= (long)L) continue;             // across the seam
      std::vector<float> ref(N);
      for (int j = 0; j < N; j++) { const long m = rs[i] + 480 + j; ref[j] = rev ? v.slab_[s][(long)L - 1 - m] : v.slab_[s][m]; }
      float c = 0.f; const int lagv = BestLagRef(r.start + SlB(L, r.n, i) + 480, ref, 50, &c);
      (*tot)++; if (lagv == 0 && c > 0.99f) (*ok)++; if (c < *worst) *worst = c;
    }
  }
}

// ---------------------------------------------------------------------------
// Stage 3, timing mode 3: the three stacked rhythm layers (K3 = all three).
// A noise loop is captured; one clean pass is kept as the reference; then with
// K3 up every pass's rendered map (per step: which step's material, direction,
// ratchet, silence) is checked against the output sample by sample.
struct LayerStats { int G = 0, cells = 0; long steps = 0, good = 0, silent = 0, silent_ok = 0, bad_lines = 0;
                    bool changed = false; long first_pass_events = -1; };
static LayerStats LayerRun(float k3, int tapms, int passes) {
  LayerStats st;
  Reset(); cs.sw[0] = 0; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({tapms}); RunFor(0.6f);
  seen_acts = v.act_count_;
  CapRec r{}; noise_from = n; noise_to = n + tapms * 48;
  if (!WaitActivation(tapms / 1000.f + 6.f, &r)) { noise_from = noise_to = -1; return st; }
  noise_from = noise_to = -1;
  const int s = r.s; const long Q = (long)r.Q;
  RunFor(2.5f); blk = 1;
  auto to_pass = [&]() { const int32_t pp = v.pass_[s]; const long lim = n + 20L * 48000; while (v.pass_[s] == pp && n < lim) RunFor(1.f / sr); };
  to_pass(); hist_on = true; audit_on = true; audit_bad = 0;
  wet_hist.clear(); hist_n0 = n; RunFor((float)(Q - 2) / sr);
  const std::vector<float> ref(wet_hist);
  auto refm = [&](long k) { k = ((k % Q) + Q) % Q; return (k >= 1 && k - 1 < (long)ref.size()) ? ref[k - 1] : 0.f; };
  cs.knob[2] = k3;
  std::vector<std::string> first(3);
  for (int p = 0; p < passes; p++) {
    to_pass();
    if (p == 0) st.first_pass_events = v.trig_cnt_[s];      // K3 just came up: seeds pending, pass clean
    wet_hist.clear(); hist_n0 = n;
    RunFor((float)(Q - 2) / sr);
    const int G = v.sl_n_[s];
    st.G = G; st.cells = v.ln_cells_[s];
    // Line rules: hits 1..SPAN_MAX, inside the line, at least one pause apart (cyclic).
    for (int l = 0; l < 3; l++) {
      std::string line(v.ln_cells_[s], '-'); bool ok = true;
      for (int h = 0; h < v.ln_nh_[s][l]; h++) {
        const auto& x = v.ln_hit_[s][l][h];
        if (x.len < 1 || x.len > VESTIGE_TIMING_SPAN_MAX || x.start < 0 || x.start + x.len > v.ln_cells_[s]) ok = false;
        for (int c = x.start - 1; c <= x.start + x.len; c++) {
          const int cc = (c + v.ln_cells_[s]) % v.ln_cells_[s];
          if (c >= x.start && c < x.start + x.len) { if (line[cc] != '-') ok = false; line[cc] = 'x'; }
        }
      }
      for (int h = 0; h < v.ln_nh_[s][l]; h++) {             // pause cells around each hit are empty
        const auto& x = v.ln_hit_[s][l][h];
        const int lo = (x.start - 1 + v.ln_cells_[s]) % v.ln_cells_[s], hi = (x.start + x.len) % v.ln_cells_[s];
        if (v.ln_nh_[s][l] > 1 && (line[lo] == 'x' || line[hi] == 'x')) ok = false;
      }
      if (!ok) st.bad_lines++;
      std::string typed(line);                                 // positions + types: what evolves
      for (int h = 0; h < v.ln_nh_[s][l]; h++) { const auto& x = v.ln_hit_[s][l][h]; typed[x.start] = (char)('a' + x.type); }
      if (p == 1) first[l] = typed; else if (p > 1 && typed != first[l]) st.changed = true;
    }
    if (G == 0) continue;
    auto bnd = [&](int i) { return (i >= G) ? Q : (long)((double)Q * i / G + 0.5); };
    for (int i = 0; i < G; i++) {
      const int rt = v.tl_rat_[s][i], src = v.tl_src_[s][i], dr = v.tl_dir_[s][i], cn = v.tl_cnd_[s][i];
      for (int j = 0; j < rt; j++) {
        const long b0 = bnd(i), b1 = bnd(i + 1);
        const long a = (j == 0) ? b0 : (long)(b0 + (double)(b1 - b0) * j / rt + 0.5);
        const long e = (j + 1 < rt) ? (long)(b0 + (double)(b1 - b0) * (j + 1) / rt + 0.5) : b1;
        const long t0 = a + 300, t1 = e - 20;                  // past the 5 ms restart / fade
        if (t1 - t0 < 200 || t1 > (long)wet_hist.size()) continue;
        if (cn == 1) { double en = 0; for (long t = t0; t < t1; t++) en += wet_hist[t-1] * wet_hist[t-1];
                       st.silent++; if (en / (t1 - t0) < 1e-8) st.silent_ok++; continue; }
        if (cn >= 2) continue;                                 // decimated: not the material itself
        double best = -2;
        for (int lag = -2; lag <= 2; lag++) { double sxy = 0, sxx = 0, syy = 0;
          for (long t = t0; t < t1; t++) {
            const long k = (dr > 0) ? bnd(src) + (t - a) : bnd(src + 1) - 1 - (t - a);
            const float x = wet_hist[t - 1], y = refm(k + lag);
            sxy += x * y; sxx += x * x; syy += y * y; }
          best = std::max(best, sxy / sqrt(sxx * syy + 1e-20)); }
        st.steps++; if (best > 0.95) st.good++;
      }
    }
  }
  audit_on = false; hist_on = false; blk = 48; cs.knob[2] = 0.f; Realign();
  return st;
}
static void TestTimingLayers() {
  printf("-- stage 3: the TIMING error, mode 3: three stacked rhythm layers\n");
  // Steps: ~VESTIGE_TIMING_STEP_MS up to the knee, slower past it; always an
  // exact division of the loop (2^k or 3 x 2^k).
  struct Len { int ms, G; } lens[] = {{500, 4}, {2000, 16}, {4000, 24}, {6000, 32}, {8000, 32}};
  bool g_ok = true; std::string gs;
  for (const Len& x : lens) {
    const LayerStats st = LayerRun(0.65f, x.ms, 4);
    char b[48]; snprintf(b, sizeof b, "%d ms: G %d (line %d)  ", x.ms, st.G, st.cells); gs += b;
    if (st.G != x.G || st.cells < VESTIGE_TIMING_LINE_STEPS || st.cells % st.G) g_ok = false;
  }
  printf("      %s\n", gs.c_str());
  Check(g_ok, "steps per pass: 500 ms 4 / 2 s 16 (125 ms) / 4 s 24 / 6 s 32 / 8 s 32 (250 ms); line >= 32 steps, whole passes");
  // Render: every step plays exactly its map; rests are silent; no bad reads.
  for (float k3 : {0.65f, 1.f}) {
    const LayerStats st = LayerRun(k3, 2000, 12);
    printf("      K3 %.2f, 2 s: %ld / %ld steps match their map, %ld / %ld rests silent, audit bad %ld, bad lines %ld\n",
           k3, st.good, st.steps, st.silent_ok, st.silent, audit_bad, st.bad_lines);
    char m[160]; snprintf(m, sizeof m, "K3 %.2f: every step = its source step (direction, ratchet) at c > 0.95; rests silent; no read outside the guard", k3);
    Check(st.steps > 60 && st.good == st.steps && st.silent > 0 && st.silent_ok == st.silent && audit_bad == 0, m);
    snprintf(m, sizeof m, "K3 %.2f: hit lines keep their rules (1..%d steps, a pause apart) and evolve (places / types)", k3, VESTIGE_TIMING_SPAN_MAX);
    Check(st.bad_lines == 0 && st.changed, m);
    if (k3 < 1.f) Check(st.first_pass_events == 0, "K3 up from 0: the first pass plays clean (the lines are built during it)");
  }
  { const LayerStats st = LayerRun(1.f, 8000, 3);
    Check(st.steps > 30 && st.good == st.steps && st.silent_ok == st.silent && audit_bad == 0,
          "8 s loop, K3 max: every step matches its map, rests silent, no bad reads"); }
  // K3 CCW: nothing at all.
  { cs.knob[2] = 0.f; RunFor(0.1f);
    Check(v.err_level_[0] == 0.f && v.err_level_[1] == 0.f && v.err_level_[2] == 0.f, "K3 CCW: no error levels"); }
}
// K4 = degradation colour: clean (engine idle) around noon, BBD CCW, tape CW;
// engaging / leaving / flipping never steps the wet more than the engaged sound.
static void TestK4Degrade() {
  printf("-- K4: degradation colour\n");
  Reset(); cs.sw[0] = 0; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.6f);
  seen_acts = v.act_count_;
  CapRec r{}; sustain_hz = 110.f; sustain_input = true; play_input = true; WaitActivation(3.f, &r); sustain_input = false; play_input = false;
  RunFor(2.f); blk = 1;
  auto seg = [&](float k, float secs) { cs.knob[3] = k; maxd = 0.f; RunFor(secs); return maxd; };
  const float clean = seg(0.5f, 1.5f); const bool idle_noon = v.degrade_.Idle();
  cs.knob[3] = 0.5f + VESTIGE_K4_DEADZONE * 0.5f; RunFor(0.3f); const bool idle_near = v.degrade_.Idle();
  const float in_tape = seg(0.85f, 1.0f), tape = seg(0.85f, 1.5f), out_tape = seg(0.5f, 1.0f);
  const float in_bbd = seg(0.15f, 1.0f), bbd = seg(0.15f, 1.5f), flip = seg(0.85f, 1.5f), back = seg(0.5f, 1.5f);
  const bool idle_end = v.degrade_.Idle();
  printf("      max sample step: clean %.4f | ->tape %.4f tape %.4f ->noon %.4f | ->BBD %.4f BBD %.4f ->tape %.4f ->noon %.4f\n",
         clean, in_tape, tape, out_tape, in_bbd, bbd, flip, back);
  Check(idle_noon && idle_near && idle_end, "K4 at noon (and inside its dead zone): colour engine idle");
  const float lim = 1.5f * std::max(std::max(tape, bbd), clean);
  Check(in_tape <= lim && out_tape <= lim && in_bbd <= lim && flip <= lim && back <= lim,
        "K4 into / out of / across the colour: no step above 1.5x the engaged sound's own");
  blk = 48; cs.knob[3] = 0.5f; Realign();
}
// Voice cap = the live voices + ONE fading tail: SW1 UP, a long K5 fade and
// three captures in quick succession never leave more than two loops
// granulating; the oldest tail is stolen (fast release) and freed at once.
// MIDDLE keeps VESTIGE_MAX_VOICES.
static void TestFadeVoiceCap() {
  printf("-- voice cap: live voices + one fading tail\n");
  auto sounding = []() { int c = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) if (v.active_[q]) c++; return c; };
  auto unstolen = []() { int c = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) if (v.active_[q] && !v.stolen_[q]) c++; return c; };
  Reset(); cs.knob[4] = 1.0f; cs.knob[0] = 0.5f; cs.sw[0] = 0; Taps({1000}); RunFor(0.5f);   // K5 CW: the longest fade
  seen_acts = v.act_count_;
  int worst_unstolen = 0, worst_after = 0; bool freed_fast = true;
  for (int i = 0; i < 3; i++) {
    CapRec q{}; noise_from = n; noise_to = n + 12000; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    worst_unstolen = std::max(worst_unstolen, unstolen());
    RunFor(0.02f);                                           // 20 ms: a stolen tail (6 ms release) is gone
    if (sounding() > 2) freed_fast = false;
    worst_after = std::max(worst_after, sounding());
    RunFor(0.3f);
  }
  printf("      SW1 UP, K5 max, 3 captures: at most %d unstolen, %d sounding 20 ms after an activation\n", worst_unstolen, worst_after);
  Check(worst_unstolen <= 2 && freed_fast, "SW1 UP: a third capture during a long fade steals the oldest tail (at most 2 loops granulate)");
  Reset(); cs.knob[4] = 1.0f; cs.sw[0] = 1; Taps({1000}); RunFor(0.5f);
  Check(v.VoiceCap() == VESTIGE_MAX_VOICES, "SW1 MIDDLE: the cap stays VESTIGE_MAX_VOICES");
  cs.sw[0] = 0; RunFor(0.1f);
  Check(v.VoiceCap() == 2, "SW1 UP: the cap is 2 (one live + one fading)");
  cs.knob[4] = K5Fade(0.05f); noise_from = noise_to = -1;
}
// K5 CCW half = number of repeats N: the first at full level, repeat k at
// (1 - k/N)^VESTIGE_REPEAT_CURVE, the last one ramps out on its pass end and
// the loop is freed; FS2 hold pauses the count; noon keeps the level.
static float K5Repeats(int nrep) {              // raw knob for N repeats
  const float u = 1.f - logf((float)nrep) / logf(VESTIGE_REPEAT_N_MAX);
  const float r = 0.5f - VESTIGE_K5_DEADZONE - u * (0.5f - VESTIGE_K5_DEADZONE);
  return r * (KNOB_MAX - KNOB_MIN) + KNOB_MIN;
}
static void TestK5Repeats() {
  printf("-- K5 CCW: number of repeats\n");
  int s = -1; long Q = 0;
  auto setup = [&]() {
    Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 0; Taps({500}); RunFor(0.6f);
    seen_acts = v.act_count_;
    CapRec r{}; noise_from = n; noise_to = n + 24000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
    s = r.s; Q = (long)r.Q; RunFor(1.f); blk = 1;
  };
  auto to_pass = [&]() { const int32_t pp = v.pass_[s]; const long lim = n + 5L * 48000; while (v.pass_[s] == pp && n < lim) RunFor(1.f / sr); };
  // The rest of the current pass, up to the next wrap: RMS of its middle (past
  // the 10 ms level ramp), and the peak of its last 2 ms.
  auto pass_rms = [&](float* tail) {
    hist_on = true; wet_hist.clear(); hist_n0 = n;
    { const int32_t pp = v.pass_[s]; const long lim = n + 5L * 48000; while (v.pass_[s] == pp && n < lim) RunFor(1.f / sr); }
    hist_on = false; if (!wet_hist.empty()) wet_hist.pop_back();   // (the wrap sample belongs to the next pass)
    double e = 0; long c = 0; for (size_t i = 960; i + 960 < wet_hist.size(); i++) { e += wet_hist[i] * wet_hist[i]; c++; }
    if (tail) { float m = 0.f; for (size_t i = wet_hist.size() - 96; i < wet_hist.size(); i++) m = std::max(m, fabsf(wet_hist[i])); *tail = m; }
    return (float)sqrt(e / (double)(c ? c : 1));
  };
  setup();
  to_pass(); const float r0 = pass_rms(nullptr);
  cs.knob[4] = K5Repeats(4); RunFor(0.011f);               // the count begins in this pass (repeat 1)
  const int nn = v.rep_n_;
  float lv[4], tail = 0.f;
  for (int k = 0; k < 4; k++) lv[k] = pass_rms(k == 3 ? &tail : nullptr);
  RunFor(0.02f);
  const bool freed = !v.active_[s];
  float db[4]; for (int k = 0; k < 4; k++) db[k] = 20.f * log10f(lv[k] / r0);
  printf("      N %d: repeat levels %.1f / %.1f / %.1f / %.1f dB (want 0 / -5.0 / -12.0 / -24.1); last 2 ms peak %.4f; freed %d\n",
         nn, db[0], db[1], db[2], db[3], tail, (int)freed);
  Check(nn == 4, "K5 CCW maps to a whole number of repeats (N = 4 here)");
  bool lv_ok = true; const float want[4] = {0.f, -5.f, -12.04f, -24.08f};
  for (int k = 0; k < 4; k++) if (fabsf(db[k] - want[k]) > 0.7f) lv_ok = false;
  Check(lv_ok, "repeat 1 at full level, then (1 - k/N)^curve");
  Check(tail < 0.02f * r0 * 4.f && freed, "the last repeat ramps out on its pass end; the loop is freed");
  // N = 1: plays once.
  setup(); to_pass(); cs.knob[4] = K5Repeats(1); RunFor(0.011f);
  const float one = pass_rms(nullptr); RunFor(0.02f);
  Check(v.rep_n_ == 1 && fabsf(20.f * log10f(one / r0)) < 0.7f && !v.active_[s], "N = 1: the loop plays once, at full level, then stops");
  // Hold pauses the count.
  // (The hold gesture is a 900 ms press, so hold goes on before the count.)
  setup(); Hold(); to_pass(); cs.knob[4] = K5Repeats(4); RunFor(0.011f); pass_rms(nullptr);
  const float h1 = pass_rms(nullptr), h2 = pass_rms(nullptr), h3 = pass_rms(nullptr);
  const bool held_ok = v.held_ && v.active_[s] && fabsf(20.f * log10f(h1 / r0)) < 0.7f && fabsf(20.f * log10f(h3 / r0)) < 0.7f;
  Unhold(); pass_rms(nullptr); const float u1 = pass_rms(nullptr);   // counting again
  printf("      held: %.1f / %.1f / %.1f dB, then released: %.1f dB\n", 20.f * log10f(h1 / r0), 20.f * log10f(h2 / r0),
         20.f * log10f(h3 / r0), 20.f * log10f(u1 / r0));
  Check(held_ok && u1 < h3 * 0.9f, "FS2 hold pauses the count (full level held); released, it counts on");
  // Noon keeps the level.
  setup(); to_pass(); cs.knob[4] = K5Repeats(4); RunFor(0.011f); pass_rms(nullptr);
  cs.knob[4] = 0.5f; RunFor(0.011f); const float a1 = pass_rms(nullptr);
  const float a2 = pass_rms(nullptr); const float a3 = pass_rms(nullptr);
  Check(v.rep_n_ == 0 && v.active_[s] && fabsf(20.f * log10f(a3 / a2)) < 0.5f && a1 > 0.f, "K5 back at noon: endless, the level kept");
  blk = 48; Realign(); Reset();
}
static void TestTimingSlices() {
  printf("-- stage 3: the TIMING error, mode 1: SLICE REARRANGEMENT (%d slices)\n", VESTIGE_TIMING_SLICES);
  // Level 0: nothing drawn.
  Reset(); cs.sw[0] = 0; cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  CapRec r{}; noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  const int s = r.s; const size_t Q = r.Q;
  RunFor(0.8f);
  long grid0 = 0;
  { const uint32_t rng0 = v.timing_rng_, t0 = v.timing_trigs_, p0 = v.timing_patterns_;
    blk = 1; const int32_t pp = v.pass_[s]; while (v.pass_[s] == pp) RunFor(1.f / sr); grid0 = n - 1;
    RunFor(3.0f); Realign();
    Check(v.timing_rng_ == rng0 && v.timing_trigs_ == t0 && v.timing_patterns_ == p0 && v.trig_off_[s] == 0.f,
          "timing level 0: nothing drawn, nothing rearranged, the read head is the timeline head"); }
  // The drawn material: a priority order of the steps 1..N-1 and a
  // replacement slice per step, other than its own.
  { bool ok = true; int nsl = VESTIGE_TIMING_SLICES;
    for (int t = 0; t < VESTIGE_TIMING_SLICE_TIERS && nsl >= 2; t++, nsl /= 2) {
      int seen[VESTIGE_TIMING_SLICE_MAX] = {0};
      for (int i = 0; i < nsl - 1; i++) { const int st = v.sl_prio_[s][t][i]; if (st < 1 || st >= nsl || seen[st]++) ok = false; }
      for (int i = 1; i < nsl; i++) { const int rp = v.sl_repl_[s][t][i]; if (rp < 0 || rp >= nsl || rp == i) ok = false; }
    }
    int pr[8]; for (int i = 0; i < 7; i++) pr[i] = v.sl_prio_[s][0][i];
    int rp[8]; rp[0] = 0; for (int i = 1; i < 8; i++) rp[i] = v.sl_repl_[s][0][i];
    printf("      this loop's draw (8 slices): priority %d %d %d %d %d %d %d, replacements %s\n", pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], ArrStr(rp, 8).c_str());
    Check(ok, "loop start: a priority PERMUTATION of steps 1..N-1 and a replacement other than the step's own, for every slice count"); }

  // Levels: exactly round(L*(N-1)) steps rearranged, nested, stable, step 0
  // anchored; jumps only where the slice is not the continuation.
  int tier8 = 0; const int n8 = WantN((double)Q, &tier8);
  { int prev_arr[VESTIGE_TIMING_SLICE_MAX]; bool have = false, nested = true, all_ok = true; std::string line;
    for (float lv : {1.f / 7.f, 2.f / 7.f, 0.5f, 5.f / 7.f, 1.f}) {
      v.err_level_[0] = lv;
      const SlLog ll = RunSl(s, 6);
      const SlStats sl = CheckSl(ll, s, Q, lv, n8, tier8, true);
      if (!sl.ok() || sl.played != sl.passes) all_ok = false;
      const SlRec& b = ll.p.back();
      if (have) for (int i = 0; i < b.n; i++) if (prev_arr[i] != i && b.arr[i] != prev_arr[i]) nested = false;
      for (int i = 0; i < b.n; i++) prev_arr[i] = b.arr[i]; have = true;
      char buf[64]; snprintf(buf, sizeof buf, "L=%.2f %s (%d)  ", lv, ArrStr(b.arr, b.n).c_str(), sl.swapped); line += buf;
    }
    printf("      arrangements: %s\n", line.c_str());
    Check(all_ok, "every level: exactly round(L x 7) steps rearranged per the loop's draw, step 0 = slice A, stable across passes, jumps only where the slice is not the continuation");
    Check(nested, "nested: a higher level keeps every swap of a lower one"); }

  // Level 1 run: grid, content per step, returns.
  v.err_level_[0] = 1.f;
  { const SlLog lg = RunSl(s, 20, 60.f, true);
    const SlStats st = CheckSl(lg, s, Q, 1.f, n8, tier8, true);
    bool grid_ok = true; for (const SlRec& pr : lg.p) if ((pr.start - grid0) % (long)Q != 0) grid_ok = false;
    int ok, tot; float worst; StepWindows(lg, s, Q, false, &ok, &tot, &worst);
    PrintSl("level 1", st);
    printf("      level 1: %s; %d / %d steps = their slice at lag 0 (worst c %.4f); grid %s; read out of range %ld; half-speed head jumps outside restarts %ld\n",
           ArrStr(lg.p[0].arr, n8).c_str(), ok, tot, worst, grid_ok ? "unchanged" : "MOVED", lg.rh_bad, lg.half_jumps);
    Check(st.ok() && st.played == st.passes, "level 1: all 7 steps rearranged, as drawn; jumps exactly at the slice boundaries that need one");
    Check(tot >= 60 && ok == tot, "each step's output is its slice (lag 0, c > 0.99)");
    Check(grid_ok && lg.rh_bad == 0, "pass length and grid unchanged; the read head stays in the loop");
    Check(lg.half_jumps == 0, "K1 half-speed head continuous except at the jumps (also where a slice runs on over the seam)"); }

  // K3 change mid-pass: applies at the next pass start.
  { blk = 1; int32_t p0 = v.pass_[s]; while (v.pass_[s] == p0) RunFor(1.f / sr);
    for (long j = 0; j < (long)Q / 2; j++) RunFor(1.f / sr);
    int before[VESTIGE_TIMING_SLICE_MAX]; for (int i = 0; i < n8; i++) before[i] = v.sl_arr_[s][i];
    v.err_level_[0] = 2.f / 7.f; p0 = v.pass_[s]; bool kept = true;
    while (v.pass_[s] == p0) { RunFor(1.f / sr); if (v.pass_[s] == p0) for (int i = 0; i < n8; i++) if (v.sl_arr_[s][i] != before[i]) kept = false; }
    int sw = 0; for (int i = 0; i < n8; i++) if (v.sl_arr_[s][i] != i) sw++;
    Realign();
    Check(kept && sw == 2, "a K3 change applies at the next pass start (that pass: 2 swaps), not mid-pass"); }

  // Randomness: per step, per pass, independently, with r = L^curve; a random
  // slice is never the planned one; at L = 1 every step every pass; the
  // arrangement core stays as drawn.
  { struct Lv { float level; int passes; };
    bool ok_all = true, rate_ok = true; std::string line;
    for (const Lv& lv : {Lv{2.f / 7.f, 200}, Lv{0.5f, 150}, Lv{5.f / 7.f, 100}, Lv{1.f, 80}}) {
      v.err_level_[0] = lv.level;
      const SlLog ll = RunSl(s, lv.passes, 200.f);
      const SlStats sv = CheckSl(ll, s, Q, lv.level, n8, tier8, true);
      const double r = pow((double)lv.level, (double)VESTIGE_TIMING_RAND_CURVE);
      const double rate = (double)sv.rsteps / sv.steps, sd = sqrt(r * (1 - r) / sv.steps);
      // Passes: are the random steps independent? The number of passes with
      // no random step ~ (1-r)^7.
      const double p0 = pow(1.0 - r, (double)(n8 - 1)), none = (double)(sv.passes - sv.rpasses) / sv.passes, sd0 = sqrt(p0 * (1 - p0) / sv.passes);
      char b2[200]; snprintf(b2, sizeof b2, "\n        L=%.2f: %d / %d steps random = %.4f (r = L^%.1f = %.4f +- %.4f); passes with none %.3f (expect %.3f +- %.3f)",
                             lv.level, sv.rsteps, sv.steps, rate, VESTIGE_TIMING_RAND_CURVE, r, 4 * sd, none, p0, 4 * sd0);
      line += b2;
      if (!sv.ok() || sv.played != sv.passes) ok_all = false;
      if (r >= 1.0 ? sv.rsteps != sv.steps : (fabs(rate - r) > 4 * sd || fabs(none - p0) > 4 * sd0 + 1e-9)) rate_ok = false;
      if (lv.level == 1.f) {                                   // the random slice: uniform over the other N-1
        const double e = sv.rsteps / (double)(n8 - 1), sde = sqrt(sv.rsteps * (1.0 / (n8 - 1)) * (1 - 1.0 / (n8 - 1)));
        std::string hs; bool uni = sv.hist[0] == 0;
        for (int k = 1; k < n8; k++) { char c[16]; snprintf(c, sizeof c, "%d ", sv.hist[k]); hs += c; if (fabs(sv.hist[k] - e) > 4 * sde) uni = false; }
        printf("      L=1 random slice - planned slice (mod 8), counts for +1..+7: %s(expect %.0f +- %.0f), +0: %d\n", hs.c_str(), e, 4 * sde, sv.hist[0]);
        Check(uni, "a random slice is uniform over the N-1 slices other than the planned one (never the planned one)");
      }
    }
    printf("      per-step randomness:%s\n", line.c_str());
    Check(ok_all, "randomness: step 0 never random, the module's random steps = the steps that differ, the arrangement core as drawn and stable");
    Check(rate_ok, "per-step random rate ~ L^curve, steps independent (tolerance 4 sd); L = 1: every step random every pass");
    v.err_level_[0] = 0.f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // A loop that joins late with the level already up: the first pass's
  // arrangement is played from the join on — the steps behind the join count
  // as the timeline's (so a step right after it that is its own slice runs on).
  { int ok = 0, tot = 0, differ = 0; std::string line;
    for (float lv : {1.f, 4.f / 7.f, 2.f / 7.f, 3.f / 7.f, 5.f / 7.f, 1.f / 7.f, 2.f / 7.f, 3.f / 7.f, 6.f / 7.f, 1.f / 7.f}) {
      Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({1000}); RunFor(0.6f);
      v.err_level_[0] = lv;
      seen_acts = v.act_count_;
      const uint32_t t0 = v.timing_trigs_;
      CapRec q{}; noise_from = n; noise_to = n + 25400; WaitActivation(3.f, &q); noise_from = noise_to = -1;
      const int qs = q.s; const size_t L = q.Q; const int nsl = v.sl_n_[qs];
      int ord[VESTIGE_TIMING_SLICE_MAX]; for (int i = 0; i < nsl; i++) ord[i] = v.sl_order_[qs][i];
      blk = 1; const int32_t p0 = v.pass_[qs]; uint32_t tin = v.timing_trigs_;
      while (v.pass_[qs] == p0) { tin = v.timing_trigs_; RunFor(1.f / sr); }
      Realign();
      int i0 = 0; for (int i = 1; i < nsl; i++) if (SlB(L, nsl, i) <= (long)q.phase) i0 = i;
      int want = 0, naive = 0;
      for (int i = i0 + 1; i < nsl; i++) {
        const int prev = (i - 1 <= i0) ? i - 1 : ord[i - 1];
        if (ord[i] != (prev + 1) % nsl) want++;
        if (ord[i] != (ord[i - 1] + 1) % nsl) naive++;
      }
      tot++; if (q.phase > 0 && nsl == 8 && (int)(tin - t0) == want) ok++; if (naive != want) differ++;
      char b[112]; snprintf(b, sizeof b, "\n        L=%.2f phase %u step %d: %u jumps (model %d, arrangement-only rule %d)", lv, q.phase, i0, tin - t0, want, naive); line += b;
    }
    printf("      late joins:%s\n", line.c_str());
    Check(ok == tot && differ >= 1, "a loop joining mid-pass plays its arrangement from the join on (steps behind it = the timeline's)");
    v.err_level_[0] = 0.f; }

  // A loop whose length the slices do not divide, and a FORCED arrangement
  // A H A D E F G H at level 2/7 (host-only: this loop's drawn tables
  // overwritten): a jump to H at step 1, H runs on over the loop's seam into
  // A (no restart there), a jump to D at step 3, the rest on the timeline, no
  // return; the half-speed head continuous.
  { size_t T = 0; float k2 = 0.f;
    for (float k = 0.64f; k < 0.74f; k += 0.001f) { const size_t t = Vestige::KnobPeriod(RemapKnob(k)); if (t >= 16000 && t <= 30000 && t % 8 != 0) { T = t; k2 = k; break; } }
    Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; cs.knob[1] = k2; RunFor(0.5f);
    hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
    seen_acts = v.act_count_;
    CapRec q{}; noise_from = n; noise_to = n + (long)T + 12000; WaitActivation(4.f, &q); noise_from = noise_to = -1;
    RunFor(0.5f);
    const int qs = q.s; const size_t L = q.Q; int tr = 0; const int nw = WantN((double)L, &tr);
    v.err_level_[0] = 1.f;
    const SlLog lo = RunSl(qs, 12, 60.f, true);
    const SlStats so = CheckSl(lo, qs, L, 1.f, nw, tr, true);
    int ok, tot; float worst; StepWindows(lo, qs, L, false, &ok, &tot, &worst);
    PrintSl("loop not divisible by the slices", so);
    printf("      K2 at %.3f -> T %zu; loop %zu (%% 8 = %zu, %d slices): %d / %d steps = their slice at lag 0 (worst c %.4f)\n", k2, T, L, L % 8, nw, ok, tot, worst);
    Check(L == T && L % 8 != 0 && so.ok() && so.played == so.passes && ok == tot && tot >= 40,
          "a loop the slices do not divide: boundaries at round(L x i / N), jumps and continuations as the model");
    for (int i = 0; i < 7; i++) v.sl_prio_[qs][0][i] = (int8_t)(i + 1);
    v.sl_repl_[qs][0][1] = 7; v.sl_repl_[qs][0][2] = 0;       // steps 1, 2 first: H, A
    v.err_level_[0] = 2.f / 7.f;
    RunSl(qs, 1);
    const SlLog lf = RunSl(qs, 24, 60.f, true);
    const SlStats sf = CheckSl(lf, qs, L, 2.f / 7.f, 8, 0, true);
    StepWindows(lf, qs, L, false, &ok, &tot, &worst);
    int base_one = 0, base_n = 0;
    for (const SlRec& pr : lf.p) if (pr.rmask == 0) { base_n++; if (pr.jumps_el.size() == 2 && pr.jumps_el[0] == SlB(L, 8, 1) && pr.jumps_el[1] == SlB(L, 8, 3) && !pr.ret) base_one++; }
    PrintSl("forced A H A D E F G H", sf);
    printf("      forced: %s; %d / %d passes without random steps = jumps at steps 1 and 3 only, no return; %d / %d steps = their slice (lag 0); half-speed jumps outside restarts %ld; read out of range %ld\n",
           ArrStr(lf.p[0].arr, 8).c_str(), base_one, base_n, ok, tot, lf.half_jumps, lf.rh_bad);
    Check(sf.ok() && base_n >= 10 && base_one == base_n && ok == tot && lf.rh_bad == 0,
          "a slice that continues over the loop's seam (H -> A) runs on: no restart there");
    Check(lf.half_jumps == 0, "K1 half-speed head: continuous where the read runs over the seam inside a pass");
    v.err_level_[0] = 0.f; cs.knob[1] = 0.85f; RunFor(0.5f);
    hist_on = false; in_hist.clear(); wet_hist.clear(); }

  // No click on a sine loop; also with the K1 half-speed version.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  sustain_hz = 220.f; sustain_input = true; play_input = true; WaitActivation(2.f, &r);
  sustain_input = false; play_input = false; RunFor(1.0f);
  { const int q = r.s;
    maxd = 0.f; RunFor(2.0f); const float st = maxd;
    size_t nj = 0;
    for (float lv : {1.f, 0.5f}) { v.err_level_[0] = lv; const SlLog lc = RunSl(q, 10); for (const SlRec& pr : lc.p) nj += pr.jumps_at.size(); }
    const float sj = maxd;
    const float khalf = 0.5f - (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = khalf; v.err_level_[0] = 0.f; RunFor(1.5f); maxd = 0.f; RunFor(2.0f); const float sth = maxd;
    v.err_level_[0] = 1.f; maxd = 0.f; RunSl(q, 8); const float sh = maxd;
    printf("      sine loop max step: steady %.5f | 20 passes, %zu jumps: %.5f (bound %.5f) | K1 half side: steady %.5f, level 1: %.5f (bound %.5f)\n",
           st, nj, sj, 1.5f * st, sth, sh, 1.5f * sth);
    Check(nj >= 40 && sj <= 1.5f * st, "slice jumps and returns: no step above 1.5x steady (5 ms restarts)");
    Check(sh <= 1.5f * sth, "K1 half-speed version with slices: no step above 1.5x steady");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; }

  // Reverse: the arrangement on the reversed loop.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; cs.knob[1] = 0.2f; RunFor(0.3f); Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  RunFor(0.8f);
  { const int q = r.s; v.err_level_[0] = 1.f;
    const SlLog lr = RunSl(q, 10);
    const SlStats sr_ = CheckSl(lr, q, r.Q, 1.f, n8, tier8, true);
    int ok, tot; float worst; StepWindows(lr, q, r.Q, true, &ok, &tot, &worst);
    PrintSl("reverse, level 1", sr_);
    printf("      reverse: %s on the reversed loop; %d / %d steps = their reversed slice at lag 0 (worst c %.4f); read out of range %ld\n",
           ArrStr(lr.p[0].arr, n8).c_str(), ok, tot, worst, lr.rh_bad);
    Check(v.rev_play_ && sr_.ok() && sr_.played == sr_.passes && tot >= 30 && ok == tot && lr.rh_bad == 0,
          "reverse: the same arrangement on the reversed loop (its slice A = the loop's last eighth, backward)");
    v.err_level_[0] = 0.f; cs.knob[1] = 0.85f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // K1 midpoint: clean and double speed jump together.
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  hist_on = true; hist_n0 = n; in_hist.clear(); wet_hist.clear();
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  { const int q = r.s; const long L = (long)r.Q;
    const float kmid = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = kmid; RunFor(1.5f);
    v.err_level_[0] = 1.f;
    const SlLog lk = RunSl(q, 6);
    int ok = 0, tot = 0;
    for (const SlRec& pr : lk.p) { std::vector<long> w, rs; WantJumps(pr.ord, pr.n, (size_t)L, &w, &rs);
      for (size_t k = 0; k < pr.jumps_at.size() && tot < 20; k++) {
        int st = 0; for (int i = 1; i < pr.n; i++) if (SlB((size_t)L, pr.n, i) == pr.jumps_el[k]) st = i;
        const long r0 = rs[st]; const int NN = (int)std::min(2400L, SlB((size_t)L, pr.n, st + 1) - SlB((size_t)L, pr.n, st) - 600);
        if (NN < 400 || r0 + 480 + NN >= L) continue;
        std::vector<float> rc(NN), rsp(NN);
        for (int j = 0; j < NN; j++) { rc[j] = v.slab_[q][r0 + 480 + j]; rsp[j] = v.slab_[q][(2 * (r0 + 480 + j)) % L]; }
        float gc = 0, gs = 0, res = 0; Split(pr.jumps_at[k] + 480, rc, rsp, &gc, &gs, &res);
        tot++; if (fabsf(gc - v.g_c_) < 0.05f && fabsf(gs - v.g_sp_) < 0.05f && res < 0.05f) ok++;
      } }
    printf("      K1 midpoint, level 1: %d / %d jumps = clean from the slice + double speed from 2x the slice\n", ok, tot);
    Check(tot >= 6 && ok == tot, "K1 midpoint: clean and speed versions jump together");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; }
  hist_on = false; in_hist.clear(); wet_hist.clear();

  // Stretch at a rate != 1: slice boundaries at i/N of the pass (output time).
  Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({500}); RunFor(0.6f);
  seen_acts = v.act_count_;
  noise_from = n; noise_to = n + 36000; WaitActivation(3.f, &r); noise_from = noise_to = -1;
  { const int q = r.s;
    Taps({700}); RunFor(1.0f);
    v.err_level_[0] = 1.f;
    const SlLog ls = RunSl(q, 6);
    const long Lt = (long)GridQuantize::Boundary(v.div_[q], v.period_);
    int tr = 0; const int nw = WantN((double)Lt, &tr);
    const SlStats ss = CheckSl(ls, q, v.PlayLen(q), 1.f, nw, tr, false);
    int len_bad = 0; for (size_t p = 1; p < ls.p.size(); p++) if (labs(ls.p[p].start - ls.p[p - 1].start - Lt) > 1) len_bad++;
    int tbad = 0;
    for (const SlRec& pr : ls.p) { std::vector<long> w, rs; WantJumps(pr.ord, pr.n, v.PlayLen(q), &w, &rs);
      for (size_t k = 0; k < w.size() && k < pr.jumps_el.size(); k++) if (labs(pr.jumps_el[k] - (long)((double)w[k] / v.rho_d_[q])) > 2) tbad++; }
    PrintSl("stretch", ss);
    printf("      stretch at rate %.4f: pass %ld, jump times off %d, lengths off %d\n", v.rho_d_[q], Lt, tbad, len_bad);
    Check(v.rho_d_[q] != 1.0 && ss.ok() && ss.played == ss.passes && tbad == 0 && len_bad == 0,
          "stretch at rate != 1: boundaries at i/N of the pass, the pass stays d x T_now");
    v.err_level_[0] = 0.f; }

  // Hold: continues. Freeze: never.
  { Hold(); v.err_level_[0] = 1.f;
    const int q = FirstLive(); const SlLog lh = RunSl(q, 3);
    int played = 0; for (const SlRec& pr : lh.p) if (pr.n > 0) played++;
    Check(v.held_ && played >= 2, "held loop: slices play too");
    v.err_level_[0] = 0.f; Unhold(); }
  { Reset(); cs.sw[0] = 2; cs.knob[4] = 0.5f; RunFor(1.0f);
    seen_acts = v.act_count_;
    v.err_level_[0] = 1.f;
    const uint32_t t0 = v.timing_trigs_, p0 = v.timing_patterns_;
    noise_from = n; noise_to = n + 9600; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    RunFor(3.0f);
    Check(Vestige::PoolOf(q.s) == Vestige::kPoolFreeze && v.timing_trigs_ == t0 && v.timing_patterns_ == p0,
          "freeze: the timing error never touches it");
    v.err_level_[0] = 0.f; cs.sw[0] = 0; RunFor(1.0f); }

  // Short-loop guard: 8 -> 4 -> 2 -> none.
  { struct G { int tap_ms; long burst; };
    std::string line; bool ok = true; int seen8 = 0, seen4 = 0, seen2 = 0, seen0 = 0;
    for (const G& g : {G{400, 9300}, G{100, 9000}, G{100, 3300}, G{100, 1300}}) {
      Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({g.tap_ms}); RunFor(0.6f);
      seen_acts = v.act_count_;
      noise_from = n; noise_to = n + g.burst; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
      RunFor(0.3f);
      v.err_level_[0] = 1.f;
      int tr = 0; const int want = WantN((double)q.Q, &tr);
      const uint32_t sk0 = v.timing_skipped_, fb0 = v.timing_fallbacks_, t0 = v.timing_trigs_;
      const SlLog lt = RunSl(q.s, 8, 20.f);
      const SlStats sg = CheckSl(lt, q.s, q.Q, 1.f, want, tr, true);
      if (!sg.ok() || lt.p.size() < 8) ok = false;
      if (want == 0 && (v.timing_skipped_ - sk0 < 8 || v.timing_trigs_ != t0)) ok = false;
      if (want > 0 && want < VESTIGE_TIMING_SLICES && v.timing_fallbacks_ - fb0 < 8) ok = false;
      if (want == 8) seen8++; if (want == 4) seen4++; if (want == 2) seen2++; if (want == 0) seen0++;
      char b[80]; snprintf(b, sizeof b, "%.1f ms -> %d slices (%.1f ms)  ", q.Q / 48.0, want, want ? q.Q / 48.0 / want : 0.0); line += b;
      v.err_level_[0] = 0.f;
    }
    printf("      guard: %s\n", line.c_str());
    Check(ok && seen8 && seen4 && seen2 && seen0, "short-loop guard: slices >= 20 ms: 8 -> 4 -> 2 -> none, as the model");
    // Output time: a 200 ms loop following T to 150 ms (tape) has 18.75 ms
    // slices at 8 -> 4.
    Reset(); cs.knob[4] = 0.5f; cs.knob[0] = 0.5f; Taps({400}); RunFor(0.6f);
    seen_acts = v.act_count_;
    noise_from = n; noise_to = n + 9300; CapRec q{}; WaitActivation(3.f, &q); noise_from = noise_to = -1;
    Taps({300}); RunFor(1.0f);
    v.err_level_[0] = 1.f;
    const long Lt = (long)GridQuantize::Boundary(v.div_[q.s], v.period_);
    int tr = 0; const int want = WantN((double)Lt, &tr), wmat = WantN((double)v.PlayLen(q.s), &tr);
    WantN((double)Lt, &tr);
    const SlLog lt = RunSl(q.s, 6);
    const SlStats sg = CheckSl(lt, q.s, v.PlayLen(q.s), 1.f, want, tr, false);
    printf("      loop %zu material, %ld output (rate %.4f): %d slices (by material it would be %d)\n", v.PlayLen(q.s), Lt, v.rho_d_[q.s], lt.p.empty() ? -1 : lt.p[0].n, wmat);
    Check(want == 4 && wmat == 8 && sg.ok() && sg.played == sg.passes, "the guard measures the slice in output time (a loop following a shorter T)");
    v.err_level_[0] = 0.f; }

  // 3 voices: own arrangements, own variations; budget and the PHYSICAL pool.
  Reset(); cs.knob[4] = 0.5f; cs.sw[0] = 1; cs.knob[0] = 0.5f; Taps({400}); RunFor(0.5f);
  seen_acts = v.act_count_;
  const long bursts[3] = {9000, 12500, 14000};              // -> 200 / 267 / 300 ms: 8 slices each
  for (int i = 0; i < 3; i++) { CapRec q{}; noise_from = n; noise_to = n + bursts[i]; WaitActivation(3.f, &q); RunFor(0.2f); }
  noise_from = noise_to = -1;
  RunFor(1.0f);
  { int slots[3]; int ns = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS && ns < 3; q++) if (v.active_[q] && !v.dying_[q]) slots[ns++] = q;
    v.err_level_[0] = 0.5f;                                    // r = 0.25: random steps now and then
    blk = 1; int32_t pp[3]; for (int i = 0; i < ns; i++) pp[i] = v.pass_[slots[i]];
    std::vector<std::vector<int>> vars(3); std::string arr[3];
    for (long j = 0; j < 48000L * 10; j++) {
      RunFor(1.f / sr);
      for (int i = 0; i < ns; i++) if (v.pass_[slots[i]] != pp[i]) {
        pp[i] = v.pass_[slots[i]]; vars[i].push_back((int)v.sl_rand_mask_[slots[i]]);
        int a[VESTIGE_TIMING_SLICE_MAX]; for (int k = 0; k < v.sl_n_[slots[i]]; k++) a[k] = v.sl_arr_[slots[i]][k];
        arr[i] = ArrStr(a, v.sl_n_[slots[i]]);
      }
    }
    Realign();
    int vdiff = 0, nvar[3] = {0, 0, 0}; const size_t m = std::min(vars[0].size(), std::min(vars[1].size(), vars[2].size()));
    for (size_t k = 0; k < m; k++) if (vars[0][k] != vars[1][k] || vars[1][k] != vars[2][k]) vdiff++;
    for (int i = 0; i < ns; i++) for (int x : vars[i]) nvar[i] += Pop((uint32_t)x);
    printf("      3 voices, level 0.5, 10 s: arrangements %s / %s / %s; random steps %d / %d / %d; random-step masks differ in %d of %zu pass indices\n",
           arr[0].c_str(), arr[1].c_str(), arr[2].c_str(), nvar[0], nvar[1], nvar[2], vdiff, m);
    Check(ns == 3 && arr[0] != arr[1] && arr[1] != arr[2] && arr[0] != arr[2], "3 voices: each has its own arrangement");
    Check(nvar[0] > 0 && nvar[1] > 0 && nvar[2] > 0 && vdiff >= (int)m / 2, "3 voices roll their random steps independently");
    v.err_level_[0] = 1.f;
    const float kmid = 0.5f + (VESTIGE_K1_DEADZONE + (0.5f - VESTIGE_K1_DEADZONE) * 0.5f);
    cs.knob[0] = kmid; RunFor(1.5f);
    max_grains = 0; max_counted = 0; const uint32_t d0 = v.grain_cap_drops_, pf0 = v.pool_full_, t0 = v.timing_trigs_;
    blk = 1; RunFor(10.0f); Realign();
    printf("      3 voices (200-300 ms loops), K1 midpoint, level 1, 10 s: %u jumps; counted max %d (budget 12), PHYSICAL max %d of %d, cap refusals %u, pool exhausted %u\n",
           v.timing_trigs_ - t0, max_counted, max_grains, VESTIGE_GRAINS, v.grain_cap_drops_ - d0, v.pool_full_ - pf0);
    Check(max_counted <= 12 && v.grain_cap_drops_ - d0 == 0, "slices, 3 voices + K1 midpoint: counted grains within 12, none refused");
    Check(v.pool_full_ - pf0 == 0 && max_grains < VESTIGE_GRAINS, "slices: the physical grain pool never runs out");
    v.err_level_[0] = 0.f; cs.knob[0] = 0.5f; cs.sw[0] = 0; RunFor(1.0f); }
  Unhold();
}

int main() {
  v.Init(sr);
  cs.sw[0] = 0; v.follow_mode_cfg_ = 0; cs.sw[2] = 0;
  cs.knob[1] = 0.85f;   // K2 CW: forward, long-ish
  SetK4Thresh(0.1f);    // K4 sensitive
  cs.knob[4] = K5Fade(0.05f);   // K5 short fades (0.3 s)
  v.Activate();

  TestMemoryLayout();
  // Before anything has run the freeze side is untouched memory.
  Check(AllZero(&vestige_freeze_slab[0][0], sizeof(vestige_freeze_slab) / sizeof(float)),
        "freeze slab zeroed at init");
  TestStage0();
  TestBufferSeparation();
  TestTimeBase();                  // the tap rule itself: real three-tap agreement
  // Later sections re-tap at sample-exact moments to test how loops FOLLOW T;
  // they keep the two-tap timing they were written for (host-only hook).
  v.tap_accept_one_ = true;
  TestQuantisedCapture();
  TestSpeedXfade();
  TestFollowTape();
  TestFollowStretch();
  TestOnsetRearm();
  TestGateMeter();
  // Discovery phase: TestTimingSlices models the fixed-arrangement concept the
  // builder dropped (e7afd92). Behaviour tests come back once the slice idea is
  // settled; the safety checks elsewhere still run over everything.
  if (VESTIGE_TIMING_MODE == 0) TestTimingError();
  else if (VESTIGE_TIMING_MODE == 3) TestTimingLayers();
  else if (VESTIGE_TIMING_FIXED_ARRANGEMENT) TestTimingSlices();
  else printf("-- slice / pass-memory mode: behaviour tests skipped (discovery phase)\n");
  TestK4Degrade();
  TestFadeVoiceCap();
  TestK5Repeats();
  TestFollowRecut();

  printf("max |wet| over run %.4f, non-finite/huge samples %d, rec overruns %ld\n", maxabs, bad, rec_overrun);
  Check(bad == 0, "no non-finite / >10 samples");
  // Errors: K3 sets all three layers' levels together; SW2 does nothing.
  { printf("-- errors: K3\n");
    { Vestige fresh; Check(fresh.err_level_[0] == 0.f && fresh.err_level_[1] == 0.f && fresh.err_level_[2] == 0.f,
                           "error levels start at 0 (no errors until K3 moves)"); }
    cs.sw[1] = 0; cs.knob[2] = 0.8f; RunFor(0.1f);
    const float t = RemapKnob(0.8f);
    Check(v.err_level_[0] == t && v.err_level_[1] == t && v.err_level_[2] == t, "K3 sets all three levels");
    cs.sw[1] = 2; RunFor(0.1f);
    Check(v.err_level_[0] == t && v.err_level_[1] == t && v.err_level_[2] == t, "SW2 does not change the error levels");
    cs.knob[2] = 0.f; RunFor(0.1f);
    Check(v.err_level_[0] == 0.f && v.err_level_[1] == 0.f && v.err_level_[2] == 0.f, "K3 CCW: all off");
    cs.sw[1] = 0; }
  // LED2 = effect state: off dark, on solid, recording rapid flicker, held slow blink.
  { printf("-- LED2\n");
    auto Sample = [](float secs, int& ons, int& toggles) {   // LED2 over secs, per 10 ms tick
      ons = 0; toggles = 0; bool prev = led2.v > 0.5f;
      for (int i = 0; i < (int)(secs * 100.f); i++) { RunFor(0.01f); const bool on = led2.v > 0.5f; if (on) ons++; if (on != prev) toggles++; prev = on; }
    };
    int ons, tg;
    Reset(); Unhold(); if (v.engaged_) Tap(); RunFor(0.5f);
    Sample(0.5f, ons, tg); Check(!v.engaged_ && ons == 0, "LED2 dark while the effect is off");
    Engage(); RunFor(0.2f);
    Sample(0.5f, ons, tg); Check(v.engaged_ && !v.recording_ && ons == 50 && tg == 0, "LED2 solid while on and not recording");
    Taps({2000}); sustain_hz = 110.f; sustain_input = true; play_input = true; RunFor(0.1f);
    Sample(0.4f, ons, tg); const bool rec = v.recording_;
    sustain_input = false; play_input = false; RunFor(1.0f);
    Check(rec && tg >= 8 && ons > 5 && ons < 35, "LED2 flickers rapidly while recording");
    Hold(); Sample(2.0f, ons, tg);
    Check(v.held_ && tg >= 2 && tg <= 6, "LED2 blinks slowly while held");
    Unhold(); RunFor(0.3f); }
  { const int cfg = v.follow_mode_cfg_; v.follow_mode_cfg_ = -1;
    int m[3]; for (int p = 0; p < 3; p++) { cs.sw[1] = p; RunFor(0.1f); m[p] = v.follow_mode_; }
    Check(m[0] == Vestige::kFollowTape && m[1] == Vestige::kFollowStretch && m[2] == Vestige::kFollowRecut,
          "SW2 selects the follow mode: UP tape, MIDDLE stretch, DOWN re-cut");
    cs.sw[1] = 0; v.follow_mode_cfg_ = cfg; RunFor(0.1f); }
  { Vestige fresh; Check(fresh.follow_mode_cfg_ == -1, "by default SW2 selects the follow mode"); }
  printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
  return fails ? 1 : 0;
}
