#pragma once
//
// armitage — polyphonic feedback-FM voice on a band engine.  SW3 UP on NitroLab.
//
// WHAT IT IS: a polyphonic synth voice WITHOUT pitch tracking. The input is
// split into 80 complex (analytic) bands (a private copy of the POG band
// engine, armitage_bands.h: decimate 48 -> 8 kHz, centres
// f_k = 480 * 2^(0.027 k) - 420 = 60 Hz .. 1.69 kHz, interpolate 8 -> 48 kHz).
// Each band's analytic signal z = (yr, yi) is first transposed by SW2 with
// POG's own per-band octave shift (z' = z / Up1 / Down1, complex, |z'| = |z|),
// then turned into a DX7-style feedback-FM operator riding on the (transposed)
// played note (docs/FM_FEEDBACK_VOICE_SPEC.md):
//
//   z'      = SW2 UP: z^2/|z| (POG Up1)   MID: z   DOWN: |z| e^{j phi/2} with sign
//             continuation (POG Down1: half-angle identities + per-band sign flip
//             on every phase crossing of pi)
//   theta_k = b_k * yprev_k + beta_x * m_prev
//   out_k   = Re(z'_k e^{j theta_k})                       (|z'| kept)
//   yprev_k = onepole_K5(out_k * ARM_RAW_GAIN), clamped to [-1, 1], flushed below ARM_DENORM
//   m       = Re(sum_{f_k < ARM_ROOT_HZ} z'_k) / sum_{f_k < ARM_ROOT_HZ} |z'_k|   (in [-1, 1], silence -> 0)
//             m_prev = m of the previous 8 kHz sample (one-sample delay, like yprev)
//   silence guard: |z|^2 < ARM_SILENCE -> out_k = 0, yprev_k = 0
//
//   b_k = min(ARM_BETA_CLAMP, beta * (ARM_F_REF / (r f_k))^p),  r = 2 / 1 / 0.5 (UP/MID/DOWN)
//   w_k = min(1, (f_k / ARM_TILT_REF)^-gamma)           (on the input centre f_k)
//   wet = interpolate( ARM_OUT_GAIN * g_oct * sum_k w_k * out_k )
//
// K1 = 0, K3 = 0, SW2 MID -> theta = 0 -> out_k = yr: the plain band sum.
//
// OCTAVE SWITCH: two processing slots (each: octave, yprev[], smoothed b_k[],
// m_prev). Normally only one runs. On an SW2 change the idle slot starts at the
// new octave (yprev = 0, m_prev = 0, b_k = target) and the two are crossfaded
// linearly over ARM_XFADE_MS; then the old one stops. An SW2 change during a
// fade is picked up when that fade ends.
//
// CONTROL MAP
//   K1  feedback amount     beta = ARM_BETA_MAX * k1^2
//   K2  high-band roll-off  gamma = ARM_TILT_MAX * k2 (bands above ARM_TILT_REF fall;
//                           after the shaping: w_k scales level only)
//   K3  cross-modulation    beta_x = ARM_XMOD_MAX * k3^2 (bass root m modulates all bands)
//   K4  fold-back           p = 1 - k4 (CCW: high bands gentler; CW: same beta on all)
//   K5  feedback smoothing  one-pole cutoff ARM_SMOOTH_HZ_MAX (CCW) -> _MIN (CW), exp.
//   K6  dry/wet             (shell)
//   SW1 unused
//   SW2 octave              UP = octave up, MID = as played, DOWN = octave down
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
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch(1) values

  void Init(float sr) override {
    using namespace armitage_k;
    sr8_ = sr / (float)kFactor;
    for (int i = 0; i < ARM_SIN_SIZE; i++)
      armitage_sin_tab[i] = sinf(6.283185307f * (float)i / (float)ARM_SIN_SIZE);
    n_root_ = 0;
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      ln_fref_[k] = logf(ARM_F_REF / f);
      ln_tilt_[k] = logf(f / ARM_TILT_REF);
      if (f < ARM_ROOT_HZ) n_root_ = k + 1;   // centres rise with k
      w_[k] = w_t_[k] = 1.f;
      for (int o = 0; o < 3; o++) fb_t_[o][k] = 0.f;
    }
    for (int s = 0; s < 2; s++) ResetSlot(slot_[s], kMid);
    act_ = 0;
    fade_n_ = 0;
    xfade_len_ = (int)(ARM_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (xfade_len_ < 1) xfade_len_ = 1;
    ctrl_a_ = 1.f - expf(-1000.f / (ARM_CTRL_SMOOTH_MS * sr8_));
    a5_ = a5_t_ = SmoothCoef(ARM_SMOOTH_HZ_MAX);
    bx_ = bx_t_ = 0.f;
    oct_t_ = kMid;
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace armitage_k;
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    int oct = cs.Switch(1);
    if (oct < kUp || oct > kDown) oct = kMid;

    const float beta  = ARM_BETA_MAX * k1 * k1;
    const float p     = 1.f - k4;
    const float gamma = ARM_TILT_MAX * k2;
    // (ARM_F_REF / (r f_k))^p = (ARM_F_REF / f_k)^p * r^-p
    const float ln2   = 0.693147181f;
    const float rp[3] = { expf(-p * ln2), 1.f, expf(p * ln2) };   // UP r=2, MID, DOWN r=1/2
    for (int k = 0; k < kBands; k++) {
      const float b1 = beta * expf(p * ln_fref_[k]);       // beta * (F_REF/f_k)^p
      for (int o = 0; o < 3; o++) {
        float bk = b1 * rp[o];
        if (bk > ARM_BETA_CLAMP) bk = ARM_BETA_CLAMP;
        fb_t_[o][k] = bk;
      }
      float w = expf(-gamma * ln_tilt_[k]);                // (f_k/TILT_REF)^-gamma
      if (w > 1.f) w = 1.f;
      w_t_[k] = w;
    }
    bx_t_ = ARM_XMOD_MAX * k3 * k3;
    a5_t_ = SmoothCoef(ARM_SMOOTH_HZ_MAX * powf(ARM_SMOOTH_HZ_MIN / ARM_SMOOTH_HZ_MAX, k5));
    oct_t_ = oct;

    led1.Set(1.f);
    led2.Set(0.f);
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace armitage_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      const float d = dec_(in + i);
      a5_ += (a5_t_ - a5_) * ca;
      bx_ += (bx_t_ - bx_) * ca;

      // Start a crossfade when SW2 changed and no fade is running.
      const int ot = oct_t_;
      if (fade_n_ == 0 && ot != slot_[act_].oct) {
        Slot& ns = slot_[act_ ^ 1];
        ResetSlot(ns, ot);
        for (int k = 0; k < kBands; k++) ns.fb[k] = fb_t_[ot][k];
        fade_n_ = xfade_len_;
      }

      Slot& sa = slot_[act_];
      float acc = RunSlot(sa, d, true);          // updates the bands + w_k
      if (fade_n_ > 0) {
        Slot& sn = slot_[act_ ^ 1];
        const float g = 1.f - (float)fade_n_ / (float)(xfade_len_ + 1);   // new-path gain, rises to 1
        acc = acc * (1.f - g) + RunSlot(sn, d, false) * g;
        if (--fade_n_ == 0) act_ ^= 1;           // new path takes over
      }
      // ARM_OUT_GAIN applied before the (linear) interpolator: identical result,
      // 1 multiply instead of 6.
      int_(acc * ARM_OUT_GAIN, wet + i);
    }
  }

 private:
  struct Slot {
    int   oct;
    float m;                 // root modulator of the previous 8 kHz sample
    float yprev[kBands];     // feedback state per band
    float fb[kBands];        // effective feedback beta (smoothed toward fb_t_[oct])
  };

  void ResetSlot(Slot& s, int oct) {
    s.oct = oct;
    s.m   = 0.f;
    for (int k = 0; k < kBands; k++) s.yprev[k] = s.fb[k] = 0.f;
  }

  // One 8 kHz sample of one slot, returns sum_k w_k * out_k * g_oct.
  // update = true: also clocks the band filters and the w_k smoothers (the
  // first slot of the sample does it; the second, during a fade, reads them).
  float RunSlot(Slot& s, float d, bool update) {
    using namespace armitage_k;
    if (update) {
      switch (s.oct) {
        case kUp:   return ARM_GAIN_UP   * Run<kUp,   true>(s, d);
        case kDown: return ARM_GAIN_DOWN * Run<kDown, true>(s, d);
        default:    return                 Run<kMid,  true>(s, d);
      }
    }
    switch (s.oct) {
      case kUp:   return ARM_GAIN_UP   * Run<kUp,   false>(s, d);
      case kDown: return ARM_GAIN_DOWN * Run<kDown, false>(s, d);
      default:    return                 Run<kMid,  false>(s, d);
    }
  }

  template <int O, bool kUpdate>
  float Run(Slot& s, float d) {
    using namespace armitage_k;
    const float ca  = ctrl_a_;
    const float a5  = a5_;
    const float xm  = bx_ * s.m;                 // cross-mod phase, previous sample's m
    const float* __restrict fbt = fb_t_[O];
    float* __restrict sfb = s.fb;
    float* __restrict syp = s.yprev;
    float* __restrict w   = w_;
    const float* __restrict wt = w_t_;
    const int nroot = n_root_;
    float acc = 0.f, mnum = 0.f, mden = 0.f;
    for (int k = 0; k < kBands; k++) {
      if (kUpdate) {
        bands_[k].Update(d);
        w[k] += (wt[k] - w[k]) * ca;
      }
      sfb[k] += (fbt[k] - sfb[k]) * ca;
      const float yr = bands_[k].Re();
      const float yi = bands_[k].Im();
      const float n  = yr * yr + yi * yi;
      if (n < ARM_SILENCE) { syp[k] = 0.f; continue; }
      float zr, zi, inv = 0.f;
      if (O == kMid) {
        zr = yr; zi = yi;
      } else {
        inv = armbands::ArmFastInvSqrt(n);
        if (O == kUp) bands_[k].ArmUp1(inv, zr, zi);
        else          bands_[k].ArmDown1(inv, zr, zi);
      }
      if (k < nroot) {
        if (O == kMid) inv = armbands::ArmFastInvSqrt(n);
        mnum += zr;
        mden += n * inv;                         // |z'| = |z|
      }
      float sn, cs;
      SinCos(sfb[k] * syp[k] + xm, sn, cs);
      const float out = zr * cs - zi * sn;
      float y = syp[k] + (out * ARM_RAW_GAIN - syp[k]) * a5;
      if (y > 1.f) y = 1.f;
      if (y < -1.f) y = -1.f;
      if (fabsf(y) < ARM_DENORM) y = 0.f;
      syp[k] = y;
      acc += w[k] * out;
    }
    s.m = (mden > 0.f) ? mnum / mden : 0.f;
    return acc;
  }

  float SmoothCoef(float hz) const { return 1.f - expf(-6.283185307f * hz / sr8_); }

  // sin/cos of theta (rad) from the shared table: linear interpolation, cos at
  // a quarter-period offset, mask wrap. The +16384 offset (a multiple of the
  // table size) makes the index positive so truncation == floor.
  static inline void SinCos(float theta, float& s, float& c) {
    using namespace armitage_k;
    static_assert((ARM_SIN_SIZE & (ARM_SIN_SIZE - 1)) == 0, "power of two");
    static_assert(ARM_BETA_CLAMP + ARM_XMOD_MAX < 90.f,
                  "SinCos offset covers |theta| < ~100 rad");
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

  float ln_fref_[kBands];          // ln(ARM_F_REF / f_k)
  float ln_tilt_[kBands];          // ln(f_k / ARM_TILT_REF)
  float fb_t_[3][kBands];          // beta_k target per octave (UP / MID / DOWN)
  float w_[kBands], w_t_[kBands];  // output weight (smoothed / target)
  Slot  slot_[2];                  // active + crossfade-in path
  int   act_ = 0;                  // index of the active slot
  int   fade_n_ = 0;               // crossfade samples left (0 = no fade)
  int   xfade_len_ = 80;           // crossfade length (8 kHz samples)
  int   n_root_ = 0;               // bands 0..n_root_-1 form the root modulator

  float sr8_    = 8000.f;
  float ctrl_a_ = 0.f;                 // control-smoothing coefficient (8 kHz)
  float a5_ = 1.f, a5_t_ = 1.f;        // K5 feedback-smoothing coefficient
  float bx_ = 0.f, bx_t_ = 0.f;        // K3 cross-mod depth beta_x (smoothed / target)
  volatile int oct_t_ = kMid;          // SW2 target octave
};
