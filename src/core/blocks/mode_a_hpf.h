#pragma once

#include <cmath>
#include "constants.h"

// Mode A K4-CW high-pass: two cascaded one-pole HPs (12 dB/oct). The cutoff is
// smoothed per block (MODE_A_HPF_SMOOTH) so sweeping K4 across noon is
// click-free. At MODE_A_HPF_MIN_HZ it sits below the bass range → transparent,
// so it can stay in series for every waveform including triangle.
class ModeAHpf {
 public:
  void Init(float sr) {
    sr_      = sr;
    lp1_     = lp2_ = 0.f;
    sm_cut_  = MODE_A_HPF_MIN_HZ;
    coeff_   = 1.f - expf(-6.2831853f * sm_cut_ / sr_);
  }

  // Call once per block with the target cutoff (Hz).
  void SetCutoff(float target_hz) {
    sm_cut_ += MODE_A_HPF_SMOOTH * (target_hz - sm_cut_);
    coeff_   = 1.f - expf(-6.2831853f * sm_cut_ / sr_);
  }

  float Process(float x) {
    lp1_ += coeff_ * (x - lp1_);
    const float h1 = x - lp1_;
    lp2_ += coeff_ * (h1 - lp2_);
    return h1 - lp2_;
  }

 private:
  float sr_     = 48000.f;
  float lp1_    = 0.f;
  float lp2_    = 0.f;
  float sm_cut_ = MODE_A_HPF_MIN_HZ;
  float coeff_  = 0.f;
};
