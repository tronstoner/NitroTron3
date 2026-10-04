// test_dynquapoteg.cpp — the time grid block (src/core/blocks/dynquapoteg.h)
// with vestige's configuration (vestige_constants.h, as Vestige::GridConfig()
// builds it). Pure maths, no module, no audio: deterministic and instant.
//
// Locks in (builder: "lock it in", 2026-10-04):
//   K2 taper   three log sections per half from noon: 5 ms .. 100 ms over the
//              first 15 % of the travel, .. 2 s up to 80 %, .. 8 s at the end;
//              noon dead zone = 5 ms; both halves alike; monotonic.
//   free-run   the outer 2 % at each end; a posted free length becomes T,
//              holds (also out of the zone) until the knob moves on, a tap
//              wins over it; pins release once it no longer is T; a free
//              capture is unquantised (raw, inside T_MIN..T_MAX, division 0).
//   floor      no loop below 240 samples: a quantised length under it is
//              extended, its division kept; follow / pass lengths too.
//   tap        valid intervals 5 ms .. 8 s, two agreeing intervals (15 %)
//              set T (their mean); a press >= 300 ms is not a tap.
//
// Build/run via tools/host/run.sh.
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#define private public
#include "dynquapoteg.h"
#undef private
#include "vestige_constants.h"

static int fails = 0;
static void Check(bool ok, const char* what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

// Vestige::GridConfig(), field by field (test_vestige checks the module uses
// the same values).
static DynquapotegConfig Cfg() {
  DynquapotegConfig c;
  c.t_min = VESTIGE_T_MIN_SAMPLES;   c.t_max = VESTIGE_T_MAX_SAMPLES;
  c.t_min_ms = VESTIGE_T_MIN_MS;     c.t_max_ms = VESTIGE_T_MAX_MS;
  c.loop_floor = VESTIGE_LOOP_MIN_LEN;
  c.knob_deadzone = VESTIGE_K2_DEADZONE; c.free_zone = VESTIGE_K2_FREE_ZONE;
  c.knob_move_eps = VESTIGE_K2_MOVE_EPS; c.knob_follow_db = VESTIGE_K2_FOLLOW_DB;
  c.knob_seg1 = VESTIGE_K2_SEG1;     c.knob_seg2 = VESTIGE_K2_SEG2;
  c.knob_t1 = (size_t)(VESTIGE_K2_T1_MS * 48); c.knob_t2 = (size_t)(VESTIGE_K2_T2_MS * 48);
  c.tap_release_ms = VESTIGE_TAP_RELEASE_MS; c.tap_agree = VESTIGE_TAP_AGREE;
  c.step_ms = VESTIGE_TIMING_STEP_MS; c.step_knee_ms = VESTIGE_TIMING_STEP_KNEE_MS; c.step_exp = VESTIGE_TIMING_STEP_EXP;
  c.max_steps = VESTIGE_TIMING_LAYER_MAX_STEPS; c.min_step_ms = VESTIGE_TIMING_MIN_STEP_MS;
  return c;
}
static bool Near(double a, double b, double rel) { return fabs(a - b) <= rel * b; }

int main() {
  const float sr = 48000.f;
  const DynquapotegConfig cfg = Cfg();

  // ---- the settled numbers themselves ---------------------------------------
  Check(VESTIGE_T_MIN_SAMPLES == 240 && VESTIGE_T_MAX_SAMPLES == 384000 && VESTIGE_LOOP_MIN_LEN == 240 &&
        cfg.knob_t1 == 4800 && cfg.knob_t2 == 96000 && VESTIGE_K2_SEG1 == 0.15f && VESTIGE_K2_SEG2 == 0.80f &&
        VESTIGE_K2_FREE_ZONE == 0.02f && VESTIGE_T_MIN_MS == 5 && VESTIGE_T_MAX_MS == 8000,
        "config: T 5 ms .. 8 s, K2 sections 15 % / 80 % at 100 ms / 2 s, free zone 2 %, loop floor 240");

  // ---- K2 taper ----------------------------------------------------------------
  { Dynquapoteg g; g.Init(cfg, sr);
    const float dz = cfg.knob_deadzone;
    auto pos = [&](float mag, int side) { return 0.5f + (float)side * (dz + mag * (0.5f - dz)); };
    struct B { float mag; double t; const char* name; };
    const B bounds[] = { {0.f, 240, "noon edge 5 ms"}, {VESTIGE_K2_SEG1, 4800, "15 % 100 ms"},
                         {VESTIGE_K2_SEG2, 96000, "80 % 2 s"}, {1.f, 384000, "end 8 s"} };
    bool b_ok = true, cont_ok = true;
    for (int side = -1; side <= 1; side += 2)
      for (const B& b : bounds) {
        const size_t t = g.KnobPeriod(pos(b.mag, side));
        printf("      side %+d %-14s T %zu (want %.0f)\n", side, b.name, t, b.t);
        if (!Near((double)t, b.t, 0.005)) b_ok = false;
        if (b.mag > 0.f && b.mag < 1.f) {                     // continuous across a section bound
          const double lo = (double)g.KnobPeriod(pos(b.mag - 1e-4f, side)), hi = (double)g.KnobPeriod(pos(b.mag + 1e-4f, side));
          if (!Near(lo, b.t, 0.01) || !Near(hi, b.t, 0.01)) cont_ok = false;
        }
      }
    Check(b_ok, "K2 taper: 5 ms at the noon dead zone, 100 ms at 15 %, 2 s at 80 %, 8 s at the end (both halves)");
    Check(cont_ok, "K2 taper: continuous across the section bounds");
    // Inside the dead zone: T_MIN. Each section logarithmic: its mid-travel = the geometric mean.
    Check(g.KnobPeriod(0.5f) == 240 && g.KnobPeriod(0.5f + 0.9f * dz) == 240 && g.KnobPeriod(0.5f - 0.9f * dz) == 240,
          "K2 noon dead zone: T = T_MIN (5 ms)");
    const double gm[3] = { sqrt(240.0 * 4800.0), sqrt(4800.0 * 96000.0), sqrt(96000.0 * 384000.0) };
    const float mid[3] = { 0.5f * VESTIGE_K2_SEG1, 0.5f * (VESTIGE_K2_SEG1 + VESTIGE_K2_SEG2), 0.5f * (VESTIGE_K2_SEG2 + 1.f) };
    bool log_ok = true;
    for (int i = 0; i < 3; i++) for (int side = -1; side <= 1; side += 2) {
      const double t = (double)g.KnobPeriod(pos(mid[i], side));
      if (!Near(t, gm[i], 0.01)) log_ok = false;
    }
    Check(log_ok, "K2 taper: each section logarithmic (mid-travel = geometric mean of its bounds)");
    bool mono = true; size_t prev = 0;
    for (int i = 0; i <= 2000; i++) { const size_t t = g.KnobPeriod(0.5f + 0.5f * (float)i / 2000.f); if (t < prev) mono = false; prev = t; }
    prev = 0;
    for (int i = 0; i <= 2000; i++) { const size_t t = g.KnobPeriod(0.5f - 0.5f * (float)i / 2000.f); if (t < prev) mono = false; prev = t; }
    Check(mono, "K2 taper: monotonic away from noon on both halves"); }

  // ---- free-run zone --------------------------------------------------------------
  { Dynquapoteg g; g.Init(cfg, sr);
    uint32_t ms = 1000;
    auto knob = [&](float k) { ms += 10; g.UpdatePeriod(k, k, false, false, ms, nullptr); };
    bool zone_ok = true;
    for (float k : {0.f, 0.01f, 0.02f, 0.98f, 0.99f, 1.f}) { knob(k); if (!g.InFreeZone()) zone_ok = false; }
    for (float k : {0.03f, 0.3f, 0.5f, 0.7f, 0.97f}) { knob(k); if (g.InFreeZone()) zone_ok = false; }
    Check(zone_ok, "free-run zone: the outer 2 % at each end of K2 (<= 0.02, >= 0.98), nowhere else");
    knob(0.85f);
    const size_t t_knob = g.T();
    knob(0.99f);
    const size_t Q = 50000;                                   // a free capture decided (audio thread)
    g.PostFree(Q);
    const bool held0 = g.PinsHeld() && !g.TakePinRelease();
    knob(0.99f);
    const bool adopt = g.T() == Q && !g.TakePinRelease();
    knob(0.985f); knob(1.f); knob(0.98f);
    const bool wiggle = g.T() == Q && !g.TakePinRelease();
    knob(0.978f);                                             // out of the zone, but not moved on (follow deadband)
    const bool kept = g.T() == Q && !g.InFreeZone() && !g.TakePinRelease();
    knob(0.95f);                                              // moved on: the knob sets T again, pins release once
    const bool moved = g.T() == g.KnobPeriod(0.95f);
    const bool rel1 = g.TakePinRelease(), rel2 = g.TakePinRelease();
    printf("      knob T %zu; free post %zu -> T %zu; knob moved on -> T %zu\n", t_knob, Q, (size_t)Q, g.T());
    Check(held0 && adopt, "free-run: a posted free length becomes T (pins held while it is T)");
    Check(wiggle, "free-run: moving inside the zone is no move (T stays the free length)");
    Check(kept, "free-run: leaving the zone by less than the follow deadband keeps the free T");
    Check(moved && rel1 && !rel2, "free-run: moving on hands T back to the knob; the pins release exactly once");
    // A tap wins over a free T.
    knob(0.99f); g.PostFree(30000); knob(0.99f);
    const bool adopt2 = g.T() == 30000;
    g.SetTapAcceptOne(true);
    ms += 20000; g.UpdatePeriod(0.99f, 0.99f, true, false, ms, nullptr); g.UpdatePeriod(0.99f, 0.99f, false, true, ms + 10, nullptr);
    ms += 500;   g.UpdatePeriod(0.99f, 0.99f, true, false, ms, nullptr); g.UpdatePeriod(0.99f, 0.99f, false, true, ms + 10, nullptr);
    Check(adopt2 && g.T() == 24000 && g.TakePinRelease(), "free-run: a tap wins over the free T (pins release)");
    // Free capture length: unquantised, inside T_MIN .. T_MAX, division 0.
    int d = -9; const size_t a = g.DecideLen(33333, 96000, true, &d);
    int d2 = -9; const size_t b = g.DecideLen(100, 96000, true, &d2);
    int d3 = -9; const size_t c = g.DecideLen(500000, 96000, true, &d3);
    int d4 = -9; const size_t e = g.DecideLen(33333, 96000, false, &d4);
    Check(a == 33333 && d == 0 && b == 240 && d2 == 0 && c == 384000 && d3 == 0,
          "free capture: Q = the raw length (kept inside 5 ms .. 8 s), division 0 (1/1 of itself)");
    Check(e == 32000 && d4 == 4, "quantised capture (not free): the nearest division of T (33333 of 96000 -> 1/3)"); }

  // ---- loop floor ----------------------------------------------------------------
  { Dynquapoteg g; g.Init(cfg, sr);
    int d = -9, d2 = -9, d3 = -9;
    const size_t q1 = g.DecideLen(130, 960, false, &d);       // nearest 120 = 1/8 -> 240
    const size_t q2 = g.DecideLen(150, 960, false, &d2);      // nearest 160 = 1/6 -> 240
    const size_t q3 = g.DecideLen(300, 960, false, &d3);      // nearest 320 = 1/3: above the floor
    printf("      T 960: raw 130 -> %zu (div %d), raw 150 -> %zu (div %d), raw 300 -> %zu (div %d)\n", q1, d, q2, d2, q3, d3);
    Check(q1 == 240 && d == 7 && q2 == 240 && d2 == 6 && q3 == 320 && d3 == 4,
          "loop floor: a quantised length under 240 samples is extended to 240, its division kept");
    Check(g.LoopFloor(1) == 240 && g.LoopFloor(239) == 240 && g.LoopFloor(240) == 240 && g.LoopFloor(241) == 241,
          "loop floor: 240 samples (5 ms), nothing above it changes");
    g.period_ = 960;
    Check(g.FollowLen(240, 7) == 240 && g.FollowLen(240, 6) == 240 && g.PassLen(7, 0) == 240 && g.PassLen(4, 0) == 320 &&
          g.LoopBoundary(7, 1920) == 240 && g.LoopBoundary(7, 4000) == 500 && g.LoopBoundary(0, 960) == 960,
          "loop floor: follow-T targets and virtual passes are floored too (Boundary(d, T) < 240 -> 240)");
    g.period_ = 4000;
    Check(g.FollowLen(240, 7) == 500 && g.FollowLen(240, 6) == 667, "loop floor: a floored loop follows T by its division again once T grows"); }

  // ---- tap ranges ------------------------------------------------------------------
  { Dynquapoteg g; g.Init(cfg, sr);
    uint32_t ms = 1000;
    g.UpdatePeriod(0.85f, 0.85f, false, false, ms, nullptr);
    const size_t knobT = g.T();
    // Three taps iv ms apart (press 1 ms), after a pause that breaks any chain.
    auto taps = [&](uint32_t iv, uint32_t iv2, uint32_t press = 1) {
      ms += 20000;
      for (uint32_t t : {0u, iv, iv + iv2}) {
        g.UpdatePeriod(0.85f, 0.85f, true, false, ms + t, nullptr);
        g.UpdatePeriod(0.85f, 0.85f, false, true, ms + t + press, nullptr);
      }
      ms += iv + iv2 + press;
      return g.T();
    };
    const size_t t5 = taps(5, 5);
    const size_t t4 = taps(4, 4);
    const size_t t8k = taps(8000, 8000);
    const size_t t8k1 = taps(8001, 8001);
    const size_t tlong = taps(500, 500, 300);
    const size_t tag = taps(500, 570);
    const size_t tdis = taps(1000, 1200);
    printf("      knob T %zu | taps 5 ms -> %zu, 4 ms -> %zu, 8000 ms -> %zu, 8001 ms -> %zu, 300 ms presses -> %zu, 500/570 -> %zu, 1000/1200 -> %zu\n",
           knobT, t5, t4, t8k, t8k1, tlong, tag, tdis);
    Check(t5 == 240, "tap: 5 ms intervals are valid (T = 240 samples)");
    Check(t4 == 240, "tap: 4 ms intervals are out of range (T unchanged)");
    Check(t8k == 384000, "tap: 8000 ms intervals are valid (T = 8 s)");
    Check(t8k1 == 384000, "tap: 8001 ms intervals are out of range (T unchanged)");
    Check(tlong == 384000, "tap: presses of 300 ms are not taps (T unchanged)");
    Check(tag == 25680, "tap: two intervals within 15 % set T to their mean (500 / 570 ms -> 535 ms)");
    Check(tdis == 25680, "tap: intervals 20 % apart do not agree (T unchanged)"); }

  printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
  return fails ? 1 : 0;
}
