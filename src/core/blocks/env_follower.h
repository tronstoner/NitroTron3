#pragma once

#include <cmath>

// Moog MoogerFooger envelope follower topology:
// Full wave rectifier → 4-pole (24 dB/oct) lowpass.
//
// The 4-pole LP is implemented as 4 cascaded one-pole filters.
// Cutoff default is 33 Hz (Moog canonical). Attack/release behavior
// is inherent to the filter time constant — no separate AR controls.
class EnvFollower {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    for (int i = 0; i < 4; i++) stage_[i] = 0.f;
    SetCutoff(33.f);
  }

  // Set LP cutoff in Hz (controls tracking speed)
  void SetCutoff(float hz) {
    // One-pole coefficient: g = 1 - exp(-2π * fc / sr)
    coeff_ = 1.f - expf(-6.2831853f * hz / sr_);
  }

  // Set input gain (sensitivity) before rectifier
  void SetPreGain(float g) { pre_gain_ = g; }

  // Process one sample. Input is the raw bass signal.
  // Returns envelope value (0.0 – ~1.0+, depending on input level).
  float Process(float in) {
    // Pre-gain
    in *= pre_gain_;

    // Full wave rectifier
    float rect = fabsf(in);

    // 4-pole lowpass (4 cascaded one-pole filters)
    stage_[0] += coeff_ * (rect - stage_[0]);
    stage_[1] += coeff_ * (stage_[0] - stage_[1]);
    stage_[2] += coeff_ * (stage_[1] - stage_[2]);
    stage_[3] += coeff_ * (stage_[2] - stage_[3]);

    return stage_[3];
  }

 private:
  float sr_      = 48000.f;
  float coeff_   = 0.f;
  float pre_gain_ = 1.f;
  float stage_[4] = {};
};
