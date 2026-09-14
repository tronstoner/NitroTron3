#pragma once
//
// sprawl — texture shaper (SW1). 1:1 port of the `switch (texture_mode)` block
// in NitroTron3's ProcessGranular():
//   0 = bipolar K4 decimator (CCW) / clean (noon) / wavefolder (CW)
//   1 = event-driven digital glitch (GlitchEvents, K4 bipolar)
//   2 = ringmod — tremolo below 30 % K4, keytracked bell partials above
//
// NOTE: glitch_zones.h does `#include "constants.h"` and reads the GLITCH_*
// globals from it; for this pedal that resolves to pedals/chronotron3/
// constants.h, which does NOT define them. sprawl_constants.h (included first,
// right below) supplies them at global scope — keep this include order.
//
#include "sprawl_constants.h"
#include "sprawl_params.h"
#include "glitch_zones.h"   // core/blocks — needs GLITCH_* from sprawl_constants.h
#include <math.h>

class SprawlTexture {
 public:
  void Init() { glitch_events_.Init(); }

  // One sample. `env` = envelope follower value (grain_env in the original),
  // `note_on` = upstream transient detection.
  float Process(float wet, const SprawlParams& p, float env, bool note_on) {
    switch (p.texture_mode) {
    case 0: {
      // Bipolar: CCW = decimator, noon = clean, CW = wavefolder
      if (p.decim_amt > 0.01f) {
        decim_count_ += 1.f;
        if (decim_count_ >= p.decim_rate) {
          decim_count_ -= p.decim_rate;
          decim_hold_ = wet;
        }
        wet = wet * (1.f - p.decim_amt) + decim_hold_ * p.decim_amt;
      }
      if (p.fold_amt > 0.01f) {
        float driven = wet * (1.f + p.fold_amt * 30.f);
        float folded = sinf(driven * 1.5707963f) * 0.15f;
        wet = wet * (1.f - p.fold_amt) + folded * p.fold_amt;
      }
      break;
    }
    case 1: {
      // Event-driven digital glitch: stochastic triggers, K4 alone controls density.
      wet = glitch_events_.Process(wet, p.glitch_side, p.glitch_effect_pos, env, note_on);
      break;
    }
    case 2: {
      // Ringmod: sine carrier, keytracked LPF
      float carrier = sinf(2.f * 3.14159265f * ringmod_phase_);
      ringmod_phase_ += p.ringmod_inc;
      if (ringmod_phase_ >= 1.f) ringmod_phase_ -= 1.f;
      float rm;
      if (p.k4 < 0.3f) {
        // Tremolo region: AM (50:50 clean/modulated)
        rm = wet * (0.5f + 0.5f * carrier);
      } else {
        // Bell region: true ringmod
        rm = wet * carrier;
      }
      // Keytracked one-pole LPF to tame highs
      ringmod_lp_state_ += p.ringmod_lp_g * (rm - ringmod_lp_state_);
      wet = ringmod_lp_state_;
      break;
    }
    }
    return wet;
  }

 private:
  float decim_hold_ = 0.f;        // decimator sample-and-hold value
  float decim_count_ = 0.f;       // decimator sample counter
  float ringmod_phase_ = 0.f;     // ringmod carrier oscillator phase
  float ringmod_lp_state_ = 0.f;  // one-pole LPF after ringmod
  GlitchEvents glitch_events_;    // SW1 MIDDLE: event-driven digital glitch
};
