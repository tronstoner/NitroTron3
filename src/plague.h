#pragma once

#include <cmath>
#include "svf_nonlinear.h"
#include "constants.h"

// Plague filter — Flight of Harmony Plague Bearer model.
// Two parallel saturating SVFs at fixed corners (PLAGUE_LOW_HZ / PLAGUE_HIGH_HZ).
// K1 = constant-sum input balance (bipolar around noon).
// K2 = tandem intensity (pre-saturation drive AND per-band feedback drive).
// K3 (handled by caller) = bipolar env modulation on K1.
class Plague {
 public:
  void Init(float sample_rate) {
    lo_.Init(sample_rate);
    hi_.Init(sample_rate);
    lo_.SetCutoff(PLAGUE_LOW_HZ);
    hi_.SetCutoff(PLAGUE_HIGH_HZ);
    k1_signed_ = 0.f;
    k2_input_  = 0.f;
  }

  // Per-block: k1_knob and k2_knob in [0, 1].
  void SetParams(float k1_knob, float k2_knob) {
    k1_signed_ = k1_knob - 0.5f;                 // [-0.5, +0.5]
    k2_input_  = k2_knob * PLAGUE_INPUT_RATIO;
    // Tandem feedback drive → SVF damping. k2_fb in [BASE, BASE+RANGE].
    const float k2_fb   = PLAGUE_FB_BASE + k2_knob * PLAGUE_FB_RANGE;
    const float damping = 2.f * (1.f - k2_fb);   // k=2 at fb=0, k≈0 at fb=1
    lo_.SetDamping(damping);
    hi_.SetDamping(damping);
  }

  // Per-sample. env_contribution = k3_signed * env_val (signed, ~[-1, +1]).
  float Process(float in, float env_contribution) {
    // Constant-sum input balance with env shift.
    float bal = k1_signed_ + env_contribution * PLAGUE_BALANCE_ENV_SCALE;
    if (bal < -0.5f) bal = -0.5f;
    if (bal >  0.5f) bal =  0.5f;
    const float bal_lo = 0.5f - bal;
    const float bal_hi = 0.5f + bal;

    const float driven = in * k2_input_;
    const float lo_out = lo_.ProcessBp(driven * bal_lo);
    const float hi_out = hi_.ProcessBp(driven * bal_hi);

    // Output tanh bounds peaks after the band sum.
    return SoftClip((lo_out + hi_out) * PLAGUE_OUT_GAIN);
  }

 private:
  SvfNonlinear lo_, hi_;
  float k1_signed_ = 0.f;
  float k2_input_  = 0.f;

  // tanh approximation — Pade (3,3).
  static float SoftClip(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }
};
