// test_pitch_grains.cpp — transposed-grain behaviour across the K2 travel.
//
// Covers the two things GRAIN_PITCH_SHORT_GRAINS changes, on the REAL Sprawl
// module with stubbed hardware:
//
//   1. GATING. Capping the grain without re-deriving the hop emits a short grain
//      once per uncapped interval, i.e. 50 ms of audio per 300 ms = a hard
//      tremolo. Measured as the modulation depth of the wet envelope.
//   2. SLAPBACK. The artifact itself: at ratio r every source sample is played
//      2*r times, one hop apart. The hop is what decides whether those copies
//      fuse (timbre) or separate (slapback), so this asserts the emitted grain
//      length and the hop stay put as K2 opens instead of scaling with it.
//
// Setup: SW2 UP (fixed interval), K1 full CW = +12 semitones, K3 at the noon pad
// (no glitch, no cloud, no diffusion), K4 = 0 (texture clean), K5 noon (no
// feedback, no reverb). Input is a steady sine, so anything periodic in the wet
// envelope comes from the scheduler, not from the source.
//
// Build/run via tools/host/run.sh.
//
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <new>
#include <vector>
#include "daisy.h"
#include "hothouse.h"
#include "control_surface.h"
#include "knob_map.h"
#include "sprawl.h"
uint32_t daisy::System::now_ms = 0;

static Sprawl sprawl;
static ControlSurface cs;
static daisy::Led led1, led2;

static constexpr float kSR = 48000.f;

struct Result {
  float depth;          // slow-envelope modulation depth, 0 = steady, 1 = gated
  float silent_duty;    // fraction of the time the wet is essentially silent
  uint32_t grain_len;   // grain length actually handed to the voices
  uint32_t hop;         // measured samples between grain launches
  uint32_t base_delay;
  float    rate;        // pitch ratio the engine used
};

// Runs `seconds` of a steady sine at one K2 setting and reports what the
// scheduler did. `settle` seconds at the start are discarded so the ring is full
// and the delay has established before anything is measured.
static Result Run(float k2, float seconds, float settle) {
  sprawl.~Sprawl(); new (&sprawl) Sprawl();
  sprawl.Init(kSR);
  cs = ControlSurface();
  cs.sw[0] = 0;        // SW1 UP  — texture mode (amount is 0 below anyway)
  cs.sw[1] = 0;        // SW2 UP  — fixed interval
  cs.sw[2] = 2;        // SW3 DOWN — sprawl
  cs.knob[0] = 1.0f;   // K1 full CW = +12 semitones
  cs.knob[1] = k2;
  cs.knob[2] = 0.5f;   // K3 noon pad: no glitch, no cloud, no diffusion
  cs.knob[3] = 0.0f;   // K4 texture clean
  cs.knob[4] = 0.5f;   // K5 noon: no feedback, no reverb
  cs.knob[5] = 0.5f;
  daisy::System::now_ms = 0;
  sprawl.Activate();
  sprawl.Controls(cs, led1, led2);

  const long total  = (long)(seconds * kSR);
  const long ignore = (long)(settle  * kSR);
  float in[48], wet[48];

  // Envelope in 20 ms windows (RMS), collected only after settling. The window
  // is deliberately long: granular pitch shifting ALWAYS ripples at the hop rate
  // (overlapping grains read the same material at different phases -- that is
  // the old-school-shifter character, present in the tuned K2-noon case too), so
  // a short window cannot tell that ripple apart from gating. At 20 ms the
  // 40 Hz ripple averages out while a 3 Hz gate stands right out.
  const int kWin = 960;
  std::vector<float> env;
  double acc = 0.0; int acc_n = 0;

  // Grain launches: a voice going inactive -> active is one launch. Gaps between
  // launches give the hop the scheduler actually used.
  bool was_active[8] = {};
  std::vector<long> launches;
  uint32_t seen_len = 0;
  float    seen_rate = 1.f;
  SprawlDebug d;

  for (long n = 0; n < total; n += 48) {
    if (n % 480 == 0) { daisy::System::now_ms += 10; sprawl.Controls(cs, led1, led2); }
    for (int i = 0; i < 48; i++) {
      const float ph = 2.f * (float)M_PI * 220.f * (float)(n + i) / kSR;
      in[i] = 0.25f * sinf(ph);
    }
    sprawl.Process(in, wet, 48);

    sprawl.DebugFillLive(d);
    for (int v = 0; v < 8; v++) {
      const bool a = d.v[v].active != 0;
      if (a && !was_active[v] && n >= ignore) {
        launches.push_back(n);
        seen_len  = d.v[v].len;
        seen_rate = d.v[v].rate;
      }
      was_active[v] = a;
    }

    for (int i = 0; i < 48; i++) {
      acc += (double)wet[i] * wet[i];
      if (++acc_n == kWin) {
        if (n >= ignore) env.push_back((float)sqrt(acc / kWin));
        acc = 0.0; acc_n = 0;
      }
    }
  }

  Result r{};
  r.grain_len  = seen_len;
  r.rate       = seen_rate;
  r.base_delay = d.base_delay;

  // Modulation depth, robust to the odd outlier: (p90 - p10) / (p90 + p10).
  if (env.size() > 20) {
    std::vector<float> s = env;
    std::sort(s.begin(), s.end());
    const float lo  = s[s.size() / 10];
    const float hi  = s[s.size() * 9 / 10];
    const float med = s[s.size() / 2];
    r.depth = (hi + lo) > 1e-9f ? (hi - lo) / (hi + lo) : 0.f;
    // Gating's real signature: long stretches at (near) zero. A continuous
    // overlap-add stream never goes quiet, however much it ripples.
    int quiet = 0;
    for (float e : s) if (e < med * 0.15f) quiet++;
    r.silent_duty = (float)quiet / (float)s.size();
  }

  // Median gap between launches = the hop. Median, because several voices can
  // start within one block and the odd gap is a dropped grain.
  if (launches.size() > 8) {
    std::vector<long> gaps;
    for (size_t i = 1; i < launches.size(); i++) {
      const long g = launches[i] - launches[i - 1];
      if (g > 0) gaps.push_back(g);
    }
    if (!gaps.empty()) {
      std::sort(gaps.begin(), gaps.end());
      r.hop = (uint32_t)gaps[gaps.size() / 2];
    }
  }
  return r;
}

int main(int argc, char** argv) {
  const float secs = (argc > 1) ? (float)atof(argv[1]) : 6.f;

  // K2 settings: noon (live passthrough, the tuned reference), then three
  // increasingly deep buffer settings.
  struct Case { const char* name; float k2; float settle; };
  const Case cases[] = {
    {"K2 noon  (live)", 0.50f, 1.0f},
    {"K2 0.62  (short)", 0.62f, 1.5f},
    {"K2 0.75  (mid)",   0.75f, 3.0f},
    {"K2 1.00  (deep)",  1.00f, 4.0f},
  };

  printf("  %-17s  %7s  %6s  %9s  %7s  %9s  %5s\n",
         "case", "AMdepth", "quiet", "grain_ms", "hop_ms", "delay_ms", "rate");

  int fail = 0;
  float ref_len = 0.f, ref_hop = 0.f;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const Result r = Run(cases[i].k2, secs, cases[i].settle);
    const float glen_ms  = (float)r.grain_len * 1000.f / kSR;
    const float hop_ms   = (float)r.hop       * 1000.f / kSR;
    const float delay_ms = (float)r.base_delay * 1000.f / kSR;
    printf("  %-17s  %7.3f  %6.3f  %9.1f  %7.1f  %9.1f  %5.2f\n",
           cases[i].name, r.depth, r.silent_duty, glen_ms, hop_ms, delay_ms, r.rate);
    if (i == 0) {
      ref_len = glen_ms; ref_hop = hop_ms;
      // The K2-noon pass-through shifter is TUNED and must not move: 150 ms
      // grains, 75 ms hop. Shortening it combs (metallic), lengthening it
      // spreads the copies into slapback.
      if (fabsf(glen_ms - 150.f) > 1.f || fabsf(hop_ms - 75.f) > 1.f) {
        printf("     FAIL  tuned K2-noon shifter moved: %.1f ms grain / %.1f ms hop,"
               " expected 150.0 / 75.0\n", glen_ms, hop_ms);
        fail = 1;
      }
    }

    // 1. No gating. A 50 ms grain emitted once per 300 ms is quiet ~83% of the
    //    time; a continuous stream is never quiet. The depth check backs it up.
    if (r.silent_duty > 0.20f) {
      printf("     FAIL  wet is silent %.0f%% of the time — the stream is gated\n",
             r.silent_duty * 100.f);
      fail = 1;
    }
    if (r.depth > 0.60f) {
      printf("     FAIL  slow-envelope depth %.3f — the stream is not continuous\n", r.depth);
      fail = 1;
    }
    // 2. The transposed grain must not grow with K2. Allow a little slack for
    //    the median and for the live case deriving its own length.
    if (glen_ms > ref_len * 1.5f + 5.f) {
      printf("     FAIL  grain %.1f ms grew with K2 (reference %.1f ms)\n", glen_ms, ref_len);
      fail = 1;
    }
    if (hop_ms > ref_hop * 1.5f + 5.f) {
      printf("     FAIL  hop %.1f ms grew with K2 (reference %.1f ms)\n", hop_ms, ref_hop);
      fail = 1;
    }
    // 3. Sanity: the engine really is transposing, or the test proves nothing.
    if (fabsf(r.rate - 2.f) > 0.01f) {
      printf("     FAIL  pitch ratio %.3f, expected 2.0 (+12 semitones)\n", r.rate);
      fail = 1;
    }
  }

  printf("%s\n", fail ? "  RESULT: FAIL" : "  RESULT: ok");
  return fail;
}
