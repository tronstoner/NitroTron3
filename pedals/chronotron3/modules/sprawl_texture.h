#pragma once
//
// sprawl — texture shaper (SW1). 1:1 port of the `switch (texture_mode)` block
// in NitroTron3's ProcessGranular():
//   0 = bipolar K4 decimator (CCW) / clean (noon) / wavefolder (CW)
//   1 = tape/BBD colour (shared degrade engine + post-stage warble, K4 bipolar)
//   2 = ringmod — tremolo below 30 % K4, keytracked bell partials above
//
// Slot 1 was the event-driven digital glitch (GlitchEvents) until 2026-09-17.
// It is unwired, not deleted: glitch_zones.h is untouched in core/blocks and the
// GLITCH_* constants stay in sprawl_constants.h, so restoring it is re-adding
// the include, the member and the case.
//
#include "sprawl_constants.h"
#include "sprawl_params.h"
#include "mnemonic_degrade.h"   // shared BBD/Tape colour engine (mnemonic, vestige)
#include "ring_buffer.h"        // core/blocks — post-stage warble delay line
#include "sprawl_debug.h"
#include <math.h>

class SprawlTexture {
 public:
  // `warble_slab` = externally allocated SDRAM, SPRAWL_WARBLE_LEN floats.
  void Init(float sr, float* warble_slab) {
    degrade_.Init(sr);
    degrade_.SetFoldScale(SPRAWL_BBD_FOLD_SCALE);
    degrade_.SetBbdLpfScale(SPRAWL_BBD_LPF_SCALE);
    degrade_.SetTapeDriveScale(SPRAWL_TAPE_DRIVE_SCALE);
    degrade_.SetTapeLevel(SPRAWL_TAPE_LEVEL);
    degrade_.SetBbdLevel(SPRAWL_BBD_LEVEL);
    degrade_.SetTapeDepthScale(SPRAWL_TAPE_DEPTH_SCALE);
    degrade_.SetBbdSlip(SPRAWL_BBD_SLIP);
    degrade_.SetBbdReplayMix(SPRAWL_BBD_REPLAY_MIX);
    degrade_.SetBbdCrush(SPRAWL_BBD_CRUSH);
    degrade_.SetBbdDrift(SPRAWL_BBD_DRIFT);
    degrade_.SetBbdDepthComp(SPRAWL_BBD_DEPTH_COMP);
    degrade_.SetTapeDepthComp(SPRAWL_TAPE_DEPTH_COMP);
    warble_ring_.Init(warble_slab, SPRAWL_WARBLE_LEN);
    warble_base_ = SPRAWL_WARBLE_BASE_MS * 0.001f * sr;
  }

  void DebugFill(SprawlDebug& d) const {
    degrade_.DebugFill(d.deg);
    d.warble_int = warble_int_; d.decim_hold = decim_hold_; d.ringmod_lp = ringmod_lp_state_;
  }

  // Scale the colour engine's slip events to the module's current echo time
  // (see MNEMD_BBD_SLIP_REF_*). Per block, from Sprawl::Process.
  void SetSlipTimeRef(float ref_ms, float blend) {
    degrade_.SetBbdSlipTimeRef(ref_ms, blend);
  }

  // Once per audio block (the engine's chain switching is control-rate work).
  void Block(const SprawlParams& p) { degrade_.SetDepth(p.k4 * 2.f - 1.f); }

  // Mute the engine's noise injection (bypass) so hiss can't ring on forever —
  // sprawl's bypass is a trail bypass, so the wet path keeps running.
  void SetNoiseGate(float g) { degrade_.SetNoiseGate(g); }

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
      // Tape/BBD colour, bipolar K4 (clean in the engine's centre deadzone):
      // CCW = BBD (decimation + crush + rounding), CW = tape (drive + wow/
      // flutter + HF loss). Vestige's adaptation — the engine's tape pitch rides
      // a short POST-stage warble line, so no host delay topology is needed.
      // Tape speed first, then head/electronics, as in vestige.
      warble_ring_.Write(wet);
      const float wpos = (float)warble_ring_.GetWritePos() - warble_base_;
      if (degrade_.Idle()) {
        warble_int_ += (0.f - warble_int_) * SPRAWL_WARBLE_EASE;   // ease wobble to 0
        wet = warble_ring_.ReadFrac(wpos - warble_int_);
      } else {
        const float w_cents = degrade_.TapePitchCents();           // wow/flutter/snag/drift
        warble_int_ = warble_int_ * SPRAWL_WARBLE_LEAK
                    + w_cents * SPRAWL_WARBLE_CENTS_TO_RATE;
        const float w_max = warble_base_ - 2.f;
        if (warble_int_ >  w_max) warble_int_ =  w_max;
        else if (warble_int_ < -w_max) warble_int_ = -w_max;
        wet = warble_ring_.ReadFrac(wpos - warble_int_);
        wet = degrade_.ColourProcess(wet);
      }
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
  // SW1 MIDDLE: tape/BBD colour + its post-stage warble line.
  MnemDegrade degrade_;
  RingBuffer  warble_ring_;
  float       warble_base_ = 0.f;   // fixed base delay (samples) = tap centre
  float       warble_int_  = 0.f;   // leaky-integrated cents -> sample displacement
};
