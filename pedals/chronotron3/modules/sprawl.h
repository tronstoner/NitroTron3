#pragma once
//
// sprawl — granular delay / glitch texture.  SW3 DOWN.
// Plan: docs/ChronoTron3/sprawl-port-plan.md
//
// A LOSSLESS 1:1 port of NitroTron3's Mode B ("Sprawl"): same signal path, same
// per-sample order of operations, same constants, same control mapping. The
// only intentional divergences are bundle-level (see the port plan's
// "Deliberate deviations"): FS2 is a trail bypass (gates the send, the wet tail
// rings; hold = panic kill) instead of NitroTron3's hard relay bypass, the pitch tracker uses
// ChronoTron3's instrument-agnostic TRACK_* profile, the envelope LP is fixed at
// the bass 50 Hz value, and the retired (dormant, `if (false)`) micro-stutter
// engine is not ported.
//
//   K1 = interval (SW2 UP/MID) / frequency shift (SW2 DOWN)
//   K2 = bipolar buffer depth + direction — CCW backward · noon live · CW forward
//   K3 = bipolar grain axis — CCW cloud/smear · noon neutral · CW character/glitch
//   K4 = texture amount (meaning set by SW1)
//   K5 = bipolar — CCW reverb mix · noon off · CW ring-buffer feedback
//   K6 = dry/wet mix (shell-owned equal-power; sprawl does NOT own output)
//   SW1 = texture mode: UP decimate/fold · MID glitch events · DOWN ringmod
//   SW2 = harmony mode: UP fixed interval · MID resonance table · DOWN freq shift
//   FS1 = tap tempo (tap: one interval = the echo time, no divisions; overrides
//         K2's length until K2 is moved again, EHX-style last-wins)
//       / buffer FREEZE (hold: toggle — the ring stops being written, grains
//         keep playing the held material; FS2 panic also releases it)
//   FS2 = bypass (tap: gate the send, trail + feedback ring on) / PANIC (hold:
//         bypass + spin recirculation and wet down to silence, wipe ring)
//   LED1 = echo clock (flashes once per echo; INVERTED while frozen) ·
//   LED2 = active / off when bypassed
//
// Requires daisy.h + hothouse.h + control_surface.h + knob_map.h included first.
//
#include "module.h"
// sprawl_constants.h MUST come before glitch_zones.h (pulled in by
// sprawl_texture.h): glitch_zones.h does `#include "constants.h"` and reads the
// GLITCH_* names as plain globals, which for this pedal resolves to
// pedals/chronotron3/constants.h — a file that does not define them.
#include "sprawl_constants.h"
#include "sprawl_params.h"
#include "sprawl_harmony.h"
#include "sprawl_grain_engine.h"
#include "sprawl_texture.h"      // includes glitch_zones.h (see note above)
#include "sprawl_feedback.h"
#include "sprawl_reverb.h"
#include "env_follower.h"        // core/blocks
#include "pitch_tracker.h"       // core/blocks — TRACK_* from chronotron3/constants.h
#include "freq_shifter.h"        // core/blocks — Bode SSB shifter (SW2 DOWN)
#include <math.h>

// ---------------------------------------------------------------------------
// SDRAM storage — 8 s grain ring + Clouds reverb buffer. File scope (single TU).
// ---------------------------------------------------------------------------
static float    DSY_SDRAM_BSS sprawl_ring_slab[GRAIN_BUF_SAMPLES];
static uint16_t DSY_SDRAM_BSS sprawl_reverb_slab[16384];

class Sprawl : public Module {
 public:
  // -------------------------------------------------------------------------
  void Init(float sr) override {
    sr_ = sr;
    env_.Init(sr);
    env_.SetCutoff(SPRAWL_ENV_LP_CUTOFF_HZ);
    tracker_.Init(sr);
    grain_.Init(sprawl_ring_slab, GRAIN_BUF_SAMPLES);
    b_shifter_.Init(sr);
    feedback_.Init(sr);
    reverb_.Init(sprawl_reverb_slab);
    texture_.Init();
    send_coef_ = 1.f - expf(-1.f / (0.003f * sr_));   // 3 ms send gate ramp
    panic_rise_coef_ = 1.f - expf(-1.f / (SPRAWL_PANIC_RISE_MS * 0.001f * sr_));
    panic_fall_coef_ = 1.f - expf(-1.f / (SPRAWL_PANIC_FADE_MS * 0.001f * sr_));
  }

  // Mode switches keep all state: NitroTron3 did not reset Mode B on a mode
  // change either (the ring, the grains and the reverb tail simply carry on).
  void Activate() override {}
  void Deactivate() override {}

  // -------------------------------------------------------------------------
  // Control-rate: raw surface snapshot + FS2 bypass + LEDs. All knob math lives
  // in DeriveParams() (the UI seam), run once per audio block as the original
  // did at the top of ProcessGranular().
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    controls_.k1  = cs.Knob(0);
    controls_.k2  = cs.Knob(1);
    controls_.k3  = cs.Knob(2);
    controls_.k4  = cs.Knob(3);
    controls_.k5  = cs.Knob(4);
    controls_.sw1 = cs.Switch(0);
    controls_.sw2 = cs.Switch(1);

    // YIN in the control loop — same ~10 ms cadence as NitroTron3's main loop.
    tracker_.Update();

    // ---- K2 / FS1 arbitration (EHX-style: the last gesture wins) ----------
    // Moving K2 past a small dead zone cancels a tapped length; a tap overrides
    // the knob. K2 keeps its direction + noon-deadzone job either way.
    if (!k2_seeded_) { k2_last_ = controls_.k2; k2_seeded_ = true; }
    if (fabsf(controls_.k2 - k2_last_) > SPRAWL_K2_MOVE_EPS) {
      k2_last_ = controls_.k2;
      controls_.tap_samples = -1.f;          // knob wins: drop the tapped length
    }

    // ---- FS1: tap tempo (tap) / buffer FREEZE (hold) ----------------------
    // Hold-then-commit, as on mnemonic's FS1: the DOWN-press is the timing
    // reference (so tempo accuracy does not depend on release), and the press
    // LENGTH disambiguates. Released < TAP_RELEASE = tap; held >= LONGPRESS =
    // freeze toggle, fired once on crossing the threshold so the state flips
    // under the foot; released in between = no-op.
    const uint32_t now = daisy::System::GetNow();
    const FootswitchEvent& f1 = cs.Foot(0);
    if (f1.rising) { f1_down_ms_ = now; f1_fired_ = false; }
    if (f1.down && !f1_fired_ && f1.held_ms >= SPRAWL_LONGPRESS_MS) {
      frozen_  = !frozen_;                 // HOLD: freeze / release the buffer
      f1_fired_ = true;
    }
    if (f1.falling) {
      const uint32_t press = now - f1_down_ms_;
      if (!f1_fired_ && press < SPRAWL_TAP_RELEASE_MS) {
        // TAP: the interval between two DOWN-presses IS the echo time (one
        // "quarter note" = the whole span, no subdivisions). Hard cut, no glide.
        if (tap_prev_ms_ != 0) {
          const uint32_t iv = f1_down_ms_ - tap_prev_ms_;
          if (iv >= SPRAWL_TAP_MIN_MS && iv <= SPRAWL_TAP_MAX_MS) {
            // Store the tapped ECHO TIME in samples; DeriveParams pins
            // base_delay to it and picks a K2 magnitude that holds it.
            controls_.tap_samples = (float)iv * 0.001f * sr_;
            led1_phase_ms_ = 0;            // re-sync the clock flash to the tap
          }
        }
        tap_prev_ms_ = f1_down_ms_;
      }
      f1_fired_ = false;
    }

    // ---- FS2: bypass (tap) / PANIC (hold) — mnemonic pattern ---------------
    // Tap  = trail bypass: only the input SEND is gated; the wet keeps running,
    //        feedback loop included, so a K5-CW drone rings on through bypass
    //        (bundle rule G2). The clean path is never touched (the shell lifts
    //        dry to unity via Bypassed()).
    // Hold = panic: always lands in bypass and fades the recirculation AND the
    //        wet output to true silence (panic_env_), then wipes ring + grains.
    //        Any later tap re-engages over the short rise ramp.
    const FootswitchEvent& f2 = cs.Foot(1);
    if (f2.rising) kill_fired_ = false;
    if (f2.down && f2.held_ms >= SPRAWL_LONGPRESS_MS && !kill_fired_) {
      bypassed_ = true;
      panic_active_ = true;
      panic_cleared_ = false;
      frozen_ = false;                  // panic is the global escape: unfreeze too
      kill_fired_ = true;
    }
    if (f2.falling) {
      if (!kill_fired_) { bypassed_ = !bypassed_; panic_active_ = false; }  // tap; also cancels panic
      kill_fired_ = false;
    }
    send_target_ = bypassed_ ? 0.f : 1.f;

    // Deferred wipe: once the panic envelope has faded the wet to silence, clear
    // the ring / voices on THIS (control) thread — the audio thread only raises
    // the request. Reverb + feedback-return state are left to decay (already
    // muted by panic_env_ at the output).
    if (panic_clear_req_) {
      grain_.Kill();
      prev_wet_ = 0.f;
      panic_clear_req_ = false;
    }

    // LED1 = buffer clock: one flash per buffer length (the tapped or knob-set
    // span). Period comes from the last derived max_range, so it always shows
    // what the engine is actually using.
    led1_phase_ms_ += 10;                        // Controls runs on the 10 ms tick
    uint32_t period = (uint32_t)(led_period_ms_);
    if (period < 100) period = 100;
    if (led1_phase_ms_ >= period) led1_phase_ms_ = 0;
    const bool flash = (led1_phase_ms_ < SPRAWL_LED1_FLASH_MS);
    // Frozen = inverted (mostly lit, brief gap on the beat): the clock stays
    // readable and the held state is unmistakable.
    led1.Set(frozen_ ? (flash ? 0.f : 1.f) : (flash ? 1.f : 0.f));
    led2.Set(bypassed_ ? 0.f : 1.f);
  }

  // -------------------------------------------------------------------------
  // Audio-rate: fill `wet` with the mono wet output (shell adds dry via K6).
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    const SprawlParams p = DeriveParams(controls_);

    // Per-sample wet bus capture (used by the block-based reverb pipeline below).
    float wet_block[CT3_BLOCK_SIZE];

    for (size_t i = 0; i < size; i++) {
      send_gain_ += (send_target_ - send_gain_) * send_coef_;
      float dry = in[i] * send_gain_;

      // Envelope follower (feeds pitch tracker gating)
      grain_env_ = env_.Process(dry);

      // Note-on detection (shared): rising edge — env jumps a factor above a slow
      // baseline, gated above the noise floor, with a refractory lock so one pluck
      // = one trigger. Drives two reactive features off the SAME detected attack:
      //   • grain engine — seeds a burst (K3-CW glitch side only; see scheduler);
      //   • SW1-MID glitch effect — forces an event (passed into GlitchEvents).
      // Baseline/refractory update every sample so state never goes stale.
      trans_slow_ += TRANSIENT_SLOW_COEF * (grain_env_ - trans_slow_);
      if (trans_refractory_ > 0) trans_refractory_--;
      bool note_on = (trans_refractory_ == 0 && grain_env_ > TRANSIENT_GATE
                      && grain_env_ > trans_slow_ * TRANSIENT_RISE);
      if (note_on) trans_refractory_ = TRANSIENT_REFRACTORY;
      // Grain burst only on the CW glitch side (glitch_amount > 0 ⇒ K3 past the
      // neutral pad): the CCW cloud and K3-neutral passthrough stay untriggered.
      if (note_on && p.glitch_amount > 0.01f) {
        grain_.ArmBurst();                 // fire on the next scheduler tick
      }

      // Feed pitch tracker
      tracker_.Feed(dry, grain_env_);

      // Panic envelope: 1 in normal operation AND in trail bypass (the loop
      // rings on); only the FS2 hold pulls it to 0, throttling the
      // recirculation itself so even feedback >= 1 collapses to silence.
      const float pe_tgt = panic_active_ ? 0.f : 1.f;
      panic_env_ += (pe_tgt - panic_env_) *
                    (pe_tgt > panic_env_ ? panic_rise_coef_ : panic_fall_coef_);
      if (panic_active_ && !panic_cleared_ && panic_env_ < 0.01f) {
        panic_clear_req_ = true; panic_cleared_ = true;
      }

      // Write input + feedback into ring buffer (even in direct-texture mode,
      // so turning K2 back up reveals a buffer with textured material).
      // Active / trail bypass: panic_env_ == 1 → identical to NitroTron3.
      // Inject() runs unconditionally so the build-up / on-play duck envelopes
      // stay live — unfreezing must not jump on stale state.
      const float fb = feedback_.Inject(prev_wet_, p.feedback_amt * panic_env_,
                                        grain_env_);
      // FREEZE (FS1 hold): stop writing. The write head parks, so every later
      // grain anchors to the same held material. Holds the feedback return too
      // (same write), so the buffer cannot be overwritten by its own tail.
      if (!frozen_) grain_.Write(dry + fb);

      // Grain engine: scheduler tick + sum of all active voices.
      float w = grain_.Tick(p, harmony_);

      // Texture shaper — K4 meaning depends on SW1 mode
      w = texture_.Process(w, p, grain_env_, note_on);

      // SW2 DOWN: Bode SSB frequency shifter, applied inside the feedback loop
      // (output feeds prev_wet → cascading shift each pass). HPF on the return
      // cleans any sub-audio energy the shifter introduces near unison.
      if (p.freq_shift_active) {
        w = b_shifter_.Process(w);
      }

      // Wet output is full-range now — the HPF sits on the feedback return only
      // (see the injection above), so there's no gated-filter click at the K2
      // noon boundary and the live/wet signal keeps its lows.

      // Store wet for next sample's feedback injection (pre-reverb, so reverb
      // does not feed the ring buffer).
      prev_wet_ = w;

      // Capture mono wet for the block-based reverb pipeline.
      wet_block[i] = w;
    }

    reverb_.ProcessBlock(wet_block, size, p.reverb_amt, wet);
    // Panic: mute the wet output (reverb included) — per-block gain is fine,
    // the envelope moves ~1 % per block at the 120 ms fade.
    if (panic_env_ < 0.9999f)
      for (size_t i = 0; i < size; i++) wet[i] *= panic_env_;
  }

  bool OwnsOutput() const override { return false; }   // shell applies K6
  bool Bypassed() const override { return bypassed_; }

 private:
  // -------------------------------------------------------------------------
  // The UI seam: raw surface snapshot → per-block derived parameters. This is
  // verbatim the per-block math from the top of NitroTron3's ProcessGranular().
  // -------------------------------------------------------------------------
  SprawlParams DeriveParams(const SprawlControls& c) {
    SprawlParams p;

    // K1: interval (±24 semi, centered with dead zone)
    float k1 = RemapKnob(c.k1);
    p.k1 = k1;

    // K2: bipolar. Magnitude (|K2-0.5| past the deadzone) = buffer range +
    // timescale, exactly as the old unipolar K2. Sign = global grain playback
    // direction (CW = forward, CCW = backward), replacing randomized per-grain
    // reverse as the *direction* source. Noon deadzone = direct-texture
    // passthrough (grain engine bypassed). See docs/MODE_B_DISCOVERY.md.
    // K3 first: the tap-tempo inversion below needs the current grain length.
    // K3: bipolar. Noon (padded) = neutral single coherent stream. CW half =
    // character/glitch (as the old unipolar K3). CCW half = Clouds-style
    // deterministic density.
    float k3 = RemapKnob(c.k3);
    float k3c = k3 - 0.5f;
    bool  cloud_mode = (k3c < -GRAIN_K3_DEADZONE);
    float k3mag = (fabsf(k3c) - GRAIN_K3_DEADZONE) / (0.5f - GRAIN_K3_DEADZONE);
    if (k3mag < 0.f) k3mag = 0.f;
    if (k3mag > 1.f) k3mag = 1.f;
    p.cloud_mode = cloud_mode;
    p.k3mag      = k3mag;

    float k2c = RemapKnob(c.k2) - 0.5f;                // [-0.5, +0.5]
    bool  direct_texture = (fabsf(k2c) < GRAIN_K2_DEADZONE);
    bool  buf_reverse    = (k2c < -GRAIN_K2_DEADZONE); // backward only past the CCW deadzone; live/deadzone stays forward
    float k2 = (fabsf(k2c) - GRAIN_K2_DEADZONE) / (0.5f - GRAIN_K2_DEADZONE);
    if (k2 < 0.f) k2 = 0.f;
    if (k2 > 1.f) k2 = 1.f;
    // FS1 tap tempo overrides the LENGTH only (direction + the noon deadzone
    // above stay on the knob). The tap names the audible ECHO TIME, so it is
    // inverted through the real delay formula — see SolveK2ForDelay.
    if (c.tap_samples >= 0.f)
      k2 = K2ForTap(c.tap_samples, GrainBaseLen(cloud_mode, k3mag));
    size_t max_range = GRAIN_MIN_RANGE +
        static_cast<size_t>(k2 * static_cast<float>(GRAIN_BUF_SAMPLES - GRAIN_MIN_RANGE));
    p.direct_texture = direct_texture;
    p.buf_reverse    = buf_reverse;
    p.max_range      = max_range;

    // Grain params (only used when !direct_texture, but cheap to compute always).
    // CW/neutral: k3mag drives grain shortening + glitch. Cloud (CCW): both are
    // held at 0 so grain length stays fixed and scatter/jitter/loops/random-
    // reverse all switch off — only the emission density (overlap below) sweeps.
    float grain_character = cloud_mode ? 0.f : k3mag;
    float glitch_amount   = cloud_mode ? 0.f : k3mag;
    p.grain_character = grain_character;
    p.glitch_amount   = glitch_amount;

    // K2 timescale factor: CCW = 0.5× (shorter/faster), CW = 2× (longer/slower)
    float k2_scale = 0.5f + k2 * 1.5f;
    p.k2_scale = k2_scale;

    // (the former `gc_sq` lives inside GrainBaseLen now — single source of
    // truth shared with the tap-tempo inversion)

    // Grain length + emission density — one K3 axis through the noon origin
    // (GRAIN_NEUTRAL_LEN / GRAIN_NEUTRAL_OVERLAP). At k3mag=0 both branches equal
    // the origin, so crossing noon is seamless in either direction.
    //  Cloud (CCW): k3mag lengthens neutral→CLOUD_LEN_MAX; overlap held at neutral
    //    so the emission rate falls as grains grow — a long, slow smear.
    //  CW/neutral: gc_sq shortens the *block base* only to a coarse floor
    //    (GRAIN_CW_LEN_FLOOR) — the short fast-stutter grains come from per-grain
    //    variation at trigger, not from collapsing the base. Overlap thins
    //    neutral→1× as chaos rises.
    // Overlap anchor is context-dependent. Echo (K2 engaged) + SW2 MID blooms
    // (dense); everything else — including the whole K2-noon live/glitch zone —
    // stays sparse. The K3-CW side still thins the anchor toward 1× for glitch.
    float ovl_anchor = (!direct_texture && c.sw2 == 1) ? GRAIN_OVERLAP_MID_ECHO
                                                       : GRAIN_NEUTRAL_OVERLAP;
    size_t grain_len;
    float  overlap;
    if (cloud_mode) {
      // CCW lengthens grains neutral→CLOUD_LEN_MAX (linear, felt across the whole
      // travel). Overlap held at neutral, so the emission RATE falls as grains
      // grow — a long, slow smear that leans on the deep buffer.
      float t = k3mag;
      grain_len = static_cast<size_t>(
          GrainBaseLen(cloud_mode, t) * k2_scale);
      overlap = ovl_anchor;
    } else {
      grain_len = static_cast<size_t>(
          GrainBaseLen(cloud_mode, k3mag) * k2_scale);
      overlap = ovl_anchor - glitch_amount * (ovl_anchor - 1.f);
    }
    if (grain_len < GRAIN_MIN_LEN) grain_len = GRAIN_MIN_LEN;
    size_t base_interval = static_cast<size_t>(
        static_cast<float>(grain_len) / overlap);
    if (base_interval < 32) base_interval = 32;
    p.grain_len     = grain_len;
    p.overlap       = overlap;
    p.base_interval = base_interval;

    // Grain window: smooth grains (cloud + neutral, glitch_amount≈0) use a full
    // Hann window so the constant-overlap sum is flat (no tremolo at unison).
    // CW character grains keep the length-based Tukey (flatter = more present).
    p.grain_alpha = (glitch_amount < 0.01f) ? 1.0f : -1.0f;

    // K4: texture amount (0 = clean, CW = full effect)
    float k4 = RemapKnob(c.k4);
    p.k4 = k4;

    // SW1: texture mode (0=decimate/fold bipolar, 1=free, 2=ringmod)
    p.texture_mode = c.sw1;

    // Ringmod: sine carrier, keytracked LPF
    // K4 < 30% = tremolo (1–15 Hz), K4 >= 30% = bell partials (3.5× at noon)
    float ringmod_inc;
    float ringmod_lp_g = 1.f;
    if (k4 < 0.3f) {
      // Tremolo: 1–15 Hz, not pitch-tracked, no compensation needed
      float trem_freq = 1.f + (k4 / 0.3f) * 14.f;
      ringmod_inc = trem_freq / 48000.f;
    } else {
      // Bell/metallic partials — 3.5× lands at noon, inharmonic spread
      static const float RATIOS[] = {1.5f, 2.76f, 3.5f, 4.2f, 5.4f, 6.5f, 7.3f};
      int idx = static_cast<int>((k4 - 0.3f) / 0.7f * 7.f);
      if (idx > 6) idx = 6;
      float ratio = RATIOS[idx];
      float carrier_freq = MidiToFreq(tracker_.GetMidiNote()) * ratio;
      ringmod_inc = carrier_freq / 48000.f;
      // Keytracked LPF: cutoff = carrier × 6 (gentle top-end rolloff,
      // only tames the extreme highs at high ratios). One-pole coefficient.
      float lp_cutoff = carrier_freq * 6.f;
      if (lp_cutoff > 20000.f) lp_cutoff = 20000.f;
      ringmod_lp_g = 1.f - expf(-2.f * 3.14159265f * lp_cutoff / 48000.f);
    }
    p.ringmod_inc  = ringmod_inc;
    p.ringmod_lp_g = ringmod_lp_g;

    // Bipolar K4 helpers for case 0 (decimator/folder)
    // CCW→noon (k4 0→0.5): decimator amount, inverted so CCW = max crush
    // noon→CW (k4 0.5→1): fold amount
    p.decim_amt = (k4 < 0.5f) ? (1.f - k4 / 0.5f) : 0.f;   // 1 at CCW, 0 at noon
    p.fold_amt  = (k4 > 0.5f) ? ((k4 - 0.5f) / 0.5f) : 0.f; // 0 at noon, 1 at CW
    p.decim_rate = 1.f + p.decim_amt * 47.f;  // 1 (clean at noon) to 48 (max crush at CCW)

    // Glitch (SW1 MIDDLE) — bipolar K4, event-driven. CCW = bit-flip events,
    // CW = timing events (freeze / stutter / reverse). Env-gated mix per
    // sample inside the processor. See docs/MODE_B_TEXTURE_IDEAS.md.
    float k4_centered = k4 - 0.5f;
    float glitch_magnitude = fabsf(k4_centered) * 2.f;  // 0..1
    p.glitch_side = (k4_centered < 0.f) ? 0 : 1;        // 0=CCW bit-flip, 1=CW timing
    p.glitch_effect_pos = (glitch_magnitude > GLITCH_DEADZONE)
        ? (glitch_magnitude - GLITCH_DEADZONE) / (1.f - GLITCH_DEADZONE)
        : 0.f;

    // K5: bipolar — CCW = reverb dry/wet (0→1), center = off (deadzone),
    //               CW = ring-buffer feedback (0→0.95, existing behavior).
    float k5 = RemapKnob(c.k5);
    float reverb_amt;
    float feedback_amt;
    const float dead = K5_CENTER_DEADZONE;
    if (k5 < 0.5f - dead) {
      reverb_amt = (0.5f - dead - k5) / (0.5f - dead);  // 0 at edge of deadzone, 1 at full CCW
      feedback_amt = 0.f;
    } else if (k5 > 0.5f + dead) {
      feedback_amt = ((k5 - 0.5f - dead) / (0.5f - dead)) * FEEDBACK_MAX;
      reverb_amt = 0.f;
    } else {
      reverb_amt = 0.f;
      feedback_amt = 0.f;
    }

    // Even out perceived loudness across K1.
    // SW2 UP: scale by K1's exact semitone offset.
    // SW2 MID: scale by the closest resonance interval (same scan as GrainPitchRatio).
    // SW2 DOWN: untouched.
    if (c.sw2 == 0) {
      feedback_amt *= FixedIntervalFeedbackScale(K1ToSemi(k1, 12));
    } else if (c.sw2 == 1) {
      int k1_semi = K1ToSemi(k1, 36);
      int closest_semi = 0;
      int min_dist = 100;
      for (int i = 0; i < NUM_RES; i++) {
        int d = RESONANCES[i] - k1_semi;
        if (d < 0) d = -d;
        if (d < min_dist) { min_dist = d; closest_semi = RESONANCES[i]; }
      }
      feedback_amt *= FixedIntervalFeedbackScale(closest_semi);
    }
    p.reverb_amt   = reverb_amt;
    p.feedback_amt = feedback_amt;

    // Live-grain mode: the whole K2-noon zone (direct-texture) runs the grain
    // engine on the live ring for a near-zero-latency granular texture. K3 then
    // behaves identically to buffer mode (neutral / cloud-CCW / character-CW) —
    // K2 only sets read-back depth (live here → deep trails when engaged). HPF
    // stays on the feedback return only, for the full-range live feel.
    bool live_grain = direct_texture;
    // Read-back depth (how far behind the write head grains read). Buffer
    // engaged: scales with K2 range, floored at grain length (trails). Live: 0 —
    // grains follow the write head; the per-grain safety floor at trigger adds
    // only the minimum a reverse/pitch-up grain physically needs, so a
    // forward-unison grain stays ~0 latency (dry passthrough at the neutral).
    size_t base_delay;
    if (live_grain) {
      base_delay = 0;
    } else if (c.tap_samples >= 0.f) {
      // TAP: the tapped time IS the echo, so use it verbatim and skip the
      // `>= grain_len` floor below. That floor is a TRAILS AESTHETIC for the
      // knob, not a physical limit — the real read-overrun guard is the
      // per-grain safety at trigger time (reverse / pitch-up only; a forward
      // unison grain stays exactly base_delay behind the write head forever).
      // Leaving the floor in was what pinned the echo to the grain length and
      // made short taps impossible on K3-CCW, where grains reach seconds.
      // Only cap for ring wrap: keep a grain's worth of headroom below the
      // buffer span so a read can never lap the write head.
      const size_t cap = (max_range > grain_len + 64) ? (max_range - grain_len - 64)
                                                      : (max_range / 2);
      base_delay = static_cast<size_t>(c.tap_samples);
      if (base_delay > cap) base_delay = cap;
    } else {
      base_delay = max_range / 8;
      if (base_delay < grain_len) base_delay = grain_len;
    }
    p.live_grain = live_grain;
    p.base_delay = base_delay;
    // LED1 clock period (read on the control thread): the AUDIBLE echo time,
    // i.e. the grain read-back depth — not the buffer span, which is 8x longer.
    // In live mode (K2 noon) there is no read-back, so show the grain rate.
    led_period_ms_ = (float)(base_delay > 0 ? base_delay : grain_len)
                   * 1000.f / sr_;

    // K6 (dry/wet mix) is the SHELL's — it applies the same MixCurve/equal-power
    // crossfade and the same smoothing coefficient NitroTron3 used here.

    // SW2: harmony mode
    p.harmony = c.sw2;  // 0=fixed, 1/2=resonance

    // SW2 DOWN: K1 drives a Bode SSB frequency shifter on the wet bus (inside
    // the feedback loop). Buffer-read pitch is forced to unison so K1 isn't
    // doing two jobs at once.
    bool freq_shift_active = (c.sw2 == 2);
    if (freq_shift_active) {
      float k1_norm = (k1 - 0.5f) * 2.f;  // [-1, 1]
      float abs_k = fabsf(k1_norm);
      float shift_hz = 0.f;
      if (abs_k > FREQ_SHIFT_DEADZONE) {
        float t = (abs_k - FREQ_SHIFT_DEADZONE) / (1.f - FREQ_SHIFT_DEADZONE);
        shift_hz = (expf(t * FREQ_SHIFT_CURVE) - 1.f)
                 / (expf(FREQ_SHIFT_CURVE) - 1.f) * FREQ_SHIFT_MAX_HZ;
        if (k1_norm > 0.f) shift_hz = -shift_hz;  // CCW = down-shift (bass), CW = up-shift (bright)
      }
      b_shifter_.SetShiftHz(shift_hz);
    }
    p.freq_shift_active = freq_shift_active;

    return p;
  }

  // -------------------------------------------------------------------------
  // K3 grain base length (before the K2 timescale). Single source of truth for
  // both grain_len and the tap-tempo inversion.
  // -------------------------------------------------------------------------
  static float GrainBaseLen(bool cloud_mode, float k3mag) {
    if (cloud_mode)  // CCW: lengthen neutral -> CLOUD_LEN_MAX
      return GRAIN_NEUTRAL_LEN + k3mag * (CLOUD_LEN_MAX - GRAIN_NEUTRAL_LEN);
    const float gc_sq = k3mag * k3mag;  // CW: shorten neutral -> CW_LEN_FLOOR
    return GRAIN_NEUTRAL_LEN - gc_sq * (GRAIN_NEUTRAL_LEN - GRAIN_CW_LEN_FLOOR);
  }

  // -------------------------------------------------------------------------
  // Tap tempo -> K2 magnitude. The tap names the audible ECHO TIME, which the
  // knob produces as max_range/8, so invert that: the tapped echo lands on the
  // same buffer depth / timescale / grain character the knob would give for it.
  // The echo itself is then pinned exactly (see base_delay below), so this only
  // has to make the buffer deep enough and keep the feel consistent.
  // -------------------------------------------------------------------------
  static float K2ForTap(float target_samples, float base_len) {
    float k2 = (8.f * target_samples - (float)GRAIN_MIN_RANGE) /
               (float)(GRAIN_BUF_SAMPLES - GRAIN_MIN_RANGE);
    // ...but the span must also physically HOLD the tapped echo plus a grain of
    // headroom, or the scatter range (max_range - base_delay) underflows:
    //   MIN_RANGE + k2·(BUF-MIN_RANGE) >= target + base_len·(0.5+1.5·k2) + 64
    // Linear in k2; the denominator is always positive (base_len <= CLOUD_LEN_MAX
    // = 96000 vs a 379200 span slope). This is what makes short taps reachable on
    // K3-CCW, where grains alone can be seconds long.
    const float need = (target_samples + 0.5f * base_len + 64.f - (float)GRAIN_MIN_RANGE) /
                       ((float)(GRAIN_BUF_SAMPLES - GRAIN_MIN_RANGE) - 1.5f * base_len);
    if (need > k2) k2 = need;
    if (k2 < 0.f) k2 = 0.f;
    if (k2 > 1.f) k2 = 1.f;
    return k2;
  }

  // --- components ----------------------------------------------------------
  SprawlControls    controls_;
  SprawlHarmony     harmony_;
  SprawlGrainEngine grain_;
  SprawlTexture     texture_;
  SprawlFeedback    feedback_;
  SprawlReverb      reverb_;
  EnvFollower       env_;        // module-private (shared with A/C in NitroTron3)
  PitchTracker      tracker_;    // module-private; only consumer = ringmod carrier
  FreqShifter       b_shifter_;  // SW2 DOWN — Bode SSB frequency shifter

  // --- per-sample state ----------------------------------------------------
  float sr_ = 48000.f;
  float grain_env_ = 0.f;        // envelope value for grain amplitude
  float trans_slow_ = 0.f;       // slow envelope baseline for note-on detection
  int   trans_refractory_ = 0;   // samples until another attack can trigger
  float prev_wet_ = 0.f;         // previous sample's wet output for feedback injection

  // --- FS2 trail bypass ----------------------------------------------------
  bool  bypassed_ = false;
  float send_gain_ = 1.f, send_target_ = 1.f, send_coef_ = 0.f;
  // FS1 tap tempo / K2 arbitration + LED1 buffer clock (control thread; the
  // period is written once per block by DeriveParams).
  uint32_t tap_prev_ms_ = 0;        // last COMMITTED tap down-press (0 = no chain)
  uint32_t f1_down_ms_ = 0;         // current FS1 press start
  bool     f1_fired_ = false;       // freeze toggle already fired this press
  bool     frozen_ = false;         // FS1 hold: ring write held
  float    k2_last_ = 0.f;          // last seen K2 position (move detector)
  bool     k2_seeded_ = false;
  uint32_t led1_phase_ms_ = 0;
  float    led_period_ms_ = 1000.f;
  // FS2 panic (hold): spin-down envelope gates recirculation + wet output.
  bool  kill_fired_ = false, panic_active_ = false, panic_cleared_ = false;
  volatile bool panic_clear_req_ = false;   // audio -> control: wipe ring once faded
  float panic_env_ = 1.f, panic_rise_coef_ = 0.f, panic_fall_coef_ = 0.f;
};
