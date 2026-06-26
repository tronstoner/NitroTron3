#pragma once

#include <cmath>
#include <cstdint>
#include "constants.h"

// First-order allpass phaser, voiced toward the EHX Small Stone but denser.
// All stages share the same modulated allpass corner ω. The output mixes
// dry + allpass-chain at unity 0.5/0.5 — this internal mix is intrinsic to
// phaser character (cannot be moved to K6). The wet (allpass-filtered)
// signal interfering with the dry creates notches sweeping in tandem: N
// identical-ω stages produce N/2 notches (a stock Small Stone is 4 stages /
// 2 notches; we run 6 / 3 notches for a thicker, denser sweep).
//
// K2 = feedback (Color analog): tap from end of allpass chain back into
// stage 0's input. Off (K2=0) = clean dry-flat-with-notches sweep; up =
// deeper, narrower notches with a hint of resonance, classic Small Stone
// "Color ON" feel. Clamped below self-oscillation by PHASER_FB_MAX.
//
// K3 bipolar: CCW = triangle LFO, CW = sample-and-hold. Sign selects
// shape, magnitude (after the caller's deadzone snap-to-zero) sets LFO
// rate. Rate maps exponentially from PHASER_LFO_HZ_MIN (true ambient,
// sub-Hz) up to PHASER_LFO_HZ_MAX (near sub-audio, where triangle
// modulation starts generating sidebands).
class Phaser {
 public:
  enum Shape : uint8_t { kTriangle = 0, kSampleHold = 1 };

  void Init(float sample_rate) {
    sr_     = sample_rate;
    inv_sr_ = 1.f / sample_rate;
    for (int i = 0; i < kStages; ++i) ap_state_[i] = 0.f;
    // Per-stage coefficient detune: symmetric spread in coefficient space
    // around the shared corner. Cheap (additive offset, no extra tanf/sample)
    // and breaks the perfect-alignment "digital" notch stack — stages land at
    // slightly different corners so notches/resonance spread organically.
    const float center = (kStages - 1) * 0.5f;
    for (int i = 0; i < kStages; ++i)
      stage_offset_[i] = PHASER_STAGE_SPREAD * ((i - center) / center);
    fb_state_     = 0.f;
    a_            = 0.f;
    fb_amt_       = 0.f;
    fc_center_    = PHASER_F1_HZ_MIN;
    lfo_phase_    = 0.f;
    lfo_rate_hz_  = 0.f;
    sh_value_     = 0.f;
    shape_        = kTriangle;
    lfo_active_   = false;
    rng_          = 0xCAFEBABEu;
    UpdateAllpassCoeff(fc_center_);
  }

  // k1, k2 in [0, 1]. k3_signed in [-1, +1]; sign = shape (negative
  // triangle, positive S&H), magnitude past the caller's deadzone = LFO
  // rate. The caller should snap k3_signed to exactly 0 inside its
  // deadzone — that disables the LFO and locks ω to f1.
  void SetParams(float k1, float k2, float k3_signed) {
    fc_center_ = PHASER_F1_HZ_MIN *
                 powf(PHASER_F1_HZ_MAX / PHASER_F1_HZ_MIN, k1);

    fb_amt_ = k2 * PHASER_FB_MAX;

    if (k3_signed == 0.f) {
      lfo_active_ = false;
    } else {
      lfo_active_ = true;
      shape_ = (k3_signed < 0.f) ? kTriangle : kSampleHold;
      const float mag = (k3_signed < 0.f) ? -k3_signed : k3_signed;
      // Exponential rate mapping over a per-shape range so K3 magnitude
      // moves perceptually evenly. Triangle reaches near sub-audio at full
      // travel; S&H tops out well below audio-rate.
      const float rate_min = (shape_ == kTriangle)
          ? PHASER_LFO_TRI_HZ_MIN : PHASER_LFO_SH_HZ_MIN;
      const float rate_max = (shape_ == kTriangle)
          ? PHASER_LFO_TRI_HZ_MAX : PHASER_LFO_SH_HZ_MAX;
      lfo_rate_hz_ = rate_min * powf(rate_max / rate_min, mag);
    }

    if (!lfo_active_) UpdateAllpassCoeff(fc_center_);
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
        const float p = lfo_phase_;
        if      (p < 0.25f) mod = p * 4.f;
        else if (p < 0.75f) mod = 2.f - p * 4.f;
        else                mod = p * 4.f - 4.f;
      } else {
        mod = sh_value_;
      }
    }

    if (lfo_active_) {
      // Apply mod directly: S&H stays as true steps, no slew between
      // values. Triangle is already smooth so no smoothing needed.
      const float fc = fc_center_ * FastExp2(mod * PHASER_SWEEP_OCT);
      UpdateAllpassCoeff(fc);
    }

    // Allpass chain, optional feedback from chain output. Per-stage 1st-order
    // allpass in transposed direct form II, each stage detuned in coefficient
    // space (stage_offset_) off the shared corner:
    //   y[n] = -a * x[n] + s[n-1]
    //   s[n] =  a * y[n] + x[n]
    // Feedback path is soft-saturated (tanh) like an analog OTA loop: at low
    // resonance it's ~linear, but as the loop rings up it blooms and self-
    // limits instead of ringing as a pure (sterile, "digital") sine.
    float x = in - fb_amt_ * tanhf(fb_state_);
    for (int i = 0; i < kStages; ++i) {
      float ai = a_ + stage_offset_[i];
      if      (ai >  0.999f) ai =  0.999f;
      else if (ai < -0.999f) ai = -0.999f;
      const float y = -ai * x + ap_state_[i];
      ap_state_[i] = ai * y + x;
      x = y;
    }
    fb_state_ = x;

    // Internal dry + wet sum at unity 0.5/0.5. THIS is what creates the
    // two moving notches; bypassing or relocating it loses phaser character.
    return 0.5f * (in + x);
  }

 private:
  static constexpr int   kStages = 6;
  static constexpr float kPi     = 3.14159265f;
  static constexpr float kLn2    = 0.6931472f;

  float sr_     = 48000.f;
  float inv_sr_ = 1.f / 48000.f;
  float fc_center_;
  float a_;
  float fb_amt_;
  float fb_state_;
  float ap_state_[kStages];
  float stage_offset_[kStages];

  float lfo_phase_;
  float lfo_rate_hz_;
  float sh_value_;
  Shape shape_;
  bool  lfo_active_;

  uint32_t rng_;

  float RandBipolar() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    const uint32_t v = rng_ & 0xFFFFFFu;
    return v * (2.f / 16777216.f) - 1.f;
  }

  static float FastExp2(float x) { return expf(x * kLn2); }

  // Bilinear-transform first-order allpass coefficient:
  //   H(z) = (-a + z⁻¹) / (1 - a · z⁻¹),   a = (1 − tan(π·fc/sr)) / (1 + tan(π·fc/sr))
  // Phase passes through −90° at f = fc. Stage stack of 4 → notches at
  // fc · tan(22.5°) and fc · tan(67.5°).
  void UpdateAllpassCoeff(float fc) {
    if (fc < 20.f)         fc = 20.f;
    if (fc > sr_ * 0.45f)  fc = sr_ * 0.45f;
    const float t = tanf(kPi * fc * inv_sr_);
    a_ = (1.f - t) / (1.f + t);
  }
};
