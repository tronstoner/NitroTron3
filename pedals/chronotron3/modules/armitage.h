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

    // SW1 is free now (modal core dropped) — left unread until reassigned.
    const int sw2 = cs.Switch(1);   // behaviour: UP=fixed, MID=mono, DOWN=quant
    behavior_ = (sw2 == 0) ? kFixed : (sw2 == 1) ? kMono : kQuant;

    Smooth(asym_,      Mapf(k4, armitage_k::ASYM_MIN, armitage_k::ASYM_MAX));
    Smooth(structure_, k3);
    Smooth(register_oct_, (k1 * 2.f - 1.f) * armitage_k::REGISTER_OCT);
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

    // Run YIN only for the tracked behaviours (heavy); fixed bank skips it.
    if (behavior_ != kFixed) tracker_.Update();

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

      // --- Conditioning: asymmetric saturation (F3) + DC block. ---
      const float t = tanhf(armitage_k::DRIVE * x);
      float e = (1.f - asym_) * t + asym_ * fabsf(t);
      e = DcBlock(e) * armitage_k::EXCITE_GAIN;

      // --- Comb resonator bank (active_voices_ tunings). ---
      float y = 0.f;
      for (int v = 0; v < active_voices_; v++) y += combs_[v].Process(e, ap_coeff_);
      y *= armitage_k::COMB_MAKEUP * voice_norm_;

      // --- K5 gated-AR 4-pole LP (Moog-style, non-resonant). ---
      // Input gate = note on/off (hysteresis). Note-on → attack toward open
      // (retriggers each note); sustains at open while the signal is present;
      // note-off → release toward closed. Closed → muted; the release IS the
      // perceived decay, decoupled from resonator damping (K2).
      // Onset detect: proven hysteresis crossing on the fast env (as the
      // nitrotron3 attack-sync). A fresh onset restarts the sweep (retrigger);
      // the gate holds open through the natural ring-out and releases only when
      // the input falls to near-silence (ONSET_OFF is low for that).
      if (fenv_armed_ && env_val_ > armitage_k::ONSET_ON) {
        fenv_ = 0.f;             // retrigger: restart the attack sweep
        fenv_armed_ = false;
      } else if (!fenv_armed_ && env_val_ < armitage_k::ONSET_OFF) {
        fenv_armed_ = true;
      }
      // Gate hold: peak follower (instant attack, slow release) keeps the gate
      // open through the note's decay so it isn't cut too soon (guitar).
      if (env_val_ > gate_env_) gate_env_ = env_val_;
      else gate_env_ += gate_rel_c_ * (0.f - gate_env_);
      fenv_gate_ = (gate_env_ > armitage_k::ONSET_OFF);
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

 private:
  enum Behavior { kFixed = 0, kMono = 1, kQuant = 2 };

  // ---------------------------------------------------------------- Comb voice
  struct CombVoice {
    float* buf = nullptr;
    int    wp  = 0;
    float  g     = 0.f;
    float  dfrac = 1.f;
    float  ap_x[armitage_k::DISP_STAGES] = {};
    float  ap_y[armitage_k::DISP_STAGES] = {};

    void Init(float* b) {
      buf = b;
      for (int i = 0; i < armitage_k::COMB_MAX_SAMPLES; i++) buf[i] = 0.f;
      wp = 0;
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) { ap_x[s] = 0.f; ap_y[s] = 0.f; }
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
    inline float Process(float x, float ap_a) {
      float lp = 0.5f * (Read(dfrac) + Read(dfrac + 1.f));
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) {
        const float in = lp;
        const float o  = ap_a * in + ap_x[s] - ap_a * ap_y[s];
        ap_x[s] = in; ap_y[s] = o;
        lp = o;
      }
      const float y = x + g * lp;
      buf[wp] = y;
      wp++; if (wp >= armitage_k::COMB_MAX_SAMPLES) wp = 0;
      return y;
    }
  };

  // -------------------------------------------------- note-set per behaviour
  void BuildNotes() {
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

  // ------------------------------------------------------ coefficient recompute
  void RecomputeVoices() {
    ap_coeff_ = structure_ * armitage_k::DISP_MAX;

    for (int v = 0; v < active_voices_; v++) {
      float f = MidiToFreq(note_midi_[v]) * Exp2f(register_oct_);
      if (f < 1.f) f = 1.f;

      // Comb: Dtot = fs/f, g from F1 T60 relation using the actual Dtot.
      float dtot = sr_ / f;
      const float maxd = (float)(armitage_k::COMB_MAX_SAMPLES - 3);
      if (dtot > maxd) dtot = maxd;
      if (dtot < 2.f)  dtot = 2.f;
      combs_[v].dfrac = dtot - 0.5f;
      float g = powf(10.f, -3.f * dtot / (t60_ * sr_));
      if (g > armitage_k::G_MAX) g = armitage_k::G_MAX;
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

  // Active note set (filled per behaviour) + level normalisation.
  float note_midi_[armitage_k::MAX_VOICES] = {};
  int   active_voices_ = 1;
  float voice_norm_    = 1.f;

  // Key-quant note stack (FIFO of distinct in-key notes).
  float quant_notes_[armitage_k::QUANT_MAX_VOICES] = {};
  int   quant_count_ = 0;
  float last_quant_  = -999.f;

  // Smoothed control params.
  float asym_ = 0.f, structure_ = 0.f, register_oct_ = 0.f;
  float t60_ = 1.f;
  float ap_coeff_ = 0.f;

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
