#pragma once

#include <cmath>
#include "constants.h"

// Post-filter peak limiter for Mode C (Schism).
//
// Goals:
//   - Tame resonance/peak excursions from the Moog ladder, Grendel formants,
//     and Plague filter before they hit the DAC and clip.
//   - Preserve bass fundamentals: a one-pole 2-band split at MODE_C_LIMIT_SPLIT_HZ
//     routes LF straight through; only HF passes through the limiter, so the
//     low end never ducks with peaks above the crossover.
//   - "Compress toward fundamentals" — no waveshaping on clean signals; only
//     when the limiter actually does work does a small amount of tanh-warmth
//     bloom in (proportional to gain-reduction depth). Spectrum stays clean
//     when nothing is being limited.
//
// Single Init(sr); single Process(x). All tuning constants live in constants.h.
class PeakLimiter {
 public:
  void Init(float sample_rate) {
    sr_       = sample_rate;
    env_      = 0.f;
    lp_state_ = 0.f;
    // 2-band crossover one-pole.
    lp_alpha_ = 1.f - expf(-2.f * 3.14159265f * MODE_C_LIMIT_SPLIT_HZ / sr_);
    // Envelope one-pole coefficients: c = exp(-1 / (t_seconds * sr)).
    atk_ = expf(-1.f / (MODE_C_LIMIT_ATK_MS * 0.001f * sr_));
    rel_ = expf(-1.f / (MODE_C_LIMIT_REL_MS * 0.001f * sr_));
  }

  float Process(float x) {
    // --- 2-band split: LF preserved, HF goes to the limiter. ---
    lp_state_ += lp_alpha_ * (x - lp_state_);
    const float lf = lp_state_;
    const float hf = x - lf;

    // --- Peak envelope follower (asymmetric attack/release). ---
    const float abs_h = (hf >= 0.f) ? hf : -hf;
    const float coef  = (abs_h > env_) ? atk_ : rel_;
    env_ = abs_h + (env_ - abs_h) * coef;

    // --- Soft-knee gain reduction: asymptote toward threshold, never clips. ---
    float gr = 1.f;
    if (env_ > MODE_C_LIMIT_THR) {
      const float over = env_ - MODE_C_LIMIT_THR;
      gr = MODE_C_LIMIT_THR / (MODE_C_LIMIT_THR + over * MODE_C_LIMIT_RATIO_INV);
    }
    float y = hf * gr;

    // --- Warmth-when-working: blend a touch of tanh proportional to GR depth.
    //     work=0 (idle) → bypass; work→1 (heavy limiting) → up to WARM_MIX tanh.
    const float work = 1.f - gr;
    if (work > 0.001f) {
      const float warm = tanhf(y * (1.f + work * MODE_C_LIMIT_WARM_DRV));
      const float mix  = work * MODE_C_LIMIT_WARM_MIX;
      y = y * (1.f - mix) + warm * mix;
    }

    return lf + y;
  }

 private:
  float sr_        = 48000.f;
  float lp_alpha_  = 0.f;
  float lp_state_  = 0.f;
  float env_       = 0.f;
  float atk_       = 0.f;
  float rel_       = 0.f;
};
