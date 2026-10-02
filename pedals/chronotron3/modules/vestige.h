#pragma once
//
// vestige — dynamic looper / freeze (grain-based).  SW3 UP.
// Spec: docs/ChronoTron3/dynamic-looper-concept.md
//
// Onward-rework STAGE 0 (docs/ChronoTron3/vestige-onward-rework-plan.md §7):
// retirement + re-map, no new DSP. Auto capture is always on; manual capture,
// frippertronics, the K1 voice count and the K3 loop↔freeze blend are retired.
//   SW1  = mode: UP 1-voice loop · MIDDLE 6-voice loop · DOWN freeze (fixed).
//   K1   = playback speed crossfade (stage 6): CCW only half speed · noon only
//          clean · CW only double speed; tape-style (pitch + time). Loop side only.
//   K2   = T (the master period) + direction, bipolar: CCW reverse · noon
//          shortest T (100 ms) · CW forward; either end T = 8 s. On the loop side
//          T is the max capture length. Direction is inert in freeze.
//   K3   = reserved (read, unused) — error intensity.
//   K4   = capture sensitivity (auto-capture gate threshold; CCW = sensitive).
//   K5   = loop fade in/out time (unchanged).
//   K6   = dry/wet mix (shell-owned, equal-power). vestige no longer owns output.
//   SW2  = TEMPORARY: how playing loops follow a T change (stage 2.5 takes SW2
//          for the error editor). UP = A tape (speed + pitch follow) · MIDDLE =
//          B stretch (time follows, pitch stays) · DOWN = C re-cut (speed and
//          pitch stay; the loop is cut or silence-padded at its end, applied at
//          a wrap, non-destructively).
//   FS1  = tap tempo, dedicated: the interval between taps IS T; overrides K2's
//          magnitude until K2 moves (stage 1).
//   FS2  = tap: capture + playback on/off · hold: toggle buffer hold (in either
//          state). Switching OFF while not held clears the buffers.
//
// ARCHIVED, unwired but kept on disk (same pattern as
// docs/ChronoTron3/impulse resonator - armitage/ARMITAGE_ARCHIVED.md):
//   - frippertronics (fripp_mode_ is never set; its branches below are dead),
//   - the K3 loop↔freeze blend (the loop modes pin its CCW end, freeze its CW end),
//   - the K4 degradation colour (MnemDegrade held at depth 0),
//   - the K6 looper volume + SW2 dry routing.
// Each unwired site carries an "ARCHIVED" comment saying how to revive it.
//
// Reuses the core grain engine (grain_voice.h / ring_buffer.h). Playback is a
// shared pool of GrainVoice objects; each grain is tagged with the RingBuffer it
// reads from, so any number of loop-voices share the pool with bounded CPU.
//
// Storage: two physically separate SDRAM slabs, one per side.
//   LOOP side  (SW1 UP / MIDDLE) — vestige_slab: 9 voiced + 1 frippertronics
//              [archived, still allocated] + 1 record scratch, 8 s + guard each.
//   FREEZE side (SW1 DOWN)       — vestige_freeze_slab: 9 playback + 1 record
//              scratch, 400 ms + seam overhang each.
// Each side is a POOL: it records into its own scratch and commits into its own
// playback slots; nothing ever copies across. Recording writes directly into
// the pool's record scratch so it never evicts a playing loop; commit copies the
// scratch into a target slot of the same pool. Each slot's RingBuffer views its
// slab row (with that row's real length) for grain reads. On the loop side a
// wrap-guard copy of the loop head sits after the loop end so grains that read
// across the loop boundary stay seamless; freeze grains never cross the seam.
// Switching SW1 between the sides: see SwitchPool().
//
// Requires daisy.h + hothouse.h + control_surface.h + knob_map.h included first.
//
#include "module.h"
#include "constants.h"         // pedals/chronotron3 — CT3_DIAG
#include "vestige_constants.h"
#include "grain_voice.h"   // core/blocks — pulls in ring_buffer.h
#include "mnemonic_degrade.h" // BBD/Tape degradation engine (folded in on K4)
#include "grid_quantize.h"   // core/blocks — loop length -> nearest division of T
#include <cmath>
#include <cstring>         // memcpy (commit copies record scratch → target slot)
#include <cstdio>          // snprintf (DIAG builds only: the K3 CCW rhythm log line)

// ---------------------------------------------------------------------------
// SDRAM storage — one row per slot. File scope (single TU) is safe here.
// Loop-side row:   [0 .. loop_len)       = captured loop
//                  [loop_len .. +GUARD)  = copy of the loop head (wrap-guard)
// Freeze-side row: [0 .. loop_len)       = captured fragment (<= 400 ms)
//                  [loop_len .. +240)    = recorded seam overhang (see
//                                          VESTIGE_FREEZE_GUARD)
// The two arrays are distinct allocations; a row's neighbour in SDRAM is
// arbitrary data (the next row, or whatever the linker placed next), so every
// write is bounded by the row's own capacity (cap_[s]) and every grain reads
// through a RingBuffer whose length is that capacity.
// ---------------------------------------------------------------------------
static float DSY_SDRAM_BSS vestige_slab[VESTIGE_LOOP_SIDE_SLOTS][VESTIGE_VOICE_CAP];
static float DSY_SDRAM_BSS vestige_freeze_slab[VESTIGE_SLOTS - VESTIGE_FREEZE_SLOT0][VESTIGE_FREEZE_CAP];
// C (re-cut) guards: per loop voice slot, TWO (double-buffered read-time views),
// each the seam + head continuation behind a re-cut length. Separate from the
// slab so the recorded material and its capture guard are never rewritten.
static constexpr size_t VESTIGE_RECUT_GUARD_LEN = VESTIGE_GUARD_SAMPLES + 4;
static float DSY_SDRAM_BSS vestige_recut_guard[VESTIGE_VOICE_SLABS][2][VESTIGE_RECUT_GUARD_LEN];
// Post-grain warble modulated-delay line (K4 tape/BBD pitch modulation).
// Written EVERY sample (also with K4 idle), so it lives in internal RAM, not
// SDRAM: in SDRAM each write is FMC traffic that can close the row the next
// grain fetch needs. Placement: RAM_D2 (D2 SRAM1 from 0x30008000, cacheable;
// the first 32 KB = the MPU's non-cacheable DMA window stays libDaisy's). The
// libDaisy SRAM linker script maps that region only through its `.heap`
// output section (NOLOAD, before `end`, so newlib's sbrk heap starts after it).
// Not DTCM: the DIAG build leaves only ~20 KB of DTCM for the stack, this ring
// would eat almost all of it. Not RAM_D2_DMA: ~16 KB free < 19.2 KB. NOLOAD =
// not zeroed at boot; Init() memsets it (RingBuffer::Init), same as before.
#if defined(__arm__)
#define VESTIGE_D2_BSS __attribute__((section(".heap")))
#else
#define VESTIGE_D2_BSS                 // host build (tools/host): plain static
#endif
static float VESTIGE_D2_BSS vestige_warble_slab[VESTIGE_WARBLE_LEN];

// xorshift32 RNG for grain scatter (audio-thread only).
static uint32_t vestige_rng = 0x1234567u;
static inline float VestigeRand() {
  vestige_rng ^= vestige_rng << 13;
  vestige_rng ^= vestige_rng >> 17;
  vestige_rng ^= vestige_rng << 5;
  return static_cast<float>(vestige_rng) / 4294967295.f;
}

static inline float VestigeClamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

class Vestige : public Module {
 public:
  // -------------------------------------------------------------------------
  void Init(float sr) override {
    sr_ = sr;
    degrade_.Init(sr_);   // BBD/Tape degradation engine (K4)
    degrade_.SetFoldScale(VESTIGE_BBD_FOLD_SCALE);  // brighter BBD fold in the looper (clarity, keeps grit)
    MBInit();             // multiband granular freeze coeffs + band params
    warble_ring_.Init(vestige_warble_slab, VESTIGE_WARBLE_LEN);  // post-grain pitch warble
    warble_base_ = VESTIGE_WARBLE_BASE_MS * 0.001f * sr_;
    warble_int_  = 0.f;
    mute_inc_    = 1.f / (VESTIGE_TIMING_MUTE_MS * 0.001f * sr_);   // timing mode 2 rest fade step
    for (int k = 0; k < VESTIGE_TIMING_DECIM_N; k++) {                // DECIMATE post-hold low-passes
      float fc = VESTIGE_TIMING_DECIM_LP_MULT * 0.5f * sr_ / (float)VESTIGE_TIMING_DECIM_FACTORS[k];
      if (fc > 0.45f * sr_) fc = 0.45f * sr_;
      MBSetLP(decim_lp_[k], fc);
    }
    frip_od_coef_ = 1.f - expf(-1.f / (VESTIGE_FRIP_OD_RAMP_S * sr_));
    steal_inc_ = 1.f / (VESTIGE_STEAL_RELEASE_S * sr_);   // fast-release step for stolen voices
    for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) { dec_g_[q] = dec_t_[q] = rep_base_[q] = 1.f; }   // K5 repeats: full level
    release_samples_ = (uint32_t)((float)VESTIGE_AUTO_RELEASE_MS * 0.001f * sr_);  // phrase-end silence, in samples
    gate_rel_coef_   = 1.f - expf(-1.f / (VESTIGE_GATE_RELEASE_MS * 0.001f * sr_)); // gate meter fall (mode 1)
    onset_refr_len_  = (int)((float)VESTIGE_ONSET_REFRACTORY_MS * 0.001f * sr_);
    TimingBuildPatterns();
    if (CT3_DIAG) RhyBuildList();   // DIAG: the numbered K3 CCW rhythm list (sent once)
    tape_coef_ = 1.f - expf(-1.f / ((float)VESTIGE_TAPE_SMOOTH_MS * 0.001f * sr_));
    for (int q = 0; q < VESTIGE_SLOTS; q++) { rho_s_[q] = 1.f; rho_t_[q] = rho_d_[q] = 1.0; fwd_d_[q] = 0.0; div_[q] = -1; beat_k_[q] = 0.0; }
    for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) { play_len_[q] = 0; cur_view_[q] = -1; rc_building_[q] = rc_ready_[q] = false; rc_want_[q] = 0; }
    for (int g = 0; g < VESTIGE_GRAINS; g++) grain_view_[g] = -1;
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      // Each slot views its OWN side's slab row, at that row's real length —
      // the grain reader wraps at this length, so it can never leave the row.
      if (s < VESTIGE_FREEZE_SLOT0) { slab_[s] = vestige_slab[s];                             cap_[s] = VESTIGE_VOICE_CAP;  }
      else                          { slab_[s] = vestige_freeze_slab[s - VESTIGE_FREEZE_SLOT0]; cap_[s] = VESTIGE_FREEZE_CAP; }
      ring_[s].Init(slab_[s], cap_[s]);  // memsets the row
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      active_[s]   = false;
      age_[s]      = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;
      fade_target_[s] = 0.f;
      fade_phase_[s] = 0.f; fade_from_[s] = 0.f;
      fwd_[s]      = 0.f;
      dying_[s] = false; stolen_[s] = false;
    }
    // Per-pool engine addressing. Controls() refreshes the ACTIVE pool's entry
    // every tick; these defaults only matter before the first tick.
    eng_[kPoolLoop]   = PoolEngine{0.f, VESTIGE_K3_GSCALE_CCW, 0.f, 0.f, false, false, 1};
    eng_[kPoolFreeze] = PoolEngine{1.f, 1.f, 1.f, VESTIGE_FREEZE_POS_FRAC, true, false, VESTIGE_FREEZE_BANDS};
    pool_ = kPoolLoop;
    for (int g = 0; g < VESTIGE_GRAINS; g++) { grain_src_[g] = &ring_[0]; grain_slot_[g] = 0; }
    // Sensible defaults so Process is silent before the first Controls pass.
    grain_len_    = VESTIGE_CCW_GRAIN_LEN;
    overlap_      = VESTIGE_CCW_OVERLAP;
    spray_        = 0.f;
    jitter_       = 0.f;
    k3_mode_         = kLooper;
    scrub_back_frac_ = 0.f;
    freeze_pos_frac_ = 0.f;
    k3_amt_          = 0.f;
    target_voices_ = 1;
    frip_head_ = 0.f; frip_rec_ = 0.f;
  }

  void Activate() override {
    // Material persists across mode switches; nothing to reset.
  }
  void Deactivate() override {
    // Recording cannot straddle a mode switch — drop any in-flight capture and
    // any capture waiting for its grid point. The capture machine lives in the
    // audio thread, so this is a request; Process() is not called while another
    // module is active, and its first sample after re-activation honours it.
    cap_allow_ = false;
    drop_req_  = true;
    rec_full_  = false;
    commit_pending_ = false; pending_len_ = 0; overhang_left_ = 0;
    frip_od_gain_ = 0.f; frip_od_target_ = 0.f; frip_stop_pending_ = false;
    frip_head_ = 0.f; frip_rec_ = 0.f;
  }

  // -------------------------------------------------------------------------
  // Control-rate (~10 ms). Footswitch policy, knob mapping, topology, LEDs.
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    // Reserved controls are READ so the surface contract stays explicit, but
    // they have no effect in stage 0 (docs/ChronoTron3/vestige-onward-rework-plan.md).
    const float k1  = RemapKnob(cs.Knob(0)); // playback speed crossfade (half · clean · double)
    const float k2  = RemapKnob(cs.Knob(1)); // T (master period) + direction (bipolar)
    const float k3  = RemapKnob(cs.Knob(2)); // error amount: all three layers at once
    const float k4  = RemapKnob(cs.Knob(3)); // capture sensitivity (gate threshold)
    diag_k4_ = k4;                            // DIAG heartbeat only
    const float k5  = RemapKnob(cs.Knob(4)); // loop fade in/out
    const int   sw1 = cs.Switch(0);          // 0=UP 1-voice · 1=MID 6-voice · 2=DOWN freeze
    const int   sw2 = cs.Switch(1);          // K3 mode: UP 1 Euclidean · MIDDLE 2 random · DOWN 3 straight
    const FootswitchEvent f1 = cs.Foot(0);   // tap tempo: the tap interval IS T
    const FootswitchEvent f2 = cs.Foot(1);   // tap: on/off · hold: buffer hold
    // ---- K3, bipolar: noon clean; each half per SW2's K3 mode ----------------
    // Within VESTIGE_K3_DEADZONE of noon: no errors. Mode 1 (SW2 UP): the
    // RHYTHM line (stutters + rests, see TimingPlanRhythm) at depth 0 -> 1 over
    // either half (engine VESTIGE_TIMING_RHY_ENGINE; engine 0: CCW 12 : 8, CW
    // 15 : 10; engine 2: its two tables; engine 1: CW clean). Mode 2 (SW2
    // MIDDLE) CW half: the three layers (TIMING / CONDITION / PLAYBACK)
    // together at level 0 -> 1 over the half.
    {
      // Centred on the knob's real noon: a physical noon (raw 0.5) reads
      // RemapKnob(0.5) = 0.517, which a 0.02 dead zone around 0.5 would only
      // just contain. Each half normalises over its own length.
      constexpr float kNoon = (0.5f - KNOB_MIN) / (KNOB_MAX - KNOB_MIN);
      const float c3 = k3 - kNoon, a3 = fabsf(c3);
      const float half3 = (c3 > 0.f) ? (1.f - kNoon) : kNoon;
      const float u3 = (a3 <= VESTIGE_K3_DEADZONE) ? 0.f : fminf(1.f, (a3 - VESTIGE_K3_DEADZONE) / (half3 - VESTIGE_K3_DEADZONE));
      // SW2 = the K3 MODE (each bipolar): UP 1 Euclidean (CCW traditional
      // timelines, CW academic odd-cycle Euclids), MIDDLE 2 random (CW the
      // three glitch layers, CCW tbd), DOWN 3 straight tremolo / shutter (tbd).
      // A half that is not built yet is clean.
      glitch_mode_ = (sw2 == 0) ? 0 : (sw2 == 1) ? 1 : 2;
      const float cw = (c3 > 0.f && glitch_mode_ == 1) ? u3 : 0.f;
      err_level_[kErrTiming] = err_level_[kErrCondition] = err_level_[kErrPlayback] = cw;
      // Mode 1: one rhythm, its table by the side, its depth = that half's u3
      // (the running step count and the planner are shared). The ISR reads
      // side and depth separately: a pass planned across a jump over noon
      // plays one valid row of either table (RhyRow clamps per table).
      const int side = (c3 > 0.f) ? kRhyCw : kRhyCcw;
      const bool rhy_on = glitch_mode_ == 0 && (side == kRhyCcw || VESTIGE_TIMING_RHY_ENGINE != 1);   // (engine 1: CCW only)
      rhy_side_  = side;
      rhy_level_ = rhy_on ? u3 : 0.f;
    }
    if (CT3_DIAG) DiagRhyTick(k3, sw2);     // DIAG builds: the K3 mode-1 rhythm log line (CCW# / CW#)

    // ---- How playing loops follow a T change: STRETCH (the builder's pick) ---
    // Tape (speed + pitch) and re-cut (cut / pad) stay in the code, selectable
    // only through follow_mode_cfg_ (host tests); SW2 is the K3 mode now.
    follow_mode_ = (follow_mode_cfg_ >= 0) ? follow_mode_cfg_ : kFollowStretch;


    // ---- K1 = playback speed crossfade (rework stage 6, plan §4.2) -------------
    // A CROSSFADE between versions of the same loop, not an added voice: CCW end
    // = only half speed, noon (dead zone) = only clean, CW end = only double.
    // Published as a side (-1 half / 0 / +1 double) and an amount 0..1; the audio
    // thread smooths it and swaps the speed version only while it is silent
    // (UpdateSpeedXfade). Both sides: the freeze crossfades its band streams the
  // same way (ServiceMBFreeze).
    {
      const float c1 = k1 - 0.5f;
      const int side = (c1 < -VESTIGE_K1_DEADZONE) ? -1 : (c1 > VESTIGE_K1_DEADZONE) ? 1 : 0;
      float x = (fabsf(c1) - VESTIGE_K1_DEADZONE) / (0.5f - VESTIGE_K1_DEADZONE);
      if (x < 0.f) x = 0.f;
      if (x > 1.f) x = 1.f;
      k1_x_    = (side == 0) ? 0.f : x;
      k1_side_ = side;
    }
    // K6 is not read here: it is the shell's equal-power dry/wet mix.

    blink_++;

    // ---- T: the master period (K2 magnitude or FS1 tap) --------------------
    // sprawl's arbitration, exactly: the last gesture wins. A tap overrides the
    // knob; moving K2 (raw position) past a small epsilon cancels the tap. K2
    // keeps its direction job either way. T is only a PERIOD — nothing here
    // runs a clock or a downbeat (plan §4.3); see UpdateLeds for LED1.
    UpdatePeriod(cs.Knob(1), k2, f1);

    // ---- SW1 = mode --------------------------------------------------------
    const bool freeze_mode = (sw1 == 2);
    const int  want_pool   = freeze_mode ? kPoolFreeze : kPoolLoop;
    if (sw1 != sw1_prev_) {
      if (want_pool != pool_) {
        // Loop side <-> freeze side: a different buffer. See SwitchPool().
        SwitchPool(want_pool);
      } else if (recording_) {
        // UP <-> MIDDLE: same buffer, but the capture must not straddle the
        // voice-count change — close it out at what it has (stage-0 behaviour;
        // since stage 2 "what it has" is quantised to its own grid).
        if (fripp_mode_) EndRecording(); else end_req_ = true;
      }
      sw1_prev_ = sw1;
    }
    // ARCHIVED — K1 voice count / frippertronics. fripp_mode_ is never set any
    // more, so every frip branch below is dead. Revive: restore the pre-stage-0
    // K1 block here (k1 > VESTIGE_K1_NOON_HI → EnterFrippertronics(), K1 →
    // target_voices_ / frip_decay_) in place of the SW1 voice count below.
    // Voice management (retire beyond target, age-ramp gains) runs in the
    // audio thread since stage 2 — Process() block start and each activation —
    // so it has ONE owner and can never race a sample-accurate activation.
    target_voices_ = (sw1 == 1) ? VESTIGE_MAX_VOICES : 1;

    // ---- K5, bipolar: CCW decay · noon endless · CW fade ---------------------
    // Within VESTIGE_K5_DEADZONE of noon: endless repeats, shortest crossfade.
    // CW half: the fade in/out between captures, 0 -> max over the half. CCW
    // half: the NUMBER OF REPEATS N (VESTIGE_REPEAT_N_MAX just past the dead
    // zone down to 1 at full CCW, whole numbers, log taper; see UpdateRepeats);
    // captures crossfade as at noon. Fades are phase ramps that FINISH in their
    // time (no one-pole tail): attack = convex swell, release = concave.
    const float c5 = k5 - 0.5f, a5 = fabsf(c5);
    const float u5 = (a5 <= VESTIGE_K5_DEADZONE) ? 0.f : (a5 - VESTIGE_K5_DEADZONE) / (0.5f - VESTIGE_K5_DEADZONE);
    const float fade_u = (c5 > 0.f) ? u5 : 0.f;
    rep_n_ = (c5 < 0.f && u5 > 0.f) ? (int)(powf(VESTIGE_REPEAT_N_MAX, 1.f - u5) + 0.5f) : 0;   // 0 = endless
    if (rep_n_ < 0) rep_n_ = 0;
    float atk_s = fade_u * VESTIGE_FADE_ATTACK_MAX_S;
    float rel_s = fade_u * VESTIGE_FADE_RELEASE_MAX_S;
    // Floor at a short declick so K5 hard-CCW is "instant" but not a 1-sample
    // step — a voice-steal (old cut / new started with no fade) clicks otherwise.
    if (atk_s < VESTIGE_FADE_MIN_S) atk_s = VESTIGE_FADE_MIN_S;
    if (rel_s < VESTIGE_FADE_MIN_S) rel_s = VESTIGE_FADE_MIN_S;
    atk_inc_ = 1.f / (atk_s * sr_);
    rel_inc_ = 1.f / (rel_s * sr_);

    // ---- Engine addressing: fixed per mode --------------------------------
    // ARCHIVED — the K3 loop↔freeze blend (order → chaos → focus → sweep). The
    // engine is unchanged; only its addressing is: the loop modes pin the old
    // K3-CCW end (s = 0: the clean loop) and freeze pins the old K3 freeze half
    // with the band count and window from mnemonic (vestige_constants.h
    // "FREEZE"). Revive: restore the pre-stage-0 "K3 smoothness macro" + "K3
    // unified engine params" blocks, driven by k3.
    if (freeze_mode) {
      // Freeze = the old s >= 0.5 end, pinned. K2 and K3 are inert here.
      grain_len_ = VESTIGE_CW_GRAIN_LEN;
      if (grain_len_ < VESTIGE_GRAIN_MIN_LEN) grain_len_ = VESTIGE_GRAIN_MIN_LEN;
      overlap_         = VESTIGE_CW_OVERLAP;
      spray_           = (float)VESTIGE_FREEZE_SPRAY;
      jitter_          = VESTIGE_FREEZE_JITTER;
      k3_amt_          = 1.f;
      k3_chaos_        = 1.f;
      k3_focus_        = 1.f;
      k3_gscale_       = 1.f;
      k3_frozen_       = true;
      freeze_pos_frac_ = VESTIGE_FREEZE_POS_FRAC;
      k3_bands_chaos_  = VESTIGE_FREEZE_BANDS;
      k3_mode_         = kFreeze;          // legacy single-stream fallback only
      scrub_back_frac_ = 0.f;
      max_loop_len_    = VESTIGE_FREEZE_SAMPLES;   // 400 ms capture window
      rev_play_        = false;
    } else {
      // Loop modes = the old s = 0 end, pinned (clean forward COLA loop).
      grain_len_ = VESTIGE_CCW_GRAIN_LEN;
      if (grain_len_ < VESTIGE_GRAIN_MIN_LEN) grain_len_ = VESTIGE_GRAIN_MIN_LEN;
      overlap_         = VESTIGE_CCW_OVERLAP;
      spray_           = 0.f;
      jitter_          = 0.f;
      k3_amt_          = 0.f;
      k3_chaos_        = 0.f;
      k3_focus_        = 0.f;
      k3_gscale_       = VESTIGE_K3_GSCALE_CCW;
      k3_frozen_       = false;
      freeze_pos_frac_ = 0.f;
      k3_bands_chaos_  = 1;
      k3_mode_         = kLooper;          // legacy single-stream fallback only
      scrub_back_frac_ = 0.f;

      // ---- K2 sign = direction; T = the capture ceiling (rule 1) -----------
      // Past the CCW dead zone the loop plays in reverse; the dead zone and the
      // CW half play forward. T (UpdatePeriod) is the maximum capture length.
      rev_play_     = ((k2 - 0.5f) < -VESTIGE_K2_DEADZONE);
      max_loop_len_ = period_;
    }

    // ---- K4 = degradation colour (MnemDegrade, bipolar) --------------------
    // CCW half = BBD, noon (deadzone) = clean — the engine is skipped, no CPU —,
    // CW half = tape. (K4 was capture sensitivity; that is now the constant
    // VESTIGE_AUTO_THRESH.)
    // A clean zone of +-VESTIGE_K4_DEADZONE around noon (the engine's own is
    // only +-0.015 of travel, and a physical noon reads ~0.52 after RemapKnob);
    // the colour travel starts past it.
    {
      const float c = k4 - 0.5f, a = fabsf(c);
      const float d = (a <= VESTIGE_K4_DEADZONE) ? 0.f : (a - VESTIGE_K4_DEADZONE) / (0.5f - VESTIGE_K4_DEADZONE);
      degrade_.SetDepth(c < 0.f ? -d : d);
      // Deep BBD end: an input gain into the degrader over the last part of
      // the CCW travel (its low-passes make it dark and quiet there), 0 dB at
      // VESTIGE_K4_BBD_BOOST_FROM of the BBD depth up to VESTIGE_K4_BBD_BOOST_DB.
      float bdb = 0.f;
      if (c < 0.f && d > VESTIGE_K4_BBD_BOOST_FROM)
        bdb = VESTIGE_K4_BBD_BOOST_DB * (d - VESTIGE_K4_BBD_BOOST_FROM) / (1.f - VESTIGE_K4_BBD_BOOST_FROM);
      deg_in_tgt_ = powf(10.f, bdb / 20.f);
    }
    // Idle-hiss guard: with no loop captured the engine's injected noise would
    // add a hiss bed to the output, so duck it to 0 until there is content.
    bool degrade_has_content = false;
    for (int s = 0; s < VESTIGE_SLOTS; s++) if (active_[s]) { degrade_has_content = true; break; }
    degrade_.SetNoiseGate(degrade_has_content ? 1.f : 0.f);
    pitch_rate_ = 1.f;                    // no transposition (tape varispeed retired earlier)

    // ---- Capture sensitivity: a constant (was K4) ---------------------------
    auto_thresh_ = (thresh_cfg_ > 0.f) ? thresh_cfg_ : VESTIGE_AUTO_THRESH;

    // ---- FS2: tap = capture + playback on/off · hold = buffer hold ---------
    // Hold fires once, while the switch is still down, and toggles hold in
    // either on/off state. Any release that did not fire the hold is a tap.
    // (ControlSurface zeroes held_ms on the falling edge, so the hold latch —
    // not a tap-length compare — is what separates the two gestures; this is
    // the same idiom the retired FS1 stop/clear used.)
    if (f2.down && !hold_latched_ && f2.held_ms >= VESTIGE_FS_HOLD_MS) {
      held_ = !held_;
      hold_latched_ = true;
    }
    if (f2.falling) {
      if (!hold_latched_) SetEngaged(!engaged_);
      hold_latched_ = false;
    }

    // ---- Auto capture (always on while engaged and not held) ---------------
    // ARCHIVED — manual capture (SW1 UP: FS2 held = record) is retired; auto
    // capture runs in every SW1 mode. Holding locks the buffer: no resampling.
    // Since stage 2 the gate, the capture start/end and the playback start are
    // all decided per SAMPLE in the audio thread (IsrCapture). This thread only
    // sets policy: whether a capture may start, which free slot it records
    // into, and requests (end / drop) that the audio thread executes.
    if (fripp_mode_) {
      if (engaged_ && !held_) RunAutoCapture();
      else if (recording_)    EndRecording();
    } else {
      const bool allow = engaged_ && !held_;
      if (!allow && recording_) end_req_ = true;   // hold engaged mid-phrase → keep it (quantised)
      if (allow) ReserveArmSlot();                 // BEFORE allowing: the ISR needs a slot to start
      cap_allow_ = allow;
    }

    // ---- Recording auto-stop (length ceiling reached) ----------------------
    // The note is usually still ringing when the K2 ceiling cuts the capture,
    // so block re-arming until the envelope has fallen back below the close
    // threshold: the next loop starts on a fresh silence→sound transition,
    // never back-to-back.
    if (rec_full_) {
      rec_full_ = false;
      auto_rearm_block_ = true;
      EndRecording();
    }

    // ---- Deferred commit (fires after the seam overhang is recorded) -------
    if (commit_pending_) {
      commit_pending_ = false;
      CommitRecording();
    }

    // ---- Off + not held = empty --------------------------------------------
    // Switching off fades the loops out over K5; once every fade-out has
    // finished, the buffers are cleared unless hold is on. Being state-derived
    // (not a one-shot), it also clears when hold is released while stopped.
    if (!engaged_ && !held_ && HasContent() && FadesOutDone()) {
      ClearAll();
      muted_ = true;
    }
    // Same rule for the side SW1 is NOT on: leaving it faded it out; once that
    // fade has finished it is cleared unless hold is on. Also state-derived, so
    // releasing hold while on the other side clears it too.
    const int other_pool = 1 - pool_;
    if (!held_ && PoolHasContent(other_pool) && PoolFadesOutDone(other_pool))
      ClearPool(other_pool);

    // ---- Adaptive multiband bands (poly CPU) ∩ mode band count -------------
    // Voice budget (CPU-safe): 5 bands only at 1 voice, then 3 / 2 / 1 as poly
    // rises. The effective count is min(voice budget, the mode's band count):
    // 1 for the loop modes, VESTIGE_FREEZE_BANDS for freeze. Counted over the
    // active pool (the only one that can hold more than a fading tail).
    int nv = CountLive();
    int bands_voice = (nv <= VESTIGE_MB_5BAND_MAX_VOICES) ? 5
                    : (nv <= VESTIGE_MB_3BAND_MAX_VOICES) ? 3
                    : (nv <= VESTIGE_MB_2BAND_MAX_VOICES) ? 2 : 1;
    mb_nbands_ = (bands_voice < k3_bands_chaos_) ? bands_voice : k3_bands_chaos_;

    // ---- Publish the active pool's engine addressing -----------------------
    // The pool SW1 is NOT on keeps the addressing it had when it was left, so
    // its fade-out tail is rendered by its own engine (a loop tail stays a loop,
    // never reinterpreted as a freeze of its first 400 ms, and vice versa).
    eng_[pool_] = PoolEngine{k3_focus_, k3_gscale_, k3_amt_, freeze_pos_frac_,
                             k3_frozen_, rev_play_, mb_nbands_};

    // ---- LEDs --------------------------------------------------------------
    UpdateLeds(led1, led2);
  }

  // Output is shell-mixed on K6 like mnemonic/sprawl. ARCHIVED: vestige used to
  // own its output (K6 looper volume + SW2 dry routing); revive by returning
  // true here and restoring the k6_vol_/dry_gain_ mix at the end of Process.
  bool OwnsOutput() const override { return false; }
  // Off (FS2) = bypassed: the shell lifts the dry to unity (hard rule G1) while
  // the loops' fade-out tail still plays on the wet.
  bool Bypassed() const override { return !engaged_; }

  // -------------------------------------------------------------------------
  // Audio-rate. Recording writes, grain scheduler, grain sum, texture.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    // Running sample count — a TIMESTAMP source only (LED1 phase against the
    // capture anchor). It is never a grid: nothing is ever aligned to it.
    // Sample i of this block is sample number clk0 + i; the control thread
    // (which never runs mid-block) always sees the index of the NEXT sample.
    const uint32_t clk0 = sample_clock_;
    // Voice management, once per block, audio thread only (see Controls):
    // leaving MIDDLE retires the older loops oldest-first over K5, and the
    // age-ramp gains follow the live set.
    if (!fripp_mode_) { EvictToTarget(); UpdateVoicedGains(); UpdateTapeTargets(); }
    UpdateRepeats();
    const uint32_t dt0 = diag_clock_ ? diag_clock_() : 0;
    if (VESTIGE_TIMING_MODE == 3) TimingLayerTick();
    if (diag_clock_) { const uint32_t d = diag_clock_() - dt0; if (d > diag_tick_us_) diag_tick_us_ = d; }
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // ---- Tape varispeed (K4): one-pole glide so the knob doesn't zip -----
      pitch_rate_s_ += (pitch_rate_ - pitch_rate_s_) * VESTIGE_PITCH_SMOOTH;

      // ---- Frippertronics loop head (single per-sample head) --------------
      // ARCHIVED (rework stage 0): fripp_mode_ is never set, so this and the frip
      // record/playback branches below are dead. Revive via Controls()' K1 block.
      // Advance it before record/playback so both reference the same position.
      if (fripp_mode_ && frip_len_ > 0) AdvanceFripHead();

      // ---- Input envelope (drives continuous-auto gate) -------------------
      float a = fabsf(x);
      env_ += VESTIGE_ENV_COEF * (a - env_);
      // Gate meter (see VESTIGE_GATE_ENV_MODE). Mode 1 follows env_ UP at once,
      // so a capture starts on the same sample as before, but falls slowly, so
      // the low-note ripple on env_ cannot cross the close and open levels.
      if (VESTIGE_GATE_ENV_MODE == 1) {
        if (env_ > env_gate_) env_gate_ = env_;
        else                  env_gate_ += gate_rel_coef_ * (env_ - env_gate_);
      } else {
        env_gate_ = env_;
      }

      // ---- Recording ------------------------------------------------------
      if (recording_ && fripp_mode_) {   // ARCHIVED frippertronics recorder only
        float* m = slab_[rec_slot_];
        if (fripp_mode_ && frip_len_ > 0) {
          // Overdub (sound-on-sound), tape-style. The record head frip_rec_
          // moves at the varispeed rate, so we RESAMPLE the live input onto
          // every buffer cell the head sweeps this sample rather than writing
          // one sample per tick (which left gaps pitched-up / accumulated in
          // one cell pitched-down = the glitch). Decay existing, add ramped
          // input. The input ramp (frip_od_gain_) declicks record in/out; the
          // decay is ramped WITH it (eff_decay: 1.0 when faded out → matches
          // the untouched loop, real decay at full overdub) so auto-record's
          // partial ducking has no amplitude step at its seams. At rate 1 this
          // advances exactly one cell/tick = the original behaviour.
          frip_od_gain_ += frip_od_coef_ * (frip_od_target_ - frip_od_gain_);
          float eff_decay = 1.f + (frip_decay_ - 1.f) * frip_od_gain_;
          const size_t cur = (size_t)frip_rec_;
          const size_t xf  = SeamXfadeLen(frip_len_);
          if (pitch_rate_s_ >= 1.f) {
            // Upsample (rate ≥ 1): the head crossed ≥1 cell this tick. Linear-ramp
            // the input across the crossed cells (write-side dual of the read's
            // interpolation) so the tape gets a smooth slope, not a stair-step —
            // the stair-step is what bakes imaging/aliasing at non-integer rates.
            // At rate 1 exactly this writes one cell with in=x = the original.
            if (cur != frip_rec_prev_idx_) {
              size_t count = (cur + frip_len_ - frip_rec_prev_idx_) % frip_len_;
              size_t c = frip_rec_prev_idx_;
              for (size_t k = 1; k <= count; k++) {
                c++; if (c >= frip_len_) c = 0;
                float t  = (float)k / (float)count;             // 0→1 across span
                float in = frip_in_prev_ + (x - frip_in_prev_) * t;
                m[c] = m[c] * eff_decay + in * frip_od_gain_;
                // Keep the wrap-guard in lock-step at AUDIO rate (a control-rate
                // refresh lags the fast decay → seam mismatch = a click). The seam
                // crossfade region (c < xf) is refreshed as a live tail→head blend
                // so it tracks the decaying body instead of lagging a full pass —
                // that lag is the short-loop overdub click.
                if (c < xf)                          FrippSeamCell(m, frip_len_, xf, c);
                else if (c < VESTIGE_GUARD_SAMPLES)   m[frip_len_ + c] = m[c];
                if (c + 1 == frip_len_) FrippSeamXfade(m, frip_len_, xf);
              }
              frip_rec_prev_idx_ = cur;
            }
            frip_in_acc_ = 0.f; frip_in_cnt_ = 0;   // unused in this regime
          } else {
            // Downsample (rate < 1): several ticks map to one cell. Box-average
            // the input over those ticks (anti-alias the decimation), write once
            // when the cell advances.
            frip_in_acc_ += x; frip_in_cnt_ += 1;
            if (cur != frip_rec_prev_idx_) {
              float in_avg = frip_in_acc_ / (float)frip_in_cnt_;
              frip_in_acc_ = 0.f; frip_in_cnt_ = 0;
              size_t c = frip_rec_prev_idx_;
              do {
                c++; if (c >= frip_len_) c = 0;
                m[c] = m[c] * eff_decay + in_avg * frip_od_gain_;
                if (c < xf)                          FrippSeamCell(m, frip_len_, xf, c);
                else if (c < VESTIGE_GUARD_SAMPLES)   m[frip_len_ + c] = m[c];
                if (c + 1 == frip_len_) FrippSeamXfade(m, frip_len_, xf);
              } while (c != cur);
              frip_rec_prev_idx_ = cur;
            }
          }
          frip_in_prev_ = x;
          if (frip_stop_pending_ && frip_od_gain_ < 1e-3f) {
            frip_stop_pending_ = false;
            commit_pending_    = true;   // faded out → Controls commits (WriteGuard)
          }
        } else if (fripp_mode_) {
          // Frippertronics FIRST capture: lay the input onto the tape through the
          // same rate-r resampler as overdubs (tape speed sets the recording
          // density — the ONE thing that used to differ). The record head grows
          // forward from 0 (no wrap, no guard, no decay: empty buffer); the loop
          // length = the cells the head sweeps. The seam is synthesised at commit
          // (RefreshFrippGuard), so no overhang here. Consequence, by design: the
          // faster the tape, the sooner it fills → shorter max window (real tape).
          frip_rec_ += pitch_rate_s_;
          const size_t cur = (size_t)frip_rec_;
          if (pitch_rate_s_ >= 1.f) {
            if (cur != frip_rec_prev_idx_) {
              size_t count = cur - frip_rec_prev_idx_;      // forward, no wrap
              size_t c = frip_rec_prev_idx_;
              for (size_t k = 1; k <= count; k++) {
                c++;
                if (c >= VESTIGE_VOICE_CAP) { rec_full_ = true; break; }
                float t = (float)k / (float)count;          // 0→1 linear ramp
                m[c] = frip_in_prev_ + (x - frip_in_prev_) * t;
              }
              frip_rec_prev_idx_ = cur;
            }
            frip_in_acc_ = 0.f; frip_in_cnt_ = 0;   // unused in this regime
          } else {
            frip_in_acc_ += x; frip_in_cnt_ += 1;
            if (cur != frip_rec_prev_idx_) {
              float in_avg = frip_in_acc_ / (float)frip_in_cnt_;
              frip_in_acc_ = 0.f; frip_in_cnt_ = 0;
              size_t c = frip_rec_prev_idx_;
              while (c != cur) {
                c++;
                if (c >= VESTIGE_VOICE_CAP) { rec_full_ = true; break; }
                m[c] = in_avg;
              }
              frip_rec_prev_idx_ = cur;
            }
          }
          frip_in_prev_ = x;
          rec_idx_ = cur;                             // growing loop length
          if (rec_idx_ >= max_loop_len_) rec_full_ = true;
        }
      }
      // ---- Voiced / freeze capture: the sample-accurate machine -----------
      if (!fripp_mode_) IsrCapture(x, clk0 + (uint32_t)i);
      // DIAG: meter trace every 20 ms while anything is sounding or capturing.
      if (CT3_DIAG && ++diag_trace_ >= 960u) {
        diag_trace_ = 0;
        // Only once the level can matter to the gate (above the re-arm "quiet"
        // level) — touching the strings must not flood the log.
        if (env_gate_ > auto_thresh_ * VESTIGE_AUTO_HYST * VESTIGE_REARM_DEEP || recording_)
          DiagPush(GateDiag{'M', recording_ ? 'R' : '-', (uint8_t)rearm_block_, clk0 + (uint32_t)i,
                            env_, env_gate_, onset_slow_, 0.f, 0u, 0u});
      }
      // ---- K1 speed crossfade amount (smoothed; version swap at silence) --
      UpdateSpeedXfade();
      // ---- C re-cut: background build of the spare view's guard ---------
      rec_clock_now_ = clk0 + (uint32_t)i;
      IsrRecutBuild();

      // ---- Grain scheduler + per-slot sum + fade envelope -----------------
      // Mute/unmute rides the per-slot fade (K5), so the scheduler runs even
      // while muted so the fade-out tail can play; fully-faded slots sum to 0.
      // Unified engine: ServiceMBFreeze is the ONE granular engine. Since rework
      // stage 0 its params no longer morph on K3 — Controls() pins them to the
      // clean-loop end (SW1 UP/MIDDLE) or the multiband-freeze end (SW1 DOWN).
      // Legacy single-stream looper/scrub/freeze stays behind VESTIGE_MB_FREEZE=0.
      if (fripp_mode_) {
        if (VESTIGE_MB_FREEZE) ServiceMBFreeze(VESTIGE_FRIP_SLOT);
        else                   ServiceSlot(VESTIGE_FRIP_SLOT);
      } else {
        // Both pools' playback slots. The pool SW1 is not on only plays its
        // fade-out tail; once a slot there is silent it is PARKED — not
        // scheduled at all, so it spends no CPU and takes no grains from the
        // shared pool / grain cap the active side needs.
        for (int p = 0; p < 2; p++) {
          const int lo = PoolLo(p), hi = PoolHi(p);
          for (int v = lo; v < hi; v++) {
            if (!active_[v]) continue;             // (both services no-op on it anyway)
            if (p != pool_ && Parked(v)) { AdvanceParkedHeads(v); continue; }
            if (VESTIGE_MB_FREEZE) ServiceMBFreeze(v);
            else                   ServiceSlot(v);
          }
        }
      }
      // Per slot, per version: [0] = clean (rate 1), [1] = K1 speed version.
      float slot_sum[VESTIGE_SLOTS] = {0.f};
      float slot_sp[VESTIGE_SLOTS]  = {0.f};
      // Only the grains in grain_live_ (a superset of the active ones), visited
      // in ASCENDING index order — the same grains in the same order as a scan
      // of all VESTIGE_GRAINS, so the float sums are bit-identical. A grain that
      // ended (here, or overwritten elsewhere) leaves the mask.
      for (int w = 0; w < kGrainLiveWords; w++) {
        for (uint32_t m = grain_live_[w]; m != 0; m &= m - 1) {
          const int b = __builtin_ctz(m);
          const int g = w * 32 + b;
          if (grains_[g].IsActive()) {
            const float gv = grains_[g].Process(*grain_src_[g]);
            if (grain_ver_[g]) slot_sp[grain_slot_[g]]  += gv;
            else               slot_sum[grain_slot_[g]] += gv;
          }
          if (!grains_[g].IsActive()) grain_live_[w] &= ~(1u << b);
        }
      }
      float y = 0.f;
      for (int s = 0; s < VESTIGE_SLOTS; s++) {
        // Skip fully dormant slots — no grains, silent, not fading in — so they
        // cost nothing (and no cosf/sinf). Most slots at low voice counts.
        if (slot_sum[s] == 0.f && slot_sp[s] == 0.f && fade_gain_[s] == 0.f && fade_target_[s] < 0.5f) continue;
        // One duration-based fade for both directions. fade_phase_ ramps 0→1
        // over the (attack|release) time; fade_from_ is the gain the fade
        // started at, so an interrupted fade resumes smoothly with no jump.
        const bool rising = (fade_target_[s] > 0.5f);
        // Stolen (fast-released) voices fade out at steal_inc_, not the K5 rate.
        const bool fast = stolen_[s];
        if (fade_phase_[s] < 1.f) {
          fade_phase_[s] += rising ? atk_inc_ : (fast ? steal_inc_ : rel_inc_);
          if (fade_phase_[s] > 1.f) fade_phase_[s] = 1.f;
          // Recompute the fade gain ONLY while actively fading. Once fade_phase_
          // reaches 1 the gain is constant, so steady-state playback does NO trig
          // here (this was the bulk of the idle CPU baseline).
          const float p = fade_phase_[s];
          // EQUAL-POWER crossfade curves: rise = sin(0.5*pi*p), fall = cos(0.5*pi*p),
          // so rise^2 + fall^2 == 1. A steal crossfades TWO uncorrelated loops (old
          // out / new in) whose POWERS add — linear-complementary (1-cos / 1-sin)
          // Hann curves dip ~7.7 dB mid-fade, heard as a duck/hiccup. Equal-power
          // holds level constant. Still LUT-only (no per-sample trig) via the
          // identities sin(0.5*pi*p)=sqrt(GrainHannRise(p)), cos=sqrt(1-that); both
          // ends keep a finite slope (click-free).
          if (rising) {
            float sh = sqrtf(GrainHannRise(p));              // = sin(0.5*pi*p)
            fade_gain_[s] = fade_from_[s] + (1.f - fade_from_[s]) * sh;
          } else {
            float sh = sqrtf(1.f - GrainHannRise(p));        // = cos(0.5*pi*p)
            fade_gain_[s] = fade_from_[s] * sh;
          }
        }
        // Playback path per slot: speed crossfade -> [error stage] -> K5 fade.
        //  1. K1 speed crossfade (both sides): clean and speed versions of the
        //     same loop / freeze, equal-power. At noon it is exactly the clean sum.
        float pv = slot_sum[s];
        if (!(g_c_ == 1.f && g_sp_ == 0.f))
          pv = slot_sum[s] * g_c_ + slot_sp[s] * g_sp_;
        //  2. Error stage (plan §5, stages 3-5) goes HERE, on whatever speed K1
        //     selected. Signal-domain errors (CONDITION: mutes, rate reduction)
        //     replace this pass-through; head-domain ones (PLAYBACK speed /
        //     direction, TIMING re-length) compose with SpeedRatio() and the
        //     head advance in ServiceMBFreeze — the speed crossfade needs no
        //     restructuring for either.
        pv = PlaybackErrors(s, pv);
        //     TIMING mode 2 sample-rate reduction: hold every decim_n_-th
        //     sample, crossfaded in/out over VESTIGE_TIMING_MUTE_MS.
        if (s < VESTIGE_VOICE_SLABS && (decim_d_[s] != 0.f || decim_dt_[s] != 0.f)) {
          float& d = decim_d_[s]; const float t = decim_dt_[s];
          if (d < t) { d += mute_inc_; if (d > t) d = t; }
          else if (d > t) { d -= mute_inc_; if (d < t) d = t; }
          const int ck = decim_k_[s];
          if (decim_init_[s]) {                             // start at the signal: no bump
            decim_init_[s] = false; decim_c_[s] = 0; decim_h_[s] = pv;
            if (ck >= 0) { const float* c = decim_lp_[ck]; decim_z1_[s] = pv * (1.f - c[0]); decim_z2_[s] = pv * (c[2] - c[4]); }
          }
          if (++decim_c_[s] >= decim_n_[s]) { decim_c_[s] = 0; decim_h_[s] = pv; }
          float yc = decim_h_[s];
          if (ck >= 0) {                                    // post-hold 2-pole LP (TDF-II)
            const float* c = decim_lp_[ck];
            const float x = yc;
            yc = c[0] * x + decim_z1_[s];
            decim_z1_[s] = c[1] * x - c[3] * yc + decim_z2_[s];
            decim_z2_[s] = c[2] * x - c[4] * yc;
          }
          pv = (d >= 1.f) ? yc : pv + (yc - pv) * d;
        }
        //     TIMING mode 2 rests: a linear VESTIGE_TIMING_MUTE_MS fade.
        if (s < VESTIGE_VOICE_SLABS && (mute_d_[s] != 0.f || mute_dt_[s] != 0.f)) {
          float& d = mute_d_[s]; const float t = mute_dt_[s];
          if (d < t) { d += mute_inc_; if (d > t) d = t; }
          else if (d > t) { d -= mute_inc_; if (d < t) d = t; }
          pv *= 1.f - d;
        }
        //  3. K5 loop fade.
        if (s < VESTIGE_VOICE_SLABS) {                        // K5 CCW: the per-repeat level
          float& g = dec_g_[s]; const float t = dec_t_[s], st = dec_step_[s];
          if (g < t) { g += st; if (g > t) g = t; } else if (g > t) { g -= st; if (g < t) g = t; }
          pv *= g;
        }
        y += pv * fade_gain_[s];
        // Free a retired (dying) voiced slot once its fade-out has completed.
        if (dying_[s] &&
            fade_target_[s] < 0.5f && fade_phase_[s] >= 1.f) {
          active_[s] = false; dying_[s] = false; stolen_[s] = false;
          loop_len_[s] = 0; gain_[s] = 0.f;
        }
      }

      // ---- Post-grain tape/BBD warble + degrade colour (K4) ---------------
      // GATED: when K4 is clean (degrade idle), the modulation (3 sinf/sample in
      // TapePitchCents) and the colour chain (control-rate powf + filters in
      // ColourProcess) do audible NOTHING but still burn CPU — so skip them.
      // This is why K4-at-noon didn't relieve the overload before: the always-on
      // cost wasn't gated. (The fixed-base-delay tap used to stay on both paths
      // so engaging/disengaging K4 didn't step the wet delay; since degrade is
      // retired the idle path bypasses it — see ARCHIVED note below.)
      warble_ring_.Write(y);
      if (degrade_.Idle()) {
        // K4 at noon: the colour engine is skipped (no CPU) and the wet goes
        // straight through, on the grid. The ring is still written, so the
        // path below has its history the moment K4 leaves noon.
        warble_int_ += (0.f - warble_int_) * VESTIGE_ROUTING_SMOOTH;   // ease wobble to 0
      } else {
        float w_cents = degrade_.TapePitchCents();            // wow/flutter/snag/drift
        warble_int_ = warble_int_ * VESTIGE_WARBLE_LEAK
                    + w_cents * VESTIGE_WARBLE_CENTS_TO_RATE;
        const float w_max = warble_base_ - 2.f;
        if (warble_int_ >  w_max) warble_int_ =  w_max;
        else if (warble_int_ < -w_max) warble_int_ = -w_max;
        const float yd = warble_ring_.ReadFrac((float)warble_ring_.GetWritePos()
                                  - warble_base_ - warble_int_);
        // The warble tap sits ~3 ms (warble_base_) behind the direct wet: the
        // engine's own engage fade (Mix, 0 -> 1 leaving noon, back to 0 before
        // Idle) crossfades direct -> delayed + coloured, so neither edge steps
        // the delay. Engaged, the wet is ~3 ms late; at noon it is on the grid.
        deg_in_g_ += (deg_in_tgt_ - deg_in_g_) * VESTIGE_ROUTING_SMOOTH;   // (smoothed: no zipper)
        const float yc = degrade_.ColourProcess(yd * deg_in_g_);   // tape speed first, then head/electronics
        const float m  = degrade_.Mix();
        y = y * (1.f - m) + yc * m;
      }

      // WET only: the shell mixes it against the dry on K6 (equal-power).
      // ARCHIVED — vestige-owned output: was
      //   wet[i] = dry_gain_s_ * x + k6_vol_s_ * y;
      // with k6_vol_ (K6 looper volume 0 → unity at noon → VESTIGE_LOOP_BOOST_MAX)
      // and dry_gain_ (SW2: UP clean · MID cut while recording/armed · DOWN off),
      // both one-pole smoothed at VESTIGE_ROUTING_SMOOTH. Revive with OwnsOutput().
      wet[i] = y;
    }
    sample_clock_ = clk0 + (uint32_t)size;
    if (diag_clock_) {                                    // DIAG: block time + grain peak
      const uint32_t d = diag_clock_() - dt0; if (d > diag_proc_us_) diag_proc_us_ = d;
      int a = 0; for (int g = 0; g < VESTIGE_GRAINS; g++) if (grains_[g].IsActive()) a++;
      if (a > diag_gmax_) diag_gmax_ = a;
    }
  }

 private:
  // -------------------------------------------------------------------------
  // Grain scheduling for one slot.
  // -------------------------------------------------------------------------
  // Effective grain length for a slot: the K3 macro length, capped so it never
  // exceeds the loop or the wrap-guard. (Short loops → short grains → the loop
  // simply repeats continuously, which is the EHX-style short-sample freeze.)
  size_t SlotGrainLen(size_t L) const {
    size_t glen = grain_len_;
    if (glen > L) glen = L;
    if (glen > VESTIGE_GUARD_SAMPLES) glen = VESTIGE_GUARD_SAMPLES;
    if (glen < VESTIGE_GRAIN_MIN_LEN) glen = (L < VESTIGE_GRAIN_MIN_LEN) ? L : VESTIGE_GRAIN_MIN_LEN;
    return glen;
  }

  // Advance the single frippertronics loop head one sample. K3 sets its motion:
  // forward (looper), backward (scrub), or pinned to the live freeze point.
  // Grains read from it and recording writes to it → one head, phrases land
  // where played. Phase-locked forward grains COLA-sum to a clean tape stream,
  // so CCW is a seamless loop with no straight-head special case.
  void AdvanceFripHead() {
    const int    s = VESTIGE_FRIP_SLOT;
    const size_t L = frip_len_;
    // Record phase: ALWAYS advances forward at rate 1.0 (a normal tape write
    // head), independent of K3. Overdub writes here, so recording lays material
    // forward through the buffer even while playback scrubs or freezes.
    frip_rec_ += 1.f;
    while (frip_rec_ >= (float)L) frip_rec_ -= (float)L;
    // Playback tap: K3 scans it. Unified engine (VESTIGE_MB_FREEZE): the head
    // ALWAYS advances forward — the freeze pinning + break-up scatter now live in
    // the grain emit (base = head → freeze point via focus). Legacy fallback keeps
    // the old kFreeze pin / kScrub backward-scrub head motion.
    if (!VESTIGE_MB_FREEZE && k3_mode_ == kFreeze) {
      size_t glen = SlotGrainLen(L);
      float end_anchor = (float)L - ((float)glen + spray_);
      if (end_anchor < 0.f) end_anchor = 0.f;
      frip_head_ = end_anchor * freeze_pos_frac_;          // pinned freeze point
    } else if (!VESTIGE_MB_FREEZE && k3_mode_ == kScrub) {
      frip_head_ -= scrub_back_frac_;
      while (frip_head_ >= (float)L) frip_head_ -= (float)L;
      while (frip_head_ < 0.f)      frip_head_ += (float)L;
    } else {  // kLooper
      // Playback advances forward on its OWN (continuous across K3 transitions —
      // snapping it to frip_rec_ jumped the read position when returning from
      // scrub/freeze = a click). Record tracks playback here so overdub is in
      // time; they only diverge while scrubbing/frozen. Tape varispeed: the head
      // moves at pitch_rate_s_, so the loop period follows pitch and the record
      // head (tracking it) resamples the live input — the intended tape feel.
      frip_head_ += pitch_rate_s_;
      while (frip_head_ >= (float)L) frip_head_ -= (float)L;
      while (frip_head_ < 0.f)       frip_head_ += (float)L;
      // Record head TRAILS the play tap by a small gap (tape-machine head gap):
      // the read passes each spot first, the write overwrites it a moment later,
      // so playback returns the PREVIOUS revolution's content — an overdub comes
      // back one loop later (real looper), not as a slapback that combs against
      // the clean signal. Grains also read forward, away from the trailing write-
      // frontier → the varispeed hash stays fixed. Skip if the loop is too short.
      float gap = (float)VESTIGE_FRIP_HEAD_GAP;
      if (gap > (float)L * 0.5f) gap = 0.f;
      frip_rec_ = frip_head_ - gap;
      while (frip_rec_ < 0.f)       frip_rec_ += (float)L;
      while (frip_rec_ >= (float)L) frip_rec_ -= (float)L;
    }
    play_pos_[s] = (size_t)frip_head_;
  }

  void ServiceSlot(int s) {
    const size_t L = loop_len_[s];
    if (!active_[s] || L < VESTIGE_GRAIN_MIN_LEN) return;
    if (--timer_[s] > 0) return;

    size_t glen = SlotGrainLen(L);
    // Varispeed coverage guard: a grain reads glen*rate source samples. Pitched
    // up it reads further, so cap glen to keep the read inside the wrap-guard's
    // head-continuation copy (else it wraps through the seam mid-grain = click).
    if (pitch_rate_s_ > 1.f) {
      size_t cov = (size_t)((float)VESTIGE_GUARD_SAMPLES / pitch_rate_s_);
      if (glen > cov) glen = cov;
    }
    const bool is_frip = (s == VESTIGE_FRIP_SLOT);

    // Freeze (noon→CW): pin the head to the LIVE freeze point before emitting.
    // Frip's head is pinned per-sample in AdvanceFripHead(); voiced slots pin
    // here.
    if (!is_frip && k3_mode_ == kFreeze) {
      float end_anchor = (float)L - ((float)glen + spray_);   // deepest safe point
      if (end_anchor < 0.f) end_anchor = 0.f;
      play_pos_[s] = (size_t)(end_anchor * freeze_pos_frac_);
    }

    EmitGrain(s, glen);

    // Hop = grain/overlap, so overlap stays >= 1 (continuous output).
    size_t hop = (size_t)((float)glen / overlap_);
    if (hop < VESTIGE_MIN_INTERVAL) hop = VESTIGE_MIN_INTERVAL;

    // Head advance: voiced slots step play_pos per emit here; frip's head
    // advances per-sample in AdvanceFripHead(), so skip it.
    if (!is_frip) {
      if (k3_mode_ == kScrub) {
        // Backward auto-scrub (CCW→noon); speed per K3, 0 at noon. Live.
        float ppos = (float)play_pos_[s] - scrub_back_frac_ * (float)hop;
        ppos = fmodf(ppos, (float)L); if (ppos < 0.f) ppos += (float)L;
        play_pos_[s] = (size_t)ppos;
      } else if (k3_mode_ == kLooper) {
        // Tape varispeed: advance the head at the pitch rate so the loop period
        // follows pitch (grains are also read at that rate in EmitGrain).
        size_t adv = (size_t)((float)hop * pitch_rate_s_);
        if (adv < 1) adv = 1;
        play_pos_[s] += adv;                                  // forward loop @ rate
        while (play_pos_[s] >= L) play_pos_[s] -= L;
      }
      // kFreeze: head pinned above; no advance.
    }

    // Reset the timer, with a little jitter (small so the freeze stays steady).
    float j = (VestigeRand() * 2.f - 1.f) * jitter_ * 0.6f;
    int itv = (int)((float)hop * (1.f + j));
    if (itv < (int)VESTIGE_MIN_INTERVAL) itv = (int)VESTIGE_MIN_INTERVAL;
    timer_[s] = itv;
  }

  void EmitGrain(int s, size_t glen) {
    int g = -1;
    for (int k = 0; k < VESTIGE_GRAINS; k++) {
      int idx = (next_grain_ + k) % VESTIGE_GRAINS;
      if (!grains_[idx].IsActive()) { g = idx; next_grain_ = (idx + 1) % VESTIGE_GRAINS; break; }
    }
    if (g < 0) return;   // pool exhausted → drop (glitch, acceptable)

    const size_t L = loop_len_[s];
    // Position = the read head + a NARROW phasing spray (not a full-buffer
    // scatter): overlapping grains around the (frozen at freeze) head phase
    // against each other for a consistent, evolving freeze.
    float slot_spray = spray_;
    if (L < VESTIGE_SHORT_LEN) {                 // short loop: keep spray from wrapping
      float cap = (float)L * 0.125f;
      if (slot_spray > cap) slot_spray = cap;
    }
    float off = (VestigeRand() * 2.f - 1.f) * slot_spray;
    float pos = fmodf((float)play_pos_[s] + off, (float)L);
    if (pos < 0.f) pos += (float)L;
    size_t posi = (size_t)pos;

    // Delay = distance from the (frozen) write head back to this absolute index.
    const size_t wp  = ring_[s].GetWritePos();
    const size_t cap = ring_[s].GetLength();   // this row's real length (loop 8 s+guard, freeze 400 ms+240)
    size_t delay = (wp + cap - posi) % cap;

    grain_src_[g]  = &ring_[s];
    grain_slot_[g] = s;
    // Overlap gain-compensation: Hann overlap-add is COLA (flat) only at 2×;
    // at 3× the sum ripples ~1.5×, so scale by 2/overlap to hold level constant.
    float ov_comp = 2.f / overlap_;
    // rate 1.0, forward, full Hann (alpha=1) so overlap-add stays click-free.
    // First grain of a fresh loop: (near-)instant attack so playback starts
    // immediately, softened proportionally toward freeze (k3_amt_) to avoid a
    // sharp attack-repeat there. attack_scale 0 = instant, 1 = normal fade-in.
    float atk_scale = first_grain_[s] ? k3_amt_ : 1.f;
    // rate = tape varispeed (K4): the grain reads its window at this rate, so
    // playback transposes; the head speed above matches it → coupled pitch+time.
    MarkGrainLive(g);                      // hot loop renders it from now on
    grains_[g].Trigger(ring_[s], delay, glen, false, pitch_rate_s_,
                       gain_[s] * ov_comp, 1, 1.0f, atk_scale);
    first_grain_[s] = false;
  }

  // -------------------------------------------------------------------------
  // Unified K3 granular engine: order → chaos → focus → sweep (one cloud).
  // Per band (low/mid/high): a grain cloud, each grain band-limited by a per-grain
  // filter (no band buffers), scanning at a COPRIME length so bands never re-sync.
  // K3 morphs the cloud continuously (see vestige_constants.h):
  //   • grain length ×k3_gscale_ : long clean-loop grains → freeze size.
  //   • read base = forward head, pulled onto the swept freeze point as focus→1.
  //   • coprime scan faded in with focus (0 = clean loop, 1 = evolving freeze).
  //   • scatter 0 at CCW → wide through the break-up → collapses to the band spray
  //     as focus→1 (the "focusing brings order").
  //   • band count grows with chaos (1 at the clean end), capped by the voice CPU
  //     budget in Controls.
  // At s>=0.5 every lever reduces to the prior multiband freeze exactly.
  // -------------------------------------------------------------------------
  void ServiceMBFreeze(int s) {
    size_t L = PlayLen(s);   // == loop_len_ unless C re-cut has changed it
    if (!active_[s] || L < VESTIGE_GRAIN_MIN_LEN) return;

    // Forward read head (clean-loop / break-up anchor). Frip advances its own head
    // per-sample in AdvanceFripHead; voiced slots advance here (once per sample).
    // K2 CCW half = reverse: the head walks backward (and EmitBandGrain reads
    // each grain backward) so the loop plays in reverse. rev_play_ is always
    // false in freeze mode, where the head is irrelevant (focus = 1).
    // Addressing comes from the slot's OWN pool (eng_), not the live controls:
    // a pool SW1 has left keeps rendering its fade-out tail as it sounded.
    const PoolEngine& e = eng_[PoolOf(s)];
    const bool is_frip = (s == VESTIGE_FRIP_SLOT);
    // A fresh loop whose guard is still being written plays forward until it is
    // ready (forward never outruns the guard job; reverse could read it at once).
    const bool rev = e.rev && GuardReady(s);
    // Tape rate (SW2 A): 1 unless this loop is following a changed T.
    const double rho_d = SmoothTape(s);
    const float  rho   = rho_s_[s];
    if (!is_frip) {
      // pass_ counts the clean head's wraps: the half-speed version needs its
      // parity (it covers the loop once per TWO clean passes).
      AdvanceHead(s, rev, rho_d);
      AdvanceBeat(s, rho_d);
      // The advance may have been the wrap that applied a new C length: this
      // sample's grains must be sized for (and bound to) the NEW loop.
      L = PlayLen(s);
      // TIMING error (stage 3): pass-start plan + in-pass retrigger. Loop side
      // only; nothing at all happens while the timing level is 0.
      if (s < VESTIGE_VOICE_SLABS && !e.frozen) TimingStep(s, rev, L);
      else if (s < VESTIGE_VOICE_SLABS) { TimingCond(s, 0); lrev_[s] = false; }   // no timing: nothing hangs
    }
    const bool orig = OrigPath(s, rho_d) && cur_view_[s] < 0;   // no T change has touched this loop
    // Grain read rate: tape reads at the head rate (pitch follows), stretch at
    // 1 (pitch stays; only the head moves at rho). K1 multiplies either.
    const bool  stretch = (follow_mode_ == kFollowStretch);
    const float gr      = stretch ? 1.f : rho;
    // The READ head: the timeline head, minus the TIMING retrigger offset (the
    // timeline itself never moves). == fwd_ unless a retrigger is in progress.
    const float head = is_frip ? frip_head_ : ReadHead(s);
    // Grain direction: the loop's, flipped inside a TIMING reverse span.
    const bool grev = (s < VESTIGE_VOICE_SLABS && lrev_[s]) ? !rev : rev;
    // Seam-crossing (non-frozen) grains need the head-continuation guard behind
    // L; clamp to what this row actually has. Loop rows always have the full
    // VESTIGE_GUARD_SAMPLES (unchanged). Frozen grains never cross the seam, so
    // they keep the old cap — the freeze row's short guard does not bind them.
    const size_t gcap = e.frozen ? VESTIGE_GUARD_SAMPLES : SlotGuard(s, L);
    // K1 on the freeze (frozen slots): the same crossfade as the loop side — a
    // clean and a speed version of every band stream. A version returning from
    // silence restarts ALL its bands at once with an instant attack, under the
    // smoothed crossfade gain (~VESTIGE_K1_GATE_EPS). A fresh freeze keeps the
    // pool's soft entry (e.amt) for whichever version emits first. At noon
    // neither restart can happen: the freeze is exactly as before.
    const bool frz_fresh = e.frozen && first_grain_[s];
    const bool frz_rs_c  = e.frozen && !frz_fresh && ver_idle_[s][0] && g_c_ > VESTIGE_K1_GATE_EPS;
    const bool frz_rs_sp = e.frozen && ver_idle_[s][1] && g_sp_ > VESTIGE_K1_GATE_EPS;

    // Per-band-count tables are indexed [nbands-1][band]: the log-spaced
    // filterbank (built in MBInit) plus the tunable glen/scan/spray rows. The band
    // index bi maps straight through — no remap — so every band count 1..MAX gets
    // its own full-spectrum split. 1-band = no filter (coef nullptr).
    const int nb  = e.nbands;
    const int row = nb - 1;
    for (int bi = 0; bi < nb; bi++) {
      size_t glen = (size_t)((float)VESTIGE_MB_GLEN[row][bi] * e.gscale);
      if (glen > L) glen = L;
      if (glen > gcap) glen = gcap;
      if (glen < VESTIGE_GRAIN_MIN_LEN) glen = (L < VESTIGE_GRAIN_MIN_LEN) ? L : VESTIGE_GRAIN_MIN_LEN;
      size_t maxscan = (L > glen + 1) ? (L - glen - 1) : 1;
      size_t scanlen = VESTIGE_MB_SCAN[row][bi]; if (scanlen > maxscan) scanlen = maxscan; if (scanlen < 1) scanlen = 1;
      mb_scan_[s][bi] += 1.f;
      if (mb_scan_[s][bi] >= (float)scanlen) mb_scan_[s][bi] -= (float)scanlen;
      // Clean-stream grain at the tape rate (== glen when rho == 1).
      // Stretching (rate != 1) uses short stretch grains; at rate 1 — also
      // after a stretch, head no longer on an integer — the long loop grains,
      // i.e. the reconstruction as before.
      size_t glen_c = glen;
      if (!e.frozen && !orig) {
        if (stretch && rho_d != 1.0) {
          const size_t gst = (size_t)((float)VESTIGE_STRETCH_GRAIN_MS * 0.001f * sr_);
          glen_c = CoverGrain(glen < gst ? glen : gst, L, gcap, 1.f);
        } else {
          glen_c = CoverGrain(glen, L, gcap, gr);
        }
      }
      // A version returning from silence emits its restart grain (instant
      // attack) the very sample its gain crosses the gate — i.e. while it is
      // still ~VESTIGE_K1_GATE_EPS — not a hop later at an audible gain.
      if ((ver_idle_[s][0] && !e.frozen && g_c_ > VESTIGE_K1_GATE_EPS) || frz_rs_c) mb_timer_[s][bi] = 1;
      // Tape path: a stream holds at most 2 grains. When the grain length
      // changes mid-glide, the next grain waits for one to end instead of
      // being the 3rd (which a full pool would refuse = a dropout).
      if (!orig && !e.frozen && mb_timer_[s][bi] <= 1 && StreamGrains(s, 0) >= 2) mb_timer_[s][bi] = 2;
      if (--mb_timer_[s][bi] <= 0) {
        // Base = forward head → swept freeze point as focus→1; scan fades in with
        // focus so the clean loop has no scan and the freeze has full scan.
        float span = (float)L - (float)glen - (float)scanlen; if (span < 0.f) span = 0.f;
        float freeze_base = span * e.pos_frac;
        float base = head + (freeze_base - head) * e.focus;
        float posf = base + mb_scan_[s][bi] * e.focus;
        // Position spray = the small freeze phasing spray ONLY, faded in with focus.
        // NO random break-up scatter: displaced grains on the moving head read as
        // slapback echoes. The break-up decorrelates via grain-shortening + band-
        // split + the motion-conserving scan (base rate stays 1.0), never a jump.
        float spray_width = (float)VESTIGE_MB_SPRAY[row][bi] * e.focus;
        const float* coef = (nb == 1) ? nullptr : mb_bank_coef_[row][bi];
        // K1 at the speed end: the clean version is silent, so it emits
        // nothing (the grain count only doubles INSIDE the crossfade). Its
        // scheduling keeps running, and it restarts with an instant-attack
        // grain under the smoothed crossfade gain.
        // Tape rate != 1: the clean stream reads at rho, starting exactly on
        // the (fractional) head, its grain clamped to the coverage span. At
        // rho == 1 the original path runs untouched (bit-identical).
        // The freeze gates its clean version the same way (always on at noon).
        if (g_c_ > VESTIGE_K1_GATE_EPS) {
          // (A stream restart after a C change always takes the stream path:
          // it needs the short matching attack.)
          if (e.frozen || (orig && !(s < VESTIGE_VOICE_SLABS && restart_[s][0]))) {
            EmitBandGrain(s, glen, posf, coef, spray_width, e.frozen, 0, 1.f, frz_rs_c ? 0.f : -1.f);
          } else {
            float atk = first_grain_[s] ? e.amt : (ver_idle_[s][0] ? 0.f : 1.f);
            const bool rs = (s < VESTIGE_VOICE_SLABS) && restart_[s][0];
            if (rs) atk = RestartAttack(glen_c);
            EmitStreamGrain(s, glen_c, head, gr, grev, coef, atk, 0, true, rs);
            first_grain_[s] = false;
            if (rs) restart_[s][0] = false;
          }
          ver_idle_[s][0] = false;
        } else {
          ver_idle_[s][0] = true;
          if (s < VESTIGE_VOICE_SLABS) restart_[s][0] = false;
        }
        int hop = (int)((float)glen_c / VESTIGE_MB_OVERLAP);
        if (hop < (int)VESTIGE_MIN_INTERVAL) hop = (int)VESTIGE_MIN_INTERVAL;
        mb_timer_[s][bi] = hop;
      }
      // ---- K1 speed version (loop side): tape-style, pitch AND time --------
      // Its own grains on its own head, read at the same rate the head moves,
      // so it is a true resample of the static loop (no multi-copy artefact).
      if (!e.frozen && !is_frip) {
        const float r = SpeedRatio();
        // Coverage clamp (the retired varispeed guard, generalised): a grain
        // reads glen*R source samples, R = K1 ratio x tape rate; keep that
        // within the loop AND the guard, so a read never passes L + min(L,
        // guard) — the extent the stage-2 guard gate already guarantees.
        size_t gsp = orig ? glen : glen_c;
        const float Rsp = r * gr;
        {
          const size_t span = (L < gcap) ? L : gcap;
          if (Rsp > 1.f && (float)gsp * Rsp > (float)span) gsp = (size_t)((float)span / Rsp);
          if (gsp < VESTIGE_GRAIN_MIN_LEN) gsp = VESTIGE_GRAIN_MIN_LEN;
          // The floor must not undo the clamp (only reachable at tape rates).
          if (Rsp > 1.f && (float)gsp * Rsp > (float)span) gsp = (size_t)((float)span / Rsp);
          if (!orig) gsp &= ~(size_t)1;                      // even: see CoverGrain
        }
        if (ver_idle_[s][1] && g_sp_ > VESTIGE_K1_GATE_EPS) mb_timer_sp_[s][bi] = 1;   // see clean version
        const float hsp = SpeedHead(s, r);
        // Half speed: the head sits on .5 every other sample and a grain starts
        // on an integer, so fire only on integer heads (the even hop then keeps
        // every later grain there too): the version is exactly on its timeline,
        // not half a sample off.
        const bool on_int = !(r < 1.f) || !orig || hsp == (float)(size_t)hsp;   // tape: grains start on the exact head instead
        if (!orig && mb_timer_sp_[s][bi] <= 1 && StreamGrains(s, 1) >= 2) mb_timer_sp_[s][bi] = 2;   // see clean stream
        if (--mb_timer_sp_[s][bi] <= 0 && !on_int) mb_timer_sp_[s][bi] = 1;   // retry next sample
        else if (mb_timer_sp_[s][bi] <= 0) {
          if (g_sp_ > VESTIGE_K1_GATE_EPS) {
            const bool rs = (s < VESTIGE_VOICE_SLABS) && restart_[s][1];
            EmitStreamGrain(s, gsp, hsp, Rsp, grev,
                            (nb == 1) ? nullptr : mb_bank_coef_[row][bi],
                            rs ? RestartAttack(gsp) : (ver_idle_[s][1] ? 0.f : 1.f), 1, !orig, rs);
            if (rs) restart_[s][1] = false;
            ver_idle_[s][1] = false;
          } else {
            ver_idle_[s][1] = true;
            if (s < VESTIGE_VOICE_SLABS) restart_[s][1] = false;   // silent version: nothing to restart
          }
          int hop = (int)((float)gsp / VESTIGE_MB_OVERLAP);
          if (r < 1.f && orig) hop &= ~1;          // even hop: every half-speed grain
                                                   // starts on the same .5 phase
          if (hop < (int)VESTIGE_MIN_INTERVAL) hop = (int)VESTIGE_MIN_INTERVAL;
          mb_timer_sp_[s][bi] = hop;
        }
      }
      // ---- K1 speed version (freeze side) -----------------------------------
      // The same band stream (band filter, scan position, spray) read at r.
      // A grain of glen samples reads glen*r of the window: EmitBandGrain clamps
      // its start to [0, L - glen*r]; a window shorter than glen*r shortens the
      // grain. Its timers run like the loop side's (it emits nothing while
      // silent, so at noon no grain and no random draw).
      if (e.frozen) {
        const float r = SpeedRatio();
        size_t gsp = glen;
        if ((float)gsp * r > (float)L) gsp = (size_t)((float)L / r);
        if (frz_rs_sp) mb_timer_sp_[s][bi] = 1;
        if (--mb_timer_sp_[s][bi] <= 0) {
          if (g_sp_ > VESTIGE_K1_GATE_EPS) {
            float span = (float)L - (float)glen - (float)scanlen; if (span < 0.f) span = 0.f;
            const float freeze_base = span * e.pos_frac;
            const float base = head + (freeze_base - head) * e.focus;
            const float posf = base + mb_scan_[s][bi] * e.focus;
            const float atk  = frz_fresh ? e.amt : (frz_rs_sp ? 0.f : 1.f);
            EmitBandGrain(s, gsp, posf, (nb == 1) ? nullptr : mb_bank_coef_[row][bi],
                          (float)VESTIGE_MB_SPRAY[row][bi] * e.focus, true, 1, r, atk);
            ver_idle_[s][1] = false;
          } else {
            ver_idle_[s][1] = true;
          }
          int hop = (int)((float)gsp / VESTIGE_MB_OVERLAP);
          if (hop < (int)VESTIGE_MIN_INTERVAL) hop = (int)VESTIGE_MIN_INTERVAL;
          mb_timer_sp_[s][bi] = hop;
        }
      }
    }
  }

  // ---- Loops follow T: A = tape (rework "already-playing loops follow T") ----
  // A loop keeps its DIVISION d (stored at activation); its target length is
  // Boundary(d, T_now). Tape plays the recorded material at rate
  //   rho = material length / target length,
  // on the head AND the grain read rate (pitch and time together), composed
  // with K1. The head integrates rho, so the loop's position in loop-fraction
  // space is continuous through any change and it stays on its own grid.
  // rho is NOT folded: the pass is exactly the target at any rate the T range
  // can ask for (up to 80x either way). Safe because the coverage clamp keeps
  // glen * R inside min(L, guard) and the guard fill outruns any reader
  // (VESTIGE_GUARD_FILL_PER_SAMPLE).
  //
  // B = stretch uses the same HEAD rate (so the same continuity, glide, grid
  // and LED), but its grains read at the K1 rate only: pitch stays, time
  // follows. Its grains never read faster for it, so it needs no folding —
  // the loop takes exactly Boundary(d, T_now) at any rate.
  double TapeTarget(int s) const {
    const int fm = follow_mode_;
    if ((fm != kFollowTape && fm != kFollowStretch) || div_[s] < 0) return 1.0;
    const size_t M  = loop_len_[s];
    const size_t Lt = GridQuantize::Boundary(div_[s], period_);
    if (Lt == 0 || Lt == M) return 1.0;                     // unchanged T: exactly 1
    double r = (double)M / (double)Lt;
    if (fm == kFollowTape) {                                 // no folding: exactly M / Lt
      if (r > (double)VESTIGE_TAPE_RATE_MAX) r = (double)VESTIGE_TAPE_RATE_MAX;   // degenerate-value guard only
      if (r < (double)VESTIGE_TAPE_RATE_MIN) r = (double)VESTIGE_TAPE_RATE_MIN;
    }
    return r;
  }
  // Audio thread, once per block: targets for every loop-side slot.
  void UpdateTapeTargets() {
    for (int s = 0; s < VESTIGE_VOICE_SLABS; s++) {
      if (!active_[s]) continue;
      rho_t_[s] = TapeTarget(s);
      RecutPlan(s);
    }
  }

  // ---- Loops follow T: C = re-cut (non-destructive, read-time view) --------
  // Speed and pitch stay at the K1 rate; the loop's LENGTH becomes
  // Boundary(d, T_now): cut at the end, or padded with silence after the
  // material (with a read-time fade-out at its end). The recorded material —
  // and the capture guard behind it — are never written again: a re-cut loop
  // is read through a GrainView (body / silence / a separate guard holding the
  // seam crossfade into the head + head continuation), so a later, longer T
  // brings the cut material back first. The new length takes effect only at a
  // wrap (OnWrap), once the spare view is fully built; grains keep the view
  // they started with. Changes that cannot apply at a wrap are counted
  // (recut_slips_*), not hidden.
  // Loop voice slots only; every other slot (freeze, archived frip) plays its
  // stored length.
  size_t PlayLen(int s) const { return (s < VESTIGE_VOICE_SLABS) ? play_len_[s] : loop_len_[s]; }
  size_t RecutTarget(int s) const {
    if (follow_mode_ != kFollowRecut || div_[s] < 0) return loop_len_[s];
    const size_t Lt = GridQuantize::Boundary(div_[s], period_);
    return (Lt == 0) ? loop_len_[s] : Lt;
  }
  bool ViewBusy(int s, int v) const {
    const int id = s * 2 + v;
    for (int g = 0; g < VESTIGE_GRAINS; g++) if (grain_view_[g] == id && grains_[g].IsActive()) return true;
    return false;
  }
  // Per block: make sure a view for the wanted length is being / has been built.
  void RecutPlan(int s) {
    const size_t want = RecutTarget(s);
    rc_want_[s] = want;
    if (want == play_len_[s] || want == loop_len_[s]) { rc_building_[s] = false; if (want == play_len_[s]) rc_ready_[s] = false; return; }
    if ((rc_building_[s] || rc_ready_[s]) && rc_le_[s] == want) return;     // on its way
    const int spare = (cur_view_[s] == 0) ? 1 : 0;
    if (ViewBusy(s, spare)) { rc_building_[s] = rc_ready_[s] = false; return; }   // old grains still read it
    // Initialise the spare view for `want`.
    const size_t M  = loop_len_[s];
    GrainView& vw = views_[s][spare];
    vw.body     = slab_[s];
    vw.loop_len = want;
    if (want < M) {                                          // cut at the end
      vw.body_end = want; vw.fade_start = want; vw.fade_k = 0.f;
    } else {                                                 // pad with silence
      size_t F = VESTIGE_SEAM_XFADE_MAX; if (F > M / 2) F = M / 2;
      vw.body_end = M; vw.fade_start = M - F; vw.fade_k = 1.f / (float)(F + 1);
    }
    vw.guard     = vestige_recut_guard[s][spare];
    // Reads reach L + min(L, guard) (coverage clamp) + the sub-sample start
    // nudge + the interpolation partner: 4 cells of margin.
    vw.guard_len = ((want < VESTIGE_GUARD_SAMPLES) ? want : VESTIGE_GUARD_SAMPLES) + 4;
    rc_view_[s] = spare; rc_le_[s] = want; rc_k_[s] = 0;
    rc_building_[s] = true; rc_ready_[s] = false;
    rc_idle_ = false;
  }
  // Guard cell k of the view being built: the seam crossfade (what would
  // follow the new end — cut material, or silence — into the head) over the
  // first xf cells, then the virtual loop's head repeated. Same curve as the
  // capture seam.
  float RecutGuardCell(int s, const GrainView& vw, size_t k) const {
    const size_t Le = vw.loop_len;
    const float head = vw.At(k % Le);
    const size_t xf = SeamXfadeLen(Le);
    if (k >= xf) return head;
    const size_t M = loop_len_[s];
    const float cont = (Le < M) ? slab_[s][Le + k] : 0.f;    // cut material (read-only) / silence
    const float t = (float)(k + 1) / (float)(xf + 1);
    return cont * cosf(t * 1.5707963f) + head * sinf(t * 1.5707963f);
  }
  void IsrRecutBuild() {
    if (rc_idle_) return;
    int budget = (int)VESTIGE_RECUT_FILL_PER_SAMPLE;
    bool any = false;
    for (int s = 0; s < VESTIGE_VOICE_SLABS && budget > 0; s++) {
      if (!rc_building_[s]) continue;
      any = true;
      const GrainView& vw = views_[s][rc_view_[s]];
      float* gbuf = vestige_recut_guard[s][rc_view_[s]];
      while (budget > 0 && rc_k_[s] < vw.guard_len) { gbuf[rc_k_[s]] = RecutGuardCell(s, vw, rc_k_[s]); rc_k_[s]++; budget--; }
      if (rc_k_[s] >= vw.guard_len) { rc_building_[s] = false; rc_ready_[s] = true; }
    }
    if (!any) rc_idle_ = true;
  }
  void SetPlayLen(int s, size_t Le) {
    play_len_[s] = Le;
    if (div_[s] >= 0) {
      const double frac = (double)GridQuantize::kDivNum[div_[s]] / (double)GridQuantize::kDivDen[div_[s]];
      beat_k_[s] = frac / (double)Le;                        // a pass is still its division of a beat
    }
  }
  // At a wrap: apply the wanted length if it is ready. Returns true if the
  // play length changed. Reasons it cannot are counted.
  bool OnWrap(int s, bool rev) {
    if (s >= VESTIGE_VOICE_SLABS) return false;
    const size_t want = rc_want_[s];
    if (want == play_len_[s]) return false;
    // A half-speed K1 version covers the loop once per TWO passes: changing
    // the length on an odd pass would jump it mid-pass.
    if (sp_rate_ < 1.f && g_sp_ > VESTIGE_K1_GATE_EPS && (pass_[s] & 1)) { recut_slip_parity_++; return false; }
    if (want == loop_len_[s]) {                              // back to the stored loop: no view
      cur_view_[s] = -1;
    } else {
      if (!(rc_ready_[s] && rc_le_[s] == want)) { recut_slip_build_++; return false; }
      cur_view_[s] = rc_view_[s]; rc_ready_[s] = false;
    }
    SetPlayLen(s, want); recut_applied_++; last_recut_at_[s] = rec_clock_now_;
    RestartStreams(s);
    (void)rev;
    return true;
  }
  // At an applied change the loop's streams restart on the new length: every
  // grain in flight fades out over the seam length (Hann fall) while the new
  // streams start THIS sample with a matching Hann attack. Across the wrap the
  // old and new grains read the same material (the head both views share), so
  // this is a crossfade of two identical signals. Without it the old, longer
  // grains hold the stream's 2-grain slots for up to their full length and the
  // new loop runs on one grain: a level dip to ~-14 dB for ~300 ms (measured).
  void RestartStreams(int s) {
    const size_t xf = VESTIGE_SEAM_XFADE_MAX;
    for (int g = 0; g < VESTIGE_GRAINS; g++)
      if (grain_slot_[g] == s && grains_[g].IsActive() && !grains_[g].FadingOut()) grains_[g].FadeOut(xf);
    for (int b = 0; b < VESTIGE_MAX_BANDS; b++) { mb_timer_[s][b] = 0; mb_timer_sp_[s][b] = 0; }
    restart_[s][0] = restart_[s][1] = true;
  }
  // Per sample: glide rho toward its target (a tap is a jump, K2 a staircase;
  // both become a smooth ~VESTIGE_TAPE_SMOOTH_MS glide). Snaps exactly onto the
  // target, so an unchanged T keeps rho == 1.0f and the original path.
  // The glide state is DOUBLE: in float the one-pole stalls ~1e-4 short of its
  // target (the step (t - r) * coef drops below half an ulp), which would leave
  // the loop a few samples per pass off Boundary(d, T) — drifting off its grid.
  double SmoothTape(int s) {
    const double t = rho_t_[s];
    double& r = rho_d_[s];
    if (r != t) {
      r += (t - r) * (double)tape_coef_;
      if (fabs(t - r) < 1e-9 * t) r = t;
      rho_s_[s] = (float)r;
    }
    return r;
  }
  // Advance slot s's clean head by one sample. At a tape rate of exactly 1 this
  // is the original float step (bit-identical). Otherwise the head integrates in
  // DOUBLE: a float head near 16000 rounds every non-integer step with a bias
  // (a few samples per pass), which would drift the loop off Boundary(d, T).
  // Every wrap is also the only place a C re-cut length can take effect
  // (OnWrap): forward subtracts the OLD length, then the new one applies;
  // backward wraps into the NEW length (reverse plays from its new end).
  void AdvanceHead(int s, bool rev, double rho) {
    size_t L = PlayLen(s);
    if (rho == 1.0 && fwd_d_[s] == (double)fwd_[s]) {
      fwd_[s] += rev ? -pitch_rate_s_ : pitch_rate_s_;
      while (fwd_[s] >= (float)L) { fwd_[s] -= (float)L; pass_[s]++; if (OnWrap(s, rev)) L = PlayLen(s); }
      while (fwd_[s] < 0.f)       { pass_[s]--; if (OnWrap(s, rev)) L = PlayLen(s); fwd_[s] += (float)L; }
      fwd_d_[s] = (double)fwd_[s];
      return;
    }
    double& h = fwd_d_[s];
    h += rev ? -(double)pitch_rate_s_ * rho : (double)pitch_rate_s_ * rho;
    while (h >= (double)L) { h -= (double)L; pass_[s]++; if (OnWrap(s, rev)) L = PlayLen(s); }
    while (h < 0.0)        { pass_[s]--; if (OnWrap(s, rev)) L = PlayLen(s); h += (double)L; }
    float f = (float)h;
    if (f >= (float)L) f = nextafterf((float)L, 0.f);      // float rounding must not reach L
    fwd_[s] = f;
  }
  // The original (integer-start) grain paths apply only while the head is on
  // an integer at rate 1 — i.e. exactly as before any T change.
  bool OrigPath(int s, double rho) const { return rho == 1.0 && fwd_[s] == floorf(fwd_[s]); }
  // Loop time in beats of T (for LED1): one pass = its division of a beat, at
  // whatever speed it is playing. Its integer crossings are the loop's actual
  // "one", however T has changed since the capture.
  void AdvanceBeat(int s, double rho) {
    if (beat_k_[s] == 0.0) return;
    beat_[s] += beat_k_[s] * rho;
    if (beat_[s] >= 1.0) beat_[s] -= 1.0;
    beat_frac_[s] = (float)beat_[s];
  }
  // Grain length so glen * R source samples stay inside min(L, guard).
  // Grains that count toward VESTIGE_MB_GRAIN_CAP: active and NOT fading out.
  // A fading grain (RestartStreams) ends within the seam length, so a restarted
  // stream takes its slots at once — the physical count is briefly cap + the
  // fading ones, for ~5 ms. With nothing fading this is the plain active count.
  int CapCount() const {
    int c = 0;
    for (int k = 0; k < VESTIGE_GRAINS; k++) if (grains_[k].IsActive() && !grains_[k].FadingOut()) c++;
    return c;
  }
  // Attack scale giving a Hann attack of the seam length (the grain's taper is
  // half its length at alpha 1): complementary to RestartStreams' fade-out.
  static float RestartAttack(size_t glen) {
    const float t = (float)VESTIGE_SEAM_XFADE_MAX / (0.5f * (float)glen);
    return (t < 1.f) ? t : 1.f;
  }
  // Active grains of one stream (slot s, version ver).
  int StreamGrains(int s, int ver) const {
    int c = 0;
    for (int g = 0; g < VESTIGE_GRAINS; g++)
      if (grain_slot_[g] == s && grain_ver_[g] == ver && grains_[g].IsActive() && !grains_[g].FadingOut()) c++;
    return c;
  }
  // EVEN, so hop = glen / 2 is exact and a stream never holds 3 grains for a
  // sample (odd lengths let the next-but-one grain start on the last sample of
  // the first — refusals at a full pool).
  static size_t CoverGrain(size_t glen, size_t L, size_t gcap, float R) {
    const size_t span = (L < gcap) ? L : gcap;
    if (R > 1.f && (float)glen * R > (float)span) glen = (size_t)((float)span / R);
    glen &= ~(size_t)1;
    if (glen < 2) glen = 2;
    return glen;
  }

  // ---- Stage 3: the TIMING error — a steady Euclidean groove inside a pass -
  // With a timing level > 0 every pass plays the BASE pattern for the level
  // (TimingBaseIndex, short-loop fallback in TimingFit) in the loop's own fixed
  // rotation (rot_seed_, drawn once when the loop starts playing); a rare pass
  // plays a one-pass variation (VESTIGE_TIMING_VAR_PROB). At each hit step the
  // READ head jumps back to the loop's start (reverse: its end) and plays on
  // from there. The TIMELINE head (fwd_) is never touched: the pass ends at its
  // normal length, the next pass starts on the loop's own "one", LED1 / K1 /
  // follow-T all see the same grid. Both jumps — the retrigger and the return
  // at the pass end — use the 5 ms stream restart.
  // Per sample: one pass compare, plus one head compare while a hit is
  // pending. The draws (rotation at loop start, variation roll per pass) use
  // their own RNG, so level 0 never draws from the shared VestigeRand sequence.
  // *rpass (optional): the pass the READ head is in — pass_ unless the offset
  // carries it across the seam (the read runs on through a pass wrap inside a
  // pattern span), so the K1 half-speed version stays continuous there too.
  float ReadHead(int s, int32_t* rpass = nullptr) const {
    if (rpass) *rpass = pass_[s];
    if (s < VESTIGE_VOICE_SLABS && lrev_[s]) {              // TIMING reverse span: the mirror
      const float Lm = (float)PlayLen(s);
      float h = lrev_m_[s] - fwd_[s];
      if (h >= Lm) h -= Lm;
      if (h < 0.f) h += Lm;
      return h;
    }
    if (s >= VESTIGE_VOICE_SLABS || trig_off_[s] == 0.f) return fwd_[s];
    const float L = (float)PlayLen(s);
    float h = trig_rev_[s] ? fwd_[s] + trig_off_[s] : fwd_[s] - trig_off_[s];
    // (A retrigger span of 1 keeps the pass counter as it was: a pattern never
    // carries the read across a wrap there, so this only changes spans > 1 —
    // and the slice mode, whose read can run over the seam inside a pass.)
    const bool carry = (rpass != nullptr) && (VESTIGE_TIMING_PATTERN_PASSES > 1 || VESTIGE_TIMING_MODE == 1);
    if (h >= L) { h -= L; if (carry) (*rpass)++; }
    if (h < 0.f) { h += L; if (carry) (*rpass)--; }
    return h;
  }
  uint32_t TimingRandU() {                   // xorshift32, audio thread only
    timing_rng_ ^= timing_rng_ << 13; timing_rng_ ^= timing_rng_ >> 17; timing_rng_ ^= timing_rng_ << 5;
    return timing_rng_;
  }
  float TimingRand() { return (float)TimingRandU() / 4294967295.f; }
  int TimingPick(int n) { int i = (int)(TimingRand() * (float)n); return i >= n ? n - 1 : i; }
  // Bjorklund's spreading of k hits over n steps (the standard recursive
  // form), rotated to start on a hit. Init only.
  static uint32_t Bjorklund(int k, int n) {
    int counts[VESTIGE_TIMING_MAX_STEPS + 2] = {0}, rem[VESTIGE_TIMING_MAX_STEPS + 2] = {0};
    int div = n - k, lv = 0; rem[0] = k;
    for (;;) {
      counts[lv] = div / rem[lv]; rem[lv + 1] = div % rem[lv]; div = rem[lv]; lv++;
      if (rem[lv] <= 1) break;
    }
    counts[lv] = div;
    int bits[VESTIGE_TIMING_MAX_STEPS]; int nb = 0;
    // Iterative expansion of build(level): an explicit stack of levels.
    int stack[256]; int sp = 0; stack[sp++] = lv;
    while (sp > 0) {
      const int l = stack[--sp];
      if (l == -1) { if (nb < VESTIGE_TIMING_MAX_STEPS) bits[nb++] = 0; continue; }
      if (l == -2) { if (nb < VESTIGE_TIMING_MAX_STEPS) bits[nb++] = 1; continue; }
      // build(l) = counts[l] x build(l-1), then build(l-2) if rem[l] != 0;
      // pushed in reverse so they pop in order.
      if (rem[l] != 0) stack[sp++] = l - 2;
      for (int i = 0; i < counts[l]; i++) stack[sp++] = l - 1;
    }
    int first = 0; while (first < nb && !bits[first]) first++;
    uint32_t m = 0;
    for (int i = 0; i < n; i++) if (bits[(first + i) % n]) m |= (1u << i);
    return m;
  }
  void TimingBuildPatterns() {
    for (int p = 0; p < VESTIGE_TIMING_PATTERNS; p++)
      timing_mask_[p] = Bjorklund(VESTIGE_TIMING_PAT_HITS[p], VESTIGE_TIMING_PAT_STEPS[p]);
  }
  // The level -> base pattern mapping (the one place): list position round(L*(N-1)).
  static int TimingBaseIndex(float level) {
    int i = (int)(level * (float)(VESTIGE_TIMING_PATTERNS - 1) + 0.5f);
    return i < 0 ? 0 : (i >= VESTIGE_TIMING_PATTERNS ? VESTIGE_TIMING_PATTERNS - 1 : i);
  }
  // Short-loop guard: the base itself if its step fits, else the densest pattern
  // below it that does; nothing below fits (a low level on a short loop): the
  // sparsest one above it that does; -1 = none fits at all. (span_out: output
  // samples per pattern span.)
  int TimingFit(int base, double span_out) const {
    const double min_step = (double)VESTIGE_TIMING_MIN_STEP_MS * 0.001 * (double)sr_;
    for (int i = base; i >= 0; i--)
      if (span_out / (double)VESTIGE_TIMING_PAT_STEPS[i] >= min_step) return i;
    for (int i = base + 1; i < VESTIGE_TIMING_PATTERNS; i++)
      if (span_out / (double)VESTIGE_TIMING_PAT_STEPS[i] >= min_step) return i;
    return -1;
  }
  // Which pass of its pattern span pass `p` is (0 = the instance's first).
  // Instances are aligned to the loop's own pass counter: forward they start
  // on multiples of the span, reverse (pass_ counts down) on span-1 mod span —
  // for a span of 2 exactly the K1 half-speed cycle in either direction.
  static int TimingSpanK(int32_t p, bool rev) {
    const int S = VESTIGE_TIMING_PATTERN_PASSES;
    const int m = (int)(((p % S) + S) % S);
    return rev ? S - 1 - m : m;
  }
  static uint32_t TimingRotate(uint32_t m, int n, int h0) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) if (m & (1u << ((i + h0) % n))) r |= (1u << i);
    return r;
  }
  // Instance start (or loop start, el0 = the elapsed SPAN it joins at) with a
  // timing level > 0: the base pattern in the loop's rotation, or — rarely,
  // never twice running — a one-instance variation; lay out its hit positions
  // in material units over the span (span x the pass's own length). Hits at or
  // before el0 are skipped.
  void TimingPlanPass(int s, bool rev, size_t L, double rho, float el0) {
    trig_cnt_[s] = 0; trig_next_[s] = 0; cur_pat_[s] = -1; cur_var_[s] = kVarNone;
    trig_L_[s] = L; trig_rev_[s] = rev;
    trig_start_[s] = rev ? pass_[s] + TimingSpanK(pass_[s], rev) : pass_[s] - TimingSpanK(pass_[s], rev);
    const float level = err_level_[kErrTiming];
    if (!(level > 0.f)) { var_last_[s] = false; return; }     // level 0: nothing drawn
    const double pass_out = (double)L / (rho > 0.0 ? rho : 1.0);   // output samples
    const int want = TimingBaseIndex(level);
    const int pat  = TimingFit(want, (double)VESTIGE_TIMING_PATTERN_PASSES * pass_out);
    if (pat < 0) { timing_skipped_++; var_last_[s] = false; return; }   // nothing fits this loop
    if (pat != want) timing_fallbacks_++;
    const int n = VESTIGE_TIMING_PAT_STEPS[pat];
    const uint32_t m = timing_mask_[pat];
    int hits[VESTIGE_TIMING_MAX_STEPS]; int nh = 0;
    for (int i = 0; i < n; i++) if (m & (1u << i)) hits[nh++] = i;
    if (nh == 0) { var_last_[s] = false; return; }            // (an edited table with k = 0)
    const int h0 = hits[rot_seed_[s] % (uint32_t)nh];          // the loop's fixed rotation
    const uint32_t base = TimingRotate(m, n, h0);
    uint32_t rm = base; int rot = h0; int var = kVarNone;
    // Variation: never on the pass right after one (that pass is the base).
    if (!var_last_[s] && TimingRand() < VESTIGE_TIMING_VAR_PROB) {
      uint32_t cand[3][VESTIGE_TIMING_MAX_STEPS]; int crot[VESTIGE_TIMING_MAX_STEPS]; int nc[3] = {0, 0, 0};
      for (int i = 1; i < n; i++) {
        if (!(base & (1u << i))) cand[0][nc[0]++] = base | (1u << i);    // add a hit
        else                     cand[1][nc[1]++] = base & ~(1u << i);   // drop a hit
      }
      for (int j = 0; j < nh; j++) {                                     // another rotation
        const uint32_t r = TimingRotate(m, n, hits[j]);
        bool dup = (r == base);
        for (int k = 0; k < nc[2] && !dup; k++) dup = (cand[2][k] == r);
        if (!dup) { crot[nc[2]] = hits[j]; cand[2][nc[2]++] = r; }
      }
      int types[3]; int nt = 0;                                  // types this pattern can do
      for (int t = 0; t < 3; t++) if (nc[t] > 0) types[nt++] = t;
      if (nt > 0) {
        const int t = types[TimingPick(nt)];
        const int k = TimingPick(nc[t]);
        rm = cand[t][k]; var = kVarAdd + t;
        if (t == 2) rot = crot[k];
        timing_vars_++;
      }
    }
    var_last_[s] = (var != kVarNone);
    // Hit positions (step 0 = the instance start: no extra restart there).
    const double span = (double)VESTIGE_TIMING_PATTERN_PASSES * (double)L;
    for (int i = 1; i < n; i++)
      if (rm & (1u << i)) trig_pos_[s][trig_cnt_[s]++] = (float)(size_t)(span * (double)i / (double)n + 0.5);
    while (trig_next_[s] < trig_cnt_[s] && trig_pos_[s][trig_next_[s]] <= el0) trig_next_[s]++;
    trig_rev_[s] = rev;
    cur_pat_[s] = pat; cur_mask_[s] = rm; cur_rot_[s] = rot; cur_var_[s] = var;
    base_mask_[s] = base; base_rot_[s] = h0; timing_patterns_++;
  }
  // ---- Timing mode 1: SLICE REARRANGEMENT ------------------------------------
  // Slice count for this pass: VESTIGE_TIMING_SLICES halved until a slice
  // (output time) is >= the minimum; 0 = none fits. *tier = its table.
  int TimingSliceN(double pass_out, int* tier) const {
    const double min_step = (double)VESTIGE_TIMING_MIN_STEP_MS * 0.001 * (double)sr_;
    int nsl = VESTIGE_TIMING_SLICES, t = 0;
    while (nsl >= 2 && t < VESTIGE_TIMING_SLICE_TIERS) {
      if (pass_out / (double)nsl >= min_step) { *tier = t; return nsl; }
      nsl /= 2; t++;
    }
    return 0;
  }
  // Loop start: this loop's arrangement material, drawn once for every slice
  // count the guard may pick — a priority order of the steps 1..N-1 and a
  // replacement slice per step (any slice but the step's own).
  void TimingDrawSlices(int s) {
    int nsl = VESTIGE_TIMING_SLICES;
    for (int t = 0; t < VESTIGE_TIMING_SLICE_TIERS && nsl >= 2; t++, nsl /= 2) {
      for (int i = 0; i < nsl - 1; i++) sl_prio_[s][t][i] = (int8_t)(i + 1);
      for (int i = nsl - 2; i > 0; i--) {                     // Fisher-Yates
        const int j = TimingPick(i + 1);
        const int8_t x = sl_prio_[s][t][i]; sl_prio_[s][t][i] = sl_prio_[s][t][j]; sl_prio_[s][t][j] = x;
      }
      for (int i = 1; i < nsl; i++) {
        int r = TimingPick(nsl - 1); if (r >= i) r++;
        sl_repl_[s][t][i] = (int8_t)r;
      }
    }
  }
  // Pass start (or loop start, el0 = the elapsed pass it joins at) with a
  // level > 0: the arrangement for the level (+ per-step random slices),
  // then its JUMPS: every step whose slice is not the natural continuation of
  // what the read plays there, with the read offset that puts the read on its
  // slice's start (elapsed units: offset = step start - slice start). Steps
  // before el0 count as played on the timeline.
  void TimingPlanSlices(int s, bool rev, size_t L, double rho, float el0) {
    trig_cnt_[s] = 0; trig_next_[s] = 0; cur_pat_[s] = -1; cur_var_[s] = kVarNone;
    trig_L_[s] = L; trig_rev_[s] = rev; trig_start_[s] = pass_[s];
    sl_n_[s] = 0; sl_rand_mask_[s] = 0;
    const float level = err_level_[kErrTiming];
    if (!(level > 0.f)) { var_last_[s] = false; return; }     // level 0: nothing drawn
    int tier = 0;
    const int nsl = TimingSliceN((double)L / (rho > 0.0 ? rho : 1.0), &tier);
    if (nsl == 0) { timing_skipped_++; var_last_[s] = false; return; }   // no slice fits this loop
    if (nsl != VESTIGE_TIMING_SLICES) timing_fallbacks_++;
    int8_t* ord = sl_order_[s];
    for (int i = 0; i < nsl; i++) ord[i] = (int8_t)i;
    int cnt = VESTIGE_TIMING_FIXED_ARRANGEMENT ? (int)(level * (float)(nsl - 1) + 0.5f) : 0;
    if (cnt > nsl - 1) cnt = nsl - 1;
    for (int k = 0; k < cnt; k++) { const int st = sl_prio_[s][tier][k]; ord[st] = sl_repl_[s][tier][st]; }
    for (int i = 0; i < nsl; i++) sl_arr_[s][i] = ord[i];
    // Randomness: each step 1..N-1, independently, with r = L^curve, plays a
    // fresh random slice other than its own for this pass only (r >= 1: every
    // step, no roll).
    const float rr = powf(level, VESTIGE_TIMING_RAND_CURVE);
    for (int st = 1; st < nsl; st++) {
      if (!(rr >= 1.f || TimingRand() < rr)) continue;
      int r = TimingPick(nsl - 1); if (r >= ord[st]) r++;
      ord[st] = (int8_t)r; sl_rand_mask_[s] |= (1u << st); timing_vars_++;
    }
    if (sl_rand_mask_[s]) cur_var_[s] = kVarRot;
    var_last_[s] = false;
    // Jumps. prev = the slice the read plays in the step before (the timeline's
    // own before el0).
    int i0 = 0;
    for (int i = 1; i < nsl; i++) if ((double)(size_t)((double)L * (double)i / (double)nsl + 0.5) <= (double)el0) i0 = i;
    for (int i = 1; i < nsl; i++) {
      if (i <= i0) continue;                                     // already behind the join point
      const int prev = (i - 1 <= i0) ? (i - 1) : ord[i - 1];
      if (ord[i] == (prev + 1) % nsl) continue;                  // natural continuation: runs on
      const float b = (float)(size_t)((double)L * (double)i / (double)nsl + 0.5);
      const float sj = (float)(size_t)((double)L * (double)ord[i] / (double)nsl + 0.5);
      trig_pos_[s][trig_cnt_[s]] = b; sl_off_[s][trig_cnt_[s]] = b - sj; trig_cnt_[s]++;
    }
    sl_n_[s] = nsl; cur_pat_[s] = 0; timing_patterns_++;
  }
  // ---- Timing mode 2: PASS MEMORY ------------------------------------------
  struct TimingFig { int8_t type, step, len, age, sub; };   // a figure on a remembered pass (len = span in
                                                        //  steps; RETRIG: its hits. age = turns played.
                                                        //  sub: REST 1 = silent, DECIMATE N = hold N)
  enum TimingFigType { kFigRest = 0, kFigStutter, kFigRepeat, kFigDouble, kFigRatchet, kFigRetrig, kFigReverse, kFigDecimate, kFigClean };
  // RETRIG with `hits`: the step it starts changing (its pattern's 2nd hit), -1 = does not fit n.
  static int TimingRetrigStep(int hits, int n) {
    if (n < 2 * hits) return -1;                            // E(2,4+) and E(3,6+) only
    const uint32_t m = Bjorklund(hits, n);
    int k = 1; while (k < n && !(m & (1u << k))) k++;
    return k < n ? k : -1;
  }
  // The steps a figure holds on an n-step pass (bit i = step i): its span
  // [step, step + len); RETRIG from its step to the end of the pass.
  static uint32_t TimingFigCover(const TimingFig& f, int n) {
    const int e = (f.type == kFigRetrig) ? n : f.step + f.len;
    uint32_t m = 0;
    for (int i = f.step; i < e && i < n; i++) m |= (1u << i);
    return m;
  }
  uint32_t TimingMemCover(int s) const {
    uint32_t m = 0;
    for (int i = 0; i < pm_nf_[s][pm_cur_[s]]; i++) m |= TimingFigCover(pm_fig_[s][pm_cur_[s]][i], pm_n_[s][pm_cur_[s]]);
    return m;
  }
  static int TimingBits(uint32_t m) { int c = 0; while (m) { c += (int)(m & 1u); m >>= 1; } return c; }
  // The lengths a type can take (RETRIG: its hit counts).
  static int TimingFigLens(int t, int* lens) {
    if (t == kFigRetrig) { lens[0] = 2; lens[1] = 3; return 2; }
    int c = 0;
    for (int l = 1; l <= VESTIGE_TIMING_SPAN_MAX; l++) lens[c++] = l;
    return c;
  }
  // Does figure (t, k, len) fit: inside the pass, on free steps, within `room`,
  // and (the ratchets) its sub-steps long enough for this loop.
  bool TimingFigFits(int t, int k, int len, int n, uint32_t used, int room, bool ok2, bool ok4, bool okr) const {
    if ((t == kFigDouble && !ok2) || (t == kFigRatchet && !ok4) || (t == kFigReverse && !okr)) return false;
    if (t == kFigRetrig) { if (k != TimingRetrigStep(len, n)) return false; }
    else if (k + len > n) return false;
    const uint32_t c = TimingFigCover(TimingFig{(int8_t)t, (int8_t)k, (int8_t)len, 0}, n);
    return !(c & used) && TimingBits(c) <= room;
  }
  static float TimingStepWeight(int k, int n) { return (2 * k >= n) ? VESTIGE_TIMING_MEM_BACK_WEIGHT : 1.f; }
  // Add one figure of at most `room` steps on the free steps: its type by
  // VESTIGE_TIMING_FIG_WEIGHT among the types that fit somewhere, then its
  // length uniformly among the lengths that fit, then its first step by step
  // weight (back half x VESTIGE_TIMING_MEM_BACK_WEIGHT). ok2 / ok4: a step
  // split in 2 / 4 is long enough for this loop (double / ratchet); okr: the
  // guard is ready, so a grain may read backward across the seam (reverse).
  bool TimingMemAdd(int s, int room, bool ok2, bool ok4, bool okr) {
    const int m = pm_cur_[s];
    const int n = pm_n_[s][m];
    if (room < 1 || pm_nf_[s][m] >= VESTIGE_TIMING_MEM_FIGS) return false;
    const uint32_t used = TimingMemCover(s);
    int lens[VESTIGE_TIMING_SPAN_MAX + 2];
    auto any = [&](int t, int len) {
      for (int k = 0; k < n; k++) if (TimingFigFits(t, k, len, n, used, room, ok2, ok4, okr)) return true;
      return false;
    };
    bool tok[VESTIGE_TIMING_FIGS]; float tot = 0.f;
    const bool rat_on = err_level_[kErrTiming] >= VESTIGE_TIMING_RATCHET_FROM;
    for (int t = 0; t < VESTIGE_TIMING_FIGS; t++) {
      tok[t] = false;
      if (!(VESTIGE_TIMING_FIG_WEIGHT[t] > 0.f)) continue;
      if (t == kFigRatchet && !rat_on) continue;
      const int nl = TimingFigLens(t, lens);
      for (int i = 0; i < nl && !tok[t]; i++) tok[t] = any(t, lens[i]);
      if (tok[t]) tot += VESTIGE_TIMING_FIG_WEIGHT[t];
    }
    if (!(tot > 0.f)) return false;
    float r = TimingRand() * tot;
    int t = -1;
    for (int i = 0; i < VESTIGE_TIMING_FIGS; i++) {
      if (!tok[i]) continue;
      t = i;
      if (r < VESTIGE_TIMING_FIG_WEIGHT[i]) break;
      r -= VESTIGE_TIMING_FIG_WEIGHT[i];
    }
    int fl[VESTIGE_TIMING_SPAN_MAX + 2]; int nf = 0;
    const int nl = TimingFigLens(t, lens);
    for (int i = 0; i < nl; i++) if (any(t, lens[i])) fl[nf++] = lens[i];
    const int len = fl[TimingPick(nf)];
    float tw = 0.f;
    for (int k = 0; k < n; k++) if (TimingFigFits(t, k, len, n, used, room, ok2, ok4, okr)) tw += TimingStepWeight(k, n);
    float q = TimingRand() * tw;
    int k = -1;
    for (int i = 0; i < n; i++) {
      if (!TimingFigFits(t, i, len, n, used, room, ok2, ok4, okr)) continue;
      k = i;
      if (q < TimingStepWeight(i, n)) break;
      q -= TimingStepWeight(i, n);
    }
    int8_t sub = 0;
    if (t == kFigRest)  sub = 1;
    if (t == kFigDecimate) sub = (int8_t)VESTIGE_TIMING_DECIM_FACTORS[TimingPick(VESTIGE_TIMING_DECIM_N)];
    pm_fig_[s][m][pm_nf_[s][m]++] = TimingFig{(int8_t)t, (int8_t)k, (int8_t)len, 0, sub};
    return true;
  }
  // A step's condition: 0 clean, 1 silent, N >= 2 sample-rate reduced (hold N).
  void TimingCond(int s, int c) {
    mute_dt_[s] = (c == 1) ? 1.f : 0.f;
    if (c >= 2) {
      if (decim_d_[s] == 0.f) decim_init_[s] = true;       // fresh: hold + filter start on the signal
      decim_n_[s] = c; decim_dt_[s] = 1.f; decim_k_[s] = -1;
      for (int k = 0; k < VESTIGE_TIMING_DECIM_N; k++) if (VESTIGE_TIMING_DECIM_FACTORS[k] == c) decim_k_[s] = k;
    } else decim_dt_[s] = 0.f;
  }
  void TimingMemClear(int s) {
    for (int m = 0; m < VESTIGE_TIMING_MEM_PASSES; m++) { pm_nf_[s][m] = 0; pm_n_[s][m] = 0; }
  }
  // Remove one figure placed on an earlier pass (age > 0); false = none.
  bool TimingMemRemove(int s) {
    int old[VESTIGE_TIMING_MEM_FIGS]; int no = 0;
    for (int i = 0; i < pm_nf_[s][pm_cur_[s]]; i++) if (pm_fig_[s][pm_cur_[s]][i].age > 0) old[no++] = i;
    if (no == 0) return false;
    const int i = old[TimingPick(no)];
    pm_fig_[s][pm_cur_[s]][i] = pm_fig_[s][pm_cur_[s]][--pm_nf_[s][pm_cur_[s]]];
    return true;
  }
  // Pass start (or loop start, el0 = the elapsed pass it joins at) with a
  // level > 0: maybe edit the remembered pass (see vestige_constants.h), then
  // render it into this pass's events — read jumps and rest mute changes
  // at step (and ratchet sub-step) boundaries, in material units.
  void TimingPlanMem(int s, bool rev, size_t L, double rho, float el0) {
    pm_cur_[s] = (pm_cur_[s] + 1) % VESTIGE_TIMING_MEM_PASSES;   // this pass's own memory (A B C A B C ...)
    trig_cnt_[s] = 0; trig_next_[s] = 0; cur_pat_[s] = -1; cur_var_[s] = kVarNone;
    trig_L_[s] = L; trig_rev_[s] = rev; trig_start_[s] = pass_[s];
    sl_n_[s] = 0; TimingCond(s, 0);                           // a pass starts clean
    const float level = err_level_[kErrTiming];
    if (!(level > 0.f)) { TimingMemClear(s); var_last_[s] = false; return; }   // level 0: memory cleared
    const double pass_out = (double)L / (rho > 0.0 ? rho : 1.0);
    int tier = 0;
    const int n = TimingSliceN(pass_out, &tier);
    if (n == 0) { timing_skipped_++; TimingMemClear(s); var_last_[s] = false; return; }
    if (n != VESTIGE_TIMING_SLICES) timing_fallbacks_++;
    if (pm_n_[s][pm_cur_[s]] != n) { pm_nf_[s][pm_cur_[s]] = 0; pm_n_[s][pm_cur_[s]] = n; }      // steps changed: the memory no longer fits
    const double min_step = (double)VESTIGE_TIMING_MIN_STEP_MS * 0.001 * (double)sr_;
    const bool ok4 = pass_out / (double)n / 4.0 >= min_step;   // a step split in 4 (ratchet) fits
    const bool ok2 = pass_out / (double)n / 2.0 >= min_step;   // split in 2 (double) fits
    const bool okr = GuardReady(s);                             // reverse reads need the guard
    // 1. Age: a figure that has played its VESTIGE_TIMING_MEM_LIFE passes goes.
    for (int i = 0; i < pm_nf_[s][pm_cur_[s]];) {
      if (++pm_fig_[s][pm_cur_[s]][i].age >= VESTIGE_TIMING_MEM_LIFE) pm_fig_[s][pm_cur_[s]][i] = pm_fig_[s][pm_cur_[s]][--pm_nf_[s][pm_cur_[s]]];
      else i++;
    }
    // 2. The edits (at least one every pass).
    int edits = (int)(VESTIGE_TIMING_MEM_EDITS_B * level * (float)n + 0.5f);
    if (edits < 1) edits = 1;
    const float D = VESTIGE_TIMING_MEM_TARGET_A + VESTIGE_TIMING_MEM_TARGET_B * level;
    for (int e = 0; e < edits; e++) {
      int tgt = (int)D;
      if (TimingRand() < D - (float)tgt) tgt++;
      if (tgt > n) tgt = n;
      // The target counts FIGURES, whatever their span (a span only needs free steps).
      const int c = pm_nf_[s][pm_cur_[s]];
      if (c > tgt)      TimingMemRemove(s);
      else if (c < tgt && TimingMemAdd(s, n, ok2, ok4, okr)) {}
      else if (TimingMemRemove(s))                             // swap (an earlier figure) — also
        TimingMemAdd(s, n, ok2, ok4, okr);                          //  when the pass is too full to add
      timing_edits_++;
    }
    // Render: which slice each step plays, muted or not, ratchet division.
    int8_t play[VESTIGE_TIMING_SLICE_MAX], mute[VESTIGE_TIMING_SLICE_MAX], rat[VESTIGE_TIMING_SLICE_MAX];
    int8_t rvk[VESTIGE_TIMING_SLICE_MAX], rve[VESTIGE_TIMING_SLICE_MAX];   // REVERSE span [rvk, rve) per step (-1 none)
    for (int i = 0; i < n; i++) { play[i] = (int8_t)i; mute[i] = 0; rat[i] = 1; rvk[i] = rve[i] = -1; }
    for (int f = 0; f < pm_nf_[s][pm_cur_[s]]; f++) {
      const TimingFig& g = pm_fig_[s][pm_cur_[s]][f];
      const int k = g.step, e = (k + g.len < n) ? k + g.len : n;
      // (Steps before step 1 are the loop's last steps.)
      switch (g.type) {
        case kFigRest:
        case kFigDecimate:   for (int i = k; i < e; i++) mute[i] = g.sub; break;   // 1 silent, N hold N
        case kFigStutter: for (int i = k; i < e; i++) play[i] = (int8_t)((i - g.len + n) % n); break;   // the len steps before, again
        case kFigRepeat:  for (int i = k; i < e; i++) play[i] = (int8_t)((k - 1 + n) % n); break;       // the step before, len times
        case kFigDouble:  for (int i = k; i < e; i++) rat[i] = 2; break;
        case kFigRatchet: for (int i = k; i < e; i++) rat[i] = 4; break;
        case kFigReverse: for (int i = k; i < e; i++) { rvk[i] = (int8_t)k; rve[i] = (int8_t)e; } break;
        case kFigRetrig: {
          const uint32_t m = Bjorklund(g.len, n);
          int last = 0;
          for (int i = 0; i < n; i++) { if (m & (1u << i)) last = i; if (i >= k) play[i] = (int8_t)(i - last); }
        } break;
        default: break;                                      // CLEAN: plays as recorded
      }
    }
    for (int i = 0; i < n; i++) sl_order_[s][i] = (mute[i] == 1) ? (int8_t)-1 : play[i];
    auto bnd = [&](int i) { return (float)(size_t)((double)L * (double)i / (double)n + 0.5); };
    // REVERSE: the read mirrors the timeline over the span, h = M - fwd_ (see
    // ReadHead): forward M = b0 + b1 - 1 (read from the span's end down to its
    // start); a reversed loop M = 2L - 1 - b0 - b1 (read up through it).
    auto mirror = [&](int k, int e) {
      const float b0 = bnd(k), b1 = (e < n) ? bnd(e) : (float)L;
      return rev ? 2.f * (float)L - 1.f - b0 - b1 : b0 + b1 - 1.f;
    };
    // Step 1 at the pass start itself: its mute and its slice apply right here
    // (the read was just put back on the timeline by TimingStep).
    if (!(el0 > 0.f)) {
      TimingCond(s, mute[0]);
      if (rvk[0] >= 0) { lrev_[s] = true; lrev_m_[s] = mirror(rvk[0], rve[0]); RestartStreams(s); timing_trigs_++; }
      else {
        const float off0 = 0.f - bnd(play[0]);
        if (off0 != trig_off_[s]) { trig_off_[s] = off0; RestartStreams(s); timing_trigs_++; }
      }
    }
    for (int i = 0; i < n; i++) {
      const float b0 = bnd(i), b1 = (i + 1 < n) ? bnd(i + 1) : (float)L;
      const float sj = bnd(play[i]);
      for (int j = 0; j < rat[i]; j++) {
        const float at = (j == 0) ? b0 : (float)(size_t)(b0 + (b1 - b0) * (float)j / (float)rat[i] + 0.5f);
        bool jump = false; int8_t mc = -1, rv = -1;
        if (j > 0) jump = true;
        else if (i > 0) {
          if (rvk[i] >= 0 && rvk[i] != rvk[i - 1]) rv = 1;           // a reverse span starts
          else if (rvk[i] < 0 && rvk[i - 1] >= 0) rv = 0;            // one ends: back to reading forward
          else if (rvk[i] < 0) jump = (play[i] != play[i - 1] + 1) || rat[i - 1] > 1;
          if (mute[i] != mute[i - 1]) mc = mute[i];
        }
        if (!jump && mc < 0 && rv < 0) continue;
        if (at <= el0) continue;                               // behind the join point
        if (trig_cnt_[s] >= kTimingEvents) break;
        const int e = trig_cnt_[s]++;
        trig_pos_[s][e] = at; sl_off_[s][e] = (rv == 1) ? mirror(rvk[i], rve[i]) : at - sj;
        pm_jump_ev_[s][e] = jump; pm_mute_ev_[s][e] = mc; pm_rev_ev_[s][e] = rv;
      }
    }
    sl_n_[s] = n; cur_pat_[s] = 0; timing_patterns_++;
    if (pm_nf_[s][pm_cur_[s]] > 0) { cur_var_[s] = kVarRot; timing_vars_++; }
    var_last_[s] = false;
  }
  // ---- Timing mode 3: LAYERS -------------------------------------------------
  // Three rhythm lines (TIMING / CONDITION / PLAYBACK) per loop voice, each a
  // list of hits on a line of ln_cells_ cells (G steps x the passes per line).
  struct LineHit { int8_t start, len, type, sub, age; };
  static constexpr int kLineMaxHits = VESTIGE_TIMING_LINE_MAX_CELLS / 2;
  static uint64_t LineMask(int start, int len) { return ((1ull << len) - 1ull) << start; }   // len <= 4
  // Cells a new hit may not touch: every other hit plus one pause cell on each
  // side (cyclic: the line loops). skip = a hit to leave out (-1 none).
  uint64_t LineBlocked(int s, int l, int skip) const {
    const int C = ln_cells_[s];
    uint64_t b = 0;
    for (int h = 0; h < ln_nh_[s][l]; h++) {
      if (h == skip) continue;
      const LineHit& x = ln_hit_[s][l][h];
      b |= LineMask(x.start, x.len);
      const int lo = x.start > 0 ? x.start - 1 : C - 1, hi = x.start + x.len < C ? x.start + x.len : 0;   // cyclic neighbours
      b |= (1ull << lo) | (1ull << hi);
    }
    return b;
  }
  // Fits against a precomputed LineBlocked() mask (the placement searches
  // compute it once, not once per candidate cell: that was the CPU burst).
  bool LineFitsIn(int s, int start, int len, uint64_t blocked) const {
    const int C = ln_cells_[s];
    if (start < 0 || len < 1 || start + len > C || len > VESTIGE_TIMING_SPAN_MAX) return false;
    return (LineMask(start, len) & blocked) == 0;
  }
  bool LineFits(int s, int l, int start, int len, int skip) const {
    return LineFitsIn(s, start, len, LineBlocked(s, l, skip));
  }
  // A type for a new hit on layer l (weights; RATCHET only from its level).
  int LineDrawType(int l, int8_t* sub) {
    *sub = 0;
    if (l == kErrPlayback) return kFigReverse;
    if (l == kErrCondition) {
      const float t = VESTIGE_TIMING_W_REST + VESTIGE_TIMING_W_DECIM;
      if (TimingRand() * t < VESTIGE_TIMING_W_REST) { *sub = 1; return kFigRest; }
      *sub = (int8_t)VESTIGE_TIMING_DECIM_FACTORS[TimingPick(VESTIGE_TIMING_DECIM_N)];
      return kFigDecimate;
    }
    const bool rat = err_level_[kErrTiming] >= VESTIGE_TIMING_RATCHET_FROM;
    const int   ty[5] = {kFigStutter, kFigRepeat, kFigDouble, kFigRatchet, kFigRetrig};
    const float w[5]  = {VESTIGE_TIMING_W_STUTTER, VESTIGE_TIMING_W_REPEAT, VESTIGE_TIMING_W_DOUBLE,
                         rat ? VESTIGE_TIMING_W_RATCHET : 0.f, VESTIGE_TIMING_W_RETRIG};
    float tot = 0.f; for (int i = 0; i < 5; i++) tot += w[i];
    if (!(tot > 0.f)) return kFigClean;                   // (only RATCHET on, below its level: a silent hit)
    float r = TimingRand() * tot; int last = kFigClean;
    for (int i = 0; i < 5; i++) { if (!(w[i] > 0.f)) continue; last = ty[i]; if (r < w[i]) return ty[i]; r -= w[i]; }
    return last;
  }
  void LineRetype(int l, LineHit& h) {
    int8_t sub = 0; int t = h.type;
    for (int tries = 0; tries < 4 && t == h.type; tries++) t = LineDrawType(l, &sub);
    if (t == h.type) t = LineDrawType(l, &sub);            // (a one-type layer keeps its type)
    h.type = (int8_t)t; h.sub = sub; h.age = 0;
  }
  // Grow hit h cell by cell with chance fill (up to VESTIGE_TIMING_SPAN_MAX).
  void LineGrow(int s, int l, int h, float fill) {
    LineHit& x = ln_hit_[s][l][h];
    const uint64_t blocked = LineBlocked(s, l, h);          // the other hits do not move while it grows
    while (x.len < VESTIGE_TIMING_SPAN_MAX && TimingRand() < fill && LineFitsIn(s, x.start, x.len + 1, blocked)) x.len++;
  }
  bool LineAdd(int s, int l, int start, float fill) {
    if (ln_nh_[s][l] >= kLineMaxHits || !LineFits(s, l, start, 1, -1)) return false;
    LineHit& x = ln_hit_[s][l][ln_nh_[s][l]++];
    x.start = (int8_t)start; x.len = 1; x.age = 0;
    int8_t sub; x.type = (int8_t)LineDrawType(l, &sub); x.sub = sub;
    LineGrow(s, l, ln_nh_[s][l] - 1, fill);
    return true;
  }
  // A free cell for a new hit: a pulse (even) cell, or any cell if !pulse;
  // -1 = none.
  int LineFreeCell(int s, int l, bool pulse) {
    const int C = ln_cells_[s];
    int c[VESTIGE_TIMING_LINE_MAX_CELLS]; int nc = 0;
    const uint64_t blocked = LineBlocked(s, l, -1);
    for (int i = 0; i < C; i += (pulse ? 2 : 1)) if (!(blocked & (1ull << i))) c[nc++] = i;
    if (nc == 0) return -1;
    return c[TimingPick(nc)];
  }
  float LineTarget(int s, float level) const {
    return (VESTIGE_TIMING_LINE_HITS_A + VESTIGE_TIMING_LINE_HITS_B * level) * (float)ln_cells_[s] / 16.f;
  }
  int LineDrawTarget(int s, float level) {
    const float D = LineTarget(s, level);
    int t = (int)D; if (TimingRand() < D - (float)t) t++;
    return t;
  }
  void LineSeed(int s, int l, float level) {
    const int C = ln_cells_[s];
    ln_nh_[s][l] = 0;
    const int P = C / 2;                                    // pulse cells
    if (P < 1) return;
    int k = (int)(LineTarget(s, level) + 0.5f); if (k < 1) k = 1; if (k > P) k = P;
    const int rot = TimingPick(P);
    const float fill = VESTIGE_TIMING_LINE_FILL_A + VESTIGE_TIMING_LINE_FILL_B * level;
    // Evenly spread (Euclidean) on distinct even cells: always free, always a
    // pause apart, so no search. Computed directly, in O(k) — the old search
    // per hit per cell (LineAdd / LineGrow) was a CPU spike at every new loop.
    // Same choices from the same random numbers as that search: per hit its
    // type draw and the fill-0 grow draw, then the grows in insertion order.
    LineHit* hit = ln_hit_[s][l];
    for (int i = 0; i < k; i++) {
      LineHit& x = hit[i];
      x.start = (int8_t)(2 * (((i * P) / k + rot) % P)); x.len = 1; x.age = 0;
      int8_t sub; x.type = (int8_t)LineDrawType(l, &sub); x.sub = sub;
      (void)TimingRand();                                   // (the fill-0 grow's draw)
    }
    ln_nh_[s][l] = k;
    // A hit grows right up to one pause before its successor (the starts are a
    // cyclic rotation of an increasing run): the last one before the wrap stops
    // at the line end, or one cell earlier when the first hit sits on cell 0.
    for (int i = 0; i < k; i++) {
      LineHit& x = hit[i];
      int limit = C;                                        // exclusive end
      if (k > 1) {
        const int nx = hit[(i + 1) % k].start;
        limit = (nx > x.start) ? nx - 1 : (nx == 0 ? C - 1 : C);
      }
      while (x.len < VESTIGE_TIMING_SPAN_MAX && TimingRand() < fill && x.start + x.len + 1 <= limit) x.len++;
    }
  }
  static int LineOps(float level) { return 1 + (int)(VESTIGE_TIMING_LINE_OPS_B * level + 0.5f); }
  // `ops` changes to layer l's line (see vestige_constants.h).
  void LineMutate(int s, int l, float level, int ops) {
    const int C = ln_cells_[s];
    const float fill = VESTIGE_TIMING_LINE_FILL_A + VESTIGE_TIMING_LINE_FILL_B * level;
    float rnd = (level - VESTIGE_TIMING_LINE_RAND_FROM) / (1.f - VESTIGE_TIMING_LINE_RAND_FROM);
    if (rnd < 0.f) rnd = 0.f;
    for (int o = 0; o < ops; o++) {
      int& nh = ln_nh_[s][l];
      const int tgt = LineDrawTarget(s, level);
      if (rnd > 0.f && nh > 0 && TimingRand() < rnd) {          // break-up: a random hit anywhere
        const int h = TimingPick(nh);
        ln_hit_[s][l][h] = ln_hit_[s][l][--nh];
        const int c = LineFreeCell(s, l, false);
        if (c >= 0) LineAdd(s, l, c, fill);
        continue;
      }
      if (nh < tgt) {
        const int c = LineFreeCell(s, l, true);
        if (c >= 0) { LineAdd(s, l, c, fill); continue; }
      } else if (nh > tgt && nh > 0) {
        const int h = TimingPick(nh);
        ln_hit_[s][l][h] = ln_hit_[s][l][--nh];
        continue;
      }
      if (nh == 0) continue;
      const int h = TimingPick(nh);
      LineHit& x = ln_hit_[s][l][h];
      switch (TimingPick(2)) {
        case 0: {                                               // shift by one cell
          const int d = TimingPick(2) ? 1 : -1;
          const int ns = (x.start + d + C) % C;
          if (LineFits(s, l, ns, x.len, h)) x.start = (int8_t)ns;
        } break;
        default: {                                              // grow / shrink by one cell
          if (TimingPick(2) && LineFits(s, l, x.start, x.len + 1, h)) x.len++;
          else if (x.len > 1) x.len--;
        } break;
      }
    }
  }
  // A layer with all its type weights at 0 is off (RATCHET counts as on: its
  // level gate only delays it).
  static bool LayerOn(int l) {
    if (l == kErrPlayback)  return VESTIGE_TIMING_W_REVERSE > 0.f;
    if (l == kErrCondition) return VESTIGE_TIMING_W_REST + VESTIGE_TIMING_W_DECIM > 0.f;
    return VESTIGE_TIMING_W_STUTTER + VESTIGE_TIMING_W_REPEAT + VESTIGE_TIMING_W_DOUBLE +
           VESTIGE_TIMING_W_RATCHET + VESTIGE_TIMING_W_RETRIG > 0.f;
  }
  void LineClear(int s) { for (int l = 0; l < kErrTypes; l++) { ln_nh_[s][l] = 0; ln_ops_[s][l] = 0; } ln_cells_[s] = 0; }
  // Once per audio block: one pending change per layer per loop voice. A pass
  // start only renders; the changes for the NEXT pass are spread over the
  // blocks after it (at once they were a CPU burst of up to 5 blocks — clicks).
  // A pending SEED (ln_ops_ == -1: a new loop, or K3 up from 0) is built here
  // too, at most one layer per block, so that is no spike either; that line
  // then plays from the next pass on.
  void TimingLayerTick() {
    bool seeded = false;
    for (int s = 0; s < VESTIGE_VOICE_SLABS; s++)
      for (int l = 0; l < kErrTypes; l++) {
        const float lv = err_level_[l];
        if (ln_ops_[s][l] < 0) {
          if (seeded) continue;
          ln_ops_[s][l] = 0;
          if (lv > 0.f && ln_cells_[s] > 0 && LayerOn(l)) { LineSeed(s, l, lv); seeded = true; }
          continue;
        }
        if (ln_ops_[s][l] == 0) continue;
        ln_ops_[s][l]--;
        if (lv > 0.f && ln_nh_[s][l] > 0 && LayerOn(l)) LineMutate(s, l, lv, 1);
      }
  }
  // Pass start (or loop start, el0 = the elapsed pass it joins at): evolve
  // each live layer, then render this pass's segment of the three lines into
  // per-step (source step, direction, ratchet, condition) and from those the
  // pass's events.
  // ---- K3 mode 1 (both halves): the RHYTHM --------------------------------
  // Three engines, VESTIGE_TIMING_RHY_ENGINE (vestige_constants.h): 0 the
  // POLYMETRIC Euclidean engine, 1 the curated AFRO TABLE, 2 the INTERLOCK
  // TABLE (default; 1 and 2 share the table machinery, VESTIGE_TIMING_RHY_TABLE
  // = the selected one). All write this pass's segment of the TIMING
  // (stutters) and CONDITION (rests, decimates) lines, so the render stacks
  // them as usual; all share the one-added-stutter variation; engines 0 + 1
  // share the off-beat decimate colour, engine 2's decimates are its rows'
  // OVERLAP masks (dec). Engine 0's rotations are drawn per loop under rules
  // (stutter on the 1, audible rests; RHY_ROT_RANDOM), the tables' are fixed.
  // Engine 2 has two tables, one per K3 half (rhy_side_: kRhyCcw traditional,
  // kRhyCw academic); they share the planner and the running step count.
  void TimingPlanRhythm(int s, int G, int seg0) {
    if (VESTIGE_TIMING_RHY_ENGINE == 0) TimingPlanRhythmPoly(s, G, seg0);
    else                                TimingPlanRhythmTable(s, G, seg0);
  }
  static bool RhyHit(int i, int k, int n, int rot) { return k > 0 && n > 0 && (((i + rot) % n) * k) % n < k; }
  // ---- engine 0: POLYMETRIC ------------------------------------------------
  // Two Euclidean cycles that do NOT fit the bar, on the voice's RUNNING step
  // count (it never resets per pass or per line, so the rhythm rolls across
  // bars): STUTTERS on a RHY_S_CYCLE-step cycle (k = S_K_MIN .. S_K_MAX with
  // the depth: E(2,12) .. the E(7,12) bell), RESTS on a RHY_R_CYCLE-step cycle
  // (R_K_MIN .. R_K_MAX: E(1,8) .. E(3,8) tresillo); a stutter wins where both land. The
  // rotations are FIXED constants (RHY_S_ROT / RHY_R_ROT / RHY_D_ROT), the same
  // for every loop — never random. Every hit is one step. With chance
  // RHY_VAR_PROB per ~32 steps a pass gets one VARIATION (one added stutter),
  // for that pass only.
  // Hit counts for depth u (the same rounding the planner uses).
  // CW half (side kRhyCw): the same engine on the RHY_CW_* cycles (15 : 10).
  static int RhyPolyNs(int side) { return side == kRhyCw ? VESTIGE_TIMING_RHY_CW_S_CYCLE : VESTIGE_TIMING_RHY_S_CYCLE; }
  static int RhyPolyNr(int side) { return side == kRhyCw ? VESTIGE_TIMING_RHY_CW_R_CYCLE : VESTIGE_TIMING_RHY_R_CYCLE; }
  static int RhyPolyKs(float u, int side = kRhyCcw) {
    if (side == kRhyCw) return (int)(VESTIGE_TIMING_RHY_CW_S_K_MIN + (VESTIGE_TIMING_RHY_CW_S_K_MAX - VESTIGE_TIMING_RHY_CW_S_K_MIN) * u + 0.5f);
    return (int)(VESTIGE_TIMING_RHY_S_K_MIN + (VESTIGE_TIMING_RHY_S_K_MAX - VESTIGE_TIMING_RHY_S_K_MIN) * u + 0.5f);
  }
  static int RhyPolyKr(float u, int side = kRhyCcw) {
    if (side == kRhyCw) return (int)(VESTIGE_TIMING_RHY_CW_R_K_MIN + (VESTIGE_TIMING_RHY_CW_R_K_MAX - VESTIGE_TIMING_RHY_CW_R_K_MIN) * u + 0.5f);
    const int kr = (int)(VESTIGE_TIMING_RHY_R_K_MIN + (VESTIGE_TIMING_RHY_R_K_MAX - VESTIGE_TIMING_RHY_R_K_MIN) * u + 0.5f);
    return (u < VESTIGE_TIMING_RHY_HALF_U && kr < VESTIGE_TIMING_RHY_HALF_KR_MIN_CCW) ? VESTIGE_TIMING_RHY_HALF_KR_MIN_CCW : kr;   // (CCW half time)
  }
  // Off-beat decimate density (engines 0 + 1) from the depth; engine 2 has no
  // off-beat decimate (0: its decimates are the rows' overlap masks).
  static int RhyKd(float u) {
    if (VESTIGE_TIMING_RHY_ENGINE == 2) return 0;
    return (int)(VESTIGE_TIMING_RHY_D_K_MIN + (VESTIGE_TIMING_RHY_D_K_MAX - VESTIGE_TIMING_RHY_D_K_MIN) * u + 0.5f);
  }
  // The rhythm's step scale at depth u: 2 = half time (u < HALF_U), else 1.
  static float RhyTempoStep(float u) { return (u < VESTIGE_TIMING_RHY_HALF_U) ? 2.f : 1.f; }
  static const char* RhyTempoName(float u) { return RhyTempoStep(u) > 1.f ? "half" : "1x"; }
  // The ratchet ramp at depth u: 0 below RAT_U0 .. 1 at full.
  static float RhyRatRamp(float u) {
    return u <= VESTIGE_TIMING_RHY_RAT_U0 ? 0.f : fminf(1.f, (u - VESTIGE_TIMING_RHY_RAT_U0) / (1.f - VESTIGE_TIMING_RHY_RAT_U0));
  }
  // A loop's drawn rotation word r on an n-step cycle: 0 .. n-1, or with
  // RHY_ROT_EVEN only the even ones (0, 2, 4, ...).
  static uint32_t RhyRotOf(uint32_t r, int n) {
    if (!VESTIGE_TIMING_RHY_ROT_EVEN) return r % (uint32_t)n;
    return 2u * (r % (uint32_t)((n + 1) / 2));
  }
  // ON THE 1: the loop's stutter word r picks one of the rotations of
  // E(ks, Ns) that hit step 0 (there are ks of them).
  static uint32_t RhyOn1Rot(uint32_t r, float u, int side) {
    const int ks = RhyPolyKs(u, side), Ns = RhyPolyNs(side);
    int m = 0;
    for (int x = 0; x < Ns; x++) m += RhyHit(0, ks, Ns, x) ? 1 : 0;
    if (m <= 0) return 0u;
    int want = (int)(r % (uint32_t)m);
    for (int x = 0; x < Ns; x++) if (RhyHit(0, ks, Ns, x) && want-- == 0) return (uint32_t)x;
    return 0u;
  }
  // AUDIBLE RESTS (RHY_AUDIBLE_RESTS): the loop's rest word r picks one of
  // the candidate rest rotations for depth u and stutter rotation srot.
  static uint32_t RhyRestRot(uint32_t r, float u, uint32_t srot, int side) {
    const int ks = RhyPolyKs(u, side), kr = RhyPolyKr(u, side), Ns = RhyPolyNs(side), Nr = RhyPolyNr(side);
    int g = Ns, h = Nr; while (h) { const int x = g % h; g = h; h = x; }
    const int L = Ns / g * Nr;
    int aud[16] = {}, tot = 0, best = 0;
    for (int x = 0; x < Nr && x < 16; x++) {
      int n = 0, a = 0;
      for (int t = 0; t < L; t++)
        if (RhyHit(t % Nr, kr, Nr, x)) { n++; if (!RhyHit(t % Ns, ks, Ns, (int)(srot % (uint32_t)Ns))) a++; }
      aud[x] = a; tot = n; if (a > best) best = a;
    }
    const int nx = Nr < 16 ? Nr : 16;
    uint16_t cand = 0;
    for (int x = 0; x < nx; x += 2) if (2 * aud[x] >= tot && aud[x] > 0) cand |= (uint16_t)(1u << x);   // even, >= half
    if (!cand) for (int x = 0; x < nx; x++) if (2 * aud[x] >= tot && aud[x] > 0) cand |= (uint16_t)(1u << x);   // any, >= half
    if (!cand) for (int x = 0; x < nx; x += 2) if (aud[x] == best && best > 0) cand |= (uint16_t)(1u << x);   // most, even
    if (!cand) for (int x = 0; x < nx; x++) if (aud[x] == best) cand |= (uint16_t)(1u << x);   // most, any
    int m = 0; for (int x = 0; x < nx; x++) m += (cand >> x) & 1u;
    int want = (int)(r % (uint32_t)(m > 0 ? m : 1));
    for (int x = 0; x < nx; x++) if (((cand >> x) & 1u) && want-- == 0) return (uint32_t)x;
    return 0u;
  }
  // NO FLAMS: from the drawn rest rotation rrot on, the first allowed one
  // (RhyRotOf's set) whose rests never sit one step from a stutter over the
  // whole lcm(Ns, Nr) period (else the fewest such rests); srot already folded.
  static uint32_t RhyNoFlamRot(float u, uint32_t srot, uint32_t rrot, int side) {
    const int ks = RhyPolyKs(u, side), kr = RhyPolyKr(u, side), Ns = RhyPolyNs(side), Nr = RhyPolyNr(side);
    const int step = VESTIGE_TIMING_RHY_ROT_EVEN ? 2 : 1, nc = (Nr + step - 1) / step;
    int g = Ns, h = Nr; while (h) { const int x = g % h; g = h; h = x; }
    const int L = Ns / g * Nr;
    auto S = [&](int t) { return RhyHit(((t % Ns) + Ns) % Ns, ks, Ns, (int)(srot % (uint32_t)Ns)); };
    const int j0 = (int)(rrot / (uint32_t)step) % nc;
    uint32_t best = rrot; int best_n = 1 << 30;
    for (int j = 0; j < nc; j++) {
      const int r = ((j0 + j) % nc) * step;
      int n = 0;
      for (int t = 0; t < L && n < best_n; t++)
        if (RhyHit(t % Nr, kr, Nr, r) && !S(t) && (S(t - 1) || S(t + 1))) n++;
      if (n < best_n) { best_n = n; best = (uint32_t)r; if (n == 0) break; }
    }
    return best;
  }
  // Engine 0's fixed rotations, folded into their cycles.
  static_assert(VESTIGE_TIMING_RHY_S_ROT >= 0 && VESTIGE_TIMING_RHY_R_ROT >= 0 && VESTIGE_TIMING_RHY_D_ROT >= 0,
                "rhythm rotations are >= 0");
  static constexpr int kRhySRot = VESTIGE_TIMING_RHY_S_ROT % VESTIGE_TIMING_RHY_S_CYCLE;
  static constexpr int kRhyRRot = VESTIGE_TIMING_RHY_R_ROT % VESTIGE_TIMING_RHY_R_CYCLE;
  static constexpr int kRhyDRot = VESTIGE_TIMING_RHY_D_ROT % VESTIGE_TIMING_RHY_D_SLOTS;
  // The base cell (0 pause, 1 stutter, 2 rest) at running step t, depth u.
  // dks / dkr: a hit variation's change of the stutters' / rests' k (clamped).
  static int RhyPolyBase(int32_t t, float u, uint32_t srot, uint32_t rrot, int side = kRhyCcw, int dks = 0, int dkr = 0) {
    const int Ns = RhyPolyNs(side), Nr = RhyPolyNr(side);
    int ks = RhyPolyKs(u, side) + dks, kr = RhyPolyKr(u, side) + dkr;
    ks = ks < 0 ? 0 : ks > Ns ? Ns : ks;
    kr = kr < 0 ? 0 : kr > Nr ? Nr : kr;
    const int ts = (int)(((t % Ns) + Ns) % Ns), tr = (int)(((t % Nr) + Nr) % Nr);
    if (RhyHit(ts, ks, Ns, (int)(srot % (uint32_t)Ns))) return 1;   // a stutter wins where both land
    if (RhyHit(tr, kr, Nr, (int)(rrot % (uint32_t)Nr))) return 2;
    return 0;
  }
  // HIT VARIATION (RHY_KVAR) at running step t of loop s: schedules / ends
  // the one-cycle k +-1 of one voice; dks / dkr = its change at t.
  // The next varied run from step t on: a voice v (random), then its
  // EVERY_MIN-th .. EVERY_MAX-th whole cycle from t (that many runs of it,
  // the last one varied).
  void RhyKVarPlan(int s, int32_t t, int side) {
    const int v = TimingPick(2);                            // 0 stutters, 1 rests
    const int N = v ? RhyPolyNr(side) : RhyPolyNs(side);
    const int e = VESTIGE_TIMING_RHY_KVAR_EVERY_MIN + TimingPick(VESTIGE_TIMING_RHY_KVAR_EVERY_MAX - VESTIGE_TIMING_RHY_KVAR_EVERY_MIN + 1);
    rkv_voice_[s] = (int8_t)v;
    rkv_t0_[s] = (t + N - 1) / N * N + (e - 1) * N; rkv_t1_[s] = rkv_t0_[s] + N;
    rkv_dk_[s] = 0;                                         // (its +-1: drawn when it starts)
  }
  // Audible rests of the run [t0, t1) with the rests' k changed by dkr.
  int RhyKVarAudible(int32_t t0, int32_t t1, uint32_t srot, uint32_t rrot, int side, int dkr) const {
    int a = 0;
    for (int32_t t = t0; t < t1; t++) a += RhyPolyBase(t, rhy_level_, srot, rrot, side, 0, dkr) == 2 ? 1 : 0;
    return a;
  }
  void RhyKVarAt(int s, int32_t t, int side, uint32_t srot, uint32_t rrot, int& dks, int& dkr) {
    if (rkv_next_[s] < 0) { RhyKVarPlan(s, t, side); rkv_next_[s] = 0; }   // a new loop: its first plan
    if (t >= rkv_t1_[s]) RhyKVarPlan(s, rkv_t1_[s], side);   // the varied run is over: plan the next
    if (t >= rkv_t0_[s] && rkv_dk_[s] == 0) {               // it starts: one hit more or less
      const int ks = RhyPolyKs(rhy_level_, side), Ns = RhyPolyNs(side);
      const int kr = RhyPolyKr(rhy_level_, side), Nr = RhyPolyNr(side);
      const int first = TimingPick(2) ? 1 : -1;
      // Stutters: k +-1 only if the varied cycle still hits its 1 (the run is
      // one whole stutter cycle). Rests: must not lose them to the stutters —
      // +1 keeps at least the base's audible rests, -1 at most one fewer (>= 1).
      auto stut_ok = [&](int dd) {
        return ks + dd >= 1 && ks + dd <= Ns - 1 && RhyHit(0, ks + dd, Ns, (int)(srot % (uint32_t)Ns));
      };
      auto rest_ok = [&](int dd) {
        const int kr_min = (side == kRhyCcw && RhyTempoStep(rhy_level_) > 1.f) ? VESTIGE_TIMING_RHY_HALF_KR_MIN_CCW : 1;
        if (kr + dd < kr_min || kr + dd > Nr - 1) return false;   // (CCW half time: never below E(2,8))
        const int base = RhyKVarAudible(rkv_t0_[s], rkv_t1_[s], srot, rrot, side, 0);
        const int a = RhyKVarAudible(rkv_t0_[s], rkv_t1_[s], srot, rrot, side, dd);
        return a >= 1 && a >= base - (dd < 0 ? 1 : 0);
      };
      auto pick = [&](int v) {
        if (v == 1) return rest_ok(first) ? first : rest_ok(-first) ? -first : 0;
        return stut_ok(first) ? first : stut_ok(-first) ? -first : 0;
      };
      int d = pick(rkv_voice_[s]);
      if (d == 0) {                                         // the other voice, on its own next whole cycle
        const int v2 = 1 - rkv_voice_[s], N2 = v2 ? Nr : Ns;
        rkv_voice_[s] = (int8_t)v2; rkv_t0_[s] = (t + N2 - 1) / N2 * N2; rkv_t1_[s] = rkv_t0_[s] + N2;
        d = pick(v2);
      }
      rkv_dk_[s] = (int8_t)(d != 0 ? d : kRkvNone);         // (none fits: this run plays the base)
    }
    if (t >= rkv_t0_[s] && t < rkv_t1_[s] && rkv_dk_[s] != kRkvNone) (rkv_voice_[s] ? dkr : dks) = rkv_dk_[s];
  }
  static constexpr int8_t kRkvNone = 99;                  // hit variation: drawn, but nothing fits
  void TimingPlanRhythmPoly(int s, int G, int seg0) {
    uint8_t cell[VESTIGE_TIMING_LAYER_MAX_STEPS];
    bool base_rest_[VESTIGE_TIMING_LAYER_MAX_STEPS];        // the base (no variation) rests of this pass
    const int32_t t0 = rhy_t_[s];
    const int side = rhy_side_;                            // (read once: one half per pass)
    // RE-ROLL: another rhythm than this loop's last pass -> new rotation draws.
    if (VESTIGE_TIMING_RHY_ROT_RANDOM && VESTIGE_TIMING_RHY_REROLL) {
      const float u = rhy_level_;
      const int32_t key = ((((side * 32 + RhyPolyKs(u, side)) * 32 + RhyPolyKr(u, side)) * 32 + RhyKd(u)) * 2) + (RhyTempoStep(u) > 1.f ? 1 : 0);
      if (rhy_key_[s] >= 0 && rhy_key_[s] != key) {
        rhy_rot_[s] = TimingRandU(); rhy_rrot_[s] = TimingRandU();
        if (CT3_DIAG) { rhy_act_slot_ = s; rhy_act_n_ = rhy_act_n_ + 1; }   // DIAG: a line with the new rotations
      }
      rhy_key_[s] = key;
    }
    // Rotations: this loop's own draws (RHY_ROT_RANDOM) or the fixed constants.
    const uint32_t srot = !VESTIGE_TIMING_RHY_ROT_RANDOM ? (uint32_t)kRhySRot
                        : VESTIGE_TIMING_RHY_ROT_ON1 ? RhyOn1Rot(rhy_rot_[s], rhy_level_, side)
                        : RhyRotOf(rhy_rot_[s], RhyPolyNs(side));
    uint32_t rrot = !VESTIGE_TIMING_RHY_ROT_RANDOM ? (uint32_t)kRhyRRot
                  : VESTIGE_TIMING_RHY_AUDIBLE_RESTS ? RhyRestRot(rhy_rrot_[s], rhy_level_, srot, side)
                  : RhyRotOf(rhy_rrot_[s], RhyPolyNr(side));
    if (VESTIGE_TIMING_RHY_ROT_RANDOM && VESTIGE_TIMING_RHY_NO_FLAM) rrot = RhyNoFlamRot(rhy_level_, srot, rrot, side);
    for (int i = 0; i < G; i++) {
      const int32_t t = t0 + i;
      int dks = 0, dkr = 0;
      if (VESTIGE_TIMING_RHY_KVAR) RhyKVarAt(s, t, side, srot, rrot, dks, dkr);
      int c = RhyPolyBase(t, rhy_level_, srot, rrot, side, dks, dkr);
      const bool base_rest = RhyPolyBase(t, rhy_level_, srot, rrot, side) == 2;
      // A stutter variation never lands on a rest (rests are never swallowed).
      if (dks != 0 && c == 1 && base_rest) c = 2;
      cell[i] = (uint8_t)c;
      base_rest_[i] = base_rest;
    }
    rhy_t_[s] = t0 + G;                                    // runs on across passes
    if (TimingRand() < VESTIGE_TIMING_RHY_VAR_PROB * (float)G / 32.f) {
      int hits[VESTIGE_TIMING_LAYER_MAX_STEPS], nh = 0, frees[VESTIGE_TIMING_LAYER_MAX_STEPS], nf = 0;
      for (int i = 0; i < G; i++) { if (cell[i]) hits[nh++] = i; else if (!base_rest_[i]) frees[nf++] = i; }
      (void)hits; (void)nh;
      if (nf > 0) cell[frees[TimingPick(nf)]] = 1;         // a variation = one added stutter (never on a base rest)
      timing_vars_++;
    }
    ln_nh_[s][kErrTiming] = ln_nh_[s][kErrCondition] = ln_nh_[s][kErrPlayback] = 0;
    const float ramp = RhyRatRamp(rhy_level_);
    for (int i = 0; i < G; i++) {
      if (!cell[i]) continue;
      const int l = (cell[i] == 2) ? kErrCondition : kErrTiming;
      if (ln_nh_[s][l] >= kLineMaxHits) continue;
      LineHit& x = ln_hit_[s][l][ln_nh_[s][l]++];
      x.start = (int8_t)(seg0 + i); x.len = 1; x.age = 0;
      x.type = (int8_t)((cell[i] == 2) ? kFigRest : kFigStutter); x.sub = (int8_t)((cell[i] == 2) ? 1 : 0);
      // RATCHETS (top end): this stutter also retriggers 2x / 4x in its step
      // (a second hit on the same cell: the stutter sets the source, the
      // ratchet the retrig count; the render falls back below MIN_STEP).
      if (cell[i] == 1 && ramp > 0.f && ln_nh_[s][kErrTiming] < kLineMaxHits && TimingRand() < VESTIGE_TIMING_RHY_RAT_P_MAX * powf(ramp, VESTIGE_TIMING_RHY_RAT_P_CURVE)) {
        LineHit& r = ln_hit_[s][kErrTiming][ln_nh_[s][kErrTiming]++];
        r.start = (int8_t)(seg0 + i); r.len = 1; r.age = 0; r.sub = 0;
        r.type = (int8_t)((TimingRand() < VESTIGE_TIMING_RHY_RAT_Q_MAX * ramp) ? kFigRatchet : kFigDouble);
      }
    }
    // DECIMATE colour: its own Euclidean pattern on the 8th-note OFF-BEATS
    // (running steps t = 2 mod 4): E(k, RHY_D_SLOTS) over those slots, k =
    // D_K_MIN .. D_K_MAX with the depth, at the loop's drawn rotation (fixed
    // RHY_D_ROT with ROT_RANDOM off); one step each,
    // stacked on a stutter, never on a rest; its factor fixed per slot.
    {
      const int kd = (int)(VESTIGE_TIMING_RHY_D_K_MIN + (VESTIGE_TIMING_RHY_D_K_MAX - VESTIGE_TIMING_RHY_D_K_MIN) * rhy_level_ + 0.5f);
      const int Nd = VESTIGE_TIMING_RHY_D_SLOTS;
      const int drot = VESTIGE_TIMING_RHY_ROT_RANDOM ? (int)((rhy_rot_[s] >> 8) % (uint32_t)Nd) : kRhyDRot;
      for (int i = 0; i < G; i++) {
        const int32_t t = t0 + i;
        if ((((t % 4) + 4) % 4) != 2 || cell[i] == 2) continue;
        const int q = (int)((((t / 4) % Nd) + Nd) % Nd);
        if (!RhyHit(q, kd, Nd, drot) || ln_nh_[s][kErrCondition] >= kLineMaxHits) continue;
        LineHit& x = ln_hit_[s][kErrCondition][ln_nh_[s][kErrCondition]++];
        x.start = (int8_t)(seg0 + i); x.len = 1; x.age = 0; x.type = (int8_t)kFigDecimate;
        x.sub = (int8_t)VESTIGE_TIMING_DECIM_FACTORS[(q + drot) % VESTIGE_TIMING_DECIM_N];
      }
    }
    for (int l = 0; l < kErrTypes; l++) ln_ops_[s][l] = 0;
    timing_edits_++;
  }
  // ---- engines 1 + 2: the TABLES (1 AFRO, 2 INTERLOCK) ------------------------
  // VESTIGE_TIMING_RHY_TABLE (vestige_constants.h): the depth picks a row, the
  // row gives STUTTERS and RESTS on the voice's RUNNING step count (it never
  // resets per pass or per line, so 12- and 8-step cycles roll across bars). A
  // stutter wins where both land. HALF-grid rows: one entry = an 8th = 2 steps
  // (a stutter replays the previous 8th, a rest covers both steps); 1x rows:
  // every hit is one step. The patterns are never rotated per loop; engine 2's
  // rows also carry their decimate mask (dec = the overlaps of the voices).
  // With chance RHY_VAR_PROB per ~32 steps a pass gets one VARIATION (one added
  // 1-step stutter), for that pass only.
  static int RhyMod(int32_t a, int n) { return (int)(((a % n) + n) % n); }
  static int32_t RhyDiv(int32_t a, int n) { return (a >= 0) ? a / n : -((-a + n - 1) / n); }   // floor
  // The table of K3 side `side` (kRhyCcw: the selected engine's table; kRhyCw:
  // engine 2's CW table) and its row count.
  static const VestigeRhyRow* RhyTab(int side) { return side == kRhyCw ? VESTIGE_TIMING_RHY_TABLE_CW : VESTIGE_TIMING_RHY_TABLE; }
  static int RhyRows(int side) { return side == kRhyCw ? VESTIGE_TIMING_RHY_ROWS_CW : VESTIGE_TIMING_RHY_ROWS; }
  // The row for depth u (0 = off) on side `side`: min(ROWS-1, (int)(u * ROWS)).
  static int RhyRow(float u, int side = kRhyCcw) {
    const int N = RhyRows(side);
    int r = (int)(u * (float)N);
    if (r < 0) r = 0;
    if (r > N - 1) r = N - 1;
    return r;
  }
  // Pattern p at entry i (the row's grid index) / running step t (tremolo).
  static bool RhyPatHit(const VestigeRhyPat& p, int32_t i, int32_t t) {
    switch (p.kind) {
      case kRhyStr:  return p.len > 0 && p.str[RhyMod(i, p.len)] == 'x';
      case kRhyEuc:  return p.n > 0 && RhyHit(RhyMod(i, p.n), p.k, p.n, RhyMod(p.rot, p.n));
      case kRhyTrem: return RhyMod(t, 2) == 1 && (p.k == kRhyTremOdd || RhyMod(t, 16) >= 12);
      default:       return false;
    }
  }
  // The base cell (0 pause, 1 stutter, 2 rest) at running step t of row `row`.
  static int RhyCell(int32_t t, int row, int side = kRhyCcw) {
    const VestigeRhyRow& R = RhyTab(side)[row];
    const int32_t i = (R.grid == kRhyGridHalf) ? RhyDiv(t, 2) : t;
    if (RhyPatHit(R.stut, i, t)) return 1;                 // a stutter wins where both land
    if (RhyPatHit(R.rest, i, t)) return 2;
    return 0;
  }
  static int RhyTableBase(int32_t t, float u, int side = kRhyCcw) { return RhyCell(t, RhyRow(u, side), side); }
  // Engine 2: a decimate accent at running step t of row `row` (the row's
  // overlap mask; the table guarantees it only lands on a stutter).
  static bool RhyDecAt(int32_t t, int row, int side = kRhyCcw) {
    return VESTIGE_TIMING_RHY_ENGINE == 2 && RhyPatHit(RhyTab(side)[row].dec, t, t);
  }
  // Its factor, fixed by the overlap's position in the dec cycle: the j-th
  // hit of the cycle takes DECIM_FACTORS[j % DECIM_N] (deterministic; the
  // same every cycle, every pass, every loop).
  static int RhyDecFactor(int32_t t, int row, int side) {
    const VestigeRhyPat& p = RhyTab(side)[row].dec;
    const int n = (p.kind == kRhyStr) ? p.len : (p.kind == kRhyEuc) ? p.n : 1;
    const int i = RhyMod(t, n > 0 ? n : 1);
    int j = 0;
    for (int x = 0; x < i; x++) j += RhyPatHit(p, x, x) ? 1 : 0;
    return VESTIGE_TIMING_DECIM_FACTORS[j % VESTIGE_TIMING_DECIM_N];
  }
  void TimingPlanRhythmTable(int s, int G, int seg0) {
    uint8_t cell[VESTIGE_TIMING_LAYER_MAX_STEPS];
    const int32_t t0 = rhy_t_[s];
    const int side = (VESTIGE_TIMING_RHY_ENGINE == 2) ? rhy_side_ : kRhyCcw;   // (read once: one table per pass)
    const int row = RhyRow(rhy_level_, side);
    const bool half = RhyTab(side)[row].grid == kRhyGridHalf;
    for (int i = 0; i < G; i++) cell[i] = (uint8_t)RhyCell(t0 + i, row, side);
    rhy_t_[s] = t0 + G;                                    // runs on across passes
    if (TimingRand() < VESTIGE_TIMING_RHY_VAR_PROB * (float)G / 32.f) {
      int frees[VESTIGE_TIMING_LAYER_MAX_STEPS], nf = 0;
      for (int i = 0; i < G; i++) if (!cell[i]) frees[nf++] = i;
      if (nf > 0) cell[frees[TimingPick(nf)]] = 3;         // a variation = one added 1-step stutter
      timing_vars_++;
    }
    ln_nh_[s][kErrTiming] = ln_nh_[s][kErrCondition] = ln_nh_[s][kErrPlayback] = 0;
    for (int i = 0; i < G; i++) {
      if (!cell[i]) continue;
      // A HALF-grid hit spans its 8th (both steps, clipped at the pass edges);
      // a 1x hit and the variation are one step.
      int len = 1;
      if (half && cell[i] != 3 && i + 1 < G && cell[i + 1] == cell[i] && RhyMod(t0 + i, 2) == 0) len = 2;
      const bool rest = (cell[i] == 2);
      const int l = rest ? kErrCondition : kErrTiming;
      if (ln_nh_[s][l] < kLineMaxHits) {
        LineHit& x = ln_hit_[s][l][ln_nh_[s][l]++];
        x.start = (int8_t)(seg0 + i); x.len = (int8_t)len; x.age = 0;
        x.type = (int8_t)(rest ? kFigRest : kFigStutter); x.sub = (int8_t)(rest ? 1 : 0);
      }
      i += len - 1;
    }
    // Engine 2: the DECIMATE accents = the row's overlap mask (where voice A's
    // stutters and voice B coincide): one step each, on that stutter (never
    // on a rest, never on the variation's free step), its factor fixed by its
    // position in the dec cycle. No off-beat pattern.
    if (VESTIGE_TIMING_RHY_ENGINE == 2) {
      for (int i = 0; i < G; i++) {
        const int32_t t = t0 + i;
        if (cell[i] != 1 || !RhyDecAt(t, row, side) || ln_nh_[s][kErrCondition] >= kLineMaxHits) continue;
        LineHit& x = ln_hit_[s][kErrCondition][ln_nh_[s][kErrCondition]++];
        x.start = (int8_t)(seg0 + i); x.len = 1; x.age = 0; x.type = (int8_t)kFigDecimate;
        x.sub = (int8_t)RhyDecFactor(t, row, side);
      }
    }
    // Engine 1: the DECIMATE colour = its own Euclidean pattern on the 8th-note
    // OFF-BEATS (running steps t = 2 mod 4; HALF rows: the quarter off-beats,
    // t = 4 mod 8): E(k, RHY_D_SLOTS) over those slots, k = D_K_MIN .. D_K_MAX
    // with the depth, at the fixed RHY_D_ROT (never random); one step each,
    // stacked on a stutter, never on a rest; its factor fixed per slot.
    else {
      const int kd = RhyKd(rhy_level_);
      const int Nd = VESTIGE_TIMING_RHY_D_SLOTS, drot = kRhyDRot;
      const int per = half ? 8 : 4;                         // off-beat period in steps
      for (int i = 0; i < G; i++) {
        const int32_t t = t0 + i;
        if (RhyMod(t, per) != per / 2 || cell[i] == 2) continue;
        const int q = RhyMod(RhyDiv(t, per), Nd);
        if (!RhyHit(q, kd, Nd, drot) || ln_nh_[s][kErrCondition] >= kLineMaxHits) continue;
        LineHit& x = ln_hit_[s][kErrCondition][ln_nh_[s][kErrCondition]++];
        x.start = (int8_t)(seg0 + i); x.len = 1; x.age = 0; x.type = (int8_t)kFigDecimate;
        x.sub = (int8_t)VESTIGE_TIMING_DECIM_FACTORS[(q + drot) % VESTIGE_TIMING_DECIM_N];
      }
    }
    for (int l = 0; l < kErrTypes; l++) ln_ops_[s][l] = 0;
    timing_edits_++;
  }
  void TimingPlanLayers(int s, bool rev, size_t L, double rho, float el0) {
    if (!diag_clock_) { TimingPlanLayersImpl(s, rev, L, rho, el0); return; }
    const uint32_t t0 = diag_clock_();
    TimingPlanLayersImpl(s, rev, L, rho, el0);
    const uint32_t d = diag_clock_() - t0; if (d > diag_plan_us_) diag_plan_us_ = d;
  }
  void TimingPlanLayersImpl(int s, bool rev, size_t L, double rho, float el0) {
    trig_cnt_[s] = 0; trig_next_[s] = 0; cur_pat_[s] = -1; cur_var_[s] = kVarNone;
    trig_L_[s] = L; trig_rev_[s] = rev; trig_start_[s] = pass_[s];
    sl_n_[s] = 0; TimingCond(s, 0);
    var_last_[s] = false;
    const bool rhythm = rhy_level_ > 0.f;
    const bool any = rhythm || err_level_[kErrTiming] > 0.f || err_level_[kErrCondition] > 0.f || err_level_[kErrPlayback] > 0.f;
    if (!any) { LineClear(s); return; }
    if (ln_rhythm_[s] != rhythm) { LineClear(s); ln_rhythm_[s] = rhythm; }   // the two halves never share lines
    const double pass_out = (double)L / (rho > 0.0 ? rho : 1.0);
    const double min_step = (double)VESTIGE_TIMING_MIN_STEP_MS * 0.001 * (double)sr_;
    // G: 2^k or 3 x 2^k steps per pass, the step closest (in ratio) to the
    // wanted step in output time: VESTIGE_TIMING_STEP_MS up to a loop of
    // VESTIGE_TIMING_STEP_KNEE_MS, then growing as (loop / knee)^STEP_EXP
    // (long, ambient loops glitch slower: 8 s = 250 ms at 0.5).
    const double knee = (double)VESTIGE_TIMING_STEP_KNEE_MS * 0.001 * (double)sr_;
    const double want = (double)VESTIGE_TIMING_STEP_MS * 0.001 * (double)sr_
                      * (pass_out > knee ? pow(pass_out / knee, (double)VESTIGE_TIMING_STEP_EXP) : 1.0)
                      * (rhythm ? (double)RhyTempoStep(rhy_level_) : 1.0);   // K3 mode 1: half / double time
    int G = 1; double best = 1e30;
    for (int base = 1; base <= 3; base += 2)
      for (int g = base; g <= VESTIGE_TIMING_LAYER_MAX_STEPS; g *= 2) {
        const double r = pass_out / (double)g / want;
        const double d = r > 1.0 ? r : 1.0 / r;
        const bool tie = fabs(d - best) <= best * 1e-6;       // (a tie keeps the finer grid)
        if ((d < best && !tie) || (tie && g > G)) { best = d; G = g; }
      }
    const int LP = (G >= VESTIGE_TIMING_LINE_STEPS) ? 1 : (VESTIGE_TIMING_LINE_STEPS + G - 1) / G;   // passes per line
    const int C = G * LP;
    if (ln_cells_[s] != C) { LineClear(s); ln_cells_[s] = C; ln_pass_[s] = -1; }
    ln_pass_[s] = (ln_pass_[s] + 1) % LP;
    const int seg0 = ln_pass_[s] * G;
    const bool ok4 = pass_out / (double)G / 4.0 >= min_step;
    const bool ok2 = pass_out / (double)G / 2.0 >= min_step;
    const bool okr = GuardReady(s);
    if (rhythm) TimingPlanRhythm(s, G, seg0);
    // Evolve + age (the CW layers).
    for (int l = 0; l < kErrTypes && !rhythm; l++) {
      const float lv = err_level_[l];
      if (!(lv > 0.f) || !LayerOn(l)) { ln_nh_[s][l] = 0; ln_ops_[s][l] = 0; continue; }
      if (ln_nh_[s][l] == 0) { ln_ops_[s][l] = -1; continue; }   // empty: seed in the block tick (no hits to age)
      if (ln_ops_[s][l] > 0) LineMutate(s, l, lv, ln_ops_[s][l]);   // leftovers (a pass shorter than the spread)
      ln_ops_[s][l] = LineOps(lv);                          // this pass's changes, one per block (TimingLayerTick)
      for (int h = 0; h < ln_nh_[s][l]; h++) {               // a hit in this pass plays once more
        LineHit& x = ln_hit_[s][l][h];
        if (x.start + x.len > seg0 && x.start < seg0 + G && ++x.age >= VESTIGE_TIMING_LINE_LIFE) LineRetype(l, x);
      }
      timing_edits_++;
    }
    // Render. src = which step's material plays (0..G-1), dir = +1 / -1.
    int8_t src[VESTIGE_TIMING_LAYER_MAX_STEPS], dir[VESTIGE_TIMING_LAYER_MAX_STEPS];
    int8_t rat[VESTIGE_TIMING_LAYER_MAX_STEPS], cnd[VESTIGE_TIMING_LAYER_MAX_STEPS];
    for (int i = 0; i < G; i++) { src[i] = (int8_t)i; dir[i] = 1; rat[i] = 1; cnd[i] = 0; }
    auto seg_cells = [&](const LineHit& x, int* a, int* b) {  // the hit's steps in this pass [a, b)
      int lo = x.start - seg0, hi = x.start + x.len - seg0;
      if (lo < 0) lo = 0;
      if (hi > G) hi = G;
      *a = lo; *b = hi; return lo < hi;
    };
    for (int h = 0; h < ln_nh_[s][kErrTiming]; h++) {
      const LineHit& x = ln_hit_[s][kErrTiming][h];
      int a, b; if (!seg_cells(x, &a, &b)) continue;
      for (int i = a; i < b; i++) {
        const int c = i + seg0, j = c - x.start;             // line cell, cell within the hit
        switch (x.type) {
          case kFigStutter: src[i] = (int8_t)(((c - x.len) % G + G) % G); break;   // the len steps before, again
          case kFigRepeat:  src[i] = (int8_t)(((x.start - 1) % G + G) % G); break; // the step before, held
          case kFigDouble:  if (ok2) rat[i] = 2; break;
          case kFigRatchet: rat[i] = ok4 ? 4 : (ok2 ? 2 : 1); break;
          case kFigRetrig:  src[i] = (int8_t)(j % G); break;                       // the loop from its start
          default: break;
        }
      }
    }
    if (okr) for (int h = 0; h < ln_nh_[s][kErrPlayback]; h++) {
      const LineHit& x = ln_hit_[s][kErrPlayback][h];
      int a, b; if (!seg_cells(x, &a, &b)) continue;
      int8_t ts[VESTIGE_TIMING_LAYER_MAX_STEPS], td[VESTIGE_TIMING_LAYER_MAX_STEPS], tr[VESTIGE_TIMING_LAYER_MAX_STEPS];
      for (int i = a; i < b; i++) { ts[i] = src[i]; td[i] = dir[i]; tr[i] = rat[i]; }
      for (int i = a; i < b; i++) { const int mI = a + b - 1 - i; src[i] = ts[mI]; dir[i] = (int8_t)-td[mI]; rat[i] = tr[mI]; }
    }
    for (int h = 0; h < ln_nh_[s][kErrCondition]; h++) {
      const LineHit& x = ln_hit_[s][kErrCondition][h];
      int a, b; if (!seg_cells(x, &a, &b)) continue;
      for (int i = a; i < b; i++) cnd[i] = x.sub;
    }
    for (int i = 0; i < G; i++) {
      if (i < VESTIGE_TIMING_SLICE_MAX) sl_order_[s][i] = (cnd[i] == 1) ? (int8_t)-1 : src[i];
      tl_src_[s][i] = src[i]; tl_dir_[s][i] = dir[i]; tl_rat_[s][i] = rat[i]; tl_cnd_[s][i] = cnd[i];   // (diag)
    }
    // Events.
    auto bnd = [&](int i) { return (i >= G) ? (float)L : (float)(size_t)((double)L * (double)i / (double)G + 0.5); };
    // Read mapping at elapsed position `at` for step i: forward = restart at
    // its source step's start (offset); reverse = read its source step from
    // its end backward (mirror M, converted for a reversed loop).
    auto emit = [&](float at, int i, int8_t mc) {
      if (trig_cnt_[s] >= kTimingEvents) return;
      const int e = trig_cnt_[s]++;
      trig_pos_[s][e] = at; pm_mute_ev_[s][e] = mc; pm_jump_ev_[s][e] = true;
      if (dir[i] > 0) { sl_off_[s][e] = at - bnd(src[i]); pm_rev_ev_[s][e] = 0; }
      else {
        const float Me = at + bnd(src[i] + 1) - 1.f;
        sl_off_[s][e] = rev ? 2.f * (float)L - 2.f - Me : Me; pm_rev_ev_[s][e] = 1;
      }
    };
    if (!(el0 > 0.f)) {                                     // step 0, at the pass start itself
      TimingCond(s, cnd[0]);
      if (dir[0] < 0) { lrev_[s] = true; lrev_m_[s] = rev ? 2.f * (float)L - 2.f - (bnd(src[0] + 1) - 1.f) : bnd(src[0] + 1) - 1.f;
                        RestartStreams(s); timing_trigs_++; }
      else if (src[0] != 0) { trig_off_[s] = -bnd(src[0]); RestartStreams(s); timing_trigs_++; }
    }
    for (int i = 0; i < G; i++) {
      const float b0 = bnd(i), b1 = bnd(i + 1);
      for (int j = 0; j < rat[i]; j++) {
        const float at = (j == 0) ? b0 : (float)(size_t)(b0 + (b1 - b0) * (float)j / (float)rat[i] + 0.5f);
        if (at <= el0 || (i == 0 && j == 0)) continue;
        int8_t mc = -1; bool map = (j > 0);
        if (j == 0) {
          if (cnd[i] != cnd[i - 1]) mc = cnd[i];
          map = !(dir[i] == dir[i - 1] && rat[i - 1] == 1 && src[i] == src[i - 1] + dir[i]);
        }
        if (map) emit(at, i, mc);
        else if (mc >= 0 && trig_cnt_[s] < kTimingEvents) {
          const int e = trig_cnt_[s]++;
          trig_pos_[s][e] = at; pm_mute_ev_[s][e] = mc; pm_jump_ev_[s][e] = false; pm_rev_ev_[s][e] = -1;
        }
      }
    }
    sl_n_[s] = G; cur_pat_[s] = 0; timing_patterns_++;
    if (ln_nh_[s][0] + ln_nh_[s][1] + ln_nh_[s][2] > 0) { cur_var_[s] = kVarRot; timing_vars_++; }
  }
  // Per sample, per loop voice (after the head advance): instance-start plan,
  // then the next pending hit. Cost: one compare while nothing is pending; one
  // more float compare while a pattern plays.
  void TimingStep(int s, bool rev, size_t L) {
    if (pass_[s] != trig_pass_[s]) {                        // a new pass began (this sample)
      trig_pass_[s] = pass_[s];
      const int k = (VESTIGE_TIMING_MODE != 0) ? 0 : TimingSpanK(pass_[s], rev);
      if (k == 0) {                                         // a new pattern instance, on the one
        if (trig_off_[s] != 0.f || lrev_[s]) {              // leaving a pattern instance:
          trig_off_[s] = 0.f; lrev_[s] = false;             // back on the timeline, at the one
          RestartStreams(s);
          timing_returns_++;
        }
        if (VESTIGE_TIMING_MODE == 3)      TimingPlanLayers(s, rev, L, rho_d_[s], 0.f);
        else if (VESTIGE_TIMING_MODE == 2) TimingPlanMem(s, rev, L, rho_d_[s], 0.f);
        else if (VESTIGE_TIMING_MODE == 1) TimingPlanSlices(s, rev, L, rho_d_[s], 0.f);
        else                               TimingPlanPass(s, rev, L, rho_d_[s], 0.f);
        return;
      }
      // A pass wrap INSIDE the span: the read simply runs on (no restart) —
      // unless the instance no longer matches the loop (its length changed at
      // this wrap — a C re-cut —, the direction flipped, or it is not this
      // span's): then its remaining hits are dropped and the read goes back to
      // the timeline, at the one.
      const int32_t kk = trig_rev_[s] ? trig_start_[s] - pass_[s] : pass_[s] - trig_start_[s];
      if (L != trig_L_[s] || rev != trig_rev_[s] || kk != k) {
        trig_next_[s] = trig_cnt_[s];
        if (trig_off_[s] != 0.f) { trig_off_[s] = 0.f; RestartStreams(s); timing_returns_++; }
        timing_aborts_++;
        return;
      }
    }
    if (trig_next_[s] >= trig_cnt_[s]) return;
    // Elapsed span (material units): the pass within the span x L, plus the
    // elapsed pass: forward = the head (a pass starts at 0), reverse = (L - 1)
    // - head (a reverse pass starts at L - 1). With that, the read head lands
    // exactly on 0 (forward) / L - 1 (reverse) at each hit.
    const float el = trig_rev_[s] ? (float)L - 1.f - fwd_[s] : fwd_[s];
    const float kL = (VESTIGE_TIMING_MODE != 0) ? 0.f
                   : (float)(trig_rev_[s] ? trig_start_[s] - pass_[s] : pass_[s] - trig_start_[s]) * (float)L;
    const float at = trig_pos_[s][trig_next_[s]];
    if (kL + el >= at) {
      // Retrigger: read head -> the loop's start (rev: end). Slices: -> the
      // step's slice start (rev: on the reversed loop).
      const int ev = trig_next_[s];
      const float off = (VESTIGE_TIMING_MODE != 0) ? sl_off_[s][ev] : at - kL;
      trig_next_[s]++;
      if (VESTIGE_TIMING_MODE >= 2) {                       // pass memory / layers: mute change and/or jump
        if (pm_mute_ev_[s][ev] >= 0) TimingCond(s, pm_mute_ev_[s][ev]);
        const int8_t rv = pm_rev_ev_[s][ev];
        if (rv >= 0) {                                      // reverse span on (mirror) / off (offset)
          lrev_[s] = (rv == 1);
          if (rv == 1) lrev_m_[s] = off; else trig_off_[s] = off;
          RestartStreams(s);
          timing_trigs_++; last_trig_slot_ = s; last_trig_at_ = rec_clock_now_;
          return;
        }
        if (!pm_jump_ev_[s][ev]) return;
      }
      if (off != trig_off_[s]) {                            // (already there: a hit on a pass start
        trig_off_[s] = off;                                 //  with the read on the timeline — no jump)
        RestartStreams(s);
        timing_trigs_++; last_trig_slot_ = s; last_trig_at_ = rec_clock_now_;
      } else {
        timing_inplace_++;
      }
    }
  }

  // ---- K1 speed crossfade helpers -------------------------------------------
  float SpeedRatio() const { return sp_rate_; }
  // Speed-version head, DERIVED from the clean head (never integrated), so every
  // version stays on the capture's grid by construction: position = r x (clean
  // timeline) mod L. Half speed = (clean + L*(pass parity)) / 2, double = 2*clean
  // mod L. They coincide with the clean head whenever it is at 0 on an even pass:
  // every two loop periods, exactly as the plan's power-of-two argument says.
  float SpeedHead(int s, float r) const {
    const float L = (float)PlayLen(s);
    int32_t rp;
    const float c = ReadHead(s, &rp);      // derived from the clean READ head: retriggers together
    if (r < 1.f) return (c + ((rp & 1) ? L : 0.f)) * 0.5f;
    float h = c * 2.f;
    if (h >= L) h -= L;
    return h;
  }
  // Audio thread, per sample. Smooths the K1 amount (declick) and swaps the
  // speed version (half <-> double) only while it is exactly silent, so the
  // noon crossing never jumps a sounding head.
  void UpdateSpeedXfade() {
    const int want = k1_side_;
    float tgt = k1_x_;
    if (want != 0 && want != sp_side_) {
      tgt = 0.f;                                   // fade the current version out first
      if (sp_x_ == 0.f) {
        sp_side_ = want; sp_rate_ = (want < 0) ? 0.5f : 2.f;
        // The old version's grains are still mid-window, silent only because
        // its gain is 0 right now; clear them so they cannot come back up as a
        // ghost under the new version's fade-in. Silent by construction.
        for (int g = 0; g < VESTIGE_GRAINS; g++)
          if (grain_ver_[g] && grains_[g].IsActive()) grains_[g] = GrainVoice{};
      }
    }
    if (sp_x_ != tgt) {
      sp_x_ += (tgt - sp_x_) * VESTIGE_K1_SMOOTH;
      if (fabsf(tgt - sp_x_) < 1e-4f) sp_x_ = tgt;
      const float h = GrainHannRise(sp_x_);        // sin^2(pi/2 x): equal-power pair
      g_sp_ = sqrtf(h);
      g_c_  = sqrtf(1.f - h);
    }
  }
  // Error-stage hook (plan §5): identity until stages 3-5 exist.
  static inline float PlaybackErrors(int /*s*/, float x) { return x; }

  // One speed-version grain at rate r, starting AT the speed head. Forward reads
  // [head, head + r*glen); reverse reads backward from head down to head - r*glen
  // — through the guard (start at head + L) when that would cross 0, so a read
  // never wraps below the row start.
  // Also used for the CLEAN stream when the tape rate is not 1 (ver 0), and
  // with `exact` the grain starts on the fractional head (NudgeStart) instead
  // of the integer below it.
  // `restart`: the first grain of a stream restarted by a C length change (its
  // predecessors are fading out; see CapCount).
  void EmitStreamGrain(int s, size_t glen, float head, float r, bool rev,
                       const float* coef, float atk_scale, int ver, bool exact,
                       bool restart = false) {
    const int nactive = CapCount();
    if (nactive >= VESTIGE_MB_GRAIN_CAP) { grain_cap_drops_++; return; }
    (void)restart;
    int g = -1;
    for (int k = 0; k < VESTIGE_GRAINS; k++) {
      int idx = (next_grain_ + k) % VESTIGE_GRAINS;
      if (!grains_[idx].IsActive()) { g = idx; next_grain_ = (idx + 1) % VESTIGE_GRAINS; break; }
    }
    if (g < 0) { pool_full_++; return; }
    const float  L  = (float)PlayLen(s);
    const size_t wp = ring_[s].GetWritePos();
    const size_t bl = ring_[s].GetLength();
    size_t start, delay;
    float  sf;
    if (!rev) {
      sf    = head;
      start = (size_t)sf;
      delay = (wp + bl - start) % bl;                       // Trigger: start = wp - delay
    } else {
      sf    = (head - r * (float)glen < 0.f) ? head + L : head;
      start = (size_t)sf;
      delay = (wp + bl + glen - start) % bl;                // Trigger(rev): start = wp - delay + glen
    }
    grain_src_[g]  = &ring_[s];
    grain_slot_[g] = s;
    grain_ver_[g]  = (uint8_t)ver;
    const float ov_comp = 2.f / VESTIGE_MB_OVERLAP;
    MarkGrainLive(g);                      // hot loop renders it from now on
    grains_[g].Trigger(ring_[s], delay, glen, rev, r, gain_[s] * ov_comp, 1, 1.0f, atk_scale);
    // C re-cut: the grain reads the loop through the view current NOW, and
    // keeps it for its whole life (a later change swaps only new grains).
    const int cv = (s < VESTIGE_VOICE_SLABS) ? cur_view_[s] : -1;
    if (cv >= 0) { grains_[g].SetView(&views_[s][cv]); grain_view_[g] = (int8_t)(s * 2 + cv); }
    else         grain_view_[g] = -1;
    if (exact) {
      grains_[g].NudgeStart(sf - (float)start);
      grains_[g].SetExactTrack();          // tape rates are not binary fractions: no float drift
      tape_grains_++;
    }
    if (coef) grains_[g].SetBandFilter(coef[0], coef[1], coef[2], coef[3], coef[4]);
  }

  // Emit one band grain. frozen=true (freeze half): clamp the read inside the safe
  // [0, L-glen] range (pinned point stays exact). frozen=false (loop/break-up): wrap
  // the loop seam so the forward head + scatter read across the loop point (the
  // wrap-guard holds the seamless head-continuation copy).
  // ver / rate: the K1 version (0 clean, 1 speed) and its read rate — frozen
  // only (a frozen grain reads glen*rate of the window). atk >= 0 overrides the
  // attack scale (a version restart); -1 = the default rule below.
  void EmitBandGrain(int s, size_t glen, float posf, const float* coef,
                     float spray_width, bool frozen,
                     int ver = 0, float rate = 1.f, float atk = -1.f) {
    // hard cap on concurrent grains (CPU guard for multi-voice freeze)
    const int nactive = CapCount();
    if (nactive >= VESTIGE_MB_GRAIN_CAP) { grain_cap_drops_++; return; }
    int g = -1;
    for (int k = 0; k < VESTIGE_GRAINS; k++) {
      int idx = (next_grain_ + k) % VESTIGE_GRAINS;
      if (!grains_[idx].IsActive()) { g = idx; next_grain_ = (idx + 1) % VESTIGE_GRAINS; break; }
    }
    if (g < 0) { pool_full_++; return; }
    const size_t L = loop_len_[s];
    float sprayf = spray_width;
    if (L < VESTIGE_SHORT_LEN) { float cap = (float)L * 0.125f; if (sprayf > cap) sprayf = cap; }
    float pos = posf + (VestigeRand() * 2.f - 1.f) * sprayf;
    // Reverse (K2 CCW, loop modes only): the grain reads [pos, pos+glen] backward,
    // so shift its window down by glen to make it START at the head. A fresh loop
    // then plays from its tail backward, and a live direction flip through K2 noon
    // is positionally continuous (forward grains also start at the head).
    const PoolEngine& e = eng_[PoolOf(s)];   // this slot's own pool addressing
    bool rev = e.rev && !frozen && GuardReady(s);         // see ServiceMBFreeze
    if (!frozen && s < VESTIGE_VOICE_SLABS && lrev_[s]) rev = !rev;   // TIMING reverse span
    if (rev) pos -= (float)glen;
    size_t posi;
    if (frozen) {
      float hi = (float)L - (float)glen * rate; if (hi < 0.f) hi = 0.f;
      if (pos < 0.f) pos = 0.f; else if (pos > hi) pos = hi;
      posi = (size_t)pos + frz_base_[s];                   // (past the skipped attack)
    } else {
      pos = fmodf(pos, (float)L); if (pos < 0.f) pos += (float)L;
      posi = (size_t)pos;
    }
    const size_t wp  = ring_[s].GetWritePos();
    const size_t cap = ring_[s].GetLength();   // this row's real length (loop 8 s+guard, freeze 400 ms+240)
    size_t delay = (wp + cap - posi) % cap;
    grain_src_[g]  = &ring_[s];
    grain_slot_[g] = s;
    float ov_comp = 2.f / VESTIGE_MB_OVERLAP;
    // First grain of a fresh loop starts (near-)instantly, softened toward freeze
    // by the pool's amt (0 on the loop side = instant, 1 on the freeze side).
    // Without this the long loop grains fade in over their full Hann rise = an
    // audible slow attack on loop start.
    float atk_scale = (atk >= 0.f) ? atk : first_grain_[s] ? e.amt : (ver_idle_[s][0] ? 0.f : 1.f);
    grain_ver_[g] = (uint8_t)ver;
    grain_view_[g] = -1;
    // Reverse: same [posi, posi+glen] window as forward, read backward — so the
    // wrap-guard coverage is identical. Every grain in a band shares glen, so the
    // overlap-add stays coherent on the backward-walking head.
    MarkGrainLive(g);                      // hot loop renders it from now on
    grains_[g].Trigger(ring_[s], delay, glen, rev, rate, gain_[s] * ov_comp, 1, 1.0f, atk_scale);
    first_grain_[s] = false;
    if (coef) grains_[g].SetBandFilter(coef[0], coef[1], coef[2], coef[3], coef[4]);
    // coef == nullptr → 1-band full-range grain (no filter): the old-style freeze.
  }

  // RBJ 2-pole coeffs (normalised a0=1) into c[] = {b0,b1,b2,a1,a2}.
  void MBSetLP(float* c, float fc) {
    float w = 6.2831853f * fc / sr_, cs = cosf(w), sn = sinf(w), al = sn / 1.41421356f, a0 = 1 + al;
    c[0] = (1 - cs) * 0.5f / a0; c[1] = (1 - cs) / a0; c[2] = c[0]; c[3] = -2 * cs / a0; c[4] = (1 - al) / a0;
  }
  void MBSetHP(float* c, float fc) {
    float w = 6.2831853f * fc / sr_, cs = cosf(w), sn = sinf(w), al = sn / 1.41421356f, a0 = 1 + al;
    c[0] = (1 + cs) * 0.5f / a0; c[1] = -(1 + cs) / a0; c[2] = c[0]; c[3] = -2 * cs / a0; c[4] = (1 - al) / a0;
  }
  void MBSetBP(float* c, float fc, float Q) {   // constant 0 dB peak
    float w = 6.2831853f * fc / sr_, cs = cosf(w), sn = sinf(w), al = sn / (2.f * Q), a0 = 1 + al;
    c[0] = al / a0; c[1] = 0.f; c[2] = -al / a0; c[3] = -2 * cs / a0; c[4] = (1 - al) / a0;
  }
  // Build the crossover filterbank for a band count N into mb_bank_coef_[N-1][].
  // N-1 crossovers, log-spaced across [XLO, XHI] inclusive: band 0 = LP, mids = BP
  // (centre = geomean of its two crossovers, Q = centre/bandwidth), band N-1 = HP.
  // Reproduces the old 2-band (single fmid crossover) and 3-band (250/2000) splits
  // exactly, and generalises to 4/5. N=1 is a full-band cloud → no filter.
  void MBBuildBank(int n) {
    if (n <= 1) return;
    float x[VESTIGE_MAX_BANDS - 1];               // crossover frequencies (n-1 of them)
    if (n == 2) x[0] = sqrtf(VESTIGE_MB_XLO * VESTIGE_MB_XHI);
    else for (int i = 0; i < n - 1; i++)
      x[i] = VESTIGE_MB_XLO * powf(VESTIGE_MB_XHI / VESTIGE_MB_XLO, (float)i / (float)(n - 2));
    float (*bank)[5] = mb_bank_coef_[n - 1];
    MBSetLP(bank[0], x[0]);                        // low band
    for (int b = 1; b < n - 1; b++) {              // mid bands (bandpass)
      float lo = x[b - 1], hi = x[b], c = sqrtf(lo * hi);
      MBSetBP(bank[b], c, c / (hi - lo));
    }
    MBSetHP(bank[n - 1], x[n - 2]);                // high band
  }
  void MBInit() {
    for (int n = 1; n <= VESTIGE_MAX_BANDS; n++) MBBuildBank(n);
    for (int s = 0; s < VESTIGE_SLOTS; s++)
      for (int b = 0; b < VESTIGE_MAX_BANDS; b++) { mb_scan_[s][b] = (float)((s * 2 + b) * 373 % 4096); mb_timer_[s][b] = 0; }
  }

  // -------------------------------------------------------------------------
  // Recording lifecycle
  // -------------------------------------------------------------------------
  // Resume paused loops (fade back in from the current gain). No-op if playing.
  // Active pool only: the pool SW1 is not on stays parked.
  void ResumeFromMute() {
    if (!muted_) return;
    muted_ = false;
    for (int s = OwnLo(pool_); s < OwnHi(pool_); s++)
      if (active_[s] && !dying_[s])
        SetFade(s, 1.f);   // don't resurrect dying voices
  }

  // FS2 tap: capture + playback on/off. Hold state is untouched.
  //   ON : loops fade back in over K5 (if any were kept); auto capture resumes
  //        unless held.
  //   OFF: any in-flight capture is DROPPED (a commit would un-mute and swell a
  //        new loop in while off), and every loop fades out over K5. If not
  //        held, Controls() clears the buffers once those fade-outs finish.
  void SetEngaged(bool on) {
    if (on == engaged_) return;
    engaged_ = on;
    if (engaged_) {
      auto_rearm_block_ = false;   // a fresh engage listens immediately
      // Nothing kept on this side = no capture beat yet: LED1 flashes T from
      // the engage itself until the first capture re-anchors it.
      if (!PoolHasContent(pool_) && !recording_ && npend_ == 0) led_anchor_[pool_] = sample_clock_;
      ResumeFromMute();
      return;
    }
    // Off: no new capture, and the audio thread drops the one in flight and
    // any capture still waiting for its grid point (IsrDrop) at its next sample.
    cap_allow_ = false;
    drop_req_  = true;
    DropRecording();   // frip-side bookkeeping (archived); the voiced state is ISR-owned
    muted_ = true;
    // Active pool only: the other pool is already faded out / parked, and
    // restarting its fade would only reset its phase.
    for (int s = OwnLo(pool_); s < OwnHi(pool_); s++)
      // Don't touch dying voices — resurrecting their fade would strand them
      // active forever (they'd never hit the free condition).
      if (active_[s] && !dying_[s])
        SetFade(s, 0.f);
  }

  // Frippertronics (archived) capture bookkeeping; the voiced / freeze capture
  // is dropped by the audio thread on drop_req_ (IsrDrop).
  void DropRecording() {
    if (fripp_mode_) recording_ = false;
    rec_full_  = false;
    commit_pending_ = false; pending_len_ = 0; overhang_left_ = 0;
    silence_since_ = 0;
  }

  // -------------------------------------------------------------------------
  // SW1 side switch: loop side (UP/MIDDLE) <-> freeze side (DOWN).
  // The two sides are separate buffers and nothing crosses between them:
  //   - an in-flight capture belongs to the side being left, so it is DROPPED
  //     (committing it later would land a loop-side capture in the freeze pool
  //     or vice versa). A note still sounding is picked up by the new side's
  //     own auto capture on the next tick;
  //   - the side being left fades out over K5 — the same fade FS2 off uses, so
  //     the switch never clicks — and, once silent, is PARKED (not scheduled);
  //   - once that fade has finished it is CLEARED unless hold is on (Controls,
  //     state-derived). This is plan §4.2's rule for FS2 off applied to the
  //     side you leave: only hold preserves a buffer. Held, it is kept and
  //     fades back in when SW1 returns to it (if the effect is on);
  //   - the side being entered plays only its OWN content: if it kept some
  //     (held), that fades in over K5; otherwise it starts empty and fills from
  //     its own auto capture.
  // -------------------------------------------------------------------------
  void SwitchPool(int to) {
    if (to == pool_) return;
    // Order matters (the audio thread can run between any two lines): stop new
    // captures, request the drop (honoured before anything else at the ISR's
    // next sample), fade the side being left — which also catches a capture the
    // ISR activated just before the drop — then switch and retract the slot
    // reserved on the old side (the ISR ignores it once pool_ has moved).
    cap_allow_ = false;
    drop_req_  = true;
    DropRecording();
    const int from = pool_;
    for (int s = OwnLo(from); s < OwnHi(from); s++)
      if (active_[s] && !dying_[s]) SetFade(s, 0.f);
    pool_ = to;
    arm_slot_ = -1;
    if (!PoolHasContent(to)) led_anchor_[to] = sample_clock_;   // empty side: flash from the switch
    if (engaged_) {
      for (int s = OwnLo(to); s < OwnHi(to); s++) {
        if (!active_[s] || dying_[s]) continue;
        // Parked slots were not scheduled: fire their next grain promptly
        // rather than after a stale countdown. The read heads kept running
        // while parked (AdvanceParkedHeads), so the loop is still on its beat.
        for (int b = 0; b < VESTIGE_MAX_BANDS; b++) mb_timer_[s][b] = 0;
        timer_[s] = 0;
        SetFade(s, 1.f);
      }
    }
  }

  // A parked slot (other side, faded out) emits no grains, but its read heads
  // keep running exactly as ServiceMBFreeze would move them, so a held loop
  // stays on its own beat while away — the same way a held loop keeps running
  // silently while FS2 is off — and LED1's anchor for that side stays true.
  void AdvanceParkedHeads(int s) {
    size_t L = PlayLen(s);
    if (L < VESTIGE_GRAIN_MIN_LEN) return;
    const PoolEngine& e = eng_[PoolOf(s)];
    const double rho = SmoothTape(s);
    AdvanceHead(s, e.rev && GuardReady(s), rho);
    AdvanceBeat(s, rho);
    L = PlayLen(s);                                          // a wrap may have applied a new C length
    const size_t gcap = e.frozen ? VESTIGE_GUARD_SAMPLES : SlotGuard(s, L);
    const int row = e.nbands - 1;
    for (int bi = 0; bi < e.nbands; bi++) {
      size_t glen = (size_t)((float)VESTIGE_MB_GLEN[row][bi] * e.gscale);
      if (glen > L) glen = L;
      if (glen > gcap) glen = gcap;
      if (glen < VESTIGE_GRAIN_MIN_LEN) glen = (L < VESTIGE_GRAIN_MIN_LEN) ? L : VESTIGE_GRAIN_MIN_LEN;
      size_t maxscan = (L > glen + 1) ? (L - glen - 1) : 1;
      size_t scanlen = VESTIGE_MB_SCAN[row][bi]; if (scanlen > maxscan) scanlen = maxscan; if (scanlen < 1) scanlen = 1;
      mb_scan_[s][bi] += 1.f;
      if (mb_scan_[s][bi] >= (float)scanlen) mb_scan_[s][bi] -= (float)scanlen;
    }
  }

  // =========================================================================
  // STAGE 2 — sample-accurate quantised capture (plan §4.3, §7 Stage 2).
  //
  // Division of labour. The CONTROL thread only sets policy: cap_allow_
  // (engaged and not held), the slot the next capture records into
  // (arm_slot_), and requests (end_req_, drop_req_). The AUDIO thread runs the
  // whole capture lifecycle per sample, so every boundary is an exact sample:
  //   start  — the sample the gate opens (env_ crosses the open threshold);
  //            T is latched here (cap_T_), the LED anchor is set here.
  //   end    — decided at an exact sample: the T ceiling, or 80 ms of silence
  //            (raw length = where the silence began), or an end request.
  //            Loop side: length = GridQuantize::Quantize(raw, cap_T_), the
  //            NEAREST division of the capture's own T — up (recording goes on
  //            to the boundary) or down (the material past it is cut).
  //            Freeze side: unquantised (fixed 400 ms window, no grid).
  //   play   — at the exact sample start + k*len (its own grid, k >= 1): the
  //            head is placed so that sample outputs loop sample 0.
  // A capture records STRAIGHT INTO the free voice slot reserved for it (no
  // scratch, no copy), so the loop exists in place the moment its last sample
  // is written and can start on the very next one. Behind the loop end the
  // recorder writes the seam overhang already crossfaded into the head (same
  // curve WriteGuard used), and a background job (IsrFillGuards) writes the
  // head-continuation guard at up to VESTIGE_GUARD_FILL_PER_SAMPLE cells per
  // sample. Forward grains first read the guard one full loop after playback
  // starts, so the job (>= 1 cell/sample, started no later than the loop end)
  // is always ahead of them. Reverse grains can read it at once, so a reverse
  // loop only starts once its guard is ready — otherwise on its next grid point.
  // =========================================================================

  // Control thread: keep one free slot of the active side reserved for the
  // next capture. Single producer / single consumer: this thread writes
  // arm_slot_ only while it is -1; the audio thread only consumes it (sets -1).
  void ReserveArmSlot() {
    if (arm_slot_ >= 0 && PoolOf(arm_slot_) == pool_) return;
    const int s = FindFreeSlot();
    if (s >= 0) arm_slot_ = s;
    // None free: every slot is sounding or fading. The voice cap bounds the
    // unstolen voices, so one frees within a fade; the capture waits for it.
  }

  // Audio thread, once per sample (Process). x = this sample's input, now = its
  // sample number.
  void IsrCapture(float x, uint32_t now) {
    if (drop_req_) { drop_req_ = false; IsrDrop(); }
    const float close = auto_thresh_ * VESTIGE_AUTO_HYST;
    // Onset detector (sprawl's note-on idiom): the envelope jumping a RATIO
    // above a slow baseline, above a floor tied to the K4 open threshold, with
    // a refractory so one pluck is one onset. Runs every sample so its
    // baseline never goes stale; it is USED only to lift the re-arm block.
    // VESTIGE_ONSET_ON_GATE: read the smooth gate meter, not the fast one.
    const float oe = VESTIGE_ONSET_ON_GATE ? env_gate_ : env_;
    onset_slow_ += VESTIGE_ONSET_SLOW_COEF * (oe - onset_slow_);
    if (onset_refr_ > 0) onset_refr_--;
    const bool onset = (onset_refr_ == 0 && oe > auto_thresh_ * VESTIGE_ONSET_FLOOR_REL &&
                        oe > onset_slow_ * VESTIGE_ONSET_RISE);
    if (onset) {
      onset_refr_ = onset_refr_len_; onset_count_++;
      if (CT3_DIAG) DiagPush(GateDiag{'O', recording_ ? 'R' : '-', (uint8_t)rearm_block_, now,
                                      env_, onset_slow_, oe / (onset_slow_ > 1e-9f ? onset_slow_ : 1e-9f),
                                      env_gate_, 0u, 0u});
    }
    const float g = env_gate_;                    // the gate's meter: start / silence / re-arm
    if (!recording_) {
      if (rearm_block_) {                         // after a capture end: wait for the note to die...
        // With VESTIGE_REARM_EVERY_END the note must really have gone quiet
        // (well below the close level), so a beating tail cannot re-open it.
        if (g < close * (VESTIGE_REARM_EVERY_END ? VESTIGE_REARM_DEEP : 1.f)) {
          rearm_block_ = false;
          if (CT3_DIAG) DiagPush(GateDiag{'B', 'Q', 0, now, env_, g, 0.f, 0.f, 0u, 0u});   // lifted: quiet
        } else if (onset) {
          // ...or for a NEW attack. The old note may still be ringing above
          // both levels, so level hysteresis cannot see it; the onset can. It
          // starts the capture at this very sample — the new capture's "one".
          // (It never ends or splits a capture: phrase ends stay on the level
          // gate + the 80 ms silence.)
          const int a = arm_slot_;
          if (cap_allow_ && a >= 0 && PoolOf(a) == pool_) {
            rearm_block_ = false;
            onset_starts_++;
            if (CT3_DIAG) DiagPush(GateDiag{'B', 'O', 0, now, env_, g, 0.f, 0.f, 0u, 0u});   // lifted: onset
            diag_why_ = 'O';
            IsrStart(a, now);
          }
        }
      } else {
        const int a = arm_slot_;
        if (cap_allow_ && a >= 0 && PoolOf(a) == pool_ && g > auto_thresh_) {
          diag_why_ = 'L';
          IsrStart(a, now);
        }
      }
    }
    if (recording_) {
      const int s = rec_slot_;
      float* m = slab_[s];
      const size_t r = rec_idx_;
      const size_t E = cap_len_[s];
      if (r < cap_[s]) {                          // bounded by the row, always
        if (E > 0 && r >= E) {
          // Seam overhang, crossfaded into the head as it is recorded:
          // guard[E+k] = overhang*cos + head*sin (WriteGuard's curve).
          const size_t k  = r - E;
          const size_t xf = SeamXfadeLen(E);
          if (k < xf) {
            const float t = (float)(k + 1) / (float)(xf + 1);
            m[r] = x * cosf(t * 1.5707963f) + m[k] * sinf(t * 1.5707963f);
          }
        } else {
          m[r] = x;
        }
        rec_idx_ = r + 1;
      }
      // Where the sound last reached the close level, on the FAST meter. With a
      // slow gate meter the silence is detected later than it began; the
      // recorded length must not grow by that delay.
      if (env_ >= close) last_loud_ = r;
      if (E == 0) {
        // End not decided yet: request, ceiling, or sustained silence.
        if (end_req_) {
          end_req_ = false;
          diag_end_ = 'R';
          IsrDecide(s, rec_idx_, now);
        } else if (rec_idx_ >= cap_ceil_) {
          // The note is usually still ringing when the ceiling cuts the
          // capture: block re-arming until the envelope has fallen below the
          // close threshold, so the next loop starts on a fresh onset.
          rearm_block_ = true;
          // Mode 1: the slow gate meter decides a phrase end later than it
          // happened, so a phrase that ended shortly before T reaches the
          // ceiling first. If the FAST meter shows the sound already stopped,
          // this is really a phrase end: record its true length, not T.
          const bool ended = (VESTIGE_GATE_ENV_MODE != 0) && env_ < close && last_loud_ + 1 < rec_idx_;
          diag_end_ = ended ? 'c' : 'C';          // C = ceiling, c = ceiling but the sound had ended
          IsrDecide(s, ended ? last_loud_ + 1 : rec_idx_, now);   // (freeze too: no silent tail)
        } else if (g < close) {
          if (sil_run_ == 0) sil_onset_ = r;      // this sample is the first silent one
          if (++sil_run_ >= release_samples_) {
            if (VESTIGE_REARM_EVERY_END) rearm_block_ = true;   // next capture: onset or real quiet
            diag_end_ = 'S';
            const size_t raw_end = (VESTIGE_GATE_ENV_MODE == 0) ? sil_onset_ : last_loud_ + 1;
            IsrDecide(s, raw_end, now);             // where the sound stopped (freeze too: no silent tail)
          }
        } else {
          sil_run_ = 0;
        }
      }
      if (cap_len_[s] > 0 && rec_idx_ >= rec_stop_) recording_ = false;   // body + overhang done
    }
    if (npend_ > 0) IsrActivations(now);
    IsrFillGuards();
  }

  void IsrStart(int s, uint32_t now) {
    if (CT3_DIAG) DiagPush(GateDiag{'S', diag_why_, (uint8_t)PoolOf(s), now, env_, env_gate_,
                                    auto_thresh_, auto_thresh_ * VESTIGE_AUTO_HYST, (uint32_t)period_, 0u});
    diag_why_ = '?';
    KillSlotGrains(s);                  // zombie grains from the slot's last life (audio thread owns grains)
    rec_slot_  = s;
    rec_idx_   = 0;
    cap_len_[s] = 0;
    rec_stop_  = 0;
    sil_run_   = 0; sil_onset_ = 0; last_loud_ = 0;
    end_req_   = false;                 // a stale request must not end this capture
    cap_start_[s] = now;
    const bool loop = (PoolOf(s) == kPoolLoop);
    cap_T_[s]  = loop ? period_ : 0;    // T latched at the START: K2 / a tap cannot move this grid
    cap_ceil_  = loop ? period_ : max_loop_len_;
    age_[s]    = ++age_counter_;        // capture order = voice age
    led_anchor_[pool_] = now;           // the capture start is the "one" (LED1)
    led_slot_[pool_]   = s;             // ...and once it plays, its own loop time
    // Speculative guard for the ceiling: a capture that runs to T then starts
    // on time even in reverse. Re-based if the end turns out shorter.
    GuardJob(s, cap_ceil_);
    recording_ = true;
    arm_slot_  = -1;                    // consumed; the control thread reserves the next
  }

  // The end is known at sample `now`: raw = captured length before quantising.
  void IsrDecide(int s, size_t raw, uint32_t now) {
    const bool loop = (PoolOf(s) == kPoolLoop);
    size_t Q;
    if (loop) {
      Q = GridQuantize::Quantize(raw, cap_T_[s]);           // nearest division, up or down
    } else {
      Q = raw;
      if (Q < VESTIGE_MIN_LOOP_SAMPLES) Q = VESTIGE_MIN_LOOP_SAMPLES;
      if (Q > cap_ceil_)                Q = cap_ceil_;
    }
    cap_raw_[s] = raw;
    cap_decide_at_[s] = now;           // diagnostics / host test: when the end was known
    if (CT3_DIAG) DiagPush(GateDiag{'E', diag_end_, (uint8_t)rearm_block_, now, env_, env_gate_,
                                    0.f, 0.f, (uint32_t)raw, (uint32_t)Q});
    diag_end_ = '?';
    cap_len_[s] = Q;
    float* m = slab_[s];
    const size_t xf = SeamXfadeLen(Q);
    // Rounded DOWN (or exactly on): part of the overhang is already recorded
    // as plain audio — crossfade it into the head now. Anything recorded past
    // Q + xf is truncated: never read as loop, overwritten by the guard.
    const size_t have = (rec_idx_ > Q) ? (rec_idx_ - Q) : 0;
    for (size_t k = 0; k < xf && k < have; k++) {
      const float t = (float)(k + 1) / (float)(xf + 1);
      m[Q + k] = m[Q + k] * cosf(t * 1.5707963f) + m[k] * sinf(t * 1.5707963f);
    }
    rec_stop_ = Q + xf;                 // rounded UP: recording continues to Q, then the overhang
    if (gfill_base_[s] != Q) GuardJob(s, Q);
    // First playback sample. The loop always plays ON its own grid (loop
    // sample j sounds at start + k*Q + j); what varies is where it can enter:
    //   - end known before start+Q (ceiling, early round-up): at start+Q, on
    //     its "one";
    //   - end decided after start+Q (every round-down, and a round-up closer
    //     than the 80 ms release to its boundary): either join NOW, in phase
    //     (VESTIGE_LATE_JOIN_IN_PHASE), or wait for the next "one".
    // Freeze has no grid: it plays as soon as its fragment exists.
    const uint32_t el = now - cap_start_[s];                // this sample is `el` after the start
    uint32_t at;
    if (!loop) {
      at = (el >= Q) ? now : cap_start_[s] + (uint32_t)Q;
    } else if (el <= Q) {
      at = cap_start_[s] + (uint32_t)Q;                      // on time: the loop's "one"
    } else if (VESTIGE_LATE_JOIN_IN_PHASE) {
      at = now;                                              // late: join in phase, now
    } else {
      at = cap_start_[s] + (uint32_t)(((el + Q - 1) / Q) * Q);   // late: next "one"
    }
    act_at_[s] = at;
    pend_[s]   = true;
    npend_++;
  }

  void IsrActivations(uint32_t now) {
    for (int s = PoolLo(pool_); s < PoolHi(pool_); s++) {
      if (!pend_[s] || (int32_t)(now - act_at_[s]) < 0) continue;
      const bool need_rev = eng_[PoolOf(s)].rev && !eng_[PoolOf(s)].frozen;
      if (need_rev && !GuardReady(s)) {
        // Reverse can read the guard at once; not written yet. In-phase joining
        // retries every sample (the phase is taken at the moment it enters);
        // the wait-for-the-one variant moves to the next grid point.
        act_at_[s] += (PoolOf(s) == kPoolLoop && !VESTIGE_LATE_JOIN_IN_PHASE)
                      ? (uint32_t)cap_len_[s] : 1u;
        continue;
      }
      IsrActivate(s, now);
    }
  }

  // Loop s starts playing at THIS sample. Everything the old control-thread
  // commit did for its target, plus the retire / cap / gains that went with it,
  // happens here in one place, at the same sample.
  void IsrActivate(int s, uint32_t now) {
    pend_[s] = false; npend_--;
    size_t L = cap_len_[s];
    // Freeze: skip the pick attack — the window starts VESTIGE_FREEZE_ATTACK_SKIP_MS
    // in (less when that would leave under VESTIGE_FREEZE_MIN_KEEP_MS of tone).
    size_t base = 0;
    if (PoolOf(s) == kPoolFreeze) {
      const size_t skip = (size_t)(VESTIGE_FREEZE_ATTACK_SKIP_MS * 0.001f * sr_);
      const size_t keep = (size_t)(VESTIGE_FREEZE_MIN_KEEP_MS * 0.001f * sr_);
      base = (L > keep) ? (L - keep < skip ? L - keep : skip) : 0;
      L -= base;
    }
    frz_base_[s] = base;
    loop_len_[s] = L;
    play_pos_[s] = 0;
    timer_[s]    = 0;
    for (int b = 0; b < VESTIGE_MAX_BANDS; b++) { mb_timer_[s][b] = 0; mb_scan_[s][b] = 0.f; }
    // Phase on the capture's own grid: samples since its latest grid point
    // (0 when it enters on its "one"). ServiceMBFreeze advances the head BEFORE
    // emitting, later in this same sample, so place it one step back: forward
    // then reads loop sample p NOW, reverse reads L-1-p (the tail at p = 0).
    const bool     loop = (PoolOf(s) == kPoolLoop);
    const uint32_t p    = loop ? (uint32_t)((now - cap_start_[s]) % (uint32_t)L) : 0u;
    const bool     rev  = eng_[PoolOf(s)].rev && GuardReady(s);
    fwd_[s] = rev ? (float)(L - p) : (float)p - pitch_rate_s_;
    act_phase_[s] = p;
    // K1 speed versions start with the loop: the half-speed timeline begins at
    // this pass (forward: passes since the playback grid began at A+Q; reverse:
    // odd, so half speed also enters from the tail). Double speed needs none.
    pass_[s] = rev ? 1 : (int32_t)((now - cap_start_[s]) / (uint32_t)L) - 1;
    for (int b = 0; b < VESTIGE_MAX_BANDS; b++) mb_timer_sp_[s][b] = 0;
    ver_idle_[s][0] = ver_idle_[s][1] = true;   // whichever version emits first: instant attack
    // Follow-T bookkeeping. The capture was quantised against the T latched at
    // its start; from here on it keeps its DIVISION and follows T_now.
    div_[s] = loop ? GridQuantize::IndexOf(L, cap_T_[s]) : -1;
    if (div_[s] >= 0) {
      const double frac = (double)GridQuantize::kDivNum[div_[s]] / (double)GridQuantize::kDivDen[div_[s]];
      beat_k_[s] = frac / (double)L;                        // beats of T per material sample
      const double b = (double)(now - cap_start_[s]) / (double)cap_T_[s];
      beat_[s] = b - floor(b);                              // on the capture's own T grid
    } else {
      beat_k_[s] = 0.0; beat_[s] = 0.0;
    }
    beat_frac_[s] = (float)beat_[s];
    // C re-cut starts from the stored loop; a changed T applies at the first wrap.
    if (s < VESTIGE_VOICE_SLABS) {
      play_len_[s] = L; cur_view_[s] = -1; rc_building_[s] = rc_ready_[s] = false; rc_want_[s] = L;
      trig_off_[s] = 0.f; trig_cnt_[s] = trig_next_[s] = 0; cur_pat_[s] = -1; trig_pass_[s] = pass_[s];   // (pass_ set just above)
      rot_seed_[s] = TimingRandU();                         // this loop's rotation, for its whole life
      // K3 mode 1: this loop's rotation words (engine 0 with RHY_ROT_RANDOM:
      // stutters / decimates from rhy_rot_, rests from rhy_rrot_; redrawn on
      // a re-roll).
      rhy_rot_[s] = TimingRandU(); rhy_rrot_[s] = TimingRandU();
      rhy_t_[s] = 0;                                        //  and its running step count
      rkv_next_[s] = -1; rkv_t0_[s] = rkv_t1_[s] = 0;         //  and no hit variation yet
      rhy_key_[s] = -1;                                     //  and no rhythm played yet (re-roll)
      if (CT3_DIAG) { rhy_act_slot_ = s; rhy_act_n_ = rhy_act_n_ + 1; }   // DIAG: the rhythm log's newest loop
      if (VESTIGE_TIMING_MODE == 1) TimingDrawSlices(s);    // this loop's arrangement, for its whole life
      TimingMemClear(s); pm_cur_[s] = VESTIGE_TIMING_MEM_PASSES - 1;   // mode 2: fresh, empty memories;
      LineClear(s);                                            // mode 3: fresh, empty lines
      mute_d_[s] = mute_dt_[s] = 0.f; lrev_[s] = false;        //  the first pass plays memory 0
      decim_d_[s] = decim_dt_[s] = 0.f; decim_c_[s] = 0;
      var_last_[s] = false; cur_var_[s] = kVarNone;
    }
    rho_t_[s] = TapeTarget(s);
    rho_d_[s] = rho_t_[s];                                  // enters playing at T_now, no glide
    // TIMING: the pattern starts with the loop (this first pass may be joined
    // mid-way: elapsed p, the hits already behind it are skipped).
    if (s < VESTIGE_VOICE_SLABS && !eng_[PoolOf(s)].frozen) {
      if (VESTIGE_TIMING_MODE == 3) TimingPlanLayers(s, rev, L, rho_d_[s], (float)p);
      else if (VESTIGE_TIMING_MODE == 2) TimingPlanMem(s, rev, L, rho_d_[s], (float)p);
      else if (VESTIGE_TIMING_MODE == 1) TimingPlanSlices(s, rev, L, rho_d_[s], (float)p);
      else TimingPlanPass(s, rev, L, rho_d_[s], (float)TimingSpanK(pass_[s], rev) * (float)L + (float)p);
    }
    rho_s_[s] = (float)rho_d_[s];
    fwd_d_[s] = (double)fwd_[s];
    active_[s] = true; dying_[s] = false; stolen_[s] = false;
    if (s < VESTIGE_VOICE_SLABS) {                          // a new loop: full level, its count from 0
      dec_g_[s] = dec_t_[s] = rep_base_[s] = 1.f; dec_step_[s] = 0.f; rep_k_[s] = 0; rep_pass_[s] = pass_[s];
    }
    StartFadeIn(s);                     // swells in over K5; first grain instant
    ResumeFromMute();                   // record END unpauses the retained loops (stage-0 behaviour)
    while (CountLive() > target_voices_) {
      const int o = OldestLive();
      if (o < 0 || o == s) break;
      StartDying(o);                    // single voice: the old loop fades out as this fades in
    }
    EnforceVoiceCap();
    UpdateVoicedGains();
    last_act_slot_ = s; last_act_at_ = now; act_count_++;   // diagnostics / host test
  }

  void IsrDrop() {
    recording_   = false;
    sil_run_     = 0;
    rearm_block_ = false;               // a fresh engage / side listens immediately
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      if (pend_[s]) { pend_[s] = false; cap_len_[s] = 0; }
    }
    npend_ = 0;
  }

  // Head-continuation guard behind a loop of length L, written in the
  // background: cells L+k = head[k % L] for k in [xf, SlotGuard) — exactly
  // what WriteGuard wrote. Cells [L, L+xf) are the crossfaded overhang.
  void GuardJob(int s, size_t L) {
    const size_t xf = SeamXfadeLen(L);
    gfill_base_[s] = L;
    gfill_k_[s]    = xf;
    gfill_src_[s]  = xf % L;
    gfill_end_[s]  = SlotGuard(s, L);
    // Reads reach at most L + glen + 1 with glen <= L: guard cells < L + 2
    // are what a grain can touch (the rest keeps parity with WriteGuard).
    gready_[s]     = (L + 2 < gfill_end_[s]) ? L + 2 : gfill_end_[s];
    gfill_idle_    = false;
  }
  bool GuardReady(int s) const { return gfill_k_[s] >= gready_[s]; }
  void IsrFillGuards() {
    if (gfill_idle_) return;                        // no job anywhere: cost = one branch
    // Adaptive copy speed (see VESTIGE_GUARD_FILL_*): 8 per sample, more only
    // while the loop being copied is read fast. A flat fast copy was a CPU burst
    // at every new loop and overran the pedal (click on loop replace).
    int budget = (int)VESTIGE_GUARD_FILL_PER_SAMPLE;
    for (int q = 0; q < VESTIGE_SLOTS; q++) {
      if (gfill_k_[q] >= gfill_end_[q] || !active_[q]) continue;   // only a slot that is being READ
      const double r = ((double)rho_s_[q] > rho_t_[q]) ? (double)rho_s_[q] : rho_t_[q];
      const int need = (int)(2.5 * r) + 1;          // x2 for K1 double speed, x1.25 margin
      if (need > budget) budget = need;
    }
    if (budget > (int)VESTIGE_GUARD_FILL_MAX) budget = (int)VESTIGE_GUARD_FILL_MAX;
    if (budget > fill_budget_max_) fill_budget_max_ = budget;   // diagnostics / host test
    bool any = false;
    for (int s = 0; s < VESTIGE_SLOTS && budget > 0; s++) {
      if (gfill_k_[s] >= gfill_end_[s]) continue;
      any = true;
      const bool rec_here = recording_ && s == rec_slot_;
      if (!(active_[s] || pend_[s] || rec_here)) { gfill_k_[s] = gfill_end_[s]; continue; }  // slot left its life: stop
      const size_t L = gfill_base_[s];
      // Source must be recorded (and final: the body below L never changes).
      const size_t avail = rec_here ? ((rec_idx_ < L) ? rec_idx_ : L) : L;
      float* m = slab_[s];
      while (budget > 0 && gfill_k_[s] < gfill_end_[s] && gfill_src_[s] < avail) {
        m[L + gfill_k_[s]] = m[gfill_src_[s]];
        gfill_k_[s]++;
        if (++gfill_src_[s] >= L) gfill_src_[s] = 0;
        budget--;
      }
    }
    if (!any) gfill_idle_ = true;
  }

  // ---- T: master period ----------------------------------------------------
  // K2 magnitude -> T: log taper, noon dead zone = T_MIN, either end = T_MAX.
  static size_t KnobPeriod(float k2) {
    const float c = k2 - 0.5f;                        // [-0.5, +0.5]
    float mag = (fabsf(c) - VESTIGE_K2_DEADZONE) / (0.5f - VESTIGE_K2_DEADZONE);
    if (mag < 0.f) mag = 0.f;
    if (mag > 1.f) mag = 1.f;
    const float lo = (float)VESTIGE_T_MIN_SAMPLES;
    const float hi = (float)VESTIGE_T_MAX_SAMPLES;
    size_t t = (size_t)(lo * powf(hi / lo, mag));
    if (t < VESTIGE_T_MIN_SAMPLES) t = VESTIGE_T_MIN_SAMPLES;
    if (t > VESTIGE_T_MAX_SAMPLES) t = VESTIGE_T_MAX_SAMPLES;
    return t;
  }

  // K2 / FS1 arbitration (sprawl's, exactly — see sprawl.h Controls): the last
  // gesture wins. FS1 is a DEDICATED tap (no hold function), but the press is
  // still measured the sprawl way: the DOWN-press is the timing reference (so
  // tempo accuracy does not depend on the release), timed here from our own
  // rising-edge timestamp — NOT FootswitchEvent::held_ms, which is 0 whenever
  // the switch is up and therefore always 0 on the falling edge.
  void UpdatePeriod(float k2_raw, float k2, const FootswitchEvent& f1) {
    if (!k2_seeded_) { k2_last_ = k2_raw; k2_seeded_ = true; }
    if (fabsf(k2_raw - k2_last_) > VESTIGE_K2_MOVE_EPS) {
      k2_last_ = k2_raw;
      tap_period_ = 0;                                // knob wins: drop the tapped T
    }
    const uint32_t now = daisy::System::GetNow();
    if (f1.rising) f1_down_ms_ = now;
    if (f1.falling) {
      const uint32_t press = now - f1_down_ms_;
      if (press < VESTIGE_TAP_RELEASE_MS) {
        if (tap_prev_ms_ != 0) {
          const uint32_t iv = f1_down_ms_ - tap_prev_ms_;
          const bool valid = (iv >= VESTIGE_T_MIN_MS && iv <= VESTIGE_T_MAX_MS);
          // VESTIGE_TAP_AGREE: only a SECOND interval agreeing with the previous
          // one sets T (their mean), so one stray press cannot re-time the loops.
          const uint32_t pv = tap_prev_iv_ms_;
          const bool agree = valid && (tap_accept_one_ || (pv != 0 &&
              fabsf((float)iv - (float)pv) <= VESTIGE_TAP_AGREE * (float)pv));
          tap_prev_iv_ms_ = valid ? iv : 0;          // an invalid interval breaks the chain
          if (agree) {
            const float mean_ms = tap_accept_one_ ? (float)iv : 0.5f * ((float)iv + (float)pv);
            size_t t = (size_t)(mean_ms * 0.001f * sr_ + 0.5f);
            if (t < VESTIGE_T_MIN_SAMPLES) t = VESTIGE_T_MIN_SAMPLES;
            if (t > VESTIGE_T_MAX_SAMPLES) t = VESTIGE_T_MAX_SAMPLES;
            tap_period_ = t;
            // Nothing captured on this side yet = no capture beat to show, so
            // LED1 flashes from the tap itself (the down-press that closed the
            // interval). Once a capture exists, a tap changes only the period:
            // the beat stays on the capture's own start.
            if (!PoolHasContent(pool_) && !recording_ && npend_ == 0)
              led_anchor_[pool_] = sample_clock_ - (uint32_t)((float)press * 0.001f * sr_);
          }
        }
        tap_prev_ms_ = f1_down_ms_;
      }
    }
    // Knob T through a small movement deadband: loops now FOLLOW T, so ADC
    // jitter on K2 must not reach it (1% of T is ~17 cents of tape warble).
    // T is recomputed only when K2 has moved more than VESTIGE_K2_FOLLOW_DB;
    // a knob that does not move gives exactly the same T as before.
    if (!k2f_seeded_) { k2f_ = k2; k2f_seeded_ = true; }
    if (fabsf(k2 - k2f_) > VESTIGE_K2_FOLLOW_DB) k2f_ = k2;
    period_ = (tap_period_ > 0) ? tap_period_ : KnobPeriod(k2f_);
  }

  // Slot ranges. OWN = every slot backed by that side's slab (incl. the
  // archived frip slot and the record scratch, which are never audible voices).
  // PLAY = the pool's voice slots (what voice management and the scheduler use).
  static int PoolOf(int s) { return (s >= VESTIGE_FREEZE_SLOT0) ? kPoolFreeze : kPoolLoop; }
  static int OwnLo(int p)  { return (p == kPoolFreeze) ? VESTIGE_FREEZE_SLOT0 : 0; }
  static int OwnHi(int p)  { return (p == kPoolFreeze) ? VESTIGE_SLOTS : VESTIGE_LOOP_SIDE_SLOTS; }
  static int PoolLo(int p) { return (p == kPoolFreeze) ? VESTIGE_FREEZE_SLOT0 : 0; }
  static int PoolHi(int p) { return (p == kPoolFreeze) ? VESTIGE_FREEZE_SLOT0 + VESTIGE_FREEZE_SLABS
                                                       : VESTIGE_VOICE_SLABS; }
  static int PoolRec(int p) { return (p == kPoolFreeze) ? VESTIGE_FREEZE_REC_SLOT : VESTIGE_REC_SLOT; }
  // Head-continuation guard actually available behind a loop of length L in
  // slot s (loop rows: always VESTIGE_GUARD_SAMPLES; freeze rows: what is left).
  size_t SlotGuard(int s, size_t L) const {
    const size_t room = (cap_[s] > L) ? (cap_[s] - L) : 0;
    return (room < VESTIGE_GUARD_SAMPLES) ? room : VESTIGE_GUARD_SAMPLES;
  }
  // A slot faded to silence (its fade-out has finished). In the pool SW1 is
  // not on, such a slot is parked: kept, but not scheduled.
  bool Parked(int s) const {
    return fade_target_[s] < 0.5f && (fade_phase_[s] >= 1.f || fade_gain_[s] == 0.f);
  }
  bool PoolHasContent(int p) const {
    for (int s = OwnLo(p); s < OwnHi(p); s++) if (active_[s]) return true;
    return false;
  }
  bool PoolFadesOutDone(int p) const {
    for (int s = OwnLo(p); s < OwnHi(p); s++) {
      if (!active_[s]) continue;
      if (fade_target_[s] > 0.5f) return false;
      if (fade_phase_[s] < 1.f && fade_gain_[s] != 0.f) return false;
    }
    return true;
  }

  bool HasContent() const {
    for (int s = 0; s < VESTIGE_SLOTS; s++) if (active_[s]) return true;
    return false;
  }
  // Every active slot is fading out and has finished (or is already silent).
  // fade_* are written in the audio ISR; a stale read only delays this a tick.
  bool FadesOutDone() const {
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      if (!active_[s]) continue;
      if (fade_target_[s] > 0.5f) return false;
      if (fade_phase_[s] < 1.f && fade_gain_[s] != 0.f) return false;
    }
    return true;
  }

  void StartRecording() {
    if (recording_) return;
    if (fripp_mode_) {
      rec_slot_ = VESTIGE_FRIP_SLOT;
      rec_idx_  = (frip_len_ > 0) ? play_pos_[VESTIGE_FRIP_SLOT] : 0; // overdub syncs to playback
      if (frip_len_ > 0) {          // overdub: fade the summed input in (declick)
        frip_od_gain_ = 0.f; frip_od_target_ = 1.f; frip_stop_pending_ = false;
        frip_rec_prev_idx_ = (size_t)frip_rec_;  // seed tape resample cursor
        frip_in_acc_ = 0.f; frip_in_cnt_ = 0; frip_in_prev_ = 0.f;
      } else {                      // first capture: record head grows from 0 at rate r
        frip_rec_ = 0.f; frip_rec_prev_idx_ = 0;
        frip_in_acc_ = 0.f; frip_in_cnt_ = 0; frip_in_prev_ = 0.f;
      }
    } else {
      return;   // voiced / freeze captures start in the audio thread (IsrStart)
    }
    recording_ = true;
  }

  static constexpr float kVestigePi = 3.14159265358979323846f;

  // Begin a fade toward `target` (0 or 1) for slot s. Captures the current gain
  // as the fade's start point and resets its phase, so an interrupted fade
  // resumes smoothly (no jump) regardless of direction.
  void SetFade(int s, float target) {
    fade_target_[s] = target;
    fade_from_[s]   = fade_gain_[s];
    fade_phase_[s]  = 0.f;
  }

  // Minimal seam crossfade length (samples), scaled down for tiny loops.
  static size_t SeamXfadeLen(size_t L) {
    size_t xf = VESTIGE_SEAM_XFADE_MAX;
    if (xf > L / 2) xf = L / 2;
    return xf;
  }

  // Record end (FS2 release / phrase end / buffer full): set the loop length
  // NOW (timing-exact), then keep recording a short overhang for the seam
  // crossfade. Commit fires once the overhang is captured (commit_pending_).
  void EndRecording() {
    if (!recording_) return;
    if (fripp_mode_) {
      // Overdub: fade the input out, then commit once silent (declick record-out).
      if (frip_len_ > 0) { frip_od_target_ = 0.f; frip_stop_pending_ = true; return; }
      CommitRecording();                                // first pass: nothing to fade
      return;
    }
    // Voiced / freeze captures end in the audio thread (end_req_ -> IsrDecide).
  }

  void CommitRecording() {
    if (!recording_) return;
    recording_ = false;
    ResumeFromMute();   // record END unpauses the retained loops

    // ---- Frippertronics path (unchanged: records in-place into FRIP_SLOT) --
    if (fripp_mode_) {
      const int s = rec_slot_;   // == VESTIGE_FRIP_SLOT
      if (frip_len_ > 0) {
        // Overdub pass ended — refresh the wrap-guard, keep playing.
        RefreshFrippGuard(frip_len_);
        return;
      }
      size_t L = rec_idx_;
      if (L < VESTIGE_MIN_LOOP_SAMPLES) L = VESTIGE_MIN_LOOP_SAMPLES;
      if (L > max_loop_len_) L = max_loop_len_;
      RefreshFrippGuard(L);
      loop_len_[s] = L;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      active_[s]   = true;
      age_[s]      = ++age_counter_;
      frip_len_    = L;
      frip_head_   = 0.f;         // play the fresh loop from the top
      frip_rec_    = 0.f;         // record phase starts at the top too
      StartFadeIn(s);
      return;
    }

    // Voiced / freeze captures are activated in the audio thread (IsrActivate):
    // recorded straight into their slot, no scratch copy.
  }

  // Begin a fade-in for a slot: silent now, swelling to unity over K5 time.
  void StartFadeIn(int s) {
    fade_gain_[s] = 0.f;
    SetFade(s, 1.f);          // swell from silence
    first_grain_[s] = true;   // first grain of this fresh loop starts (near-)instantly
    // Position isn't anchored here: looper/scrub start from CommitRecording's
    // play_pos = 0; freeze pins its own live centre→end point in ServiceSlot.
  }

  // Build the wrap-guard so grains reading across the loop boundary continue
  // seamlessly. First xf samples = equal-power crossfade of the recorded
  // overhang m[L+k] (natural continuation of the loop tail) fading OUT into the
  // loop head m[k] fading IN — kills the seam click with no timing change. The
  // remainder is the loop repeated. (Fripp has no overhang → the crossfade
  // degenerates to the head copy, i.e. the old behaviour.)
  // Rebuild the frippertronics wrap-guard from the CURRENT loop. Unlike the
  // voiced guard (which crossfades a RECORDED overhang), fripp has no overhang,
  // so the seam [L, L+xf) bridges the loop's own tail into its head — killing
  // the end≠start step ("dang"). Called live during overdub so the guard tracks
  // the decaying/overdubbed loop instead of being a stale, undecayed snapshot
  // (that snapshot was the transient "stuck" at the seam that never faded).
  // Seam crossfade only: bridge the loop's tail (m[L-1]) into its head over xf.
  static void FrippSeamXfade(float* m, size_t L, size_t xf) {
    float tail = m[L - 1];
    for (size_t k = 0; k < xf; k++) {
      float t = (float)(k + 1) / (float)(xf + 1);        // 0→1
      m[L + k] = tail * cosf(t * 1.5707963f) + m[k] * sinf(t * 1.5707963f);
    }
  }
  // Refresh ONE seam-crossfade guard cell (c < xf) at AUDIO rate: a live equal-
  // power tail→head blend so the seam tracks the decaying loop body instead of
  // lagging a full pass (the short-loop overdub click). Trig-free via the LUT
  // (cos(t·π/2)=√(1-GrainHannRise), sin=√GrainHannRise) — same curve as above.
  static inline void FrippSeamCell(float* m, size_t L, size_t xf, size_t c) {
    float t = (float)(c + 1) / (float)(xf + 1);
    float g = GrainHannRise(t);
    m[L + c] = m[L - 1] * sqrtf(1.f - g) + m[c] * sqrtf(g);
  }
  void RefreshFrippGuard(size_t L) {
    if (L == 0) return;
    float* m = slab_[VESTIGE_FRIP_SLOT];
    size_t xf = SeamXfadeLen(L);
    FrippSeamXfade(m, L, xf);
    for (size_t k = xf; k < VESTIGE_GUARD_SAMPLES; k++) m[L + k] = m[k % L];
  }

  // Voice management works on the ACTIVE pool's voice slots only
  // [PoolLo(pool_), PoolHi(pool_)): a capture is committed into the side it was
  // recorded on, and the side SW1 is not on is never re-targeted.
  //
  // Reduce active voiced count to target, oldest-first (live K1 control).
  // Live = active and not fading out. Dying voices still sound (fading) but no
  // longer count toward the target or the gain normalization.
  int CountLive() const {
    int n = 0;
    for (int v = PoolLo(pool_); v < PoolHi(pool_); v++) if (active_[v] && !dying_[v]) n++;
    return n;
  }
  int OldestLive() const {
    int oldest = -1; uint32_t best = 0xFFFFFFFFu;
    for (int v = PoolLo(pool_); v < PoolHi(pool_); v++)
      if (active_[v] && !dying_[v] && age_[v] < best) { best = age_[v]; oldest = v; }
    return oldest;
  }
  // Free = not sounding, not fading, not waiting for its grid point, not being
  // recorded into and not already reserved for the next capture.
  int FindFreeSlot() const {
    const int rs = recording_ ? rec_slot_ : -1;
    for (int v = PoolLo(pool_); v < PoolHi(pool_); v++)
      if (!active_[v] && !dying_[v] && !pend_[v] && v != rs && v != arm_slot_) return v;
    return -1;
  }
  // Retire a voice gracefully: keep it sounding but fade it out over K5, then
  // free it once silent (Process). Used for K1-reduce and the crossfade tail.
  void StartDying(int s) {
    if (s < 0 || dying_[s]) return;
    dying_[s] = true;
    SetFade(s, 0.f);   // release over K5
  }

  // Concurrency cap: count/find the oldest voice that is NOT already being
  // fast-released. Total granulating voices are bounded to VESTIGE_MAX_VOICES
  // (the pre-regression CPU/grain ceiling); the excess oldest gets stolen.
  // Counted across BOTH pools, because the side SW1 just left is still
  // granulating its fade-out tail — exactly as it did when both sides shared
  // one pool. Two exemptions: a parked (silent, unscheduled) slot costs nothing,
  // and a held slot on the other side must not be stolen — stealing frees it,
  // and hold is the promise that it survives. Held tails are therefore exempt
  // (bounded by the grain cap and by their K5 fade).
  bool CapCounted(int s) const {
    if (!active_[s] || stolen_[s]) return false;
    if (PoolOf(s) == pool_) return true;
    return !held_ && !Parked(s);
  }
  int CountUnstolen() const {
    int n = 0;
    for (int p = 0; p < 2; p++)
      for (int v = PoolLo(p); v < PoolHi(p); v++) if (CapCounted(v)) n++;
    return n;
  }
  int OldestUnstolen() const {
    int oldest = -1; uint32_t best = 0xFFFFFFFFu;
    for (int p = 0; p < 2; p++)
      for (int v = PoolLo(p); v < PoolHi(p); v++)
        if (CapCounted(v) && age_[v] < best) { best = age_[v]; oldest = v; }
    return oldest;
  }
  // Steal a voice: fast-release it (declicked) so its slab frees quickly.
  void StealVoice(int s) {
    if (s < 0) return;
    stolen_[s] = true;
    if (!dying_[s]) { dying_[s] = true; SetFade(s, 0.f); }
  }
  // Keep total granulating voices within the CPU/grain budget by stealing the
  // oldest not-already-stolen voice until the count is back under the cap:
  // the live voices plus ONE fading tail (SW1 UP / DOWN: 2 — a third capture
  // during a long K5 crossfade steals the oldest tail; MIDDLE: 3 as before),
  // never above VESTIGE_MAX_VOICES. Checked at each activation only.
  int VoiceCap() const {
    const int c = target_voices_ + 1;
    return c < VESTIGE_MAX_VOICES ? c : VESTIGE_MAX_VOICES;
  }
  void EnforceVoiceCap() {
    const int cap = VoiceCap();
    while (CountUnstolen() > cap) {
      int o = OldestUnstolen();
      if (o < 0) break;
      StealVoice(o);
    }
  }
  void KillSlotGrains(int s) {
    for (int g = 0; g < VESTIGE_GRAINS; g++)
      if (grain_slot_[g] == s) grains_[g] = GrainVoice{};   // deactivate
  }

  void EvictToTarget() {
    while (CountLive() > target_voices_) {
      int o = OldestLive();
      if (o < 0) break;
      StartDying(o);   // fade out over K5, free when silent
    }
  }

  // Age-ramped fade over the FIFO stack. rank r (0 = newest); linear gain
  // (N-r)/N, normalised 1/sqrt(N) for the stacking law. Fixed slope (K5 now
  // drives the loop fade envelope, not this age-fade). Active pool only: the
  // other pool's gains are frozen with it (gain_ is baked into each grain at
  // trigger, and a parked pool triggers none).
  // K5 CCW = number of repeats, once per block. Per loop voice: rep_k_ counts
  // its passes since the count began (activation, or K5 leaving noon); repeat
  // k (0 = the first) plays at base x RepeatLevel(k, N) (a dB curve). A level
  // NEVER changes inside a repeat (no ducking while it plays): the step to
  // the next repeat's level is a VESTIGE_REPEAT_RAMP_MS ramp that ENDS on the
  // pass end, so every repeat starts at its own level; after the last one the
  // target is 0 and the next wrap frees the voice. A K5 change applies at the
  // next pass end. Noon / CW (N = 0): no count, the level stays where it is
  // (base follows it). FS2 hold pauses the count and the ramps.
  // Level of repeat k (0 = the first, full) of N, as a gain: a curve in dB,
  // FLOOR_DB x (k / (N-1))^CURVE — gentle early, steeper later, the last
  // repeat on the quiet floor, so the stop after it is not heard as a cut.
  static float RepeatLevel(int k, int N) {
    if (k <= 0 || N <= 1) return 1.f;
    const float x = (float)k / (float)(N - 1);
    return powf(10.f, VESTIGE_REPEAT_FLOOR_DB * powf(x, VESTIGE_REPEAT_CURVE) / 20.f);
  }
  void UpdateRepeats() {
    const int N = rep_n_;
    const float ramp = VESTIGE_REPEAT_RAMP_MS * 0.001f * sr_;
    for (int s = 0; s < VESTIGE_VOICE_SLABS; s++) {
      if (!active_[s]) continue;
      if (pass_[s] != rep_pass_[s]) { rep_pass_[s] = pass_[s]; if (N > 0 && !held_) rep_k_[s]++; }
      if (N <= 0 || eng_[PoolOf(s)].frozen) {                // endless: keep the level, no count
        rep_k_[s] = 0; rep_base_[s] = dec_g_[s]; dec_t_[s] = dec_g_[s]; continue;
      }
      if (dying_[s]) continue;
      const int k = rep_k_[s];
      if (k >= N) { StealVoice(s); continue; }              // after the last repeat (level already 0)
      dec_t_[s] = dec_g_[s]; dec_step_[s] = 0.f;            // inside a repeat: the level holds
      if (held_) continue;
      const bool   rev = eng_[PoolOf(s)].rev && GuardReady(s);
      const double rho = rho_d_[s] > 0.0 ? rho_d_[s] : 1.0;
      const float  L   = (float)PlayLen(s);
      const float  rem = (float)((double)(rev ? fwd_[s] : L - fwd_[s]) / rho);   // output samples left
      if (rem <= ramp + 48.f) {                             // the ramp into the next repeat, ending on the wrap
        float nxt = 0.f;                                    // (after the last repeat: silence)
        if (k + 1 < N) nxt = rep_base_[s] * RepeatLevel(k + 1, N);
        dec_t_[s] = nxt; dec_step_[s] = fabsf(dec_g_[s] - nxt) / (rem > 1.f ? rem : 1.f);
      }
    }
  }
  void UpdateVoicedGains() {
    // Level tracks the ACTUAL active-voice count. Age-ramp weights (newest = 1,
    // oldest = 1-d) are power-normalized as a SET so the total power equals a
    // single voice → the loop stays equally loud at any voice count (single is
    // no longer the loudest), while newer voices still sit above older ones.
    const int lo = PoolLo(pool_), hi = PoolHi(pool_);
    int n = 0;
    for (int v = lo; v < hi; v++) if (active_[v] && !dying_[v]) n++;
    const float d = VESTIGE_AGE_FADE_DEPTH;
    float sumsq = 0.f;
    for (int v = lo; v < hi; v++) {
      if (!active_[v]) { gain_[v] = 0.f; continue; }
      if (dying_[v]) continue;       // fading out: keep its frozen gain_
      int r = 0;   // rank among live voices: 0 = newest
      for (int w = lo; w < hi; w++)
        if (active_[w] && !dying_[w] && age_[w] > age_[v]) r++;
      float wr = (n > 1) ? (1.f - d * ((float)r / (float)(n - 1))) : 1.f;
      if (wr < 0.f) wr = 0.f;
      gain_[v] = wr;                 // stash weight; power-normalize below
      sumsq += wr * wr;
    }
    if (sumsq > 1e-9f) {
      const float norm = 1.f / sqrtf(sumsq);
      for (int v = lo; v < hi; v++)
        if (active_[v] && !dying_[v]) gain_[v] *= norm;
    }
  }

  // -------------------------------------------------------------------------
  // Frippertronics transitions (provisional — see Open items)
  // -------------------------------------------------------------------------
  void EnterFrippertronics() {
    // Independent buffer: fripp plays ONLY its own content, which persists across
    // mode switches. No seeding / carry-over from the voiced loops — the voiced
    // and fripp buffers are entirely separate.
    active_[VESTIGE_FRIP_SLOT]   = (frip_len_ > 0);
    loop_len_[VESTIGE_FRIP_SLOT] = frip_len_;
    play_pos_[VESTIGE_FRIP_SLOT] = 0;
    timer_[VESTIGE_FRIP_SLOT]    = 0;
    frip_head_                   = 0.f;
    if (active_[VESTIGE_FRIP_SLOT]) StartFadeIn(VESTIGE_FRIP_SLOT);
  }

  void LeaveFrippertronics() {
    // Sweeping back keeps the loop but repopulates voices from new playing.
    active_[VESTIGE_FRIP_SLOT] = false;
  }

  // -------------------------------------------------------------------------
  // Continuous-auto capture: silence is the phrase delimiter.
  // -------------------------------------------------------------------------
  void RunAutoCapture() {
    const float open  = auto_thresh_;
    const float close = auto_thresh_ * VESTIGE_AUTO_HYST;
    const uint32_t now = daisy::System::GetNow();
    if (!recording_) {
      // After a ceiling auto-stop, wait for the envelope to drop before re-arming.
      if (auto_rearm_block_) {
        if (env_ < close) auto_rearm_block_ = false;
      } else if (env_ > open) {
        StartRecording();                  // silence → sound: begin a phrase
      }
    } else {
      if (env_ < close) {
        if (silence_since_ == 0) silence_since_ = now;
        else if (now - silence_since_ >= VESTIGE_AUTO_RELEASE_MS) {
          EndRecording();                  // sound → sustained silence: end + overhang
          silence_since_ = 0;
        }
      } else {
        silence_since_ = 0;
      }
    }
  }

  // -------------------------------------------------------------------------
  void ClearAll() {
    recording_ = false;
    commit_pending_ = false; pending_len_ = 0; overhang_left_ = 0;
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      active_[s]   = false;
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;   // silence immediately (no fade)
      fade_target_[s] = 0.f;
      fade_phase_[s] = 0.f; fade_from_[s] = 0.f;
      dying_[s] = false; stolen_[s] = false;
      for (int b = 0; b < VESTIGE_MAX_BANDS; b++) { mb_timer_[s][b] = 0; mb_timer_sp_[s][b] = 0; }   // re-freeze fires promptly
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++) grains_[g] = GrainVoice{};
    frip_len_ = 0;
    frip_head_ = 0.f; frip_rec_ = 0.f;
    muted_    = false;
    // NB: engaged_ / held_ are intentionally preserved — clearing is a buffer
    // operation, not a transport one (the caller re-asserts muted_).
  }

  // Clear ONE side (the one SW1 is not on, once its fade-out has finished).
  // Leaves the active side, the transport and any in-flight capture alone.
  void ClearPool(int p) {
    const int lo = OwnLo(p), hi = OwnHi(p);
    for (int s = lo; s < hi; s++) {
      if (s == rec_slot_ && recording_) continue;   // never the active scratch (defensive)
      active_[s]   = false;
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;
      fade_target_[s] = 0.f;
      fade_phase_[s] = 0.f; fade_from_[s] = 0.f;
      dying_[s] = false; stolen_[s] = false;
      for (int b = 0; b < VESTIGE_MAX_BANDS; b++) { mb_timer_[s][b] = 0; mb_timer_sp_[s][b] = 0; }
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++)
      if (grain_slot_[g] >= lo && grain_slot_[g] < hi) grains_[g] = GrainVoice{};
    if (p == kPoolLoop) { frip_len_ = 0; frip_head_ = 0.f; frip_rec_ = 0.f; }
  }

  // -------------------------------------------------------------------------
  // LED mapping (single-colour). Stage 1:
  //   led1 (CLOCK):  always (on or bypassed: T can be tapped either way), one
  //                   40 ms flash per T, anchored to this side's most recent capture start
  //   led2 (CAPTURE/HOLD): solid while a capture is recording · slow-blink while
  //                   the buffer is held · off otherwise
  //   playing = 1 flashing / 2 off (on while capturing) · held-playing = 1
  //   flashing / 2 blink · held-stopped = 1 off / 2 blink · stopped = both off
  // (flash_ is kept but no longer set: the FS1 clear-confirm flash went with FS1.)
  // -------------------------------------------------------------------------
  void UpdateLeds(daisy::Led& led1, daisy::Led& led2) {
    const bool slow = ((blink_ / VESTIGE_BLINK_SLOW) & 1) != 0;
    const bool fast = ((blink_ / VESTIGE_BLINK_FAST) & 1) != 0;

    if (flash_ > 0) {   // confirm flash overrides (currently unused)
      flash_--;
      float f = fast ? 1.f : 0.f;
      led1.Set(f); led2.Set(f);
      return;
    }

    // LED1 = the clock: one flash per T, anchored to this side's most recent
    // capture start (the current "one"), never free-running. Always — on or
    // bypassed — since T can be tapped while bypassed too.
    if (period_ > 0) {
      const uint32_t width = (uint32_t)((float)VESTIGE_LED1_FLASH_MS * 0.001f * sr_);
      const int ls = led_slot_[pool_];
      if (pool_ == kPoolLoop && !recording_ &&
          ls >= 0 && active_[ls] && !dying_[ls] && div_[ls] >= 0) {
        // A following loop's "one" is no longer A + k*T once it has changed
        // speed: flash on its own loop time instead (integer beats of T).
        led1.Set(beat_frac_[ls] < (float)width / (float)period_ ? 1.f : 0.f);
      } else {
        const uint32_t el = sample_clock_ - led_anchor_[pool_];
        const uint32_t ph = el % (uint32_t)period_;
        led1.Set(ph < width ? 1.f : 0.f);
      }
    } else {
      led1.Set(0.f);
    }

    // LED2 = the effect's state (builder's spec, 2026-09-29):
    //   recording -> rapid flicker · held + on -> blink (2x the slow rate) ·
    //   held + bypassed -> slow blink · on -> solid · off -> dark
    const bool flicker = ((blink_ / VESTIGE_BLINK_FLICKER) & 1) != 0;
    const bool held_on = ((blink_ / VESTIGE_BLINK_HELD_ON) & 1) != 0;
    if (recording_)     led2.Set(flicker ? 1.f : 0.f);
    else if (held_)     led2.Set(engaged_ ? (held_on ? 1.f : 0.f) : (slow ? 1.f : 0.f));
    else if (engaged_)  led2.Set(1.f);
    else                led2.Set(0.f);
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  float sr_ = CT3_SAMPLE_RATE_HZ;
  float mute_inc_ = 1.f / (VESTIGE_TIMING_MUTE_MS * 0.001f * CT3_SAMPLE_RATE_HZ);

  // Grain pool (shared across all slots)
  GrainVoice       grains_[VESTIGE_GRAINS];
  const RingBuffer* grain_src_[VESTIGE_GRAINS];
  int              grain_slot_[VESTIGE_GRAINS] = {0};  // which slot emitted grain g
  // Bit g set = grain g may be active (set at every Trigger, cleared by the
  // per-sample render once the grain is inactive). Only ever a SUPERSET of the
  // active grains, so starting with all bits set is safe; the render keeps it
  // exact. Lets the per-sample render skip the idle part of the pool.
  // Two 32-bit words (not one uint64_t): __builtin_ctz is one rbit+clz on the
  // M7, __builtin_ctzll a libgcc call.
  static constexpr int kGrainLiveWords = 2;
  static_assert(VESTIGE_GRAINS > 32 && VESTIGE_GRAINS <= 64, "grain_live_ init assumes 2 words");
  static constexpr uint32_t GrainLiveAll(int w) {      // every grain of word w (no bit past the pool)
    return (VESTIGE_GRAINS - w * 32 >= 32) ? 0xFFFFFFFFu : ((1u << (VESTIGE_GRAINS - w * 32)) - 1u);
  }
  uint32_t         grain_live_[kGrainLiveWords] = {GrainLiveAll(0), GrainLiveAll(1)};
  void MarkGrainLive(int g) { grain_live_[g >> 5] |= 1u << (g & 31); }
  int              next_grain_ = 0;
  uint8_t          grain_ver_[VESTIGE_GRAINS] = {0};  // 0 = clean, 1 = K1 speed version
  int8_t           grain_view_[VESTIGE_GRAINS];       // C re-cut view it reads (slot*2+v), -1 = raw row
  uint32_t         grain_cap_drops_ = 0;              // grains refused by VESTIGE_MB_GRAIN_CAP (diag)
  uint32_t         tape_grains_ = 0;                  // grains emitted on the tape-rate path (diag)

  // Multiband granular freeze: per-slot per-band scan pointer + scheduler timer, and
  // the crossover filterbank indexed [band count-1][band]. Per-band grain length /
  // scan length / spray live in the VESTIGE_MB_{GLEN,SCAN,SPRAY} constexpr tables.
  // (VESTIGE_MB_FREEZE.)
  float  mb_scan_[VESTIGE_SLOTS][VESTIGE_MAX_BANDS]  = {};
  int    mb_timer_[VESTIGE_SLOTS][VESTIGE_MAX_BANDS] = {};
  int    mb_timer_sp_[VESTIGE_SLOTS][VESTIGE_MAX_BANDS] = {};  // K1 speed version's band timers
  int32_t pass_[VESTIGE_SLOTS] = {0};                 // clean-head wraps (half-speed parity)
  bool   ver_idle_[VESTIGE_SLOTS][2] = {};           // version emitted nothing last time (restart = instant attack)
  // K1 speed crossfade. Control -> ISR: side (-1 half / 0 / +1 double), amount.
  volatile int   k1_side_ = 0;
  volatile float deg_in_tgt_ = 1.f;                       // K4 deep-BBD input gain (target)
  float          deg_in_g_   = 1.f;                       // its smoothed value
  uint32_t (*diag_clock_)() = nullptr;                    // DIAG only (SetDiagClock)
  volatile uint32_t diag_plan_us_ = 0, diag_tick_us_ = 0, diag_proc_us_ = 0;
  volatile int      diag_gmax_ = 0;
  volatile float k1_x_    = 0.f;
  // ISR-owned: smoothed amount, the speed version's side / rate, the gain pair.
  float  sp_x_    = 0.f;
  int    sp_side_ = 0;
  float  sp_rate_ = 2.f;
  float  g_c_     = 1.f;   // clean version gain   (cos)
  float  g_sp_    = 0.f;   // speed version gain   (sin)
  float  mb_bank_coef_[VESTIGE_MAX_BANDS][VESTIGE_MAX_BANDS][5] = {};  // [N-1][band] RBJ coeffs
  int    mb_nbands_ = 3;                        // adaptive: min(voice budget, K3 chaos ramp)

  // K3 unified-engine params (per block; see the K3 macro in Controls).
  float  k3_chaos_       = 0.f;   // 0 = clean loop (CCW) → 1 = noon/freeze
  float  k3_focus_       = 0.f;   // 0 = break-up → 1 = focused freeze (collapses scatter, pins base)
  float  k3_gscale_      = VESTIGE_K3_GSCALE_CCW;  // grain-length × (long clean loop → 1 at freeze)
  bool   k3_frozen_      = false; // s>=0.5: grains clamp (freeze) vs wrap the loop seam (loop/break-up)
  int    k3_bands_chaos_ = 1;     // band count the chaos ramp wants (∩ voice budget)
  float  fwd_[VESTIGE_SLOTS] = {0.f};   // per-slot forward read head (voiced; frip uses frip_head_)

  // Two pools (sides), selected by SW1. See SwitchPool().
  enum Pool { kPoolLoop = 0, kPoolFreeze = 1 };
  int pool_ = kPoolLoop;             // the side SW1 is on (Controls writes; ISR reads)
  // Engine addressing per pool, published by Controls for the active pool and
  // frozen for the other, so a tail keeps sounding like the side it came from.
  struct PoolEngine {
    float focus;     // k3_focus_
    float gscale;    // k3_gscale_
    float amt;       // k3_amt_ (first-grain attack softening)
    float pos_frac;  // freeze_pos_frac_
    bool  frozen;    // k3_frozen_
    bool  rev;       // rev_play_ (K2 CCW half; loop side only)
    int   nbands;    // mb_nbands_ (1..VESTIGE_MAX_BANDS)
  };
  PoolEngine eng_[2];

  // Per-slot loop state. Slots [0, VESTIGE_VOICE_SLABS) voiced, then the
  // archived frippertronics slot and the loop record scratch (all loop side,
  // vestige_slab); then the freeze pool + its record scratch (freeze side,
  // vestige_freeze_slab). slab_/cap_ = the row each slot owns and its length.
  float*     slab_[VESTIGE_SLOTS] = {nullptr};
  size_t     cap_[VESTIGE_SLOTS]  = {0};
  RingBuffer ring_[VESTIGE_SLOTS];
  size_t     loop_len_[VESTIGE_SLOTS] = {0};
  size_t     play_pos_[VESTIGE_SLOTS] = {0};
  int        timer_[VESTIGE_SLOTS]    = {0};
  bool       active_[VESTIGE_SLOTS]   = {false};
  bool       dying_[VESTIGE_SLOTS]  = {false}; // voice slot fading out → free when silent
  bool       stolen_[VESTIGE_SLOTS] = {false}; // dying voice being fast-released (voice-steal)
  float      steal_inc_ = 1.f;                        // fast-release phase step for stolen voices
  volatile int rep_n_ = 0;                            // K5 CCW: number of repeats (0 = endless)
  float      dec_g_[VESTIGE_VOICE_SLABS] = {};         // per loop voice repeat level (1 set in Init / activation)
  float      dec_t_[VESTIGE_VOICE_SLABS] = {};         // its target
  float      dec_step_[VESTIGE_VOICE_SLABS] = {};      // its ramp step per sample
  float      rep_base_[VESTIGE_VOICE_SLABS] = {};      // the level the count started from
  int        rep_k_[VESTIGE_VOICE_SLABS] = {};         // repeats played since the count began
  int32_t    rep_pass_[VESTIGE_VOICE_SLABS] = {};      // last pass seen
  uint32_t   age_[VESTIGE_SLOTS]      = {0};
  float      gain_[VESTIGE_SLOTS]     = {0.f};
  bool       first_grain_[VESTIGE_SLOTS] = {false};  // next grain skips its fade-in
  uint32_t   age_counter_ = 0;

  // Per-slot loop fade envelope (K5): multiplier on each slot's summed output.
  float      fade_gain_[VESTIGE_SLOTS]   = {0.f};  // current smoothed gain
  float      fade_target_[VESTIGE_SLOTS] = {0.f};  // 0 (fade out) or 1 (fade in)
  float      fade_phase_[VESTIGE_SLOTS] = {0.f};   // progress of current fade (0..1)
  float      fade_from_[VESTIGE_SLOTS]  = {0.f};   // gain the current fade started at
  float      atk_inc_ = 1.f;                       // attack phase step (1/(atk_s*sr))
  float      rel_inc_ = 1.f;                       // release phase step (1/(rel_s*sr))
  // ARCHIVED — output routing (K6 looper volume + SW2 dry gate). Unused since
  // vestige stopped owning its output; kept for the revival path (see Process).
  float      k6_vol_    = 1.f, k6_vol_s_   = 1.f;  // looper volume target / smoothed
  float      dry_gain_  = 1.f, dry_gain_s_ = 1.f;  // dry (clean) gain target / smoothed
  // K2 CCW half: reverse loop playback (loop modes only).
  bool       rev_play_  = false;

  // Cached K3 grain-macro params (Controls → Process)
  size_t grain_len_    = VESTIGE_CCW_GRAIN_LEN;
  float  overlap_      = VESTIGE_CCW_OVERLAP;
  float  spray_        = 0.f;   // ± phasing spray (samples) around the head
  float  jitter_       = 0.f;   // scheduler timing jitter
  // K3 backward-scrub / deterministic end-freeze (Controls → ServiceSlot)
  enum K3Mode { kLooper = 0, kScrub = 1, kFreeze = 2 };
  K3Mode k3_mode_         = kLooper;
  float  scrub_back_frac_ = 0.f;  // backward scrub speed frac of hop (CCW→noon, →0 at noon)
  float  freeze_pos_frac_ = 0.f;  // freeze point: 0 = start (noon) → 1 = end (CW)
  size_t frz_base_[VESTIGE_SLOTS] = {};   // freeze: the skipped attack (window read offset)
  float  k3_amt_          = 0.f;  // raw K3 (first-grain attack softening toward freeze)

  // Topology. target_voices_ comes from SW1 (1 / 6 / 1).
  int  target_voices_ = 1;
  int  sw1_prev_      = -1;     // SW1 edge detect (close a capture on mode change)
  // ARCHIVED — frippertronics: never set true since rework stage 0, so all the
  // frip state below and every `if (fripp_mode_)` branch is dead but compiled.
  // Revive via the K1 block in Controls() (see the ARCHIVED note there).
  bool fripp_mode_    = false;
  size_t frip_len_    = 0;
  float  frip_decay_  = 1.f;
  // Single frippertronics loop head (per-sample): record writes to it and grains
  // read from it, so overdubs land exactly where they were played. K3 sets its
  // motion (forward / backward scrub / frozen). Voiced slots keep their own
  // per-emit play_pos_ — this is frip-only.
  float  frip_head_   = 0.f;   // playback tap (K3-scanned: loop / scrub / freeze)
  float  frip_rec_    = 0.f;   // record phase (always forward; overdub writes here)
  size_t frip_rec_prev_idx_ = 0;  // last integer cell written (tape resample cursor)
  float  frip_in_acc_ = 0.f;   // input accumulator for box-averaged write (pitch-down)
  int    frip_in_cnt_ = 0;
  float  frip_in_prev_ = 0.f;  // previous input sample (write-side linear ramp, pitch-up)
  // Frippertronics overdub declick: ramp the summed input in/out over a few ms
  // at record engage/disengage so the sound-on-sound add has no hard step.
  float  frip_od_gain_    = 0.f;   // current overdub input gain (0..1)
  float  frip_od_target_  = 0.f;   // 1 = fading in, 0 = fading out
  float  frip_od_coef_    = 1.f;   // per-sample one-pole ramp coef (set in Init)
  volatile bool frip_stop_pending_ = false;  // deferred commit: wait for fade-out

  // Tape varispeed (K4): pitch + speed coupled. Target set in Controls, glided
  // per-sample to pitch_rate_s_ (see Process); read by the grain engine + heads.
  float pitch_rate_   = 1.f;   // target rate (1 = unity)
  float pitch_rate_s_ = 1.f;   // smoothed (audio-rate tape glide)

  // Degradation colour (K4): mnemonic's BBD/Tape engine, applied to the wet
  // looper output. Depth set from K4 in Controls; per-sample in Process.
  MnemDegrade degrade_;
  // Post-grain warble: short modulated delay line carrying the engine's tape/BBD
  // pitch modulation (wow/flutter/snag/drift) on the continuous looper output.
  RingBuffer warble_ring_;
  float      warble_base_ = 0.f;   // fixed base delay (samples) = tap centre
  float      warble_int_  = 0.f;   // leaky-integrated cents → sample displacement

  // Recording
  volatile bool recording_ = false;
  volatile bool rec_full_  = false;
  int    rec_slot_ = 0;
  size_t rec_idx_  = 0;
  size_t max_loop_len_ = VESTIGE_LOOP_MAX_SAMPLES;  // K4 capture-length ceiling (samples)
  bool   auto_rearm_block_ = false;  // ceiling stop: hold off until env falls
  // Seam-crossfade overhang: loop end is set at EndRecording, then we record
  // `overhang_left_` more samples before committing (commit_pending_).
  volatile bool commit_pending_ = false;
  size_t pending_len_   = 0;
  int    overhang_left_ = 0;

  // Transport / capture (FS2). engaged_ = capture + playback on (off at power-up,
  // as before: nothing captured until FS2). held_ = buffer hold (no resampling,
  // buffers survive switching off). hold_latched_ = the hold fired this press.
  bool     muted_        = false;
  bool     engaged_      = false;
  bool     held_         = false;
  bool     hold_latched_ = false;
  int      flash_        = 0;
  uint32_t silence_since_= 0;
  float    auto_thresh_  = VESTIGE_AUTO_THRESH;
  float    thresh_cfg_   = -1.f;                 // > 0 overrides VESTIGE_AUTO_THRESH (host tests)
  float    env_          = 0.f;
  float    env_gate_     = 0.f;          // the gate's meter (VESTIGE_GATE_ENV_MODE)
  float    gate_rel_coef_ = 0.f;         // its fall coefficient (mode 1), set in Init

  int blink_ = 0;

  // Stage-2 capture machine (see IsrCapture). ISR-owned unless noted.
  volatile bool cap_allow_ = false;    // control -> ISR: a capture may start
  volatile bool drop_req_  = false;    // control -> ISR: drop in-flight + pending captures
  volatile bool end_req_   = false;    // control -> ISR: end the capture at the next sample
  volatile int  arm_slot_  = -1;       // control reserves (when -1), ISR consumes
  bool     rearm_block_ = false;       // ceiling stop: hold off until env falls (or an onset)
  float    onset_slow_  = 0.f;         // onset detector: slow envelope baseline
  int      onset_refr_  = 0;           // samples until another onset can fire
  int      onset_refr_len_ = 2400;     // refractory, samples (set from sr_ in Init)
  uint32_t onset_count_  = 0;          // onsets detected (diag)
  uint32_t onset_starts_ = 0;          // captures started by an onset lifting the block (diag)
  size_t   rec_stop_    = 0;           // record up to here (loop end + overhang); 0 = end unknown
  size_t   cap_ceil_    = 0;           // this capture's ceiling (latched T, or the freeze window)
  uint32_t sil_run_     = 0;           // consecutive below-close samples
  size_t   sil_onset_   = 0;           // index where the current silence began
  size_t   last_loud_   = 0;           // last index where the FAST meter reached the close level
  uint32_t release_samples_ = (uint32_t)(VESTIGE_AUTO_RELEASE_MS * 48);  // set from sr_ in Init
  uint32_t cap_start_[VESTIGE_SLOTS] = {0};   // capture start sample = its grid's "one"
  size_t   cap_T_[VESTIGE_SLOTS]     = {0};   // T latched at that start (loop side)
  size_t   cap_raw_[VESTIGE_SLOTS]   = {0};   // raw length before quantising (diagnostics)
  size_t   cap_len_[VESTIGE_SLOTS]   = {0};   // decided (quantised) length; 0 = not yet
  bool     pend_[VESTIGE_SLOTS]      = {false}; // decided, waiting for its grid point
  uint32_t act_at_[VESTIGE_SLOTS]    = {0};   // sample it starts playing
  int      npend_ = 0;
  size_t   gfill_base_[VESTIGE_SLOTS] = {0};  // loop length the guard job is for
  size_t   gfill_k_[VESTIGE_SLOTS]    = {0};  // next guard cell (offset past the base)
  size_t   gfill_src_[VESTIGE_SLOTS]  = {0};  // its source cell (k mod base)
  size_t   gfill_end_[VESTIGE_SLOTS]  = {0};  // guard extent
  size_t   gready_[VESTIGE_SLOTS]     = {0};  // extent a grain can read (reverse start gate)
  bool     gfill_idle_ = true;                // no guard job pending (fast path)
  uint32_t act_phase_[VESTIGE_SLOTS] = {0}; // grid phase it entered at (0 = on its "one")
  // Diagnostics (host test): the last activation.
  int      last_act_slot_ = -1;
  uint32_t last_act_at_   = 0;
  uint32_t cap_decide_at_[VESTIGE_SLOTS] = {};   // diagnostics: sample the capture's end was decided
  int      fill_budget_max_ = 0;                 // diagnostics: largest guard-copy speed used

  // ---- DIAG (CT3_DIAG builds only): capture-gate event log ------------------
  // The audio thread pushes fixed-size records into a single-producer /
  // single-consumer ring; the main loop pops ONE per 10 ms tick and prints it
  // (the logger drops bursts). Observation only: nothing here feeds back into
  // the gate. In a normal build CT3_DIAG is constexpr false, every push is dead
  // code and the ring is one element.
 public:
  struct GateDiag { char kind; char why; uint8_t flag; uint32_t t; float a, b, c, d; uint32_t u, w; };
  bool DiagPop(GateDiag& out) {
    const uint32_t h = diag_head_;
    if (diag_tail_ == h) return false;
    out = diag_ring_[diag_tail_ & (kDiagN - 1)];
    diag_tail_ = diag_tail_ + 1;
    return true;
  }
  uint32_t DiagDrops() const { return diag_drops_; }
  // DIAG timing (a microsecond clock from the shell; nullptr = off). The
  // Take* readers return the peak since the last read and reset it.
  void     SetDiagClock(uint32_t (*f)()) { diag_clock_ = f; }
  uint32_t TakePlanUs() { const uint32_t v = diag_plan_us_; diag_plan_us_ = 0; return v; }
  uint32_t TakeTickUs() { const uint32_t v = diag_tick_us_; diag_tick_us_ = 0; return v; }
  uint32_t TakeProcUs() { const uint32_t v = diag_proc_us_; diag_proc_us_ = 0; return v; }
  int      TakeGrainMax() { const int v = diag_gmax_; diag_gmax_ = 0; return v; }
  int      DiagLive() const { int c = 0; for (int q = 0; q < VESTIGE_VOICE_SLABS; q++) if (active_[q] && !dying_[q]) c++; return c; }
  int      DiagK1() const { return k1_side_; }
  float    DiagSpX() const { return sp_x_; }
  uint32_t DiagCapDrops() const { return grain_cap_drops_; }
  uint32_t DiagPoolFull() const { return pool_full_; }
  uint32_t DiagJumps()    const { return timing_trigs_; }
  uint32_t DiagPlans()    const { return timing_patterns_; }
  float    DiagK3()       const { return err_level_[kErrTiming]; }
  float    DiagK4()    const { return diag_k4_; }
  float    DiagOpen()  const { return auto_thresh_; }
  size_t   DiagT()     const { return period_; }
 private:
  static constexpr uint32_t kDiagN = CT3_DIAG ? 512u : 1u;   // power of two
  GateDiag diag_ring_[kDiagN] = {};
  volatile uint32_t diag_head_ = 0, diag_tail_ = 0, diag_drops_ = 0;
  float    diag_k4_ = 0.f;
  char     diag_why_ = '?', diag_end_ = '?';
  uint32_t diag_trace_ = 0;
  void DiagPush(const GateDiag& r) {
    const uint32_t h = diag_head_;
    if (h - diag_tail_ >= kDiagN) { diag_drops_ = diag_drops_ + 1; return; }
    diag_ring_[h & (kDiagN - 1)] = r;
    diag_head_ = h + 1;
  }
  uint32_t act_count_     = 0;

  // ---- DIAG (CT3_DIAG builds only): the K3 mode-1 rhythm log ----------------
  // Whenever the rhythm that plays changes — engine 0: its (ks, kr, kd);
  // engine 1: its table row (+ kd) or a new loop; engine 2: its side + row — ONE
  // line is prepared here in the main loop (Controls) once the change has held
  // for kRhySettleTicks control ticks (~250 ms: turning the knob does not
  // flood), and the shell sends it in a main-loop iteration of its own
  // (DiagRhyLine / DiagRhyDone). Also forced once when K3 enters the CCW half
  // and when SW2 switches to UP. Each line starts with the rhythm's NUMBER,
  // prefixed by its K3 side (CCW#n / CW#n): engine 0 numbers its distinct
  // (ks, kr, kd) combinations CCW#1 (just past the dead zone) .. CCW#N (full
  // CCW) in travel order; engines 1 + 2 = table row + 1 (engine 2: CCW#1 ..
  // CCW#ROWS and CW#1 .. CW#ROWS_CW, numbered per side, each from noon's edge
  // outwards; its decimates are part of the row).
  // At start the whole numbered list is sent once ("VS RHY LIST", a header
  // with the fixed rotations, then one line per number — engine 2: CCW#1..,
  // then CW#1.. — one per main-loop iteration; a changed-rhythm line goes
  // first when both are pending).
  // The pattern is the engine's own BASE from running step 0, 3 bars of 16:
  // s stutter · _ rest · d decimate · D decimate on a stutter · - plain.
  // Observation only: nothing here touches the audio thread's state or RNG.
 public:
  // The 3-bar base pattern (|16|16|16|, 52 chars + NUL) for depth u > 0 at
  // stutter / rest / decimate rotations srot / rrot / drot (engine 1: only
  // drot is used, its patterns carry their own fixed rotations; engine 2:
  // none, its decimates are the row's overlap mask).
  // side: which K3 half's table (engine 2; engines 0 + 1: kRhyCcw only).
  static void RhyRenderBars(char* out, float u, int srot, int rrot, int drot, int side = kRhyCcw) {
    const int Nd = VESTIGE_TIMING_RHY_D_SLOTS, kd = RhyKd(u), row = RhyRow(u, side);
    const bool half = (VESTIGE_TIMING_RHY_ENGINE != 0) && RhyTab(side)[row].grid == kRhyGridHalf;
    const int per = half ? 8 : 4;                           // the decimate's off-beat period
    int o = 0; out[o++] = '|';
    for (int t = 0; t < 48; t++) {
      const int c = (VESTIGE_TIMING_RHY_ENGINE == 0) ? RhyPolyBase(t, u, (uint32_t)srot, (uint32_t)rrot, side) : RhyTableBase(t, u, side);
      const bool dec = (VESTIGE_TIMING_RHY_ENGINE == 2) ? (c == 1 && RhyDecAt(t, row, side))
                     : c != 2 && t % per == per / 2 && RhyHit((t / per) % Nd, kd, Nd, RhyMod(drot, Nd));
      out[o++] = (c == 2) ? '_' : dec ? (c == 1 ? 'D' : 'd') : (c == 1 ? 's' : '-');
      if (t % 16 == 15) out[o++] = '|';
    }
    out[o] = 0;
  }
  // The rhythm's key at depth u > 0: engine 0 (ks, kr, kd), engine 1 (row, 0, kd), engine 2 (row, 0, 0) of side `side`.
  static void RhyKeyOf(float u, int& a, int& b, int& kd, int side = kRhyCcw) {
    a  = (VESTIGE_TIMING_RHY_ENGINE == 0) ? RhyPolyKs(u, side) : RhyRow(u, side);
    b  = (VESTIGE_TIMING_RHY_ENGINE == 0) ? RhyPolyKr(u, side) * 4 + (int)RhyTempoStep(u) : 0;   // (engine 0: + its tempo)
    kd = RhyKd(u);
  }
  // The numbered list, sampled over each half's travel: u = i / kRhyListSteps
  // (exact binary fractions, so no float-rounding sliver splits a
  // combination); engine 2: the CCW side's rows, then the CW side's.
  void RhyBuildList() {
    rl_n_ = 0;
    const int sides = (VESTIGE_TIMING_RHY_ENGINE != 1) ? 2 : 1;
    for (int side = kRhyCcw; side < sides; side++) {
      const int first = rl_n_;
      for (int i = 1; i <= kRhyListSteps; i++) {
        const float u = (float)i / (float)kRhyListSteps;
        int a, b, kd; RhyKeyOf(u, a, b, kd, side);
        if (rl_n_ > first && rl_[rl_n_ - 1].a == a && rl_[rl_n_ - 1].b == b && rl_[rl_n_ - 1].kd == kd) { rl_[rl_n_ - 1].u1 = u; continue; }
        if (rl_n_ >= (int)(sizeof rl_ / sizeof rl_[0])) break;
        RhyEntry& e = rl_[rl_n_++];
        e.a = a; e.b = b; e.kd = kd; e.side = side; e.u0 = (float)(i - 1) / (float)kRhyListSteps; e.u1 = u;
        e.num = (VESTIGE_TIMING_RHY_ENGINE == 0) ? rl_n_ - first : a + 1;
      }
    }
    rl_i_ = 0; rl_ready_ = false;
  }
  // The number #n of the rhythm at depth u > 0 on side `side` (0 = none).
  int RhyNum(float u, int side = kRhyCcw) const {
    if (VESTIGE_TIMING_RHY_ENGINE != 0) return RhyRow(u, side) + 1;
    int a, b, kd; RhyKeyOf(u, a, b, kd, side);
    for (int i = 0; i < rl_n_; i++) if (rl_[i].side == side && rl_[i].a == a && rl_[i].b == b && rl_[i].kd == kd) return rl_[i].num;
    for (int i = 0; i < rl_n_; i++) if (rl_[i].side == side && u <= rl_[i].u1) return rl_[i].num;   // (a float sliver: by range)
    return 0;
  }
  int RhyListN() const { return rl_n_; }
  const char* DiagRhyLine() const { return rd_ready_ ? rd_line_ : rl_ready_ ? rl_line_ : nullptr; }   // pending line (nullptr = none)
  void        DiagRhyDone()       { if (rd_ready_) rd_ready_ = false; else rl_ready_ = false; }      // (the one DiagRhyLine returned)
 private:
  static constexpr int kRhySettleTicks = 25;              // x ~10 ms control ticks
  static constexpr int kRhyLineN = 176;                   // < the shell's DiagLine buffer (192)
  static constexpr int kRhyListMax = 48;                  // list entries (engine 0: 8, engine 1: ~17, engine 2: one per row of both tables)
  static_assert(VESTIGE_TIMING_RHY_ENGINE != 2 || kRhyListMax >= VESTIGE_TIMING_RHY_ROWS + VESTIGE_TIMING_RHY_ROWS_CW,
                "DIAG rhythm list: one entry per interlock row (CCW + CW)");
  static constexpr int kRhyListSteps = 4096;              // list sampling of the depth
  struct RhyKey {
    int on, side, a, b, kd, slot; uint32_t loop;
    bool operator==(const RhyKey& o) const { return on == o.on && side == o.side && a == o.a && b == o.b && kd == o.kd && slot == o.slot && loop == o.loop; }
  };
  struct RhyEntry { int num, side, a, b, kd; float u0, u1; };
  static const char* RhySideName(int side) { return side == kRhyCw ? "CW" : "CCW"; }
  static void RhyFx3(char* b, size_t n, float x) {        // 0.123 (newlib-nano has no %f)
    if (!(x >= 0.f)) x = 0.f;
    int ip = (int)x, fp = (int)((x - (float)ip) * 1000.f + 0.5f);
    if (fp >= 1000) { ip++; fp -= 1000; }
    snprintf(b, n, "%d.%03d", ip, fp);
  }
  // Engine 2: the exact values of a row's layers, as the log prints them:
  // "S=<A> k<k>/<n> r<rot> | R=<duck|neg> <B> k<k>/<n> r<rot> | D=overlaps".
  // A voice without a name in the row's meta is read from its pattern (a
  // Euclid: E k/n r; a string: str <hits>/<length> r0; none: none k0/0 r0).
  static void RhyVoiceOf(const VestigeRhyVoice& m, const VestigeRhyPat& p, const char*& nm, int& k, int& n, int& rot) {
    if (m.name) { nm = m.name; k = m.k; n = m.n; rot = m.rot; return; }
    k = n = rot = 0; nm = "none";
    if (p.kind == kRhyEuc) { nm = "E"; k = p.k; n = p.n; rot = p.rot; }
    else if (p.kind == kRhyStr && p.str) { nm = "str"; n = p.len; for (int i = 0; i < p.len; i++) k += p.str[i] == 'x'; }
    else if (p.kind == kRhyTrem) nm = "trem";
  }
  static void RhyLayersFmt(char* b, size_t n, int row, int side = kRhyCcw) {
    const VestigeRhyRow& R = RhyTab(side)[row];
    const char *an, *bn; int ak, aN, ar, bk, bN, br;
    RhyVoiceOf(R.meta.a, R.stut, an, ak, aN, ar);
    RhyVoiceOf(R.meta.b, R.rest, bn, bk, bN, br);          // (no B in the meta: the rest pattern itself)
    const char* pr = R.meta.pr == kRhyPrDuck ? "duck" : R.meta.pr == kRhyPrNeg ? "neg" : "-";
    snprintf(b, n, "S=%s k%d/%d r%d | R=%s %s k%d/%d r%d | D=overlaps", an, ak, aN, ar, pr, bn, bk, bN, br);
  }
  // K3's (remapped) reading at depth u on side `side`: the inverse of Controls' mapping.
  static float RhyK3At(float u, int side = kRhyCcw) {
    constexpr float kNoon = (0.5f - KNOB_MIN) / (KNOB_MAX - KNOB_MIN);
    if (side == kRhyCw) return kNoon + VESTIGE_K3_DEADZONE + u * ((1.f - kNoon) - VESTIGE_K3_DEADZONE);
    return kNoon - VESTIGE_K3_DEADZONE - u * (kNoon - VESTIGE_K3_DEADZONE);
  }
  // The next list line into its own pending slot (rl_i_ 0 = the header).
  void RhyListNext() {
    if (rl_ready_ || rl_i_ > rl_n_) return;
    char line[kRhyLineN]; const size_t n = sizeof line;
    if (rl_i_ == 0) {
      if (VESTIGE_TIMING_RHY_ENGINE == 0)
        snprintf(line, n, VESTIGE_TIMING_RHY_ROT_RANDOM
                 ? "VS RHY LIST engine=0 n=%d ccw=%d:%d cw=%d:%d var=%s (rotations drawn per loop; list at rot 0)"
                 : "VS RHY LIST engine=0 n=%d ccw=%d:%d cw=%d:%d var=%s (fixed rotations)",
                 rl_n_, VESTIGE_TIMING_RHY_S_CYCLE, VESTIGE_TIMING_RHY_R_CYCLE, VESTIGE_TIMING_RHY_CW_S_CYCLE,
                 VESTIGE_TIMING_RHY_CW_R_CYCLE, VESTIGE_TIMING_RHY_VAR_PROB > 0.f ? "on" : "off");
      else if (VESTIGE_TIMING_RHY_ENGINE == 2)            // (decimates = each row's A/B overlaps)
        snprintf(line, n, "VS RHY LIST engine=2 n=%d ccw=%d cw=%d var=%s (fixed for every loop; D = A+B overlaps)",
                 rl_n_, VESTIGE_TIMING_RHY_ROWS, VESTIGE_TIMING_RHY_ROWS_CW, VESTIGE_TIMING_RHY_VAR_PROB > 0.f ? "on" : "off");
      else
        snprintf(line, n, "VS RHY LIST engine=1 n=%d rows=%d drot=%d var=%s (fixed for every loop)",
                 rl_n_, VESTIGE_TIMING_RHY_ROWS, kRhyDRot, VESTIGE_TIMING_RHY_VAR_PROB > 0.f ? "on" : "off");
    } else {
      const RhyEntry& e = rl_[rl_i_ - 1];
      const char* sn = RhySideName(e.side);
      char u0[24], u1[24], k0[24], k1[24], pat[56];
      RhyFx3(u0, sizeof u0, e.u0); RhyFx3(u1, sizeof u1, e.u1);
      RhyFx3(k0, sizeof k0, RhyK3At(e.u0, e.side)); RhyFx3(k1, sizeof k1, RhyK3At(e.u1, e.side));
      const bool rnd = VESTIGE_TIMING_RHY_ENGINE == 0 && VESTIGE_TIMING_RHY_ROT_RANDOM;
      RhyRenderBars(pat, 0.5f * (e.u0 + e.u1), rnd ? 0 : kRhySRot, rnd ? 0 : kRhyRRot, rnd ? 0 : kRhyDRot, e.side);
      if (VESTIGE_TIMING_RHY_ENGINE == 0)
        snprintf(line, n, "VS RHY LIST %s#%d %s ks=%d kr=%d kd=%d u=%s..%s k3=%s..%s %s", sn, e.num,
                 RhyTempoName(0.5f * (e.u0 + e.u1)), e.a, e.b / 4, e.kd, u0, u1, k0, k1, pat);
      else if (VESTIGE_TIMING_RHY_ENGINE == 2) {
        char ly[96]; RhyLayersFmt(ly, sizeof ly, e.a, e.side);
        snprintf(line, n, "VS RHY LIST %s#%d %s k3=%s..%s %s", sn, e.num, ly, k0, k1, pat);
      } else
        snprintf(line, n, "VS RHY LIST %s#%d row=%d kd=%d u=%s..%s k3=%s..%s %s", sn, e.num, e.a, e.kd, u0, u1, k0, k1, pat);
    }
    memcpy(rl_line_, line, sizeof rl_line_); rl_line_[sizeof rl_line_ - 1] = 0;
    rl_i_++; rl_ready_ = true;
  }
  void DiagRhyTick(float k3, int sw2) {
    RhyListNext();                                        // the numbered list, once (its own slot)
    const float u = rhy_level_;
    const int side = (VESTIGE_TIMING_RHY_ENGINE != 1) ? rhy_side_ : kRhyCcw;
    const int slot = rhy_act_slot_;
    RhyKey k{};
    k.on = u > 0.f;
    if (k.on) {
      k.side = side;                                      // (CCW -> CW: a new line)
      RhyKeyOf(u, k.a, k.b, k.kd, side);
      // Engines 0 + 2: a new loop plays the same rhythm (fixed rotations) - no line.
      // (engine 0 with RHY_ROT_RANDOM: a new loop = new rotations - a new line)
      if (VESTIGE_TIMING_RHY_ENGINE == 1 || (VESTIGE_TIMING_RHY_ENGINE == 0 && VESTIGE_TIMING_RHY_ROT_RANDOM)) { k.slot = slot; k.loop = rhy_act_n_; }
    }
    const bool force = (k.on && !rd_cur_.on) || (sw2 == 0 && rd_sw2_ != 0);
    rd_sw2_ = sw2;
    if (!(k == rd_cur_) || force) { rd_cur_ = k; rd_settle_ = kRhySettleTicks; rd_force_ = rd_force_ || force; }
    if (rd_settle_ <= 0 || --rd_settle_ > 0) return;
    if (rd_cur_ == rd_logged_ && !rd_force_) return;
    rd_logged_ = rd_cur_; rd_force_ = false;
    char k3s[24], us[24], line[kRhyLineN];
    RhyFx3(k3s, sizeof k3s, k3); RhyFx3(us, sizeof us, u);
    const unsigned tms = (unsigned)daisy::System::GetNow();
    if (!k.on) { snprintf(line, sizeof line, "VS RHY t=%u k3=%s sw2=%d off", tms, k3s, sw2); DiagRhyPost(line); return; }
    const int num = RhyNum(u, side);
    const char* sn = RhySideName(side);
    char pat[56];
    if (VESTIGE_TIMING_RHY_ENGINE == 0) {
      const bool have = slot >= 0 && slot < VESTIGE_VOICE_SLABS;
      const int Ns = RhyPolyNs(side), Nr = RhyPolyNr(side), Nd = VESTIGE_TIMING_RHY_D_SLOTS;
      const int sr = !VESTIGE_TIMING_RHY_ROT_RANDOM ? kRhySRot : !have ? 0
                   : VESTIGE_TIMING_RHY_ROT_ON1 ? (int)RhyOn1Rot(rhy_rot_[slot], u, side) : (int)RhyRotOf(rhy_rot_[slot], Ns);
      int rr = !VESTIGE_TIMING_RHY_ROT_RANDOM ? kRhyRRot : !have ? 0
             : VESTIGE_TIMING_RHY_AUDIBLE_RESTS ? (int)RhyRestRot(rhy_rrot_[slot], u, (uint32_t)sr, side) : (int)RhyRotOf(rhy_rrot_[slot], Nr);
      if (VESTIGE_TIMING_RHY_ROT_RANDOM && VESTIGE_TIMING_RHY_NO_FLAM) rr = (int)RhyNoFlamRot(u, (uint32_t)sr, (uint32_t)rr, side);
      const int dr = VESTIGE_TIMING_RHY_ROT_RANDOM ? (have ? (int)((rhy_rot_[slot] >> 8) % (uint32_t)Nd) : 0) : kRhyDRot;
      RhyRenderBars(pat, u, sr, rr, dr, side);
      snprintf(line, sizeof line, "VS RHY %s#%d t=%u k3=%s u=%s %s E(%d,%d)r%d E(%d,%d)r%d kd=%d dr%d %s",
               sn, num, tms, k3s, us, RhyTempoName(u), k.a, Ns, sr, k.b / 4, Nr, rr, k.kd, dr, pat);
    } else if (VESTIGE_TIMING_RHY_ENGINE == 2) {
      RhyRenderBars(pat, u, 0, 0, 0, side);
      char ly[96]; RhyLayersFmt(ly, sizeof ly, RhyRow(u, side), side);
      snprintf(line, sizeof line, "VS RHY %s#%d t=%u k3=%s u=%s %s %s", sn, num, tms, k3s, us, ly, pat);
    } else {
      RhyRenderBars(pat, u, 0, 0, kRhyDRot);
      snprintf(line, sizeof line, "VS RHY %s#%d t=%u k3=%s u=%s row=%d kd=%d drot=%d slot=%d %s",
               sn, num, tms, k3s, us, k.a, k.kd, kRhyDRot, slot, pat);
    }
    DiagRhyPost(line);
  }
  void DiagRhyPost(const char* line) {                    // into the pending slot (DIAG builds: kRhyLineN bytes)
    memcpy(rd_line_, line, sizeof rd_line_); rd_line_[sizeof rd_line_ - 1] = 0;
    rd_ready_ = true;
  }
  volatile int      rhy_act_slot_ = -1;                   // ISR-written: newest loop voice
  volatile uint32_t rhy_act_n_    = 0;                    //  and the loop count
  RhyKey   rd_cur_{}, rd_logged_{};
  int      rd_settle_ = 0, rd_sw2_ = -1;
  bool     rd_force_ = false, rd_ready_ = false;
  char     rd_line_[CT3_DIAG ? kRhyLineN : 1] = {};
  RhyEntry rl_[CT3_DIAG ? kRhyListMax : 1] = {};          // the numbered list (RhyBuildList)
  int      rl_n_ = 0, rl_i_ = 0;                          //  its length, the next line to send (0 = header)
  bool     rl_ready_ = false;
  char     rl_line_[CT3_DIAG ? kRhyLineN : 1] = {};

  // C re-cut (ISR-owned). play_len_ = the pass length in effect (== loop_len_
  // unless re-cut); cur_view_ = view new grains bind to (-1 = the raw row).
  size_t    play_len_[VESTIGE_VOICE_SLABS];
  int       cur_view_[VESTIGE_VOICE_SLABS];
  GrainView views_[VESTIGE_VOICE_SLABS][2];
  size_t    rc_want_[VESTIGE_VOICE_SLABS];      // length wanted now (Boundary(d, T) or the stored length)
  size_t    rc_le_[VESTIGE_VOICE_SLABS];        // length the spare view is built / being built for
  int       rc_view_[VESTIGE_VOICE_SLABS];      // which view that is
  size_t    rc_k_[VESTIGE_VOICE_SLABS];         // build progress (guard cells)
  bool      rc_building_[VESTIGE_VOICE_SLABS];
  bool      rc_ready_[VESTIGE_VOICE_SLABS];
  bool      rc_idle_ = true;
  uint32_t  rec_clock_now_ = 0;                 // sample number being processed (diag timestamps)
  uint32_t  last_recut_at_[VESTIGE_VOICE_SLABS] = {0};
  uint32_t  recut_applied_ = 0, recut_slip_build_ = 0, recut_slip_parity_ = 0;   // diag
  bool      restart_[VESTIGE_VOICE_SLABS][2] = {};  // stream restarts pending after a C change (clean / speed)


  // Loops follow T (SW2 temporary selector). follow_mode_: control -> ISR.
  enum FollowMode { kFollowTape = 0, kFollowStretch = 1, kFollowRecut = 2 };
  // Stage 2.5 error editor state (control thread writes, audio thread reads the
  // levels; each is one word, a torn read is at worst one tick old).
  enum ErrType { kErrTiming = 0, kErrCondition = 1, kErrPlayback = 2, kErrTypes = 3 };
  // Stage 3 TIMING retrigger (ISR-owned, per loop voice slot).
  static constexpr int kTimingEvents = VESTIGE_TIMING_MAX_EVENTS > VESTIGE_TIMING_MAX_STEPS
                                    ? VESTIGE_TIMING_MAX_EVENTS : VESTIGE_TIMING_MAX_STEPS;
  float    trig_off_[VESTIGE_VOICE_SLABS]     = {0.f};   // read offset in effect (0 = on the timeline)
  float    trig_pos_[VESTIGE_VOICE_SLABS][kTimingEvents] = {};   // this pass's hit points (material)
  int      trig_cnt_[VESTIGE_VOICE_SLABS]     = {0};     // hits after step 0 in this pass
  int      trig_next_[VESTIGE_VOICE_SLABS]    = {0};     // next hit to play
  bool     trig_rev_[VESTIGE_VOICE_SLABS]     = {false};
  uint32_t timing_mask_[VESTIGE_TIMING_PATTERNS] = {0};  // Bjorklund masks (bit i = step i), built at init
  int      cur_pat_[VESTIGE_VOICE_SLABS]      = {0};     // this pass's pattern (-1 = none) (diag)
  int      cur_rot_[VESTIGE_VOICE_SLABS]      = {0};     // its rotation (the hit it starts on) (diag)
  uint32_t cur_mask_[VESTIGE_VOICE_SLABS]     = {0};     // this pass's mask, variation applied (diag)
  enum TimingVar { kVarNone = 0, kVarAdd = 1, kVarDrop = 2, kVarRot = 3 };
  int      cur_var_[VESTIGE_VOICE_SLABS]      = {0};     // this pass's variation (kVar*) (diag)
  uint32_t base_mask_[VESTIGE_VOICE_SLABS]    = {0};     // the base pattern in the loop's rotation (diag)
  int      base_rot_[VESTIGE_VOICE_SLABS]     = {0};     // the loop's rotation for the base pattern (diag)
  uint32_t rot_seed_[VESTIGE_VOICE_SLABS]     = {0};     // drawn at loop start; rotation = seed % hits
  bool     var_last_[VESTIGE_VOICE_SLABS]     = {false}; // the previous pass was a variation
  uint32_t timing_patterns_ = 0, timing_skipped_ = 0;    // diag
  uint32_t timing_vars_ = 0, timing_fallbacks_ = 0;      // diag
  size_t   trig_L_[VESTIGE_VOICE_SLABS]       = {0};     // the play length the instance was laid out for
  int32_t  trig_start_[VESTIGE_VOICE_SLABS]   = {0};     // pass_ of the instance's first pass
  uint32_t timing_inplace_ = 0, timing_aborts_ = 0;      // diag: hits with no jump / instances dropped
  // Timing mode 1 (slices), per loop voice slot.
  int8_t   sl_prio_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_SLICE_TIERS][VESTIGE_TIMING_SLICE_MAX] = {};  // step priority (drawn at loop start)
  int8_t   sl_repl_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_SLICE_TIERS][VESTIGE_TIMING_SLICE_MAX] = {};  // replacement slice per step
  int8_t   sl_order_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_SLICE_MAX] = {};   // this pass's slice per step (diag)
  int8_t   sl_arr_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_SLICE_MAX]   = {};   // the arrangement before variation (diag)
  float    sl_off_[VESTIGE_VOICE_SLABS][kTimingEvents]   = {};   // read offset per jump
  // Timing mode 2 (pass memory), per loop voice slot.
  TimingFig pm_fig_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_MEM_PASSES][VESTIGE_TIMING_MEM_FIGS] = {};   // the remembered passes
  int      pm_nf_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_MEM_PASSES] = {};   // figures in each
  int      pm_n_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_MEM_PASSES]  = {};   // the step count they were placed on
  int      pm_cur_[VESTIGE_VOICE_SLABS]      = {0};      // the memory this pass plays
  int8_t   pm_mute_ev_[VESTIGE_VOICE_SLABS][kTimingEvents] = {};   // per event: -1 jump only, 0 unmute, 1 mute
  int8_t   pm_rev_ev_[VESTIGE_VOICE_SLABS][kTimingEvents]  = {};   // per event: -1 none, 1 reverse on (sl_off_ = M), 0 off
  bool     lrev_[VESTIGE_VOICE_SLABS]        = {false};  // a TIMING reverse span is playing: read = lrev_m_ - fwd_
  float    lrev_m_[VESTIGE_VOICE_SLABS]      = {0.f};
  bool     pm_jump_ev_[VESTIGE_VOICE_SLABS][kTimingEvents] = {};   // per event: the read jumps
  float    mute_d_[VESTIGE_VOICE_SLABS]      = {0.f};    // rest/break depth (0 = open, 1 = silent)
  float    mute_dt_[VESTIGE_VOICE_SLABS]     = {0.f};    // its target
  float    decim_d_[VESTIGE_VOICE_SLABS]     = {0.f};    // sample-rate reduction mix (0 = clean)
  float    decim_dt_[VESTIGE_VOICE_SLABS]    = {0.f};    // its target
  int      decim_n_[VESTIGE_VOICE_SLABS]     = {0};      // hold length (samples)
  int      decim_c_[VESTIGE_VOICE_SLABS]     = {0};      // samples since the last hold
  float    decim_h_[VESTIGE_VOICE_SLABS]     = {0.f};    // the held sample
  float    decim_z1_[VESTIGE_VOICE_SLABS]    = {0.f};    // its low-pass state
  float    decim_z2_[VESTIGE_VOICE_SLABS]    = {0.f};
  int      decim_k_[VESTIGE_VOICE_SLABS]     = {0};      // its factor's table index (-1 = no LP)
  bool     decim_init_[VESTIGE_VOICE_SLABS]  = {false};  // start the hold + LP on the next sample
  float    decim_lp_[VESTIGE_TIMING_DECIM_N][5] = {};     // LP coefs per factor (MBSetLP)
  uint32_t timing_edits_ = 0;                             // diag: memory edits
  // Timing mode 3 (layers), per loop voice slot.
  LineHit  ln_hit_[VESTIGE_VOICE_SLABS][kErrTypes][VESTIGE_TIMING_LINE_MAX_CELLS / 2] = {};
  int      ln_nh_[VESTIGE_VOICE_SLABS][kErrTypes] = {};
  int      ln_cells_[VESTIGE_VOICE_SLABS]    = {0};      // the line length the hits were placed on
  int      ln_pass_[VESTIGE_VOICE_SLABS]     = {0};      // which pass of the line plays
  int      ln_ops_[VESTIGE_VOICE_SLABS][kErrTypes] = {};   // changes still pending for the next pass
  bool     ln_rhythm_[VESTIGE_VOICE_SLABS]   = {};       // the lines hold the K3 CCW rhythm
  uint32_t rhy_rot_[VESTIGE_VOICE_SLABS]     = {};       // per-loop rotation word: stutters (+ decimates, >> 8)
  uint32_t rhy_rrot_[VESTIGE_VOICE_SLABS]    = {};       //  and rests
  int32_t  rhy_t_[VESTIGE_VOICE_SLABS]       = {};       // running step count (the polymeter's clock)
  int32_t  rhy_key_[VESTIGE_VOICE_SLABS]     = {};       // re-roll: the rhythm of its last pass (-1 = none yet)
  int32_t  rkv_next_[VESTIGE_VOICE_SLABS]    = {};       // hit variation: next start (-1 = draw; set at loop start)
  int32_t  rkv_t0_[VESTIGE_VOICE_SLABS]      = {};       //  its cycle [t0, t1) (t1 0 = none)
  int32_t  rkv_t1_[VESTIGE_VOICE_SLABS]      = {};
  int8_t   rkv_voice_[VESTIGE_VOICE_SLABS]   = {};       //  0 stutters, 1 rests
  int8_t   rkv_dk_[VESTIGE_VOICE_SLABS]      = {};       //  +1 / -1 hit
  volatile float rhy_level_ = 0.f;                        // K3 mode-1 rhythm depth, either half (0 = off)
  volatile int   rhy_side_  = 0;                          //  its half: kRhyCcw / kRhyCw (which table)
  int8_t   tl_src_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_LAYER_MAX_STEPS] = {},   // this pass's render (diag)
           tl_dir_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_LAYER_MAX_STEPS] = {},
           tl_rat_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_LAYER_MAX_STEPS] = {},
           tl_cnd_[VESTIGE_VOICE_SLABS][VESTIGE_TIMING_LAYER_MAX_STEPS] = {};
  int      sl_n_[VESTIGE_VOICE_SLABS]        = {0};      // this pass's slice count (0 = none) (diag)
  uint32_t sl_rand_mask_[VESTIGE_VOICE_SLABS] = {0};    // the steps playing a random slice this pass (diag)
  int32_t  trig_pass_[VESTIGE_VOICE_SLABS]    = {0};     // last pass seen
  uint32_t timing_rng_ = 0x9E3779B9u;                    // own RNG: level 0 never touches VestigeRand
  uint32_t timing_trigs_ = 0, timing_returns_ = 0;       // diag
  int      last_trig_slot_ = -1; uint32_t last_trig_at_ = 0;
  uint32_t pool_full_ = 0;                               // grains lost to a full physical pool (diag)
  volatile float err_level_[kErrTypes] = {0.f, 0.f, 0.f};   // 0 = off; start with no errors
  volatile int follow_mode_ = kFollowTape;
  int follow_mode_cfg_ = -1;                    // -1 = stretch; >= 0 forces a mode (host tests)
  volatile int glitch_mode_ = 0;                // SW2 = the K3 mode: 0 Euclidean · 1 random · 2 straight
  double   rho_t_[VESTIGE_SLOTS];            // tape-rate target per slot (ISR, per block)
  double   rho_d_[VESTIGE_SLOTS];            // glide state (double: see SmoothTape)
  float    rho_s_[VESTIGE_SLOTS];            // float copy for grain rates / diagnostics
  double   fwd_d_[VESTIGE_SLOTS];            // double shadow of fwd_ (see AdvanceHead)
  float    tape_coef_ = 1.f;                 // one-pole coef for rho
  int      div_[VESTIGE_SLOTS];              // division index at capture (-1 = none / freeze)
  double   beat_[VESTIGE_SLOTS];             // loop time in beats of T, mod 1 (ISR)
  double   beat_k_[VESTIGE_SLOTS];           // beats per material sample
  float    beat_frac_[VESTIGE_SLOTS] = {0.f};// copy for the control thread (atomic 32-bit)
  int      led_slot_[2] = {-1, -1};          // slot of each side's most recent capture
  float    k2f_ = 0.f;                       // K2 through the follow deadband
  bool     k2f_seeded_ = false;

  // T, the master period (samples), and its K2 / FS1 arbitration. Control
  // thread only. tap_period_ = 0 means "no tap: the knob sets T".
  size_t   period_       = VESTIGE_T_MIN_SAMPLES;
  size_t   tap_period_   = 0;
  uint32_t tap_prev_ms_  = 0;     // last committed tap down-press (0 = no chain)
  uint32_t tap_prev_iv_ms_ = 0;   // previous valid tap interval (0 = none): the agreement check
  // HOST-TEST HOOK, never set by the firmware: accept a single interval (the old
  // two-tap rule), for tests that re-tap at a sample-exact moment to exercise
  // how loops follow T — not the tap rule itself, which is tested without it.
  bool     tap_accept_one_ = false;
  uint32_t f1_down_ms_   = 0;     // current FS1 press start (own timestamp)
  float    k2_last_      = 0.f;   // last seen raw K2 (move detector)
  bool     k2_seeded_    = false;
  // Sample count (audio thread writes, control thread reads — one aligned
  // 32-bit word). A timestamp source, NOT a grid: see Process.
  volatile uint32_t sample_clock_ = 0;
  // LED1 anchor per side = that side's most recent capture start (sample_clock_
  // value), or the tap / engage / switch-in while that side holds nothing.
  uint32_t led_anchor_[2] = {0, 0};
};
