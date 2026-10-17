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
// then reshaped by one of three SHAPING MODELS (SW1) riding on the (transposed)
// played note (FB = DX7-style feedback FM, docs/FM_FEEDBACK_VOICE_SPEC.md):
//
//   z'      = SW2 UP: z^2/|z| (POG Up1)   MID: z   DOWN: |z| e^{j phi/2} with sign
//             continuation (POG Down1: half-angle identities + per-band sign flip
//             on every phase crossing of pi)
//   L_ref   = max_k |z'_k| of the slot, instant attack, one-pole release ARM_REL_RELEASE_MS,
//             floor ARM_REL_FLOOR; the value of the PREVIOUS 8 kHz sample is used (one-sample lag)
//   s_k     = min(1, |z'_k| / L_ref)                    (relative share: loudest band 1)
//   tilt_k  = min(1, (ARM_SHAPE_REF / f'_k)^p)          (K4, all models)
//   xm      = beta_x * m_prev                           (K3 cross-mod phase, all models)
//
//   SW1 UP = FB (feedback FM; the approved relative mode, unchanged):
//     theta_k = b_k * yprev_k + xm
//     out_k   = Re(z'_k e^{j theta_k})                     (|z'| kept)
//     yprev_k = onepole_K5,k(out_k / L_ref), clamped to [-1, 1], flushed below ARM_DENORM
//     b_k     = min(ARM_BETA_CLAMP, beta * tilt_k), then min(b_k, max(0, ARM_FOLD_LIMIT_HZ / f'_k - 1))
//               (anti-fold, Carson; ARM_ANTIFOLD; the K3 term xm is NOT capped)
//   K1 is split at ARM_K1_SPLIT (0.6) for ADD and PD (FB uses K1 over its full
//   travel, unchanged): u = min(K1/ARM_K1_SPLIT, 1), v = max((K1-ARM_K1_SPLIT)
//   / (1-ARM_K1_SPLIT), 0). u covers today's old law over the lower 60% of the
//   travel; v extends it over the top 40%.
//
//   SW1 MID = ADD (additive saw, harmonics by repeated complex multiplication, no atan2):
//     u'      = (z'_k / |z'_k|) e^{j xm}                (cross-mod rotation BEFORE the harmonics)
//     out_k   = |z'_k| * [ Re(u') + amt_k * sum_{m=2..top} g_m h_mk Re(u'^m) / m^s ]
//     amt_k   = s_k * tilt_k
//     g_m     = clamp(M_eff - m + 1, 0, 1)
//     M_eff   = 1 + u (ARM_ADD_MID - 1) + v (ARM_ADD_MMAX - ARM_ADD_MID)   (1 -> 8 -> 16;
//               K1 = 0: sine only; the top harmonic fades in continuously)
//     top     = ARM_ADD_MMAX
//     s      = 1 - (1 - ARM_ADD_S_MIN) v                                 (1 -> 1 -> 0.6;
//               only the upper 40% of K1 brightens the per-harmonic weight)
//     h_mk    = clamp((ARM_ADD_FMAX - m f'_k) / ARM_ADD_FEDGE, 0, 1)   (anti-fold: harmonics at
//               m f'_k >= 3800 Hz dropped, linear fade from 3400 Hz)
//   SW1 DOWN = PD (Casio-CZ phase-distortion saw):
//     phi     = arg(z'_k e^{j xm}) / 2 pi in [0, 1)     (polynomial atan2, max error ~0.005 rad)
//     d_k     = max(0.5 - (ARM_PD_DEPTH u + ARM_PD_DEPTH_HI v) * amt_k, dfloor_k)
//               (0.5 = sine, 0.05 = saw-like at u=1, 0.01 at u=1,v=1, amt_k = 1)
//     fold_k  = ARM_PD_FOLD_HZ_LO + (ARM_PD_FOLD_HZ_HI - ARM_PD_FOLD_HZ_LO) v   (500 -> 1000 Hz with v)
//     dfloor_k = floor_min + (0.5 - floor_min) clamp((f'_k - fold_k) / (ARM_PD_FOLD_TOP - fold_k), 0, 1),
//               floor_min = (0.5 - ARM_PD_DEPTH) - ARM_PD_DEPTH_HI v        (0.05 -> 0.01 with v)
//               (anti-fold: full depth up to fold_k, linearly back to sine at 3.8 kHz;
//               recomputed every 8 kHz sample from v, since fold_k/floor_min move with K1)
//     phi_w   = phi / (2 d_k) for phi < d_k, else 0.5 + (phi - d_k) / (2 (1 - d_k))
//               (d_k >= 0.01 always: finite, no overflow)
//     out_k   = |z'_k| cos(2 pi phi_w)                  (sin table)
//
//   m       = Re(sum_{f_k < ARM_ROOT_HZ} z'_k) / max(sum_{f_k < ARM_ROOT_HZ} |z'_k|, ARM_XMOD_DEN_FLOOR * L_ref)
//             (in [-1, 1], silence -> 0; denominator floor stops near-silent root leakage flickering m)
//             m_prev = m of the previous 8 kHz sample (one-sample delay, like yprev)
//   silence guard: |z|^2 < ARM_SILENCE -> out_k = 0, yprev_k = 0
//
//   f'_k = r f_k,  r = 2 / 1 / 0.5 (SW2 UP/MID/DOWN)      (transposed band frequency)
//   w_k = 1 (K2 roll-off removed 2026-10-17)
//   K5 one-pole cutoff_k = clamp(c * f'_k, ARM_SMOOTH_HZ_LO, ARM_SMOOTH_HZ_HI)   (FB only)
//
//   wet = interpolate( ARM_OUT_GAIN * g_oct * g_model * band sum )      (48 kHz)
//         g_model = ARM_GAIN_FB / ARM_GAIN_ADD / ARM_GAIN_PD
//
//   K2 MICRO PITCH SHIFT DETUNE ("Eventide micro pitch" style, on the final 48 kHz wet, so
//   for every SW1 model and SW2 octave alike; model / octave crossfades are upstream of it):
//     The wet is written every sample into one circular buffer (ARM_MP_BUF, power of two,
//     in D2 SRAM; written even at K2 = 0 so turning K2 up has history). 4 delay-based
//     pitch shifters read it, voice v = -out, -in, +in, +out:
//       r_v   = 2^(s_v D / 1200),  s_v = ARM_MP_SPREAD[v] = -1, -0.468, +0.468, +1
//               (Schism / JP-8000 0.234 : 0.5, scaled so the outer pair = D)
//       W_v   = ARM_MP_WIN_MS[v] = 41 / 47 / 53 / 59 ms (different, so the crossfades don't line up)
//       p_v  += (1 - r_v) / W_v per sample, wrapped to [0, 1)
//       tap A delay = p_v W_v, tap B delay = frac(p_v + 0.5) W_v   (each ramps by 1 - r_v per sample)
//       sh_v  = sin^2(pi p_v) A + cos^2(pi p_v) B   (complementary, sum of window gains = 1;
//               a tap is silent exactly where its delay wraps; linear interpolation for the
//               fractional read; window from the shared sin table)
//     Staging (smoothed K2): g_in  = clamp(k2 / ARM_SS_IN_END, 0, 1)
//                            g_out = clamp((k2 - ARM_SS_IN_END) / (ARM_SS_OUT_END - ARM_SS_IN_END), 0, 1)
//                            D = ARM_SS_D_MIN (10 c) for k2 <= ARM_SS_OUT_END, then linear to ARM_SS_D_MAX (35 c)
//     out = (wet + g_in (sh_-in + sh_+in) + g_out (sh_-out + sh_+out)) * (1 + 2 g_in^2 + 2 g_out^2)^-ARM_MP_COMP_EXP
//     Gains / D are taken once per audio block from the smoothed K2 (gains ramped linearly
//     across the block). A pair with gain < ARM_SS_ON over the whole block is not computed
//     (K2 = 0: the wet is returned untouched). The detuned voices lag by ~W_v / 2 (~20-30 ms);
//     the centre stays undelayed.
//
// K1 = 0, K2 = 0, K3 = 0, SW2 MID -> out_k = yr in every model: the plain band sum.
//
// MODEL SWITCH (SW1): the old and the new model's band outputs are both computed
// for ARM_MODEL_XFADE_MS and crossfaded linearly (each with its own g_model);
// then only the new one runs. The entering model starts from its targets: FB
// from yprev = 0 with b_k and the K5 coefficients at target (FB state is not
// computed while ADD / PD run), ADD / PD with tilt_k at target. An SW1 change
// during a fade is picked up when that fade ends. Independent of the SW2 crossfade (both can run).
//
// OCTAVE SWITCH: two processing slots (each: octave, yprev[], smoothed b_k[],
// smoothed K5 coefficients[], smoothed tilt_k[], m_prev, L_ref). Normally only one
// runs. On an SW2 change the idle slot starts at the new octave (yprev = 0,
// m_prev = 0, b_k and K5 coefficients = target, L_ref = the active slot's L_ref,
// since |z'| = |z| in every octave) and the two are crossfaded linearly over
// ARM_XFADE_MS; then the old one stops. An SW2 change during a fade is picked up
// when that fade ends.
//
// CONTROL MAP
//   K1  shape amount        FB: beta = ARM_BETA_MAX * k1^2 (full travel, unchanged).
//                           ADD / PD split at ARM_K1_SPLIT = 0.6 into u (0..0.6 of
//                           travel) and v (0.6..1): ADD: M_eff = 1 + u*7 + v*8
//                           (harmonics 2..8 fade in over u, 9..16 over v; harmonic
//                           weight 1/m^s, s = 1 -> 0.6 over v); PD: knee 0.5 -> 0.05
//                           over u, 0.05 -> 0.01 over v (anti-fold corner 500 -> 1000 Hz
//                           with v)
//   K2  micro pitch detune  4 pitch shifters on the final wet: 0 -> 0.3: inner pair
//                           (+-0.468 D) fades in; 0.3 -> 0.6: outer pair (+-D) fades in,
//                           D = 10 c; 0.6 -> 1: D 10 -> 35 c. All models / octaves.
//   K3  cross-modulation    beta_x = ARM_XMOD_MAX * k3^2 (bass root m modulates all bands)
//   K4  shaping tilt        p = ARM_SHAPE_P_MAX * (1 - k4) (CCW: only bands below
//                           ARM_SHAPE_REF get the full shaping, upper bands much less =
//                           warm; CW: p = 0, every band gets it = nasal, gritty); all models
//   K5  feedback smoothing  c = ARM_SMOOTH_REL_MAX (CCW, little) -> _MIN (CW, strong),
//                           exponential; cutoff relative to each band's f'_k (FB only)
//   K6  dry/wet             (shell)
//   SW1 shaping model       UP = FB (feedback FM), MID = ADD (additive saw),
//                           DOWN = PD (phase-distortion saw); ARM_MODEL_XFADE_MS crossfade
//   SW2 octave              UP = octave up, MID = as played, DOWN = octave down
//   FS1/FS2 unused. LED1 on, LED2 off.
//   DIAG builds only: FS1 tap = "note this setting" (LED1 off ~ARM_DIAG_BLINK_MS,
//   numbered line on the serial log); an `auto` line once the controls have
//   settled after a change. The module only fills a snapshot (Controls(), main
//   loop); pedals/nitrolab/main.cpp prints it.
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

// K2 micro-pitch circular buffer of the 48 kHz wet: ARM_MP_BUF floats = 16 KB in
// D2 SRAM (RAM_D2, cached), placed via libDaisy's `.heap` output section like
// ChronoTron3's vestige ring (the only linker-script route into RAM_D2; NOLOAD,
// so Init() zeroes it). Not DTCM: keeps the stack headroom of the DIAG build.
#if defined(__arm__)
#define ARM_MP_BSS __attribute__((section(".heap")))
#else
#define ARM_MP_BSS                     // host builds: plain static
#endif
static float ARM_MP_BSS armitage_mp_buf[armitage_k::ARM_MP_BUF];

class Armitage : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch(1) values
  enum { kFB = 0, kADD = 1, kPD = 2, kFade = 3 };   // shaping model = cs.Switch(0); kFade = both
  static constexpr int kMM = armitage_k::ARM_ADD_MMAX;

  void Init(float sr) override {
    using namespace armitage_k;
    sr8_ = sr / (float)kFactor;
    for (int i = 0; i < ARM_SIN_SIZE; i++)
      armitage_sin_tab[i] = sinf(6.283185307f * (float)i / (float)ARM_SIN_SIZE);
    for (int m = 2; m <= kMM; m++) ln_m_[m] = logf((float)m);   // ADD harmonic weight: 1/m^s = expf(-s*ln_m_[m])
    n_root_ = 0;
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      ln_fref_[k] = logf(ARM_SHAPE_REF / f);
      f_k_[k] = f;
      if (f < ARM_ROOT_HZ) n_root_ = k + 1;   // centres rise with k
      w_[k] = w_t_[k] = 1.f;                  // K2 roll-off removed: output weight always 1
      static const float r[3] = { 2.f, 1.f, 0.5f };          // UP / MID / DOWN
      for (int o = 0; o < 3; o++) {
        fb_t_[o][k] = 0.f;
        tl_t_[o][k] = 1.f;
        const float fp = r[o] * f;                           // f'_k
        // ADD anti-fold weights h_mk (m = 2..kMM) and the highest harmonic kept.
        add_nh_[o][k] = 1;
        for (int m = 0; m <= kMM; m++) {
          float h = (ARM_ADD_FMAX - (float)m * fp) / ARM_ADD_FEDGE;
          h = (h < 0.f) ? 0.f : (h > 1.f) ? 1.f : h;
          add_fw_[o][k][m] = (m >= 2) ? h : 0.f;
          if (m >= 2 && h > 0.f) add_nh_[o][k] = m;
        }
        // PD knee floor pd_dfl_[o][k]: depends on v (time-varying via K1), so it
        // is recomputed every 8 kHz sample in Process(); just seed it here at v = 0.
        pd_dfl_[o][k] = (0.5f - ARM_PD_DEPTH);
      }
    }
    SetSmoothTargets(ARM_SMOOTH_REL_MAX);
    for (int s = 0; s < 2; s++) ResetSlot(slot_[s], kMid);
    // K2 micro pitch: buffer zeroed (NOLOAD section), window lengths in samples.
    for (int i = 0; i < ARM_MP_BUF; i++) armitage_mp_buf[i] = 0.f;
    mp_wp_ = 0;
    for (int v = 0; v < 4; v++) {
      mp_w_[v]  = ARM_MP_WIN_MS[v] * 0.001f * sr;
      mp_iw_[v] = 1.f / mp_w_[v];
      mp_p_[v]  = 0.25f * (float)v;              // staggered start phases (wrap points apart)
      mp_inc_[v] = 0.f;
    }
    mp_gi_ = mp_go_ = 0.f;
    act_ = 0;
    fade_n_ = 0;
    xfade_len_ = (int)(ARM_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (xfade_len_ < 1) xfade_len_ = 1;
    ctrl_a_ = 1.f - expf(-1000.f / (ARM_CTRL_SMOOTH_MS * sr8_));
    mdl_ = mdl_t_ = mfrom_ = kFB;
    mfade_n_ = 0;
    mfade_len_ = (int)(ARM_MODEL_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (mfade_len_ < 1) mfade_len_ = 1;
    k1s_ = k1_t_ = 0.f;
    k2s_ = k2_t_ = 0.f;
    rel_a_ = 1.f - expf(-1000.f / (ARM_REL_RELEASE_MS * sr8_));
    bx_ = bx_t_ = 0.f;
    oct_t_ = kMid;
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace armitage_k;
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));   // micro pitch detune
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    int oct = cs.Switch(1);
    if (oct < kUp || oct > kDown) oct = kMid;
    int sw1 = cs.Switch(0);
    if (sw1 < kFB || sw1 > kPD) sw1 = kADD;
    mdl_t_ = sw1;                                          // UP FB / MID ADD / DOWN PD
    k1_t_ = k1;
    k2_t_ = k2;

    const float beta  = ARM_BETA_MAX * k1 * k1;
    const float p     = ARM_SHAPE_P_MAX * (1.f - k4);
    // (ARM_SHAPE_REF / (r f_k))^p = (ARM_SHAPE_REF / f_k)^p * r^-p
    const float ln2   = 0.693147181f;
    const float rp[3] = { expf(-p * ln2), 1.f, expf(p * ln2) };   // UP r=2, MID, DOWN r=1/2
    static const float r[3] = { 2.f, 1.f, 0.5f };          // UP / MID / DOWN
    for (int k = 0; k < kBands; k++) {
      const float t1 = expf(p * ln_fref_[k]);              // (SHAPE_REF/f_k)^p
      for (int o = 0; o < 3; o++) {
        float t = t1 * rp[o];                              // (SHAPE_REF/f'_k)^p
        if (t > 1.f) t = 1.f;                              // never more than full shaping
        tl_t_[o][k] = t;                                   // tilt_k (ADD / PD)
        float bk = beta * t;
        if (bk > ARM_BETA_CLAMP) bk = ARM_BETA_CLAMP;
        if (ARM_ANTIFOLD) {                                // Carson: (b_k + 1) f'_k <= limit
          float cap = ARM_FOLD_LIMIT_HZ / (r[o] * f_k_[k]) - 1.f;
          if (cap < 0.f) cap = 0.f;
          if (bk > cap) bk = cap;
        }
        fb_t_[o][k] = bk;
      }
      // w_t_[k] stays 1 (K2 roll-off removed); set once in Init, never touched here.
    }
    bx_t_ = ARM_XMOD_MAX * k3 * k3;
    const float c = ARM_SMOOTH_REL_MAX * powf(ARM_SMOOTH_REL_MIN / ARM_SMOOTH_REL_MAX, k5);
    SetSmoothTargets(c);
    oct_t_ = oct;

    led1.Set(1.f);
    led2.Set(0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      DiagControls(cs, led1, kv, sw1, oct, beta, p, c);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;                 // tap number (#n), 0 = automatic line
    float k[5];              // K1..K5, the module's remapped 0..1 values
    int   sw1, sw2;          // 0 UP, 1 MID, 2 DOWN
    float beta, p, c, xmod;  // derived values the engine runs toward
    float ss_d, ss_in, ss_out;   // K2 micro-pitch target: outer detune D (cents), inner / outer pair gain
    int   oct;               // 0 up, 1 played, 2 down
  };
  void Activate() override {
    if (NL_DIAG) { diag_changed_ = true; diag_t_ = daisy::System::GetNow(); }
  }
  // Takes the next pending line (tap first). Returns false when none.
  bool DiagTake(DiagSnap& out) {
    if (diag_tap_pend_)  { out = diag_tap_;  diag_tap_pend_ = false;  return true; }
    if (diag_auto_pend_) { out = diag_auto_; diag_auto_pend_ = false; return true; }
    return false;
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace armitage_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      const float d = dec_(in + i);
      bx_  += (bx_t_ - bx_) * ca;
      k1s_ += (k1_t_ - k1s_) * ca;
      k2s_ += (k2_t_ - k2s_) * ca;              // K2 micro pitch: staged per block below
      // K1 split at ARM_K1_SPLIT into u (lower 60% of travel, scaled 0..1) and
      // v (upper 40%, scaled 0..1); u alone reproduces the old ADD/PD law.
      const float u = (k1s_ < ARM_K1_SPLIT) ? (k1s_ / ARM_K1_SPLIT) : 1.f;
      const float v = (k1s_ > ARM_K1_SPLIT) ? (k1s_ - ARM_K1_SPLIT) / (1.f - ARM_K1_SPLIT) : 0.f;
      {                                          // ADD harmonic weights g_m / m^s (shared by all bands)
        const float me = 1.f + u * (float)(ARM_ADD_MID - 1) + v * (float)(kMM - ARM_ADD_MID);
        const float s_exp = 1.f - (1.f - ARM_ADD_S_MIN) * v;   // 1 (u-range) -> ARM_ADD_S_MIN at v = 1
        mtop_ = 1;
        for (int m = 2; m <= kMM; m++) {
          float g = me - (float)m + 1.f;
          g = (g < 0.f) ? 0.f : (g > 1.f) ? 1.f : g;
          // 1/m^s = expf(-s * ln m); at s = 1 (v = 0) this is exactly the old 1/m.
          gm_[m] = g * expf(-s_exp * ln_m_[m]);
          if (g > 0.f) mtop_ = m;
        }
      }
      pd_d0_ = ARM_PD_DEPTH * u + ARM_PD_DEPTH_HI * v;   // PD knee depth coefficient before amt_k
      {                                          // PD anti-fold knee floor pd_dfl_[o][k]: depends on
        // v (fold corner 500->1000 Hz, floor 0.05->0.01), so recomputed every 8 kHz sample.
        const float floor_min = (0.5f - ARM_PD_DEPTH) - ARM_PD_DEPTH_HI * v;
        const float fold_hz = ARM_PD_FOLD_HZ_LO + (ARM_PD_FOLD_HZ_HI - ARM_PD_FOLD_HZ_LO) * v;
        const float inv_span = 1.f / (ARM_PD_FOLD_TOP - fold_hz);
        static const float rr[3] = { 2.f, 1.f, 0.5f };    // UP / MID / DOWN
        for (int o = 0; o < 3; o++) {
          for (int k = 0; k < kBands; k++) {
            const float fp = rr[o] * f_k_[k];
            float x = (fp - fold_hz) * inv_span;
            x = (x < 0.f) ? 0.f : (x > 1.f) ? 1.f : x;
            pd_dfl_[o][k] = floor_min + (0.5f - floor_min) * x;
          }
        }
      }

      // Start a model crossfade when SW1 changed and no model fade is running.
      if (mfade_n_ == 0 && mdl_t_ != mdl_) {
        mfrom_ = mdl_;
        mdl_ = mdl_t_;
        // The entering model starts from its targets: FB from yprev = 0 with b_k and
        // K5 coefficients at target; ADD / PD with tilt_k at target.
        for (int s = 0; s < 2; s++) {
          Slot& sl = slot_[s];
          for (int k = 0; k < kBands; k++) {
            if (mdl_ == kFB) {
              sl.yprev[k] = 0.f;
              sl.fb[k] = fb_t_[sl.oct][k];
              sl.a5[k] = a5_t_[sl.oct][k];
            } else {
              sl.tl[k] = tl_t_[sl.oct][k];
            }
          }
        }
        mfade_n_ = mfade_len_;
      }
      const int kern = (mfade_n_ > 0) ? kFade : mdl_;
      mg_ = 1.f - (float)mfade_n_ / (float)(mfade_len_ + 1);   // new-model gain during a fade

      // Start a crossfade when SW2 changed and no fade is running.
      const int ot = oct_t_;
      if (fade_n_ == 0 && ot != slot_[act_].oct) {
        Slot& ns = slot_[act_ ^ 1];
        ResetSlot(ns, ot);
        for (int k = 0; k < kBands; k++) {       // a5 set by ResetSlot
          ns.fb[k] = fb_t_[ot][k];
          ns.tl[k] = tl_t_[ot][k];
        }
        ns.lref = slot_[act_].lref;              // |z'| = |z|: same reference in every octave
        fade_n_ = xfade_len_;
      }

      Slot& sa = slot_[act_];
      float acc = RunSlot(sa, d, true, kern);   // updates the bands + w_k
      if (fade_n_ > 0) {
        Slot& sn = slot_[act_ ^ 1];
        const float g = 1.f - (float)fade_n_ / (float)(xfade_len_ + 1);   // new-path gain, rises to 1
        acc = acc * (1.f - g) + RunSlot(sn, d, false, kern) * g;
        if (--fade_n_ == 0) act_ ^= 1;           // new path takes over
      }
      if (mfade_n_ > 0) --mfade_n_;
      // ARM_OUT_GAIN applied before the (linear) interpolator: identical result,
      // 1 multiply instead of 6.
      int_(acc * ARM_OUT_GAIN, wet + i);
    }
    MicroPitch(wet, size);
  }

 private:
  struct Slot {
    int   oct;
    float m;                 // root modulator of the previous 8 kHz sample
    float lref;              // SW1 relative reference L_ref (loudest band |z'|, smoothed)
    float yprev[kBands];     // feedback state per band
    float fb[kBands];        // effective feedback beta (smoothed toward fb_t_[oct])
    float a5[kBands];        // K5 one-pole coefficient per band (smoothed toward a5_t_[oct])
    float tl[kBands];        // tilt_k per band (smoothed toward tl_t_[oct]; ADD / PD)
  };

  void ResetSlot(Slot& s, int oct) {
    s.oct = oct;
    s.m   = 0.f;
    s.lref = armitage_k::ARM_REL_FLOOR;
    for (int k = 0; k < kBands; k++) {
      s.yprev[k] = s.fb[k] = 0.f;
      s.a5[k] = a5_t_[oct][k];
      s.tl[k] = tl_t_[oct][k];
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

  // One 8 kHz sample of one slot, returns g_oct * g_model * band sum.
  // update = true: also clocks the band filters and the w_k smoothers (the
  // first slot of the sample does it; the second, during a fade, reads them).
  // kern = kFB / kADD / kPD, or kFade (old mfrom_ and new mdl_ crossfaded).
  float RunSlot(Slot& s, float d, bool update, int kern) {
    using namespace armitage_k;
    if (update) {
      switch (s.oct) {
        case kUp:   return ARM_GAIN_UP   * RunK<kUp,   true>(s, d, kern);
        case kDown: return ARM_GAIN_DOWN * RunK<kDown, true>(s, d, kern);
        default:    return                 RunK<kMid,  true>(s, d, kern);
      }
    }
    switch (s.oct) {
      case kUp:   return ARM_GAIN_UP   * RunK<kUp,   false>(s, d, kern);
      case kDown: return ARM_GAIN_DOWN * RunK<kDown, false>(s, d, kern);
      default:    return                 RunK<kMid,  false>(s, d, kern);
    }
  }

  template <int O, bool kUpdate>
  float RunK(Slot& s, float d, int kern) {
    switch (kern) {
      case kFB:  return Run<O, kUpdate, kFB>(s, d);
      case kADD: return Run<O, kUpdate, kADD>(s, d);
      case kPD:  return Run<O, kUpdate, kPD>(s, d);
      default:   return Run<O, kUpdate, kFade>(s, d);
    }
  }

  static float ModelGain(int m) {
    using namespace armitage_k;
    return (m == kFB) ? ARM_GAIN_FB : (m == kADD) ? ARM_GAIN_ADD : ARM_GAIN_PD;
  }

  // Per-sample values shared by all bands of one slot.
  struct Ctx {
    float g;         // FB: 1 / L_ref (relative feedback signal)
    float invl;      // 1 / L_ref (s_k)
    float xm;        // cross-mod phase (rad)
    float xc, xs;    // cos / sin of xm (ADD / PD rotation)
  };

  // One band of model M (kFB / kADD / kPD). zr, zi = z'_k; mag = |z'_k|;
  // inv = 1/|z'_k|. Returns out_k.
  template <int M, int O>
  inline float BandOut(Slot& s, int k, float zr, float zi, float mag, float inv,
                       const Ctx& c) {
    using namespace armitage_k;
    if (M == kFB) {
      float sn, cs;
      SinCos(s.fb[k] * s.yprev[k] + c.xm, sn, cs);
      const float out = zr * cs - zi * sn;
      float y = s.yprev[k] + (out * c.g - s.yprev[k]) * s.a5[k];
      if (y > 1.f) y = 1.f;
      if (y < -1.f) y = -1.f;
      if (fabsf(y) < ARM_DENORM) y = 0.f;
      s.yprev[k] = y;
      return out;
    }
    float sh = mag * c.invl;                     // s_k = min(1, |z'| / L_ref)
    if (sh > 1.f) sh = 1.f;
    const float amt = sh * s.tl[k];              // amt_k = s_k * tilt_k
    if (M == kADD) {
      const float ur = zr * inv, ui = zi * inv;  // u = z' / |z'|
      const float vr = ur * c.xc - ui * c.xs;    // u e^{j xm}
      const float vi = ur * c.xs + ui * c.xc;
      return mag * (vr + amt * AddSum<O>(k, vr, vi));
    }
    // kPD
    const float wr = zr * c.xc - zi * c.xs;      // z' e^{j xm}
    const float wi = zr * c.xs + zi * c.xc;
    float dk = 0.5f - pd_d0_ * amt;
    if (dk < pd_dfl_[O][k]) dk = pd_dfl_[O][k];
    if (dk >= 0.5f) return wr;                   // no distortion: |z'| e^{j phi} = z' e^{j xm}
    const float ph = FastAtan2Cycles(wi, wr);    // [0, 1)
    const float pw = (ph < dk) ? ph * (0.5f / dk)
                               : 0.5f + (ph - dk) * (0.5f / (1.f - dk));
    return mag * Cos01(pw);
  }

  // ADD harmonic sum sum_{m=2..top} g_m h_mk Re(v^m) / m^s for the unit phasor v.
  template <int O>
  inline float AddSum(int k, float vr, float vi) const {
    int top = add_nh_[O][k];
    if (top > mtop_) top = mtop_;
    float sum = 0.f, pr = vr, pi = vi;
    const float* __restrict gm = gm_;
    const float* __restrict fw = add_fw_[O][k];
    for (int m = 2; m <= top; m++) {             // v^m by repeated complex multiplication
      const float t = pr * vr - pi * vi;
      pi = pr * vi + pi * vr;
      pr = t;
      sum += gm[m] * fw[m] * pr;
    }
    return sum;
  }

  template <int O, bool kUpdate, int M>
  float Run(Slot& s, float d) {
    using namespace armitage_k;
    const float ca  = ctrl_a_;
    Ctx c;
    c.invl = 1.f / s.lref;                       // previous sample's L_ref
    c.g    = c.invl;
    c.xm   = bx_ * s.m;                          // cross-mod phase, previous sample's m
    if (M != kFB) SinCos(c.xm, c.xs, c.xc);
    const int mf = mfrom_, mt = mdl_;
    const float* __restrict a5t = a5_t_[O];
    float* __restrict sa5 = s.a5;
    const float* __restrict fbt = fb_t_[O];
    float* __restrict sfb = s.fb;
    const float* __restrict tlt = tl_t_[O];
    float* __restrict stl = s.tl;
    float* __restrict syp = s.yprev;
    float* __restrict w   = w_;
    const float* __restrict wt = w_t_;
    const int nroot = n_root_;
    float acc = 0.f, acc2 = 0.f, mnum = 0.f, mden = 0.f, nmax = 0.f;
    for (int k = 0; k < kBands; k++) {
      if (kUpdate) {
        bands_[k].Update(d);
        w[k] += (wt[k] - w[k]) * ca;
      }
      if (M == kFB || M == kFade) {              // FB state: smoothed only while FB runs
        sfb[k] += (fbt[k] - sfb[k]) * ca;
        sa5[k] += (a5t[k] - sa5[k]) * ca;
      }
      if (M != kFB) stl[k] += (tlt[k] - stl[k]) * ca;   // tilt_k: only while ADD / PD run
      const float yr = bands_[k].Re();
      const float yi = bands_[k].Im();
      const float n  = yr * yr + yi * yi;
      if (n < ARM_SILENCE) {
        syp[k] = 0.f;
        continue;
      }
      if (n > nmax) nmax = n;                    // loudest band |z|^2 (= |z'|^2)
      const float inv = armbands::ArmFastInvSqrt(n);   // 1/|z| = 1/|z'|
      const float mag = n * inv;                 // |z'| = |z|
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
        mden += mag;
      }
      if (M != kFade) {
        acc += w[k] * BandOut<M, O>(s, k, zr, zi, mag, inv, c);
      } else {                                   // model crossfade: old (acc) and new (acc2)
        acc  += w[k] * BandOutRT<O>(mf, s, k, zr, zi, mag, inv, c);
        acc2 += w[k] * BandOutRT<O>(mt, s, k, zr, zi, mag, inv, c);
      }
    }
    {                                            // L_ref: instant attack, one-pole release, floor
      const float lm = sqrtf(nmax);
      float l = s.lref;
      l = (lm > l) ? lm : l + (lm - l) * rel_a_;
      s.lref = (l < ARM_REL_FLOOR) ? ARM_REL_FLOOR : l;
    }
    {                                            // K3 modulator denominator floor as a share of the
                                                  // loudest band: root-band leakage near silence no
                                                  // longer jumps m (clicks); 1.0 = stronger, also
                                                  // tames K3 on notes without root content
      const float den = (mden > ARM_XMOD_DEN_FLOOR * s.lref) ? mden : ARM_XMOD_DEN_FLOOR * s.lref;
      s.m = mnum / den;
    }
    if (M != kFade) return ModelGain(M) * acc;
    return ModelGain(mf) * (1.f - mg_) * acc + ModelGain(mt) * mg_ * acc2;
  }

  // Runtime-dispatched BandOut (model crossfade only).
  template <int O>
  inline float BandOutRT(int m, Slot& s, int k, float zr, float zi, float mag,
                         float inv, const Ctx& c) {
    if (m == kFB)  return BandOut<kFB,  O>(s, k, zr, zi, mag, inv, c);
    if (m == kADD) return BandOut<kADD, O>(s, k, zr, zi, mag, inv, c);
    return BandOut<kPD, O>(s, k, zr, zi, mag, inv, c);
  }

  // K2 micro pitch shift detune on the final 48 kHz wet (in place). The wet is
  // always written to the buffer; a pair is computed only while its gain is on.
  void MicroPitch(float* wet, size_t size) {
    using namespace armitage_k;
    constexpr int kMask = ARM_MP_BUF - 1;
    float dc, gi, go;
    SsStage(k2s_, dc, gi, go);                   // smoothed K2, once per block
    const float gi0 = mp_gi_, go0 = mp_go_;      // previous block's gains (ramp start)
    mp_gi_ = gi;
    mp_go_ = go;
    const bool on_i = (gi >= ARM_SS_ON) || (gi0 >= ARM_SS_ON);
    const bool on_o = (go >= ARM_SS_ON) || (go0 >= ARM_SS_ON);
    float* __restrict buf = armitage_mp_buf;
    int wp = mp_wp_;
    if (!on_i && !on_o) {                        // K2 off: history only, wet untouched
      for (size_t n = 0; n < size; n++) {
        buf[wp] = wet[n];
        wp = (wp + 1) & kMask;
      }
      mp_wp_ = wp;
      return;
    }
    for (int v = 0; v < 4; v++) {                // p_v += (1 - r_v) / W_v per sample
      const float r = exp2f(ARM_MP_SPREAD[v] * dc * (1.f / 1200.f));
      mp_inc_[v] = (1.f - r) * mp_iw_[v];
    }
    const float n0 = powf(1.f + 2.f * gi0 * gi0 + 2.f * go0 * go0, -ARM_MP_COMP_EXP);
    const float n1 = powf(1.f + 2.f * gi * gi + 2.f * go * go, -ARM_MP_COMP_EXP);
    const float inv_sz = 1.f / (float)size;
    const float dgi = (gi - gi0) * inv_sz, dgo = (go - go0) * inv_sz, dn = (n1 - n0) * inv_sz;
    for (size_t n = 0; n < size; n++) {
      const float x = wet[n];
      buf[wp] = x;
      const float t = (float)(n + 1);
      float y = x;
      if (on_i) y += (gi0 + dgi * t) * (Shift(1, wp) + Shift(2, wp));   // -in, +in
      if (on_o) y += (go0 + dgo * t) * (Shift(0, wp) + Shift(3, wp));   // -out, +out
      wet[n] = y * (n0 + dn * t);
      wp = (wp + 1) & kMask;
    }
    mp_wp_ = wp;
  }

  // One output sample of pitch shifter v; wp = index of the sample just written.
  inline float Shift(int v, int wp) {
    float p = mp_p_[v] + mp_inc_[v];
    if (p >= 1.f) p -= 1.f;
    else if (p < 0.f) p += 1.f;
    if (p >= 1.f) p = 0.f;                       // rounding guard (p tiny negative + 1)
    mp_p_[v] = p;
    float q = p + 0.5f;                          // tap B: half a window apart
    if (q >= 1.f) q -= 1.f;
    const float ga = 0.5f - 0.5f * Cos01(p);     // sin^2(pi p): 0 where tap A wraps
    const float w = mp_w_[v];
    const float a = ReadBuf(wp, p * w);
    const float b = ReadBuf(wp, q * w);
    return b + ga * (a - b);                     // ga A + (1 - ga) B
  }

  // Buffer value d samples (fractional, 0 <= d < ARM_MP_BUF - 1) before index wp.
  static inline float ReadBuf(int wp, float d) {
    using namespace armitage_k;
    constexpr int kMask = ARM_MP_BUF - 1;
    const float pos = (float)(wp + ARM_MP_BUF) - d;   // > 0: truncation == floor
    const int   ip  = (int)pos;
    const float fr  = pos - (float)ip;
    const float x0 = armitage_mp_buf[ip & kMask];
    const float x1 = armitage_mp_buf[(ip + 1) & kMask];
    return x0 + fr * (x1 - x0);
  }

  // K2 micro-pitch staging (k2 = 0..1): outer detune D (cents), inner / outer pair gain.
  static void SsStage(float k2, float& dc, float& gi, float& go) {
    using namespace armitage_k;
    gi = k2 * (1.f / ARM_SS_IN_END);
    gi = (gi < 0.f) ? 0.f : (gi > 1.f) ? 1.f : gi;
    go = (k2 - ARM_SS_IN_END) * (1.f / (ARM_SS_OUT_END - ARM_SS_IN_END));
    go = (go < 0.f) ? 0.f : (go > 1.f) ? 1.f : go;
    dc = (k2 <= ARM_SS_OUT_END) ? ARM_SS_D_MIN
       : ARM_SS_D_MIN + (ARM_SS_D_MAX - ARM_SS_D_MIN) * (k2 - ARM_SS_OUT_END) * (1.f / (1.f - ARM_SS_OUT_END));
  }

  void DiagControls(const ControlSurface& cs, daisy::Led& led1, const float* kv,
                    int sw1, int oct, float beta, float p, float c) {
    using namespace armitage_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.sw1 = sw1; s.sw2 = oct; s.oct = oct;
    s.beta = beta; s.p = p; s.c = c; s.xmod = bx_t_;
    SsStage(kv[1], s.ss_d, s.ss_in, s.ss_out);

    // FS1 tap: numbered line + LED1 blink-off.
    if (cs.Foot(0).rising) {
      s.n = ++diag_n_;
      diag_tap_ = s;
      diag_tap_pend_ = true;
      diag_blink_t_ = now;
      diag_blink_ = true;
    }
    if (diag_blink_) {
      if (now - diag_blink_t_ < ARM_DIAG_BLINK_MS) led1.Set(0.f);
      else diag_blink_ = false;
    }

    // Settle detector: any knob > EPS away from the last reference, or a switch
    // change, restarts the timer; one `auto` line once nothing changed for
    // ARM_DIAG_SETTLE_MS.
    bool moved = (sw1 != diag_ref_sw1_) || (oct != diag_ref_sw2_);
    for (int i = 0; i < 5; i++) if (fabsf(kv[i] - diag_ref_k_[i]) > ARM_DIAG_KNOB_EPS) moved = true;
    if (moved) {
      for (int i = 0; i < 5; i++) diag_ref_k_[i] = kv[i];
      diag_ref_sw1_ = sw1; diag_ref_sw2_ = oct;
      diag_changed_ = true;
      diag_t_ = now;
    } else if (diag_changed_ && now - diag_t_ >= ARM_DIAG_SETTLE_MS) {
      diag_changed_ = false;
      s.n = 0;
      diag_auto_ = s;
      diag_auto_pend_ = true;
    }
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

  // cos(2 pi x) for x in [0, 1] from the shared table (linear interpolation).
  static inline float Cos01(float x) {
    using namespace armitage_k;
    constexpr int kMask = ARM_SIN_SIZE - 1;
    const float xi = x * (float)ARM_SIN_SIZE;
    const int   ix = (int)xi;                    // x >= 0: truncation == floor
    const float fr = xi - (float)ix;
    const int   q  = (ix + ARM_SIN_SIZE / 4) & kMask;
    const float c0 = armitage_sin_tab[q], c1 = armitage_sin_tab[(q + 1) & kMask];
    return c0 + fr * (c1 - c0);
  }

  // cos and sin of 2 pi x for x in [0, 1] (same table and interpolation as Cos01).
  static inline void CosSin01(float x, float& c, float& s) {
    using namespace armitage_k;
    constexpr int kMask = ARM_SIN_SIZE - 1;
    const float xi = x * (float)ARM_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + ARM_SIN_SIZE / 4) & kMask;
    const float c0 = armitage_sin_tab[q],  c1 = armitage_sin_tab[(q + 1) & kMask];
    const float s0 = armitage_sin_tab[i0], s1 = armitage_sin_tab[(i0 + 1) & kMask];
    c = c0 + fr * (c1 - c0);
    s = s0 + fr * (s1 - s0);
  }

  // atan2(y, x) / 2 pi wrapped to [0, 1). Octant reduction + atan(a) ~
  // a (0.97239411 - 0.19194795 a^2) on a = min/max in [0, 1]: max error
  // ~0.005 rad (0.0008 cycles). One division. (0, 0) -> 0.
  static inline float FastAtan2Cycles(float y, float x) {
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = (ax > ay) ? ax : ay;
    const float mn = (ax > ay) ? ay : ax;
    if (mx <= 0.f) return 0.f;
    const float a  = mn / mx;
    constexpr float k1 = 0.97239411f / 6.283185307f;   // in cycles
    constexpr float k3 = 0.19194795f / 6.283185307f;
    float r = a * (k1 - k3 * a * a);             // [0, 1/8]
    if (ay > ax) r = 0.25f - r;
    if (x < 0.f) r = 0.5f - r;
    if (y < 0.f) r = 1.f - r;                    // (0, 1]; 1 only at y = -0 edge
    return (r >= 1.f) ? 0.f : r;
  }

  armbands::ArmBand         bands_[kBands];
  armbands::ArmDecimator    dec_;
  armbands::ArmInterpolator int_;

  float ln_fref_[kBands];          // ln(ARM_SHAPE_REF / f_k)
  float f_k_[kBands];              // band input centre f_k (Hz)
  float a5_t_[3][kBands];          // K5 one-pole coefficient target per octave (UP / MID / DOWN)
  float ln_m_[armitage_k::ARM_ADD_MMAX + 1] = {};   // ln(m), m = 2..kMM (ADD harmonic weight exponent)
  float fb_t_[3][kBands];          // beta_k target per octave (UP / MID / DOWN)
  float tl_t_[3][kBands];          // tilt_k target per octave (ADD / PD amount)
  float add_fw_[3][kBands][armitage_k::ARM_ADD_MMAX + 1];   // ADD anti-fold weight h_mk per octave
  int   add_nh_[3][kBands];        // ADD highest harmonic with h_mk > 0 (1 = none)
  float pd_dfl_[3][kBands];        // PD knee floor dfloor_k per octave (recomputed every 8 kHz sample from v)
  float gm_[armitage_k::ARM_ADD_MMAX + 1] = {};   // ADD g_m / m^s (per 8 kHz sample, from smoothed K1)
  int   mtop_ = 1;                 // ADD highest harmonic with g_m > 0
  float k2s_ = 0.f, k2_t_ = 0.f;   // K2 micro pitch (smoothed / target)
  int   mp_wp_ = 0;                // micro-pitch buffer write index
  float mp_p_[4] = {};             // shifter window phase p_v in [0, 1): -out, -in, +in, +out
  float mp_inc_[4] = {};           // (1 - r_v) / W_v per sample (per block, from D)
  float mp_w_[4] = {}, mp_iw_[4] = {};   // window W_v (samples) and 1 / W_v
  float mp_gi_ = 0.f, mp_go_ = 0.f;   // inner / outer pair gain of the previous block
  float pd_d0_ = 0.f;              // PD depth coefficient (ARM_PD_DEPTH*u + ARM_PD_DEPTH_HI*v) before amt_k
  float w_[kBands], w_t_[kBands];  // output weight (always 1; K2 roll-off removed, kept for the smoother call site)
  Slot  slot_[2];                  // active + crossfade-in path
  int   act_ = 0;                  // index of the active slot
  int   fade_n_ = 0;               // crossfade samples left (0 = no fade)
  int   xfade_len_ = 80;           // crossfade length (8 kHz samples)
  int   n_root_ = 0;               // bands 0..n_root_-1 form the root modulator

  float sr8_    = 8000.f;
  float ctrl_a_ = 0.f;                 // control-smoothing coefficient (8 kHz)
  float k1s_ = 0.f, k1_t_ = 0.f;       // K1 (smoothed / target) for ADD / PD
  int   mdl_ = kFB, mfrom_ = kFB;      // current shaping model / model faded out
  volatile int mdl_t_ = kFB;           // SW1 target model
  int   mfade_n_ = 0;                  // model crossfade samples left (0 = no fade)
  int   mfade_len_ = 80;               // model crossfade length (8 kHz samples)
  float mg_ = 1.f;                     // new-model gain during a model fade
  float rel_a_ = 0.0025f;              // L_ref release coefficient (8 kHz)
  float bx_ = 0.f, bx_t_ = 0.f;        // K3 cross-mod depth beta_x (smoothed / target)
  volatile int oct_t_ = kMid;          // SW2 target octave

  // DIAG (main loop only)
  DiagSnap diag_tap_{}, diag_auto_{};
  bool     diag_tap_pend_ = false, diag_auto_pend_ = false;
  int      diag_n_ = 0;
  bool     diag_blink_ = false;
  uint32_t diag_blink_t_ = 0;
  bool     diag_changed_ = true;             // first settle -> one `auto` line with the start state
  uint32_t diag_t_ = 0;
  float    diag_ref_k_[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
  int      diag_ref_sw1_ = -1, diag_ref_sw2_ = -1;
};
