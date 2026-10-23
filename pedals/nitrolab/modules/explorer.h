#pragma once
//
// explorer — sound-exploration module on the armitage band engine.  SW3 MIDDLE on NitroLab.
//
// The band engine is COPIED from armitage.h (not shared): the input is decimated 48 -> 8 kHz,
// split into 84 complex bands (armitage_bands.h, included read-only), each band reshaped by one
// of 14 models, summed, interpolated back to 48 kHz, DC-blocked, soft-clipped, FS1-muted.
// No K4 post filter, no envelopes / onset detector, no LFO. No pitch tracking: every band runs
// the selected model on its OWN phase phi_k and magnitude a_k = |z'_k|; nothing picks or decides
// per band.
//
// SLOTS (each a full voice, the model runs per slot):
//   played  z'_k = (-1)^k z_k (phase alignment, ARM_ALIGN), output weight w_k (fitted, armitage's wmid_)
//   up      z'_k = z_k^2 / |z_k|                 (POG Up1, f'_k = 2 f_k), slot gain EXP_GAIN_UP
//   down    z'_k = Down1(z_k) (half phase, sign continuation; f'_k = f_k / 2), slot gain EXP_GAIN_DOWN
//   u_k = z'_k / |z'_k| = (cos 2 pi phi_k, sin 2 pi phi_k), phi_k = atan2 / 2 pi in [0, 1) (polynomial)
//   y0_k = Re(z'_k) = a_k cos(2 pi phi_k)        (the unshaped band, = armitage's unshaped band)
//   shaped_k = a_k W(...)                         (W per model, below)
//   out_k = y0_k + amt (shaped_k - y0_k)          (K4 amount; linear, so applied to the slot sums)
//   slot  = sum_k w_k out_k                       (w_k = 1 in the octave slots)
//
// MIX:  wet8 = EXP_OUT_GAIN [ (1 - mix) played + mix (su EXP_GAIN_UP up + sd EXP_GAIN_DOWN down) ]
//       mix = K5 (smoothed, snaps exactly to 0 / 1); (su, sd) = SW2 UP (1, 0) / MID (0.5, 0.5) /
//       DOWN (0, 1), slid linearly over EXP_OCT_XFADE_MS on an SW2 change. No level compensation.
//       A slot with weight exactly 0 is NOT computed: K5 full CCW = played slot only (no octave
//       copies derived, no shaping for them). The played slot is always computed.
//       A slot that starts again after being idle starts from cleared model state.
//   wet = interpolate(wet8) -> DC blocker -> soft clipper -> FS1 mute (48 kHz)
//
// MODELS (L = EXP_FOLD_LIMIT_HZ, Fpd = EXP_PD_FMAX; "cap" = anti-fold, only with EXP_ANTIFOLD)
//  SW1 UP — FM & modulation
//   1 RATIO FM   W = cos(2 pi phi + I_k sin(2 pi pm)),  pm += r wrap(phi - phi_prev) (the modulator runs
//                at r x the band's own phase; for integer r identical to r phi). r = 0.5 * 16^K2,
//                I = 12 K3^2, cap I_k <= (L / f' - 1) / r - 1 (Carson).
//   2 XBAND FM   W = cos(2 pi phi_k + I_k m), m = sin(2 pi phi_{k+n}), n = 1 + 23 K2 continuous (linear
//                interpolation of the two neighbouring partners' sin); partner beyond the top band: 0.
//                cap I_k <= (L - f'_k) / f'_{partner, upper} - 1.
//   3 FB FM      m = sin(2 pi phi + b (m1 + m2) / 2)  (DX7 two-sample average), W = cos(2 pi phi + I_k m);
//                b = 1.5 K2, cap b_k <= L / f' - 1 (armitage FB); I = 12 K3^2, cap I_k <= L / f' - 2.
//   4 RING / AM  ring = cos_k cos_p = 0.5 [Re(u_k u_p) + Re(u_k conj u_p)], u_p = partner unit phasor
//                (n = 1 + 23 K2, interpolated); W = K3 cos_k + ring  (K3 CCW pure product, CW carrier
//                + product). cap: the sum term is dropped where f'_k + f'_p > L.
//   5 FREQ SHIFT W = Re(u_k e^{j 2 pi theta_k}), theta_k += s_k / 8000,
//                s_k = s ((1 - K3) + K3 f'_k / EXP_FS_REF_HZ), s = sign(t) ((801)^|t| - 1) Hz, t = 2 K2 - 1.
//  SW1 MID — phase distortion
//   1 SYNC       ps = frac(R_k phi) (reset at the band's wrap), W = (1 - K3) cos(2 pi ps) + K3 (1 - 2 ps);
//                R = 1 + 7 K2, cap R_k <= Fpd / f'.
//   2 CZ         K2 = bend b (0 = plain sine), K3 4 zones:
//                double sine  T = (1 + b) phi - b [phi >= 0.5]   (b = 1: two sines per cycle), W = cos(2 pi T)
//                reso tri     W = 1 - wb (1 - cos(2 pi R_k phi)), wb = (1 - b) + b tri(phi),
//                reso trap    same, wb = (1 - b) + b min(1, 2 tri(phi)); tri = 1 - |1 - 2 phi|;
//                             R = 1 + 7 b, cap R_k <= Fpd / f'
//                two-seg bend W = cos(2 pi SawWarp(phi, d_k)), d = 0.5 (0.01 / 0.5)^b, d_k = max(d, dfloor_k)
//                             (armitage's knee law and anti-fold knee floor)
//   3 FOLD       W = cos(pi tri_s(frac(F_k phi))), tri_s(x) = x / s (x < s), (1 - x) / (1 - s) else;
//                F = 1 + 7 K2, cap F_k <= Fpd / f'; s = 0.5 (1 - K3) (symmetric -> skewed to a ramp).
//                F = 1, s = 0.5: the plain sine.
//   4 FORMANT    W = 0.5 (1 - cos(2 pi phi / w)) cos(2 pi Fr_k phi) for phi < w, else 0 (one burst per
//                band cycle); Fr = 1 + 15 K2, cap Fr_k <= Fpd / f'; w = K3 (CCW = silence).
//   5 HARMONICS  H = (1 - fr) h_n0 cos(2 pi n0 phi) + fr h_n1 cos(2 pi n1 phi), n = 1 + 15 K2 = n0 + fr,
//                n1 = n0 + 1, h_n = clamp((3800 - n f') / 400, 0, 1) (armitage ADD drop);
//                W = (1 - K3) H + K3 cos(2 pi phi).
//  SW1 DOWN — digital
//   1 WAVETABLE  W = table(set, scan, level_k)(phi), linear interpolation in phase and between
//                adjacent waves (scan = 3 K2); set = K3 zone (analog / PPG / vocal / noisy);
//                level_k = largest band-limited version with H <= 4000 / f'_k (static per band, slot).
//   2 PHASE Q    Q = 64 (2 / 64)^K2 steps, q = floor(Q phi) / Q, W = c0 + K3 f (c1 - c0),
//                c0 = cos(2 pi q), c1 = cos(2 pi (q + 1/Q)), f = Q phi - floor(Q phi).
//   3 WALSH      W = (1 - K3) wal_n(phi) + K3 wal_{n+1}(phi), n = 1..16 (K2 in 16 zones, sequency order,
//                32 segments per cycle).
//   4 CRUSH      Wq = (floor(L cos 2 pi phi) + 0.5) / L, L = 2^(B - 1), B = 12 - 11 K2 (waveform crushed,
//                band level a_k exact); shaped_k = a_k Wq held by a per-slot sample & hold clock at
//                R = 8000 (100 / 8000)^K3 Hz (one clock for all bands of the slot).
//   5 BITTAB     16-step pattern v[0..15] (+-1 / SQR values), one step per 1/16 of the band cycle.
//                K2 first half: s = 32 K2 in [0, 16): crossfade LFSR pattern floor(s) -> the next one
//                (pattern 15 -> SQR n = 1, so K2 noon joins continuously); second half: SQR
//                v[j] = sq(j) cos(2 pi n j / 16), sq = +1 (j < 8) / -1, n = 1 + 15 (2 K2 - 1)
//                (neighbouring n crossfaded; sampled at the step starts, so n > 8 mirrors on the
//                16-step grid). K3: W = v0 + K3 e (v1 - v0), e = 0.5 (1 - cos pi f) (cosine
//                interpolation to the next step, f = position inside the step). Anti-fold: S_k steps
//                (16 / 8 / 4 / 2, (S/2) f' <= 4 kHz), pattern subsampled every 16 / S_k-th step.
//
// MODEL CHANGE (SW1, K1 zone, CZ / wavetable K3 zone, Walsh K2 index): old and new model are both
// computed for EXP_MODEL_XFADE_MS and crossfaded linearly; the entering model starts from cleared
// state. A change during a fade is picked up when that fade ends.
//
// CONTROL MAP
//   K1  model        equal zones: SW1 UP 5, MID 5, DOWN 5 (see MODELS)
//   K2  sweep A      per model (see MODELS)
//   K3  sweep B      per model
//   K4  amount       CCW = unshaped band (armitage's unshaped band sum), CW = model only; linear
//   K5  octave mix   CCW = played only (octave work skipped), CW = selected octave only; linear
//   K6  dry / wet    (shell)
//   SW1 family       UP = FM & modulation, MID = phase distortion, DOWN = digital
//   SW2 octave       UP = +1 oct, MID = +1 and -1 (0.5 / 0.5), DOWN = -1 oct
//   FS1 wet mute     toggle, LED2 on while muted; LED1 on. FS2 unused.
//   DIAG builds: FS1 tap also = numbered "note this setting" line; `auto` line after settling.
//
// K2..K5 are one-pole smoothed per 8 kHz sample (EXP_CTRL_SMOOTH_MS); all derived values are
// computed per 8 kHz sample from the smoothed knobs.
//
// Header-only; main.cpp includes daisy.h / hothouse.h / control_surface.h / module.h first.
// No dynamic allocation. Wavetables (int16, ~112 KB) are computed at Init into SDRAM.

#include <cmath>
#include <cstdint>
#include "module.h"
#include "knob_map.h"          // RemapKnob, Quantize
#include "armitage_bands.h"    // band engine (read-only use)
#include "explorer_constants.h"

#ifndef DTCM_MEM_SECTION
#define DTCM_MEM_SECTION       // host builds
#endif
#ifndef DSY_SDRAM_BSS
#define DSY_SDRAM_BSS          // host builds
#endif

// Shared read-only sin table (filled in Init; DTCM = zero-wait-state reads).
static float DTCM_MEM_SECTION explorer_sin_tab[explorer_k::EXP_SIN_SIZE];

// Band-limited wavetables [set][wave][level][sample], int16 (scale EXP_WT_I16_SCALE), SDRAM.
static int16_t DSY_SDRAM_BSS explorer_wt[explorer_k::EXP_WT_SETS][explorer_k::EXP_WT_WAVES]
                                        [explorer_k::EXP_WT_LEVELS][explorer_k::EXP_WT_SIZE];
// Init-only wavetable build scratch (SDRAM: keeps DTCM free).
struct ExplorerWtScratch {
  float tab[explorer_k::EXP_WT_SIZE], x[explorer_k::EXP_WT_SIZE], acc[explorer_k::EXP_WT_SIZE];
  float A[explorer_k::EXP_WT_HMAX + 1], B[explorer_k::EXP_WT_HMAX + 1];
};
static ExplorerWtScratch DSY_SDRAM_BSS explorer_wt_scratch;

class Explorer : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch() values; also the slot index (kMid = played)
  // Global model ids (family offset + K1 zone).
  enum { kRFM = 0, kXFM, kFBFM, kRING, kFSH,          // SW1 UP
         kSYNC, kCZ, kFOLD, kFORM, kHARM,             // SW1 MID
         kWT, kPQ, kWAL, kCRUSH, kBIT,                // SW1 DOWN
         kNumModels };
  enum { kCzDsin = 0, kCzRtri = 1, kCzRtrap = 2, kCzBend = 3 };   // CZ K3 zones

  void Init(float sr) override {
    using namespace explorer_k;
    sr8_ = sr / (float)kFactor;
    for (int i = 0; i < EXP_SIN_SIZE; i++)
      explorer_sin_tab[i] = sinf(6.283185307f * (float)i / (float)EXP_SIN_SIZE);
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;
    static const float r[3] = { 2.f, 1.f, 0.5f };          // UP / played / DOWN
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      {                                                    // armitage ARM_ALIGN fitted weight (played slot)
        float fc = f;
        if (fc < EXP_ALIGN_W_FLO) fc = EXP_ALIGN_W_FLO;
        if (fc > EXP_ALIGN_W_FHI) fc = EXP_ALIGN_W_FHI;
        const float x = log2f(fc);
        wmid_[k] = powf(10.f, (EXP_ALIGN_WA * x * x + EXP_ALIGN_WB * x + EXP_ALIGN_WC) / 20.f);
      }
      for (int o = 0; o < 3; o++) {
        const float fp = r[o] * f;                         // f'_k
        fp_[o][k] = fp;
        ifp_[o][k] = 1.f / fp;
        pdcap_[o][k] = EXP_PD_FMAX / fp;
        float x = (fp - EXP_CZ_FOLD_HZ) / (EXP_CZ_FOLD_TOP - EXP_CZ_FOLD_HZ);
        x = (x < 0.f) ? 0.f : (x > 1.f) ? 1.f : x;
        czfl_[o][k] = EXP_CZ_KNEE + (0.5f - EXP_CZ_KNEE) * x;
        const int hk = (int)(EXP_WT_FMAX / fp);           // harmonics that fit below the ceiling
        int lv = 0;
        for (int l = 0; l < EXP_WT_LEVELS; l++) if (EXP_WT_LEVEL_H[l] <= hk) lv = l;
        wtlvl_[o][k] = (uint8_t)lv;
        int st = EXP_BT_STEPS;                             // BITTAB steps (anti-fold: (S/2) f' <= limit)
        if (EXP_ANTIFOLD)
          while (st > EXP_BT_STEPS_MIN && 0.5f * (float)st * fp > EXP_FOLD_LIMIT_HZ) st >>= 1;
        btst_[o][k] = (uint8_t)st;
      }
    }
    BuildWalsh();
    BuildBitPatterns();
    BuildTables();
    ln_cz_knee_ = logf(EXP_CZ_KNEE / 0.5f);
    for (int o = 0; o < 3; o++) { ResetSlotState(o); slot_on_[o] = false; }
    dc_a_ = expf(-6.283185307f * EXP_DC_HZ / sr);
    dc_x1_ = dc_y1_ = 0.f;
    ctrl_a_ = 1.f - expf(-1000.f / (EXP_CTRL_SMOOTH_MS * sr8_));
    mfade_len_ = (int)(EXP_MODEL_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (mfade_len_ < 1) mfade_len_ = 1;
    mfade_n_ = 0;
    oct_step_ = 1.f / (EXP_OCT_XFADE_MS * 0.001f * sr8_);
    var_ = var_t_ = vfrom_ = VarKey(kRFM, 0);
    k2s_ = k2_t_ = 0.f;
    k3s_ = k3_t_ = 0.f;
    k4s_ = k4_t_ = 0.f;
    k5s_ = k5_t_ = 0.f;
    su_ = su_t_ = 0.f;
    sd_ = sd_t_ = 1.f;
    mute_ = false;
    mute_g_ = 1.f;
    mute_step_ = 1.f / (EXP_MUTE_SLIDE_MS * 0.001f * sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace explorer_k;
    if (cs.Foot(0).rising) mute_ = !mute_;   // FS1: wet mute toggle
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    int fam = cs.Switch(0);
    if (fam < kUp || fam > kDown) fam = kMid;
    int oct = cs.Switch(1);
    if (oct < kUp || oct > kDown) oct = kMid;
    const int model = (fam == kUp)  ? kRFM  + Quantize(k1, EXP_N_UP)
                    : (fam == kMid) ? kSYNC + Quantize(k1, EXP_N_MID)
                                    : kWT   + Quantize(k1, EXP_N_DOWN);
    const int zone = ZoneOf(model, k2, k3);
    var_t_ = VarKey(model, zone);
    k2_t_ = k2;
    k3_t_ = k3;
    k4_t_ = k4;
    k5_t_ = k5;
    su_t_ = (oct == kUp) ? 1.f : (oct == kMid) ? EXP_OCT_BOTH : 0.f;
    sd_t_ = (oct == kDown) ? 1.f : (oct == kMid) ? EXP_OCT_BOTH : 0.f;

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      const Params p = Derive(model, zone, k2, k3);
      DiagControls(cs, led1, kv, fam, oct, model, zone, p.a, p.b);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;          // tap number (#n), 0 = automatic line
    float k[5];       // K1..K5 (remapped 0..1)
    int   sw1, sw2;   // 0 UP, 1 MID, 2 DOWN
    int   model;      // global model id (kRFM .. kBIT)
    int   zone;       // CZ shape / wavetable set (0..3), Walsh index - 1, else 0
    float a, b;       // sweep A / sweep B as derived values (units per model)
    bool  mute;
  };
  void Activate() override {
    if (NL_DIAG) { diag_changed_ = true; diag_t_ = daisy::System::GetNow(); }
  }
  bool DiagTake(DiagSnap& out) {
    if (diag_tap_pend_)  { out = diag_tap_;  diag_tap_pend_ = false;  return true; }
    if (diag_auto_pend_) { out = diag_auto_; diag_auto_pend_ = false; return true; }
    return false;
  }
  static const char* ModelName(int m) {
    static const char* const n[kNumModels] = { "RATIO-FM", "XBAND-FM", "FB-FM", "RING-AM", "FSHIFT",
                                               "SYNC", "CZ", "FOLD", "FORMANT", "HARM",
                                               "WTABLE", "PHASEQ", "WALSH", "CRUSH", "BITTAB" };
    return (m >= 0 && m < kNumModels) ? n[m] : "?";
  }
  static const char* ZoneName(int m, int z) {
    static const char* const cz[4] = { "dsine", "rtri", "rtrap", "bend" };
    static const char* const wt[4] = { "analog", "ppg", "vocal", "noisy" };
    if (z < 0 || z > 3) return "";
    if (m == kCZ) return cz[z];
    if (m == kWT) return wt[z];
    return "";
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace explorer_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      const float d = dec_(in + i);
      k2s_ += (k2_t_ - k2s_) * ca;
      k3s_ += (k3_t_ - k3s_) * ca;
      k4s_ += (k4_t_ - k4s_) * ca;
      if (fabsf(k4_t_ - k4s_) < EXP_CTRL_SNAP) k4s_ = k4_t_;
      k5s_ += (k5_t_ - k5s_) * ca;
      if (fabsf(k5_t_ - k5s_) < EXP_CTRL_SNAP) k5s_ = k5_t_;   // exact 0: octave work skipped
      su_ = Slide(su_, su_t_, oct_step_);
      sd_ = Slide(sd_, sd_t_, oct_step_);

      // Model / zone change: start a crossfade (picked up when no fade is running).
      if (mfade_n_ == 0 && var_t_ != var_) {
        vfrom_ = var_;
        var_ = var_t_;
        const int mn = var_ >> 4;
        for (int o = 0; o < 3; o++) ResetModelState(mn, o);   // entering model starts cleared
        mfade_n_ = mfade_len_;
      }
      const bool fading = (mfade_n_ > 0);
      pn_ = Derive(var_ >> 4, var_ & 15, k2s_, k3s_);
      if (fading) pf_ = Derive(vfrom_ >> 4, vfrom_ & 15, k2s_, k3s_);
      mg_ = fading ? 1.f - (float)mfade_n_ / (float)(mfade_len_ + 1) : 1.f;

      // Pass 1: clock all bands, magnitudes (|z'| = |z| in every slot).
      float* __restrict mg = mag_ + 1;
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        const float yr = bands_[k].Re();
        const float yi = bands_[k].Im();
        const float n  = yr * yr + yi * yi;
        if (n < EXP_SILENCE) {
          mg[k] = 0.f;
          inv_[k] = 0.f;
        } else {
          const float iv = armbands::ArmFastInvSqrt(n);
          inv_[k] = iv;
          mg[k] = n * iv;
        }
      }

      const float mix = k5s_;
      const float gu = mix * su_, gd = mix * sd_;
      float acc = (1.f - mix) * RunSlot(kMid, fading);   // played slot: always computed
      if (gu != 0.f) acc += gu * EXP_GAIN_UP   * RunSlot(kUp, fading);
      else slot_on_[kUp] = false;
      if (gd != 0.f) acc += gd * EXP_GAIN_DOWN * RunSlot(kDown, fading);
      else slot_on_[kDown] = false;
      if (mfade_n_ > 0) --mfade_n_;
      int_(acc * EXP_OUT_GAIN, wet + i);
    }
    DcBlock(wet, size);
    Saturate(wet, size);
    ApplyMute(wet, size);
  }

 private:
  // Per-sample model parameters (derived from the smoothed K2 / K3).
  struct Params {
    float a = 0.f, b = 0.f;     // sweep A / B as displayed (DIAG)
    float p0 = 0.f, p1 = 0.f, p2 = 0.f, p3 = 0.f;   // model-specific (see Derive)
    int   n0 = 0;               // integer part (partner offset, harmonic, Walsh index)
    int   zone = 0;
    float bt[explorer_k::EXP_BT_STEPS] = {};   // BITTAB: blended step values for this sample
  };

  static inline int VarKey(int model, int zone) { return (model << 4) | (zone & 15); }

  static int ZoneOf(int model, float k2, float k3) {
    using namespace explorer_k;
    if (model == kCZ || model == kWT) return Quantize(k3, 4);
    if (model == kWAL) return Quantize(k2, EXP_WAL_N_MAX);   // index n - 1
    return 0;
  }

  // Derived values per model from K2 / K3 (0..1).
  Params Derive(int m, int zone, float k2, float k3) const {
    using namespace explorer_k;
    Params p;
    p.zone = zone;
    switch (m) {
      case kRFM: {                        // p0 = r, p1 = 1/r, p2 = I
        const float r = EXP_RFM_RATIO_MIN * expf(k2 * logf(EXP_RFM_RATIO_MAX / EXP_RFM_RATIO_MIN));
        p.p0 = r; p.p1 = 1.f / r; p.p2 = EXP_FM_INDEX_MAX * k3 * k3;
        p.a = r; p.b = p.p2; break;
      }
      case kXFM: case kRING: {            // n0 + p0 = partner offset; XFM p1 = I; RING p1 = carrier share
        const float n = EXP_XB_N_MIN + (EXP_XB_N_MAX - EXP_XB_N_MIN) * k2;
        p.n0 = (int)n; p.p0 = n - (float)p.n0;
        p.p1 = (m == kXFM) ? EXP_FM_INDEX_MAX * k3 * k3 : k3;
        p.a = n; p.b = p.p1; break;
      }
      case kFBFM:                          // p0 = self-feedback b, p1 = I
        p.p0 = EXP_FBFM_FB_MAX * k2; p.p1 = EXP_FM_INDEX_MAX * k3 * k3;
        p.a = p.p0; p.b = p.p1; break;
      case kFSH: {                         // p0 = fixed increment / sample, p1 = proportional increment per Hz of f'
        const float t = 2.f * k2 - 1.f;
        const float at = fabsf(t);
        float s = EXP_FS_CURVE_HZ * (expf(at * logf(EXP_FS_MAX_HZ / EXP_FS_CURVE_HZ + 1.f)) - 1.f);
        if (t < 0.f) s = -s;
        if (t == 0.f) s = 0.f;
        p.p0 = s * (1.f - k3) / sr8_;
        p.p1 = s * k3 / (EXP_FS_REF_HZ * sr8_);
        p.a = s; p.b = k3; break;
      }
      case kSYNC:                          // p0 = R, p1 = saw share
        p.p0 = 1.f + (EXP_SYNC_R_MAX - 1.f) * k2; p.p1 = k3;
        p.a = p.p0; p.b = k3; break;
      case kCZ:                            // p0 = bend b, p1 = knee d (bend zone), p2 = R (reso zones)
        p.p0 = k2;
        p.p1 = (k2 >= 1.f) ? EXP_CZ_KNEE : 0.5f * expf(k2 * ln_cz_knee_);
        p.p2 = 1.f + (EXP_CZ_RES_MAX - 1.f) * k2;
        p.a = k2; p.b = k3; break;
      case kFOLD: {                        // p0 = F, p1 = breakpoint s, p2 = 1/s, p3 = 1/(1-s)
        const float s = 0.5f * (1.f - k3);
        p.p0 = 1.f + (EXP_FOLD_MAX - 1.f) * k2;
        p.p1 = s; p.p2 = (s > 0.f) ? 1.f / s : 0.f; p.p3 = 1.f / (1.f - s);
        p.a = p.p0; p.b = s; break;
      }
      case kFORM:                          // p0 = Fr, p1 = width w, p2 = 1/w
        p.p0 = 1.f + (EXP_FORM_MAX - 1.f) * k2;
        p.p1 = k3; p.p2 = (k3 > 0.f) ? 1.f / k3 : 0.f;
        p.a = p.p0; p.b = k3; break;
      case kHARM: {                        // n0 + p0 = harmonic n, p1 = fundamental share
        const float n = 1.f + (EXP_HARM_MAX - 1.f) * k2;
        p.n0 = (int)n; p.p0 = n - (float)p.n0; p.p1 = k3;
        p.a = n; p.b = k3; break;
      }
      case kWT: {                          // n0 + p0 = scan position over the set's waves
        const float pos = (float)(EXP_WT_WAVES - 1) * k2;
        p.n0 = (int)pos;
        if (p.n0 > EXP_WT_WAVES - 2) p.n0 = EXP_WT_WAVES - 2;
        p.p0 = pos - (float)p.n0;
        p.a = pos; p.b = k3; break;
      }
      case kPQ: {                          // p0 = Q, p1 = 1/Q, p2 = interpolated share
        const float q = EXP_PQ_STEPS_MAX * expf(k2 * logf(EXP_PQ_STEPS_MIN / EXP_PQ_STEPS_MAX));
        p.p0 = q; p.p1 = 1.f / q; p.p2 = k3;
        p.a = q; p.b = k3; break;
      }
      case kWAL:                           // n0 = Walsh index (1..16), p0 = blend with n0 + 1
        p.n0 = zone + 1; p.p0 = k3;
        p.a = (float)p.n0; p.b = k3; break;
      case kBIT: {                         // bt[] = blended pattern, p0 = smooth share (K3)
        // a = scroll display: 1..17 = LFSR pattern position (17 = SQR n = 1), 17..32 = SQR n + 16
        float va[EXP_BT_STEPS], vb[EXP_BT_STEPS];
        float fr;
        if (k2 < 0.5f) {
          const float sp = 2.f * (float)EXP_BT_PATTERNS * k2;   // [0, 16)
          int i = (int)sp;
          if (i > EXP_BT_PATTERNS - 1) i = EXP_BT_PATTERNS - 1;
          fr = sp - (float)i;
          for (int j = 0; j < EXP_BT_STEPS; j++) va[j] = bt_pat_[i][j];
          if (i < EXP_BT_PATTERNS - 1) for (int j = 0; j < EXP_BT_STEPS; j++) vb[j] = bt_pat_[i + 1][j];
          else                         SqrPattern(1, vb);
          p.a = 1.f + sp;
        } else {
          const float n = 1.f + (EXP_BT_SQR_N_MAX - 1.f) * (2.f * k2 - 1.f);
          int n0 = (int)n;
          if (n0 > (int)EXP_BT_SQR_N_MAX - 1) n0 = (int)EXP_BT_SQR_N_MAX - 1;
          fr = n - (float)n0;
          SqrPattern(n0, va);
          SqrPattern(n0 + 1, vb);
          p.a = 16.f + n;
        }
        for (int j = 0; j < EXP_BT_STEPS; j++) p.bt[j] = va[j] + fr * (vb[j] - va[j]);
        p.p0 = k3;
        p.b = k3; break;
      }
      default: {                           // kCRUSH: p0 = L = 2^(B-1), p1 = 1/L, p2 = S&H clock increment
        const float bits = EXP_CRUSH_BITS_MAX + (EXP_CRUSH_BITS_MIN - EXP_CRUSH_BITS_MAX) * k2;
        const float L = exp2f(bits - 1.f);
        const float rate = EXP_CRUSH_RATE_MAX * expf(k3 * logf(EXP_CRUSH_RATE_MIN / EXP_CRUSH_RATE_MAX));
        p.p0 = L; p.p1 = 1.f / L; p.p2 = rate / sr8_;
        p.a = bits; p.b = rate; break;
      }
    }
    return p;
  }

  // One 8 kHz sample of slot o: derive z'_k / u_k, sum the unshaped band and the model output,
  // return y0 + amt (shaped - y0).
  float RunSlot(int o, bool fading) {
    if (!slot_on_[o]) { ResetSlotState(o); slot_on_[o] = true; }
    const float y0 = Prep(o);
    float sh = Shape(var_ >> 4, o, pn_);
    if (fading) sh = (1.f - mg_) * Shape(vfrom_ >> 4, o, pf_) + mg_ * sh;
    return y0 + k4s_ * (sh - y0);
  }

  // u_k = z'_k / |z'_k| for slot o into ur_ / ui_ (0 for silent bands); returns sum w_k Re(z'_k).
  float Prep(int o) {
    const float* __restrict mg = mag_ + 1;
    float y0 = 0.f;
    for (int k = 0; k < kBands; k++) {
      if (mg[k] == 0.f) { ur_[k] = ui_[k] = 0.f; continue; }
      const float iv = inv_[k];
      float zr, zi;
      if (o == kMid) {
        zr = bands_[k].Re(); zi = bands_[k].Im();
        if (k & 1) { zr = -zr; zi = -zi; }        // phase alignment (armitage ARM_ALIGN)
        y0 += wmid_[k] * zr;
      } else {
        if (o == kUp) bands_[k].ArmUp1(iv, zr, zi);
        else          bands_[k].ArmDown1(iv, zr, zi);
        y0 += zr;
      }
      ur_[k] = zr * iv;
      ui_[k] = zi * iv;
    }
    return y0;
  }

  float Shape(int m, int o, const Params& p) {
    switch (m) {
      case kRFM:  return ShapeK<kRFM>(o, p);
      case kXFM:  return ShapeK<kXFM>(o, p);
      case kFBFM: return ShapeK<kFBFM>(o, p);
      case kRING: return ShapeK<kRING>(o, p);
      case kFSH:  return ShapeK<kFSH>(o, p);
      case kSYNC: return ShapeK<kSYNC>(o, p);
      case kCZ:   return ShapeK<kCZ>(o, p);
      case kFOLD: return ShapeK<kFOLD>(o, p);
      case kFORM: return ShapeK<kFORM>(o, p);
      case kHARM: return ShapeK<kHARM>(o, p);
      case kWT:   return ShapeK<kWT>(o, p);
      case kPQ:   return ShapeK<kPQ>(o, p);
      case kWAL:  return ShapeK<kWAL>(o, p);
      case kBIT:  return ShapeK<kBIT>(o, p);
      default:    return ShapeK<kCRUSH>(o, p);
    }
  }

  // sum_k w_k a_k W_k of model M in slot o.
  template <int M>
  float ShapeK(int o, const Params& p) {
    using namespace explorer_k;
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict ur  = ur_;
    const float* __restrict ui  = ui_;
    const float* __restrict fp  = fp_[o];
    const float* __restrict ifp = ifp_[o];
    const float* __restrict pdc = pdcap_[o];
    const bool played = (o == kMid);
    bool tick = false;
    if (M == kCRUSH) {                           // one S&H clock per slot
      float c = cr_acc_[o] + p.p2;
      if (c >= 1.f) { c -= 1.f; tick = true; }
      cr_acc_[o] = c;
    }
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) {                            // silent band: state cleared
        if (M == kRFM)  pp_[o][k] = -1.f;
        if (M == kFBFM) { m1_[o][k] = 0.f; m2_[o][k] = 0.f; }
        if (M == kCRUSH) {
          if (tick) hold_[o][k] = 0.f;
          acc += (played ? wmid_[k] : 1.f) * hold_[o][k];
        }
        continue;
      }
      const float cr = ur[k], ci = ui[k];
      float W;
      if (M == kRFM) {
        const float ph = FastAtan2Cycles(ci, cr);
        float pm = pm_[o][k];
        const float pp = pp_[o][k];
        if (pp < 0.f) {
          pm = p.p0 * ph;
        } else {
          float dph = ph - pp;
          if (dph < -0.5f) dph += 1.f;
          else if (dph >= 0.5f) dph -= 1.f;
          pm += p.p0 * dph;
        }
        pm -= floorf(pm);
        pm_[o][k] = pm;
        pp_[o][k] = ph;
        float I = p.p2;
        if (EXP_ANTIFOLD) {
          float cap = (EXP_FOLD_LIMIT_HZ * ifp[k] - 1.f) * p.p1 - 1.f;
          if (cap < 0.f) cap = 0.f;
          if (I > cap) I = cap;
        }
        float s, c;
        SinCos(I * Sin01(pm), s, c);
        W = cr * c - ci * s;
      } else if (M == kXFM) {
        const int q0 = k + p.n0, q1 = q0 + 1;
        const float s0 = (q0 < kBands) ? ui[q0] : 0.f;
        const float s1 = (q1 < kBands) ? ui[q1] : 0.f;
        const float mod = s0 + p.p0 * (s1 - s0);
        float I = p.p1;
        if (EXP_ANTIFOLD && q0 < kBands) {
          const int qc = (q1 < kBands) ? q1 : q0;
          float cap = (EXP_FOLD_LIMIT_HZ - fp[k]) * ifp[qc] - 1.f;
          if (cap < 0.f) cap = 0.f;
          if (I > cap) I = cap;
        }
        float s, c;
        SinCos(I * mod, s, c);
        W = cr * c - ci * s;
      } else if (M == kFBFM) {
        float b = p.p0, I = p.p1;
        if (EXP_ANTIFOLD) {
          float bc = EXP_FOLD_LIMIT_HZ * ifp[k] - 1.f;
          if (bc < 0.f) bc = 0.f;
          if (b > bc) b = bc;
          float ic = EXP_FOLD_LIMIT_HZ * ifp[k] - 2.f;
          if (ic < 0.f) ic = 0.f;
          if (I > ic) I = ic;
        }
        float s, c;
        SinCos(b * 0.5f * (m1_[o][k] + m2_[o][k]), s, c);
        float mod = ci * c + cr * s;             // sin(2 pi phi + b avg)
        if (fabsf(mod) < EXP_DENORM) mod = 0.f;
        m2_[o][k] = m1_[o][k];
        m1_[o][k] = mod;
        SinCos(I * mod, s, c);
        W = cr * c - ci * s;
      } else if (M == kRING) {
        const int q0 = k + p.n0, q1 = q0 + 1;
        const float r0 = (q0 < kBands) ? ur[q0] : 0.f, i0 = (q0 < kBands) ? ui[q0] : 0.f;
        const float r1 = (q1 < kBands) ? ur[q1] : 0.f, i1 = (q1 < kBands) ? ui[q1] : 0.f;
        const float pr = r0 + p.p0 * (r1 - r0), pi = i0 + p.p0 * (i1 - i0);
        float sum = cr * pr - ci * pi;           // Re(u_k u_p): f'_k + f'_p
        const float dif = cr * pr + ci * pi;     // Re(u_k conj u_p): f'_k - f'_p
        if (EXP_ANTIFOLD && q0 < kBands) {
          const int qc = (q1 < kBands) ? q1 : q0;
          if (fp[k] + fp[qc] > EXP_FOLD_LIMIT_HZ) sum = 0.f;
        }
        W = p.p1 * cr + 0.5f * (sum + dif);
      } else if (M == kFSH) {
        float th = th_[o][k] + p.p0 + p.p1 * fp[k];
        th -= floorf(th);
        th_[o][k] = th;
        float c, s;
        CosSin01(th, c, s);
        W = cr * c - ci * s;
      } else if (M == kSYNC) {
        const float ph = FastAtan2Cycles(ci, cr);
        float R = p.p0;
        if (EXP_ANTIFOLD && R > pdc[k]) R = pdc[k];
        float ps = R * ph;
        ps -= (float)(int)ps;
        W = Cos01(ps) + p.p1 * ((1.f - 2.f * ps) - Cos01(ps));
      } else if (M == kCZ) {
        const float ph = FastAtan2Cycles(ci, cr);
        const float b = p.p0;
        if (p.zone == kCzBend) {
          float dk = p.p1;
          if (EXP_ANTIFOLD && dk < czfl_[o][k]) dk = czfl_[o][k];
          W = (dk >= 0.5f) ? cr : Cos01(SawWarp(ph, dk));
        } else if (p.zone == kCzDsin) {
          const float T = (1.f + b) * ph - ((ph >= 0.5f) ? b : 0.f);
          W = Cos01(T);
        } else {
          float R = p.p2;
          if (EXP_ANTIFOLD && R > pdc[k]) R = pdc[k];
          float win = 1.f - fabsf(1.f - 2.f * ph);              // triangle window
          if (p.zone == kCzRtrap) { win *= 2.f; if (win > 1.f) win = 1.f; }   // trapezoid
          const float wb = (1.f - b) + b * win;
          W = 1.f - wb * (1.f - Cos01(R * ph));
        }
      } else if (M == kFOLD) {
        const float ph = FastAtan2Cycles(ci, cr);
        float F = p.p0;
        if (EXP_ANTIFOLD && F > pdc[k]) F = pdc[k];
        float x = F * ph;
        x -= (float)(int)x;
        const float t = (x < p.p1) ? x * p.p2 : (1.f - x) * p.p3;
        W = Cos01(0.5f * t);                     // cos(pi t)
      } else if (M == kFORM) {
        const float ph = FastAtan2Cycles(ci, cr);
        if (ph < p.p1) {
          float Fr = p.p0;
          if (EXP_ANTIFOLD && Fr > pdc[k]) Fr = pdc[k];
          W = 0.5f * (1.f - Cos01(ph * p.p2)) * Cos01(Fr * ph);
        } else {
          W = 0.f;
        }
      } else if (M == kHARM) {
        const float ph = FastAtan2Cycles(ci, cr);
        const int n0 = p.n0, n1 = n0 + 1;
        float h0 = 1.f, h1 = 1.f;
        if (EXP_ANTIFOLD) {
          constexpr float ie = 1.f / EXP_HARM_FEDGE;
          h0 = (EXP_HARM_FMAX - (float)n0 * fp[k]) * ie;
          h0 = (h0 < 0.f) ? 0.f : (h0 > 1.f) ? 1.f : h0;
          h1 = (EXP_HARM_FMAX - (float)n1 * fp[k]) * ie;
          h1 = (h1 < 0.f) ? 0.f : (h1 > 1.f) ? 1.f : h1;
        }
        const float H = (1.f - p.p0) * h0 * Cos01((float)n0 * ph) + p.p0 * h1 * Cos01((float)n1 * ph);
        W = H + p.p1 * (cr - H);
      } else if (M == kWT) {
        const float ph = FastAtan2Cycles(ci, cr);
        const int lv = wtlvl_[o][k];
        const int16_t* t0 = explorer_wt[p.zone][p.n0][lv];
        const int16_t* t1 = explorer_wt[p.zone][p.n0 + 1][lv];
        const float xi = ph * (float)EXP_WT_SIZE;
        const int   i0 = (int)xi;
        const float fr = xi - (float)i0;
        const int   j0 = i0 & (EXP_WT_SIZE - 1), j1 = (i0 + 1) & (EXP_WT_SIZE - 1);
        const float v0 = (float)t0[j0] + fr * (float)(t0[j1] - t0[j0]);
        const float v1 = (float)t1[j0] + fr * (float)(t1[j1] - t1[j0]);
        W = (v0 + p.p0 * (v1 - v0)) * (1.f / EXP_WT_I16_SCALE);
      } else if (M == kPQ) {
        const float ph = FastAtan2Cycles(ci, cr);
        const float xq = ph * p.p0;
        const float q = floorf(xq);
        const float f = xq - q;
        const float c0 = Cos01(q * p.p1);
        const float c1 = Cos01((q + 1.f) * p.p1);
        W = c0 + p.p2 * f * (c1 - c0);
      } else if (M == kWAL) {
        const float ph = FastAtan2Cycles(ci, cr);
        const int j = (int)(ph * 32.f) & 31;
        const float w0 = wal_[p.n0][j], w1 = wal_[p.n0 + 1][j];
        W = w0 + p.p0 * (w1 - w0);
      } else if (M == kBIT) {
        const float ph = FastAtan2Cycles(ci, cr);
        const int S = btst_[o][k];
        const int stride = EXP_BT_STEPS / S;
        const float xs = ph * (float)S;
        int j = (int)xs;
        if (j > S - 1) j = S - 1;
        const float f = xs - (float)j;
        const float v0 = p.bt[j * stride];
        const float v1 = p.bt[((j + 1 == S) ? 0 : j + 1) * stride];
        const float e = 0.5f * (1.f - Cos01(0.5f * f));   // cosine interpolation weight
        W = v0 + p.p0 * e * (v1 - v0);
      } else {                                   // kCRUSH
        if (tick) {
          float q = floorf(cr * p.p0);
          hold_[o][k] = a * (q + 0.5f) * p.p1;
        }
        acc += (played ? wmid_[k] : 1.f) * hold_[o][k];
        continue;
      }
      acc += (played ? wmid_[k] : 1.f) * a * W;
    }
    return acc;
  }

  // Clear one model's state in slot o (on model entry).
  void ResetModelState(int m, int o) {
    for (int k = 0; k < kBands; k++) {
      if (m == kRFM)   { pm_[o][k] = 0.f; pp_[o][k] = -1.f; }
      if (m == kFBFM)  { m1_[o][k] = m2_[o][k] = 0.f; }
      if (m == kFSH)   th_[o][k] = 0.f;
      if (m == kCRUSH) hold_[o][k] = 0.f;
    }
    if (m == kCRUSH) cr_acc_[o] = 1.f;            // first sample ticks
  }
  void ResetSlotState(int o) {
    ResetModelState(kRFM, o);
    ResetModelState(kFBFM, o);
    ResetModelState(kFSH, o);
    ResetModelState(kCRUSH, o);
  }

  static inline float Slide(float v, float t, float st) {
    if (v < t) { v += st; if (v > t) v = t; }
    else if (v > t) { v -= st; if (v < t) v = t; }
    return v;
  }

  // BITTAB LFSR patterns: 16-bit Fibonacci LFSR per tap set, fixed seed, first 16 output bits.
  void BuildBitPatterns() {
    using namespace explorer_k;
    for (int t = 0; t < EXP_BT_PATTERNS; t++) {
      uint16_t st = EXP_BT_SEED;
      for (int j = 0; j < EXP_BT_STEPS; j++) {
        const unsigned bit = (unsigned)__builtin_popcount((unsigned)(st & EXP_BT_TAPS[t])) & 1u;
        st = (uint16_t)((st >> 1) | (bit << 15));
        bt_pat_[t][j] = bit ? 1.f : -1.f;
      }
    }
  }
  // BITTAB SQR pattern: square (one cycle per band cycle) x harmonic n, sampled at the step starts.
  static void SqrPattern(int n, float* v) {
    using namespace explorer_k;
    for (int j = 0; j < EXP_BT_STEPS; j++) {
      const float sq = (j < EXP_BT_STEPS / 2) ? 1.f : -1.f;
      v[j] = sq * Cos01((float)(n * j) * (1.f / (float)EXP_BT_STEPS));
    }
  }

  // Walsh functions in sequency order, 32 segments: rows of the 32x32 Hadamard matrix sorted by
  // their number of sign changes. wal_[s] for s = 0..EXP_WAL_N_MAX + 1.
  void BuildWalsh() {
    using namespace explorer_k;
    for (int row = 0; row < 32; row++) {
      float v[32];
      int changes = 0;
      for (int j = 0; j < 32; j++) {
        v[j] = (__builtin_popcount((unsigned)(row & j)) & 1) ? -1.f : 1.f;
        if (j > 0 && v[j] != v[j - 1]) changes++;
      }
      if (changes <= EXP_WAL_N_MAX + 1)
        for (int j = 0; j < 32; j++) wal_[changes][j] = v[j];
    }
  }

  // Wavetables: each wave is defined in the time domain (EXP_WT_SIZE samples, mean removed,
  // peak normalised to 1), analysed into harmonics 1..EXP_WT_HMAX (DFT), then rebuilt per level
  // from harmonics 1..EXP_WT_LEVEL_H[l] (band-limited versions), stored as int16.
  void BuildTables() {
    using namespace explorer_k;
    constexpr int N = EXP_WT_SIZE;
    // Init-only scratch (SDRAM, see explorer_wt_scratch).
    float* tab = explorer_wt_scratch.tab;        // sin(2 pi i / N), exact index arithmetic
    float* x   = explorer_wt_scratch.x;          // time-domain wave
    float* acc = explorer_wt_scratch.acc;        // level accumulator
    float* A   = explorer_wt_scratch.A;          // harmonic coefficients (sin)
    float* B   = explorer_wt_scratch.B;          // harmonic coefficients (cos)
    for (int i = 0; i < N; i++) tab[i] = sinf(6.283185307f * (float)i / (float)N);
    auto sn = [&](int i) { return tab[i & (N - 1)]; };
    auto cs = [&](int i) { return tab[(i + N / 4) & (N - 1)]; };
    uint32_t seed = EXP_WT_SEED;
    auto rnd = [&]() {                           // uniform in [-1, 1)
      seed = seed * 1664525u + 1013904223u;
      return (float)(seed >> 8) * (2.f / 16777216.f) - 1.f;
    };
    for (int s = 0; s < EXP_WT_SETS; s++) {
      for (int w = 0; w < EXP_WT_WAVES; w++) {
        // 1) time-domain wave
        if (s == 0) {                            // analog: sine, triangle, square, saw (series to HMAX)
          for (int i = 0; i < N; i++) {
            float v = 0.f;
            for (int h = 1; h <= EXP_WT_HMAX; h++) {
              const float sh = sn(h * i);
              if (w == 0)      { if (h == 1) v += sh; }
              else if (w == 1) { if (h & 1) v += ((((h - 1) / 2) & 1) ? -1.f : 1.f) * sh / (float)(h * h); }
              else if (w == 2) { if (h & 1) v += sh / (float)h; }
              else             { v += sh / (float)h; }
            }
            x[i] = v;
          }
        } else if (s == 1) {                     // PPG-style: stepped / low-resolution digital waves
          for (int i = 0; i < N; i++) {
            const float ph = (float)i / (float)N;
            float v;
            if (w == 0) {                        // sine held in 16 steps per cycle
              const int st = i / (N / 16);
              v = sn(st * (N / 16));
            } else if (w == 1) {                 // saw: 8 steps, 8 levels
              const int st = i / (N / 8);
              v = 1.f - 2.f * (float)st / 7.f;
            } else if (w == 2) {                 // odd-harmonic stack, 32 steps, 16 amplitude levels
              const int st = i / (N / 32);
              const int ii = st * (N / 32);
              const float y = sn(ii) + 0.5f * sn(3 * ii) + 0.33f * sn(5 * ii);
              v = roundf(y * 7.5f) / 7.5f;
            } else {                             // bit pattern: ((s * 5) ^ (s >> 2)) & 15 over 32 steps
              const int st = (int)(ph * 32.f);
              v = (float)(((st * 5) ^ (st >> 2)) & 15) / 7.5f - 1.f;
            }
            x[i] = v;
          }
        } else if (s == 2) {                     // vocal: formant-weighted harmonics (vowels a, e, i, o)
          for (int h = 1; h <= EXP_WT_HMAX; h++) {
            const float fh = (float)h * EXP_WT_VOX_F0;
            float amp = 0.f;
            for (int f = 0; f < 3; f++) {
              const float u = (fh - EXP_WT_VOX_F[w][f]) / (0.5f * EXP_WT_VOX_BW[f]);
              amp += EXP_WT_VOX_A[f] / (1.f + u * u);
            }
            A[h] = amp;
          }
          for (int i = 0; i < N; i++) {
            float v = 0.f;
            for (int h = 1; h <= EXP_WT_HMAX; h++) v += A[h] * sn(h * i);
            x[i] = v;
          }
        } else {                                 // noisy: random phases, flatter and rougher across the set
          if (w < 3) {
            for (int h = 1; h <= EXP_WT_HMAX; h++) {
              const float tilt = (w == 0) ? 1.f / (float)h : (w == 1) ? 1.f / sqrtf((float)h) : 1.f;
              const float am = (w == 0) ? tilt : tilt * (0.5f + 0.5f * fabsf(rnd()));
              const float ph = 0.5f * (rnd() + 1.f);
              A[h] = am * cosf(6.283185307f * ph);
              B[h] = am * sinf(6.283185307f * ph);
            }
            for (int i = 0; i < N; i++) {
              float v = 0.f;
              for (int h = 1; h <= EXP_WT_HMAX; h++) v += A[h] * sn(h * i) + B[h] * cs(h * i);
              x[i] = v;
            }
          } else {                               // single cycle of white noise
            for (int i = 0; i < N; i++) x[i] = rnd();
          }
        }
        // 2) mean removed, peak normalised to 1
        float mean = 0.f;
        for (int i = 0; i < N; i++) mean += x[i];
        mean /= (float)N;
        float pk = 0.f;
        for (int i = 0; i < N; i++) { x[i] -= mean; if (fabsf(x[i]) > pk) pk = fabsf(x[i]); }
        const float g = (pk > 0.f) ? 1.f / pk : 0.f;
        for (int i = 0; i < N; i++) x[i] *= g;
        // 3) DFT: x = sum_h A_h sin + B_h cos
        for (int h = 1; h <= EXP_WT_HMAX; h++) {
          float a = 0.f, b = 0.f;
          for (int i = 0; i < N; i++) { a += x[i] * sn(h * i); b += x[i] * cs(h * i); }
          A[h] = a * (2.f / (float)N);
          B[h] = b * (2.f / (float)N);
        }
        // 4) band-limited levels (cumulative)
        for (int i = 0; i < N; i++) acc[i] = 0.f;
        int hdone = 0;
        for (int l = 0; l < EXP_WT_LEVELS; l++) {
          const int ht = EXP_WT_LEVEL_H[l];
          for (int h = hdone + 1; h <= ht; h++)
            for (int i = 0; i < N; i++) acc[i] += A[h] * sn(h * i) + B[h] * cs(h * i);
          hdone = ht;
          int16_t* dst = explorer_wt[s][w][l];
          for (int i = 0; i < N; i++) {
            float v = acc[i] * EXP_WT_I16_SCALE;
            v = (v > 32767.f) ? 32767.f : (v < -32768.f) ? -32768.f : v;   // int16 range (storage)
            dst[i] = (int16_t)lrintf(v);
          }
        }
      }
    }
  }

  // Wet DC blocker (copied from armitage).
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

  // Wet soft clipper (copied from armitage): EXP_SAT_TRIM * EXP_SAT_T * tanh(x / EXP_SAT_T).
  void Saturate(float* wet, size_t size) {
    using namespace explorer_k;
    constexpr float kIn  = 1.f / EXP_SAT_T;
    constexpr float kOut = EXP_SAT_T * EXP_SAT_TRIM;
    for (size_t n = 0; n < size; n++) wet[n] = kOut * PadeTanh(wet[n] * kIn);
  }
  static inline float PadeTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }

  // FS1 wet mute (copied from armitage).
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

  void DiagControls(const ControlSurface& cs, daisy::Led& led1, const float* kv,
                    int sw1, int oct, int model, int zone, float a, float b) {
    using namespace explorer_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.sw1 = sw1; s.sw2 = oct;
    s.model = model; s.zone = zone;
    s.a = a; s.b = b;
    s.mute = mute_;
    if (cs.Foot(0).rising) {
      s.n = ++diag_n_;
      diag_tap_ = s;
      diag_tap_pend_ = true;
      diag_blink_t_ = now;
      diag_blink_ = true;
    }
    if (diag_blink_) {
      if (now - diag_blink_t_ < EXP_DIAG_BLINK_MS) led1.Set(0.f);
      else diag_blink_ = false;
    }
    bool moved = (sw1 != diag_ref_sw1_) || (oct != diag_ref_sw2_);
    for (int i = 0; i < 5; i++) if (fabsf(kv[i] - diag_ref_k_[i]) > EXP_DIAG_KNOB_EPS) moved = true;
    if (moved) {
      for (int i = 0; i < 5; i++) diag_ref_k_[i] = kv[i];
      diag_ref_sw1_ = sw1; diag_ref_sw2_ = oct;
      diag_changed_ = true;
      diag_t_ = now;
    } else if (diag_changed_ && now - diag_t_ >= EXP_DIAG_SETTLE_MS) {
      diag_changed_ = false;
      s.n = 0;
      diag_auto_ = s;
      diag_auto_pend_ = true;
    }
  }

  // --- fast trig (copied from armitage, own table) ---
  static inline void SinCos(float theta, float& s, float& c) {
    using namespace explorer_k;
    static_assert((EXP_SIN_SIZE & (EXP_SIN_SIZE - 1)) == 0, "power of two");
    static_assert(EXP_FM_INDEX_MAX + EXP_FBFM_FB_MAX < 90.f, "SinCos offset covers |theta| < ~100 rad");
    constexpr int   kMask = EXP_SIN_SIZE - 1;
    constexpr float kScale = (float)EXP_SIN_SIZE / 6.283185307f;
    const float x  = theta * kScale + 16384.f;
    const int   ix = (int)x;
    const float fr = x - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[(i0 + 1) & kMask];
    const float c0 = explorer_sin_tab[q],  c1 = explorer_sin_tab[(q + 1) & kMask];
    s = s0 + fr * (s1 - s0);
    c = c0 + fr * (c1 - c0);
  }
  // cos(2 pi x), x >= 0.
  static inline float Cos01(float x) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float c0 = explorer_sin_tab[q], c1 = explorer_sin_tab[(q + 1) & kMask];
    return c0 + fr * (c1 - c0);
  }
  // sin(2 pi x), x >= 0.
  static inline float Sin01(float x) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[(i0 + 1) & kMask];
    return s0 + fr * (s1 - s0);
  }
  // cos and sin of 2 pi x, x >= 0.
  static inline void CosSin01(float x, float& c, float& s) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float c0 = explorer_sin_tab[q],  c1 = explorer_sin_tab[(q + 1) & kMask];
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[(i0 + 1) & kMask];
    c = c0 + fr * (c1 - c0);
    s = s0 + fr * (s1 - s0);
  }
  // CZ knee warp (armitage SawWarp).
  static inline float SawWarp(float ph, float dk) {
    return (ph < dk) ? ph * (0.5f / dk) : 0.5f + (ph - dk) * (0.5f / (1.f - dk));
  }
  // atan2(y, x) / 2 pi in [0, 1) (armitage FastAtan2Cycles, max error ~0.005 rad).
  static inline float FastAtan2Cycles(float y, float x) {
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = (ax > ay) ? ax : ay;
    const float mn = (ax > ay) ? ay : ax;
    if (mx <= 0.f) return 0.f;
    const float a  = mn / mx;
    constexpr float k1 = 0.97239411f / 6.283185307f;
    constexpr float k3 = 0.19194795f / 6.283185307f;
    float r = a * (k1 - k3 * a * a);
    if (ay > ax) r = 0.25f - r;
    if (x < 0.f) r = 0.5f - r;
    if (y < 0.f) r = 1.f - r;
    return (r >= 1.f) ? 0.f : r;
  }

  armbands::ArmBand         bands_[kBands];
  armbands::ArmDecimator    dec_;
  armbands::ArmInterpolator int_;

  float mag_[kBands + 2];          // |z_k| at [k + 1] (0 = silent); guards at both ends
  float inv_[kBands];              // 1 / |z_k| (0 = silent)
  float ur_[kBands], ui_[kBands];  // unit phasor of the slot being run
  float wmid_[kBands];             // played-slot fitted output weight (armitage ARM_ALIGN)
  float fp_[3][kBands];            // f'_k per slot (UP / played / DOWN)
  float ifp_[3][kBands];           // 1 / f'_k
  float pdcap_[3][kBands];         // EXP_PD_FMAX / f'_k (ratio caps)
  float czfl_[3][kBands];          // CZ bend knee floor per band
  uint8_t wtlvl_[3][kBands];       // wavetable band-limit level per band
  uint8_t btst_[3][kBands];        // BITTAB steps per band (16 / 8 / 4 / 2)
  float bt_pat_[explorer_k::EXP_BT_PATTERNS][explorer_k::EXP_BT_STEPS] = {};   // BITTAB LFSR patterns (+-1)
  float wal_[explorer_k::EXP_WAL_N_MAX + 2][32] = {};   // Walsh functions, sequency 0..N_MAX + 1

  // Model state per slot and band
  float pm_[3][kBands], pp_[3][kBands];   // ratio FM: modulator phase, previous band phase (-1 = fresh)
  float m1_[3][kBands], m2_[3][kBands];   // feedback FM: last two modulator outputs
  float th_[3][kBands];                   // frequency shift: rotation phase (cycles)
  float hold_[3][kBands];                 // crush: held band output
  float cr_acc_[3] = { 1.f, 1.f, 1.f };   // crush: S&H clock per slot
  bool  slot_on_[3] = { false, false, false };   // slot computed last sample

  Params pn_, pf_;                 // current / fading-out model parameters (this sample)
  float ln_cz_knee_ = 0.f;
  float dc_a_ = 0.f, dc_x1_ = 0.f, dc_y1_ = 0.f;
  float sr8_ = 8000.f;
  float ctrl_a_ = 0.f;
  int   var_ = 0, vfrom_ = 0;      // model variant (model << 4 | zone): current / fading out
  volatile int var_t_ = 0;         // target variant (Controls)
  int   mfade_n_ = 0, mfade_len_ = 8;
  float mg_ = 1.f;                 // new-model gain during a fade
  float k2s_ = 0.f, k3s_ = 0.f, k4s_ = 0.f, k5s_ = 0.f;            // smoothed knobs
  volatile float k2_t_ = 0.f, k3_t_ = 0.f, k4_t_ = 0.f, k5_t_ = 0.f;   // targets
  float su_ = 0.f, sd_ = 1.f;      // octave-side split (slid)
  volatile float su_t_ = 0.f, sd_t_ = 1.f;
  float oct_step_ = 0.f;
  volatile bool mute_ = false;
  float mute_g_ = 1.f, mute_step_ = 0.f;

  // DIAG (main loop only)
  DiagSnap diag_tap_{}, diag_auto_{};
  bool     diag_tap_pend_ = false, diag_auto_pend_ = false;
  int      diag_n_ = 0;
  bool     diag_blink_ = false;
  uint32_t diag_blink_t_ = 0;
  bool     diag_changed_ = true;
  uint32_t diag_t_ = 0;
  float    diag_ref_k_[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
  int      diag_ref_sw1_ = -1, diag_ref_sw2_ = -1;
};
