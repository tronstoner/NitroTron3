#pragma once

#include <cmath>
#include "constants.h"

// Grendel formant filter — 4 parallel RBJ bandpass biquads at vowel formants.
// K1 sweeps a 1D path through a curated vowel set (oo → oh → ah → eh → ee
// by default, CCW dark/closed → CW bright/open); K2 scales all formant
// centers ("mouth size"); K3 modulates the path position via env follower.
//
// Coefficients are recomputed per block (not per sample): the env follower's
// 33 Hz LP smooths much more slowly than block rate, so per-sample cosf/sinf
// across 4 biquads would be wasted CPU.
class Grendel {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    for (int f = 0; f < GRENDEL_NUM_FORMANTS; f++) {
      x1_[f] = x2_[f] = y1_[f] = y2_[f] = 0.f;
      b0_[f] = b1_[f] = b2_[f] = 0.f;
      a1_[f] = a2_[f] = 0.f;
    }
  }

  // Per-block. vowel_path may sit outside [0, 1]: values past 1.0 extrapolate
  // along the eh→ee trajectory (brighter, more closed-front); values below 0
  // extrapolate along the oh→oo trajectory (darker, more closed-back).
  // SetBpf's freq clamps catch runaway formants at the extremes. size_scale
  // multiplies formant centers.
  void SetVowel(float vowel_path, float size_scale) {
    const float idx_f  = vowel_path * static_cast<float>(GRENDEL_NUM_VOWELS - 1);
    int idx_lo = static_cast<int>(floorf(idx_f));
    if (idx_lo > GRENDEL_NUM_VOWELS - 2) idx_lo = GRENDEL_NUM_VOWELS - 2;
    if (idx_lo < 0) idx_lo = 0;
    const int   idx_hi = idx_lo + 1;
    const float t      = idx_f - static_cast<float>(idx_lo);

    for (int f = 0; f < GRENDEL_NUM_FORMANTS; f++) {
      const float freq_lo = GRENDEL_VOWELS[idx_lo][f];
      const float freq_hi = GRENDEL_VOWELS[idx_hi][f];
      const float freq    = (freq_lo + t * (freq_hi - freq_lo)) * size_scale;
      SetBpf(f, freq, GRENDEL_FORMANT_Q);
    }
  }

  // Per-sample. Returns the sum of the 4 bandpass taps with per-formant gain.
  float Process(float in) {
    float sum = 0.f;
    for (int f = 0; f < GRENDEL_NUM_FORMANTS; f++) {
      const float out = b0_[f] * in + b1_[f] * x1_[f] + b2_[f] * x2_[f]
                                    - a1_[f] * y1_[f] - a2_[f] * y2_[f];
      x2_[f] = x1_[f];
      x1_[f] = in;
      y2_[f] = y1_[f];
      y1_[f] = out;
      sum += out * GRENDEL_FORMANT_GAIN[f];
    }
    return sum * GRENDEL_OUT_GAIN;
  }

 private:
  float sr_ = 48000.f;
  float b0_[GRENDEL_NUM_FORMANTS] = {};
  float b1_[GRENDEL_NUM_FORMANTS] = {};
  float b2_[GRENDEL_NUM_FORMANTS] = {};
  float a1_[GRENDEL_NUM_FORMANTS] = {};
  float a2_[GRENDEL_NUM_FORMANTS] = {};
  float x1_[GRENDEL_NUM_FORMANTS] = {};
  float x2_[GRENDEL_NUM_FORMANTS] = {};
  float y1_[GRENDEL_NUM_FORMANTS] = {};
  float y2_[GRENDEL_NUM_FORMANTS] = {};

  // RBJ cookbook BPF (constant 0 dB peak gain form).
  void SetBpf(int f, float freq, float Q) {
    if (freq < 20.f) freq = 20.f;
    if (freq > sr_ * 0.45f) freq = sr_ * 0.45f;
    const float w0     = 6.2831853f * freq / sr_;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float alpha  = sin_w0 / (2.f * Q);
    const float a0_inv = 1.f / (1.f + alpha);
    b0_[f] =  alpha * a0_inv;
    b1_[f] =  0.f;
    b2_[f] = -alpha * a0_inv;
    a1_[f] = -2.f * cos_w0 * a0_inv;
    a2_[f] = (1.f - alpha) * a0_inv;
  }
};
