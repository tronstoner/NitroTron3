#pragma once
//
// sprawl — diagnostic snapshot (POD). Filled by Sprawl::FillDebug() either in
// the audio ISR at the exact sample a non-finite value first appeared, or on
// the main loop as a periodic heartbeat. Printed by the shell over USB serial,
// one short line per control tick (the logger's line buffer is 128 bytes).
// Observation only: nothing here changes behaviour.
//
#include <cstdint>
#include "mnemonic_degrade.h"

struct SprawlDebug {
  uint32_t t_ms = 0;
  int      stage = 0;            // 0 = heartbeat · 1 grain-sum · 2 wet-bus · 3 post-reverb
  uint32_t fault_count = 0;
  // controls
  float k1, k2, k3, k4, k5, tap, panic, send;
  int   sw1, sw2, frozen, bypassed;
  // derived params
  float fb_amt, rev_amt, k2_scale, k3mag, glitch;
  int   cloud, live, tex, harmony;
  uint32_t max_range, base_delay, grain_len, base_interval;
  // grain engine
  int      active_voices, grain_timer, next_voice, hold_ctr, bad_voice_mask;
  float    cached_ratio;
  uint32_t wpos;
  struct Voice { int active, rev, loops; uint32_t delay, len; float rate, comp, last_out; } v[8];
  // feedback bus + envelopes
  float duck_env, onplay_env, hp0, hp1, prev_wet, grain_env, trans_slow;
  // texture: degrade engine + warble + the other two shapers
  MnemDegrade::DebugState deg;
  float warble_int, decim_hold, ringmod_lp;
  // meters (peak |x| since last heartbeat)
  float in_pk, grain_pk, tex_pk, wet_pk;
};
