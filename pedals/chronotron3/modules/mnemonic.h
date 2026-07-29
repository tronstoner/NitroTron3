#pragma once
//
// mnemonic — tap-tempo tape/BBD delay.  SW3 MIDDLE.
// Spec: docs/ChronoTron3/mnemonic-concept.md + mnemonic-impl-plan.md
//
// Analog-style delay: changing the delay time (knob, tap, or tape gesture) glides
// the read tap, so the pitch bends while it moves (varispeed) — never a
// clean-digital crossfade. Colour (K4/K5 filter + tape drive + K3 degrade) sits
// INSIDE the feedback loop, so repeats progressively age. K2 feedback runs into
// the always-on tape saturation up to a bounded self-oscillation (no ducker).
// On top: a Hazarai-style hold/loop, and FS1-hold tape gestures (spin-up /
// slow-down).
//
//   K1 = delay time (SW2 UP) / tap division (SW2 MID) / Edge division tap (DOWN)
//   K2 = feedback (0 -> self-oscillation)
//   K3 = degrade — bipolar: CCW BBD/decimate · noon clean · CW tape warble/drive
//   K4 = tone tilt / center — bipolar: CCW toward LPF · noon flat · CW toward HPF
//   K5 = narrow — shrinks the gap between the 24 dB HP & LP cutoffs (band-limit)
//   K6 = dry/wet mix (shell-owned equal-power; mnemonic does NOT own output)
//   SW1 = FS1-hold gesture: UP spin-up · MID loop record/play · DOWN slow-down
//   SW2 = time mode: UP knob-time · MID tap-tempo · DOWN Edge dual-tap (4/4 + div)
//   FS1 = tap tempo (tap) / SW1 gesture (hold) — unified hold-then-commit
//   FS2 = bypass (tap: gate send, trail rings) / kill+clear (hold)
//   LED1 = delay-clock blink · LED2 = bypass / loop state
//
// Requires daisy.h + hothouse.h + control_surface.h + knob_map.h included first.
//
#include "module.h"
#include "mnemonic_constants.h"
#include "mnemonic_degrade.h"   // K3 bipolar BBD/Tape degradation engine
#include "ring_buffer.h"   // core/blocks — mono circular buffer with ReadFrac
#include <math.h>
#include <cstring>         // memset (kill)

// ---------------------------------------------------------------------------
// SDRAM storage — delay line + loop capture. File scope (single TU) is safe.
// ---------------------------------------------------------------------------
static float DSY_SDRAM_BSS mnem_delay_slab[MNEM_DELAY_SAMPLES];
// Two loop slabs: one plays, one records (scratch). Commit = pointer swap, so a
// short tap that recorded into scratch never disturbs the loop already playing.
static float DSY_SDRAM_BSS mnem_loop_slab_a[MNEM_LOOP_SAMPLES];
static float DSY_SDRAM_BSS mnem_loop_slab_b[MNEM_LOOP_SAMPLES];

static inline float MnemClamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// One-pole low-pass (tape HF loss, BBD rounding).
struct MnemOnePole {
  float z = 0.f, a = 0.f;
  void SetLP(float fc, float sr) { a = expf(-2.f * 3.14159265f * fc / sr); }
  float LP(float in) { z = in * (1.f - a) + z * a; return z; }
  void Reset() { z = 0.f; }
};

// Cytomic TPT state-variable filter — one core yields LP / BP / HP at once.
struct MnemSVF {
  float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 0.f;
  void Set(float fc, float Q, float sr) {
    float g = tanf(3.14159265f * fc / sr);
    k = 1.f / Q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void CopyCoefFrom(const MnemSVF& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
  void Process(float x, float& lp, float& bp, float& hp) {
    float v3 = x - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    lp = v2; bp = v1; hp = x - k * v1 - v2;
  }
  void Reset() { ic1 = ic2 = 0.f; }
};

class Mnemonic : public Module {
 public:
  // -------------------------------------------------------------------------
  void Init(float sr) override {
    sr_ = sr;
    delay_.Init(mnem_delay_slab, MNEM_DELAY_SAMPLES);
    loop_play_buf_ = mnem_loop_slab_a;
    loop_rec_buf_  = mnem_loop_slab_b;
    memset(mnem_loop_slab_a, 0, MNEM_LOOP_SAMPLES * sizeof(float));
    memset(mnem_loop_slab_b, 0, MNEM_LOOP_SAMPLES * sizeof(float));

    base_delay_ = 0.001f * 400.f * sr_;   // 400 ms default
    base_delay_sm_ = base_delay_;
    target_eff_ = base_delay_;
    read_delay_ = base_delay_;

    gest_atk_coef_ = 1.f - expf(-1.f / (MNEM_GEST_ATK_MS * 0.001f * sr_));
    gest_rel_coef_ = 1.f - expf(-1.f / (MNEM_GEST_REL_MS * 0.001f * sr_));
    send_coef_ = 1.f - expf(-1.f / (0.003f * sr_));            // 3 ms send gate ramp
    panic_rise_coef_ = 1.f - expf(-1.f / (MNEM_PANIC_RISE_MS * 0.001f * sr_));  // re-engage ramp
    panic_fall_coef_ = 1.f - expf(-1.f / (MNEM_PANIC_FADE_MS * 0.001f * sr_));  // panic spin-down
    ngate_env_atk_ = 1.f - expf(-1.f / (MNEM_NGATE_ENV_ATK_MS * 0.001f * sr_));
    ngate_env_rel_ = 1.f - expf(-1.f / (MNEM_NGATE_ENV_REL_MS * 0.001f * sr_));
    ngate_open_    = 1.f - expf(-1.f / (MNEM_NGATE_OPEN_MS    * 0.001f * sr_));
    ngate_close_   = 1.f - expf(-1.f / (MNEM_NGATE_CLOSE_MS   * 0.001f * sr_));
    param_smooth_ = 1.f - expf(-1.f / (MNEM_SMOOTH_MS * 0.001f * sr_));  // K2-K5 zipper smoother
    time_smooth_  = 1.f - expf(-1.f / (MNEM_TIME_SMOOTH_MS * 0.001f * sr_));  // K1 delay-time de-jitter
    loop_fade_coef_ = 1.f - expf(-1.f / (MNEM_LOOP_FADE_MS * 0.001f * sr_));
    loop_xfade_samps_ = (size_t)(MNEM_LOOP_XFADE_MS * 0.001f * sr_);
    edge_detune_samps_ = MNEM_EDGE_DETUNE_MS * 0.001f * sr_;

    SetFilters(2000.f, 2000.f);   // harmless defaults until first Controls
    degrade_.Init(sr_);
  }

  void Activate() override {
    snap_ = true;                 // snap read tap to target on first Controls (no sweep-in)
    gesture_engaged_ = false; gest_amt_ = 0.f;
    hp1_.Reset(); hp2_.Reset(); lp1_.Reset(); lp2_.Reset();
  }

  // -------------------------------------------------------------------------
  // Control-rate: knobs / switches / footswitches / LEDs.
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    const uint32_t now = daisy::System::GetNow();
    sw1_ = cs.Switch(0);
    sw2_ = cs.Switch(1);

    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));

    // ---- K2 feedback ------------------------------------------------------
    fb_gain_ = k2 * MNEM_FB_MAX;

    // ---- K3 degrade: bipolar BBD (CCW) / Tape (CW) engine ----------------
    // Base tape warmth stays always-on (TapeDrive, constant); K3 drives the
    // degrade chains on top (clean-ish dead-zone at centre). Colour is applied in
    // the loop; the tape speed-irregularity modulates the MAIN read tap (Process).
    tape_drive_ = MNEM_TAPE_DRIVE;                  // constant base warmth
    degrade_.SetDepth((k3 - 0.5f) * 2.f);           // p in [-1,+1] (dead-zone in engine)

    // ---- K4 tilt/center + K5 narrow (converging 24 dB HP+LP) -------------
    // Wide band edges from K4 (K5=0): CCW lowers the LP (dark), CW raises the HP
    // (thin), noon = 20 Hz .. 20 kHz. K5 shrinks both toward the geometric center
    // => a band-limit by convergence, resonant at both cutoffs (no single peak).
    float tilt = (k4 - 0.5f) * 2.f;                    // -1 .. +1
    float lo0 = MNEM_FILT_FMIN * powf(MNEM_FILT_HP_MAX / MNEM_FILT_FMIN,
                                      tilt > 0.f ? tilt : 0.f);
    float hi0 = MNEM_FILT_FMAX * powf(MNEM_FILT_LP_MIN / MNEM_FILT_FMAX,
                                      tilt < 0.f ? -tilt : 0.f);
    float center = sqrtf(lo0 * hi0);
    lo_ = center * powf(lo0 / center, 1.f - k5);       // HP cutoff target (smoothed in Process)
    hi_ = center * powf(hi0 / center, 1.f - k5);       // LP cutoff target

    // Narrow bands lose level at their CENTER through the cascade; in the loop
    // that reads as "silenced". Compensate with a center-gain makeup (rises as K5
    // narrows; auto-falls if RES_Q adds resonance, which lifts the center itself).
    // MNEM_FILT_MAKEUP_XS over-compensates (>1) so narrow K5 sits louder, not just
    // level-restored — only the above-unity part is scaled, so wide K5 stays flat.
    float r = (hi_ > lo_) ? hi_ / lo_ : 1.0001f;
    float xh = sqrtf(r), xl = 1.f / xh, Q = MNEM_FILTER_RES_Q;
    float hpM = (xh * xh) / sqrtf((1.f - xh * xh) * (1.f - xh * xh) + (xh / Q) * (xh / Q));
    float lpM = 1.f        / sqrtf((1.f - xl * xl) * (1.f - xl * xl) + (xl / Q) * (xl / Q));
    float cg = hpM * lpM; cg *= cg;                    // two cascaded stages each
    float comp = 1.f + (1.f / (cg + 1e-6f) - 1.f) * MNEM_FILT_MAKEUP_XS;
    filter_makeup_ = MnemClamp(comp, 1.f, MNEM_FILT_MAKEUP_MAX);

    // ---- K1 delay time / division, by SW2 --------------------------------
    edge_ = (sw2_ == 2);
    if (sw2_ == 0) {                                   // knob time (free)
      float kt = powf(k1, MNEM_TIME_CURVE);            // pre-warp: more travel for short delays
      float ms = MNEM_TIME_MIN_MS *
                 powf(MNEM_TIME_MAX_MS / MNEM_TIME_MIN_MS, kt);
      base_delay_ = ms * 0.001f * sr_;
    } else if (sw2_ == 1) {                            // tap tempo -> division
      int d = QuantizeDivision(k1);
      if (have_tempo_)
        base_delay_ = quarter_ms_ * MNEM_DIV_RATIOS[d] * 0.001f * sr_;
    } else {                                           // Edge dual-tap: A=4/4, B=div
      edge_div_ratio_ = MNEM_DIV_RATIOS[QuantizeDivision(k1)];
      if (have_tempo_) base_delay_ = quarter_ms_ * 0.001f * sr_;   // tap A = quarter
    }
    base_delay_ = MnemClamp(base_delay_, 0.001f * MNEM_TIME_MIN_MS * sr_,
                            (float)MNEM_DELAY_SAMPLES - 2.f);
    if (snap_) {                                   // first Controls after Activate: no ramp-in
      read_delay_ = base_delay_; base_delay_sm_ = base_delay_;
      lo_sm_ = lo_; hi_sm_ = hi_;
      fb_sm_ = fb_gain_; drive_sm_ = tape_drive_; makeup_sm_ = filter_makeup_;
      snap_ = false;
    }

    // ---- FS1: unified hold-then-commit -----------------------------------
    // Downpress is the universal event; press length disambiguates it:
    //   released < MNEM_TAP_RELEASE_MS  -> TAP (tempo / rhythm, per SW2)
    //   held    >= MNEM_LONGPRESS_MS    -> sustained gesture (SW1 latched at down):
    //                                      MID = loop record · UP/DOWN = tape
    //   released in the deadzone        -> no-op
    // In loop mode we always record into the scratch buffer from the downpress and
    // only commit (pointer-swap) once the press becomes a sustained gesture, so a
    // short tap never disturbs the loop already playing.
    const FootswitchEvent& f1 = cs.Foot(0);
    if (f1.rising) {
      f1_down_ms_ = now;
      f1_mode_ = sw1_;                       // latch SW1 for the whole press
      f1_gesture_committed_ = false;
      loop_committed_this_press_ = false;
      if (f1_mode_ == 1) {                   // MID: start scratch recording now
        loop_scratch_recording_ = true;
        loop_rec_write_ = 0;
        loop_rec_full_ = false;
      }
    }
    if (f1.down && !f1_gesture_committed_ &&
        (now - f1_down_ms_) >= MNEM_LONGPRESS_MS) {
      f1_gesture_committed_ = true;          // crossed into sustained-gesture land
      if (f1_mode_ == 0) { gesture_engaged_ = true; gest_dir_ = +1; }   // spin-up
      else if (f1_mode_ == 2) { gesture_engaged_ = true; gest_dir_ = -1; } // slow-down
      // f1_mode_ == 1 (loop): scratch keeps recording; commit on release.
    }
    if (loop_rec_full_) {                    // scratch hit the ceiling: auto record-end
      loop_rec_full_ = false;
      if (loop_scratch_recording_ && f1_gesture_committed_) {
        CommitLoopFromScratch();
        loop_committed_this_press_ = true;
      }
    }
    if (f1.falling) {
      uint32_t held = now - f1_down_ms_;
      if (!loop_committed_this_press_) {
        if (f1_gesture_committed_) {
          if (f1_mode_ == 1) CommitLoopFromScratch();       // loop: commit + play
          // UP/DOWN tape gesture ends via the gesture_engaged_ reset below.
        } else if (held < MNEM_TAP_RELEASE_MS) {
          if (sw2_ == 1 || sw2_ == 2) RegisterTap(f1_down_ms_);  // TAP (downpress-timed)
        }
        // else: released in the deadzone -> no-op
      }
      loop_scratch_recording_ = false;       // discard any uncommitted scratch
      f1_gesture_committed_ = false;
      loop_committed_this_press_ = false;
      gesture_engaged_ = false;              // release ends the tape gesture (slew back)
    }

    // ---- FS2: bypass (tap) / PANIC (hold) --------------------------------
    // Short tap  = normal bypass: the delay trail rings out naturally.
    // Long-press = panic escape: force bypass + kill loop + spin the feedback/
    //              tail down to true silence (always available, even at fb>=1).
    const FootswitchEvent& f2 = cs.Foot(1);
    if (f2.rising) kill_fired_ = false;
    if (f2.down && f2.held_ms >= MNEM_LONGPRESS_MS && !kill_fired_) {
      bypassed_ = true;                 // panic always lands in bypass
      panic_active_ = true;             // engage the spin-down-to-silence envelope
      panic_cleared_ = false;
      KillLoop();                       // drop the loop now (silent already); tail fades via env
      kill_fired_ = true;
    }
    if (f2.falling) {
      if (!kill_fired_) { bypassed_ = !bypassed_; panic_active_ = false; }  // tap cancels panic
      kill_fired_ = false;
    }
    send_target_ = bypassed_ ? 0.f : 1.f;

    // Deferred delay-buffer wipe: once the panic env has faded the tail to
    // silence, clear the trail on THIS (control) thread so re-engage is a clean
    // slate with no click (audio thread only raises the request).
    if (panic_clear_req_) {
      memset(mnem_delay_slab, 0, MNEM_DELAY_SAMPLES * sizeof(float));
      panic_clear_req_ = false;
    }

    UpdateLeds(now, led1, led2);
  }

  // -------------------------------------------------------------------------
  // Audio-rate: fill `wet` with the mono wet output (shell adds dry via K6).
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    const float wp0 = (float)delay_.GetWritePos();

    for (size_t i = 0; i < size; i++) {
      // Audio-rate param smoothing — kills the control-tick (~10 ms) zipper on
      // K2/K3/K4/K5. (K1 is already smoothed by the varispeed glide.)
      fb_sm_     += (fb_gain_       - fb_sm_)     * param_smooth_;
      drive_sm_  += (tape_drive_    - drive_sm_)  * param_smooth_;
      makeup_sm_ += (filter_makeup_ - makeup_sm_) * param_smooth_;
      lo_sm_     += (lo_ - lo_sm_) * param_smooth_;
      hi_sm_     += (hi_ - hi_sm_) * param_smooth_;
      base_delay_sm_ += (base_delay_ - base_delay_sm_) * time_smooth_;  // de-jitter K1 before the glide
      SetFilters(lo_sm_, hi_sm_);   // recompute SVF coeffs from smoothed cutoffs (2 tanf)

      // Loop scratch record (clean input) — capture before any colour. Buffer
      // full raises a flag the control loop treats as a record-end (vestige-style).
      if (loop_scratch_recording_ && loop_rec_write_ < MNEM_LOOP_SAMPLES) {
        loop_rec_buf_[loop_rec_write_++] = in[i];
        if (loop_rec_write_ >= MNEM_LOOP_SAMPLES) loop_rec_full_ = true;
      }

      // Gesture ramp (spin-up / slow-down envelope).
      float gt = gesture_engaged_ ? 1.f : 0.f;
      gest_amt_ += (gt - gest_amt_) * (gesture_engaged_ ? gest_atk_coef_ : gest_rel_coef_);
      float time_fac = 1.f, fb_target = fb_sm_;
      if (gest_dir_ == +1) { time_fac = 1.f + gest_amt_ * (MNEM_GEST_UP_TIMEFAC - 1.f);   fb_target = MNEM_GEST_UP_FB; }
      else                 { time_fac = 1.f + gest_amt_ * (MNEM_GEST_DOWN_TIMEFAC - 1.f); fb_target = MNEM_GEST_DOWN_FB; }
      target_eff_ = MnemClamp(base_delay_sm_ * time_fac, 1.f, (float)MNEM_DELAY_SAMPLES - 2.f);
      float fb_eff = fb_sm_ + gest_amt_ * (fb_target - fb_sm_);

      // Varispeed glide (THE identity): read tap eases toward its target.
      read_delay_ += (target_eff_ - read_delay_) * MNEM_GLIDE_COEF;

      // Tape speed-irregularity (cents, from the degrade engine) integrated to a
      // read-tap position offset (speed deviation -> tape displacement), leaky so
      // a DC offset can't drift the delay time. 0 unless the tape chain is active.
      float cents = degrade_.TapePitchCents();
      flutter_int_ = flutter_int_ * MNEM_FLUTTER_LEAK + cents * MNEM_CENTS_TO_RATE;
      float wobble = flutter_int_;
      const float wp = wp0 + (float)i;

      // Read tap: single, or the Edge dual-tap (4/4 + K1 division, detuned).
      float delayed;
      if (edge_) {
        float a = delay_.ReadFrac(wp - read_delay_ - wobble);
        float b = delay_.ReadFrac(wp - (read_delay_ * edge_div_ratio_ +
                                        edge_detune_samps_) - wobble);
        delayed = a * MNEM_EDGE_A_GAIN + b * MNEM_EDGE_B_GAIN;
      } else {
        delayed = delay_.ReadFrac(wp - read_delay_ - wobble);
      }

      // Panic spin-down envelope: only engages on the FS2 long-press. Throttling
      // the RECIRCULATION with it guarantees the tail/oscillation/noise die to
      // true silence even at fb>=1. Normal bypass leaves it at 1 (trails ring).
      const float pe_tgt = panic_active_ ? 0.f : 1.f;
      panic_env_ += (pe_tgt - panic_env_) *
                    (pe_tgt > panic_env_ ? panic_rise_coef_ : panic_fall_coef_);
      // Once faded, ask the control thread to wipe the trail (once) for a clean re-engage.
      if (panic_active_ && !panic_cleared_ && panic_env_ < 0.01f) {
        panic_clear_req_ = true; panic_cleared_ = true;
      }

      // Feedback tapped from the read (already filtered on prior laps). A dedicated
      // saturator compresses the RECIRCULATION only (analog bloom: repeats warm +
      // even out); the fresh input stays present (only mild K3 tape drive touches
      // it). Saturating after fb_eff means more feedback -> more bloom.
      float fb = FbSat(delayed * fb_eff * panic_env_);

      // Loop plays INTO the delay input, parallel with the (gated) dry send.
      send_gain_ += (send_target_ - send_gain_) * send_coef_;
      float loop_s = LoopPlay();

      // Bypass noise-duck: follow the trail envelope; in bypass, once it decays
      // toward the engine's noise floor, duck the injected hiss so it dies WITH
      // the trail (not after). Threshold rides the floor -> tracks K3. Only in
      // bypass; during play the tape hiss between notes stays as character.
      const float ta = fabsf(delayed);
      trail_env_ += (ta > trail_env_ ? ngate_env_atk_ : ngate_env_rel_) * (ta - trail_env_);
      float ng_tgt = 1.f;
      if (bypassed_) {
        const float thr = degrade_.NoiseFloorLin() * MNEM_NGATE_MARGIN;
        ng_tgt = (trail_env_ > thr) ? 1.f : 0.f;
      }
      noise_gate_ += (ng_tgt - noise_gate_) * (ng_tgt > noise_gate_ ? ngate_open_ : ngate_close_);
      degrade_.SetNoiseGate(noise_gate_);

      float x = in[i] * send_gain_ + loop_s + fb;
      x = Filter(x);                                    // K4/K5 tone — IN the loop (ages repeats)
      x = TapeDrive(x);                                 // always-on base tape warmth
      x = degrade_.ColourProcess(x);                    // K3 BBD/Tape colour — IN the loop
      delay_.Write(x);

      wet[i] = delayed * panic_env_;                    // raw read, spun down on panic only
    }
  }

  // mnemonic uses the shell's K6 equal-power mix (dry stays sacrosanct).
  bool OwnsOutput() const override { return false; }

 private:
  // ---- tone filter (in the loop): 24 dB HP -> 24 dB LP -------------------
  void SetFilters(float lo, float hi) {
    lo = MnemClamp(lo, MNEM_FILT_FMIN, MNEM_FILT_FMAX);
    hi = MnemClamp(hi, lo, MNEM_FILT_FMAX);             // keep hi >= lo
    hp1_.Set(lo, MNEM_FILTER_RES_Q, sr_); hp2_.CopyCoefFrom(hp1_);  // 1 tanf, both HP stages
    lp1_.Set(hi, MNEM_FILTER_RES_Q, sr_); lp2_.CopyCoefFrom(lp1_);  // 1 tanf, both LP stages
  }
  inline float Filter(float x) {
    float lp, bp, hp;
    hp1_.Process(x, lp, bp, hp);  x = hp;               // HP stage 1
    hp2_.Process(x, lp, bp, hp);  x = hp;               // HP stage 2 (24 dB/oct)
    lp1_.Process(x, lp, bp, hp);  x = lp;               // LP stage 1
    lp2_.Process(x, lp, bp, hp);                        // LP stage 2 (24 dB/oct)
    return lp * makeup_sm_;                             // smoothed narrow-band center makeup
  }
  inline float TapeDrive(float x) {                     // unity small-signal, soft peaks
    float d = drive_sm_;
    return tanhf(x * d) / d;
  }
  inline float FbSat(float v) {                         // feedback-path compression (bloom)
    return tanhf(v * MNEM_FB_DRIVE) / MNEM_FB_DRIVE;
  }

  // ---- loop (two-slab scratch -> playback; REPLACE, not overdub) ---------
  void CommitLoopFromScratch() {
    loop_scratch_recording_ = false;
    if (loop_rec_write_ == 0) return;                   // nothing recorded
    float* tmp = loop_play_buf_;                        // swap: scratch becomes the loop
    loop_play_buf_ = loop_rec_buf_;
    loop_rec_buf_  = tmp;
    loop_len_  = loop_rec_write_;
    loop_read_ = 0;
    loop_gain_ = 0.f; loop_gain_target_ = 1.f;          // fade in
    loop_playing_ = true;
  }
  inline float LoopPlay() {
    if (!loop_playing_ || loop_len_ == 0 || bypassed_) return 0.f;  // paused in bypass
    size_t p = loop_read_;
    float s = loop_play_buf_[p];
    size_t xf = loop_xfade_samps_;
    if (loop_len_ > 2 * xf && p >= loop_len_ - xf) {    // seam crossfade at wrap
      float frac = (float)(p - (loop_len_ - xf)) / (float)xf;
      s = s * (1.f - frac) + loop_play_buf_[p - (loop_len_ - xf)] * frac;
    }
    if (++loop_read_ >= loop_len_) loop_read_ = 0;
    loop_gain_ += (loop_gain_target_ - loop_gain_) * loop_fade_coef_;
    return s * loop_gain_;
  }

  // ---- tap tempo (sets the quarter note; used by SW2 MID + DOWN) --------
  void RegisterTap(uint32_t t) {                        // t = the downpress timestamp
    if (last_tap_ms_ != 0) {
      uint32_t iv = t - last_tap_ms_;
      if (iv >= MNEM_TAP_MIN_MS && iv <= MNEM_TAP_WINDOW_MS) {
        if (iv > MNEM_TAP_MAX_MS) iv = MNEM_TAP_MAX_MS;
        tap_iv_[tap_wr_ % MNEM_TAP_MEDIAN_N] = iv;
        tap_wr_++;
        int cnt = tap_wr_ < MNEM_TAP_MEDIAN_N ? tap_wr_ : MNEM_TAP_MEDIAN_N;
        quarter_ms_ = MedianMs(cnt);
        have_tempo_ = true;
      } else {                                          // gap too long -> new gesture
        tap_wr_ = 0;
      }
    }
    last_tap_ms_ = t;
  }
  float MedianMs(int cnt) {
    float tmp[MNEM_TAP_MEDIAN_N];
    for (int i = 0; i < cnt; i++) tmp[i] = (float)tap_iv_[i];
    for (int i = 1; i < cnt; i++) {                     // insertion sort
      float v = tmp[i]; int j = i - 1;
      while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
      tmp[j + 1] = v;
    }
    return tmp[cnt / 2];
  }

  // ---- kill --------------------------------------------------------------
  // Panic drops the loop immediately (already silent in bypass); the delay
  // trail is NOT wiped here — the panic envelope fades it to silence first,
  // then the control thread wipes the buffer (deferred, click-free).
  void KillLoop() {
    loop_len_ = 0; loop_playing_ = false; loop_scratch_recording_ = false;
  }

  // ---- LEDs --------------------------------------------------------------
  void UpdateLeds(uint32_t now, daisy::Led& led1, daisy::Led& led2) {
    float delay_ms = read_delay_ / sr_ * 1000.f;
    if (delay_ms < 50.f) delay_ms = 50.f;
    if (now >= clock_next_ms_) {
      clock_on_until_ = now + (uint32_t)MNEM_LED_CLOCK_ON_MS;
      clock_next_ms_  = now + (uint32_t)delay_ms;
    }
    led1.Set(now < clock_on_until_ ? 1.f : 0.f);

    float l2;
    bool fast = ((now / 120) % 2) == 0;
    bool slow = ((now / 400) % 2) == 0;
    bool rec = loop_scratch_recording_ && f1_gesture_committed_;  // confirmed loop record
    if (rec)                  l2 = 1.f;                        // solid while recording
    else if (loop_len_ > 0) { l2 = bypassed_ ? (slow ? 0.25f : 0.f)   // loop + bypass: dim flash
                                             : (fast ? 1.f : 0.f); }  // loop armed: rapid flash
    else                      l2 = bypassed_ ? 0.f : 1.f;            // active solid / bypass off
    led2.Set(l2);
  }

  int QuantizeDivision(float k1) {
    int raw = (int)(k1 * MNEM_DIV_COUNT);
    if (raw >= MNEM_DIV_COUNT) raw = MNEM_DIV_COUNT - 1;
    if (raw < 0) raw = 0;
    float stepw = 1.f / MNEM_DIV_COUNT;
    float lo = div_idx_ * stepw, hi = (div_idx_ + 1) * stepw;
    if (k1 < lo - MNEM_DIV_HYST || k1 > hi + MNEM_DIV_HYST) div_idx_ = raw;
    return div_idx_;
  }

  // ---- state -------------------------------------------------------------
  float sr_ = MNEM_SR;
  RingBuffer delay_;

  // varispeed
  float base_delay_ = 0.f, base_delay_sm_ = 0.f, target_eff_ = 0.f, read_delay_ = 0.f;
  float time_smooth_ = 0.004f;
  bool  snap_ = true;

  // feedback / drive — targets set at control rate, *_sm_ smoothed at audio rate
  float fb_gain_ = 0.f, tape_drive_ = MNEM_TAPE_DRIVE;
  float fb_sm_ = 0.f, drive_sm_ = MNEM_TAPE_DRIVE;

  // tone filter (in loop): 24 dB HP (hp1->hp2) then 24 dB LP (lp1->lp2) + makeup
  MnemSVF hp1_, hp2_, lp1_, lp2_;
  float   lo_ = 2000.f, hi_ = 2000.f;          // cutoff targets
  float   lo_sm_ = 2000.f, hi_sm_ = 2000.f;    // smoothed cutoffs (drive the SVFs)
  float   filter_makeup_ = 1.f, makeup_sm_ = 1.f;

  // degrade (K3): bipolar BBD/Tape engine + tape-speed read-tap integrator
  MnemDegrade degrade_;
  float flutter_int_ = 0.f;

  // gestures
  bool  gesture_engaged_ = false;
  int   gest_dir_ = +1;
  float gest_amt_ = 0.f, gest_atk_coef_ = 0.f, gest_rel_coef_ = 0.f;

  // FS1 unified state machine
  int      f1_mode_ = 0;                 // SW1 latched at downpress
  uint32_t f1_down_ms_ = 0;
  bool     f1_gesture_committed_ = false;
  bool     loop_committed_this_press_ = false;

  // bypass / send gate
  bool  bypassed_ = false, kill_fired_ = false;
  float send_gain_ = 1.f, send_target_ = 1.f, send_coef_ = 0.f;
  // panic spin-down envelope (gates wet output + feedback -> true silence).
  // Engaged ONLY by the FS2 long-press; normal bypass leaves it at 1 (trails ring).
  bool  panic_active_ = false, panic_cleared_ = false;
  volatile bool panic_clear_req_ = false;   // audio->control: clear the delay buffer once faded
  float panic_env_ = 1.f, panic_rise_coef_ = 0.f, panic_fall_coef_ = 0.f;
  // bypass noise-duck: trail follower -> ducks the degrade engine's hiss to
  // silence once the trail decays into the noise floor (bypass only).
  float trail_env_ = 0.f, noise_gate_ = 1.f;
  float ngate_env_atk_ = 0.f, ngate_env_rel_ = 0.f, ngate_open_ = 0.f, ngate_close_ = 0.f;
  float param_smooth_ = 0.004f;          // audio-rate smoother for K2-K5 params

  // loop (two-slab scratch/playback, pointer-swap commit; REPLACE not overdub)
  float* loop_play_buf_ = nullptr;
  float* loop_rec_buf_  = nullptr;
  bool   loop_scratch_recording_ = false, loop_playing_ = false;
  volatile bool loop_rec_full_ = false;
  size_t loop_len_ = 0, loop_rec_write_ = 0, loop_read_ = 0, loop_xfade_samps_ = 0;
  float  loop_gain_ = 0.f, loop_gain_target_ = 0.f, loop_fade_coef_ = 0.f;

  // tap tempo
  uint32_t last_tap_ms_ = 0;
  uint32_t tap_iv_[MNEM_TAP_MEDIAN_N] = {0};
  int   tap_wr_ = 0;
  bool  have_tempo_ = false;
  float quarter_ms_ = 400.f;
  int   div_idx_ = MNEM_DIV_NOON;

  // Edge dual-tap (SW2 DOWN)
  bool  edge_ = false;
  float edge_div_ratio_ = 1.f, edge_detune_samps_ = 0.f;

  // modes / leds
  int sw1_ = 0, sw2_ = 0;
  uint32_t clock_next_ms_ = 0, clock_on_until_ = 0;
};
