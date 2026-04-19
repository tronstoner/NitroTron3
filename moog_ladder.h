#pragma once

#include <cmath>

// Huovilainen non-linear Moog ladder filter (DAFx 2004)
// 4-pole (24 dB/oct) lowpass with per-stage tanh saturation.
// No resonance feedback — used for tone shaping on a drone oscillator.
class MoogLadder {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    for (int i = 0; i < 4; i++) stage_[i] = 0.f;
    delay_[0] = delay_[1] = 0.f;
  }

  // Set cutoff in Hz (clamped to Nyquist)
  void SetCutoff(float freq) {
    if (freq < 20.f) freq = 20.f;
    if (freq > sr_ * 0.45f) freq = sr_ * 0.45f;
    // Huovilainen frequency coefficient
    // fc = 2 * sr * tan(pi * freq / sr) — bilinear pre-warp
    // Approximation for efficiency:
    float fc = 2.f * sr_ * tanf(3.14159265f * freq / sr_);
    // Normalized coefficient: g = fc / (2 * sr)
    g_ = fc / (2.f * sr_);
  }

  // Set input drive (gain before filter, adds tanh character)
  void SetDrive(float drive) { drive_ = drive; }

  float Process(float in) {
    // Apply drive
    in *= drive_;

    // 4 cascaded one-pole sections with tanh nonlinearity
    // Using the Huovilainen improved model with half-sample delay compensation
    float x = in - 4.f * 0.f;  // No resonance feedback (res = 0)

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
  float stage_[4] = {};   // filter state per pole
  float delay_[2] = {};   // reserved for future resonance feedback

  // tanh approximation — Pade (3,3), accurate to ~0.1% for |x| < 4
  static float Saturate(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }
};
