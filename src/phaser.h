#pragma once

#include <cmath>
#include <cstdint>
#include "constants.h"

// 3-band parallel resonant BPF phaser. Band 0 sits at K1's base frequency;
// bands 1 and 2 scale together off band 0 by PHASER_BPF_RATIO and its
// square so the formant relationship stays coherent during sweeps. An
// internal LFO modulates the base frequency exponentially around K1's
// position (±PHASER_SWEEP_RATIO octaves).
//
// K3 bipolar: CCW = triangle LFO, CW = sample-and-hold. Magnitude (after
// the caller's deadzone snap-to-zero) sets LFO rate. K3 at noon → LFO
// inactive, filter sits statically at K1 (delivers the "static
// bandpass/highpass" use case from the same control surface).
class Phaser {
 public:
  enum Shape : uint8_t { kTriangle = 0, kSampleHold = 1 };

  void Init(float sample_rate) {
    sr_ = sample_rate;
    inv_sr_ = 1.f / sample_rate;
    for (int i = 0; i < 3; ++i) {
      bp_[i] = 0.f;
      lp_[i] = 0.f;
      g_[i]  = 0.f;
    }
    q_inv_         = 1.f;
    lfo_phase_     = 0.f;
    lfo_rate_hz_   = 0.f;
    sh_value_      = 0.f;
    smoothed_mod_  = 0.f;
    shape_         = kTriangle;
    lfo_active_    = false;
    f1_hz_         = PHASER_F1_HZ_MIN;
    rng_           = 0xCAFEBABEu;
    UpdateBandCoeffs(f1_hz_);
  }

  // Per-block. k1, k2 in [0, 1]. k3_signed in [-1, +1]; sign selects shape
  // (negative = triangle, positive = S&H), magnitude sets LFO rate. The
  // caller should snap k3_signed to exactly 0 inside its deadzone — that
  // disables the LFO and locks bands to f1.
  void SetParams(float k1, float k2, float k3_signed) {
    f1_hz_ = PHASER_F1_HZ_MIN *
             powf(PHASER_F1_HZ_MAX / PHASER_F1_HZ_MIN, k1);

    const float q = PHASER_Q_MIN + k2 * (PHASER_Q_MAX - PHASER_Q_MIN);
    q_inv_ = 1.f / q;

    if (k3_signed == 0.f) {
      lfo_active_ = false;
    } else {
      lfo_active_ = true;
      if (k3_signed < 0.f) {
        shape_ = kTriangle;
        const float mag = -k3_signed;
        lfo_rate_hz_ = PHASER_LFO_HZ_MIN +
                       mag * (PHASER_LFO_HZ_MAX - PHASER_LFO_HZ_MIN);
      } else {
        shape_ = kSampleHold;
        lfo_rate_hz_ = PHASER_LFO_HZ_MIN +
                       k3_signed * (PHASER_LFO_HZ_MAX - PHASER_LFO_HZ_MIN);
      }
    }

    // LFO off → band coefficients fixed for the whole block.
    if (!lfo_active_) UpdateBandCoeffs(f1_hz_);
  }

  float Process(float in) {
    float mod = 0.f;

    if (lfo_active_) {
      lfo_phase_ += lfo_rate_hz_ * inv_sr_;
      if (lfo_phase_ >= 1.f) {
        lfo_phase_ -= 1.f;
        if (shape_ == kSampleHold) sh_value_ = RandBipolar();
      }

      if (shape_ == kTriangle) {
        // Triangle in [-1, +1]: 0 → +1 → 0 → −1 → 0 over phase [0, 1).
        const float p = lfo_phase_;
        if      (p < 0.25f) mod = p * 4.f;
        else if (p < 0.75f) mod = 2.f - p * 4.f;
        else                mod = p * 4.f - 4.f;
      } else {
        mod = sh_value_;
      }
    }

    // One-pole smoothing kills S&H step discontinuities and ramps mod
    // gracefully across the deadzone boundary when K3 engages/disengages.
    smoothed_mod_ += PHASER_MOD_SMOOTH_COEF * (mod - smoothed_mod_);

    if (lfo_active_) {
      const float fmod = f1_hz_ * FastExp2(smoothed_mod_ * PHASER_SWEEP_RATIO);
      UpdateBandCoeffs(fmod);
    }

    // Three parallel Chamberlin SVFs, bandpass tap.
    float sum = 0.f;
    for (int i = 0; i < 3; ++i) {
      const float hp = in - q_inv_ * bp_[i] - lp_[i];
      bp_[i] += g_[i] * hp;
      lp_[i] += g_[i] * bp_[i];
      sum    += bp_[i];
    }
    return sum * PHASER_OUT_GAIN;
  }

 private:
  static constexpr float kPi  = 3.14159265f;
  static constexpr float kLn2 = 0.6931472f;

  float sr_      = 48000.f;
  float inv_sr_  = 1.f / 48000.f;
  float f1_hz_   = PHASER_F1_HZ_MIN;
  float q_inv_   = 1.f;
  float bp_[3], lp_[3], g_[3];

  float lfo_phase_    = 0.f;
  float lfo_rate_hz_  = 0.f;
  float sh_value_     = 0.f;
  float smoothed_mod_ = 0.f;
  Shape shape_        = kTriangle;
  bool  lfo_active_   = false;

  uint32_t rng_       = 0xCAFEBABEu;

  // xorshift32 → [-1, +1). Top 24 bits to a normalized float.
  float RandBipolar() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    const uint32_t v = rng_ & 0xFFFFFFu;
    return v * (2.f / 16777216.f) - 1.f;
  }

  // 2^x via expf (single-precision, ~30 cycles on M7). Domain bounded by
  // PHASER_SWEEP_RATIO ≤ 0.5 so x ∈ [-0.5, +0.5] in practice.
  static float FastExp2(float x) { return expf(x * kLn2); }

  void UpdateBandCoeffs(float f_base) {
    float fc = f_base;
    const float fmax = sr_ * 0.166f; // Chamberlin stable below sr/6
    for (int i = 0; i < 3; ++i) {
      float fcl = fc;
      if (fcl < 20.f) fcl = 20.f;
      if (fcl > fmax) fcl = fmax;
      g_[i] = 2.f * sinf(kPi * fcl * inv_sr_);
      fc *= PHASER_BPF_RATIO;
    }
  }
};
