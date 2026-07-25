#pragma once

#include <cmath>

// Huovilainen non-linear Moog ladder filter (DAFx 2004)
// 4-pole (24 dB/oct) lowpass with per-stage tanh saturation.
// Resonance is opt-in (res_ defaults to 0).
class MoogLadder {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    for (int i = 0; i < 4; i++) stage_[i] = 0.f;
    in_prev_ = 0.f;
  }

  // Set cutoff in Hz (clamped to Nyquist)
  void SetCutoff(float freq) {
    if (freq < 20.f) freq = 20.f;
    if (freq > sr_ * 0.45f) freq = sr_ * 0.45f;
    // Huovilainen frequency coefficient with bilinear pre-warp
    float fc = 2.f * sr_ * tanf(3.14159265f * freq / sr_);
    g_ = fc / (2.f * sr_);
    // Clamp g below 1.0 for stability — prevents runaway at high cutoffs
    if (g_ > 0.95f) g_ = 0.95f;
  }

  // Set input drive (gain before filter, adds tanh character)
  void SetDrive(float drive) { drive_ = drive; }

  // Set resonance (0 = off, ~1.0 = self-oscillation threshold).
  // Caller should clamp below the self-osc limit (~0.95 is a safe ceiling).
  void SetResonance(float res) {
    if (res < 0.f) res = 0.f;
    res_ = res;
  }

  float Process(float in) {
    // Apply drive
    in *= drive_;

    // Resonance feedback with half-sample delay compensation (Huovilainen).
    // (stage_[3] - 0.5*in_prev) approximates the output midway between samples.
    float x = in - 4.f * res_ * (stage_[3] - 0.5f * in_prev_);
    in_prev_ = in;

    // Stage 1
    stage_[0] += g_ * (Saturate(x) - Saturate(stage_[0]));
    // Stage 2
    stage_[1] += g_ * (Saturate(stage_[0]) - Saturate(stage_[1]));
    // Stage 3
    stage_[2] += g_ * (Saturate(stage_[1]) - Saturate(stage_[2]));
    // Stage 4
    stage_[3] += g_ * (Saturate(stage_[2]) - Saturate(stage_[3]));

    return stage_[3];
  }

 private:
  float sr_    = 48000.f;
  float g_     = 0.f;     // filter coefficient
  float drive_ = 1.f;
  float res_   = 0.f;     // resonance feedback amount
  float stage_[4] = {};   // filter state per pole
  float in_prev_  = 0.f;  // previous input for half-sample delay compensation

  // tanh approximation — Pade (3,3), accurate to ~0.1% for |x| < 4
  static float Saturate(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }
};
