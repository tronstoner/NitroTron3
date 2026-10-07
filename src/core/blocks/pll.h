#pragma once
//
// pll.h — minimal CD4046-style phase-locked loop (header-only).
//
//   input -> hard limiter (sign) -+
//                                 XOR (type-1 phase comparator)
//   VCO square (1:1, no divider) -+    -> one-pole RC loop filter -> VCO control
//
// The loop-filter output v (0..1, the averaged XOR duty) drives the VCO
// exponentially through its free-running centre: v = 0.5 -> centre, v = 0 ->
// fmin, v = 1 -> fmax (each half its own octave span). The VCO is a naive
// (non-band-limited) square. Output is the VCO square, bipolar +-1.
//
// Nothing else: no hysteresis on the comparator, no lock detector, no
// frequency clamp beyond the VCO range itself.
//
#include <math.h>

class Pll4046 {
 public:
  void Init(float sr, float fmin, float fcentre, float fmax) {
    sr_ = sr;
    fmin_ = fmin; fc_ = fcentre; fmax_ = fmax;
    up_oct_ = logf(fmax / fcentre);    // natural-log span above the centre
    dn_oct_ = logf(fcentre / fmin);    // ... and below it
    Reset();
    SetLoopFilterHz(20.f);
  }
  void Reset() { phase_ = 0.f; v_ = 0.5f; freq_ = fc_; }
  // One-pole RC loop-filter cutoff (Hz).
  void SetLoopFilterHz(float hz) { a_ = 1.f - expf(-2.f * 3.14159265f * hz / sr_); }

  // One sample. Returns the VCO square (+-1).
  inline float Process(float in) {
    const bool in_hi  = in >= 0.f;               // comparator: sign of the input
    const bool vco_hi = phase_ < 0.5f;           // VCO square, divider 1:1
    const float xr = (in_hi != vco_hi) ? 1.f : 0.f;   // XOR phase comparator
    v_ += (xr - v_) * a_;                        // RC loop filter
    const float c = 2.f * v_ - 1.f;              // -1 .. +1 around the centre
    freq_ = fc_ * expf(c * (c >= 0.f ? up_oct_ : dn_oct_));
    phase_ += freq_ / sr_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    return vco_hi ? 1.f : -1.f;
  }

  float VcoHz() const { return freq_; }          // current VCO frequency
  float Control() const { return v_; }           // loop-filter output 0..1

 private:
  float sr_ = 48000.f, fmin_ = 30.f, fc_ = 200.f, fmax_ = 4000.f;
  float up_oct_ = 0.f, dn_oct_ = 0.f;
  float phase_ = 0.f, v_ = 0.5f, a_ = 0.f, freq_ = 200.f;
};
