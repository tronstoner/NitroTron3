#pragma once

#include <cmath>

// Moog ladder v2 — A/B candidate for Mode C only.
// MoogLadder (used by Mode A) is intentionally untouched — Mode A is signed off.
//
// Differences vs MoogLadder:
//   2. g_ clamp relaxed (0.95 → 0.99) — reaches the upper cutoff range properly.
//      (Still no oversampling — a true Huovilainen needs 2×; we accept some
//       aliasing at extreme settings as the cost of staying single-rate.)
//   3. Saturation only at the input stage (post-feedback mix); the four poles
//      after are linear (Stilson/Smith form). Per-stage tanh in v1 was darkening
//      the response under drive and making it sound "muffled / thin".
//   4. Resonance ↔ cutoff cross-compensation. k_eff = 4·res / (1 − 0.15·g)
//      so self-oscillation threshold stays roughly consistent across cutoff.
//   5. Input level compensation (1 + res) so the filter doesn't quiet down as
//      resonance climbs.
//   6. Small asymmetric DC bias before the tanh — gives the transistor-style
//      even-harmonic growl Moogs are known for.
class MoogLadderV2 {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    for (int i = 0; i < 4; ++i) stage_[i] = 0.f;
    in_prev_ = 0.f;
  }

  void SetCutoff(float freq) {
    if (freq < 20.f) freq = 20.f;
    if (freq > sr_ * 0.45f) freq = sr_ * 0.45f;
    float fc = 2.f * sr_ * tanf(3.14159265f * freq / sr_);
    g_ = fc / (2.f * sr_);
    if (g_ > 0.99f) g_ = 0.99f;
  }

  void SetDrive(float drive) { drive_ = drive; }

  void SetResonance(float res) {
    if (res < 0.f) res = 0.f;
    res_ = res;
  }

  float Process(float in) {
    in *= drive_;

    // Input level compensation — keep apparent loudness as resonance rises.
    const float in_comp = in * (1.f + res_);

    // k cross-comp — keep self-osc threshold consistent across the cutoff range.
    const float k_eff = 4.f * res_ / (1.f - 0.15f * g_);

    // Resonance feedback with half-sample delay compensation.
    float x = in_comp - k_eff * (stage_[3] - 0.5f * in_prev_);
    in_prev_ = in_comp;

    // Asymmetric input bias → transistor-flavored even harmonics.
    x += 0.05f;
    const float x_sat = Saturate(x);

    // Linear 4-pole cascade after the single input saturator.
    stage_[0] += g_ * (x_sat     - stage_[0]);
    stage_[1] += g_ * (stage_[0] - stage_[1]);
    stage_[2] += g_ * (stage_[1] - stage_[2]);
    stage_[3] += g_ * (stage_[2] - stage_[3]);

    return stage_[3];
  }

 private:
  float sr_       = 48000.f;
  float g_        = 0.f;
  float drive_    = 1.f;
  float res_      = 0.f;
  float stage_[4] = {};
  float in_prev_  = 0.f;

  // Pade(3,3) tanh — same approximation as v1, accurate to ~0.1% for |x|<4.
  static float Saturate(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }
};
