#pragma once
//
// flanger.h — simple flanger (header-only): a short delay swept by a triangle
// LFO between min_ms and max_ms, linear feedback (comb character), output =
// 0.5 (dry + delayed) like the phaser's internal mix. Nothing else: no
// through-zero, no saturation, no filtering.
//
#include <math.h>

class Flanger {
 public:
  static constexpr int kLen = 1024;               // 21 ms at 48 kHz (power of two)
  void Init(float sr, float min_ms, float max_ms, float fb) {
    sr_ = sr; fb_ = fb;
    min_ = min_ms * 0.001f * sr; max_ = max_ms * 0.001f * sr;
    if (max_ > (float)(kLen - 2)) max_ = (float)(kLen - 2);
    for (int i = 0; i < kLen; i++) buf_[i] = 0.f;
    w_ = 0; ph_ = 0.f; inc_ = 0.f;
  }
  void SetRate(float hz) { inc_ = hz / sr_; }

  inline float Process(float x) {
    ph_ += inc_; if (ph_ >= 1.f) ph_ -= 1.f;
    const float tri = ph_ < 0.5f ? 4.f * ph_ - 1.f : 3.f - 4.f * ph_;   // -1..+1
    const float d = min_ + (max_ - min_) * (0.5f + 0.5f * tri);        // samples
    float rp = (float)w_ - d; if (rp < 0.f) rp += (float)kLen;
    const int   ri = (int)rp; const float fr = rp - (float)ri;
    const int   i0 = ri & (kLen - 1);                // (rp can round up to exactly kLen)
    const float y = buf_[i0] + (buf_[(i0 + 1) & (kLen - 1)] - buf_[i0]) * fr;
    buf_[w_] = x + fb_ * y;
    w_ = (w_ + 1) & (kLen - 1);
    return 0.5f * (x + y);
  }

 private:
  float buf_[kLen];
  float sr_ = 48000.f, fb_ = 0.f, min_ = 0.f, max_ = 0.f, ph_ = 0.f, inc_ = 0.f;
  int   w_ = 0;
};
