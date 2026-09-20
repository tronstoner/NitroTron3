#pragma once
//
// diffuser.h — modulated allpass diffuser, after Mutable Instruments Clouds /
// Parasites.
//
// In Parasites' LOOPING DELAY mode this is what the DENSITY knob drives:
// `diffuser_.set_amount(parameters_.density)`, applied to the playback output.
// An allpass leaves the magnitude spectrum alone and only scrambles phase, so a
// transient is spread into dense decaying hash while sustained material passes
// through unchanged. That is what washes attacks out without repeating them,
// without flattening dynamics and without touching timing — the thing a grain
// cloud cannot do, because a cloud can only copy a transient around.
//
// STRUCTURE = CLOUDS', EXACTLY. Four stages, their tap lengths scaled from
// 32 kHz to 48 kHz, their coefficient. Verified on the host against their real
// code (fx_engine.h + fx/diffuser.h compile and run there): same largest tap
// (0.153 = g^4), same energy distribution, same flat response.
//
// Deeper chains were tried and are WORSE, not better. Their strength comes from
// the signal passing these four stages again on every repeat of the delay, not
// from depth on a single pass. Eight or sixteen stages multiply the allpass
// ringing per pass, and that ringing IS the metallic character; they also
// spread the energy thin, which reads as weaker rather than stronger, and move
// the loudest tap tens of ms late, which reads as a slap.
//
// ONE CHAIN IN SERIES, DELIBERATELY. A series allpass chain is unity gain at
// every frequency — that is what makes it an allpass, and what lets it sit
// inside a feedback loop safely. Two chains SUMMED are not: where they align
// they add, so the sum exceeds unity somewhere and a feedback loop will find
// that frequency and whistle. Measured: two chains scaled by sqrt(2) peak at
// +4.3 dB. Clouds only ever sums its two chains in the listener's ears, never
// inside the loop, and when it does it averages (x0.5), which measures 0.00 dB.
//
// MODULATION is the one deliberate addition. A static allpass network has fixed
// resonances, and exciting them repeatedly through a feedback loop is what makes
// a diffuser sing. Sweeping the delays keeps any one resonance from being fed
// continuously and turns the fixed comb into a moving one. Clouds' own reverb
// does exactly this on its first allpass, with an LFO of amplitude 60 samples,
// described upstream as "additional smearing"; their bare diffuser does not,
// but their diffuser also never sits behind a 16-stage chain.
//
#include <cstddef>

class Diffuser {
 public:
  static constexpr int kStages = 4;

  // Clouds' coefficient, unchanged.
  static constexpr float kAp = 0.625f;

  // Delay modulation. Depth is in samples, swept from the nominal length
  // downward; rates are mutually incommensurate so the sweeps never line up and
  // the pattern never repeats. Depth 0 = the static Clouds diffuser.
  static constexpr float kModDepth = 90.f;                       // ~1.9 ms (Clouds reverb uses 60 @32k)
  static constexpr float kModHz[kStages] = {0.31f, 0.47f, 0.19f, 0.67f};

  void Init(float sr) {
    for (int i = 0; i < kTotal; i++) buf_[i] = 0.f;
    for (int s = 0; s < kStages; s++) {
      idx_[s]       = 0;
      lfo_phase_[s] = (float)s * 0.25f;          // staggered starts
      lfo_inc_[s]   = kModHz[s] / sr;
    }
  }

  // One sample. `amount` is the dry/wet of the chain (0 = bypass), mixed as
  // Clouds does it: out += amount * (wet - out).
  float Process(float x, float amount) {
    if (amount <= 0.f) return x;
    const int* delay = DelayTable();
    float* p   = buf_;
    float  acc = x;
    for (int s = 0; s < kStages; s++) {
      const int n = delay[s];
      const int i = idx_[s];

      // Reading FORWARD of the write index by `off` shortens this stage's delay
      // to n - off, so the tap sweeps between n - kModDepth and n.
      lfo_phase_[s] += lfo_inc_[s];
      if (lfo_phase_[s] >= 1.f) lfo_phase_[s] -= 1.f;
      const float off = kModDepth * 0.5f * (1.f + Sine(lfo_phase_[s]));
      float r = (float)i + off;
      while (r >= (float)n) r -= (float)n;
      const int   r0 = (int)r;
      const int   r1 = (r0 + 1 == n) ? 0 : (r0 + 1);
      const float fr = r - (float)r0;
      const float d  = p[r0] + (p[r1] - p[r0]) * fr;

      const float v = acc + kAp * d;     // v[n] = x[n] + g*v[n-D]
      p[i] = v;
      acc = d - kAp * v;                 // y[n] = v[n-D] - g*v[n]
      idx_[s] = (i + 1 == n) ? 0 : (i + 1);
      p += n;
    }
    return x + amount * (acc - x);
  }

 private:
  // Parabolic sine, -1..1 over phase 0..1. Smooth enough for an LFO and far
  // cheaper than sinf at audio rate.
  static float Sine(float ph) {
    const float t = ph * 2.f - 1.f;              // -1..1
    const float a = t < 0.f ? -t : t;
    return 4.f * t * (1.f - a) * 0.9f;
  }

  // Clouds' 126 / 180 / 269 / 444 at 32 kHz, x1.5 for 48 kHz.
  static const int* DelayTable() {
    static const int d[kStages] = {189, 270, 404, 666};
    return d;
  }
  static constexpr int kTotal = 189 + 270 + 404 + 666;   // 1529 floats, ~6 kB

  float buf_[kTotal] = {};
  int   idx_[kStages] = {};
  float lfo_phase_[kStages] = {};
  float lfo_inc_[kStages]   = {};
};
