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
//   yprev_k = onepole_K5,k(out_k * g_k), clamped to [-1, 1], flushed below ARM_DENORM
//   g_k     = (1 - a) / |z_k| + a * ARM_RAW_GAIN        (SW1 blend: a = 0 normalised, 1 raw)
//   m       = Re(sum_{f_k < ARM_ROOT_HZ} z'_k) / sum_{f_k < ARM_ROOT_HZ} |z'_k|   (in [-1, 1], silence -> 0)
//             m_prev = m of the previous 8 kHz sample (one-sample delay, like yprev)
//   silence guard: |z|^2 < ARM_SILENCE -> out_k = 0, yprev_k = 0
//
//   f'_k = r f_k,  r = 2 / 1 / 0.5 (SW2 UP/MID/DOWN)      (transposed band frequency)
//   b_k = min(ARM_BETA_CLAMP, beta * min(1, (ARM_SHAPE_REF / f'_k)^p))
//   w_k = min(1, (f_k / ARM_TILT_REF)^-gamma)           (on the INPUT centre f_k)
//   K5 one-pole cutoff_k = clamp(c * f'_k, ARM_SMOOTH_HZ_LO, ARM_SMOOTH_HZ_HI)
//   wet = interpolate( ARM_OUT_GAIN * g_oct * sum_k w_k * out_k )
//
// K1 = 0, K3 = 0, SW2 MID -> theta = 0 -> out_k = yr: the plain band sum.
//
// OCTAVE SWITCH: two processing slots (each: octave, yprev[], smoothed b_k[],
// smoothed K5 coefficients[], m_prev). Normally only one runs. On an SW2 change the idle slot starts at the
// new octave (yprev = 0, m_prev = 0, b_k and K5 coefficients = target) and the two are crossfaded
// linearly over ARM_XFADE_MS; then the old one stops. An SW2 change during a
// fade is picked up when that fade ends.
//
// CONTROL MAP
//   K1  feedback amount     beta = ARM_BETA_MAX * k1^2
//   K2  roll-off            gamma = ARM_TILT_MAX * k2 (bands above ARM_TILT_REF fall,
//                           ~-6 dB/oct at CW; after the shaping: w_k scales level only)
//   K3  cross-modulation    beta_x = ARM_XMOD_MAX * k3^2 (bass root m modulates all bands)
//   K4  shaping tilt        p = ARM_SHAPE_P_MAX * (1 - k4) (CCW: only bands below
//                           ARM_SHAPE_REF get the full beta, upper bands much less =
//                           warm; CW: p = 0, every band gets beta = nasal, gritty)
//   K5  feedback smoothing  c = ARM_SMOOTH_REL_MAX (CCW, little) -> _MIN (CW, strong),
//                           exponential; cutoff relative to each band's f'_k
//   K6  dry/wet             (shell)
//   SW1 feedback signal     UP = normalised (a = 0), MID = half (0.5), DOWN = raw (1);
//                           a ramps linearly over ARM_BLEND_MS
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
      ln_fref_[k] = logf(ARM_SHAPE_REF / f);
      f_k_[k] = f;
      ln_tilt_[k] = logf(f / ARM_TILT_REF);
      if (f < ARM_ROOT_HZ) n_root_ = k + 1;   // centres rise with k
      w_[k] = w_t_[k] = 1.f;
      for (int o = 0; o < 3; o++) fb_t_[o][k] = 0.f;
    }
    SetSmoothTargets(ARM_SMOOTH_REL_MAX);
    for (int s = 0; s < 2; s++) ResetSlot(slot_[s], kMid);
    act_ = 0;
    fade_n_ = 0;
    xfade_len_ = (int)(ARM_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (xfade_len_ < 1) xfade_len_ = 1;
    ctrl_a_ = 1.f - expf(-1000.f / (ARM_CTRL_SMOOTH_MS * sr8_));
    blend_ = blend_t_ = ARM_BLEND_UP;
    blend_step_ = 1.f / (ARM_BLEND_MS * 0.001f * sr8_);
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
    const int sw1 = cs.Switch(0);
    blend_t_ = (sw1 == 0) ? ARM_BLEND_UP : (sw1 == 2) ? ARM_BLEND_DOWN : ARM_BLEND_MID;

    const float beta  = ARM_BETA_MAX * k1 * k1;
    const float p     = ARM_SHAPE_P_MAX * (1.f - k4);
    const float gamma = ARM_TILT_MAX * k2;
    // (ARM_SHAPE_REF / (r f_k))^p = (ARM_SHAPE_REF / f_k)^p * r^-p
    const float ln2   = 0.693147181f;
    const float rp[3] = { expf(-p * ln2), 1.f, expf(p * ln2) };   // UP r=2, MID, DOWN r=1/2
    for (int k = 0; k < kBands; k++) {
      const float t1 = expf(p * ln_fref_[k]);              // (SHAPE_REF/f_k)^p
      for (int o = 0; o < 3; o++) {
        float t = t1 * rp[o];                              // (SHAPE_REF/f'_k)^p
        if (t > 1.f) t = 1.f;                              // never more than beta
        float bk = beta * t;
        if (bk > ARM_BETA_CLAMP) bk = ARM_BETA_CLAMP;
        fb_t_[o][k] = bk;
      }
      float w = expf(-gamma * ln_tilt_[k]);                // (f_k/TILT_REF)^-gamma
      if (w > 1.f) w = 1.f;
      w_t_[k] = w;
    }
    bx_t_ = ARM_XMOD_MAX * k3 * k3;
    SetSmoothTargets(ARM_SMOOTH_REL_MAX * powf(ARM_SMOOTH_REL_MIN / ARM_SMOOTH_REL_MAX, k5));
    oct_t_ = oct;

    led1.Set(1.f);
    led2.Set(0.f);
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace armitage_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      const float d = dec_(in + i);
      {
        const float db = blend_t_ - blend_;        // SW1 blend: linear ramp
        const float st = blend_step_;
        blend_ += (db > st) ? st : (db < -st) ? -st : db;
      }
      bx_ += (bx_t_ - bx_) * ca;

      // Start a crossfade when SW2 changed and no fade is running.
      const int ot = oct_t_;
      if (fade_n_ == 0 && ot != slot_[act_].oct) {
        Slot& ns = slot_[act_ ^ 1];
        ResetSlot(ns, ot);
        for (int k = 0; k < kBands; k++) ns.fb[k] = fb_t_[ot][k];   // a5 set by ResetSlot
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
    float a5[kBands];        // K5 one-pole coefficient per band (smoothed toward a5_t_[oct])
  };

  void ResetSlot(Slot& s, int oct) {
    s.oct = oct;
    s.m   = 0.f;
    for (int k = 0; k < kBands; k++) {
      s.yprev[k] = s.fb[k] = 0.f;
      s.a5[k] = a5_t_[oct][k];
    }
  }

  // K5 targets: per octave and band, cutoff = c * f'_k (f'_k = r f_k), clamped.
  void SetSmoothTargets(float c) {
    using namespace armitage_k;
    static const float r[3] = { 2.f, 1.f, 0.5f };   // UP / MID / DOWN
    for (int o = 0; o < 3; o++)
      for (int k = 0; k < kBands; k++) {
        float hz = c * r[o] * f_k_[k];
        if (hz < ARM_SMOOTH_HZ_LO) hz = ARM_SMOOTH_HZ_LO;
        if (hz > ARM_SMOOTH_HZ_HI) hz = ARM_SMOOTH_HZ_HI;
        a5_t_[o][k] = SmoothCoef(hz);
      }
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
    const float gn  = 1.f - blend_;              // normalised share (x 1/|z|)
    const float gr  = blend_ * ARM_RAW_GAIN;     // raw share
    const float* __restrict a5t = a5_t_[O];
    float* __restrict sa5 = s.a5;
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
      sa5[k] += (a5t[k] - sa5[k]) * ca;
      const float yr = bands_[k].Re();
      const float yi = bands_[k].Im();
      const float n  = yr * yr + yi * yi;
      if (n < ARM_SILENCE) { syp[k] = 0.f; continue; }
      const float inv = armbands::ArmFastInvSqrt(n);   // 1/|z| = 1/|z'|
      float zr, zi;
      if (O == kMid) {
        zr = yr; zi = yi;
      } else if (O == kUp) {
        bands_[k].ArmUp1(inv, zr, zi);
      } else {
        bands_[k].ArmDown1(inv, zr, zi);
      }
      if (k < nroot) {
        mnum += zr;
        mden += n * inv;                         // |z'| = |z|
      }
      float sn, cs;
      SinCos(sfb[k] * syp[k] + xm, sn, cs);
      const float out = zr * cs - zi * sn;
      float y = syp[k] + (out * (gn * inv + gr) - syp[k]) * sa5[k];
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

  float ln_fref_[kBands];          // ln(ARM_SHAPE_REF / f_k)
  float f_k_[kBands];              // band input centre f_k (Hz)
  float a5_t_[3][kBands];          // K5 one-pole coefficient target per octave (UP / MID / DOWN)
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
  float blend_ = 0.f, blend_t_ = 0.f;  // SW1 feedback blend a (ramped / target)
  float blend_step_ = 0.0125f;         // max change of a per 8 kHz sample
  float bx_ = 0.f, bx_t_ = 0.f;        // K3 cross-mod depth beta_x (smoothed / target)
  volatile int oct_t_ = kMid;          // SW2 target octave
};
