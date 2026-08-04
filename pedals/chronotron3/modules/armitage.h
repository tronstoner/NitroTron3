#pragma once
//
// armitage — impulse synth / resonator / drone.  SW3 DOWN.
//
// Discovery build. Exciter (asymmetric-saturation conditioning, F3) -> comb
// resonator bank (Karplus-Strong) -> gated-AR filter (K5) -> limiter -> wet.
//
// Note-set BEHAVIOUR is chosen on SW2 (A/B), instead of one fixed chord:
//   SW2 UP   kFixed  — dense semitone bank (~2 oct), the spec's validation bed.
//                      Rings to anything; tests the core/conditioning/drone.
//   SW2 MID  kMono   — one voice tracked to the played pitch (YIN, mono).
//                      Tests note-following for single notes / lines.
//   SW2 DOWN kQuant  — key-quantised multivoice: play an arpeggio, it stacks the
//                      distinct in-key notes into a chord. Poor-man's poly.
// Only true polyphonic *chord detection* (the hard note-set estimator) is
// deferred. Modal core dropped (comb is the keeper); SW1 is now free.
//
// Spec: docs/ChronoTron3/impulse resonator - armitage/IMPULSE_SYNTH_SPEC.md
// Reference math: saturation.py / validate.py.  All tuning in armitage_constants.h.
//
// Header-only; main.cpp is the sole TU and includes daisy.h/daisysp.h/hothouse.h
// + module.h before this. NO dynamic allocation; large comb buffers in SDRAM.

#include <cmath>
#include "module.h"
#include "knob_map.h"          // RemapKnob, MidiToFreq, Mapf
#include "pitch_tracker.h"     // core/blocks — mono YIN (tracked behaviours)
#include "env_follower.h"      // core/blocks — fast 4-pole follower (onset detect)
#include "armitage_constants.h"

// ---------------------------------------------------------------------------
// SDRAM comb delay buffers — one per voice (only one core runs at a time).
// MAX_VOICES = BANK_NOTE_COUNT (25) * COMB_MAX_SAMPLES (2048) * 4 B ≈ 205 KB.
// ---------------------------------------------------------------------------
static float DSY_SDRAM_BSS armitage_comb_buf[armitage_k::MAX_VOICES]
                                            [armitage_k::COMB_MAX_SAMPLES];

// ===========================================================================
class Armitage : public Module {
 public:
  // -------------------------------------------------------------------------
  void Init(float sr) override {
    sr_ = sr;
    for (int v = 0; v < armitage_k::MAX_VOICES; v++) combs_[v].Init(armitage_comb_buf[v]);
    tracker_.Init(sr);
    dc_x1_ = dc_y1_ = 0.f;
    lim_env_ = 0.f;
    asym_ = 0.f; structure_ = 0.f; register_oct_ = 0.f;
    t60_ = 1.0f;
    // Onset envelope (fast 4-pole follower) + K5 gated-AR filter envelope.
    onset_env_.Init(sr_);
    onset_env_.SetCutoff(armitage_k::ONSET_ENV_HZ);
    env_val_ = 0.f;
    gate_env_ = 0.f;
    gate_rel_c_ = 1.f - expf(-1.f / (armitage_k::GATE_HOLD_MS * 0.001f * sr_));
    post_drive_ = ExpMap(0.5f, armitage_k::POST_DRIVE_MIN, armitage_k::POST_DRIVE_MAX);  // LOCKED at K4-noon
    glide_time_ms_ = armitage_k::GLIDE_TIME_MIN_MS;   // K4 portamento time (driven from the knob)
    post_bias_tanh_ = tanhf(armitage_k::POST_DRIVE_BIAS);   // static DC of the asym bias
    post_gain_ = armitage_k::POST_DRIVE_MAKEUP / sqrtf(post_drive_);
    chord_.Init(sr_);
    snap_pending_ = false; snap_timer_ = 0;
    snap_delay_samps_ = (int)(armitage_k::CHORD_SNAP_DELAY_MS * 0.001f * sr_);
    attack_blank_samps_ = (int)(armitage_k::CHORD_ATTACK_BLANK_MS * 0.001f * sr_);
    measure_timer_ = 0;
    delay_smooth_c_ = OnePoleCoeff(armitage_k::DELAY_SMOOTH_MS);   // audio-rate de-click on retune
    voice_norm_cur_ = voice_norm_;
    vnorm_smooth_c_ = OnePoleCoeff(armitage_k::VNORM_SMOOTH_MS);   // de-click voice-count level steps
    for (int v = 0; v < armitage_k::MAX_VOICES; v++) vgain_[v] = 0.f;
    sum_upto_ = 0;
    fade_step_ = 1.f / (armitage_k::FADE_MS * 0.001f * sr_);       // per-voice fade in/out
    onset_ref_ = 0.f; onset_fast_ = 0.f; onset_hp_lp_ = 0.f;
    onset_hp_c_ = 1.f - expf(-6.2831853f * armitage_k::ONSET_HP_HZ / sr_);   // HPF (one-pole) coeff
    onset_fast_c_ = OnePoleCoeff(armitage_k::ONSET_FAST_MS);
    onset_ref_c_ = OnePoleCoeff(armitage_k::ONSET_REF_MS);
    onset_refractory_ = 0;
    onset_refractory_samps_ = (int)(armitage_k::ONSET_REFRACTORY_MS * 0.001f * sr_);
    exc_env_ = 1.f; exc_target_ = 1.f;
    exc_atk_c_  = OnePoleCoeff(armitage_k::EXC_ATTACK_MS);
    exc_duck_c_ = OnePoleCoeff(armitage_k::EXC_DUCK_MS);
    exc_coeff_  = exc_atk_c_;
    if (armitage_k::CHORD_DETECT) active_voices_ = 0;   // silent until the first strum
    fenv_ = 0.f; fenv_gate_ = false; fenv_armed_ = true;
    fenv_atk_c_ = 0.1f; fenv_rel_c_ = 0.01f;
    lp1_ = lp2_ = lp3_ = lp4_ = 0.f;
    g_closed_     = 6.2831853f * armitage_k::OUTFILT_CLOSED_HZ / sr_;
    filt_octaves_ = log2f(armitage_k::OUTFILT_OPEN_HZ / armitage_k::OUTFILT_CLOSED_HZ);
    lim_atk_c_ = 1.f - expf(-1.f / (armitage_k::LIMIT_ATK_MS * 0.001f * sr_));
    lim_rel_c_ = 1.f - expf(-1.f / (armitage_k::LIMIT_REL_MS * 0.001f * sr_));
    behavior_ = kFixed; prev_behavior_ = kFixed;
    quant_count_ = 0; last_quant_ = -999.f;
    voice_norm_ = 1.f;
    BuildNotes();
    RecomputeVoices();
  }

  void Activate()   override { /* keep state; ring naturally decays */ }
  void Deactivate() override {}

  // -------------------------------------------------------------------------
  // Control-rate (~10 ms). Surface -> params, note set, per-voice coeffs.
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    // K6 = mix (shell). SW3 = module select (shell).

    // SW1 free again (FM axes locked in from the exploration pass).
    const int sw2 = cs.Switch(1);   // behaviour: UP=fixed, MID=mono, DOWN=quant
    behavior_ = (sw2 == 0) ? kFixed : (sw2 == 1) ? kMono : kQuant;

    // K4 = portamento TIME (ms) — true fixed-time linear glide. CCW ≈ instant, CW = long
    // slide. The post-loop saturator drive is LOCKED at its former K4-noon value (in Init).
    Smooth(glide_time_ms_, ExpMap(k4, armitage_k::GLIDE_TIME_MIN_MS, armitage_k::GLIDE_TIME_MAX_MS));
    asym_ = armitage_k::PULSE_ASYM;   // fixed pre-conditioning asymmetry (input side)
    // K3 = FM depth (the gnarl amount). Modulator cutoff tracks pitch per voice (set in
    // RecomputeVoices) and in-loop drive is locked off — both fixed from the SW1
    // exploration pass, so K3 has a single job again.
    Smooth(structure_, k3);
    vp_.ap_a      = 0.f;                                       // dispersion off — FM is the gnarl
    vp_.fm_depth  = structure_ * armitage_k::FM_DEPTH_FRAC_MAX;
    vp_.sat_drive = armitage_k::FM_DRIVE_OFF;                  // in-loop drive off (locked)
    vp_.sat_trim  = armitage_k::COMB_SAT_UNITY / armitage_k::FM_DRIVE_OFF;
    vp_.delay_smooth_c = delay_smooth_c_;                      // audio-rate retune de-click
    // K1 quantised register: snap to the meaningful steps (octaves + fifths) so
    // octaves lock cleanly. Smoothed → a brief glide between detents, settling
    // exactly on the step.
    {
      int ri = (int)(k1 * (float)(armitage_k::REGISTER_STEPS_N - 1) + 0.5f);
      if (ri < 0) ri = 0;
      if (ri >= armitage_k::REGISTER_STEPS_N) ri = armitage_k::REGISTER_STEPS_N - 1;
      const float reg = armitage_k::REGISTER_CENTER_OCT
                      + (float)armitage_k::REGISTER_STEPS_SEMI[ri] / 12.f;
      Smooth(register_oct_, reg);
    }
    t60_ = ExpMap(k2, armitage_k::T60_MIN_S, armitage_k::T60_MAX_S);

    // K5 bipolar: noon = shortest attack+release; CCW stretches attack, CW
    // stretches release.
    float atk_ms, rel_ms;
    if (k5 < 0.5f) {                                   // CCW → longer attack
      atk_ms = Mapf((0.5f - k5) * 2.f, armitage_k::FENV_ATK_MIN_MS, armitage_k::FENV_ATK_MAX_MS);
      rel_ms = armitage_k::FENV_REL_MIN_MS;
    } else {                                           // CW → longer release
      atk_ms = armitage_k::FENV_ATK_MIN_MS;
      rel_ms = Mapf((k5 - 0.5f) * 2.f, armitage_k::FENV_REL_MIN_MS, armitage_k::FENV_REL_MAX_MS);
    }
    fenv_atk_c_ = OnePoleCoeff(atk_ms);
    fenv_rel_c_ = OnePoleCoeff(rel_ms);

    // Run YIN only when a behaviour needs it. Chord-detect and the fixed bank /
    // fixed stage-1 pitch skip it (heavy); the detector uses its own filterbank.
    const bool track = armitage_k::CHORD_DETECT ? false
                     : armitage_k::STAGE1_TEST  ? armitage_k::STAGE1_TRACK
                                                : (behavior_ != kFixed);
    if (track) tracker_.Update();

    BuildNotes();
    RecomputeVoices();

    // LED1 = input activity (play indicator). LED2 = filter-envelope openness.
    float l1 = env_val_ * 8.f; if (l1 > 1.f) l1 = 1.f;
    led1.Set(l1);
    led2.Set(fenv_);
  }

  // -------------------------------------------------------------------------
  // Audio-rate. Fill `wet` (mono). Shell mixes dry/wet via K6.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // Onset envelope (fast 4-pole follower) — tracker gate, quant gate, K5.
      env_val_ = onset_env_.Process(x);
      tracker_.Feed(x, env_val_);

      // --- Onset chord detector: filterbank runs every sample; an onset schedules
      // a snapshot after CHORD_SNAP_DELAY (settle past the pick transient). At the
      // snapshot: peak-pick + harmonic-sieve → retune the bank → needle-excite the
      // new chord. All note management for this mode lives here (audio thread). ---
      if (armitage_k::CHORD_DETECT) {
        chord_.Process(x);
        // PEAK/onset detection — fires on each new ATTACK (env spikes above a slow
        // baseline), independent of the K5 gate, so a fresh strum re-detects even while
        // the previous chord still rings. Refractory stops one attack's rise re-firing.
        // HIGH-PASS the onset-detection input (HFC onset detection): the pick attack is
        // broadband/HF, the sustained tone + its low-fundamental ripple are LOW — so the
        // HPF emphasises transients and removes the ripple that used to retrigger every
        // cycle. onset_fast_ then rectifies + smooths |HPF| into an attack envelope.
        onset_hp_lp_ += onset_hp_c_ * (x - onset_hp_lp_);
        const float x_hp = x - onset_hp_lp_;
        onset_fast_ += onset_fast_c_ * (fabsf(x_hp) - onset_fast_);
        if (onset_refractory_ > 0) onset_refractory_--;
        if (onset_fast_ > armitage_k::ONSET_HP_FLOOR &&
            onset_fast_ > onset_ref_ * armitage_k::ONSET_RISE_RATIO &&
            onset_refractory_ <= 0) {
          snap_pending_ = true; snap_timer_ = snap_delay_samps_;   // (re)schedule the grab
          onset_refractory_ = onset_refractory_samps_;
          onset_ref_ = onset_fast_;                 // jump baseline up so this attack won't refire
          exc_target_ = armitage_k::EXC_DUCK_LEVEL; // DUCK the feed through the settle + retune
          exc_coeff_  = exc_duck_c_;
          measure_timer_ = attack_blank_samps_;     // blank the attack; measure starts after it
          chord_.StopMeasure();                     // don't accumulate the attack noise
        }
        onset_ref_ += onset_ref_c_ * (onset_fast_ - onset_ref_);   // baseline tracks smoothed env
        // Attack-blank expiry → start measuring the settled region (drops the attack).
        if (measure_timer_ > 0 && --measure_timer_ == 0) chord_.StartMeasure();
        if (snap_pending_ && --snap_timer_ <= 0) {
          snap_pending_ = false;
          int n = chord_.Snapshot(snap_notes_, armitage_k::CHORD_MAX_NOTES);
          if (n > 0) {
            AssignVoices(snap_notes_, n);   // nearest-note voice leading → musical glides
            active_voices_ = n;
            voice_norm_ = 1.f / sqrtf((float)n);
            RecomputeVoices();      // retune the bank to the new chord
            exc_target_ = 1.f;      // ATTACK the feed in — swells the excitation into the
            exc_coeff_  = exc_atk_c_;  // freshly-locked (and gliding) chord, no burst
            if (armitage_k::DEBUG_LOG) {          // hand the detected set to the main-loop logger
              dbg_n_ = n;
              for (int i = 0; i < n; i++) dbg_notes_[i] = note_midi_[i];
              dbg_new_ = true;
            }
          }
        }
      }

      // --- Excitation = the conditioned INPUT signal only (pulse trigger dropped —
      // the resonator is driven continuously by what you play). PRE-conditioning:
      // asymmetric tanh drive (DRIVE, asym_) → the "pre" half of the timbre; the
      // "post" half is the K4 drive on the output (below). ---
      const float t = tanhf(armitage_k::DRIVE * x);
      float inp = (1.f - asym_) * t + asym_ * fabsf(t);
      inp = DcBlock(inp) * armitage_k::EXCITE_GAIN;
      // Excitation-envelope guardrail: ducked on each onset (kills the old-chord burst
      // during the settle + glide), attacks back once the new chord locks (below).
      exc_env_ += exc_coeff_ * (exc_target_ - exc_env_);
      const float e = inp * exc_env_;

      // --- Comb resonator bank (active_voices_ tunings). ---
      // Per-voice fade in/out (declick add & drop). Active slots (< active_voices_) ramp
      // their gain to 1; dropped slots (the fading tail, >= active_voices_) ramp to 0 and
      // KEEP ringing until silent, then retire (Silence → clean for re-use). sum_upto_
      // spans active + the fading tail. Linear ramp = clean arrival/retire.
      if (sum_upto_ < active_voices_) sum_upto_ = active_voices_;
      float y = 0.f;
      for (int v = 0; v < sum_upto_; v++) {
        const float tgt = (v < active_voices_) ? 1.f : 0.f;
        const float d = tgt - vgain_[v];
        if      (d >  fade_step_) vgain_[v] += fade_step_;
        else if (d < -fade_step_) vgain_[v] -= fade_step_;
        else                      vgain_[v]  = tgt;
        y += combs_[v].Process(e, vp_) * vgain_[v];
      }
      while (sum_upto_ > active_voices_ && vgain_[sum_upto_ - 1] <= 0.f) {
        combs_[sum_upto_ - 1].Silence();          // fully faded → retire (clean buffer for re-use)
        sum_upto_--;
      }
      // Smooth the 1/√n level normalization: when the detected note COUNT changes at a
      // snapshot, voice_norm_ would otherwise STEP → a level pop/click (K4-independent).
      // Ramp it over a few ms instead.
      voice_norm_cur_ += vnorm_smooth_c_ * (voice_norm_ - voice_norm_cur_);
      y *= armitage_k::COMB_MAKEUP * voice_norm_cur_;

      // --- Post-loop drive (K4): asymmetric waveshaper on the resonator OUTPUT,
      // before the K5 filter (amp topology: drive → tone). OUTSIDE the feedback loop,
      // so it can be pushed hard with ZERO stability risk (in-loop drive ran away).
      // Fixed bias adds even-harmonic "saw" fatness; the static tanh(bias) DC is
      // subtracted so no offset reaches the filter. post_gain_ = drive-dependent level
      // compensation (1/√drive, precomputed at control rate) so cranking K4 adds grit,
      // not volume. ---
      y = (tanhf(post_drive_ * y + armitage_k::POST_DRIVE_BIAS) - post_bias_tanh_) * post_gain_;

      // --- K5 gated-AR 4-pole LP (Moog-style, non-resonant). ---
      // The resonator bank self-oscillates (rings indefinitely), so the FILTER
      // ends the note, not a resonator decay (Lost+Found: "the filter closes only
      // when no input signal"). Clean hysteresis gate on input presence: open
      // while you play, release toward closed (K5 release) when the input falls
      // silent. Filter stays FULLY open the whole time the gate is on — no fixed
      // hold, no early roll-off. K5 sets attack (swell-in) and release (fade-out).
      if (env_val_ > armitage_k::ONSET_ON)       fenv_gate_ = true;
      else if (env_val_ < armitage_k::ONSET_OFF) fenv_gate_ = false;
      const float ftgt = fenv_gate_ ? 1.f : 0.f;
      fenv_ += (fenv_gate_ ? fenv_atk_c_ : fenv_rel_c_) * (ftgt - fenv_);
      float fg = g_closed_ * exp2f(fenv_ * filt_octaves_);   // exponential cutoff sweep
      if (fg > 0.99f) fg = 0.99f;
      lp1_ += fg * (y    - lp1_);
      lp2_ += fg * (lp1_ - lp2_);
      lp3_ += fg * (lp2_ - lp3_);
      lp4_ += fg * (lp3_ - lp4_);

      wet[i] = Limit(lp4_);
    }
  }

  // Debug (serial): if a new chord snapshot has occurred since the last call, copy its
  // detected MIDI notes into out[] and return true. Called from the MAIN loop only.
  bool DebugTakeSnapshot(float* out, int maxn, int& n) {
    if (!dbg_new_) return false;
    dbg_new_ = false;
    n = (dbg_n_ < maxn) ? dbg_n_ : maxn;
    for (int i = 0; i < n; i++) out[i] = dbg_notes_[i];
    return true;
  }

 private:
  enum Behavior { kFixed = 0, kMono = 1, kQuant = 2 };

  // FM / in-loop-drive params handed to every CombVoice each sample. K3 sweeps ONE of
  // these (chosen by SW1); the rest hold at fixed defaults. Set in Controls().
  struct VoiceParams {
    float ap_a      = 0.f;               // dispersion allpass coeff (0 = off)
    float fm_depth  = 0.f;               // FM depth (fraction of the delay period) = K3
    float sat_drive = armitage_k::FM_DRIVE_OFF;   // in-loop saturator drive (locked: off)
    float sat_trim  = 1.f;               // DRIVE*TRIM = COMB_SAT_UNITY (stable loop gain)
    float delay_smooth_c = 1.f;          // per-sample read-delay smoothing coeff (DELAY_SMOOTH_MS)
  };
  // (The modulator LP cutoff is per-voice — combs_[v].fm_mod_c_v_ — since it tracks pitch.)

  // ---------------------------------------------------------------- Comb voice
  struct CombVoice {
    float* buf = nullptr;
    int    wp  = 0;
    float  g     = 0.f;
    float  dfrac = 1.f;       // TARGET read delay (set at control rate by RecomputeVoices)
    float  dfrac_cur_ = 1.f;  // audio-rate-smoothed read delay (click-free retune)
    float  ap_x[armitage_k::DISP_STAGES] = {};
    float  ap_y[armitage_k::DISP_STAGES] = {};
    float  fb_prev_ = 0.f;    // last loop output — feeds the delay self-modulation (FM)
    float  fm_mod_  = 0.f;    // lowpassed modulator: fb_prev_ through a one-pole LP
    float  fm_mod_c_v_ = 1.f; // per-voice modulator LP coeff (cutoff tracks this voice's pitch)

    void Init(float* b) {
      buf = b;
      for (int i = 0; i < armitage_k::COMB_MAX_SAMPLES; i++) buf[i] = 0.f;
      wp = 0;
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) { ap_x[s] = 0.f; ap_y[s] = 0.f; }
      fb_prev_ = 0.f; fm_mod_ = 0.f; dfrac_cur_ = dfrac;
    }
    // Clear buffer + loop state so a RE-activated slot starts from silence instead of
    // popping the stale audio left from whatever note it last held (declick on add).
    void Silence() {
      for (int i = 0; i < armitage_k::COMB_MAX_SAMPLES; i++) buf[i] = 0.f;
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) { ap_x[s] = 0.f; ap_y[s] = 0.f; }
      fb_prev_ = 0.f; fm_mod_ = 0.f; wp = 0; dfrac_cur_ = dfrac;
    }
    inline float Read(float d) const {
      int   di = (int)d;
      float f  = d - (float)di;
      int   i0 = wp - 1 - di;
      int   i1 = i0 - 1;
      i0 %= armitage_k::COMB_MAX_SAMPLES; if (i0 < 0) i0 += armitage_k::COMB_MAX_SAMPLES;
      i1 %= armitage_k::COMB_MAX_SAMPLES; if (i1 < 0) i1 += armitage_k::COMB_MAX_SAMPLES;
      const float a = buf[i0];
      const float b = buf[i1];
      return a + (b - a) * f;
    }
    inline float Process(float x, const VoiceParams& p) {
      // Feedback FM: modulate the delay-read position by the loop's own output →
      // audio-rate phase modulation → inharmonic "gnarl" sidebands. The modulator is
      // LOWPASSED first by fm_mod_c_v_ — a PER-VOICE coeff whose cutoff tracks this
      // voice's pitch (cutoff = MULT × fundamental), so the modulator stays on the
      // fundamental at every note instead of fizzing the highs / starving the lows. Clamped.
      // Audio-rate delay smoothing: micro-glide the read delay toward its target every
      // sample so a retune never STEPS (a control-rate dfrac jump clicks the ringing
      // line). Click-free regardless of K4 portamento — the musical glide rides on dfrac.
      dfrac_cur_ += p.delay_smooth_c * (dfrac - dfrac_cur_);
      fm_mod_ += fm_mod_c_v_ * (fb_prev_ - fm_mod_);
      float md = dfrac_cur_ * (1.f + p.fm_depth * fm_mod_);   // fm_depth = fraction of the period
      if (md < 1.f) md = 1.f;
      const float mmax = (float)(armitage_k::COMB_MAX_SAMPLES - 3);
      if (md > mmax) md = mmax;
      float lp = 0.5f * (Read(md) + Read(md + 1.f));
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) {
        const float in = lp;
        const float o  = p.ap_a * in + ap_x[s] - p.ap_a * ap_y[s];
        ap_x[s] = in; ap_y[s] = o;
        lp = o;
      }
      // In-loop saturator: bounds the loop so g can sit AT/ABOVE unity — the
      // resonator then self-oscillates (builds to a sustained level and holds)
      // instead of decaying, and the nonlinearity breeds harmonics. Small-signal
      // gain = DRIVE*TRIM (≈1 → plucky notes keep their T60); large-signal → tanh
      // caps it so over-unity g doesn't blow up. FIXED trim (not 1/drive).
      float fb = tanhf(p.sat_drive * g * lp) * p.sat_trim;
      fb_prev_ = fb;                 // drives next sample's delay self-modulation (FM)
      const float y = x + fb;
      buf[wp] = y;
      wp++; if (wp >= armitage_k::COMB_MAX_SAMPLES) wp = 0;
      return y;
    }
  };

  // -------------------------------------------- onset chord detector
  // Chromatic bandpass filterbank (24 bins, 2 octaves). Runs continuously; a
  // Snapshot() call peak-picks + harmonic-sieves the current band energies into a
  // note set. The struck-chord grab (see armitage_constants.h CHORD_*).
  struct ChordDetector {
    float coef_[armitage_k::CHORD_N_BINS][5] = {};   // RBJ bandpass (constant 0 dB)
    float z1_[armitage_k::CHORD_N_BINS] = {};
    float z2_[armitage_k::CHORD_N_BINS] = {};
    float env_[armitage_k::CHORD_N_BINS] = {};       // per-bin measured energy (window average)
    float acc_[armitage_k::CHORD_N_BINS] = {};       // energy accumulator over the settled window
    int   acc_n_ = 0;                                 // samples accumulated
    bool  measuring_ = false;                         // accumulating? (only in the post-attack window)

    void Init(float sr) {
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) {
        float f  = 440.f * powf(2.f, (armitage_k::CHORD_BASE_MIDI + (float)b - 69.f) / 12.f);
        float w  = 6.2831853f * f / sr, cs = cosf(w), sn = sinf(w);
        float al = sn / (2.f * armitage_k::CHORD_Q), a0 = 1.f + al;
        coef_[b][0] = al / a0;  coef_[b][1] = 0.f;  coef_[b][2] = -al / a0;
        coef_[b][3] = -2.f * cs / a0;  coef_[b][4] = (1.f - al) / a0;
        z1_[b] = z2_[b] = env_[b] = 0.f; acc_[b] = 0.f;
      }
      acc_n_ = 0; measuring_ = false;
    }
    inline void Process(float x) {   // biquad per bin; accumulate |band| ONLY while measuring
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) {
        float y = coef_[b][0] * x + z1_[b];
        z1_[b] = coef_[b][1] * x - coef_[b][3] * y + z2_[b];
        z2_[b] = coef_[b][2] * x - coef_[b][4] * y;
        if (measuring_) acc_[b] += fabsf(y);
      }
      if (measuring_) acc_n_++;
    }
    // Attack-blank control: StopMeasure() blanks (no accumulation) through the attack;
    // StartMeasure() clears the accumulators and begins measuring the settled region.
    void StopMeasure()  { measuring_ = false; }
    void StartMeasure() {
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) acc_[b] = 0.f;
      acc_n_ = 0; measuring_ = true;
    }
    // Peak-pick → FUNDAMENTAL filter (sub-harmonic) → fine pitch. Writes MIDI notes
    // into notes[], strongest first; returns the count.
    //
    // The hard problem (measured on bass): each plucked string lights up its whole
    // harmonic series, and the 2nd harmonic (octave) is often LOUDER than the
    // fundamental. So we cannot pick "loudest = the note", nor drop overtones *above*
    // an accepted note (the fundamental sits BELOW its octave). Instead: a peak is an
    // OVERTONE if a sub-multiple below it (f/2 f/3 f/4 f/5 → −12 −19 −24 −28 semitones)
    // carries comparable energy → drop it. What survives are the fundamentals. This
    // collapses each string's series to one note (a played octave also collapses to its
    // root — acceptable; voicing comes from register, not detection).
    int Snapshot(float* notes, int maxn) {
      // Band energies = AVERAGE over the settled window (attack was blanked, never
      // accumulated) → no peak-hold, sustained notes read evenly.
      measuring_ = false;
      if (acc_n_ <= 0) return 0;
      const float inv = 1.f / (float)acc_n_;
      float mx = 0.f;
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) {
        env_[b] = acc_[b] * inv;
        if (env_[b] > mx) mx = env_[b];
      }
      if (mx < armitage_k::CHORD_ABS_FLOOR) return 0;

      // Candidate = local maximum above a LOW gate (low, because a real fundamental is
      // often quieter than its own 2nd harmonic and must still qualify).
      float cthr = armitage_k::CHORD_CAND_THR * mx;
      if (cthr < armitage_k::CHORD_ABS_FLOOR) cthr = armitage_k::CHORD_ABS_FLOOR;
      bool cand[armitage_k::CHORD_N_BINS];
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) {
        bool p = env_[b] >= cthr;
        if (b > 0 && env_[b] < env_[b - 1]) p = false;                         // local max
        if (b < armitage_k::CHORD_N_BINS - 1 && env_[b] < env_[b + 1]) p = false;
        cand[b] = p;
      }

      // Fundamental filter: drop a candidate that has an energetic sub-multiple below it
      // (it's a harmonic of a lower note). We DELIBERATELY keep octaves (−12) and 2-oct
      // (−24) so octave-DOUBLED chord tones survive (guitar voicing richness) — only the
      // 12th (−19, stray fifth) and 2-oct+major-3rd (−28, clashy third) harmonics are
      // collapsed, since those add a wrong pitch class. A bass single note now also rings
      // its octave (consonant doubling), which is fine post range-fix.
      static const int SUB[2] = {19, 28};   // −12th, −(2oct+major3rd)  (octaves kept on purpose)
      for (int b = 0; b < armitage_k::CHORD_N_BINS; b++) {
        if (!cand[b]) continue;
        for (int k = 0; k < 2; k++) {
          const int lb = b - SUB[k];
          if (lb >= 0 && env_[lb] > armitage_k::CHORD_SUBHARM_REL * env_[b]) { cand[b] = false; break; }
        }
      }

      // Emit surviving fundamentals, strongest first.
      int count = 0;
      while (count < maxn) {
        int best = -1; float bestE = 0.f;
        for (int b = 0; b < armitage_k::CHORD_N_BINS; b++)
          if (cand[b] && env_[b] > bestE) { bestE = env_[b]; best = b; }
        if (best < 0) break;
        cand[best] = false;
        // Fine pitch: parabolic sub-bin interpolation on log energies (skip at the array
        // edges — no neighbour there, which otherwise pins a bogus ±50 c).
        float midi = armitage_k::CHORD_BASE_MIDI + (float)best;
        if (best > 0 && best < armitage_k::CHORD_N_BINS - 1) {
          const float lm = logf(env_[best] + 1e-9f);
          const float ll = logf(env_[best - 1] + 1e-9f);
          const float lr = logf(env_[best + 1] + 1e-9f);
          const float den = ll - 2.f * lm + lr;
          float d = (fabsf(den) > 1e-9f) ? 0.5f * (ll - lr) / den : 0.f;
          if (d > 0.5f) d = 0.5f; else if (d < -0.5f) d = -0.5f;
          midi += d;
        }
        notes[count++] = midi;
      }
      return count;
    }
  };

  // -------------------------------------------------- note-set per behaviour
  void BuildNotes() {
    // Chord-detect mode owns the note set (set from the audio thread on snapshot).
    if (armitage_k::CHORD_DETECT) return;
    // STAGE-1 test: a detuned STACK on one fixed sub-octave pitch (K1 register
    // still shifts the whole stack). Several near-unison voices + a harmonic
    // partial or two → "multiple stacked oscillators" that beat/shimmer into a
    // wall. No SW2 behaviours, no YIN — isolates the self-oscillating core.
    if (armitage_k::STAGE1_TEST) {
      const float base = armitage_k::STAGE1_TRACK ? tracker_.GetMidiNoteContinuous()
                                                  : armitage_k::STAGE1_TEST_MIDI;
      active_voices_ = armitage_k::STAGE1_STACK_N;
      for (int v = 0; v < armitage_k::STAGE1_STACK_N; v++)
        note_midi_[v] = base + armitage_k::STAGE1_STACK[v];
      voice_norm_ = 1.f / sqrtf((float)armitage_k::STAGE1_STACK_N);
      return;
    }
    // On (re)entering quant, start the stack fresh.
    if (behavior_ == kQuant && prev_behavior_ != kQuant) {
      quant_count_ = 0; last_quant_ = -999.f;
    }
    prev_behavior_ = behavior_;

    switch (behavior_) {
      case kFixed:
        active_voices_ = armitage_k::BANK_NOTE_COUNT;
        for (int v = 0; v < active_voices_; v++)
          note_midi_[v] = armitage_k::BANK_BASE_MIDI + v * armitage_k::BANK_STEP_SEMI;
        break;

      case kMono:
        active_voices_ = armitage_k::MONO_VOICES;               // 1
        note_midi_[0]  = tracker_.GetMidiNoteContinuous();
        break;

      case kQuant: {
        // Accumulate distinct in-key notes while a note is playing.
        if (env_val_ > armitage_k::QUANT_GATE_ENV) {
          const float q = QuantizeToScale(tracker_.GetMidiNoteContinuous());
          if (q != last_quant_) {
            last_quant_ = q;
            bool present = false;
            for (int v = 0; v < quant_count_; v++) if (quant_notes_[v] == q) present = true;
            if (!present) {
              if (quant_count_ < armitage_k::QUANT_MAX_VOICES) {
                quant_notes_[quant_count_++] = q;
              } else {                                            // FIFO evict oldest
                for (int v = 1; v < armitage_k::QUANT_MAX_VOICES; v++)
                  quant_notes_[v - 1] = quant_notes_[v];
                quant_notes_[armitage_k::QUANT_MAX_VOICES - 1] = q;
              }
            }
          }
        }
        active_voices_ = quant_count_;
        for (int v = 0; v < active_voices_; v++) note_midi_[v] = quant_notes_[v];
        break;
      }
    }

    // Quant can be empty before you play → 0 voices = silent. Guard the norm.
    // (All behaviours are silent without input anyway — resonators need
    // excitation — so this only silences the "nothing accumulated yet" case.)
    voice_norm_ = (active_voices_ > 0) ? 1.f / sqrtf((float)active_voices_) : 1.f;
  }

  // Voice leading for portamento: instead of gliding slot-by-index (arbitrary leaps),
  // assign each new detected note to the currently-ringing voice NEAREST to it, so
  // continuing voices move the smallest musical distance. Extra new voices (bigger
  // chord) enter at pitch; dropped voices (smaller chord) simply fall silent. Greedy
  // per slot — n ≤ CHORD_MAX_NOTES, so cost is negligible. Called on each snapshot
  // BEFORE active_voices_ is updated (reads the previous count as `prev`).
  void AssignVoices(const float* newnotes, int n) {
    const int prev = active_voices_;
    bool used[armitage_k::MAX_VOICES] = {};
    const int cont = (prev < n) ? prev : n;
    for (int v = 0; v < cont; v++) {            // continuing voice → nearest unused new note
      int bestj = 0; float bestd = 1e9f;
      for (int j = 0; j < n; j++) {
        if (used[j]) continue;
        const float d = fabsf(newnotes[j] - midi_cur_[v]);
        if (d < bestd) { bestd = d; bestj = j; }
      }
      note_midi_[v] = newnotes[bestj];
      used[bestj] = true;                        // midi_cur_[v] kept → glides the short way
    }
    for (int v = cont; v < n; v++) {           // extra (newly-activated) voice → leftover note
      int j = 0; while (j < n && used[j]) j++;
      note_midi_[v] = newnotes[j];
      used[j] = true;
      midi_cur_[v] = newnotes[j];              // enter at pitch (its gain fades in — see Process)
    }
    // Fixed-TIME linear glide: per voice, a constant step so it reaches its target in
    // glide_time_ms_ (same time for any interval). Fresh voices → dist 0 → step 0.
    const float updates = glide_time_ms_ / armitage_k::GLIDE_CTRL_MS;
    for (int v = 0; v < n; v++) {
      const float dist = note_midi_[v] - midi_cur_[v];
      glide_step_[v] = (updates > 1.f) ? dist / updates : dist;   // <1 tick → snap in one update
    }
  }

  // ------------------------------------------------------ coefficient recompute
  void RecomputeVoices() {
    // FM / drive params live in vp_ (set in Controls). Here we (re)compute per-voice
    // delay length + loop gain, gliding each voice's pitch toward its target note.
    for (int v = 0; v < active_voices_; v++) {
      // Legato/portamento: TRUE fixed-time LINEAR glide — march by the constant per-voice
      // step (set in AssignVoices from glide_time_ms_) and STOP exactly on arrival, so any
      // interval takes the same time and lands cleanly (no exponential creep). A fresh
      // voice (midi_cur_ still 0) enters at pitch. Register (K1) is a separate multiplier.
      float cur = midi_cur_[v];
      const float tgt = note_midi_[v];
      if (cur <= 0.f) cur = tgt;
      const float step = glide_step_[v];
      cur += step;
      if ((step >= 0.f && cur >= tgt) || (step <= 0.f && cur <= tgt)) cur = tgt;  // arrive & stop
      midi_cur_[v] = cur;

      float f = MidiToFreq(cur) * Exp2f(register_oct_);
      if (f < 1.f) f = 1.f;

      // Comb: Dtot = fs/f, g from F1 T60 relation using the actual Dtot.
      float dtot = sr_ / f;
      const float maxd = (float)(armitage_k::COMB_MAX_SAMPLES - 3);
      if (dtot > maxd) dtot = maxd;
      if (dtot < 2.f)  dtot = 2.f;
      combs_[v].dfrac = dtot - 0.5f;
      // Per-voice FM modulator cutoff tracks THIS voice's pitch: cutoff = MULT × f, and
      // f = sr/dtot, so the one-pole coeff = 1 - exp(-2π·MULT/dtot). Low notes (large
      // dtot) → small coeff = low cutoff (deep); high notes → coeff→1 (bright).
      float mc = 1.f - expf(-6.2831853f * armitage_k::FM_MOD_TRACK_MULT / dtot);
      if (mc > 1.f) mc = 1.f;
      combs_[v].fm_mod_c_v_ = mc;
      // T60-derived loop gain (register-correct), then LOOP_BOOST pushes the
      // long-T60 (K2 CW) end AT/OVER unity → self-oscillation (infinite, building)
      // while short T60 still decays (plucky). The in-loop saturator bounds it.
      float g = powf(10.f, -3.f * dtot / (t60_ * sr_)) * armitage_k::LOOP_BOOST;
      if (g > armitage_k::SELF_OSC_G_MAX) g = armitage_k::SELF_OSC_G_MAX;
      combs_[v].g = g;
    }
  }

  // ---------------------------------------------------------------- helpers
  // Quantise a MIDI note to the nearest note of QUANT_SCALE in the key.
  static float QuantizeToScale(float midi_in) {
    int n   = (int)lroundf(midi_in);
    int rel = n - (int)armitage_k::QUANT_ROOT_MIDI;
    int oct = rel / 12, pc = rel % 12;
    if (pc < 0) { pc += 12; oct -= 1; }
    int best = armitage_k::QUANT_SCALE[0], bestd = 99;
    for (int i = 0; i < armitage_k::QUANT_SCALE_LEN; i++) {
      int d = pc - armitage_k::QUANT_SCALE[i]; if (d < 0) d = -d;
      if (d < bestd) { bestd = d; best = armitage_k::QUANT_SCALE[i]; }
    }
    return (float)((int)armitage_k::QUANT_ROOT_MIDI + oct * 12 + best);
  }

  static inline float Exp2f(float x) { return powf(2.f, x); }
  static inline float Mapf(float in, float a, float b) { return a + in * (b - a); }
  static inline float ExpMap(float knob, float lo, float hi) { return lo * powf(hi / lo, knob); }
  inline float OnePoleCoeff(float ms) const { return 1.f - expf(-1.f / (ms * 0.001f * sr_)); }
  static inline void Smooth(float& s, float target) { s += armitage_k::PARAM_SMOOTH * (target - s); }

  inline float DcBlock(float x) {
    const float y = x - dc_x1_ + armitage_k::DC_BLOCK_R * dc_y1_;
    dc_x1_ = x; dc_y1_ = y;
    return y;
  }
  inline float Limit(float x) {
    const float ax = fabsf(x);
    const float c  = (ax > lim_env_) ? lim_atk_c_ : lim_rel_c_;
    lim_env_ += c * (ax - lim_env_);
    float gr = 1.f;
    if (lim_env_ > armitage_k::LIMIT_THR) {
      const float over = lim_env_ - armitage_k::LIMIT_THR;
      gr = armitage_k::LIMIT_THR / (armitage_k::LIMIT_THR + over * armitage_k::LIMIT_RATIO_INV);
    }
    return x * gr;
  }

  // ---------------------------------------------------------------- state
  float sr_ = CT3_SAMPLE_RATE_HZ;
  Behavior behavior_ = kFixed, prev_behavior_ = kFixed;

  PitchTracker tracker_;

  // Onset chord detector + snapshot scheduling (audio-thread owned).
  ChordDetector chord_;
  bool  snap_pending_   = false;   // a snapshot is scheduled
  int   snap_timer_     = 0;       // samples until the scheduled snapshot
  int   snap_delay_samps_ = 0;     // CHORD_SNAP_DELAY_MS in samples
  int   measure_timer_ = 0;        // countdown: onset → StartMeasure (drops the attack)
  int   attack_blank_samps_ = 0;   // CHORD_ATTACK_BLANK_MS in samples
  // Peak/onset detector (fires the snapshot on each new attack, independent of the gate).
  float onset_ref_ = 0.f;          // slow baseline the attack must spike above
  float onset_ref_c_ = 0.f;        // baseline follower coeff (ONSET_REF_MS)
  float onset_fast_ = 0.f;         // attack env = smoothed |high-passed input|
  float onset_fast_c_ = 0.f;       // envelope coeff (ONSET_FAST_MS)
  float onset_hp_lp_ = 0.f;        // one-pole LP state for the onset high-pass (hp = x − lp)
  float onset_hp_c_ = 0.f;         // HPF coeff (ONSET_HP_HZ)
  int   onset_refractory_ = 0;     // samples remaining before another onset may fire
  int   onset_refractory_samps_ = 0;
  // Excitation-feed envelope: duck on onset, attack in once the chord locks.
  float exc_env_ = 1.f, exc_target_ = 1.f, exc_coeff_ = 0.f;
  float exc_atk_c_ = 0.f, exc_duck_c_ = 0.f;

  // Active note set (filled per behaviour) + level normalisation.
  float note_midi_[armitage_k::MAX_VOICES] = {};   // TARGET midi per voice (post voice-leading)
  float midi_cur_[armitage_k::MAX_VOICES]  = {};   // current glided midi (portamento)
  float snap_notes_[armitage_k::MAX_VOICES] = {};  // raw detected notes before voice-leading
  int   active_voices_ = 1;

  // Debug snapshot handed to the main-loop serial logger (audio thread writes, main reads).
  float dbg_notes_[armitage_k::MAX_VOICES] = {};
  int   dbg_n_ = 0;
  volatile bool dbg_new_ = false;
  float voice_norm_    = 1.f;      // TARGET 1/√n level normalization (set at snapshot)
  float voice_norm_cur_ = 1.f;     // smoothed (ramped) normalization — de-clicks count changes
  float vnorm_smooth_c_ = 0.f;     // per-sample ramp coeff (VNORM_SMOOTH_MS)
  float vgain_[armitage_k::MAX_VOICES] = {};   // per-voice fade gain (declick add/drop)
  int   sum_upto_ = 0;             // slots summed = active_voices_ + the fading-out tail
  float fade_step_ = 1.f;          // per-sample linear fade step (FADE_MS)

  // Key-quant note stack (FIFO of distinct in-key notes).
  float quant_notes_[armitage_k::QUANT_MAX_VOICES] = {};
  int   quant_count_ = 0;
  float last_quant_  = -999.f;

  // Smoothed control params.
  float asym_ = 0.f, structure_ = 0.f, register_oct_ = 0.f;
  float post_drive_ = 1.f;               // post-loop drive — LOCKED at former K4-noon value
  float post_gain_ = 1.f;                // drive-dependent output makeup (1/√drive)
  float glide_time_ms_ = 100.f;          // K4 portamento time (ms); linear fixed-time glide
  float glide_step_[armitage_k::MAX_VOICES] = {};  // per-voice semitones/update (set on retarget)
  float post_bias_tanh_ = 0.f;           // tanhf(POST_DRIVE_BIAS) — static DC to subtract
  float t60_ = 1.f;
  VoiceParams vp_;                        // per-voice FM / in-loop-drive params (K3 + SW1)
  float delay_smooth_c_ = 1.f;            // audio-rate read-delay smoothing coeff (DELAY_SMOOTH_MS)

  float dc_x1_ = 0.f, dc_y1_ = 0.f;      // conditioning DC blocker

  // Onset envelope (fast 4-pole follower) — shared by tracker/quant/K5 gates.
  EnvFollower onset_env_;
  float env_val_ = 0.f;                  // latest onset-env value (read in Controls too)
  float gate_env_ = 0.f, gate_rel_c_ = 0.f;  // slow-release peak follower (gate hold)

  // K5: gated AR filter envelope (own generator) + 4-pole non-resonant LP.
  bool  fenv_gate_ = false;              // sustain gate: input above ONSET_OFF
  bool  fenv_armed_ = true;              // retrigger arm (hysteresis crossing)
  float fenv_ = 0.f, fenv_atk_c_ = 0.1f, fenv_rel_c_ = 0.01f;
  float lp1_ = 0.f, lp2_ = 0.f, lp3_ = 0.f, lp4_ = 0.f;
  float g_closed_ = 0.f, filt_octaves_ = 1.f;

  float lim_env_ = 0.f, lim_atk_c_ = 0.f, lim_rel_c_ = 0.f;

  CombVoice combs_[armitage_k::MAX_VOICES];
};
