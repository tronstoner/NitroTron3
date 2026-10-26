#pragma once
//
// explorer — sound-exploration module on the armitage band engine.  SW3 MIDDLE on NitroLab.
//
// The band engine is COPIED from armitage.h (not shared): the input is decimated 48 -> 8 kHz,
// split into 84 complex bands (armitage_bands.h, included read-only), each band reshaped by one
// of 9 models, summed, interpolated back to 48 kHz, DC-blocked, K4-filtered, soft-clipped,
// FS1-muted. No envelopes / onset detector, no LFO. No pitch tracking: every band runs the
// selected model on its OWN phase phi_k and magnitude a_k = |z'_k|; nothing picks or decides
// per band. The model output is always full (no unshaped crossfade).
//
// SLOTS (each a full voice; SW2 selects ONE, as armitage):
//   played  z'_k = (-1)^k z_k (phase alignment, ARM_ALIGN), output weight w_k (fitted, armitage's wmid_)
//   up      z'_k = z_k^2 / |z_k|                 (POG Up1, f'_k = 2 f_k), slot gain EXP_GAIN_UP
//   down    z'_k = Down1(z_k) (half phase, sign continuation; f'_k = f_k / 2), slot gain EXP_GAIN_DOWN
//   u_k = z'_k / |z'_k| = (cos 2 pi phi_k, sin 2 pi phi_k), phi_k = atan2 / 2 pi in [0, 1) (polynomial)
//   slot = sum_k w_k a_k W_k                     (W per model, below; w_k = 1 in the octave slots)
//
// MIX:  wet8 = EXP_OUT_GAIN [ u G_UP up + p played + d G_DOWN down ], (u, p, d) one-hot from SW2
//       (UP / MID / DOWN), slid linearly over EXP_OCT_XFADE_MS on an SW2 change (old -> new slot
//       crossfade). A slot with weight exactly 0 is NOT computed; a slot that starts again after being
//       idle starts from cleared model state.
//   wet = interpolate(wet8) -> DC blocker -> K4 post filter (SW1 MID / DOWN) -> soft clipper -> FS1 mute
//
// K4 ON SW1 UP = triggered envelope on the K3 FM index (copied 1:1 from armitage's K2 FM envelope):
//   onset detector per 48 kHz input sample before the decimator (EXP_ONSET_*); envelope gain g at 8 kHz
//   (EnvGain): t = 2 k4 - 1 (smoothed, lands on its target), noon = off (g = 1); CCW: each onset
//   g = 1 -> one-pole decay to 0 in the set time, holds 0; CW: each onset g = 0 -> rise to 1, holds 1;
//   time 36 s just off noon -> 450 ms at the ends (EXP_ENV_*). All four UP models use
//   I = EXP_FM_INDEX_MAX (k3 g)^2 (g slid 1 ms, EXP_ENV_FM_SLIDE_MS: no onset click), their caps applied after as before. On UP the K4 post filter is
//   not run: its mix slides to 0 (at most 1 per EXP_FILT_SLIDE_MS, cutoff held), then bypass; leaving UP
//   it slides back in.
//   + LIVE FOLLOWER on top (2026-10-26, armitage's): EnvFollower (rectifier x EXP_FOLLOW_GAIN -> 4-pole LP
//   EXP_FOLLOW_HZ) on the raw 48 kHz input next to the onset detector, f = min(follower, 1) per 8 kHz step;
//   k3_eff = k3 (g + EXP_FOLLOW_FM_DEPTH on f), on slid (one-pole, EXP_FOLLOW_GATE_MS) to 1 off K4 noon,
//   0 at noon. EXP_FOLLOW_FM_DEPTH = 0: bit-identical.
//
// K4 POST FILTER (armitage's K4 law, no envelopes): EXP_FILT_STAGES TPT SVF stages (1: 12 dB/oct, 2: 24 dB/oct),
//   Q = EXP_FILT_Q (0.707, flat) per stage; t = 2 k4 - 1 smoothed per block; noon = true bypass
//   (crossfade over |t| < EXP_FILT_FADE); CCW LP 4 kHz -> 180 Hz, CW HP 40 Hz -> 4 kHz, cutoff
//   linear in octaves (curve 1); LP <-> HP: fades out, swaps type from cleared state, fades in.
//
// K5 BAND TILT (2026-10-26; LP / HP by weighting the bands, not a filter): t = 2 k5 - 1 (smoothed per 8 kHz
//   sample, lands exactly); per audio block w_k from the smoothed t (expf per band, cached while t holds);
//   each band's shaped output w_k a_k W_k is multiplied by bt_k in ShapeK before the band sum (every slot, every
//   model, both sides of a model crossfade). f_k = analysed centre (before SW2), n = EXP_BTILT_ORDER;
//   CCW: fc = 1685.42 (EXP_BTILT_LP_FMIN / 1685.42)^|t|, bt = 1 / sqrt(1 + (f_k / fc)^2n);
//   CW:  fc = 25.38 (EXP_BTILT_HP_FMAX / 25.38)^t,       bt = 1 / sqrt(1 + (fc / f_k)^2n)   (band 83 / 0 centres);
//   bt_eff = 1 - m (1 - bt), m = min(|t| / EXP_BTILT_FADE, 1). t == 0: no multiply (bit-identical). No makeup.
//
// DISCRETE VALUES are stepped on the knob (snap to the item): items that change the waveform
// (cross-band partner, bit-table waveform, wavetable set) crossfade old -> new over
// EXP_MODEL_XFADE_MS at the switch moment; snapped numbers inside a running oscillator (curated
// FM ratio, interval) slide linearly to the new item over EXP_DISC_SLIDE_MS. Never blended between items.
//
// MODELS (L = EXP_FOLD_LIMIT_HZ; "cap" = anti-fold, only with EXP_ANTIFOLD)
//  SW1 UP — FM
//   1 RATIO FM   W = cos(2 pi phi + I_k sin(2 pi pm)),  pm += r wrap(phi - phi_prev) (the modulator runs
//                at r x the band's own phase; for integer r identical to r phi). r = 0.5 * 16^K2 (free),
//                I = 12 K3^2, cap I_k <= (L / f' - 1) / r - 1 (Carson).
//   2 RATIO FM C the same with r stepped through EXP_FM_RATIOS (14 equal K2 zones, slid 1 ms), same cap.
//   3 XBAND FM   W = cos(2 pi phi_k + I_k sin(2 pi phi_{k+n})), n = 1..24 integer (24 equal K2 zones,
//                1 ms crossfade); partner beyond the top band: 0. cap I_k <= (L - f'_k) / f'_{k+n} - 1.
//   4 FB FM      m = sin(2 pi phi + b (m1 + m2) / 2)  (DX7 two-sample average), W = cos(2 pi phi + I_k m);
//                EXP_FBFM_PUSHED true: b = 3 K2, no caps; false: b = 1.5 K2, cap b_k <= L / f' - 1
//                (armitage FB), cap I_k <= L / f' - 2; I = 12 K3^2; ratio 1.
//  SW1 MID — tables
//   1 WAVETABLE  W = table(set, scan, level_k)(phi), linear interpolation in phase and between
//                adjacent waves (scan = 3 K2); set = K3 zone (analog / PPG / vocal / noisy);
//                level_k = largest band-limited version with H <= 4000 / f'_k (static per band, slot).
//   2 BITTAB     (PARKED: EXP_BITTAB_ENABLE = false -> SW1 MID = wavetable only, K1 does nothing there)
//                16-step pattern v[0..15], one step per 1/16 of the band cycle; per step (position f in
//                [0, 1), v0 this step, v1 the next): tri = v0 + f (v1 - v0), saw = tri if v1 > v0 else v0,
//                pulse = v0; K3 = shape: 0 -> 0.5 crossfades tri -> saw, 0.5 -> 1 saw -> pulse (linear,
//                continuous; Ataraxic-style triangle / saw / pulse). K2 steps through 32
//                waveforms (equal zones, 1 ms crossfade): 1..16 = LFSR patterns (+-1, 16-bit Fibonacci
//                LFSR, seed EXP_BT_SEED, tap set EXP_BT_TAPS[i], first 16 new bits), 17..32 = SQR
//                v[j] = sq(j) cos(2 pi n j / 16), n = 1..16 (sampled at the step starts: n > 8 mirrors on
//                the 16-step grid). Anti-fold: S_k steps (16 / 8 / 4 / 2, (S/2) f' <= L),
//                pattern subsampled every 16 / S_k-th step.
//  SW1 DOWN — harmonics & shift
//   1 HARMONICS  H = (1 - fr) h_n0 cos(2 pi n0 phi) + fr h_n1 cos(2 pi n1 phi), n = 1 + 5 K2 = n0 + fr,
//                n1 = n0 + 1, h_n = clamp((3800 - n f') / 400, 0, 1) (armitage ADD drop);
//                W = (1 - K3) H + K3 cos(2 pi phi).
//   2 FREQ SHIFT W = Re(u_k e^{j 2 pi theta_k}), theta_k += s_k / 8000,
//                s_k = s ((1 - K3) + K3 f'_k / EXP_FS_REF_HZ), s = sign(t) ((801)^|t| - 1) Hz, t = 2 K2 - 1.
//   3 INTERVAL   proportional shift only: theta_k += f'_k (2^((st + c / 100) / 12) - 1) / 8000,
//                st = -12 .. +12 semitones (25 equal K2 zones, noon = 0, slid 1 ms), c = 50 (2 K3 - 1) cents.
//
// PHASE phi_k: computed once per band per slot and sample in Prep (only when a running model needs it:
// ratio FM free / curated, harmonics, wavetable, bit table): atan of u_k in the played / +1 / -1 slots
// (the same call on the same values the models made before). A model crossfade shares it.
//
// MODEL CHANGE (SW1, K1 zone, or a discrete K2 / K3 item that changes the waveform): old and new are
// both computed for EXP_MODEL_XFADE_MS and crossfaded linearly; the entering model starts from cleared
// state. A change during a fade is picked up when that fade ends.
//
// CONTROL MAP
//   K1  model        equal zones: SW1 UP 4, MID 2 (1 while the bit table is parked), DOWN 3 (see MODELS)
//   K2  sweep A      per model (see MODELS)
//   K3  sweep B      per model
//   K4  SW1 MID / DOWN: post filter  CCW LP 180 Hz <- 4 kHz | noon bypass | CW HP 40 Hz -> 4 kHz (12 dB/oct x EXP_FILT_STAGES, Q 0.707)
//       SW1 UP: FM index envelope    noon off | CCW decay K3 -> 0 per pluck | CW rise 0 -> K3 per pluck
//                                    (36 s just off noon -> 450 ms at the ends); no post filter
//                                    + live follower off noon: k3_eff = k3 (g + EXP_FOLLOW_FM_DEPTH f)
//   K5  band tilt    CCW LP fc 1685 Hz -> 40 Hz | noon off (bit-identical) | CW HP fc 25 Hz -> 1000 Hz
//                    (band weights 1 / sqrt(1 + x^4), all models and slots; see K5 BAND TILT)
//   K6  dry / wet    (shell)
//   SW1 family       UP = FM, MID = tables (wavetable, bit table), DOWN = harmonics & shift
//   SW2 octave       UP = +1 oct, MID = as played, DOWN = -1 oct (one slot, crossfade EXP_OCT_XFADE_MS)
//   FS1 wet mute     toggle, LED2 on while muted; LED1 on. FS2 unused.
//   DIAG builds: FS1 tap also = numbered "note this setting" line; `auto` line after settling.
//
// K2, K3, K4, K5 are one-pole smoothed per 8 kHz sample (EXP_CTRL_SMOOTH_MS); continuous derived values
// are computed per 8 kHz sample from the smoothed knobs; stepped items from the raw knob.
//
// Header-only; main.cpp includes daisy.h / hothouse.h / control_surface.h / module.h first.
// No dynamic allocation. Wavetables (int16, ~112 KB) are computed at Init into SDRAM.

#include <cmath>
#include <cstdint>
#include "module.h"
#include "knob_map.h"          // RemapKnob, Quantize
#include "armitage_bands.h"    // band engine (read-only use)
#include "explorer_constants.h"
#include "env_follower.h"      // EnvFollower (live follower on top of the FM envelope)

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

// K4 post filter stage: TPT state-variable filter (a local copy of armitage's ArmSVF).
struct ExpSVF {
  float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 0.f;
  void Set(float fc, float Q, float sr) {
    const float g = tanf(3.14159265f * fc / sr);
    k = 1.f / Q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void CopyCoefFrom(const ExpSVF& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
  inline void Process(float x, float& lp, float& hp) {
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    lp = v2; hp = x - k * v1 - v2;
  }
  void Reset() { ic1 = ic2 = 0.f; }
};

class Explorer : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch() values
  enum { kSU = 0, kSP = 1, kSD = 2, kSlots = 3 };   // slots: up, played, down (SW2 selects one)
  // Global model ids (family offset + K1 zone).
  enum { kRFM = 0, kRFMC, kXFM, kFBFM,                // SW1 UP
         kWT, kBIT,                                   // SW1 MID
         kHARM, kFSH, kISH,                           // SW1 DOWN
         kNumModels };
  enum { kAccRFM = 0, kAccRFMC = 1, kAccN = 2 };                 // phase-accumulator users
  enum { kRotFSH = 0, kRotISH = 1, kRotN = 2 };                  // rotation-phase users

  void Init(float sr) override {
    using namespace explorer_k;
    sr8_ = sr / (float)kFactor;
    sr_ = sr;
    for (int i = 0; i < EXP_SIN_SIZE; i++)
      explorer_sin_tab[i] = sinf(6.283185307f * (float)i / (float)EXP_SIN_SIZE);
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;
    static const float r[kSlots] = { 2.f, 1.f, 0.5f };          // up / played / down
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      bt_lnf_[k] = logf(f);                                // K5 band tilt: ln f_k (analysed centre)
      {                                                    // armitage ARM_ALIGN fitted weight (played slot)
        float fc = f;
        if (fc < EXP_ALIGN_W_FLO) fc = EXP_ALIGN_W_FLO;
        if (fc > EXP_ALIGN_W_FHI) fc = EXP_ALIGN_W_FHI;
        const float x = log2f(fc);
        wmid_[k] = powf(10.f, (EXP_ALIGN_WA * x * x + EXP_ALIGN_WB * x + EXP_ALIGN_WC) / 20.f);
      }
      for (int o = 0; o < kSlots; o++) {
        const float fp = r[o] * f;                         // f'_k
        fp_[o][k] = fp;
        ifp_[o][k] = 1.f / fp;
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
    BuildBitPatterns();
    BuildTables();
    for (int o = 0; o < kSlots; o++) { ResetSlotState(o); slot_on_[o] = false; }
    dc_a_ = expf(-6.283185307f * EXP_DC_HZ / sr);
    dc_x1_ = dc_y1_ = 0.f;
    ctrl_a_ = 1.f - expf(-1000.f / (EXP_CTRL_SMOOTH_MS * sr8_));
    mfade_len_ = (int)(EXP_MODEL_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (mfade_len_ < 1) mfade_len_ = 1;
    mfade_n_ = 0;
    disc_len_ = (int)(EXP_DISC_SLIDE_MS * 0.001f * sr8_ + 0.5f);
    if (disc_len_ < 1) disc_len_ = 1;
    oct_step_ = 1.f / (EXP_OCT_XFADE_MS * 0.001f * sr8_);
    var_ = var_t_ = vfrom_ = VarKey(kRFM, 0);
    k2s_ = k2_t_ = 0.f;
    k3s_ = k3_t_ = 0.f;
    k4s_ = k4_t_ = 0.5f;                         // K4 noon = envelope off
    k5s_ = k5_t_ = 0.5f;                         // K5 noon = band tilt off
    bt_t_ = 0.f;
    bt_on_ = false;
    {
      const float fbot = armbands::ArmCenterFreq(0), ftop = armbands::ArmCenterFreq(kBands - 1);
      bt_lnbot_ = logf(fbot);
      bt_lntop_ = logf(ftop);
      bt_lnhp_  = logf(EXP_BTILT_HP_FMAX / fbot);
      bt_lnlp_  = logf(EXP_BTILT_LP_FMIN / ftop);
    }
    // UP: onset detector (48 kHz, pre-decimator) + FM index envelope (8 kHz), armitage's.
    on_env_ = on_gate_ = on_base_ = 0.f;
    on_env_a_  = 1.f - expf(-1000.f / (EXP_ONSET_ENV_MS * sr));
    on_fall_a_ = 1.f - expf(-1000.f / (EXP_ONSET_FALL_MS * sr));
    on_refr_ = 0;
    on_refr_len_ = (int)((float)EXP_ONSET_REFRACTORY_MS * 0.001f * sr + 0.5f);
    on_pending_ = false;
    on_seen_ = false;
    env_fm_.Reset();
    fm_gs_ = env_fm_.g;              // slid g starts on g (1)
    fm_gs_a_ = 1.f - expf(-1000.f / (EXP_ENV_FM_SLIDE_MS * sr8_));
    ln_tau_ratio_ = logf(EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S);
    // Live follower (48 kHz, raw input) + its noon gate (8 kHz).
    fol_.Init(sr);
    fol_.SetCutoff(EXP_FOLLOW_HZ);
    fol_.SetPreGain(EXP_FOLLOW_GAIN);
    fol_y_ = fol_f_ = fol_on_ = 0.f;
    fol_gate_a_ = 1.f - expf(-1000.f / (EXP_FOLLOW_GATE_MS * sr8_));
    filt_off_ = false;
    for (int i = 0; i < 3; i++) { og_[i] = og_t_[i] = 0.f; }
    og_[1] = og_t_[1] = 1.f;                     // SW2 MID = played slot
    rfmc_i_t_ = 0;
    rfmc_.Jump(EXP_FM_RATIOS[0]);
    ish_t_ = 0;
    ish_.Jump(0.f);
    // K4 post filter: bypass, cleared state.
    ft_ = ft_t_ = 0.f;
    f_on_ = false;
    f_hp_ = false;
    f_sz_ = 0;
    f_a_ = 0.f;
    f_m_ = 0.f;
    f_step_ = 1.f / (EXP_FILT_SLIDE_MS * 0.001f * sr);
    f_lnhp_ = logf(EXP_FILT_HP_MAX / EXP_FILT_FMIN);
    f_lnlp_ = logf(EXP_FILT_LP_MIN / EXP_FILT_FMAX);
    for (int i = 0; i < 2; i++) svf_[i].Reset();
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
                    : (fam == kMid) ? kWT   + (EXP_BITTAB_ENABLE ? Quantize(k1, EXP_N_MID) : 0)
                                    : kHARM + Quantize(k1, EXP_N_DOWN);
    const int zone = ZoneOf(model, k2, k3);
    var_t_ = VarKey(model, zone);
    rfmc_i_t_ = Quantize(k2, EXP_FM_N_RATIOS);                        // curated FM ratio item
    ish_t_ = Quantize(k2, 2 * EXP_IS_SEMI_MAX + 1) - EXP_IS_SEMI_MAX; // interval, semitones
    k2_t_ = k2;
    k3_t_ = k3;
    k4_t_ = k4;                                                       // UP: FM index envelope knob
    k5_t_ = k5;                                                       // K5 band tilt
    ft_t_ = 2.f * k4 - 1.f;                                           // MID / DOWN: K4 post filter t
    filt_off_ = (fam == kUp);                                         // UP: no post filter
    og_t_[0] = (oct == kUp)   ? 1.f : 0.f;   // SW2 UP   -> +1 slot
    og_t_[1] = (oct == kMid)  ? 1.f : 0.f;   // SW2 MID  -> played slot
    og_t_[2] = (oct == kDown) ? 1.f : 0.f;   // SW2 DOWN -> -1 slot

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      const float disc = (model == kRFMC) ? EXP_FM_RATIOS[rfmc_i_t_] : (float)ish_t_;
      const Params p = Derive(model, zone, k2, k3, disc);
      DiagControls(cs, led1, kv, fam, oct, model, zone, p.a, p.b, 2.f * k4 - 1.f, env_fm_.g, fol_f_,
                   2.f * k5 - 1.f);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;          // tap number (#n), 0 = automatic line
    float k[5];       // K1..K5 (remapped 0..1)
    int   sw1, sw2;   // 0 UP, 1 MID, 2 DOWN
    int   model;      // global model id (kRFM .. kBIT)
    int   zone;       // wavetable set (0..3), cross-band n - 1, bit-table waveform - 1, else 0
    float a, b;       // sweep A / sweep B as derived values (units per model)
    float k4t;        // K4 t = 2 k4 - 1: SW1 UP = FM envelope t, else post filter t
    float envg;       // SW1 UP: FM envelope gain g (0..1) at the snapshot
    float fol;        // live follower f = min(follower, 1) at the snapshot (0..1)
    float btt;        // K5 band tilt t = 2 k5 - 1 (raw knob)
    float btfc;       // its fc (Hz), 0 at noon
    bool  mute;
  };
  void Activate() override {
    if (NL_DIAG) { diag_changed_ = true; diag_t_ = daisy::System::GetNow(); }
  }
  // K5 band tilt fc (Hz) at t: CCW LP f_top (LP_FMIN / f_top)^|t|, CW HP f_bot (HP_FMAX / f_bot)^t, 0 at noon.
  float BTiltFc(float t) const {
    if (t == 0.f) return 0.f;
    return (t < 0.f) ? expf(bt_lntop_ - t * bt_lnlp_) : expf(bt_lnbot_ + t * bt_lnhp_);
  }
  bool DiagTake(DiagSnap& out) {
    if (diag_tap_pend_)  { out = diag_tap_;  diag_tap_pend_ = false;  return true; }
    if (diag_auto_pend_) { out = diag_auto_; diag_auto_pend_ = false; return true; }
    return false;
  }
  static const char* ModelName(int m) {
    static const char* const n[kNumModels] = { "RATIO-FM", "RATIO-FM-C", "XBAND-FM", "FB-FM",
                                               "WTABLE", "BITTAB",
                                               "HARM", "FSHIFT", "INTERVAL" };
    return (m >= 0 && m < kNumModels) ? n[m] : "?";
  }
  static const char* ZoneName(int m, int z) {
    static const char* const wt[4] = { "analog", "ppg", "vocal", "noisy" };
    if (z < 0 || z > 3) return "";
    if (m == kWT) return wt[z];
    return "";
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace explorer_k;
    const float ca = ctrl_a_;
    BTiltUpdate();                               // K5 band tilt: weights for this block (smoothed t)
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      for (size_t j = 0; j < kFactor; j++) {
        OnsetDetect(in[i + j]);                  // onset detector, 48 kHz |in|, pre-decimator
        fol_y_ = fol_.Process(in[i + j]);        // live follower, 48 kHz raw input
      }
      const float d = dec_(in + i);
      k2s_ += (k2_t_ - k2s_) * ca;
      k3s_ += (k3_t_ - k3s_) * ca;
      k4s_ += (k4_t_ - k4s_) * ca;              // UP: FM envelope knob
      if (fabsf(k4_t_ - k4s_) < EXP_CTRL_SNAP) k4s_ = k4_t_;   // land exactly (noon: envelope fully off)
      k5s_ += (k5_t_ - k5s_) * ca;              // K5 band tilt
      if (fabsf(k5_t_ - k5s_) < EXP_CTRL_SNAP) k5s_ = k5_t_;   // land exactly (noon: tilt fully off)
      EnvUpdate();                               // UP: FM index envelope (consumes the 48 kHz onset flag)
      for (int j = 0; j < 3; j++) og_[j] = Slide(og_[j], og_t_[j], oct_step_);
      rfmc_.Set(EXP_FM_RATIOS[rfmc_i_t_], disc_len_);   // snapped items slide over EXP_DISC_SLIDE_MS
      rfmc_.Tick();
      ish_.Set((float)ish_t_, disc_len_);
      ish_.Tick();

      // Model / item change: start a crossfade (picked up when no fade is running).
      if (mfade_n_ == 0 && var_t_ != var_) {
        vfrom_ = var_;
        var_ = var_t_;
        const int mn = var_ >> 6;
        if (mn != (vfrom_ >> 6))                         // a new model starts cleared (items of the
          for (int o = 0; o < kSlots; o++) ResetModelState(mn, o);   // same model share its state)
        mfade_n_ = mfade_len_;
      }
      const bool fading = (mfade_n_ > 0);
      // UP models: k3_eff = k3 (g_slid + EXP_FOLLOW_FM_DEPTH on f) (the K4 envelope gain, slid 1 ms, + the live
      // follower); all others use K3 as is.
      const float k3up = k3s_ * (fm_gs_ + EXP_FOLLOW_FM_DEPTH * (fol_on_ * fol_f_));
      pn_ = Derive(var_ >> 6, var_ & 63, k2s_, IsUp(var_ >> 6) ? k3up : k3s_, DiscOf(var_ >> 6));
      if (fading) pf_ = Derive(vfrom_ >> 6, vfrom_ & 63, k2s_, IsUp(vfrom_ >> 6) ? k3up : k3s_,
                               DiscOf(vfrom_ >> 6));
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

      // SW2: one voice (up / played / down); on a change the old and the new slot crossfade
      // (weights slid over EXP_OCT_XFADE_MS); a slot with weight 0 is not computed.
      static constexpr int   oslot[3] = { kSU, kSP, kSD };
      static constexpr float ogain[3] = { EXP_GAIN_UP, 1.f, EXP_GAIN_DOWN };
      float acc = 0.f;
      for (int j = 0; j < 3; j++) {
        const float g = og_[j];
        if (g != 0.f) acc += g * ogain[j] * RunSlot(oslot[j], fading);
        else slot_on_[oslot[j]] = false;
      }
      if (mfade_n_ > 0) --mfade_n_;
      int_(acc * EXP_OUT_GAIN, wet + i);
    }
    DcBlock(wet, size);
    PostFilter(wet, size);  // K4: LP | bypass | HP, EXP_FILT_STAGES x 12 dB/oct, Q 0.707
    Saturate(wet, size);
    ApplyMute(wet, size);
  }

 private:
  // Per-sample model parameters.
  struct Params {
    float a = 0.f, b = 0.f;     // sweep A / B as displayed (DIAG)
    float p0 = 0.f, p1 = 0.f, p2 = 0.f, p3 = 0.f;   // model-specific (see Derive)
    int   n0 = 0;               // integer item (partner offset, harmonic, scan)
    int   zone = 0;
    const float* bt = nullptr;  // BITTAB: the selected 16-step waveform
  };

  // Linear slide of a snapped value to its new item over a fixed number of 8 kHz samples.
  struct DSlide {
    float v = 0.f, t = 0.f, step = 0.f;
    int   n = 0;
    void Jump(float x) { v = t = x; n = 0; }
    void Set(float target, int len) {
      if (target != t) { t = target; step = (t - v) / (float)len; n = len; }
    }
    void Tick() {
      if (n > 0) { v += step; if (--n == 0) v = t; }
    }
  };

  static inline int VarKey(int model, int zone) { return (model << 6) | (zone & 63); }
  static inline bool IsUp(int m) { return m == kRFM || m == kRFMC || m == kXFM || m == kFBFM; }

  // UP: onset detector (armitage OnsetDetect, 1:1), once per 48 kHz sample on |in| before the decimator.
  // Fast meter -> gate meter (up at once, slow fall) -> slow baseline; onset = gate > RISE x baseline
  // and > FLOOR, then a refractory. Latches on_pending_ for EnvUpdate() (8 kHz); the refractory is far
  // longer than one 8 kHz step, so an onset is never lost or double-counted.
  inline void OnsetDetect(float in_sample) {
    using namespace explorer_k;
    const float a = fabsf(in_sample);
    on_env_ += on_env_a_ * (a - on_env_);
    if (on_env_ > on_gate_) on_gate_ = on_env_;
    else                    on_gate_ += on_fall_a_ * (on_env_ - on_gate_);
    on_base_ += EXP_ONSET_SLOW_COEF * (on_gate_ - on_base_);
    if (on_refr_ > 0) on_refr_--;
    const bool onset = (on_refr_ == 0 && on_gate_ > EXP_ONSET_FLOOR &&
                        on_gate_ > on_base_ * EXP_ONSET_RISE);
    if (onset) { on_refr_ = on_refr_len_; on_pending_ = true; }
  }

  // UP: FM index envelope gain g (armitage EnvGain, 1:1). T = TIME_RATIO TAU_SLOW (FAST / SLOW)^|t|;
  //   CCW (decay): onset -> g = 1, one-pole decay reaching 0 in DECAY_RATIO T, then holds 0.
  //   CW (rise):   onset -> g = 0, one-pole rise reaching 1 in the same time, then holds 1.
  // "Reaching" = the one-pole's 98 % point (tau = travel / TRAVEL_TAUS), where g snaps to the end value.
  // Noon: off, g = 1. Before the first onset: CCW g = 1, CW g = 0.
  struct EnvGain {
    volatile float g = 1.f;
    float at = -1.f, dec_a = 0.f;
    int   phase = 0;               // 0 idle (holding), 1 decaying (CCW), 2 rising (CW)
    void Reset() { g = 1.f; at = -1.f; dec_a = 0.f; phase = 0; }
    inline void Step(bool onset, bool seen, float t2, float sr8, float ln_tau_ratio) {
      using namespace explorer_k;
      if (t2 == 0.f) { g = 1.f; phase = 0; return; }
      const bool cw = (t2 > 0.f);
      if (onset) { if (cw) { g = 0.f; phase = 2; } else { g = 1.f; phase = 1; } }
      if (!seen) { g = cw ? 0.f : 1.f; phase = 0; return; }
      const float am = fabsf(t2);
      if (am != at) {
        at = am;
        const float T = EXP_ENV_FM_TIME_RATIO * EXP_ENV_TAU_SLOW_S * expf(am * ln_tau_ratio);
        dec_a  = 1.f - expf(-EXP_ENV_TRAVEL_TAUS / (T * EXP_ENV_FM_DECAY_RATIO * sr8));
      }
      switch (phase) {
        case 1: g += dec_a * (0.f - g);  if (g <= 0.02f) { g = 0.f; phase = 0; } break;
        case 2: g += dec_a * (1.f - g);  if (g >= 0.98f) { g = 1.f; phase = 0; } break;
        default: break;
      }
    }
  };
  inline void EnvUpdate() {
    const bool onset = on_pending_;
    on_pending_ = false;
    if (onset) on_seen_ = true;
    env_fm_.Step(onset, on_seen_, 2.f * k4s_ - 1.f, sr8_, ln_tau_ratio_);
    using namespace explorer_k;
    // g jumps on an onset (CW -> 0, CCW -> 1): slide it (1 ms one-pole) before it scales K3, land exactly.
    fm_gs_ += fm_gs_a_ * (env_fm_.g - fm_gs_);
    if (fabsf(env_fm_.g - fm_gs_) < EXP_CTRL_SNAP) fm_gs_ = env_fm_.g;
    // Live follower on top: f = min(follower, 1); on slides toward 1 (K4 off noon) / 0 (noon), lands exactly.
    fol_f_ = (fol_y_ < 1.f) ? fol_y_ : 1.f;
    const float on_t = (2.f * k4s_ - 1.f != 0.f) ? 1.f : 0.f;
    fol_on_ += fol_gate_a_ * (on_t - fol_on_);
    if (fabsf(on_t - fol_on_) < EXP_CTRL_SNAP) fol_on_ = on_t;
  }

  // K5 band tilt weights bt_w_[k] from the smoothed t = 2 k5s_ - 1, once per audio block (84 expf only when t
  // moved). t == 0: off (ShapeK skips the multiply). (f / fc)^2n = exp(2n (ln f - ln fc)).
  void BTiltUpdate() {
    using namespace explorer_k;
    const float t = 2.f * k5s_ - 1.f;
    if (t == bt_t_) return;                      // unchanged: weights hold
    bt_t_ = t;
    if (t == 0.f) { bt_on_ = false; return; }
    const float at = fabsf(t);
    float m = at * (1.f / EXP_BTILT_FADE);
    if (m > 1.f) m = 1.f;
    constexpr float n2 = 2.f * (float)EXP_BTILT_ORDER;
    const bool lp = (t < 0.f);
    const float lnfc = lp ? bt_lntop_ + at * bt_lnlp_ : bt_lnbot_ + at * bt_lnhp_;
    for (int k = 0; k < kBands; k++) {
      const float x = expf(n2 * (lp ? bt_lnf_[k] - lnfc : lnfc - bt_lnf_[k]));
      const float w = 1.f / sqrtf(1.f + x);
      bt_w_[k] = 1.f - m * (1.f - w);
    }
    bt_on_ = true;
  }

  static int ZoneOf(int model, float k2, float k3) {
    using namespace explorer_k;
    if (model == kWT) return Quantize(k3, 4);
    if (model == kXFM) return Quantize(k2, EXP_XB_N_MAX);    // partner offset n - 1
    if (model == kBIT) return Quantize(k2, EXP_BT_WAVES);    // waveform index
    return 0;
  }
  float DiscOf(int m) const { return (m == kRFMC) ? rfmc_.v : (m == kISH) ? ish_.v : 0.f; }

  // Derived values per model from K2 / K3 (0..1) and the slid discrete value (curated ratio /
  // interval semitones).
  Params Derive(int m, int zone, float k2, float k3, float disc) const {
    using namespace explorer_k;
    Params p;
    p.zone = zone;
    switch (m) {
      case kRFM: case kRFMC: {            // p0 = r, p1 = 1/r, p2 = I
        const float r = (m == kRFMC) ? disc
                      : EXP_RFM_RATIO_MIN * expf(k2 * logf(EXP_RFM_RATIO_MAX / EXP_RFM_RATIO_MIN));
        p.p0 = r; p.p1 = 1.f / r; p.p2 = EXP_FM_INDEX_MAX * k3 * k3;
        p.a = r; p.b = p.p2; break;
      }
      case kXFM:                           // n0 = partner offset, p1 = I
        p.n0 = zone + 1;
        p.p1 = EXP_FM_INDEX_MAX * k3 * k3;
        p.a = (float)p.n0; p.b = p.p1; break;
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
      case kISH: {                         // p0 = 0, p1 = proportional increment per Hz of f'
        const float cents = EXP_IS_DETUNE_CENTS * (2.f * k3 - 1.f);
        const float ratio = exp2f((disc + cents * 0.01f) * (1.f / 12.f));
        p.p0 = 0.f;
        p.p1 = (ratio - 1.f) / sr8_;
        p.a = disc; p.b = cents; break;
      }
      case kHARM: {                        // n0 + p0 = harmonic n, p1 = fundamental share
        const float n = 1.f + (EXP_HARM_MAX - 1.f) * k2;
        p.n0 = (int)n;
        if (p.n0 > (int)EXP_HARM_MAX) p.n0 = (int)EXP_HARM_MAX;
        p.p0 = n - (float)p.n0; p.p1 = k3;
        p.a = n; p.b = k3; break;
      }
      case kWT: {                          // n0 + p0 = scan position over the set's waves
        const float pos = (float)(EXP_WT_WAVES - 1) * k2;
        p.n0 = (int)pos;
        if (p.n0 > EXP_WT_WAVES - 2) p.n0 = EXP_WT_WAVES - 2;
        p.p0 = pos - (float)p.n0;
        p.a = pos; p.b = k3; break;
      }
      default: {                           // kBIT: bt = waveform zone (0..31); a = 1..32;
                                           // p0 / p1 / p2 = triangle / saw / pulse weights from K3
        p.bt = bt_all_[zone];
        const float x = (k3 < 0.5f) ? 2.f * k3 : 2.f * k3 - 1.f;
        if (k3 < 0.5f) { p.p0 = 1.f - x; p.p1 = x;       p.p2 = 0.f; }   // tri -> saw
        else           { p.p0 = 0.f;     p.p1 = 1.f - x; p.p2 = x;   }   // saw -> pulse
        p.a = (float)(zone + 1); p.b = k3; break;
      }
    }
    return p;
  }

  // One 8 kHz sample of slot o: derive u_k, return the model's band sum sum_k w_k a_k W_k.
  float RunSlot(int o, bool fading) {
    if (!slot_on_[o]) { ResetSlotState(o); slot_on_[o] = true; }
    const int mn = var_ >> 6;
    Prep(o, NeedsPhase(mn) || (fading && NeedsPhase(vfrom_ >> 6)));
    float sh = Shape(var_ >> 6, o, pn_);
    if (fading) sh = (1.f - mg_) * Shape(vfrom_ >> 6, o, pf_) + mg_ * sh;
    return sh;
  }

  // Models that read the band phase phi_k (ph_).
  static inline bool NeedsPhase(int m) {
    return m == kRFM || m == kRFMC || m == kHARM || m == kWT || m == kBIT;
  }

  // u_k = z'_k / |z'_k| for slot o into ur_ / ui_ (0 for silent bands); with need_ph also the phase
  // phi_k in cycles into ph_ (once per band, shared by the models and a model crossfade).
  void Prep(int o, bool need_ph) {
    const float* __restrict mg = mag_ + 1;
    for (int k = 0; k < kBands; k++) {
      if (mg[k] == 0.f) {
        ur_[k] = ui_[k] = 0.f;
        continue;
      }
      const float iv = inv_[k];
      float zr, zi;
      if (o == kSP) {
        zr = bands_[k].Re(); zi = bands_[k].Im();
        if (k & 1) { zr = -zr; zi = -zi; }        // phase alignment (armitage ARM_ALIGN)
      } else if (o == kSU) {
        bands_[k].ArmUp1(iv, zr, zi);
      } else {                                    // kSD
        bands_[k].ArmDown1(iv, zr, zi);
      }
      ur_[k] = zr * iv;
      ui_[k] = zi * iv;
      if (need_ph) ph_[k] = FastAtan2Cycles(ui_[k], ur_[k]);   // the models' former call, same values
    }
  }

  float Shape(int m, int o, const Params& p) {
    switch (m) {
      case kRFM:   return ShapeK<kRFM>(o, p);
      case kRFMC:  return ShapeK<kRFMC>(o, p);
      case kXFM:   return ShapeK<kXFM>(o, p);
      case kFBFM:  return ShapeK<kFBFM>(o, p);
      case kFSH:   return ShapeK<kFSH>(o, p);
      case kISH:   return ShapeK<kISH>(o, p);
      case kHARM:  return ShapeK<kHARM>(o, p);
      case kWT:    return ShapeK<kWT>(o, p);
      default:     return ShapeK<kBIT>(o, p);
    }
  }

  // Modulator phase that runs at r x the band's own phase (ratio FM free / curated).
  inline float AccPhase(int u, int o, int k, float ph, float r) {
    float pm = pm_[u][o][k];
    const float pp = pp_[u][o][k];
    if (pp < 0.f) {
      pm = r * ph;
    } else {
      float dph = ph - pp;
      if (dph < -0.5f) dph += 1.f;
      else if (dph >= 0.5f) dph -= 1.f;
      pm += r * dph;
    }
    pm -= floorf(pm);
    pm_[u][o][k] = pm;
    pp_[u][o][k] = ph;
    return pm;
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
    const float* __restrict phs = ph_;
    const bool played = (o == kSP);
    const float* __restrict btw = bt_on_ ? bt_w_ : nullptr;   // K5 band tilt (nullptr at noon: no multiply)
    constexpr int au = (M == kRFMC) ? kAccRFMC : kAccRFM;
    constexpr int ru = (M == kISH) ? kRotISH : kRotFSH;
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) {                            // silent band: state cleared
        if (M == kRFM || M == kRFMC) pp_[au][o][k] = -1.f;
        if (M == kFBFM) { m1_[o][k] = 0.f; m2_[o][k] = 0.f; }
        continue;
      }
      const float cr = ur[k], ci = ui[k];
      float W;
      if (M == kRFM || M == kRFMC) {
        const float pm = AccPhase(au, o, k, phs[k], p.p0);
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
        const int q = k + p.n0;
        float W0 = cr;
        if (q < kBands) {
          float I = p.p1;
          if (EXP_ANTIFOLD) {
            float cap = (EXP_FOLD_LIMIT_HZ - fp[k]) * ifp[q] - 1.f;
            if (cap < 0.f) cap = 0.f;
            if (I > cap) I = cap;
          }
          float s, c;
          SinCos(I * ui[q], s, c);
          W0 = cr * c - ci * s;
        }
        W = W0;
      } else if (M == kFBFM) {
        float b = p.p0, I = p.p1;
        if (EXP_ANTIFOLD && !EXP_FBFM_PUSHED) {  // pushed variant: no caps
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
      } else if (M == kFSH || M == kISH) {
        float th = th_[ru][o][k] + p.p0 + p.p1 * fp[k];
        th -= floorf(th);
        th_[ru][o][k] = th;
        float c, s;
        CosSin01(th, c, s);
        W = cr * c - ci * s;
      } else if (M == kHARM) {
        const float ph = phs[k];
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
        const float ph = phs[k];
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
      } else {                                   // kBIT: step shape tri / saw / pulse (K3)
        const float ph = phs[k];
        const int S = btst_[o][k];
        const int stride = EXP_BT_STEPS / S;
        const float xs = ph * (float)S;
        int j = (int)xs;
        if (j > S - 1) j = S - 1;
        const float f = xs - (float)j;
        const float v0 = p.bt[j * stride];
        const float v1 = p.bt[((j + 1 == S) ? 0 : j + 1) * stride];
        const float tri = v0 + f * (v1 - v0);    // linear to the next step
        const float saw = (v1 > v0) ? tri : v0;    // ramps on rising steps, hard edge on falling ones
        W = p.p0 * tri + p.p1 * saw + p.p2 * v0;   // v0 = pulse (hold, hard steps)
      }
      if (btw) acc += (played ? wmid_[k] : 1.f) * a * W * btw[k];   // K5 band tilt on the band output
      else     acc += (played ? wmid_[k] : 1.f) * a * W;            // noon: exactly as before
    }
    return acc;
  }

  // Clear one model's state in slot o (on model entry).
  void ResetModelState(int m, int o) {
    const int au = (m == kRFM) ? kAccRFM : (m == kRFMC) ? kAccRFMC : -1;
    const int ru = (m == kFSH) ? kRotFSH : (m == kISH) ? kRotISH : -1;
    for (int k = 0; k < kBands; k++) {
      if (au >= 0)    { pm_[au][o][k] = 0.f; pp_[au][o][k] = -1.f; }
      if (m == kFBFM) { m1_[o][k] = m2_[o][k] = 0.f; }
      if (ru >= 0)    th_[ru][o][k] = 0.f;
    }
  }
  void ResetSlotState(int o) {
    ResetModelState(kRFM, o);
    ResetModelState(kRFMC, o);
    ResetModelState(kFBFM, o);
    ResetModelState(kFSH, o);
    ResetModelState(kISH, o);
  }

  static inline float Slide(float v, float t, float st) {
    if (v < t) { v += st; if (v > t) v = t; }
    else if (v > t) { v -= st; if (v < t) v = t; }
    return v;
  }

  // BITTAB waveforms: 0..15 = LFSR patterns (16-bit Fibonacci LFSR per tap set, fixed seed, first 16
  // new bits, +-1); 16..31 = SQR n = 1..16 (square x harmonic n, sampled at the step starts).
  void BuildBitPatterns() {
    using namespace explorer_k;
    for (int t = 0; t < EXP_BT_PATTERNS; t++) {
      uint16_t st = EXP_BT_SEED;
      for (int j = 0; j < EXP_BT_STEPS; j++) {
        const unsigned bit = (unsigned)__builtin_popcount((unsigned)(st & EXP_BT_TAPS[t])) & 1u;
        st = (uint16_t)((st >> 1) | (bit << 15));
        bt_all_[t][j] = bit ? 1.f : -1.f;
      }
    }
    for (int n = 1; n <= EXP_BT_SQR_N_MAX; n++)
      for (int j = 0; j < EXP_BT_STEPS; j++) {
        const float sq = (j < EXP_BT_STEPS / 2) ? 1.f : -1.f;
        bt_all_[EXP_BT_PATTERNS + n - 1][j] = sq * Cos01((float)(n * j) * (1.f / (float)EXP_BT_STEPS));
      }
  }

  // K4 post filter on the final 48 kHz wet (armitage's PostFilter without the envelope; Q fixed).
  void PostFilter(float* wet, size_t size) {
    using namespace explorer_k;
    if ((int)size != f_sz_) {
      f_sz_ = (int)size;
      f_a_ = 1.f - expf(-(float)size / (EXP_FILT_SMOOTH_MS * 0.001f * sr_));
    }
    const bool off = filt_off_;                  // SW1 UP: no post filter
    if (off && f_m_ <= 0.f) {                    // faded out: bypass (state kept cleared on re-entry)
      f_on_ = false;
      return;
    }
    float ts = ft_;                              // UP: t held (cutoff fixed) while the mix slides to 0
    if (!off) {
      const float tt = ft_t_;
      ts = ft_ + (tt - ft_) * f_a_;
      if (tt == 0.f && fabsf(ts) < EXP_FILT_SNAP) ts = 0.f;
      ft_ = ts;
    }
    if (ts == 0.f && f_m_ <= 0.f) {              // noon: true bypass
      f_on_ = false;
      return;
    }
    const bool want_hp = (ts > 0.f);
    if (!f_on_) {
      f_on_ = true;
      f_hp_ = want_hp;
      svf_[0].Reset();
      svf_[1].Reset();
    }
    const float at = fabsf(ts);
    float mt = at * (1.f / EXP_FILT_FADE);
    if (mt > 1.f) mt = 1.f;
    if (ts == 0.f || want_hp != f_hp_ || off) mt = 0.f;
    {
      const float tmag = expf(EXP_FILT_CURVE * logf((at > 1e-6f) ? at : 1e-6f));   // |t|^curve
      float fc = f_hp_ ? EXP_FILT_FMIN * expf(tmag * f_lnhp_)
                       : EXP_FILT_FMAX * expf(tmag * f_lnlp_);
      fc = (fc < EXP_FILT_FC_MIN) ? EXP_FILT_FC_MIN : (fc > EXP_FILT_FC_MAX) ? EXP_FILT_FC_MAX : fc;
      svf_[0].Set(fc, EXP_FILT_Q, sr_);
      svf_[1].CopyCoefFrom(svf_[0]);
    }
    float m = f_m_;
    const float st = f_step_;
    ExpSVF& s0 = svf_[0];
    ExpSVF& s1 = svf_[1];
    if (f_hp_) {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        if (EXP_FILT_STAGES == 2) s1.Process(hp, lp, y);   // second stage (24 dB/oct)
        else                      y = hp;                  // one stage (12 dB/oct)
        wet[n] = x + m * (y - x);
      }
    } else {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        if (EXP_FILT_STAGES == 2) s1.Process(lp, y, hp);   // second stage (24 dB/oct)
        else                      y = lp;                  // one stage (12 dB/oct)
        wet[n] = x + m * (y - x);
      }
    }
    f_m_ = m;
    if (m <= 0.f && want_hp != f_hp_ && ts != 0.f) {
      f_hp_ = want_hp;
      s0.Reset();
      s1.Reset();
    }
    if (m <= 0.f && ts == 0.f) f_on_ = false;
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
                    int sw1, int oct, int model, int zone, float a, float b, float k4t, float envg,
                    float fol, float btt) {
    using namespace explorer_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.sw1 = sw1; s.sw2 = oct;
    s.model = model; s.zone = zone;
    s.a = a; s.b = b;
    s.k4t = k4t; s.envg = envg;
    s.fol = fol;
    s.btt = btt;
    s.btfc = BTiltFc(btt);
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
  float ph_[kBands];               // its phase phi_k in cycles [0, 1) (when a running model needs it)
  float wmid_[kBands];             // played-slot fitted output weight (armitage ARM_ALIGN)
  float fp_[kSlots][kBands];       // f'_k per slot (up / played / down)
  float ifp_[kSlots][kBands];      // 1 / f'_k
  uint8_t wtlvl_[kSlots][kBands];  // wavetable band-limit level per band
  uint8_t btst_[kSlots][kBands];   // BITTAB steps per band (16 / 8 / 4 / 2)
  float bt_all_[explorer_k::EXP_BT_WAVES][explorer_k::EXP_BT_STEPS] = {};   // BITTAB waveforms

  // Model state per slot and band
  float pm_[kAccN][kSlots][kBands], pp_[kAccN][kSlots][kBands];   // modulator phase, previous band phase (-1 = fresh)
  float m1_[kSlots][kBands], m2_[kSlots][kBands];   // feedback FM: last two modulator outputs
  float th_[kRotN][kSlots][kBands];                  // frequency / interval shift: rotation phase (cycles)
  bool  slot_on_[kSlots] = { false, false, false };   // slot computed last sample

  Params pn_, pf_;                 // current / fading-out model parameters (this sample)
  float dc_a_ = 0.f, dc_x1_ = 0.f, dc_y1_ = 0.f;
  float sr8_ = 8000.f, sr_ = 48000.f;
  float ctrl_a_ = 0.f;
  int   var_ = 0, vfrom_ = 0;      // model variant (model << 6 | item): current / fading out
  volatile int var_t_ = 0;         // target variant (Controls)
  int   mfade_n_ = 0, mfade_len_ = 8;
  float mg_ = 1.f;                 // new-model gain during a fade
  float k2s_ = 0.f, k3s_ = 0.f;                       // smoothed knobs
  volatile float k2_t_ = 0.f, k3_t_ = 0.f;           // targets
  volatile int rfmc_i_t_ = 0, ish_t_ = 0;   // snapped items: curated ratio index, interval semitones
  DSlide rfmc_, ish_;              // their slid values
  int   disc_len_ = 8;             // EXP_DISC_SLIDE_MS in 8 kHz samples
  float og_[3] = { 0.f, 1.f, 0.f };            // slot weights (up, played, down), slid
  volatile float og_t_[3] = { 0.f, 1.f, 0.f };
  float oct_step_ = 0.f;
  // K4 post filter
  ExpSVF svf_[2];
  float ft_ = 0.f;                 // smoothed t (per block)
  volatile float ft_t_ = 0.f;      // target t = 2 k4 - 1
  volatile bool filt_off_ = false; // SW1 UP: post filter faded out / not run
  // UP: K4 FM index envelope (armitage's K2 FM envelope)
  float k4s_ = 0.5f;               // smoothed K4 (envelope t = 2 k4s_ - 1)
  volatile float k4_t_ = 0.5f;
  // K5 band tilt
  float k5s_ = 0.5f;               // smoothed K5 (t = 2 k5s_ - 1)
  volatile float k5_t_ = 0.5f;
  float bt_lnf_[kBands];           // ln f_k (analysed band centre, before SW2)
  float bt_w_[kBands];             // per-band output weight bt_eff (this block; valid while bt_on_)
  float bt_t_ = 0.f;               // t the weights were computed for
  bool  bt_on_ = false;            // false at t == 0: no multiply
  float bt_lnbot_ = 0.f, bt_lntop_ = 0.f;   // ln of the bottom / top band centre
  float bt_lnhp_ = 0.f, bt_lnlp_ = 0.f;     // ln(HP_FMAX / f_bot), ln(LP_FMIN / f_top)
  float on_env_ = 0.f, on_gate_ = 0.f, on_base_ = 0.f;   // fast meter, gate meter, slow baseline (48 kHz)
  float on_env_a_ = 0.f, on_fall_a_ = 0.f;               // meter rise / gate fall coefficients (48 kHz)
  int   on_refr_ = 0, on_refr_len_ = 4800;               // refractory samples left / length (48 kHz)
  bool  on_pending_ = false;                             // an onset fired, not yet consumed by EnvUpdate()
  bool  on_seen_ = false;                                // an onset has happened
  EnvGain env_fm_;
  float fm_gs_ = 1.f;              // env_fm_.g slid (1 ms one-pole, 8 kHz); this scales K3 on UP
  float fm_gs_a_ = 0.f;            // its coefficient (tau EXP_ENV_FM_SLIDE_MS)
  float ln_tau_ratio_ = 0.f;       // ln(EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S)
  // UP: live follower (on top of the FM envelope)
  EnvFollower fol_;                // rectifier + 4-pole LP, 48 kHz raw input
  float fol_y_ = 0.f;              // follower output, latest 48 kHz sample
  volatile float fol_f_ = 0.f;     // f = min(fol_y_, 1), per 8 kHz step (DIAG reads it)
  float fol_on_ = 0.f;             // gate: slid 1 with K4 off noon, 0 at noon
  float fol_gate_a_ = 0.f;         // gate one-pole coefficient (8 kHz, tau EXP_FOLLOW_GATE_MS)
  float f_m_ = 0.f, f_step_ = 0.f; // dry -> filtered mix and its per-sample slide
  float f_lnhp_ = 0.f, f_lnlp_ = 0.f;
  float f_a_ = 0.f;
  int   f_sz_ = 0;
  bool  f_on_ = false, f_hp_ = false;
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
