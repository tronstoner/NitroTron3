#pragma once

#include <cmath>

// Saturating state-variable filter (Andrew Simper / Cytomic trapezoidal form).
// Per-integrator tanh on the state updates — saturation lives *inside* the
// resonance feedback loop, which is what produces wavefolder-like harmonic
// generation at high resonance. Used as the core of the Plague filter.
class SvfNonlinear {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    ic1_ = 0.f;
    ic2_ = 0.f;
    SetCutoff(440.f);
    SetDamping(1.f);
  }

  void SetCutoff(float hz) {
    if (hz < 20.f) hz = 20.f;
    if (hz > sr_ * 0.45f) hz = sr_ * 0.45f;
    g_ = tanf(3.14159265f * hz / sr_);
    Recompute();
  }

  // Damping k = 1/Q. Lower = more resonance.
  // The nonlinear tanh prevents true self-oscillation even at k=0,
  // but very low values still produce extreme ringing/folding.
  void SetDamping(float k) {
    if (k < 0.f) k = 0.f;
    k_ = k;
    Recompute();
  }

  // Bandpass tap — the "ringing" output Plague uses on both bands.
  float ProcessBp(float in) {
    const float v3 = in - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    // Per-integrator tanh on the state updates.
    ic1_ = 2.f * Saturate(v1) - ic1_;
    ic2_ = 2.f * Saturate(v2) - ic2_;
    return v1;
  }

 private:
  float sr_ = 48000.f;
  float g_ = 0.f, k_ = 1.f;
  float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f;
  float ic1_ = 0.f, ic2_ = 0.f;

  void Recompute() {
    a1_ = 1.f / (1.f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
  }

  // tanh approximation — Pade (3,3), matches moog_ladder.h.
  static float Saturate(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }
};
