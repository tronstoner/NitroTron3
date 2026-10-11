#pragma once
//
// lfo_shape.h — skewed, curved triangle LFO shape (header-only).
//
//   phase p in [0,1): rise over [0, sym), fall over [sym, 1); each segment is
//   bent like an RC charge / discharge: bend(t) = (1 - e^(-k t)) / (1 - e^(-k)),
//   k = 5 x curve (curve 0 = straight; + = fast start, eases into the turn;
//   - = slow start, sharp turn). At() returns -1..+1 (bottom at p = 0).
//   sym 0.5 + curve 0 = a plain triangle. The bend is a 257-point table built
//   in Set() (no exp per sample).
//
#include <math.h>

class LfoShape {
 public:
  void Set(float sym, float curve) {
    sym_ = sym < 0.02f ? 0.02f : (sym > 0.98f ? 0.98f : sym);
    const float k = 5.f * curve;
    for (int i = 0; i <= kN; i++) {
      const float t = (float)i / (float)kN;
      lut_[i] = (fabsf(k) < 1e-3f) ? t : (1.f - expf(-k * t)) / (1.f - expf(-k));
    }
  }
  inline float At(float p) const {
    const float y = (p < sym_) ? Bend(p / sym_) : 1.f - Bend((p - sym_) / (1.f - sym_));
    return 2.f * y - 1.f;
  }

 private:
  static constexpr int kN = 256;
  inline float Bend(float t) const {
    const float x = t * (float)kN;
    int i = (int)x; if (i >= kN) i = kN - 1; if (i < 0) i = 0;
    const float f = x - (float)i;
    return lut_[i] + (lut_[i + 1] - lut_[i]) * f;
  }
  float sym_ = 0.5f;
  float lut_[kN + 1] = {};
};
