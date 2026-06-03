#pragma once

#include <cmath>
#include "constants.h"
#include "moog_osc.h"

// Mode C SW1=DOWN — pitch-tracked synth oscillator engine.
//
// K4 splits the knob at noon — saw on the left, rect on the right. Within
// each half, modulation ramps in fast across the first portion of travel
// (so the modulated timbre is "fully on" early), then the remaining travel
// only widens the modulation:
//
//   K4 = 0.00            : max hypersaw — 7 voices, full detune
//   K4 ∈ [0.00, ~0.28]   : all voices at full gain, detune widens to 50 cents
//   K4 ∈ [~0.28, 0.46]   : side voices fade in fast, detune ~10 cents
//   K4 ∈ [0.46, 0.50]    : pure single saw sweet-spot plateau
//   K4 = 0.50            : discrete swap to rect
//   K4 ∈ [0.50, ~0.70]   : PWM depth ramps in fast (LFO slow at 0.2 Hz)
//   K4 ∈ [~0.70, 1.00]   : depth at max, LFO rate speeds up to 2 Hz
//   K4 = 1.00            : max PWM (sweet-spot depth, fastest rate)
//
// Pitch + raw env-follower VCA happen at the call site (Mode A style).
class ModeCSynth {
 public:
  void Init(float sr) {
    sr_ = sr;
    const int N = MODE_C_SYNTH_UNISON_VOICES;
    for (int v = 0; v < N; v++) {
      saws_[v].Init(sr);
      saws_[v].waveform = MoogOsc::SAW;
      // Irrational stagger (√2 − 1 fractional) so voices boot fully decorrelated.
      // Avoids the slow flange-like beating that comes from voices starting in
      // phase and only drifting apart over tens of seconds at small detune.
      const float seed = (static_cast<float>(v) + 1.f) * 0.41421356f;
      saws_[v].SetPhase(seed - floorf(seed));
    }
    lfo_phase_ = 0.f;
  }

  // f0 in Hz, k4 in [0,1].
  float Process(float f0, float k4) {
    float hyper_amt     = 0.f;
    float saw_amp       = 0.f;
    float rect_amp      = 0.f;
    float pwm_depth_amt = 0.f;
    float pwm_rate_amt  = 0.f;
    // Detune defaults to MAX in the rect half (voices silent there; the value
    // only matters as a phase-advance rate so they stay decorrelated).
    float detune        = MODE_C_SYNTH_DETUNE_CENTS_MAX;

    if (k4 < 0.5f) {
      // Saw half. K4 ∈ [0.5 − SAW_PLATEAU, 0.5] holds pure single saw (sweet
      // spot so the user can land on a clean unison-free saw). Below that,
      // hypersaw modulation ramps in: side-voice gain reaches max across the
      // first HYPER_GAIN_FRAC of travel past the plateau; remaining CCW
      // travel only widens detune.
      saw_amp = 1.f;
      const float plateau_lo = 0.5f - MODE_C_SYNTH_SAW_PLATEAU;
      if (k4 < plateau_lo) {
        const float t = (plateau_lo - k4) / plateau_lo;  // 0 at plateau edge, 1 at K4=0
        hyper_amt = t / MODE_C_SYNTH_HYPER_GAIN_FRAC;
        if (hyper_amt > 1.f) hyper_amt = 1.f;
        detune    = MODE_C_SYNTH_DETUNE_CENTS_MIN + t *
                    (MODE_C_SYNTH_DETUNE_CENTS_MAX - MODE_C_SYNTH_DETUNE_CENTS_MIN);
      }
    } else {
      // Rect half. K4=0.5 = pure single rect, K4=1.0 = max PWM.
      // Depth ramps in fast across the first PWM_DEPTH_FRAC of travel from
      // noon; remaining CW travel only speeds the LFO up.
      rect_amp = 1.f;
      const float t = (k4 - 0.5f) / 0.5f;             // 0 at noon, 1 at K4=1
      pwm_depth_amt = t / MODE_C_SYNTH_PWM_DEPTH_FRAC;
      if (pwm_depth_amt > 1.f) pwm_depth_amt = 1.f;
      pwm_rate_amt = (t - MODE_C_SYNTH_PWM_DEPTH_FRAC) /
                     (1.f - MODE_C_SYNTH_PWM_DEPTH_FRAC);
      if (pwm_rate_amt < 0.f) pwm_rate_amt = 0.f;
    }

    // ----- Saw section (always advance all voices to preserve decorrelation) -----
    // Gain and detune are split (see K4 branch above): gain ramps in fast so
    // the supersaw is "fully on" early in the CCW travel; further CCW only
    // widens detune from MIN to MAX. Detune starts at MIN (already incoherent)
    // at noon so the 1/√(1+N·amt²) RMS normalization is accurate from the
    // first sample where side voices contribute — no loudness step at noon.
    const int N      = MODE_C_SYNTH_UNISON_VOICES;
    const int center = N / 2;
    float saw_sum = 0.f;
    for (int v = 0; v < N; v++) {
      const float spread     = static_cast<float>(v - center) / static_cast<float>(center);
      const float voice_freq = f0 * powf(2.f, spread * detune / 1200.f);
      const float s          = saws_[v].Process(voice_freq);
      const float w          = (v == center) ? 1.f : hyper_amt;
      saw_sum += s * w;
    }
    saw_sum *= 1.f / sqrtf(1.f + hyper_amt * hyper_amt * static_cast<float>(N - 1));

    // ----- Rect section, phase-locked to the center saw, polarity inverted -----
    float rect_out = 0.f;
    if (rect_amp > 0.f) {
      const float lfo_hz = MODE_C_SYNTH_PWM_LFO_HZ_MIN +
          pwm_rate_amt * (MODE_C_SYNTH_PWM_LFO_HZ_MAX - MODE_C_SYNTH_PWM_LFO_HZ_MIN);
      lfo_phase_ += 6.2831853f * lfo_hz / sr_;
      if (lfo_phase_ >= 6.2831853f) lfo_phase_ -= 6.2831853f;
      float duty = 0.5f + pwm_depth_amt * MODE_C_SYNTH_PWM_DEPTH_MAX * sinf(lfo_phase_);
      if (duty < 0.05f) duty = 0.05f;
      if (duty > 0.95f) duty = 0.95f;
      const float phase = saws_[center].GetPhase();
      const float inc   = f0 / sr_;
      rect_out = -PulsePwm(phase, inc, duty);
    }

    return saw_sum * saw_amp + rect_out * rect_amp;
  }

 private:
  float sr_ = 48000.f;
  MoogOsc saws_[MODE_C_SYNTH_UNISON_VOICES];
  float lfo_phase_ = 0.f;

  static float PulsePwm(float phase, float inc, float duty) {
    float p = phase < duty ? 1.f : -1.f;
    p += PolyBlep(phase, inc);                          // rising edge at phase=0
    float shifted = phase + (1.f - duty);               // falling edge at phase=duty
    if (shifted >= 1.f) shifted -= 1.f;
    p -= PolyBlep(shifted, inc);
    p -= (2.f * duty - 1.f);                            // DC compensation for off-center duty
    return p;
  }

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
