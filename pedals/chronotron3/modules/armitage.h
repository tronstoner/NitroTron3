#pragma once
//
// armitage — impulse synth / resonator / drone.  SW3 DOWN.
//
// Discovery build. Exciter (asymmetric-saturation conditioning, F3) -> resonator
// bank (comb OR modal, SW1) -> env-coupled output filter (K5) -> limiter -> wet.
//
// Note-set BEHAVIOUR is chosen on SW2 (A/B), instead of one fixed chord:
//   SW2 UP   kFixed  — dense semitone bank (~2 oct), the spec's validation bed.
//                      Rings to anything; tests the core/conditioning/drone.
//   SW2 MID  kMono   — one voice tracked to the played pitch (YIN, mono).
//                      Tests note-following for single notes / lines.
//   SW2 DOWN kQuant  — key-quantised multivoice: play an arpeggio, it stacks the
//                      distinct in-key notes into a chord. Poor-man's poly.
// Only true polyphonic *chord detection* (the hard note-set estimator) is
// deferred. SW1 selects the resonator core; both cores stay in the binary.
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
    for (int v = 0; v < armitage_k::MAX_VOICES; v++) modals_[v].Reset();
    tracker_.Init(sr);
    dc_x1_ = dc_y1_ = 0.f;
    out_lp_ = 0.f;
    env_    = 0.f;
    in_env_ = 0.f;
    lim_env_ = 0.f;
    asym_ = 0.f; structure_ = 0.f; register_oct_ = 0.f;
    t60_ = 1.0f; env_atk_c_ = 0.5f; env_rel_c_ = 0.5f;
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

    const int sw1 = cs.Switch(0);   // core: UP(0)=comb, DOWN(2)=modal, MID->comb
    core_ = (sw1 == 2) ? kModal : kComb;

    const int sw2 = cs.Switch(1);   // behaviour: UP=fixed, MID=mono, DOWN=quant
    behavior_ = (sw2 == 0) ? kFixed : (sw2 == 1) ? kMono : kQuant;

    Smooth(asym_,      Mapf(k4, armitage_k::ASYM_MIN, armitage_k::ASYM_MAX));
    Smooth(structure_, k3);
    Smooth(register_oct_, (k1 * 2.f - 1.f) * armitage_k::REGISTER_OCT);
    t60_ = ExpMap(k2, armitage_k::T60_MIN_S, armitage_k::T60_MAX_S);

    const float atk_ms = Mapf(k5, armitage_k::ENV_ATK_FAST_MS, armitage_k::ENV_ATK_SLOW_MS);
    const float rel_ms = Mapf(k5, armitage_k::ENV_REL_SLOW_MS, armitage_k::ENV_REL_FAST_MS);
    env_atk_c_ = OnePoleCoeff(atk_ms);
    env_rel_c_ = OnePoleCoeff(rel_ms);

    // Run YIN only for the tracked behaviours (heavy); fixed bank skips it.
    if (behavior_ != kFixed) tracker_.Update();

    BuildNotes();
    RecomputeVoices();

    // LED1 = core (dim comb / bright modal). LED2 = output env level.
    led1.Set(core_ == kModal ? 1.f : 0.15f);
    led2.Set(env_ > 1.f ? 1.f : env_);
  }

  // -------------------------------------------------------------------------
  // Audio-rate. Fill `wet` (mono). Shell mixes dry/wet via K6.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // Input level follower (for tracker gating + quant note gate).
      in_env_ += 0.002f * (fabsf(x) - in_env_);
      tracker_.Feed(x, in_env_);

      // --- Conditioning: asymmetric saturation (F3) + DC block. ---
      const float t = tanhf(armitage_k::DRIVE * x);
      float e = (1.f - asym_) * t + asym_ * fabsf(t);
      e = DcBlock(e) * armitage_k::EXCITE_GAIN;

      // --- Resonator bank (one core active, active_voices_ tunings). ---
      float y = 0.f;
      if (core_ == kComb) {
        for (int v = 0; v < active_voices_; v++) y += combs_[v].Process(e, ap_coeff_);
        y *= armitage_k::COMB_MAKEUP * voice_norm_;
      } else {
        for (int v = 0; v < active_voices_; v++) y += modals_[v].Process(e);
        y *= armitage_k::MODAL_MAKEUP * voice_norm_;
      }

      // --- Env-coupled output filter (env from raw input dynamics). ---
      const float rin = fabsf(x) * armitage_k::ENV_SENS;
      const float ec  = (rin > env_) ? env_atk_c_ : env_rel_c_;
      env_ += ec * (rin - env_);
      float envn = env_; if (envn > 1.f) envn = 1.f;
      const float cut = armitage_k::OUTFILT_BASE_HZ + envn * armitage_k::OUTFILT_RANGE_HZ;
      float g = 6.2831853f * cut / sr_;
      if (g > 0.99f) g = 0.99f;
      out_lp_ += g * (y - out_lp_);

      wet[i] = Limit(out_lp_);
    }
  }

 private:
  enum Core     { kComb = 0, kModal = 1 };
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

  // --------------------------------------------------------------- Modal voice
  struct Modal {
    struct Mode {
      float b0 = 0.f, a1 = 0.f, a2 = 0.f, y1 = 0.f, y2 = 0.f;
      inline float Process(float x) {
        const float y = b0 * x - a1 * y1 - a2 * y2;
        y2 = y1; y1 = y;
        return y;
      }
    } m[armitage_k::MODES_PER_VOICE];
    void Reset() {
      for (int k = 0; k < armitage_k::MODES_PER_VOICE; k++) { m[k].y1 = 0.f; m[k].y2 = 0.f; }
    }
    inline float Process(float x) {
      float y = 0.f;
      for (int k = 0; k < armitage_k::MODES_PER_VOICE; k++) y += m[k].Process(x);
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
        if (in_env_ > armitage_k::QUANT_GATE_ENV) {
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

      // Modal: MODES_PER_VOICE partials; structure spreads them inharmonically.
      const float spread = structure_ * armitage_k::MODAL_SPREAD_MAX;
      for (int k = 0; k < armitage_k::MODES_PER_VOICE; k++) {
        float ratio = armitage_k::MODAL_BASE_RATIOS[k] * (1.f + spread * (float)k);
        float fk = f * ratio;
        if (fk > 0.45f * sr_) fk = 0.45f * sr_;
        float t60k = t60_ / powf(ratio, armitage_k::MODAL_DAMP_EXP);
        float r = powf(10.f, -3.f / (t60k * sr_));
        if (r > armitage_k::R_MAX) r = armitage_k::R_MAX;
        const float theta = 6.2831853f * fk / sr_;
        Modal::Mode& mm = modals_[v].m[k];
        mm.a1 = -2.f * r * cosf(theta);
        mm.a2 = r * r;
        mm.b0 = (1.f - r);
      }
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
  Core  core_ = kComb;
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
  float env_ = 0.f, out_lp_ = 0.f;       // output env-coupled filter
  float in_env_ = 0.f;                   // input level follower
  float env_atk_c_ = 0.5f, env_rel_c_ = 0.5f;
  float lim_env_ = 0.f, lim_atk_c_ = 0.f, lim_rel_c_ = 0.f;

  CombVoice combs_[armitage_k::MAX_VOICES];
  Modal     modals_[armitage_k::MAX_VOICES];
};
