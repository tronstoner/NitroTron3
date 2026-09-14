#pragma once
//
// sprawl — the control seam.
//
// SprawlControls  = the RAW surface snapshot taken in Controls() (main loop).
// SprawlParams    = everything DeriveParams() computes once per audio block —
//                   exactly the per-block math that sits at the top of
//                   NitroTron3's ProcessGranular().
//
// Every downstream component (grain engine, texture shaper, feedback bus,
// reverb) consumes SprawlParams ONLY. Re-assigning a knob or a toggle in a
// later UI phase = editing DeriveParams(), nothing else.
//
#include <cstddef>

// Raw control snapshot: plain scalars written in the main loop (Controls) and
// read in the audio ISR (Process). Same pattern as the other ChronoTron3
// modules — word-sized, independently valid fields; a torn read just means one
// block sees a 10 ms-old knob.
struct SprawlControls {
  // Seeded at noon so the blocks before the first Controls() tick (audio starts
  // ~10 ms ahead of the first knob read) derive the neutral state: K2 live
  // passthrough, K3 neutral stream, K5 off (no reverb, no feedback).
  float k1 = 0.5f, k2 = 0.5f, k3 = 0.5f, k4 = 0.5f, k5 = 0.5f;  // raw cs.Knob(0..4)
  int   sw1 = 0;   // cs.Switch(0): texture mode (0=UP, 1=MID, 2=DOWN)
  int   sw2 = 0;   // cs.Switch(1): harmony mode  (0=UP, 1=MID, 2=DOWN)
  // FS1 tap-tempo override, stored as the tapped ECHO TIME in samples (the
  // audible grain read-back delay, not the buffer span). < 0 = no tap held
  // (knob rules). DeriveParams inverts it into a K2 magnitude each block
  // against the CURRENT grain length, so it stays accurate as K3 moves.
  float tap_samples = -1.f;
};

// Per-block derived parameters (one DeriveParams() call per audio block).
struct SprawlParams {
  // K1 / harmony
  float  k1               = 0.f;
  int    harmony          = 0;      // SW2: 0=fixed interval, 1/2=resonance table
  bool   freq_shift_active = false; // SW2 DOWN: Bode SSB shifter on the wet bus

  // K2 — buffer depth / direction
  bool   direct_texture   = false;  // noon deadzone: live-grain passthrough
  bool   buf_reverse      = false;  // CCW past the deadzone: backward stream
  size_t max_range        = 0;
  float  k2_scale         = 0.f;    // timescale factor (0.5× … 2×)
  bool   live_grain       = false;
  size_t base_delay       = 0;

  // K3 — character / cloud density
  bool   cloud_mode       = false;
  float  k3mag            = 0.f;
  float  grain_character  = 0.f;
  float  glitch_amount    = 0.f;
  size_t grain_len        = 0;
  float  overlap          = 0.f;
  size_t base_interval    = 0;
  float  grain_alpha      = 0.f;

  // K4 / SW1 — texture shaper
  float  k4               = 0.f;
  int    texture_mode     = 0;
  float  ringmod_inc      = 0.f;
  float  ringmod_lp_g     = 1.f;
  float  decim_amt        = 0.f;
  float  fold_amt         = 0.f;
  float  decim_rate       = 1.f;
  int    glitch_side      = 0;
  float  glitch_effect_pos = 0.f;

  // K5 — bipolar reverb / feedback
  float  reverb_amt       = 0.f;
  float  feedback_amt     = 0.f;   // already scaled by FixedIntervalFeedbackScale
};
