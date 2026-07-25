#pragma once
//
// armitage — impulse synth / resonator / drone.  SW3 DOWN.
//
// STAGE-1 discovery build (spec Staging table, stage 1):
//   "Conditioning plus both cores at fixed tuning, played into directly. No
//    analysis." So: asymmetric-saturation exciter -> resonator bank (comb OR
//    modal, runtime-switchable via SW1, level-matched, switchable mid-note) ->
//    env-coupled output filter -> limiter -> mono wet out.
//
// No onset/pitch analysis yet: the note set is a FIXED chord (armitage_k::CHORD_*).
// Both cores stay in the binary; one runs at a time (flash/SRAM cost only).
//
// Spec: docs/ChronoTron3/impulse resonator - armitage/IMPULSE_SYNTH_SPEC.md
// Reference math: saturation.py / validate.py.  All tuning in armitage_constants.h.
//
// Header-only; main.cpp is the sole TU and includes daisy.h/daisysp.h/hothouse.h
// + module.h before this. NO dynamic allocation; the large comb buffers live in
// SDRAM at file scope (single TU, safe).

#include <cmath>
#include "module.h"
#include "knob_map.h"          // RemapKnob, MidiToFreq, Mapf
#include "armitage_constants.h"

// ---------------------------------------------------------------------------
// SDRAM comb delay buffers. One per voice per... just per voice: the comb core
// uses these, the modal core does not touch them, so MAX_VOICES buffers suffice
// (only one core runs at a time). ~8 KB each * 6 = ~48 KB SDRAM.
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
    dc_x1_ = dc_y1_ = 0.f;
    out_lp_ = 0.f;
    env_    = 0.f;
    lim_env_ = 0.f;
    // Smoothed control targets — seed to sane mid values.
    asym_ = 0.f; structure_ = 0.f; register_oct_ = 0.f;
    t60_ = 1.0f; env_atk_c_ = 0.5f; env_rel_c_ = 0.5f;
    lim_atk_c_ = 1.f - expf(-1.f / (armitage_k::LIMIT_ATK_MS * 0.001f * sr_));
    lim_rel_c_ = 1.f - expf(-1.f / (armitage_k::LIMIT_REL_MS * 0.001f * sr_));
    RecomputeVoices();
  }

  void Activate()   override { /* keep state; ring naturally decays */ }
  void Deactivate() override {}

  // -------------------------------------------------------------------------
  // Control-rate (~10 ms). Read the surface, map, recompute per-voice coeffs.
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    // K1 register (bipolar), K2 damping, K3 structure, K4 asymmetry, K5 env.
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    // K6 = mix, owned by the shell. SW3 = module select, owned by the shell.

    // SW1: core select. UP(0)=comb, DOWN(2)=modal, MIDDLE(1) unassigned->comb.
    const int sw1 = cs.Switch(0);
    core_ = (sw1 == 2) ? kModal : kComb;

    // Smoothed timbre/character params (zipper-free).
    Smooth(asym_,      Mapf(k4, armitage_k::ASYM_MIN, armitage_k::ASYM_MAX));
    Smooth(structure_, k3);                     // 0..1, meaning per core
    // Register: bipolar +/- REGISTER_OCT octaves, continuous, no stacking (S1).
    Smooth(register_oct_, (k1 * 2.f - 1.f) * armitage_k::REGISTER_OCT);

    // Damping (K2) -> target T60 (log range). Recompute coeffs from it.
    t60_ = ExpMap(k2, armitage_k::T60_MIN_S, armitage_k::T60_MAX_S);

    // K5 bipolar attack/release of the output filter env.
    const float atk_ms = Mapf(k5, armitage_k::ENV_ATK_FAST_MS, armitage_k::ENV_ATK_SLOW_MS);
    const float rel_ms = Mapf(k5, armitage_k::ENV_REL_SLOW_MS, armitage_k::ENV_REL_FAST_MS);
    env_atk_c_ = OnePoleCoeff(atk_ms);
    env_rel_c_ = OnePoleCoeff(rel_ms);

    RecomputeVoices();

    // LED1 = active core (dim=comb, bright=modal). LED2 = output env level.
    led1.Set(core_ == kModal ? 1.f : 0.15f);
    led2.Set(env_ > 1.f ? 1.f : env_);
  }

  // -------------------------------------------------------------------------
  // Audio-rate. Fill `wet` (mono). Shell mixes dry/wet via K6.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // --- Input conditioning: asymmetric saturation (F3) + DC block. ---
      const float t = tanhf(armitage_k::DRIVE * x);
      float e = (1.f - asym_) * t + asym_ * fabsf(t);
      e = DcBlock(e) * armitage_k::EXCITE_GAIN;

      // --- Resonator bank (one core active). ---
      float y = 0.f;
      if (core_ == kComb) {
        for (int v = 0; v < armitage_k::ACTIVE_VOICES; v++)
          y += combs_[v].Process(e, ap_coeff_);
        y *= armitage_k::COMB_MAKEUP;
      } else {
        for (int v = 0; v < armitage_k::ACTIVE_VOICES; v++)
          y += modals_[v].Process(e);
        y *= armitage_k::MODAL_MAKEUP;
      }

      // --- Env-coupled output filter (env from raw input playing dynamics). ---
      const float rin = fabsf(x) * armitage_k::ENV_SENS;
      const float ec  = (rin > env_) ? env_atk_c_ : env_rel_c_;
      env_ += ec * (rin - env_);
      float envn = env_; if (envn > 1.f) envn = 1.f;
      const float cut = armitage_k::OUTFILT_BASE_HZ + envn * armitage_k::OUTFILT_RANGE_HZ;
      float g = 6.2831853f * cut / sr_;          // one-pole LP coeff (approx)
      if (g > 0.99f) g = 0.99f;
      out_lp_ += g * (y - out_lp_);

      // --- Limiter (in-spec). ---
      wet[i] = Limit(out_lp_);
    }
  }

 private:
  enum Core { kComb = 0, kModal = 1 };

  // ---------------------------------------------------------------- Comb voice
  // Feedback comb with a two-point-average loop filter (matches comb_frac in
  // validate.py) plus a first-order-allpass dispersion chain in the loop.
  struct CombVoice {
    float* buf = nullptr;
    int    wp  = 0;
    float  g     = 0.f;   // loop gain (from T60 + actual Dtot)
    float  dfrac = 1.f;   // fractional read position (Dtot - 0.5)
    float  ap_x[armitage_k::DISP_STAGES] = {};
    float  ap_y[armitage_k::DISP_STAGES] = {};

    void Init(float* b) {
      buf = b;
      for (int i = 0; i < armitage_k::COMB_MAX_SAMPLES; i++) buf[i] = 0.f;
      wp = 0;
      for (int s = 0; s < armitage_k::DISP_STAGES; s++) { ap_x[s] = 0.f; ap_y[s] = 0.f; }
    }

    // Read output d samples in the past, linear-interpolated (older = larger d).
    inline float Read(float d) const {
      int   di = (int)d;
      float f  = d - (float)di;
      int   i0 = wp - 1 - di;
      int   i1 = i0 - 1;                         // one further back
      i0 %= armitage_k::COMB_MAX_SAMPLES; if (i0 < 0) i0 += armitage_k::COMB_MAX_SAMPLES;
      i1 %= armitage_k::COMB_MAX_SAMPLES; if (i1 < 0) i1 += armitage_k::COMB_MAX_SAMPLES;
      const float a = buf[i0];
      const float b = buf[i1];
      return a + (b - a) * f;
    }

    inline float Process(float x, float ap_a) {
      // Two-point-average loop filter: mean of taps at dfrac and dfrac+1.
      float lp = 0.5f * (Read(dfrac) + Read(dfrac + 1.f));
      // Dispersion: cascade of first-order allpasses in the loop.
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
  // MODES_PER_VOICE two-pole resonators summed. Excited by the conditioned input.
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

  // ------------------------------------------------------ coefficient recompute
  // Per-voice tuning from the fixed chord + register + T60. Called at ctrl rate.
  void RecomputeVoices() {
    // Comb dispersion coeff from K3 structure.
    ap_coeff_ = structure_ * armitage_k::DISP_MAX;

    for (int v = 0; v < armitage_k::ACTIVE_VOICES; v++) {
      const float midi = armitage_k::CHORD_ROOT_MIDI +
                         armitage_k::CHORD_INTERVALS_SEMI[v];
      float f = MidiToFreq(midi) * Exp2f(register_oct_);   // register octave shift
      if (f < 1.f) f = 1.f;

      // --- Comb voice: Dtot = fs/f, g from F1 T60 relation using actual Dtot. ---
      float dtot = sr_ / f;
      const float maxd = (float)(armitage_k::COMB_MAX_SAMPLES - 3);
      if (dtot > maxd) dtot = maxd;                        // clamp to buffer
      if (dtot < 2.f)  dtot = 2.f;
      combs_[v].dfrac = dtot - 0.5f;                       // read pos
      float g = powf(10.f, -3.f * dtot / (t60_ * sr_));    // g = 10^(-3D/(T60 fs))
      if (g > armitage_k::G_MAX) g = armitage_k::G_MAX;
      combs_[v].g = g;

      // --- Modal voice: MODES_PER_VOICE partials, structure spreads them. ---
      const float spread = structure_ * armitage_k::MODAL_SPREAD_MAX;
      for (int k = 0; k < armitage_k::MODES_PER_VOICE; k++) {
        float ratio = armitage_k::MODAL_BASE_RATIOS[k] *
                      (1.f + spread * (float)k);           // inharmonic stretch
        float fk = f * ratio;
        if (fk > 0.45f * sr_) fk = 0.45f * sr_;            // keep below Nyquist
        // Per-mode T60: higher modes ring shorter (freq-dependent damping).
        float t60k = t60_ / powf(ratio, armitage_k::MODAL_DAMP_EXP);
        float r = powf(10.f, -3.f / (t60k * sr_));         // pole radius
        if (r > armitage_k::R_MAX) r = armitage_k::R_MAX;
        const float theta = 6.2831853f * fk / sr_;
        Modal::Mode& mm = modals_[v].m[k];
        mm.a1 = -2.f * r * cosf(theta);
        mm.a2 = r * r;
        mm.b0 = (1.f - r);                                 // rough gain norm
      }
    }
  }

  // ---------------------------------------------------------------- helpers
  static inline float Exp2f(float x) { return powf(2.f, x); }

  static inline float Mapf(float in, float a, float b) { return a + in * (b - a); }

  static inline float ExpMap(float knob, float lo, float hi) {
    return lo * powf(hi / lo, knob);                       // log/exponential map
  }

  inline float OnePoleCoeff(float ms) const {
    // c = 1 - exp(-1/(t*sr)); one-pole time constant for env follower.
    return 1.f - expf(-1.f / (ms * 0.001f * sr_));
  }

  static inline void Smooth(float& s, float target) {
    s += armitage_k::PARAM_SMOOTH * (target - s);
  }

  inline float DcBlock(float x) {
    // y[n] = x[n] - x[n-1] + R*y[n-1]
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
      gr = armitage_k::LIMIT_THR /
           (armitage_k::LIMIT_THR + over * armitage_k::LIMIT_RATIO_INV);
    }
    return x * gr;
  }

  // ---------------------------------------------------------------- state
  float sr_ = CT3_SAMPLE_RATE_HZ;

  Core  core_ = kComb;

  // Smoothed control params.
  float asym_ = 0.f, structure_ = 0.f, register_oct_ = 0.f;
  float t60_ = 1.f;
  float ap_coeff_ = 0.f;                 // comb dispersion allpass coeff (from K3)

  // Conditioning DC blocker.
  float dc_x1_ = 0.f, dc_y1_ = 0.f;

  // Output env-coupled filter.
  float env_ = 0.f, out_lp_ = 0.f;
  float env_atk_c_ = 0.5f, env_rel_c_ = 0.5f;

  // Limiter.
  float lim_env_ = 0.f;
  float lim_atk_c_ = 0.f, lim_rel_c_ = 0.f;

  // Voices (both cores allocated; one runs at a time).
  CombVoice combs_[armitage_k::MAX_VOICES];
  Modal     modals_[armitage_k::MAX_VOICES];
};
