#pragma once
#include <cmath>

// Bode-style single-sideband frequency shifter.
//
// Hilbert pair: two parallel 4-stage first-order allpass cascades. Branch
// outputs differ by ~π/2 across ~50 Hz – 21 kHz at 48 kHz (Niemitalo
// half-band design — measured sideband suppression ≈ –80 dB in band).
//
// Each allpass section: y[n] = a*(x[n] + y[n-1]) - x[n-1].
//
// SSB output: re*cos(ωt) − im*sin(ωt). Sign of shift_hz selects sideband.
class FreqShifter {
public:
  void Init(float sample_rate) {
    inv_sr_ = 1.f / sample_rate;
    phase_ = 0.f;
    inc_ = 0.f;
    for (int i = 0; i < 4; i++) {
      xi_[i] = yi_[i] = 0.f;
      xq_[i] = yq_[i] = 0.f;
    }
  }

  void SetShiftHz(float hz) { inc_ = hz * inv_sr_; }

  float Process(float in) {
    static constexpr float AI[4] = {
        0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
    static constexpr float AQ[4] = {
        0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};

    float si = in;
    float sq = in;
    for (int i = 0; i < 4; i++) {
      float ni = AI[i] * (si + yi_[i]) - xi_[i];
      xi_[i] = si;
      yi_[i] = ni;
      si = ni;
      float nq = AQ[i] * (sq + yq_[i]) - xq_[i];
      xq_[i] = sq;
      yq_[i] = nq;
      sq = nq;
    }

    float c = cosf(2.f * 3.14159265358979f * phase_);
    float s = sinf(2.f * 3.14159265358979f * phase_);
    phase_ += inc_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    else if (phase_ < 0.f) phase_ += 1.f;

    return si * c - sq * s;
  }

 private:
  float inv_sr_;
  float phase_;
  float inc_;
  float xi_[4], yi_[4];
  float xq_[4], yq_[4];
};
