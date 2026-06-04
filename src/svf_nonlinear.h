#pragma once

#include <cmath>

// Wavefolding resonant filter — sin() in the explicit resonance feedback
// path. The fold output is bounded to ±1 regardless of input amplitude, so
// the recirculating loop produces chaotic, amplitude-dependent folding
// rather than a settled distorted limit: folded resonance signal sums with
// fresh input on each sample and is folded again, never settling into a
// stable distorted shape. Classic Chamberlin SVF topology (one-sample
// delay in the resonance return) so the nonlinearity sits explicitly in
// the loop rather than implicitly in coefficient algebra.
//
// Soft tanh on integrator state is a far-field safety net only; the bp_
// state is allowed to roam freely through the folding region (several
// radians) where the chaos lives.
class SvfNonlinear {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    bp_ = 0.f;
    lp_ = 0.f;
    SetCutoff(440.f);
    SetDamping(1.f);
  }

  void SetCutoff(float hz) {
    if (hz < 20.f) hz = 20.f;
    // Chamberlin stability bound; safe at high resonance.
    if (hz > sr_ * 0.15f) hz = sr_ * 0.15f;
    f_ = 2.f * sinf(3.14159265f * hz / sr_);
  }

  // Damping k. k=0 → max resonance; k=2 → moderate.
  void SetDamping(float k) {
    if (k < 0.f) k = 0.f;
    k_ = k;
  }

  float ProcessBp(float in) {
    const float fold = sinf(bp_);
    const float hp = in - k_ * fold - lp_;
    bp_ += f_ * hp;
    lp_ += f_ * bp_;
    bp_ = SoftBound(bp_);
    lp_ = SoftBound(lp_);
    return bp_;
  }

 private:
  float sr_ = 48000.f;
  float f_ = 0.f, k_ = 1.f;
  float bp_ = 0.f, lp_ = 0.f;

  // Soft tanh with generous scale — asymptotes near ±6. Operating range
  // for the fold (≈ ±π) sits in the near-linear region; the soft asymptote
  // only engages on pathological excursions. The ±3 guards are Pade(3,3)
  // math-domain bounds, not signal clamps.
  static float SoftBound(float x) {
    constexpr float k_scale = 6.f;
    constexpr float inv_scale = 1.f / k_scale;
    const float xs = x * inv_scale;
    if (xs > 3.f) return k_scale;
    if (xs < -3.f) return -k_scale;
    const float xs2 = xs * xs;
    return k_scale * xs * (27.f + xs2) / (27.f + 9.f * xs2);
  }
};
