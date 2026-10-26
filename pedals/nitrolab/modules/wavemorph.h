#pragma once
//
// wavemorph — morphing wavetable voice on the band engine.  SW3 DOWN on NitroLab.
//
// The band engine is COPIED from explorer.h (not shared): decimate 48 -> 8 kHz, 84 complex bands
// (armitage_bands.h, included read-only), slots played / +1 / -1 / -2 (the -2 slot = the unwrapped
// phase accumulator / 4), band phase computed once per band / slot / sample in Prep; each band reads
// the current zone's morph table at its OWN phase phi_k with its magnitude a_k:
//   W_k = lerp_frames( lerp_phase(table[zone][frame][level_k]) ),  slot = sum_k w_k a_k W_k
//   frame position = (WM_FRAMES - 1) m, linear between the two adjacent frames (morph continuous);
//   level_k = the largest band-limited version with H <= 4000 / f'_k (static per band and slot).
// wet8 = WM_OUT_GAIN [ (1 - mix) played + mix (u G_UP up + d G_DOWN down + d2 G_DOWN2 down2) ]
// wet  = interpolate(wet8) -> DC blocker -> K4 post filter -> soft clipper (ceiling 1.0) -> FS1 mute
// No pitch tracking; nothing picks or decides per band.
//
// CONTROL MAP
//   K1  base wave     10 STEPPED equal zones (below); a zone change crossfades old -> new over
//                     WM_ZONE_XFADE_MS (the voice is stateless: the entering zone starts clean)
//   K2  morph env     explorer's FM-index envelope (EnvGain) 1:1 on the morph: t = 2 k2 - 1 (smoothed,
//                     lands exactly); noon = off (g = 1, morph = K3); CCW: each onset g = 1 -> decays
//                     to 0 and holds (morph K3 -> 0 = back to the base wave); CW: each onset g = 0 ->
//                     rises to 1 and holds (base wave -> K3); travel 36 s just off noon -> 450 ms at the ends
//   K3  morph amount  m3 0..1: where the morph sits / the envelope's target; morph m = K3 g
//   K4  post filter   CCW LP 180 Hz <- 4 kHz | noon bypass | CW HP 40 Hz -> 4 kHz (24 dB/oct, Q 0.707)
//   K5  octave mix    CCW = played only (octave slots not computed), CW = selected octave only; linear
//   K6  dry / wet     (shell)
//   SW1 unused (not read)
//   SW2 octave        UP = +1, MID = -1, DOWN = -2 (slid over WM_OCT_XFADE_MS)
//   FS1 wet mute      toggle, LED2 on while muted; LED1 on. FS2 unused.
//   DIAG builds: FS1 tap also = numbered "note this setting" line; `auto` line after settling.
//
// TABLES (built at Init into SDRAM, int16): per zone WM_FRAMES = 16 frames, u = frame / 15
// (frame 0 = the exact base wave, frame 15 = the extreme). Each frame: defined in the time domain at
// WM_TD_SIZE (1024) points and analysed by DFT into harmonics 1..127, or defined directly by its
// harmonic amplitudes / phases (no DFT); mean removed, peak normalised to 1 (full-band frame), then
// rebuilt as 14 band-limited versions {1,2,3,4,6,8,12,16,24,32,48,64,96,127} harmonics at 256 points.
//   1 SINE     -> sync:   x = sin(2 pi frac(R phi)), R = 1 + 7 u (hard-synced sine; u = 0: the sine)    [time]
//   2 TRIANGLE -> fold:   x = fold(G tri(phi)), G = 1 + 7 u, fold(v) = |((v - 1) mod 4) - 2| - 1        [time]
//   3 SAW      -> reso:   x = (1 - mix) saw + mix sin(2 pi R phi) (1 - phi), saw = 2 phi - 1,
//                         R = 1 + 7 u, mix = 0.9 u (armitage's approved SAW push)                       [time]
//   4 SQUARE   -> needle: x = +1 (phi < w) else -1, w = 0.5 - (0.5 - 0.02) u                           [time]
//   5 PPG GLASS:          fundamental 0.35 + three Gaussian clusters (sigma 0.8 harmonics) at c x
//                         {1, 1.593, 2.405} (amps 1, 0.7, 0.5), c = 6 -> 30 across the frames;
//                         fixed random phases per harmonic                                            [spectral]
//   6 PPG AIR:            fundamental 1 + 4 sparse odd partials (amp 0.5) at odd(B_j (1 + 3 u)),
//                         B = {5, 9, 15, 23}; sine phase                                                [spectral]
//   7 PPG SPECTRAL:       amp_h = h^-0.5 (full, bright); harmonic h >= 2 drops out at u >= d_h,
//                         d_h = frac(0.618 h) / 0.95 (golden-ratio comb: notches sweep, groups of
//                         partials switch off; ~5 % remain at frame 15); fixed random phases           [spectral]
//   8 VOCAL:              vowels a -> e -> i -> o -> u (formant frequencies interpolated between
//                         adjacent vowels, 4 segments), Lorentzian formants at 110 Hz reference
//                         (explorer's a / e / i / o, u = 325 / 700 / 2530 Hz); sine phase               [spectral]
//   9 FORMANT:            x = 0.5 (1 - cos 2 pi phi) cos(2 pi Fr phi), Fr = 0.5 * 8^u (0.5 -> 4)          [time]
//  10 NOISE:              amp_h = h^-(1 - u) (1 + u (r_h - 1)), r_h random in [0, 1), fixed random
//                         phases: frame 0 pitched 1/h, then rougher, frame 15 = flat random spectrum
//                         (band-limited white noise)                                                    [spectral]
//
// Header-only; main.cpp includes daisy.h / hothouse.h / control_surface.h / module.h first.
// No dynamic allocation. Tables: 10 x 16 x 14 x 256 int16 = 1.15 MB in SDRAM.

#include <cmath>
#include <cstdint>
#include "module.h"
#include "knob_map.h"          // RemapKnob, Quantize
#include "armitage_bands.h"    // band engine (read-only use)
#include "wavemorph_constants.h"

#ifndef DTCM_MEM_SECTION
#define DTCM_MEM_SECTION       // host builds
#endif
#ifndef DSY_SDRAM_BSS
#define DSY_SDRAM_BSS          // host builds
#endif

// Sin table (filled in Init; DTCM = zero-wait-state reads). Size = WM_TD_SIZE: table builds index it exactly.
static float DTCM_MEM_SECTION wavemorph_sin_tab[wavemorph_k::WM_SIN_SIZE];

// Morph tables [zone][frame][level][sample], int16 (scale WM_I16_SCALE), SDRAM.
static int16_t DSY_SDRAM_BSS wavemorph_tab[wavemorph_k::WM_ZONES][wavemorph_k::WM_FRAMES]
                                          [wavemorph_k::WM_LEVELS][wavemorph_k::WM_TAB_SIZE];
// Init-only build scratch (SDRAM: keeps DTCM free).
struct WaveMorphScratch {
  float x[wavemorph_k::WM_TD_SIZE];                          // time-domain frame
  float acc[wavemorph_k::WM_TAB_SIZE];                       // level accumulator
  float A[wavemorph_k::WM_HMAX + 1], B[wavemorph_k::WM_HMAX + 1];   // sin / cos coefficients
  float rph[wavemorph_k::WM_HMAX + 1], rr[wavemorph_k::WM_HMAX + 1]; // fixed random phase / amplitude per harmonic
};
static WaveMorphScratch DSY_SDRAM_BSS wavemorph_scratch;

// K4 post filter stage: TPT state-variable filter (a local copy of explorer's ExpSVF).
struct WmSVF {
  float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 0.f;
  void Set(float fc, float Q, float sr) {
    const float g = tanf(3.14159265f * fc / sr);
    k = 1.f / Q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void CopyCoefFrom(const WmSVF& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
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

class WaveMorph : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch() values
  enum { kSU = 0, kSP = 1, kSD = 2, kSD2 = 3, kSlots = 4 };   // slots: up, played, down, down2

  void Init(float sr) override {
    using namespace wavemorph_k;
    sr8_ = sr / (float)kFactor;
    sr_ = sr;
    for (int i = 0; i < WM_SIN_SIZE; i++)
      wavemorph_sin_tab[i] = sinf(6.283185307f * (float)i / (float)WM_SIN_SIZE);
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;
    static const float r[kSlots] = { 2.f, 1.f, 0.5f, 0.25f };   // up / played / down / down2
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      {                                                    // armitage ARM_ALIGN fitted weight (played slot)
        float fc = f;
        if (fc < WM_ALIGN_W_FLO) fc = WM_ALIGN_W_FLO;
        if (fc > WM_ALIGN_W_FHI) fc = WM_ALIGN_W_FHI;
        const float x = log2f(fc);
        wmid_[k] = powf(10.f, (WM_ALIGN_WA * x * x + WM_ALIGN_WB * x + WM_ALIGN_WC) / 20.f);
      }
      d2th_[k] = 0.f;
      d2pp_[k] = -1.f;
      for (int o = 0; o < kSlots; o++) {
        const float fp = r[o] * f;                         // f'_k
        const int hk = (int)(WM_TAB_FMAX / fp);           // harmonics that fit below the ceiling
        int lv = 0;
        for (int l = 0; l < WM_LEVELS; l++) if (WM_LEVEL_H[l] <= hk) lv = l;
        lvl_[o][k] = (uint8_t)lv;
      }
    }
    BuildTables();
    for (int o = 0; o < kSlots; o++) { ResetSlotState(o); slot_on_[o] = false; }
    dc_a_ = expf(-6.283185307f * WM_DC_HZ / sr);
    dc_x1_ = dc_y1_ = 0.f;
    ctrl_a_ = 1.f - expf(-1000.f / (WM_CTRL_SMOOTH_MS * sr8_));
    zfade_len_ = (int)(WM_ZONE_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (zfade_len_ < 1) zfade_len_ = 1;
    zfade_n_ = 0;
    oct_step_ = 1.f / (WM_OCT_XFADE_MS * 0.001f * sr8_);
    zone_ = zone_t_ = zfrom_ = 0;
    k2s_ = k2_t_ = 0.5f;                         // K2 noon = envelope off
    k3s_ = k3_t_ = 0.f;
    k5s_ = k5_t_ = 0.f;
    // Onset detector (48 kHz, pre-decimator) + morph envelope (8 kHz), explorer's.
    on_env_ = on_gate_ = on_base_ = 0.f;
    on_env_a_  = 1.f - expf(-1000.f / (WM_ONSET_ENV_MS * sr));
    on_fall_a_ = 1.f - expf(-1000.f / (WM_ONSET_FALL_MS * sr));
    on_refr_ = 0;
    on_refr_len_ = (int)((float)WM_ONSET_REFRACTORY_MS * 0.001f * sr + 0.5f);
    on_pending_ = false;
    on_seen_ = false;
    env_.Reset();
    ln_tau_ratio_ = logf(WM_ENV_TAU_FAST_S / WM_ENV_TAU_SLOW_S);
    for (int i = 0; i < 3; i++) { og_[i] = og_t_[i] = 0.f; }
    og_[1] = og_t_[1] = 1.f;                     // SW2 MID = -1 slot
    // K4 post filter: bypass, cleared state.
    ft_ = ft_t_ = 0.f;
    f_on_ = false;
    f_hp_ = false;
    f_sz_ = 0;
    f_a_ = 0.f;
    f_m_ = 0.f;
    f_step_ = 1.f / (WM_FILT_SLIDE_MS * 0.001f * sr);
    f_lnhp_ = logf(WM_FILT_HP_MAX / WM_FILT_FMIN);
    f_lnlp_ = logf(WM_FILT_LP_MIN / WM_FILT_FMAX);
    for (int i = 0; i < 2; i++) svf_[i].Reset();
    mute_ = false;
    mute_g_ = 1.f;
    mute_step_ = 1.f / (WM_MUTE_SLIDE_MS * 0.001f * sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace wavemorph_k;
    if (cs.Foot(0).rising) mute_ = !mute_;   // FS1: wet mute toggle
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    int oct = cs.Switch(1);                   // SW1 is not read
    if (oct < kUp || oct > kDown) oct = kMid;
    zone_t_ = Quantize(k1, WM_ZONES);         // K1: stepped base wave
    k2_t_ = k2;
    k3_t_ = k3;
    k5_t_ = k5;
    ft_t_ = 2.f * k4 - 1.f;                   // K4 post filter t
    og_t_[0] = (oct == kUp)   ? 1.f : 0.f;   // SW2 UP   -> +1
    og_t_[1] = (oct == kMid)  ? 1.f : 0.f;   // SW2 MID  -> -1
    og_t_[2] = (oct == kDown) ? 1.f : 0.f;   // SW2 DOWN -> -2

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      DiagControls(cs, led1, kv, oct, Quantize(k1, WM_ZONES), env_.g);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;          // tap number (#n), 0 = automatic line
    float k[5];       // K1..K5 (remapped 0..1)
    int   zone;       // K1 zone 0..9
    int   sw2;        // 0 UP (+1), 1 MID (-1), 2 DOWN (-2)
    float m3, g, m;   // morph: K3, envelope gain g, effective m = K3 g
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
  static const char* ZoneName(int z) {
    static const char* const n[wavemorph_k::WM_ZONES] = { "SINE-SYNC", "TRI-FOLD", "SAW-RESO", "SQR-NEEDLE",
                                                          "PPG-GLASS", "PPG-AIR", "PPG-SPECTRAL", "VOCAL",
                                                          "FORMANT", "NOISE" };
    return (z >= 0 && z < wavemorph_k::WM_ZONES) ? n[z] : "?";
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace wavemorph_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      for (size_t j = 0; j < kFactor; j++) OnsetDetect(in[i + j]);   // onset detector, 48 kHz |in|, pre-decimator
      const float d = dec_(in + i);
      k2s_ += (k2_t_ - k2s_) * ca;              // K2: morph envelope knob
      if (fabsf(k2_t_ - k2s_) < WM_CTRL_SNAP) k2s_ = k2_t_;   // land exactly (noon: envelope fully off)
      EnvUpdate();                               // morph envelope (consumes the 48 kHz onset flag)
      k3s_ += (k3_t_ - k3s_) * ca;
      k5s_ += (k5_t_ - k5s_) * ca;
      if (fabsf(k5_t_ - k5s_) < WM_CTRL_SNAP) k5s_ = k5_t_;   // exact 0: octave work skipped
      for (int j = 0; j < 3; j++) og_[j] = Slide(og_[j], og_t_[j], oct_step_);

      // Zone change: start a crossfade (picked up when no fade is running).
      if (zfade_n_ == 0 && zone_t_ != zone_) {
        zfrom_ = zone_;
        zone_ = zone_t_;
        zfade_n_ = zfade_len_;
      }
      const bool fading = (zfade_n_ > 0);
      zg_ = fading ? 1.f - (float)zfade_n_ / (float)(zfade_len_ + 1) : 1.f;
      // Morph m = K3 g -> frame position (WM_FRAMES - 1) m, split into frame + fraction.
      {
        const float pos = (float)(WM_FRAMES - 1) * (k3s_ * env_.g);
        int f0 = (int)pos;
        if (f0 > WM_FRAMES - 2) f0 = WM_FRAMES - 2;
        if (f0 < 0) f0 = 0;
        fr0_ = f0;
        frf_ = pos - (float)f0;
      }

      // Pass 1: clock all bands, magnitudes (|z'| = |z| in every slot).
      float* __restrict mg = mag_ + 1;
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        const float yr = bands_[k].Re();
        const float yi = bands_[k].Im();
        const float n  = yr * yr + yi * yi;
        if (n < WM_SILENCE) {
          mg[k] = 0.f;
          inv_[k] = 0.f;
        } else {
          const float iv = armbands::ArmFastInvSqrt(n);
          inv_[k] = iv;
          mg[k] = n * iv;
        }
      }

      const float mix = k5s_;
      static constexpr int   oslot[3] = { kSU, kSD, kSD2 };
      static constexpr float ogain[3] = { WM_GAIN_UP, WM_GAIN_DOWN, WM_GAIN_DOWN2 };
      float acc = (1.f - mix) * RunSlot(kSP, fading);   // played slot: always computed
      for (int j = 0; j < 3; j++) {
        const float g = mix * og_[j];
        if (g != 0.f) acc += g * ogain[j] * RunSlot(oslot[j], fading);
        else slot_on_[oslot[j]] = false;
      }
      if (zfade_n_ > 0) --zfade_n_;
      int_(acc * WM_OUT_GAIN, wet + i);
    }
    DcBlock(wet, size);
    PostFilter(wet, size);  // K4: LP | bypass | HP, 24 dB/oct, Q 0.707
    Saturate(wet, size);
    ApplyMute(wet, size);
  }

 private:
  // Onset detector (explorer / armitage OnsetDetect, 1:1), once per 48 kHz sample on |in| before the
  // decimator; latches on_pending_ for EnvUpdate() (8 kHz).
  inline void OnsetDetect(float in_sample) {
    using namespace wavemorph_k;
    const float a = fabsf(in_sample);
    on_env_ += on_env_a_ * (a - on_env_);
    if (on_env_ > on_gate_) on_gate_ = on_env_;
    else                    on_gate_ += on_fall_a_ * (on_env_ - on_gate_);
    on_base_ += WM_ONSET_SLOW_COEF * (on_gate_ - on_base_);
    if (on_refr_ > 0) on_refr_--;
    const bool onset = (on_refr_ == 0 && on_gate_ > WM_ONSET_FLOOR &&
                        on_gate_ > on_base_ * WM_ONSET_RISE);
    if (onset) { on_refr_ = on_refr_len_; on_pending_ = true; }
  }

  // Morph envelope gain g (explorer EnvGain, 1:1). T = TIME_RATIO TAU_SLOW (FAST / SLOW)^|t|;
  //   CCW: onset -> g = 1, one-pole decay reaching 0 in DECAY_RATIO T, then holds 0.
  //   CW:  onset -> g = 0, one-pole rise reaching 1 in the same time, then holds 1.
  // "Reaching" = the one-pole's 98 % point (tau = travel / TRAVEL_TAUS), where g snaps to the end value.
  // Noon: off, g = 1. Before the first onset: CCW g = 1, CW g = 0.
  struct EnvGain {
    volatile float g = 1.f;
    float at = -1.f, dec_a = 0.f;
    int   phase = 0;               // 0 idle (holding), 1 decaying (CCW), 2 rising (CW)
    void Reset() { g = 1.f; at = -1.f; dec_a = 0.f; phase = 0; }
    inline void Step(bool onset, bool seen, float t2, float sr8, float ln_tau_ratio) {
      using namespace wavemorph_k;
      if (t2 == 0.f) { g = 1.f; phase = 0; return; }
      const bool cw = (t2 > 0.f);
      if (onset) { if (cw) { g = 0.f; phase = 2; } else { g = 1.f; phase = 1; } }
      if (!seen) { g = cw ? 0.f : 1.f; phase = 0; return; }
      const float am = fabsf(t2);
      if (am != at) {
        at = am;
        const float T = WM_ENV_TIME_RATIO * WM_ENV_TAU_SLOW_S * expf(am * ln_tau_ratio);
        dec_a  = 1.f - expf(-WM_ENV_TRAVEL_TAUS / (T * WM_ENV_DECAY_RATIO * sr8));
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
    env_.Step(onset, on_seen_, 2.f * k2s_ - 1.f, sr8_, ln_tau_ratio_);
  }

  // One 8 kHz sample of slot o: derive u_k / phi_k, return sum_k w_k a_k W_k (zone crossfade inside).
  float RunSlot(int o, bool fading) {
    if (!slot_on_[o]) { ResetSlotState(o); slot_on_[o] = true; }
    Prep(o);
    float sh = Shape(zone_, o);
    if (fading) sh = (1.f - zg_) * Shape(zfrom_, o) + zg_ * sh;
    return sh;
  }

  // u_k = z'_k / |z'_k| and the phase phi_k (cycles) for slot o (explorer's Prep, phase always needed).
  void Prep(int o) {
    const float* __restrict mg = mag_ + 1;
    for (int k = 0; k < kBands; k++) {
      if (mg[k] == 0.f) {
        if (o == kSD2) d2pp_[k] = -1.f;          // -2 accumulator restarts from the phase on return
        continue;
      }
      const float iv = inv_[k];
      float zr, zi;
      if (o == kSP) {
        zr = bands_[k].Re(); zi = bands_[k].Im();
        if (k & 1) { zr = -zr; zi = -zi; }        // phase alignment (armitage ARM_ALIGN)
      } else if (o == kSU) {
        bands_[k].ArmUp1(iv, zr, zi);
      } else if (o == kSD) {
        bands_[k].ArmDown1(iv, zr, zi);
      } else {                                    // -2: unwrapped band phase / 4 (cycles: theta in [0, 4))
        const float ph = FastAtan2Cycles(bands_[k].Im(), bands_[k].Re());   // raw z, as Down1
        float th = d2th_[k];
        const float pp = d2pp_[k];
        if (pp < 0.f) {
          th = ph;                                // (re)start from the current phase
        } else {
          float dph = ph - pp;                    // wrap into (-0.5, 0.5] cycles = (-pi, pi]
          if (dph > 0.5f) dph -= 1.f;
          else if (dph <= -0.5f) dph += 1.f;
          th += dph;
          if (th >= 4.f) th -= 4.f;               // mod 4 cycles = 8 pi: theta / 4 continuous
          else if (th < 0.f) th += 4.f;
        }
        d2th_[k] = th;
        d2pp_[k] = ph;
        ph_[k] = 0.25f * th;                      // the slot phase is known: no atan
        continue;
      }
      ph_[k] = FastAtan2Cycles(zi * iv, zr * iv);
    }
  }

  // sum_k w_k a_k W_k for zone z in slot o at the current frame position.
  float Shape(int z, int o) {
    using namespace wavemorph_k;
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict phs = ph_;
    const uint8_t* __restrict lv = lvl_[o];
    const bool played = (o == kSP);
    const int f0 = fr0_;
    const float ff = frf_;
    constexpr float kScale = 1.f / WM_I16_SCALE;
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) continue;
      const int16_t* t0 = wavemorph_tab[z][f0][lv[k]];
      const int16_t* t1 = wavemorph_tab[z][f0 + 1][lv[k]];
      const float xi = phs[k] * (float)WM_TAB_SIZE;
      const int   i0 = (int)xi;
      const float fr = xi - (float)i0;
      const int   j0 = i0 & (WM_TAB_SIZE - 1), j1 = (i0 + 1) & (WM_TAB_SIZE - 1);
      const float v0 = (float)t0[j0] + fr * (float)(t0[j1] - t0[j0]);
      const float v1 = (float)t1[j0] + fr * (float)(t1[j1] - t1[j0]);
      const float W = (v0 + ff * (v1 - v0)) * kScale;
      acc += (played ? wmid_[k] : 1.f) * a * W;
    }
    return acc;
  }

  void ResetSlotState(int o) {
    if (o == kSD2) for (int k = 0; k < kBands; k++) d2pp_[k] = -1.f;   // -2 accumulator: restart from the phase
  }

  static inline float Slide(float v, float t, float st) {
    if (v < t) { v += st; if (v > t) v = t; }
    else if (v > t) { v -= st; if (v < t) v = t; }
    return v;
  }

  // --- Table build -----------------------------------------------------------------------
  // Time-domain definition of zone z at morph u, phi = i / WM_TD_SIZE.
  static float TimeDomain(int z, float u, float ph) {
    using namespace wavemorph_k;
    switch (z) {
      case 0: {                                    // sine -> sync
        const float R = 1.f + (WM_SYNC_R_MAX - 1.f) * u;
        float ps = R * ph;
        ps -= floorf(ps);
        return sinf(6.283185307f * ps);
      }
      case 1: {                                    // triangle -> fold
        const float tri = (ph < 0.25f) ? 4.f * ph : (ph < 0.75f) ? 2.f - 4.f * ph : 4.f * ph - 4.f;
        const float v = (1.f + (WM_FOLD_G_MAX - 1.f) * u) * tri;
        float m = fmodf(v - 1.f, 4.f);
        if (m < 0.f) m += 4.f;
        return fabsf(m - 2.f) - 1.f;
      }
      case 2: {                                    // saw -> resonant saw
        const float R = 1.f + (WM_RSAW_R_MAX - 1.f) * u;
        const float mix = WM_RSAW_MIX_MAX * u;
        const float saw = 2.f * ph - 1.f;
        const float res = sinf(6.283185307f * R * ph) * (1.f - ph);
        return (1.f - mix) * saw + mix * res;
      }
      case 3: {                                    // square -> needle pulse
        const float w = 0.5f - (0.5f - WM_PWM_W_MIN) * u;
        return (ph < w) ? 1.f : -1.f;
      }
      default: {                                   // formant pulse (zone 9)
        const float Fr = WM_FORM_MIN * expf(u * logf(WM_FORM_MAX / WM_FORM_MIN));
        return 0.5f * (1.f - cosf(6.283185307f * ph)) * cosf(6.283185307f * Fr * ph);
      }
    }
  }

  // Spectral definition of zone z at morph u: A_h (sin), B_h (cos) for h = 1..WM_HMAX.
  static void Spectral(int z, float u, float* A, float* B, const float* rph, const float* rr) {
    using namespace wavemorph_k;
    for (int h = 0; h <= WM_HMAX; h++) { A[h] = 0.f; B[h] = 0.f; }
    auto put = [&](int h, float amp, float phase) {   // amp at phase (cycles): amp sin(2 pi (h phi + phase))
      if (h < 1 || h > WM_HMAX) return;
      A[h] += amp * cosf(6.283185307f * phase);
      B[h] += amp * sinf(6.283185307f * phase);
    };
    switch (z) {
      case 4: {                                    // PPG glass
        put(1, WM_GLASS_FUND, 0.f);
        const float c = WM_GLASS_C0 + (WM_GLASS_C1 - WM_GLASS_C0) * u;
        for (int h = 2; h <= WM_HMAX; h++) {
          float amp = 0.f;
          for (int j = 0; j < 3; j++) {
            const float e = ((float)h - c * WM_GLASS_RATIO[j]) / WM_GLASS_SIGMA;
            amp += WM_GLASS_A[j] * expf(-0.5f * e * e);
          }
          put(h, amp, rph[h]);
        }
        break;
      }
      case 5: {                                    // PPG air
        put(1, WM_AIR_FUND, 0.f);
        for (int j = 0; j < WM_AIR_N; j++) {
          int h = (int)(WM_AIR_BASE[j] * (1.f + WM_AIR_SPREAD * u) + 0.5f);
          h |= 1;                                  // odd partials only (hollow)
          if (h > WM_HMAX) h = WM_HMAX;
          put(h, WM_AIR_PART, 0.f);
        }
        break;
      }
      case 6: {                                    // PPG spectral: golden-ratio comb through 1/sqrt(h)
        for (int h = 1; h <= WM_HMAX; h++) {
          float dh = (float)h * WM_SPEC_GOLD;
          dh = (dh - floorf(dh)) / WM_SPEC_KEEP;
          if (h > 1 && u >= dh) continue;          // this partial has switched off
          put(h, powf((float)h, -WM_SPEC_TILT), rph[h]);
        }
        break;
      }
      case 7: {                                    // vocal a -> e -> i -> o -> u
        const float v = u * (float)(WM_VOX_N - 1);
        int v0 = (int)v;
        if (v0 > WM_VOX_N - 2) v0 = WM_VOX_N - 2;
        const float vf = v - (float)v0;
        float F[3];
        for (int f = 0; f < 3; f++) F[f] = WM_VOX_F[v0][f] + vf * (WM_VOX_F[v0 + 1][f] - WM_VOX_F[v0][f]);
        for (int h = 1; h <= WM_HMAX; h++) {
          const float fh = (float)h * WM_VOX_F0;
          float amp = 0.f;
          for (int f = 0; f < 3; f++) {
            const float e = (fh - F[f]) / (0.5f * WM_VOX_BW[f]);
            amp += WM_VOX_A[f] / (1.f + e * e);
          }
          put(h, amp, 0.f);
        }
        break;
      }
      default: {                                   // noise (zone 10)
        for (int h = 1; h <= WM_HMAX; h++) {
          const float amp = powf((float)h, -(1.f - u)) * (1.f + u * (rr[h] - 1.f));
          put(h, amp, rph[h]);
        }
        break;
      }
    }
  }

  // All zones and frames: definition -> (DFT) -> mean removed, peak 1 -> 14 band-limited int16 levels.
  void BuildTables() {
    using namespace wavemorph_k;
    constexpr int N = WM_TD_SIZE;
    constexpr int M = N - 1;
    constexpr int Q = N / 4;                     // quarter period (cos offset)
    constexpr int S = N / WM_TAB_SIZE;           // table step in the sin table
    float* x   = wavemorph_scratch.x;
    float* acc = wavemorph_scratch.acc;
    float* A   = wavemorph_scratch.A;
    float* B   = wavemorph_scratch.B;
    float* rph = wavemorph_scratch.rph;
    float* rr  = wavemorph_scratch.rr;
    const float* tab = wavemorph_sin_tab;
    uint32_t seed = WM_SEED;
    auto rnd01 = [&]() {                           // uniform in [0, 1)
      seed = seed * 1664525u + 1013904223u;
      return (float)(seed >> 8) * (1.f / 16777216.f);
    };
    for (int h = 0; h <= WM_HMAX; h++) { rph[h] = rnd01(); rr[h] = rnd01(); }
    for (int z = 0; z < WM_ZONES; z++) {
      const bool spectral = (z >= 4 && z <= 7) || z == 9;
      for (int f = 0; f < WM_FRAMES; f++) {
        const float u = (float)f / (float)(WM_FRAMES - 1);
        if (!spectral) {
          // 1) time domain, mean removed, peak 1
          float mean = 0.f;
          for (int i = 0; i < N; i++) { x[i] = TimeDomain(z, u, (float)i / (float)N); mean += x[i]; }
          mean /= (float)N;
          float pk = 0.f;
          for (int i = 0; i < N; i++) { x[i] -= mean; if (fabsf(x[i]) > pk) pk = fabsf(x[i]); }
          const float g = (pk > 0.f) ? 1.f / pk : 0.f;
          // 2) DFT: x = sum_h A_h sin + B_h cos (peak scale folded in)
          for (int h = 1; h <= WM_HMAX; h++) {
            float a = 0.f, b = 0.f;
            int idx = 0;
            for (int i = 0; i < N; i++) {
              a += x[i] * tab[idx];
              b += x[i] * tab[(idx + Q) & M];
              idx = (idx + h) & M;
            }
            A[h] = a * g * (2.f / (float)N);
            B[h] = b * g * (2.f / (float)N);
          }
        } else {
          // 1) harmonic definition; peak of the full-band frame (no DC by construction)
          Spectral(z, u, A, B, rph, rr);
          float pk = 0.f;
          for (int i = 0; i < N; i++) {
            float v = 0.f;
            int idx = 0;
            for (int h = 1; h <= WM_HMAX; h++) {
              idx = (idx + i) & M;                 // h i mod N
              v += A[h] * tab[idx] + B[h] * tab[(idx + Q) & M];
            }
            if (fabsf(v) > pk) pk = fabsf(v);
          }
          const float g = (pk > 0.f) ? 1.f / pk : 0.f;
          for (int h = 1; h <= WM_HMAX; h++) { A[h] *= g; B[h] *= g; }
        }
        // 3) band-limited levels (cumulative), 256 points
        for (int i = 0; i < WM_TAB_SIZE; i++) acc[i] = 0.f;
        int hdone = 0;
        for (int l = 0; l < WM_LEVELS; l++) {
          const int ht = WM_LEVEL_H[l];
          for (int h = hdone + 1; h <= ht; h++) {
            const float a = A[h], b = B[h];
            if (a == 0.f && b == 0.f) continue;
            int idx = 0;
            const int step = (h * S) & M;
            for (int i = 0; i < WM_TAB_SIZE; i++) {
              acc[i] += a * tab[idx] + b * tab[(idx + Q) & M];
              idx = (idx + step) & M;
            }
          }
          hdone = ht;
          int16_t* dst = wavemorph_tab[z][f][l];
          for (int i = 0; i < WM_TAB_SIZE; i++) {
            float v = acc[i] * WM_I16_SCALE;
            v = (v > 32767.f) ? 32767.f : (v < -32768.f) ? -32768.f : v;   // int16 range (storage)
            dst[i] = (int16_t)lrintf(v);
          }
        }
      }
    }
  }

  // Wet DC blocker (explorer's).
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

  // K4 post filter on the final 48 kHz wet (explorer's PostFilter without the UP bypass).
  void PostFilter(float* wet, size_t size) {
    using namespace wavemorph_k;
    if ((int)size != f_sz_) {
      f_sz_ = (int)size;
      f_a_ = 1.f - expf(-(float)size / (WM_FILT_SMOOTH_MS * 0.001f * sr_));
    }
    const float tt = ft_t_;
    float ts = ft_ + (tt - ft_) * f_a_;
    if (tt == 0.f && fabsf(ts) < WM_FILT_SNAP) ts = 0.f;
    ft_ = ts;
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
    float mt = at * (1.f / WM_FILT_FADE);
    if (mt > 1.f) mt = 1.f;
    if (ts == 0.f || want_hp != f_hp_) mt = 0.f;
    {
      const float tmag = expf(WM_FILT_CURVE * logf((at > 1e-6f) ? at : 1e-6f));   // |t|^curve
      float fc = f_hp_ ? WM_FILT_FMIN * expf(tmag * f_lnhp_)
                       : WM_FILT_FMAX * expf(tmag * f_lnlp_);
      fc = (fc < WM_FILT_FC_MIN) ? WM_FILT_FC_MIN : (fc > WM_FILT_FC_MAX) ? WM_FILT_FC_MAX : fc;
      svf_[0].Set(fc, WM_FILT_Q, sr_);
      svf_[1].CopyCoefFrom(svf_[0]);
    }
    float m = f_m_;
    const float st = f_step_;
    WmSVF& s0 = svf_[0];
    WmSVF& s1 = svf_[1];
    if (f_hp_) {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        s1.Process(hp, lp, y);
        wet[n] = x + m * (y - x);
      }
    } else {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        s1.Process(lp, y, hp);
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

  // Wet soft clipper: WM_SAT_TRIM * WM_SAT_T * tanh(x / WM_SAT_T) (Pade(3,3) tanh).
  void Saturate(float* wet, size_t size) {
    using namespace wavemorph_k;
    constexpr float kIn  = 1.f / WM_SAT_T;
    constexpr float kOut = WM_SAT_T * WM_SAT_TRIM;
    for (size_t n = 0; n < size; n++) wet[n] = kOut * PadeTanh(wet[n] * kIn);
  }
  static inline float PadeTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }

  // FS1 wet mute (explorer's).
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
                    int oct, int zone, float g) {
    using namespace wavemorph_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.zone = zone; s.sw2 = oct;
    s.m3 = kv[2]; s.g = g; s.m = kv[2] * g;
    s.mute = mute_;
    if (cs.Foot(0).rising) {
      s.n = ++diag_n_;
      diag_tap_ = s;
      diag_tap_pend_ = true;
      diag_blink_t_ = now;
      diag_blink_ = true;
    }
    if (diag_blink_) {
      if (now - diag_blink_t_ < WM_DIAG_BLINK_MS) led1.Set(0.f);
      else diag_blink_ = false;
    }
    bool moved = (oct != diag_ref_sw2_);
    for (int i = 0; i < 5; i++) if (fabsf(kv[i] - diag_ref_k_[i]) > WM_DIAG_KNOB_EPS) moved = true;
    if (moved) {
      for (int i = 0; i < 5; i++) diag_ref_k_[i] = kv[i];
      diag_ref_sw2_ = oct;
      diag_changed_ = true;
      diag_t_ = now;
    } else if (diag_changed_ && now - diag_t_ >= WM_DIAG_SETTLE_MS) {
      diag_changed_ = false;
      s.n = 0;
      diag_auto_ = s;
      diag_auto_pend_ = true;
    }
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
  float ph_[kBands];               // phase phi_k (cycles, [0, 1)) of the slot being run
  float wmid_[kBands];             // played-slot fitted output weight (armitage ARM_ALIGN)
  float d2th_[kBands], d2pp_[kBands];  // -2 slot: unwrapped phase (cycles, mod 4), previous band phase (-1 = fresh)
  uint8_t lvl_[kSlots][kBands];    // table band-limit level per band and slot
  bool  slot_on_[kSlots] = { false, false, false, false };   // slot computed last sample

  float dc_a_ = 0.f, dc_x1_ = 0.f, dc_y1_ = 0.f;
  float sr8_ = 8000.f, sr_ = 48000.f;
  float ctrl_a_ = 0.f;
  int   zone_ = 0, zfrom_ = 0;     // current / fading-out zone
  volatile int zone_t_ = 0;        // target zone (K1, Controls)
  int   zfade_n_ = 0, zfade_len_ = 8;
  float zg_ = 1.f;                 // new-zone gain during a fade
  int   fr0_ = 0;                  // frame below the morph position (this sample)
  float frf_ = 0.f;                // fraction toward fr0_ + 1
  float k2s_ = 0.5f, k3s_ = 0.f, k5s_ = 0.f;                      // smoothed knobs
  volatile float k2_t_ = 0.5f, k3_t_ = 0.f, k5_t_ = 0.f;          // targets
  float og_[3] = { 0.f, 1.f, 0.f };            // octave slot weights (up, down, down2), slid
  volatile float og_t_[3] = { 0.f, 1.f, 0.f };
  float oct_step_ = 0.f;
  // K2 morph envelope
  float on_env_ = 0.f, on_gate_ = 0.f, on_base_ = 0.f;   // fast meter, gate meter, slow baseline (48 kHz)
  float on_env_a_ = 0.f, on_fall_a_ = 0.f;               // meter rise / gate fall coefficients (48 kHz)
  int   on_refr_ = 0, on_refr_len_ = 4800;               // refractory samples left / length (48 kHz)
  bool  on_pending_ = false;                             // an onset fired, not yet consumed by EnvUpdate()
  bool  on_seen_ = false;                                // an onset has happened
  EnvGain env_;
  float ln_tau_ratio_ = 0.f;       // ln(WM_ENV_TAU_FAST_S / WM_ENV_TAU_SLOW_S)
  // K4 post filter
  WmSVF svf_[2];
  float ft_ = 0.f;                 // smoothed t (per block)
  volatile float ft_t_ = 0.f;      // target t = 2 k4 - 1
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
  int      diag_ref_sw2_ = -1;
};
