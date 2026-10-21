#pragma once
//
// armitage — polyphonic feedback-FM voice on a band engine.  SW3 UP on NitroLab.
//
// WHAT IT IS: a polyphonic synth voice WITHOUT pitch tracking. The input is
// split into 84 complex (analytic) bands (a private copy of the POG band
// engine, armitage_bands.h: decimate 48 -> 8 kHz, centres
// f_k = 480 * 2^(0.027 (k - 4)) - 420 = 25.4 Hz .. 1.69 kHz: POG's 80 bands plus
// 4 continued below 60 Hz by the same law; interpolate 8 -> 48 kHz).
// Each band's analytic signal z = (yr, yi) is first transposed by SW2 with
// POG's own per-band octave shift (z' = z / Up1 / Down1, complex, |z'| = |z|),
// then reshaped by one of four MODES (K5 zones) riding on the (transposed)
// played note (FB = DX7-style feedback FM, docs/FM_FEEDBACK_VOICE_SPEC.md):
//
//   z'      = SW2 UP: z^2/|z| (POG Up1)   MID: z   DOWN: |z| e^{j phi/2} with sign
//             continuation (POG Down1: half-angle identities + per-band sign flip
//             on every phase crossing of pi)
//   y0_k    = Re(z'_k e^{j xm})   the unshaped band (xm = 0 unless ARM_XMOD_ENABLE)
//   share_k = |z'_k| / max(|z'_{k-1}| + |z'_k| + |z'_{k+1}|, ARM_SHARE_FLOOR)   (overlap share,
//             all modes; edge bands: the one existing neighbour; silent bands count 0)
//   tilt_k  = K4, all modes: k4 < 0.5: min(1, (F_lo / f'_k)^p), p = ARM_TILT_P_MAX (1 - 2 k4),
//             F_lo = ARM_TILT_LO_HZ + (ARM_TILT_LO_HZ_MIN - ARM_TILT_LO_HZ)(1 - 2 k4) (150 -> 50 Hz);
//             noon: 1; k4 > 0.5: min(1, (f'_k / ARM_TILT_HI_HZ)^q), q = ARM_TILT_P_MAX (2 k4 - 1)
//   phi     = arg(z'_k e^{j xm}) / 2 pi in [0, 1)       (polynomial atan2, max error ~0.005 rad)
//   fmo_k   = I sin(phi_down,k)  (K3 FM, rad, I = ARM_FM_INDEX_MAX * k3^2; modulator = the
//             carrier's own octave-down: MID Down1(z_k), UP z_k, DOWN Down1(z_k) = carrier 1:1)
//   phi_m   = frac(phi + fmo_k / 2 pi)                  (the shapes run on the modulated phase)
//
//   SAW (K5 0 .. 0.2) and PULSE (0.2 .. 0.4) = PD base: every band above ARM_SILENCE at
//   full amount, shape only (no clean term; true replace), G = ARM_GAIN_PD in every octave,
//   plus the fixed clean mix-back c = ARM_PD_CLEAN_MIX of y0_k (no knob):
//     dfloor_k = ARM_PD_KNEE + (0.5 - ARM_PD_KNEE) clamp((f'_k - ARM_PD_FOLD_HZ) / (ARM_PD_FOLD_TOP - ARM_PD_FOLD_HZ), 0, 1)
//     cap_k    = (0.5 - dfloor_k) / (0.5 - ARM_PD_KNEE)                (anti-fold; fixed, Init)
//     SAW:  d_k   = max(0.5 - (0.5 - d(x)) tilt_k, dfloor_k)  (d_k >= 0.01: finite)
//           saw_k = |z'_k| cos(2 pi phi_w), phi_w = phi_m / (2 d_k) below d_k, else 0.5 + (phi_m - d_k) / (2 (1 - d_k))
//           (saw mean is exactly 0 for any knee: no DC term)
//     K1 split at ARM_K1_SPLIT_PD (SAW / PULSE):
//           x = min(K1 / ARM_K1_SPLIT_PD, 1)  (knee depth; knee d(x) = 0.5 (ARM_PD_KNEE / 0.5)^x, exponential:
//           0.5 = the sine at K1 = 0 -> 0.19 -> 0.07 -> 0.027 -> the CZ knee ARM_PD_KNEE at the split),
//           p = max((K1 - ARM_K1_SPLIT_PD) / (1 - ARM_K1_SPLIT_PD), 0)  (push)
//           out_k = shape_k + c y0_k   (shape only at every K1; K1 = 0: G share_k y0_k + c y0_k)
//           SAW push = resonance: R = ARM_RES_R_MIN + (ARM_RES_R_MAX - ARM_RES_R_MIN) p (1 -> 16),
//           R_k = min(R, ARM_RES_FMAX / f'_k),
//           res_k = |z'_k| (sin(2 pi R_k phi_m) (1 - phi_m) - dc(R_k)), dc(R) = 1/(2 pi R) - sin(2 pi R)/(2 pi R)^2
//           shape_k = G share_k [ (1 - p) saw_k + p tilt_k cap_k res_k ]
//           (d_k >= 0.5 and p = 0: saw_k = y0_k)
//     PULSE: push = width: w = ARM_PUL_W_MAX - (ARM_PUL_W_MAX - ARM_PUL_W_MIN) p (50 % -> 0 %)
//           S(phi_m) = 1 - E(phi_m) + E(phi_m - w), two independent edges that may overlap,
//           E(x) = 0 (x < 0), 1 - cos(pi x / D) (0 <= x < D), 2 (x >= D),
//           D = d(x) (both edges, no tilt / floor in the knee)
//           (D = w = 0.5: S = cos(2 pi phi), the sine; w = 0.5, D = ARM_PD_KNEE: the square;
//           w = 0: S = +1). Mean over phi in [0, 1): 1 - (2 - D) + (2 - 2w - D) = 1 - 2w,
//           exact for any w in [0, 0.5] and D in (0, 0.5], removed
//           a_k = 1 - x (1 - tilt_k cap_k)   (1 at K1 = 0: the sine is neither tilted nor capped)
//           shape_k = G share_k a_k |z'_k| (S - (1 - 2w))   (w = 0, K4 CW: silence)
//
//   ADD (K5 0.4 .. 0.6): additive saw per band, harmonics by repeated complex multiplication:
//     v       = (z'_k / |z'_k|) e^{j xm},  q = v e^{j fmo_k}  (q = v without FM)
//     out_k   = |z'_k| [ Re(v) + g_add share_k ( (Re(q) - Re(v)) + amt_k sum_{m=2..top} g_m h_mk Re(q^m) / m^s ) ]
//               (y0_k = |z'_k| Re(v); g_add = ARM_GAIN_ADD times ARM_GAIN_UP_DELTA / ARM_GAIN_DOWN_DELTA
//               in the shifted octaves; the FM'd fundamental also enters through g_add share_k)
//     amt_k   = s_k * tilt_k,  s_k = min(1, |z'_k| / ARM_REF_LEVEL)
//     u = min(K1/ARM_K1_SPLIT, 1), v = max((K1-ARM_K1_SPLIT)/(1-ARM_K1_SPLIT), 0)
//     g_m     = clamp(M_eff - m + 1, 0, 1),  M_eff = 1 + u (ARM_ADD_MID - 1) + v (ARM_ADD_MMAX - ARM_ADD_MID)  (1 -> 8 -> 16)
//     s       = 1 - (1 - ARM_ADD_S_MIN) v                                 (1 -> 1 -> 0.6)
//     h_mk    = clamp((ARM_ADD_FMAX - m f'_k) / ARM_ADD_FEDGE, 0, 1)   (anti-fold)
//
//   FEEDBACK (K5 0.6 .. 1; the top fifth is reserved and plays FB too):
//     theta_k = b_k * yprev_k + xm + fmo_k
//     y_k     = Re(z'_k e^{j theta_k})                     (|z'| kept)
//     out_k   = y0_k + ARM_GAIN_FB share_k (y_k - y0_k)    (replace, G in every octave)
//     yprev_k = onepole_k(y_k / |z'_k|) (full amount, every band above the gate), clamped to
//               [-1, 1], flushed below ARM_DENORM; one-pole cutoff = ARM_SMOOTH_REL_FIXED f'_k
//               clamped to [ARM_SMOOTH_HZ_LO, ARM_SMOOTH_HZ_HI]
//     b_k     = min(ARM_BETA_CLAMP, beta * tilt_k), then min(b_k, max(0, ARM_FOLD_LIMIT_HZ / f'_k - 1))
//
//   m (former K3 cross-mod, only with ARM_XMOD_ENABLE) = Re(sum_{f_k < ARM_ROOT_HZ} z'_k)
//             / max(sum_{f_k < ARM_ROOT_HZ} |z'_k|, ARM_XMOD_DEN_FLOOR * L), xm = beta_x m_prev
//   silence guard: |z|^2 < ARM_SILENCE -> out_k = 0, yprev_k = 0
//
//   f'_k = r f_k,  r = 2 / 1 / 0.5 (SW2 UP/MID/DOWN)      (transposed band frequency)
//   w_k = 1; SW2 MID with ARM_ALIGN: w_k = fitted weight 10^(W_dB(log2 f_k) / 20) (0.96 .. 1.20)
//   ARM_ALIGN (SW2 MID only): z_k -> (-1)^k z_k before everything else (shaping, K3
//         rotation, root modulator m): adjacent bands are 180 deg apart at every
//         crossover, so the aligned sum no longer notches between centres.
//         UP: squaring removes the sign (untouched). DOWN: unrotated.
//
//   wet = interpolate( ARM_OUT_GAIN * g_oct * band sum )      (48 kHz)
//         g_oct = ARM_GAIN_UP / 1 / ARM_GAIN_DOWN (SW2 UP / MID / DOWN) on the whole slot output
//   wet  -> DC blocker -> K2 micro pitch -> soft clipper -> FS1 mute   (all 48 kHz, in place)
//   soft clipper: wet = ARM_SAT_TRIM * ARM_SAT_T * tanh(wet / ARM_SAT_T)  (Pade(3,3) tanh)
//
//   K2 MICRO PITCH SHIFT DETUNE ("Eventide micro pitch" style, on the final 48 kHz wet, so
//   for every mode and SW2 octave alike; mode / octave crossfades are upstream of it):
//     The wet is written every sample into one circular buffer (ARM_MP_BUF, power of two,
//     in D2 SRAM; written even at K2 = 0 so turning K2 up has history). 4 delay-based
//     pitch shifters read it, voice v = -out, -in, +in, +out:
//       r_v   = 2^(s_v D / 1200),  s_v = ARM_MP_SPREAD[v] = -1, -0.468, +0.468, +1
//       W_v   = ARM_MP_WIN_MS[v] = 41 / 47 / 53 / 59 ms (different, so the crossfades don't line up)
//       p_v  += (1 - r_v) / W_v per sample, wrapped to [0, 1)
//       tap A delay = p_v W_v, tap B delay = frac(p_v + 0.5) W_v
//       sh_v  = sin^2(pi p_v) A + cos^2(pi p_v) B
//     Staging (smoothed K2): g_in  = clamp(k2 / ARM_SS_IN_END, 0, 1)
//                            g_out = clamp((k2 - ARM_SS_IN_END) / (ARM_SS_OUT_END - ARM_SS_IN_END), 0, 1)
//                            D = ARM_SS_D_MIN (10 c) for k2 <= ARM_SS_OUT_END, then linear to ARM_SS_D_MAX (35 c)
//     out = (wet + g_in (sh_-in + sh_+in) + g_out (sh_-out + sh_+out)) * (1 + 2 g_in^2 + 2 g_out^2)^-ARM_MP_COMP_EXP
//     A pair with gain < ARM_SS_ON over the whole block is not computed (K2 = 0: wet untouched).
//
// MODE SWITCH (K5 zone): the old and the new mode's band outputs are both computed
// for ARM_MODEL_XFADE_MS (1 ms) and crossfaded linearly; then only the new one runs.
// The entering mode starts from its targets: FB from yprev = 0 with b_k and the
// one-pole coefficients at target (FB state is not computed while the others run),
// SAW / PULSE / ADD with tilt_k at target. A zone change during a fade is picked up
// when that fade ends. Independent of the SW2 crossfade (both can run).
//
// OCTAVE SWITCH: two processing slots (each: octave, yprev[], smoothed b_k[],
// smoothed one-pole coefficients[], smoothed tilt_k[], m_prev). Normally only one
// runs. On an SW2 change the idle slot starts at the new octave and the two are
// crossfaded linearly over ARM_XFADE_MS; then the old one stops.
//
// CONTROL MAP
//   K1  timbre (per mode)   SAW / PULSE: CCW -> ARM_K1_SPLIT_PD knee 0.5 (sine) -> ARM_PD_KNEE, shape only, then
//                           SAW: resonance (saw -> CZ reso morph, R 1 -> 16)
//                           PULSE: width 50 % -> 0 % (silence at CW)
//                           ADD: M_eff = 1 + u*7 + v*8, weight 1/m^s (s 1 -> 0.6 over v)
//                           FB: beta = ARM_BETA_MAX * k1^2
//   K2  micro pitch detune  4 pitch shifters on the final wet (all modes / octaves)
//   K3  FM index            I = ARM_FM_INDEX_MAX * k3^2 (0 -> 12 rad, squared), all modes; K3 = 0: no FM
//   K4  shaping tilt        full range, all modes: CCW warm (low bands only, p 4, F_lo 50 Hz) -> noon every
//                           band full -> CW bright (bands >= ARM_TILT_HI_HZ only, q 4)
//   K5  mode                SAW | PULSE | ADD | FEEDBACK (| reserved = FEEDBACK), fifths of the travel
//   K6  dry/wet             (shell)
//   SW1 unused (reads nothing)
//   SW2 octave              UP = octave up, MID = as played, DOWN = octave down
//   FS1  wet mute (test tool)  rising edge toggles mute_; mute_g_ slides over
//                              ARM_MUTE_SLIDE_MS; dry untouched. LED2 on while muted.
//                              FS2 unused. LED1 on.
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
  enum { kSAW = 0, kPUL = 1, kADD = 2, kFB = 3, kFade = 4 };   // mode = K5 zone; kFade = both (crossfade)
  static constexpr int kMM = armitage_k::ARM_ADD_MMAX;

  void Init(float sr) override {
    using namespace armitage_k;
    sr8_ = sr / (float)kFactor;
    for (int i = 0; i < ARM_SIN_SIZE; i++)
      armitage_sin_tab[i] = sinf(6.283185307f * (float)i / (float)ARM_SIN_SIZE);
    for (int m = 2; m <= kMM; m++) ln_m_[m] = logf((float)m);   // ADD harmonic weight: 1/m^s = expf(-s*ln_m_[m])
    ln_knee_ = logf(ARM_PD_KNEE / 0.5f);   // SAW / PULSE knee law d(x) = 0.5 exp(x ln_knee_)
    n_root_ = 0;
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;   // incl. the two zero guards
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      ln_f_[k] = logf(f);
      f_k_[k] = f;
      if (f < ARM_ROOT_HZ) n_root_ = k + 1;   // centres rise with k
      w_[k] = w_t_[k] = 1.f;                  // K2 roll-off removed: output weight always 1
      {                                       // ARM_ALIGN: fitted SW2 MID output weight (smooth in log f)
        float fc = f;
        if (fc < ARM_ALIGN_W_FLO) fc = ARM_ALIGN_W_FLO;
        if (fc > ARM_ALIGN_W_FHI) fc = ARM_ALIGN_W_FHI;
        const float x = log2f(fc);
        wmid_[k] = ARM_ALIGN ? powf(10.f, (ARM_ALIGN_WA * x * x + ARM_ALIGN_WB * x + ARM_ALIGN_WC) / 20.f) : 1.f;
      }
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
        // PD base (SAW / PULSE), fixed per band and octave: anti-fold knee floor
        // dfloor_k, cap_k = (0.5 - dfloor_k) / (0.5 - ARM_PD_KNEE), and the resonance
        // R_k cap ARM_RES_FMAX / f'_k with its DC dc(R_cap) (used where R > R_cap).
        {
          float x = (fp - ARM_PD_FOLD_HZ) / (ARM_PD_FOLD_TOP - ARM_PD_FOLD_HZ);
          x = (x < 0.f) ? 0.f : (x > 1.f) ? 1.f : x;
          pd_dfl_[o][k] = ARM_PD_KNEE + (0.5f - ARM_PD_KNEE) * x;
          float ck = (0.5f - pd_dfl_[o][k]) / (0.5f - ARM_PD_KNEE);
          pd_ck_[o][k] = (ck < 0.f) ? 0.f : (ck > 1.f) ? 1.f : ck;
          res_rcap_[o][k] = ARM_RES_FMAX / fp;
          res_dccap_[o][k] = ResDc(res_rcap_[o][k]);
        }
      }
    }
    SetSmoothTargets(ARM_SMOOTH_REL_FIXED);   // FB smoothing: fixed (K5 = mode selector)
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
    dc_a_ = expf(-6.283185307f * ARM_DC_HZ / sr);   // wet DC blocker coefficient
    dc_x1_ = dc_y1_ = 0.f;
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
    bx_ = bx_t_ = 0.f;
    fm_ = fm_t_ = 0.f;
    oct_t_ = kMid;
    mute_ = false;
    mute_g_ = 1.f;
    mute_step_ = 1.f / (ARM_MUTE_SLIDE_MS * 0.001f * sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace armitage_k;
    if (cs.Foot(0).rising) mute_ = !mute_;   // FS1: wet mute toggle (test tool)
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));   // micro pitch detune
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));   // mode selector
    int oct = cs.Switch(1);
    if (oct < kUp || oct > kDown) oct = kMid;
    // SW1 reads nothing (the mode moved to K5).
    const int mode = (k5 < ARM_MODE_ZONE[0]) ? kSAW
                   : (k5 < ARM_MODE_ZONE[1]) ? kPUL
                   : (k5 < ARM_MODE_ZONE[2]) ? kADD : kFB;   // top two fifths: FB (5th reserved)
    mdl_t_ = mode;
    k1_t_ = k1;
    k2_t_ = k2;

    const float beta  = ARM_BETA_MAX * k1 * k1;
    // K4 tilt: tilt_k = min(1, (f'_k / F)^s) = min(1, exp(s (ln f_k - ln F)) r^s), signed exponent s:
    //   low side  (k4 < 0.5): s = -p, p = ARM_TILT_P_MAX e, F = F_lo = ARM_TILT_LO_HZ + (ARM_TILT_LO_HZ_MIN - ARM_TILT_LO_HZ) e, e = 1 - 2 k4
    //   high side (k4 > 0.5): s = +q, q = ARM_TILT_P_MAX (2 k4 - 1), F = ARM_TILT_HI_HZ
    //   noon: s = 0 -> every tilt_k exactly 1
    // Cost: 84 expf (one per band, MID) + 2 for the octave factors per Controls() call (main loop).
    float p, lnF;
    if (k4 < 0.5f) {
      const float e = 1.f - 2.f * k4;
      p   = -ARM_TILT_P_MAX * e;
      lnF = logf(ARM_TILT_LO_HZ + (ARM_TILT_LO_HZ_MIN - ARM_TILT_LO_HZ) * e);
    } else {
      p   = ARM_TILT_P_MAX * (2.f * k4 - 1.f);
      lnF = logf(ARM_TILT_HI_HZ);
    }
    const float ln2   = 0.693147181f;
    const float rp[3] = { expf(p * ln2), 1.f, expf(-p * ln2) };   // r^s: UP r=2, MID, DOWN r=1/2
    static const float r[3] = { 2.f, 1.f, 0.5f };          // UP / MID / DOWN
    for (int k = 0; k < kBands; k++) {
      const float t1 = expf(p * (ln_f_[k] - lnF));         // (f_k / F)^s
      for (int o = 0; o < 3; o++) {
        float t = t1 * rp[o];                              // (f'_k / F)^s
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
    bx_t_ = ARM_XMOD_ENABLE ? ARM_XMOD_MAX * k3 * k3 : 0.f;   // former K3 cross-mod (off)
    fm_t_ = ARM_FM_INDEX_MAX * k3 * k3;                    // K3: FM index I (rad), squared taper
    oct_t_ = oct;

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);   // LED2 on while wet is muted

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      DiagControls(cs, led1, kv, mode, oct, beta, p, fm_t_);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;                 // tap number (#n), 0 = automatic line
    float k[5];              // K1..K5, the module's remapped 0..1 values
    int   sw1, sw2;          // sw1 = mode (0 SAW, 1 PULSE, 2 ADD, 3 FB); sw2: 0 UP, 1 MID, 2 DOWN
    float beta, p, fm;       // derived values the engine runs toward (FB beta, K4 signed tilt exponent s: < 0 low side, > 0 high side, K3 FM index I)
    float ss_d, ss_in, ss_out;   // K2 micro-pitch target: outer detune D (cents), inner / outer pair gain
    int   oct;               // 0 up, 1 played, 2 down
    bool  mute;              // FS1 wet mute state
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
      fm_  += (fm_t_ - fm_) * ca;                // K3 FM index (0 stays exactly 0)
      k1s_ += (k1_t_ - k1s_) * ca;
      k2s_ += (k2_t_ - k2s_) * ca;              // K2 micro pitch: staged per block below

      // Start a mode crossfade when the K5 zone changed and no mode fade is running.
      if (mfade_n_ == 0 && mdl_t_ != mdl_) {
        mfrom_ = mdl_;
        mdl_ = mdl_t_;
        // The entering mode starts from its targets: FB from yprev = 0 with b_k and
        // K5 coefficients at target; SAW / PULSE / ADD with tilt_k at target.
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
      const bool fading = (mfade_n_ > 0);
      auto uses = [&](int m) { return mdl_ == m || (fading && mfrom_ == m); };
      if (uses(kADD)) {                          // ADD harmonic weights g_m / m^s (shared by all bands)
        // K1 split at ARM_K1_SPLIT into u (lower 60% of travel, scaled 0..1) and
        // v (upper 40%, scaled 0..1).
        const float u = (k1s_ < ARM_K1_SPLIT) ? (k1s_ / ARM_K1_SPLIT) : 1.f;
        const float v = (k1s_ > ARM_K1_SPLIT) ? (k1s_ - ARM_K1_SPLIT) / (1.f - ARM_K1_SPLIT) : 0.f;
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
      if (uses(kSAW) || uses(kPUL)) {            // SAW / PULSE K1 split at ARM_K1_SPLIT_PD
        // lower part: knee depth x (d 0.5 -> ARM_PD_KNEE, shape only); upper part: push p (full knee)
        pd_a_ = (k1s_ < ARM_K1_SPLIT_PD) ? k1s_ / ARM_K1_SPLIT_PD : 1.f;
        pd_p_ = (k1s_ > ARM_K1_SPLIT_PD) ? (k1s_ - ARM_K1_SPLIT_PD) / (1.f - ARM_K1_SPLIT_PD) : 0.f;
        // knee d(x) = 0.5 (ARM_PD_KNEE / 0.5)^x, exponential in x; x = 1: exactly ARM_PD_KNEE
        pd_d_ = (pd_a_ >= 1.f) ? ARM_PD_KNEE : 0.5f * expf(pd_a_ * ln_knee_);
        pd_dd_ = 0.5f - pd_d_;                   // knee depth 0.5 - d(x) (SAW: times tilt_k)
      }
      if (uses(kSAW)) {                          // SAW resonance (push p): R and dc(R) shared by all uncapped bands
        res_r_  = ARM_RES_R_MIN + (ARM_RES_R_MAX - ARM_RES_R_MIN) * pd_p_;
        res_dc_ = (pd_p_ > 0.f) ? ResDcF(res_r_) : 0.f;
      }
      if (uses(kPUL)) {                          // PULSE width (push p) and its mean 1 - 2w
        pul_w_  = ARM_PUL_W_MAX - (ARM_PUL_W_MAX - ARM_PUL_W_MIN) * pd_p_;
        pul_dc_ = 1.f - 2.f * pul_w_;
        pul_d_  = pd_d_;                       // knee D = d(x) (both edges): 0.5 at K1 = 0 -> ARM_PD_KNEE at the split
        pul_hid_ = 0.5f / pul_d_;
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
    DcBlock(wet, size);   // wet-only DC blocker (FB carries DC); dry is untouched by the shell mix
    MicroPitch(wet, size);
    Saturate(wet, size);    // wet soft clipper ARM_SAT_T * tanh(x / ARM_SAT_T) * ARM_SAT_TRIM
    ApplyMute(wet, size);   // FS1 test-tool mute, last step: declicked wet gain, dry untouched
  }

 private:
  struct Slot {
    int   oct;
    float m;                 // root modulator of the previous 8 kHz sample
    float yprev[kBands];     // feedback state per band
    float fb[kBands];        // effective feedback beta (smoothed toward fb_t_[oct])
    float a5[kBands];        // FB one-pole coefficient per band (smoothed toward a5_t_[oct])
    float tl[kBands];        // tilt_k per band (smoothed toward tl_t_[oct]; ADD / PD)
  };

  void ResetSlot(Slot& s, int oct) {
    s.oct = oct;
    s.m   = 0.f;
    for (int k = 0; k < kBands; k++) {
      s.yprev[k] = s.fb[k] = 0.f;
      s.a5[k] = a5_t_[oct][k];
      s.tl[k] = tl_t_[oct][k];
    }
  }

  // FB one-pole targets: per octave and band, cutoff = c * f'_k (f'_k = r f_k), clamped (c fixed).
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

  // One 8 kHz sample of one slot, returns g_oct * band sum (g_model is inside, on the shaping delta).
  // update = true: also clocks the band filters and the w_k smoothers (the
  // first slot of the sample does it; the second, during a fade, reads them).
  // kern = kSAW / kPUL / kADD / kFB, or kFade (old mfrom_ and new mdl_ crossfaded).
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
      case kSAW: return Run<O, kUpdate, kSAW>(s, d);
      case kPUL: return Run<O, kUpdate, kPUL>(s, d);
      case kADD: return Run<O, kUpdate, kADD>(s, d);
      case kFB:  return Run<O, kUpdate, kFB>(s, d);
      default:   return Run<O, kUpdate, kFade>(s, d);
    }
  }

  // Gain on the shaping delta of mode M in octave O. ADD: g_model * g_oct_delta
  // (MID: 1; UP / DOWN: ARM_GAIN_UP_DELTA / ARM_GAIN_DOWN_DELTA). FB ("replace"): G alone
  // in every octave (ARM_GAIN_FB). SAW / PULSE: G on the shape itself (no clean term,
  // ARM_GAIN_PD), every octave. Constant-folded.
  template <int M, int O>
  static constexpr float DeltaGain() {
    using namespace armitage_k;
    return (M == kFB) ? ARM_GAIN_FB
         : (M == kADD) ? ARM_GAIN_ADD * ((O == kUp) ? ARM_GAIN_UP_DELTA : (O == kDown) ? ARM_GAIN_DOWN_DELTA : 1.f)
         : ARM_GAIN_PD;
  }

  // Per-sample values shared by all bands of one slot.
  struct Ctx {
    float g;         // FB: 1 / ARM_REF_LEVEL (feedback signal)
    float invl;      // 1 / ARM_REF_LEVEL (s_k)
    float xm;        // cross-mod phase (rad)
    float xc, xs;    // cos / sin of xm (rotation, y0 = unshaped band)
  };

  // One band of mode M (kSAW / kPUL / kADD / kFB). zr, zi = z'_k; mag = |z'_k|;
  // inv = 1/|z'_k|; fmo = K3 FM phase offset I sin(phi_down,k) in rad (0 = no FM).
  // Returns out_k.
  template <int M, int O>
  inline float BandOut(Slot& s, int k, float zr, float zi, float mag, float inv,
                       float fmo, const Ctx& c) {
    using namespace armitage_k;
    // Overlap share on the shaping delta (all modes): mag_[k + 1] = |z'_k|, neighbours
    // at k / k + 2 (zero guards at the ends). ADD / FB: out_k = y0 + G share_k (y_shaped - y0);
    // SAW / PULSE: out_k = G share_k y_shaped + c y0 (K4 clean mix-back).
    // y0 = Re(z'_k e^{j xm}) = the unshaped band. FM (K3) acts on the shaped term only.
    float den = mag_[k] + mag_[k + 1] + mag_[k + 2];
    if (den < ARM_SHARE_FLOOR) den = ARM_SHARE_FLOOR;
    const float share = mag / den;
    const float wr = zr * c.xc - zi * c.xs;      // y0 = Re(z' e^{j xm})
    if (M == kFB) {
      float sn, cs;
      float th = s.fb[k] * s.yprev[k] + c.xm;
      if (fmo != 0.f) th += fmo;                 // K3 FM on the carrier phase
      SinCos(th, sn, cs);
      const float out = zr * cs - zi * sn;
      // Feedback at full amount: yprev = y / |z'| (every band above the gate), K5-fixed one-pole.
      float y = s.yprev[k] + (out * inv - s.yprev[k]) * s.a5[k];
      if (y > 1.f) y = 1.f;
      if (y < -1.f) y = -1.f;
      if (fabsf(y) < ARM_DENORM) y = 0.f;
      s.yprev[k] = y;                            // feedback from the UNSCALED band output
      return wr + DeltaGain<M, O>() * share * (out - wr);   // replace: G on the delta, every octave
    }
    if (M == kADD) {
      float sh = mag * c.invl;                   // s_k = min(1, |z'| / ARM_REF_LEVEL)
      if (sh > 1.f) sh = 1.f;
      const float amt = sh * s.tl[k];            // amt_k = s_k * tilt_k
      const float ur = zr * inv, ui = zi * inv;  // u = z' / |z'|
      const float vr = ur * c.xc - ui * c.xs;    // u e^{j xm}
      const float vi = ur * c.xs + ui * c.xc;
      if (fmo == 0.f)                            // y0 = mag * vr: delta = harmonic term
        return mag * (vr + DeltaGain<M, O>() * share * amt * AddSum<O>(k, vr, vi));
      float fs, fc;                              // K3 FM: rotate the unit phasor by fmo
      SinCos(fmo, fs, fc);
      const float qr = vr * fc - vi * fs, qi = vr * fs + vi * fc;
      // shaped = mag (Re(q) + amt sum(q)); delta against the clean y0 = mag vr
      return mag * (vr + DeltaGain<M, O>() * share * ((qr - vr) + amt * AddSum<O>(k, qr, qi)));
    }
    // PD base (SAW / PULSE): shape only (no clean term), tilt_k from K4, anti-fold cap per
    // band, plus the fixed clean mix-back ARM_PD_CLEAN_MIX * y0.
    const float cy = ARM_PD_CLEAN_MIX * wr;      // clean mix-back c y0_k
    const float xa = pd_a_;                      // K1 lower part: knee depth x (0: d = 0.5 sine, 1: full knee)
    const float wi = zr * c.xs + zi * c.xc;
    const float tl = s.tl[k];
    constexpr float kTwoPiInv = 0.159154943f;
    if (M == kSAW) {
      float dk = 0.5f - pd_dd_ * tl;             // knee: K1 depth (0.5 - d(x)) and tilt (and the cap) inside, a_k = 1
      if (dk < pd_dfl_[O][k]) dk = pd_dfl_[O][k];
      const float m = pd_p_;                     // K1 upper part: saw -> resonance morph
      const float g = DeltaGain<M, O>() * share;
      if (dk >= 0.5f && m <= 0.f) return g * wr + cy;   // no distortion, no resonance: saw_k = y0
      float ph = FastAtan2Cycles(wi, wr);        // [0, 1)
      if (fmo != 0.f) ph = Wrap01(ph + fmo * kTwoPiInv);
      float saw = wr;                            // d_k >= 0.5: the sine, = y0
      if (dk < 0.5f) {
        const float pw = (ph < dk) ? ph * (0.5f / dk)
                                   : 0.5f + (ph - dk) * (0.5f / (1.f - dk));
        saw = mag * Cos01(pw);
      }
      if (m <= 0.f) return g * saw + cy;         // K1 <= split: progressive knee (shape only)
      const float rc = res_rcap_[O][k];
      const float rk = (res_r_ < rc) ? res_r_ : rc;           // R_k = min(R, 3800 / f'_k)
      const float dc = (res_r_ < rc) ? res_dc_ : res_dccap_[O][k];
      const float res = mag * (Sin01(rk * ph) * (1.f - ph) - dc);
      const float ares = tl * pd_ck_[O][k];      // a_k = tilt_k cap_k
      return g * ((1.f - m) * saw + m * ares * res) + cy;   // morph on the shape only (x = 1 here)
    }
    // kPUL: width w (K1 push), jumps at 0 (down) and w (up), knee D = pul_d_ (K1 lower part:
    // 0.5 -> ARM_PD_KNEE), mean removed. Amount a_k = 1 - x (1 - tilt_k cap_k): 1 at x = 0
    // (the sine, no shaping to tilt or cap), tilt_k cap_k at x = 1.
    const float a = 1.f - xa * (1.f - tl * pd_ck_[O][k]);
    float ph = FastAtan2Cycles(wi, wr);
    if (fmo != 0.f) ph = Wrap01(ph + fmo * kTwoPiInv);
    const float D = pul_d_, kHalfInvD = pul_hid_;
    const float w = pul_w_;
    // S = 1 - E(ph) + E(ph - w): down-edge at 0, up-edge at w, independent (they overlap for w < D).
    // 1 - E(x) = cos(pi x / D) on the knee, -1 past it; E(ph - w) = 0 before w.
    float S = (ph < D) ? Cos01(ph * kHalfInvD) : -1.f;          // 1 - E(ph)
    const float u = ph - w;
    if (u >= D)        S += 2.f;                                 // + E(ph - w)
    else if (u >= 0.f) S += 1.f - Cos01(u * kHalfInvD);
    const float g = DeltaGain<M, O>() * share * a;
    return g * mag * (S - pul_dc_) + cy;         // shape only (w = 0: S - dc = 0; D = w = 0.5: S = cos, the sine)
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
    c.invl = 1.f / ARM_REF_LEVEL;                // fixed reference (constant-folded)
    c.g    = c.invl;
    c.xm   = bx_ * s.m;                          // cross-mod phase, previous sample's m
    SinCos(c.xm, c.xs, c.xc);                    // all models: y0 = Re(z' e^{j xm}) (FB: share delta)
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
    float* __restrict mg  = mag_ + 1;            // mg[k] = |z_k| (0 = silent); mag_[0], mag_[kBands + 1] = 0
    float* __restrict ivs = inv_;
    const int nroot = n_root_;
    const float fmI = fm_;                       // K3 FM index (rad); 0 = no FM (bit-identical)
    // Pass 1 (first slot of the sample only): clock the bands, all magnitudes first
    // (the ADD overlap share needs both neighbours). A fading second slot reuses them
    // (|z'| = |z| in every octave).
    if (kUpdate) {
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        w[k] += (wt[k] - w[k]) * ca;
        const float yr = bands_[k].Re();
        const float yi = bands_[k].Im();
        const float n  = yr * yr + yi * yi;
        if (n < ARM_SILENCE) {
          mg[k] = 0.f;
          ivs[k] = 0.f;
        } else {
          const float inv = armbands::ArmFastInvSqrt(n);   // 1/|z| = 1/|z'|
          ivs[k] = inv;
          mg[k]  = n * inv;                      // |z'| = |z|
        }
      }
    }
    float acc = 0.f, acc2 = 0.f, mnum = 0.f, mden = 0.f;
    for (int k = 0; k < kBands; k++) {
      if (M == kFB || M == kFade) {              // FB state: smoothed only while FB runs
        sfb[k] += (fbt[k] - sfb[k]) * ca;
        sa5[k] += (a5t[k] - sa5[k]) * ca;
      }
      if (M != kFB) stl[k] += (tlt[k] - stl[k]) * ca;   // tilt_k: only while ADD / PD run
      const float mag = mg[k];
      if (mag == 0.f) {                          // silent band (|z|^2 < ARM_SILENCE)
        syp[k] = 0.f;
        continue;
      }
      const float inv = ivs[k];
      float zr, zi;
      if (O == kMid) {
        zr = bands_[k].Re(); zi = bands_[k].Im();
        if (ARM_ALIGN && (k & 1)) {              // phase alignment: odd bands rotated by pi (z -> -z)
          zr = -zr;                              // before the shaping, the K3 rotation and the root modulator
          zi = -zi;
        }
      } else if (O == kUp) {
        bands_[k].ArmUp1(inv, zr, zi);
      } else {
        bands_[k].ArmDown1(inv, zr, zi);
      }
      if (k < nroot) {
        mnum += zr;
        mden += mag;
      }
      // K3 FM: fmo = I sin(phi of the carrier's own octave-down), from the band itself:
      // MID: Down1(z_k) (half phase); UP: z_k (the played band, an octave below z^2/|z|);
      // DOWN: Down1(z_k) = the carrier itself (1:1; no quarter-phase signal exists).
      float fmo = 0.f;
      if (fmI != 0.f) {
        float sd;
        if (O == kMid) {
          float dr, di;
          bands_[k].ArmDown1(inv, dr, di);
          sd = di * inv;
        } else if (O == kUp) {
          sd = bands_[k].Im() * inv;
        } else {
          sd = zi * inv;
        }
        fmo = fmI * sd;
      }
      // Output weight: w_k, times the fitted ARM_ALIGN weight on SW2 MID (UP / DOWN: 1).
      const float wk = (O == kMid && ARM_ALIGN) ? w[k] * wmid_[k] : w[k];
      if (M != kFade) {
        acc += wk * BandOut<M, O>(s, k, zr, zi, mag, inv, fmo, c);
      } else {                                   // mode crossfade: old (acc) and new (acc2)
        acc  += wk * BandOutRT<O>(mf, s, k, zr, zi, mag, inv, fmo, c);
        acc2 += wk * BandOutRT<O>(mt, s, k, zr, zi, mag, inv, fmo, c);
      }
    }
    {                                            // K3 modulator denominator floor (fixed, a share of
                                                  // ARM_REF_LEVEL): root-band leakage near silence no
                                                  // longer jumps m (clicks); 1.0 = stronger, also
                                                  // tames K3 on notes without root content
      constexpr float kDenFloor = ARM_XMOD_DEN_FLOOR * ARM_REF_LEVEL;
      const float den = (mden > kDenFloor) ? mden : kDenFloor;
      s.m = mnum / den;
    }
    if (M != kFade) return acc;                  // g_model is applied per band, on the shaping delta
    return (1.f - mg_) * acc + mg_ * acc2;
  }

  // Runtime-dispatched BandOut (mode crossfade only).
  template <int O>
  inline float BandOutRT(int m, Slot& s, int k, float zr, float zi, float mag,
                         float inv, float fmo, const Ctx& c) {
    if (m == kSAW) return BandOut<kSAW, O>(s, k, zr, zi, mag, inv, fmo, c);
    if (m == kPUL) return BandOut<kPUL, O>(s, k, zr, zi, mag, inv, fmo, c);
    if (m == kADD) return BandOut<kADD, O>(s, k, zr, zi, mag, inv, fmo, c);
    return BandOut<kFB, O>(s, k, zr, zi, mag, inv, fmo, c);
  }

  // Mean of sin(2 pi R phi) (1 - phi) over phi in [0, 1): 1/a - sin(a)/a^2, a = 2 pi R.
  static float ResDc(float r) {                  // Init (double)
    const double a = 6.283185307179586 * (double)r;
    return (float)(1.0 / a - sin(a) / (a * a));
  }
  static inline float ResDcF(float r) {          // per 8 kHz sample (one shared R)
    const float a = 6.283185307f * r;
    const float ia = 1.f / a;
    return ia - sinf(a) * ia * ia;
  }

  // One-pole DC blocker on the final 48 kHz wet, in place: y = x - x1 + a*y1.
  // Removes the FB model's DC offset before it reaches MicroPitch() (5 shifted
  // copies would otherwise sum that DC in phase and eat headroom). Wet only —
  // the shell mixes the dry, which must stay untouched.
  void DcBlock(float* wet, size_t size) {
    float x1 = dc_x1_, y1 = dc_y1_;
    const float a = dc_a_;
    for (size_t n = 0; n < size; n++) {
      const float x = wet[n];
      const float y = x - x1 + a * y1;
      wet[n] = y;
      x1 = x;
      y1 = y;
    }
    dc_x1_ = x1;
    dc_y1_ = y1;
  }

  // Wet soft clipper (in place, final 48 kHz wet, after MicroPitch, before the
  // mute): y = ARM_SAT_TRIM * ARM_SAT_T * tanh(x / ARM_SAT_T). Fixed post trim, no
  // 1/drive compensation; |y| <= ARM_SAT_TRIM * ARM_SAT_T. Dry untouched.
  void Saturate(float* wet, size_t size) {
    using namespace armitage_k;
    constexpr float kIn  = 1.f / ARM_SAT_T;
    constexpr float kOut = ARM_SAT_T * ARM_SAT_TRIM;
    for (size_t n = 0; n < size; n++) wet[n] = kOut * PadeTanh(wet[n] * kIn);
  }

  // tanh(x) by Pade(3,3), x (27 + x^2) / (27 + 9 x^2), clamped to +-1 for |x| > 3
  // (value 1 and slope 0 at |x| = 3: C1-continuous, monotonic). The same
  // approximation as MoogLadder::Saturate (src/core/blocks/moog_ladder.h, private
  // there, so repeated here). Max error 0.024 at |x| ~ 1.57 (2.6 % relative near
  // |x| ~ 1.45); 0.001 at |x| = 0.3, slope at 0 exactly 1.
  static inline float PadeTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }

  // FS1 test-tool mute (in place, final 48 kHz wet). mute_g_ slides linearly
  // toward the target (0 muted / 1 unmuted) at ARM_MUTE_SLIDE_MS per sample —
  // never latched/deferred. mute_g_ == 1.f (unmuted, default) multiplies
  // every sample by exactly 1.0, so unmuted output stays bit-identical.
  void ApplyMute(float* wet, size_t size) {
    const float target = mute_ ? 0.f : 1.f;
    const float step = mute_step_;
    float g = mute_g_;
    for (size_t n = 0; n < size; n++) {
      if (g < target)      { g += step; if (g > target) g = target; }
      else if (g > target) { g -= step; if (g < target) g = target; }
      wet[n] *= g;
    }
    mute_g_ = g;
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
                    int sw1, int oct, float beta, float p, float fm) {
    using namespace armitage_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.sw1 = sw1; s.sw2 = oct; s.oct = oct;
    s.beta = beta; s.p = p; s.fm = fm;
    s.mute = mute_;
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

  // sin(2 pi x) for x >= 0 (table, linear interpolation, mask wrap).
  static inline float Sin01(float x) {
    using namespace armitage_k;
    constexpr int kMask = ARM_SIN_SIZE - 1;
    const float xi = x * (float)ARM_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const float s0 = armitage_sin_tab[i0], s1 = armitage_sin_tab[(i0 + 1) & kMask];
    return s0 + fr * (s1 - s0);
  }

  // x wrapped to [0, 1) (x within a few cycles of [0, 1)).
  static inline float Wrap01(float x) {
    x -= floorf(x);
    return (x >= 1.f) ? 0.f : x;
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

  float ln_f_[kBands];             // ln f_k (K4 tilt)
  float f_k_[kBands];              // band input centre f_k (Hz)
  float a5_t_[3][kBands];          // FB one-pole coefficient target per octave (fixed c) (UP / MID / DOWN)
  float ln_m_[armitage_k::ARM_ADD_MMAX + 1] = {};   // ln(m), m = 2..kMM (ADD harmonic weight exponent)
  float fb_t_[3][kBands];          // beta_k target per octave (UP / MID / DOWN)
  float tl_t_[3][kBands];          // tilt_k target per octave (ADD / PD amount)
  float add_fw_[3][kBands][armitage_k::ARM_ADD_MMAX + 1];   // ADD anti-fold weight h_mk per octave
  int   add_nh_[3][kBands];        // ADD highest harmonic with h_mk > 0 (1 = none)
  float pd_dfl_[3][kBands];        // PD base knee floor dfloor_k per octave (fixed, Init)
  float pd_ck_[3][kBands];         // PD base cap_k = (0.5 - dfloor_k) / (0.5 - ARM_PD_KNEE) (PULSE / resonance amount)
  float res_rcap_[3][kBands];      // resonance R cap ARM_RES_FMAX / f'_k
  float res_dccap_[3][kBands];     // dc(R cap) (used where R > R cap)
  float res_r_ = 1.f, res_dc_ = 0.f;   // SAW resonance R (from K1) and dc(R), per 8 kHz sample
  float pul_w_ = 0.5f, pul_dc_ = 0.f;  // PULSE width w (from K1) and its mean 1 - 2w, per 8 kHz sample
  float pd_a_ = 0.f, pd_p_ = 0.f;      // SAW / PULSE K1: knee depth x, push p (per 8 kHz sample)
  float pd_d_ = 0.5f, pd_dd_ = 0.f;    // SAW / PULSE knee d(x) and depth 0.5 - d(x)
  float ln_knee_ = 0.f;                // ln(ARM_PD_KNEE / 0.5) (Init)
  float pul_d_ = 0.5f, pul_hid_ = 1.f; // PULSE knee D (K1 lower part) and 0.5 / D
  float gm_[armitage_k::ARM_ADD_MMAX + 1] = {};   // ADD g_m / m^s (per 8 kHz sample, from smoothed K1)
  int   mtop_ = 1;                 // ADD highest harmonic with g_m > 0
  float k2s_ = 0.f, k2_t_ = 0.f;   // K2 micro pitch (smoothed / target)
  int   mp_wp_ = 0;                // micro-pitch buffer write index
  float mp_p_[4] = {};             // shifter window phase p_v in [0, 1): -out, -in, +in, +out
  float mp_inc_[4] = {};           // (1 - r_v) / W_v per sample (per block, from D)
  float mp_w_[4] = {}, mp_iw_[4] = {};   // window W_v (samples) and 1 / W_v
  float mp_gi_ = 0.f, mp_go_ = 0.f;   // inner / outer pair gain of the previous block
  float dc_a_ = 0.f;                  // wet DC blocker coefficient (exp(-2*pi*ARM_DC_HZ/sr))
  float dc_x1_ = 0.f, dc_y1_ = 0.f;   // wet DC blocker state (previous input / output)
  float mag_[kBands + 2];          // |z_k| of the current 8 kHz sample at [k + 1] (0 = silent); [0], [kBands + 1] = 0 guards
  float inv_[kBands];              // 1/|z_k| of the current 8 kHz sample (0 = silent)
  float w_[kBands], w_t_[kBands];  // output weight (always 1; K2 roll-off removed, kept for the smoother call site)
  float wmid_[kBands];             // ARM_ALIGN fitted output weight, SW2 MID only (1 without ARM_ALIGN)
  Slot  slot_[2];                  // active + crossfade-in path
  int   act_ = 0;                  // index of the active slot
  int   fade_n_ = 0;               // crossfade samples left (0 = no fade)
  int   xfade_len_ = 80;           // crossfade length (8 kHz samples)
  int   n_root_ = 0;               // bands 0..n_root_-1 form the root modulator

  float sr8_    = 8000.f;
  float ctrl_a_ = 0.f;                 // control-smoothing coefficient (8 kHz)
  float k1s_ = 0.f, k1_t_ = 0.f;       // K1 (smoothed / target) for ADD / SAW / PULSE
  int   mdl_ = kFB, mfrom_ = kFB;      // current mode / mode faded out
  volatile int mdl_t_ = kFB;           // K5 target mode
  int   mfade_n_ = 0;                  // model crossfade samples left (0 = no fade)
  int   mfade_len_ = 80;               // model crossfade length (8 kHz samples)
  float mg_ = 1.f;                     // new-model gain during a model fade
  float bx_ = 0.f, bx_t_ = 0.f;        // former K3 cross-mod depth beta_x (smoothed / target; 0 unless ARM_XMOD_ENABLE)
  float fm_ = 0.f, fm_t_ = 0.f;        // K3 FM index I (rad; smoothed / target)
  volatile int oct_t_ = kMid;          // SW2 target octave
  volatile bool mute_ = false;         // FS1 wet mute target (test tool)
  float mute_g_ = 1.f;                 // wet mute gain, slides toward mute_ ? 0 : 1
  float mute_step_ = 0.f;              // mute_g_ step per 48 kHz sample (1 / (ARM_MUTE_SLIDE_MS ms))

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
