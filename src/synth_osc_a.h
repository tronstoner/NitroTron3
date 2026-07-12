#pragma once

#include <cmath>
#include "constants.h"
#include "moog_osc.h"

// Mode A bipolar K5 oscillator engine — unison cloud (CCW) + audio-rate FM (CW).
//
// K5 splits at noon:
//
//   K5 = 0.00              : thickest unison cloud — 7 voices, full detune
//   K5 ∈ (0.00, noon−dz)   : voices fade in by pair (innermost → middle →
//                            outermost), then detune widens; collapses toward a
//                            single osc as K5 approaches noon
//   K5 ∈ [noon−dz, noon+dz]: single clean oscillator (dead-zone landing spot)
//   K5 ∈ (noon+dz, 1.00)   : FM depth ramps 0 → max
//   K5 = 1.00              : max FM (±MODE_A_FM_DEPTH_MAX frequency swing)
//
// The cloud runs for whatever waveform SW1 selects (saw / tri / square) — set it
// with SetWaveform(). FM is linear (freq = f0·(1 + depth·mod)) and pitch-stable:
// a zero-mean modulator averages back to f0, so adding FM doesn't sharpen the
// note. Depth is capped below ±100% so the frequency never crosses zero (no
// rectification, no through-zero phase). The modulator is conditioned inside
// Process(): a fundamental-isolation
// LP rounds the bass toward a sine, an AGC divide by the envelope keeps its
// amplitude ~unit across pluck dynamics, and a tanh bounds it to ±1.
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
    for (int v = 0; v < MODE_A_UNISON_VOICES; v++) voices_[v].waveform = wf;
  }

  // f0 Hz, k5 ∈ [0,1]. dry = raw input this sample, env = envelope-follower level
  // (both used only to build the FM modulator). Returns the RMS-normalized osc mix.
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

    // ----- K5 → cloud staging (CCW half) or FM depth (CW half) -----
    float pair_amt[3] = {0.f, 0.f, 0.f};
    float detune      = MODE_A_UNISON_DETUNE_CENTS_MAX;  // only matters when a pair is audible
    float fm_depth    = 0.f;                             // octaves

    const float dz = MODE_A_K5_DEADZONE;
    if (k5 < 0.5f - dz) {
      const float lo = 0.5f - dz;
      const float t  = (lo - k5) / lo;                   // 0 at dead-zone edge, 1 at K5=0
      pair_amt[0] = Sat01(t / MODE_A_UNISON_V3_END);
      pair_amt[1] = Sat01((t - MODE_A_UNISON_V3_END) /
                          (MODE_A_UNISON_V5_END - MODE_A_UNISON_V3_END));
      pair_amt[2] = Sat01((t - MODE_A_UNISON_V5_END) /
                          (MODE_A_UNISON_V7_END - MODE_A_UNISON_V5_END));
      detune = MODE_A_UNISON_DETUNE_CENTS_MIN +
               t * (MODE_A_UNISON_DETUNE_CENTS_MAX - MODE_A_UNISON_DETUNE_CENTS_MIN);
    } else if (k5 > 0.5f + dz) {
      const float hi = 0.5f + dz;
      const float t  = (k5 - hi) / (1.f - hi);           // 0 at dead-zone edge, 1 at K5=1
      fm_depth = t * MODE_A_FM_DEPTH_MAX;
    }

    // ----- Voice sum: center is FM'd (fm_depth=0 in the cloud half), sides are
    // detuned siblings (silent in the FM half, pair_amt=0). -----
    const int   N      = MODE_A_UNISON_VOICES;
    const int   center = N / 2;
    // Linear through-zero FM: pitch-stable (the DC-blocked, zero-mean modulator
    // averages the ±Hz deviations back to f0). At depth > 1 the multiplier goes
    // negative and the oscillator phase runs backward through zero — clangorous
    // but in tune, and no rectification artifacts.
    const float center_frq = f0 * (1.f + fm_depth * mod);

    float sum = 0.f;
    for (int v = 0; v < N; v++) {
      const int   dist = (v < center) ? (center - v) : (v - center);
      const float vf   = (v == center)
                             ? center_frq
                             : f0 * exp2f(MODE_A_UNISON_SPREAD[v] * detune / 1200.f);
      const float s    = voices_[v].Process(vf);
      const float w    = (dist == 0) ? 1.f : pair_amt[dist - 1];
      sum += s * w;
    }
    const float side_energy = 2.f * (pair_amt[0] * pair_amt[0] +
                                     pair_amt[1] * pair_amt[1] +
                                     pair_amt[2] * pair_amt[2]);
    return sum * (1.f / sqrtf(1.f + side_energy));
  }

 private:
  float   sr_       = 48000.f;
  float   lp_coeff_ = 0.f;
  float   dc_coeff_ = 0.f;
  float   fm_lp1_   = 0.f;
  float   fm_lp2_   = 0.f;
  float   mod_dc_   = 0.f;
  MoogOsc voices_[MODE_A_UNISON_VOICES];

  static float Sat01(float x) {
    if (x < 0.f) return 0.f;
    if (x > 1.f) return 1.f;
    return x;
  }
};
