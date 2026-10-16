#pragma once
//
// armitage — polyphonic feedback-FM voice on a band engine.  SW3 UP on NitroLab.
//
// WHAT IT IS: a polyphonic synth voice WITHOUT pitch tracking. The input is
// split into 80 complex (analytic) bands (a private copy of the POG band
// engine, armitage_bands.h: decimate 48 -> 8 kHz, centres
// f_k = 480 * 2^(0.027 k) - 420 = 60 Hz .. 1.69 kHz, interpolate 8 -> 48 kHz).
// Each band's analytic signal z = (yr, yi) is turned into a DX7-style
// feedback-FM operator riding on the played note (docs/FM_FEEDBACK_VOICE_SPEC.md):
//
//   theta_k = b_k * yprev_k
//   out_k   = yr*cos(theta_k) - yi*sin(theta_k)      (= Re(z e^{j theta}), |z| kept)
//   fb      = SW2 UP: out_k / |z|      SW2 MID/DOWN: out_k * ARM_RAW_GAIN
//   yprev_k = onepole_K5(fb), clamped to [-1, 1], flushed to 0 below ARM_DENORM
//   silence guard: |z|^2 < ARM_SILENCE -> out_k = 0, yprev_k = 0
//
//   b_k = min(ARM_BETA_CLAMP, beta * (ARM_F_REF / f_k)^p)  [* w_k if SW1 UP]
//   w_k = min(1, (f_k / ARM_TILT_REF)^-gamma)
//   wet = interpolate( ARM_OUT_GAIN * sum_k w_k * out_k )
//
// K1 = 0 -> theta = 0 -> out_k = yr: the plain band sum (bit-identical to it).
//
// CONTROL MAP
//   K1  feedback amount     beta = ARM_BETA_MAX * k1^2
//   K2  high-band roll-off  gamma = ARM_TILT_MAX * k2 (bands above ARM_TILT_REF fall)
//   K3  unused (reserved)
//   K4  fold-back           p = 1 - k4 (CCW: high bands gentler; CW: same beta on all)
//   K5  feedback smoothing  one-pole cutoff ARM_SMOOTH_HZ_MAX (CCW) -> _MIN (CW), exp.
//   K6  dry/wet             (shell)
//   SW1 roll-off placement  UP = before the shaping (w_k scales level AND feedback)
//                           MID/DOWN = after (w_k scales level only, plain EQ)
//   SW2 feedback mode       UP = normalised (shape independent of level)
//                           MID/DOWN = raw (shape follows playing level)
//   FS1/FS2 unused. LED1 on, LED2 off.
//
// Control targets are computed per band at control rate (~10 ms) and
// one-pole smoothed per 8 kHz sample (ARM_CTRL_SMOOTH_MS) — no zipper.
//
// Header-only; main.cpp is the sole TU and includes daisy.h/hothouse.h +
// control_surface.h + module.h before this. No dynamic allocation.

#include <cmath>
#include "module.h"
#include "knob_map.h"          // RemapKnob
#include "armitage_bands.h"
#include "armitage_constants.h"

#ifndef DTCM_MEM_SECTION
#define DTCM_MEM_SECTION       // host builds
#endif

// Shared read-only sin table (filled in Init; DTCM = zero-wait-state reads).
static float DTCM_MEM_SECTION armitage_sin_tab[armitage_k::ARM_SIN_SIZE];

class Armitage : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;

  void Init(float sr) override {
    using namespace armitage_k;
    sr8_ = sr / (float)kFactor;
    for (int i = 0; i < ARM_SIN_SIZE; i++)
      armitage_sin_tab[i] = sinf(6.283185307f * (float)i / (float)ARM_SIN_SIZE);
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      ln_fref_[k] = logf(ARM_F_REF / f);
      ln_tilt_[k] = logf(f / ARM_TILT_REF);
      yprev_[k] = 0.f;
      fb_[k] = fb_t_[k] = 0.f;
      w_[k] = w_t_[k] = 1.f;
    }
    ctrl_a_ = 1.f - expf(-1000.f / (ARM_CTRL_SMOOTH_MS * sr8_));
    a5_ = a5_t_ = SmoothCoef(ARM_SMOOTH_HZ_MAX);
    normalised_ = true;
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace armitage_k;
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    const bool  before = (cs.Switch(0) == 0);   // SW1 UP = roll-off before shaping
    normalised_ = (cs.Switch(1) == 0);          // SW2 UP = normalised feedback

    const float beta  = ARM_BETA_MAX * k1 * k1;
    const float p     = 1.f - k4;
    const float gamma = ARM_TILT_MAX * k2;
    for (int k = 0; k < kBands; k++) {
      float bk = beta * expf(p * ln_fref_[k]);           // beta * (F_REF/f_k)^p
      if (bk > ARM_BETA_CLAMP) bk = ARM_BETA_CLAMP;
      float w = expf(-gamma * ln_tilt_[k]);              // (f_k/TILT_REF)^-gamma
      if (w > 1.f) w = 1.f;
      fb_t_[k] = before ? bk * w : bk;
      w_t_[k]  = w;
    }
    a5_t_ = SmoothCoef(ARM_SMOOTH_HZ_MAX * powf(ARM_SMOOTH_HZ_MIN / ARM_SMOOTH_HZ_MAX, k5));

    led1.Set(1.f);
    led2.Set(0.f);
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace armitage_k;
    const bool  norm = normalised_;
    const float ca   = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      const float d = dec_(in + i);
      a5_ += (a5_t_ - a5_) * ca;
      const float a5 = a5_;
      float acc = 0.f;
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        fb_[k] += (fb_t_[k] - fb_[k]) * ca;
        w_[k]  += (w_t_[k]  - w_[k])  * ca;
        const float yr = bands_[k].Re();
        const float yi = bands_[k].Im();
        const float n  = yr * yr + yi * yi;
        if (n < ARM_SILENCE) { yprev_[k] = 0.f; continue; }
        float s, c;
        SinCos(fb_[k] * yprev_[k], s, c);
        const float out = yr * c - yi * s;
        const float fbs = norm ? out * armbands::ArmFastInvSqrt(n) : out * ARM_RAW_GAIN;
        float y = yprev_[k] + (fbs - yprev_[k]) * a5;
        if (y > 1.f) y = 1.f;
        if (y < -1.f) y = -1.f;
        if (fabsf(y) < ARM_DENORM) y = 0.f;
        yprev_[k] = y;
        acc += w_[k] * out;
      }
      // ARM_OUT_GAIN applied before the (linear) interpolator: identical result,
      // 1 multiply instead of 6.
      int_(acc * ARM_OUT_GAIN, wet + i);
    }
  }

 private:
  float SmoothCoef(float hz) const { return 1.f - expf(-6.283185307f * hz / sr8_); }

  // sin/cos of theta (rad) from the shared table: linear interpolation, cos at
  // a quarter-period offset, mask wrap. The +16384 offset (a multiple of the
  // table size) makes the index positive so truncation == floor.
  static inline void SinCos(float theta, float& s, float& c) {
    using namespace armitage_k;
    static_assert((ARM_SIN_SIZE & (ARM_SIN_SIZE - 1)) == 0, "power of two");
    static_assert(ARM_BETA_CLAMP < 90.f, "SinCos offset covers |theta| < ~100 rad");
    constexpr int   kMask = ARM_SIN_SIZE - 1;
    constexpr float kScale = (float)ARM_SIN_SIZE / 6.283185307f;
    const float x  = theta * kScale + 16384.f;
    const int   ix = (int)x;
    const float fr = x - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + ARM_SIN_SIZE / 4) & kMask;
    const float s0 = armitage_sin_tab[i0], s1 = armitage_sin_tab[(i0 + 1) & kMask];
    const float c0 = armitage_sin_tab[q],  c1 = armitage_sin_tab[(q + 1) & kMask];
    s = s0 + fr * (s1 - s0);
    c = c0 + fr * (c1 - c0);
  }

  armbands::ArmBand         bands_[kBands];
  armbands::ArmDecimator    dec_;
  armbands::ArmInterpolator int_;

  float ln_fref_[kBands];   // ln(ARM_F_REF / f_k)
  float ln_tilt_[kBands];   // ln(f_k / ARM_TILT_REF)
  float yprev_[kBands];     // feedback state per band
  float fb_[kBands],  fb_t_[kBands];   // effective feedback beta (smoothed / target)
  float w_[kBands],   w_t_[kBands];    // output weight (smoothed / target)

  float sr8_    = 8000.f;
  float ctrl_a_ = 0.f;                 // control-smoothing coefficient (8 kHz)
  float a5_ = 1.f, a5_t_ = 1.f;        // K5 feedback-smoothing coefficient
  volatile bool normalised_ = true;    // SW2
};
