#pragma once
//
// mnemonic — tap-tempo tape/BBD delay.  SW3 MIDDLE.
// Spec: docs/ChronoTron3/mnemonic-concept.md + mnemonic-impl-plan.md
//
// Analog-style delay: changing the delay time (knob, tap, or tape gesture) glides
// a single read tap, so the pitch bends while it moves (varispeed) — never a
// clean-digital crossfade. Colour (filter -> EQ -> tape drive -> degrade) sits
// INSIDE the feedback loop, so repeats age. K2 feedback runs into the always-on
// tape saturation up to a bounded self-oscillation. On top: a Hazarai-style
// hold/loop, and FS1-hold tape gestures (spin-up / slow-down).
//
//   K1 = delay time (SW2 UP) / tap division (SW2 MID) / rhythm stretch (SW2 DOWN)
//   K2 = feedback (0 -> self-oscillation)
//   K3 = degrade — bipolar: CCW BBD/decimate · noon clean · CW tape warble/drive
//   K4 = tone tilt — bipolar: CCW LPF · noon flat · CW HPF (cut-only)
//   K5 = resonance / EQ emphasis at K4's corner (peak -> BPF-ish)
//   K6 = dry/wet mix (shell-owned equal-power; mnemonic does NOT own output)
//   SW1 = FS1-hold gesture: UP spin-up · MID loop record/play · DOWN slow-down
//   SW2 = time mode: UP knob-time · MID tap-tempo · DOWN rhythmic (capture) taps
//   FS1 = tap tempo (tap) / SW1 gesture (hold)
//   FS2 = bypass (tap: gate send, trail rings) / kill+clear (hold)
//   LED1 = delay-clock blink · LED2 = bypass / loop state
//
// Requires daisy.h + hothouse.h + control_surface.h + knob_map.h included first.
//
#include "module.h"
#include "mnemonic_constants.h"
#include "ring_buffer.h"   // core/blocks — mono circular buffer with ReadFrac
#include <math.h>
#include <cstring>         // memset (kill)

// ---------------------------------------------------------------------------
// SDRAM storage — delay line + loop capture. File scope (single TU) is safe.
// ---------------------------------------------------------------------------
static float DSY_SDRAM_BSS mnem_delay_slab[MNEM_DELAY_SAMPLES];
static float DSY_SDRAM_BSS mnem_loop_slab[MNEM_LOOP_SAMPLES];

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
    memset(mnem_loop_slab, 0, MNEM_LOOP_SAMPLES * sizeof(float));

    base_delay_ = 0.001f * 400.f * sr_;   // 400 ms default
    target_eff_ = base_delay_;
    read_delay_ = base_delay_;

    duck_atk_g_ = 1.f - expf(-1.f / (MNEM_DUCK_ATK_MS * 0.001f * sr_));
    duck_rel_g_ = 1.f - expf(-1.f / (MNEM_DUCK_REL_MS * 0.001f * sr_));
    gest_atk_coef_ = 1.f - expf(-1.f / (MNEM_GEST_ATK_MS * 0.001f * sr_));
    gest_rel_coef_ = 1.f - expf(-1.f / (MNEM_GEST_REL_MS * 0.001f * sr_));
    send_coef_ = 1.f - expf(-1.f / (0.003f * sr_));            // 3 ms send gate ramp
    loop_fade_coef_ = 1.f - expf(-1.f / (MNEM_LOOP_FADE_MS * 0.001f * sr_));
    loop_xfade_samps_ = (size_t)(MNEM_LOOP_XFADE_MS * 0.001f * sr_);

    bbd_lp_.SetLP(MNEM_BBD_LP_HZ, sr_);
    tape_hf_lp_.SetLP(18000.f, sr_);
    svf_.Set(MNEM_CORNER_NOON_HZ, 0.7f, sr_);
    wow_inc_  = MNEM_WOW_HZ / sr_;
    flut_inc_ = MNEM_FLUTTER_HZ / sr_;
  }

  void Activate() override {
    snap_ = true;                 // snap read tap to target on first Controls (no sweep-in)
    gesture_engaged_ = false; gest_amt_ = 0.f;
    duck_env_ = 0.f;
    svf_.Reset(); tape_hf_lp_.Reset(); bbd_lp_.Reset();
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

    // ---- K3 degrade character (bipolar) ----------------------------------
    if (k3 >= 0.5f) { tape_amt_ = (k3 - 0.5f) * 2.f; bbd_amt_ = 0.f; }
    else            { bbd_amt_  = (0.5f - k3) * 2.f; tape_amt_ = 0.f; }
    tape_drive_ = MNEM_TAPE_DRIVE + tape_amt_ * MNEM_TAPE_DRIVE_K3;
    tape_hf_lp_.SetLP(Mapf(1.f - tape_amt_, MNEM_TAPE_HFLOSS_HZ, 18000.f), sr_);
    bbd_hold_ = 1 + (int)(bbd_amt_ * (MNEM_BBD_DECIM_MAX - 1));
    bbd_bits_ = Mapf(bbd_amt_, 14.f, MNEM_BBD_BITS_MIN);
    wow_depth_  = MNEM_WOW_DEPTH_MS  * 0.001f * sr_;
    flut_depth_ = MNEM_FLUTTER_DEPTH_MS * 0.001f * sr_;

    // ---- K4 tilt + K5 resonance (peak at the corner) ---------------------
    float corner;
    if (k4 >= 0.5f) { tilt_side_ = +1; tilt_amt_ = (k4 - 0.5f) * 2.f;
                      corner = MNEM_CORNER_NOON_HZ *
                               powf(MNEM_CORNER_HP_MAX_HZ / MNEM_CORNER_NOON_HZ, tilt_amt_); }
    else            { tilt_side_ = -1; tilt_amt_ = (0.5f - k4) * 2.f;
                      corner = MNEM_CORNER_NOON_HZ *
                               powf(MNEM_CORNER_LP_MIN_HZ / MNEM_CORNER_NOON_HZ, tilt_amt_); }
    peak_add_ = k5 * MNEM_PEAK_ADD;
    svf_.Set(corner, Mapf(k5, MNEM_PEAK_Q_MIN, MNEM_PEAK_Q_MAX), sr_);

    // ---- K1 delay time / division / rhythm, by SW2 -----------------------
    pattern_n_ = 0;
    if (sw2_ == 0) {                                   // knob time (free)
      float ms = MNEM_TIME_MIN_MS *
                 powf(MNEM_TIME_MAX_MS / MNEM_TIME_MIN_MS, k1);
      base_delay_ = ms * 0.001f * sr_;
    } else if (sw2_ == 1) {                            // tap tempo -> division
      int d = QuantizeDivision(k1);
      if (have_tempo_)
        base_delay_ = quarter_ms_ * MNEM_DIV_RATIOS[d] * 0.001f * sr_;
    } else {                                           // rhythmic capture taps
      int d = QuantizeDivision(k1);
      if (pattern_iv_n_ >= 1) {
        base_delay_ = pattern_total_ms_ * MNEM_DIV_RATIOS[d] * 0.001f * sr_;
        pattern_n_  = pattern_iv_n_;                   // multi-tap active
      } else if (have_tempo_) {
        base_delay_ = quarter_ms_ * MNEM_DIV_RATIOS[d] * 0.001f * sr_;
      }
    }
    base_delay_ = MnemClamp(base_delay_, 0.001f * MNEM_TIME_MIN_MS * sr_,
                            (float)MNEM_DELAY_SAMPLES - 2.f);
    if (snap_) { read_delay_ = base_delay_; snap_ = false; }

    // ---- FS1: tap tempo / SW1 gesture / loop -----------------------------
    const FootswitchEvent& f1 = cs.Foot(0);
    if (sw1_ == 1) {                                   // MID: hold/loop
      gesture_engaged_ = false;
      if (f1.rising)  StartLoopRecord(now);
      if (f1.falling) StopLoopRecord(now);
    } else {                                           // UP spin-up / DOWN slow-down
      gest_dir_ = (sw1_ == 0) ? +1 : -1;
      if (f1.rising) press_was_gesture_ = false;
      if (f1.down && f1.held_ms >= MNEM_LONGPRESS_MS) {
        gesture_engaged_ = true; press_was_gesture_ = true;
      }
      if (f1.falling) {
        if (!press_was_gesture_ && (sw2_ == 1 || sw2_ == 2)) RegisterTap(now);
        gesture_engaged_ = false;
      }
    }

    // ---- FS2: bypass (tap) / kill (hold) ---------------------------------
    const FootswitchEvent& f2 = cs.Foot(1);
    if (f2.rising) kill_fired_ = false;
    if (f2.down && f2.held_ms >= MNEM_LONGPRESS_MS && !kill_fired_) {
      Kill(); kill_fired_ = true;
    }
    if (f2.falling) {
      if (!kill_fired_) bypassed_ = !bypassed_;
      kill_fired_ = false;
    }
    send_target_ = bypassed_ ? 0.f : 1.f;

    UpdateLeds(now, led1, led2);
  }

  // -------------------------------------------------------------------------
  // Audio-rate: fill `wet` with the mono wet output (shell adds dry via K6).
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    const float wp0 = (float)delay_.GetWritePos();
    const float two_pi = 6.2831853f;

    for (size_t i = 0; i < size; i++) {
      // Loop record (clean input) — capture before any colour.
      if (loop_recording_ && loop_write_ < MNEM_LOOP_SAMPLES) {
        mnem_loop_slab[loop_write_++] = in[i];
        if (loop_write_ >= MNEM_LOOP_SAMPLES) CommitLoop();  // hit ceiling
      }

      // Gesture ramp (spin-up / slow-down envelope).
      float gt = gesture_engaged_ ? 1.f : 0.f;
      gest_amt_ += (gt - gest_amt_) * (gesture_engaged_ ? gest_atk_coef_ : gest_rel_coef_);
      float time_fac = 1.f, fb_target = fb_gain_;
      if (gest_dir_ == +1) { time_fac = 1.f + gest_amt_ * (MNEM_GEST_UP_TIMEFAC - 1.f);   fb_target = MNEM_GEST_UP_FB; }
      else                 { time_fac = 1.f + gest_amt_ * (MNEM_GEST_DOWN_TIMEFAC - 1.f); fb_target = MNEM_GEST_DOWN_FB; }
      target_eff_ = MnemClamp(base_delay_ * time_fac, 1.f, (float)MNEM_DELAY_SAMPLES - 2.f);
      float fb_eff = fb_gain_ + gest_amt_ * (fb_target - fb_gain_);

      // Varispeed glide (THE identity): read tap eases toward its target.
      read_delay_ += (target_eff_ - read_delay_) * MNEM_GLIDE_COEF;

      // Wow/flutter read-tap wobble (tape side of K3).
      float wobble = 0.f;
      if (tape_amt_ > 0.f) {
        wow_ph_  += wow_inc_;  if (wow_ph_  >= 1.f) wow_ph_  -= 1.f;
        flut_ph_ += flut_inc_; if (flut_ph_ >= 1.f) flut_ph_ -= 1.f;
        wobble = (sinf(two_pi * wow_ph_) * wow_depth_ +
                  sinf(two_pi * flut_ph_) * flut_depth_) * tape_amt_;
      }
      const float wp = wp0 + (float)i;

      // Read tap(s): single, or the captured rhythm (multi-tap).
      float delayed;
      if (pattern_n_ > 0) {
        delayed = 0.f;
        for (int t = 0; t < pattern_n_; t++)
          delayed += delay_.ReadFrac(wp - read_delay_ * pattern_ratio_[t] - wobble)
                     * pattern_gain_[t];
      } else {
        delayed = delay_.ReadFrac(wp - read_delay_ - wobble);
      }

      // Feedback build-up ducker: simmer runaway oscillation (wet only).
      float fb = delayed * fb_eff;
      if (MNEM_DUCK_ENABLE) {
        float a = fabsf(delayed);
        duck_env_ += (a > duck_env_ ? duck_atk_g_ : duck_rel_g_) * (a - duck_env_);
        if (duck_env_ > MNEM_DUCK_THRESH) fb *= MNEM_DUCK_THRESH / duck_env_;
      }

      // Loop plays INTO the delay input, parallel with the (gated) dry send.
      send_gain_ += (send_target_ - send_gain_) * send_coef_;
      float loop_s = LoopPlay();

      float x = in[i] * send_gain_ + loop_s + fb;
      if (MNEM_EQ_IN_LOOP) x = Filter(x);              // K4/K5 ages repeats
      x = TapeDrive(x);                                 // always-on saturation
      x = Degrade(x);                                   // tape HF loss / BBD decimate
      delay_.Write(x);

      wet[i] = MNEM_EQ_IN_LOOP ? delayed : Filter(delayed);  // post-loop A/B path
    }
  }

  // mnemonic uses the shell's K6 equal-power mix (dry stays sacrosanct).
  bool OwnsOutput() const override { return false; }

 private:
  // ---- colour stages -----------------------------------------------------
  inline float Filter(float x) {
    float lp, bp, hp;
    svf_.Process(x, lp, bp, hp);
    float tone = (tilt_side_ >= 0) ? (x * (1.f - tilt_amt_) + hp * tilt_amt_)
                                   : (x * (1.f - tilt_amt_) + lp * tilt_amt_);
    return tone + peak_add_ * bp;                       // resonant emphasis at corner
  }
  inline float TapeDrive(float x) {                     // unity small-signal, soft peaks
    float d = tape_drive_;
    return tanhf(x * d) / d;
  }
  inline float Degrade(float x) {
    if (bbd_amt_ > 0.f) {                               // BBD: sample-hold + gentle crush + round
      if (++bbd_counter_ >= bbd_hold_) { bbd_counter_ = 0; bbd_held_ = x; }
      x = bbd_held_;
      float q = powf(2.f, bbd_bits_);
      x = roundf(x * q) / q;
      x = bbd_lp_.LP(x);
    }
    if (tape_amt_ > 0.f) x = tape_hf_lp_.LP(x);         // tape: progressive HF loss
    return x;
  }

  // ---- loop --------------------------------------------------------------
  void StartLoopRecord(uint32_t now) {
    loop_recording_ = true; loop_playing_ = false;
    loop_write_ = 0; loop_rec_start_ms_ = now;
  }
  void StopLoopRecord(uint32_t now) {
    if (!loop_recording_) return;
    loop_recording_ = false;
    if ((now - loop_rec_start_ms_) < MNEM_LOOP_MIN_MS) { loop_len_ = 0; return; }  // too short
    CommitLoop();
  }
  void CommitLoop() {
    loop_recording_ = false;
    loop_len_ = loop_write_;
    loop_read_ = 0;
    loop_gain_ = 0.f; loop_gain_target_ = 1.f;          // fade in
    loop_playing_ = loop_len_ > 0;
  }
  inline float LoopPlay() {
    if (!loop_playing_ || loop_len_ == 0 || bypassed_) return 0.f;  // paused in bypass
    size_t p = loop_read_;
    float s = mnem_loop_slab[p];
    size_t xf = loop_xfade_samps_;
    if (loop_len_ > 2 * xf && p >= loop_len_ - xf) {    // seam crossfade at wrap
      float frac = (float)(p - (loop_len_ - xf)) / (float)xf;
      s = s * (1.f - frac) + mnem_loop_slab[p - (loop_len_ - xf)] * frac;
    }
    if (++loop_read_ >= loop_len_) loop_read_ = 0;
    loop_gain_ += (loop_gain_target_ - loop_gain_) * loop_fade_coef_;
    return s * loop_gain_;
  }

  // ---- tap tempo + rhythm capture ---------------------------------------
  void RegisterTap(uint32_t now) {
    if (last_tap_ms_ != 0) {
      uint32_t iv = now - last_tap_ms_;
      if (iv >= MNEM_TAP_MIN_MS && iv <= MNEM_TAP_WINDOW_MS) {
        if (iv > MNEM_TAP_MAX_MS) iv = MNEM_TAP_MAX_MS;
        tap_iv_[tap_wr_ % MNEM_TAP_MEDIAN_N] = iv;
        tap_wr_++;
        int cnt = tap_wr_ < MNEM_TAP_MEDIAN_N ? tap_wr_ : MNEM_TAP_MEDIAN_N;
        quarter_ms_ = MedianMs(cnt);
        have_tempo_ = true;
        if (sw2_ == 2) AppendPattern(iv);
      } else {                                          // gap too long -> new gesture
        tap_wr_ = 0; pattern_iv_n_ = 0;
      }
    }
    last_tap_ms_ = now;
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
  void AppendPattern(uint32_t iv) {
    if (pattern_iv_n_ < MNEM_MAX_PATTERN_TAPS) pattern_iv_ms_[pattern_iv_n_++] = (float)iv;
    float cum = 0.f;
    for (int k = 0; k < pattern_iv_n_; k++) cum += pattern_iv_ms_[k];
    pattern_total_ms_ = cum > 1.f ? cum : 1.f;
    float run = 0.f;
    for (int k = 0; k < pattern_iv_n_; k++) {
      run += pattern_iv_ms_[k];
      pattern_ratio_[k] = run / pattern_total_ms_;      // (0..1], last = 1
      pattern_gain_[k]  = powf(MNEM_PATTERN_TAP_DECAY, (float)k);
    }
  }

  // ---- kill --------------------------------------------------------------
  void Kill() {
    memset(mnem_delay_slab, 0, MNEM_DELAY_SAMPLES * sizeof(float));  // clear the trail
    loop_len_ = 0; loop_playing_ = false; loop_recording_ = false;
    pattern_iv_n_ = 0; pattern_n_ = 0;
    duck_env_ = 0.f;
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
    if (loop_recording_)      l2 = 1.f;                        // solid while recording
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
  float base_delay_ = 0.f, target_eff_ = 0.f, read_delay_ = 0.f;
  bool  snap_ = true;

  // feedback / drive
  float fb_gain_ = 0.f, tape_drive_ = MNEM_TAPE_DRIVE;
  float duck_env_ = 0.f, duck_atk_g_ = 0.f, duck_rel_g_ = 0.f;

  // filter
  MnemSVF svf_;
  int   tilt_side_ = -1;
  float tilt_amt_ = 0.f, peak_add_ = 0.f;

  // degrade
  float tape_amt_ = 0.f, bbd_amt_ = 0.f;
  MnemOnePole tape_hf_lp_, bbd_lp_;
  int   bbd_hold_ = 1, bbd_counter_ = 0;
  float bbd_held_ = 0.f, bbd_bits_ = 14.f;
  float wow_ph_ = 0.f, flut_ph_ = 0.f, wow_inc_ = 0.f, flut_inc_ = 0.f;
  float wow_depth_ = 0.f, flut_depth_ = 0.f;

  // gestures
  bool  gesture_engaged_ = false, press_was_gesture_ = false;
  int   gest_dir_ = +1;
  float gest_amt_ = 0.f, gest_atk_coef_ = 0.f, gest_rel_coef_ = 0.f;

  // bypass / send gate
  bool  bypassed_ = false, kill_fired_ = false;
  float send_gain_ = 1.f, send_target_ = 1.f, send_coef_ = 0.f;

  // loop
  bool   loop_recording_ = false, loop_playing_ = false;
  size_t loop_len_ = 0, loop_write_ = 0, loop_read_ = 0, loop_xfade_samps_ = 0;
  float  loop_gain_ = 0.f, loop_gain_target_ = 0.f, loop_fade_coef_ = 0.f;
  uint32_t loop_rec_start_ms_ = 0;

  // tap tempo
  uint32_t last_tap_ms_ = 0;
  uint32_t tap_iv_[MNEM_TAP_MEDIAN_N] = {0};
  int   tap_wr_ = 0;
  bool  have_tempo_ = false;
  float quarter_ms_ = 400.f;
  int   div_idx_ = MNEM_DIV_NOON;

  // rhythm pattern (SW2 DOWN)
  int   pattern_iv_n_ = 0, pattern_n_ = 0;
  float pattern_iv_ms_[MNEM_MAX_PATTERN_TAPS] = {0};
  float pattern_ratio_[MNEM_MAX_PATTERN_TAPS] = {0};
  float pattern_gain_[MNEM_MAX_PATTERN_TAPS]  = {0};
  float pattern_total_ms_ = 1.f;

  // modes / leds
  int sw1_ = 0, sw2_ = 0;
  uint32_t clock_next_ms_ = 0, clock_on_until_ = 0;
};
