#pragma once

#include <cmath>
#include "constants.h"
#include "moog_osc.h"

// Mode A bipolar K5 oscillator engine.
//
// K5 splits at noon. The CW half is always audio-rate FM; the CCW half depends
// on the SW1 waveform:
//
//   CCW half   saw       : detuned unison "cloud" (Mode C hypersaw staging) —
//                          voices fade in by pair, then detune widens
//              triangle  : same cloud, but FM deepens along the CCW travel too
//                          and is applied to EVERY voice — an FM'd ensemble
//                          (triangle is the cleanest FM carrier)
//              square    : PWM (duty-cycle modulation) — same behaviour as the
//                          Mode C rect voice, reusing the MODE_C_SYNTH_PWM_*
//                          constants (depth ramps in first, then LFO rate)
//   noon ± dz            : single clean oscillator (dead-zone landing spot)
//   CW half              : FM depth ramps 0 → max on the single center osc
//
// FM is linear through-zero (freq = f0·(1 + depth·mod)) and pitch-stable: the
// modulator is DC-blocked to zero mean, so the ±Hz deviations average back to
// f0 and adding FM doesn't sharpen the note. At depth > 1 the multiplier goes
// negative and the oscillator phase runs backward through zero (clangorous but
// in tune). The modulator is conditioned inside Process(): fundamental-isolation
// LP → partial normalization (intensity tracks playing level) → tanh soft-clip
// → DC block.
//
// Pitch base (f0) + VCA + ladder + mix happen at the call site (Mode A style).
class DroneOsc {
 public:
  void Init(float sr) {
    sr_ = sr;
    lp_coeff_ = 1.f - expf(-6.2831853f * MODE_A_FM_LP_HZ / sr_);
    dc_coeff_ = 1.f - expf(-6.2831853f * MODE_A_FM_DC_HZ / sr_);
    fm_lp1_ = fm_lp2_ = 0.f;
    mod_dc_ = 0.f;
    lfo_phase_ = 0.f;
    const int N = MODE_A_UNISON_VOICES;
    for (int v = 0; v < N; v++) {
      voices_[v].Init(sr);
      // Irrational stagger so voices boot decorrelated (no slow flange from an
      // in-phase start) — same trick as the Mode C hypersaw.
      const float seed = (static_cast<float>(v) + 1.f) * 0.41421356f;
      voices_[v].SetPhase(seed - floorf(seed));
    }
  }

  void SetWaveform(MoogOsc::Waveform wf) {
    wf_ = wf;
    for (int v = 0; v < MODE_A_UNISON_VOICES; v++) voices_[v].waveform = wf;
  }

  // f0 Hz, k5 ∈ [0,1]. dry = raw input this sample, env = envelope-follower level
  // (both used only to build the FM modulator).
  float Process(float f0, float k5, float dry, float env) {
    // ----- FM modulator: fundamental LP → partial-norm → tanh → DC block -----
    // Partial normalization (divisor = FLOOR + NORM·env) lets FM intensity grow
    // with playing level instead of being flattened by full AGC. The DC block
    // forces the modulator zero-mean so linear FM keeps the pitch stable.
    fm_lp1_ += lp_coeff_ * (dry - fm_lp1_);
    fm_lp2_ += lp_coeff_ * (fm_lp1_ - fm_lp2_);
    const float divisor = MODE_A_FM_FLOOR + MODE_A_FM_NORM * env;
    const float raw     = tanhf(MODE_A_FM_DRIVE * fm_lp2_ / divisor);
    mod_dc_ += dc_coeff_ * (raw - mod_dc_);
    const float mod = raw - mod_dc_;

    // ----- K5 → CCW amount (cloud / PWM) or FM depth (CW half) -----
    // Triangle (the cleanest FM carrier) also gets FM on its CCW side, applied
    // to the whole cloud: going CCW thickens the ensemble AND deepens FM at once.
    const bool  fm_all = (wf_ == MoogOsc::TRI);
    float ccw_t    = 0.f;   // 0 at dead-zone edge → 1 at full CCW
    float fm_depth = 0.f;
    const float dz = MODE_A_K5_DEADZONE;
    if (k5 < 0.5f - dz) {
      const float lo = 0.5f - dz;
      ccw_t = (lo - k5) / lo;
      if (fm_all) fm_depth = ccw_t * MODE_A_FM_DEPTH_MAX;
    } else if (k5 > 0.5f + dz) {
      const float hi = 0.5f + dz;
      fm_depth = ((k5 - hi) / (1.f - hi)) * MODE_A_FM_DEPTH_MAX;
    }

    const int   N      = MODE_A_UNISON_VOICES;
    const int   center = N / 2;
    // Linear through-zero FM. fm_mult applies to the center voice always, and to
    // every cloud voice when fm_all (triangle). Pitch-stable: the modulator is
    // zero-mean, so ±Hz swings average back to each voice's base frequency. At
    // depth > 1 the multiplier goes negative and the phase runs backward.
    const float fm_mult    = 1.f + fm_depth * mod;
    const float center_frq = f0 * fm_mult;

    if (wf_ == MoogOsc::SQUARE) {
      // ----- Square: PWM on the CCW half (Mode C rect behaviour) -----
      // Advance the center phase (its 50% square output is unused here); build a
      // variable-duty pulse from that phase. In the FM half ccw_t = 0 → duty =
      // 0.5 (clean square) while center_frq still carries the FM.
      voices_[center].Process(center_frq);
      float depth_amt = ccw_t / MODE_C_SYNTH_PWM_DEPTH_FRAC;
      if (depth_amt > 1.f) depth_amt = 1.f;
      float rate_amt = (ccw_t - MODE_C_SYNTH_PWM_DEPTH_FRAC) /
                       (1.f - MODE_C_SYNTH_PWM_DEPTH_FRAC);
      if (rate_amt < 0.f) rate_amt = 0.f;
      const float lfo_hz = MODE_C_SYNTH_PWM_LFO_HZ_MIN +
          rate_amt * (MODE_C_SYNTH_PWM_LFO_HZ_MAX - MODE_C_SYNTH_PWM_LFO_HZ_MIN);
      lfo_phase_ += lfo_hz / sr_;
      if (lfo_phase_ >= 1.f) lfo_phase_ -= 1.f;
      float tri;
      if      (lfo_phase_ < 0.25f) tri = lfo_phase_ * 4.f;
      else if (lfo_phase_ < 0.75f) tri = 2.f - lfo_phase_ * 4.f;
      else                         tri = lfo_phase_ * 4.f - 4.f;
      const float duty  = 0.5f + depth_amt * MODE_C_SYNTH_PWM_DEPTH_MAX * tri;
      const float phase = voices_[center].GetPhase();
      const float inc   = center_frq / sr_;
      return PulsePwm(phase, inc, duty) * OSC_SQR_GAIN;
    }

    // ----- Saw / Tri: detuned unison cloud (+ FM on the center) -----
    float pair_amt[3] = {0.f, 0.f, 0.f};
    float detune      = MODE_A_UNISON_DETUNE_CENTS_MAX;  // only matters when a pair is audible
    if (ccw_t > 0.f) {
      pair_amt[0] = Sat01(ccw_t / MODE_A_UNISON_V3_END);
      pair_amt[1] = Sat01((ccw_t - MODE_A_UNISON_V3_END) /
                          (MODE_A_UNISON_V5_END - MODE_A_UNISON_V3_END));
      pair_amt[2] = Sat01((ccw_t - MODE_A_UNISON_V5_END) /
                          (MODE_A_UNISON_V7_END - MODE_A_UNISON_V5_END));
      detune = MODE_A_UNISON_DETUNE_CENTS_MIN +
               ccw_t * (MODE_A_UNISON_DETUNE_CENTS_MAX - MODE_A_UNISON_DETUNE_CENTS_MIN);
    }

    float sum = 0.f;
    for (int v = 0; v < N; v++) {
      const int dist = (v < center) ? (center - v) : (v - center);
      float vf;
      if (v == center) {
        vf = center_frq;
      } else {
        vf = f0 * exp2f(MODE_A_UNISON_SPREAD[v] * detune / 1200.f);
        if (fm_all) vf *= fm_mult;   // FM the whole triangle ensemble
      }
      const float s = voices_[v].Process(vf);
      const float w = (dist == 0) ? 1.f : pair_amt[dist - 1];
      sum += s * w;
    }
    const float side_energy = 2.f * (pair_amt[0] * pair_amt[0] +
                                     pair_amt[1] * pair_amt[1] +
                                     pair_amt[2] * pair_amt[2]);
    return sum * (1.f / sqrtf(1.f + side_energy));
  }

 private:
  float             sr_       = 48000.f;
  float             lp_coeff_ = 0.f;
  float             dc_coeff_ = 0.f;
  float             fm_lp1_   = 0.f;
  float             fm_lp2_   = 0.f;
  float             mod_dc_   = 0.f;
  float             lfo_phase_ = 0.f;
  MoogOsc::Waveform wf_       = MoogOsc::SAW;
  MoogOsc           voices_[MODE_A_UNISON_VOICES];

  static float Sat01(float x) {
    if (x < 0.f) return 0.f;
    if (x > 1.f) return 1.f;
    return x;
  }

  // Variable-duty pulse with PolyBLEP-corrected edges, matching the Mode C rect
  // voice: rising edge at phase 0, falling edge at phase = duty; DC-compensated.
  static float PulsePwm(float phase, float inc, float duty) {
    float p = phase < duty ? 1.f : -1.f;
    p += PolyBlep(phase, inc);
    float shifted = phase + (1.f - duty);
    if (shifted >= 1.f) shifted -= 1.f;
    p -= PolyBlep(shifted, inc);
    p -= (2.f * duty - 1.f);
    return p;
  }

  static float PolyBlep(float t, float dt) {
    dt = fabsf(dt);  // through-zero FM can pass a negative increment
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.f;
    } else if (t > 1.f - dt) {
      t = (t - 1.f) / dt;
      return t * t + t + t + 1.f;
    }
    return 0.f;
  }
};
