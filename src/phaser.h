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
// "Color ON" feel. Feedback is coupled to the K2 character morph
// (PHASER_FB_AT_NOTCH … PHASER_FB_AT_PEAK), no longer on its own knob.
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
    for (int i = 0; i < kStages; ++i) {
      const float frac = (i - center) / center;   // −1 … +1 across stages
      stage_offset_[i] = PHASER_STAGE_SPREAD * frac;
      stage_frac_[i]   = frac;
      a_stage_[i]      = 0.f;
    }
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

    // K2 two-phase character travel, per side from noon:
    //   phase 1 (0 → K2_FULL_AT of the half-travel): morph to full notch
    //   (CCW, g→+1) or full peak (CW, g→−1);
    //   phase 2 (the rest): morph stays saturated, the LFO sweep range
    //   widens instead (sweep_mult_ 1 → SWEEP_MAX_MULT).
    // Feedback keeps its original full-travel mapping (resonance feel
    // unchanged): negative/wide at the notch end → resonant at the peak end.
    {
      const float side = (k2 < 0.5f) ? 1.f : -1.f;              // notch : peak
      const float t    = (k2 < 0.5f) ? (0.5f - k2) * 2.f
                                     : (k2 - 0.5f) * 2.f;       // 0..1 half-travel
      const float m    = (t >= PHASER_K2_FULL_AT)
                             ? 1.f : t / PHASER_K2_FULL_AT;
      wet_g_ = side * m;
      const float over = (t > PHASER_K2_FULL_AT)
          ? (t - PHASER_K2_FULL_AT) / (1.f - PHASER_K2_FULL_AT)
          : 0.f;
      sweep_mult_ = 1.f + over * (PHASER_SWEEP_MAX_MULT - 1.f);
    }
    fb_amt_ = PHASER_FB_AT_NOTCH + k2 * (PHASER_FB_AT_PEAK - PHASER_FB_AT_NOTCH);
    // Toward the peak end the static stage detune widens (up to
    // PHASER_SPREAD_PEAK_MULT×): the loop resonance lands on mismatched
    // corners and splits into several softer peaks instead of one sharp Q.
    spread_mult_ = 1.f + k2 * (PHASER_SPREAD_PEAK_MULT - 1.f);

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

  // Note attack (env onset, detected by the caller): restart the S&H random
  // LFO — fresh random value, phase reset — so each note owns its step and a
  // free-running switch can't fire rhythmically misaligned mid-note. No-op
  // for the triangle shape and when the LFO is off.
  void NoteAttack() {
    if (!lfo_active_ || shape_ != kSampleHold) return;
    lfo_phase_ = 0.f;
    sh_value_  = RandBipolar();
  }

  float Process(float in) {
    // Per-stage LFO phase offsets (triangle only): each stage sweeps the
    // shared LFO at its own phase, spread over PHASER_STAGE_LFO_SPAN cycles —
    // notches/peaks breathe against each other instead of moving in lockstep
    // (the Uni-Vibe/Schulte LDR-mismatch swirl). S&H keeps one shared value:
    // its character is the step itself, and per-stage step timing would smear
    // it. No slew anywhere — steps stay true steps.
    const bool per_stage = lfo_active_ && shape_ == kTriangle &&
                           PHASER_STAGE_LFO_SPAN > 0.f;

    if (lfo_active_) {
      lfo_phase_ += lfo_rate_hz_ * inv_sr_;
      if (lfo_phase_ >= 1.f) {
        lfo_phase_ -= 1.f;
        if (shape_ == kSampleHold) sh_value_ = RandBipolar();
      }

      const float sweep_oct = PHASER_SWEEP_OCT * sweep_mult_;
      if (per_stage) {
        for (int i = 0; i < kStages; ++i) {
          float p = lfo_phase_ + 0.5f * PHASER_STAGE_LFO_SPAN * stage_frac_[i];
          p -= floorf(p);
          const float m = TriAt(p);
          a_stage_[i] = CoeffFor(fc_center_ * FastExp2(m * sweep_oct));
        }
      } else {
        const float mod = (shape_ == kTriangle) ? TriAt(lfo_phase_) : sh_value_;
        // Apply mod directly: S&H stays as true steps, no slew between
        // values. Triangle is already smooth so no smoothing needed.
        UpdateAllpassCoeff(fc_center_ * FastExp2(mod * sweep_oct));
      }
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
      const float base = per_stage ? a_stage_[i] : a_;
      float ai = base + stage_offset_[i] * spread_mult_;
      if      (ai >  0.999f) ai =  0.999f;
      else if (ai < -0.999f) ai = -0.999f;
      const float y = -ai * x + ap_state_[i];
      ap_state_[i] = ai * y + x;
      x = y;
    }
    fb_state_ = x;

    // Internal dry + wet mixing node — THIS creates the response. Wet-leg
    // gain wet_g_ morphs it: +1 = notches (classic phaser), 0 = flat,
    // −1 = peaks at the same frequencies (bandpass-stack character).
    return 0.5f * (in + wet_g_ * x);
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
  float wet_g_ = 1.f;        // mixing-node wet gain: +1 notch … −1 peak (K2 morph)
  float spread_mult_ = 1.f;  // stage-detune scale, 1 (notch end) … PEAK_MULT (peak end)
  float sweep_mult_  = 1.f;  // LFO sweep-range scale, 1 … SWEEP_MAX_MULT (phase-2 travel)
  float stage_frac_[kStages] = {};  // −1 … +1 stage position, for LFO phase offsets
  float a_stage_[kStages]    = {};  // per-stage coefficient when LFO offsets active

  static float TriAt(float p) {
    if (p < 0.25f) return p * 4.f;
    if (p < 0.75f) return 2.f - p * 4.f;
    return p * 4.f - 4.f;
  }

  float CoeffFor(float fc) const {
    if (fc < 20.f)        fc = 20.f;
    if (fc > sr_ * 0.45f) fc = sr_ * 0.45f;
    const float t = tanf(kPi * fc * inv_sr_);
    return (1.f - t) / (1.f + t);
  }
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
  void UpdateAllpassCoeff(float fc) { a_ = CoeffFor(fc); }
};
