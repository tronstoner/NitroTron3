#pragma once

#include <cmath>
#include "constants.h"
#include "moog_osc.h"

// Mode C SW1=DOWN — pitch-tracked synth oscillator engine.
//
// K4 splits the knob at noon — saw on the left, rect on the right.
//
// Saw half: voices fade in by pairs along the CCW travel (innermost pair
// first, then middle, then outermost), giving a staged "single → dual
// detuned → 5-voice → full 7-voice" progression. Once all pairs are
// active, the remaining CCW travel only widens detune.
//
//   K4 = 0.00            : max hypersaw — 7 voices, full detune
//   K4 ∈ [0.00, ~0.07]   : all voices at full gain, detune widens toward max
//   K4 ∈ [~0.07, ~0.18]  : outermost pair (v=0,6) fades in
//   K4 ∈ [~0.18, ~0.32]  : middle pair (v=1,5) fades in
//   K4 ∈ [~0.32, 0.46]   : innermost pair (v=2,4) fades in — "dual detuned" with center
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
    // Per-pair side-voice gains, indexed by distance-from-center−1:
    //   pair_amt[0] = innermost pair (v=2,4 — distance 1)
    //   pair_amt[1] = middle pair    (v=1,5 — distance 2)
    //   pair_amt[2] = outermost pair (v=0,6 — distance 3)
    float pair_amt[3]   = {0.f, 0.f, 0.f};
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
      // side voices fade in by pair — innermost first, then middle, then
      // outermost — across staged windows of CCW travel. Remaining travel
      // past V7_END only widens detune.
      saw_amp = 1.f;
      const float plateau_lo = 0.5f - MODE_C_SYNTH_SAW_PLATEAU;
      if (k4 < plateau_lo) {
        const float t = (plateau_lo - k4) / plateau_lo;  // 0 at plateau edge, 1 at K4=0
        pair_amt[0] = Saturate01(t / MODE_C_SYNTH_HYPER_V3_END);
        pair_amt[1] = Saturate01((t - MODE_C_SYNTH_HYPER_V3_END) /
                                 (MODE_C_SYNTH_HYPER_V5_END - MODE_C_SYNTH_HYPER_V3_END));
        pair_amt[2] = Saturate01((t - MODE_C_SYNTH_HYPER_V5_END) /
                                 (MODE_C_SYNTH_HYPER_V7_END - MODE_C_SYNTH_HYPER_V5_END));
        detune      = MODE_C_SYNTH_DETUNE_CENTS_MIN + t *
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
    // Each pair has its own gain (staged fade-in along K4 CCW). Detune still
    // grows linearly with t and applies uniformly so voice spread is
    // continuous; the RMS norm 1/√(1 + Σ pair_amt²·voices_per_pair) keeps
    // perceived loudness flat as pairs fade in.
    const int N      = MODE_C_SYNTH_UNISON_VOICES;
    const int center = N / 2;
    float saw_sum = 0.f;
    for (int v = 0; v < N; v++) {
      const int   dist       = (v < center) ? (center - v) : (v - center);
      const float spread     = static_cast<float>(v - center) / static_cast<float>(center);
      const float voice_freq = f0 * powf(2.f, spread * detune / 1200.f);
      const float s          = saws_[v].Process(voice_freq);
      const float w          = (dist == 0) ? 1.f : pair_amt[dist - 1];
      saw_sum += s * w;
    }
    const float side_energy = 2.f * (pair_amt[0] * pair_amt[0] +
                                     pair_amt[1] * pair_amt[1] +
                                     pair_amt[2] * pair_amt[2]);
    saw_sum *= 1.f / sqrtf(1.f + side_energy);

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

  static float Saturate01(float x) {
    if (x < 0.f) return 0.f;
    if (x > 1.f) return 1.f;
    return x;
  }

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
