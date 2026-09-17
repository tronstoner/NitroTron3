#pragma once
//
// sprawl — feedback return bus. 1:1 port of the feedback-injection block in
// NitroTron3's ProcessGranular(), in the original order:
//   build-up ducker → on-play ducker → 2-pole HPF on the return → tanh
// The result is added to the dry before the ring write.
//
#include "sprawl_constants.h"
#include <math.h>

class SprawlFeedback {
 public:
  void Init(float sr) {
    // Wet HPF coefficient
    wet_hp_coeff_ = 1.f / (1.f + 2.f * 3.14159265f * WET_HPF_FREQ / sr);
    fb_duck_atk_g_ = 1.f - expf(-1.f / (FB_DUCK_ATTACK_MS  * 0.001f * sr));
    fb_duck_rel_g_ = 1.f - expf(-1.f / (FB_DUCK_RELEASE_MS * 0.001f * sr));
    on_play_rel_g_ = 1.f - expf(-1.f / (ON_PLAY_RELEASE_MS * 0.001f * sr));
  }

  void DebugFill(float& duck_env, float& onplay_env, float& hp0, float& hp1) const {
    duck_env = fb_duck_env_; onplay_env = on_play_env_;
    hp0 = wet_hp_state_[0]; hp1 = wet_hp_state_[1];
  }

  // Returns the saturated feedback sample to add to the dry before the ring
  // write. `prev_wet` = previous sample's wet output, `env` = grain_env.
  float Inject(float prev_wet, float feedback_amt, float env) {
    // tanh on the feedback return tames runaway peaks at high K5 by turning
    // overshoot into soft saturation while preserving the additive character.
    // Build-up ducker: track |prev_wet| with slow attack / slow release,
    // pull feedback_amt down by THRESHOLD/env once env exceeds threshold.
    float abs_pw = fabsf(prev_wet);
    float g_env = (abs_pw > fb_duck_env_) ? fb_duck_atk_g_ : fb_duck_rel_g_;
    fb_duck_env_ += g_env * (abs_pw - fb_duck_env_);
    float duck_gain = (fb_duck_env_ > FB_DUCK_THRESHOLD)
                    ? (FB_DUCK_THRESHOLD / fb_duck_env_)
                    : 1.f;
    // On-play ducker: dry env opens an instant duck, releases slowly so
    // the feedback gain doesn't pulse with individual notes.
    on_play_env_ = (env > on_play_env_)
                 ? env
                 : on_play_env_ + on_play_rel_g_ * (env - on_play_env_);
    float on_play_norm = (on_play_env_ - ON_PLAY_ENV_GATE) * ON_PLAY_ENV_SCALE;
    if (on_play_norm < 0.f) on_play_norm = 0.f;
    if (on_play_norm > 1.f) on_play_norm = 1.f;
    float on_play_gain = 1.f - on_play_norm * ON_PLAY_AMOUNT;

    // 2-pole HPF on the feedback return ONLY (always on) — blocks sub/DC from
    // accumulating in the loop. Kept off the wet output so there's no gated-
    // filter click at the K2 noon boundary and the wet keeps its full range.
    wet_hp_state_[0] += (1.f - wet_hp_coeff_) * (prev_wet - wet_hp_state_[0]);
    float fb_hp1 = prev_wet - wet_hp_state_[0];
    wet_hp_state_[1] += (1.f - wet_hp_coeff_) * (fb_hp1 - wet_hp_state_[1]);
    float fb_hp = fb_hp1 - wet_hp_state_[1];

    float fb_amt_eff = feedback_amt * duck_gain * on_play_gain;
    return tanhf(fb_hp * fb_amt_eff * FB_SAT_DRIVE) / FB_SAT_DRIVE;
  }

 private:
  float wet_hp_state_[2] = {};
  float wet_hp_coeff_ = 0.f;   // computed in Init
  float fb_duck_env_ = 0.f;
  float fb_duck_atk_g_ = 0.f;  // computed in Init
  float fb_duck_rel_g_ = 0.f;
  float on_play_env_ = 0.f;
  float on_play_rel_g_ = 0.f;  // computed in Init
};
