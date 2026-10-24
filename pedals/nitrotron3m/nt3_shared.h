#pragma once
//
// NitroTron3m — what the three NitroTron3 modules share.
//
// In pedals/nitrotron3/main.cpp the three modes are plain functions over one
// set of globals. Two of those globals are used by more than one mode and are
// therefore NOT owned by a module here, but by the shell, and handed to each
// module through Attach() before Init():
//
//   env     — one EnvFollower, fed by whichever mode is active (A / B / C), so
//             its state carries across a mode switch exactly as in the shipping
//             pedal (main.cpp:52).
//   tracker — one PitchTracker: Feed() from the active mode, Update() (YIN) in
//             the shell's main loop (main.cpp:53, 1683).
//
// Plus the bits of code more than one mode uses, copied verbatim from
// pedals/nitrotron3/main.cpp: the block-header list (main.cpp:6-25, same order),
// the per-sample control Smoother (main.cpp:223-229) and EnvExpand
// (main.cpp:346-357, used by Mode A and Mode C).
//
// Requires daisy.h + hothouse.h + constants.h + knob_map.h included before.

#include "moog_osc.h"
#include "moog_ladder.h"
#include "moog_ladder_v2.h"
#include "peak_limiter.h"
#include "env_follower.h"
#include "pitch_tracker.h"
#include "ring_buffer.h"
#include "grain_voice.h"
#include "clouds/reverb.h"
#include "resampler.h"
#include "phaser.h"
#include "grendel.h"
#include "glitch_zones.h"
#include "bitcrush.h"
#include "synth_osc_c.h"
#include "poly_octave.h"
#include "synth_osc_a.h"
#include "mode_a_hpf.h"
#include "freq_shifter.h"

// Shell-owned DSP state shared by the modules (see the header comment).
struct NT3Shared {
  EnvFollower  env;
  PitchTracker tracker;
};

// The control values a NitroTron3 mode reads — the knobs + SW1 + SW2 of the
// preset system's edit buffer (ModePresetData without the mode field). Each
// module copies them in Controls() (main loop, right after PresetSystem::Tick)
// and its Process() reads this copy where the shipping code read
// preset.GetEditBuffer() directly.
struct NT3Controls {
  float   knobs[6];
  uint8_t sw1;
  uint8_t sw2;
};

// Copy the edit-buffer values from the control source (preset_controls.h).
static inline void NT3ReadControls(const ControlSurface& cs, NT3Controls& eb) {
  for (int i = 0; i < 6; i++) eb.knobs[i] = cs.Knob(i);
  eb.sw1 = static_cast<uint8_t>(cs.Switch(0));
  eb.sw2 = static_cast<uint8_t>(cs.Switch(1));
}

// ---- verbatim from pedals/nitrotron3/main.cpp:218-229 ----
// One-pole control smoother. Knob values update only once per audio block, so a
// block-rate value applied to an audio-rate gain stair-steps = zipper noise.
// Call once per sample with the block's target; the returned value ramps toward
// it (PARAM_SMOOTH_COEF ≈ 10 ms). Call EVERY sample (not branch-conditionally)
// so the state never goes stale and there's no transient on re-entry.
struct Smoother {
  float v = 0.f;
  inline float operator()(float target) {
    v += PARAM_SMOOTH_COEF * (target - v);
    return v;
  }
};

// ---- verbatim from pedals/nitrotron3/main.cpp:346-357 ----
// Downward expander applied ONLY at the env→VCA amplitude stages (Mode A drone,
// Mode C SW1=DOWN synth) — see ENV_VCA_EXP_* in constants.h. Above the threshold
// the env passes unchanged (full touch sensitivity); below it the env is scaled
// toward zero by a power curve so a rig's noise floor can't open the VCA or
// smear note-offs. Not a gate — the response stays continuous. powf only runs
// in the sub-threshold (quiet) region. RATIO <= 1 → linear passthrough (off).
static inline float EnvExpand(float env) {
  if (ENV_VCA_EXP_RATIO <= 1.f || ENV_VCA_EXP_THRESH <= 0.f ||
      env >= ENV_VCA_EXP_THRESH)
    return env;
  return env * powf(env / ENV_VCA_EXP_THRESH, ENV_VCA_EXP_RATIO - 1.f);
}
