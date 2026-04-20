#pragma once

#include <cmath>
#include "constants.h"

class MoogOsc {
 public:
  enum Waveform { SAW, TRI, SQUARE };

  float k        = 0.35f;
  float dcTrim   = 0.f;
  float peakGain = 1.0f;
  Waveform waveform = SAW;

  void Init(float sample_rate) {
    sr_ = sample_rate;
    phase_ = 0.f;
  }

  float Process(float freq) {
    float inc = freq / sr_;
    phase_ += inc;
    if (phase_ >= 1.f) phase_ -= 1.f;

    float out = 0.f;

    switch (waveform) {
      case SAW: {
        // Parabolic shaping: x + k*(x - x*x)
        float shaped = phase_ + k * (phase_ - phase_ * phase_);
        // Rescale to -1..1
        shaped = shaped * 2.f - 1.f;
        // DC correction for parabolic term
        shaped -= k * (1.f / 6.f);
        shaped += dcTrim;
        // PolyBLEP at wrap discontinuity
        shaped -= PolyBlep(phase_, inc);
        out = shaped * OSC_SAW_GAIN;
        break;
      }
      case TRI: {
        // Triangle: -1 → +1 → -1 over one cycle
        float tri = 2.f * fabsf(2.f * phase_ - 1.f) - 1.f;
        // Slight parabolic warmth (minimal effect on triangle)
        tri += k * 0.25f * (tri - tri * fabsf(tri));
        out = tri * OSC_TRI_GAIN;
        break;
      }
      case SQUARE: {
        // Naive square
        float sq = phase_ < 0.5f ? 1.f : -1.f;
        // PolyBLEP at both transitions
        sq += PolyBlep(phase_, inc);
        float shifted = phase_ + 0.5f;
        if (shifted >= 1.f) shifted -= 1.f;
        sq -= PolyBlep(shifted, inc);
        out = sq * OSC_SQR_GAIN;
        break;
      }
    }

    return out * peakGain;
  }

 private:
  float sr_    = 48000.f;
  float phase_ = 0.f;

  static float PolyBlep(float t, float dt) {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.f;
    } else if (t > 1.f - dt) {
      t = (t - 1.f) / dt;
      return t * t + t + t + 1.f;
    }
    return 0.f;
  }
};
