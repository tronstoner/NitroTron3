#pragma once
//
// wavemorph — vector-synthesis wavetable voice on the band engine.  SW3 DOWN on NitroLab.
//
// The band engine is COPIED from explorer.h (not shared): decimate 48 -> 8 kHz, 84 complex bands
// (armitage_bands.h, included read-only), slots played / +1 / -1 (SW2 picks one, as armitage), band phase
// computed once per band / slot / sample in Prep; each band reads
// the current zone's frame tables at its OWN phase phi_k with its magnitude a_k:
//   W_k = lerp_frames( lerp_phase(table[zone][frame][level_k]) ),  slot = sum_k w_k a_k W_k
//   MORPH (all zones, m = K3): F_z frames per zone (7 in zone 1, 16 in zones 4 / 5, 4 in the others);
//   position = (F_z - 1) m, linear crossfade between the two adjacent frames;
//   level_k = the largest band-limited version with H <= 4000 / f'_k (static per band and slot).
// wet8 = WM_OUT_GAIN [ u G_UP up + p played + d G_DOWN down ], (u, p, d) one-hot from SW2, slid over
//        WM_OCT_XFADE_MS on a change (old -> new slot crossfade); a slot with weight 0 is not computed
// wet  = interpolate(wet8) -> DC blocker -> K4 post filter -> soft clipper (ceiling 1.0) -> FS1 mute
// No pitch tracking; nothing picks or decides per band.
//
// CONTROL MAP
//   K1  base wave     10 STEPPED equal zones (below); a zone change crossfades old -> new over
//                     WM_ZONE_XFADE_MS (the voice is stateless: the entering zone starts clean)
//   K2  filter env    armitage's K5 filter envelope on the K4 position (all zones): t2 = 2 k2 - 1, off
//                     inside the pad (k2 0.46..0.54); CW: each onset e rises toward +1 (tau), at e >= 0.98
//                     falls back to 0 (tau x 3); CCW: each onset e = 0 -> -1 and holds; tau 4 s (pad edge)
//                     -> 0.05 s (ends); t_eff = t_K4 + 0.7 e, not clamped (fc only 10 Hz..20 kHz)
//                     + LIVE FOLLOWER on top (2026-10-26, armitage's): EnvFollower (rectifier x WM_FOLLOW_GAIN
//                     -> 4-pole LP WM_FOLLOW_HZ) on the raw 48 kHz input next to the onset detector,
//                     f = min(follower, 1) per 8 kHz step; t_eff += WM_FOLLOW_DEPTH dir f, dir slid (one-pole,
//                     WM_FOLLOW_GATE_MS) to sign(t2) outside the pad (CW harder opens, CCW harder closes), 0
//                     inside it. WM_FOLLOW_DEPTH = 0: bit-identical.
//   K3  morph         m 0..1 (smoothed): position through the zone's waves / frames
//   K4  post filter   CCW LP 180 Hz <- 4 kHz | noon bypass | CW HP 40 Hz -> 4 kHz (WM_FILT_STAGES: 1 = 12 dB/oct, 2 = 24 dB/oct; Q 0.707 per stage)
//   K5  unused (not read; DIAG shows the raw value)
//   K6  dry / wet     (shell)
//   SW1 unused (not read)
//   SW2 octave        UP = +1 octave, MID = as played, DOWN = -1 octave (one slot; crossfade WM_OCT_XFADE_MS)
//   FS1 wet mute      toggle, LED2 on while muted; LED1 on. FS2 unused.
//   DIAG builds: FS1 tap also = numbered "note this setting" line; `auto` line after settling.
//
// TABLES (built at Init into SDRAM, int16, packed): per zone F_z frames (frame 1 = the exact base wave),
// u = frame / (F_z - 1). Each frame: defined in the time domain at WM_TD_SIZE (1024) points and analysed by
// DFT into harmonics 1..127 [time], or defined directly by harmonic amplitudes, sine phase [spectral];
// mean removed, peak normalised to 1 (an all-silent frame stays silent); zones 1, 2, 6, 10 re-phased to the
// saw's phase convention (sine phase, positive; magnitudes kept, then their level rule again); zones 3 and 6..10 then scaled
// to the RMS of the plain saw (zone 3 corner 1, full band), then capped to the int16 storage headroom
// (32767 / WM_ZONE_I16_SCALE[zone]); then rebuilt as 14 band-limited versions {1,2,3,4,6,8,12,16,24,32,48,64,96,127} at 256 points.
//   1 SINE SYNC  7 frames: sin(2 pi frac(R phi)), R = 1 | 1.5 | 2 | 2.7 | 4 | 4.6 | 6.5             [time]
//   2 TRI FOLD   fold(G tri(phi)), G = 1 | 2 | 4 | 8, fold(v) = |((v - 1) mod 4) - 2| - 1           [time]
//   3 SAW ROUGH  3 frames: saw 1/n | sine + needle (sin + Hann spike width 0.03 at phi 0.25, height 1;
//                re-phased) | needle saw 1/n^0.3; equal RMS (the plain saw's)      [spectral; 2 time]
//   4 PWM        16 frames: +1 (phi < w) else -1, w = 0.5 (1 - u)^2.2: 50 % -> 0 % (silence)          [time]
//   5 VOCAL      16 frames (= dd3d5f9): a -> e -> i -> o -> u, formants interpolated, 110 Hz reference [spectral]
//   6 PPG CLASSIC explorer's PPG-style set: 8-step saw (8 levels) | stepped odd-harmonic stack (32 steps,
//                16 levels) | bit pattern ((s 5) ^ (s >> 2)) & 15, 32 steps | 16-step held sine        [time]
//   7 SOFT A     tine e-piano | hollow clarinet | drawbar organ | church mixture                   [spectral]
//   8 SOFT B     harmonium | ghost fifths | glass bell | stepped lo-res (sine held in 6 steps) [spectral; 4 time]
//   9 HARD A     PPG resonant | nasal oboe reed | formant brass | bass bell 7/14                  [spectral]
//  10 HARD B     sync saw (3.5x, reset per cycle) | fuzz reed | clipper | PPG feedback (saw 1/n
//                without harmonics 1, 2)                                                    [1 time; spectral]
//   (harmonic amplitude lists in wavemorph_constants.h)
//
// Header-only; main.cpp includes daisy.h / hothouse.h / control_surface.h / module.h first.
// No dynamic allocation. Tables: 64 frames x 14 x 256 int16 = 448 KB in SDRAM.

#include <cmath>
#include <cstdint>
#include "module.h"
#include "knob_map.h"          // RemapKnob, Quantize
#include "armitage_bands.h"    // band engine (read-only use)
#include "wavemorph_constants.h"
#include "env_follower.h"      // EnvFollower (live follower on top of the filter envelope)

#ifndef DTCM_MEM_SECTION
#define DTCM_MEM_SECTION       // host builds
#endif
#ifndef DSY_SDRAM_BSS
#define DSY_SDRAM_BSS          // host builds
#endif

// Sin table (filled in Init; DTCM = zero-wait-state reads). Size = WM_TD_SIZE: table builds index it exactly.
static float DTCM_MEM_SECTION wavemorph_sin_tab[wavemorph_k::WM_SIN_SIZE];

// Frame tables, packed: [zone offset + frame][level][sample], int16 (scale WM_ZONE_I16_SCALE[zone]), SDRAM.
static int16_t DSY_SDRAM_BSS wavemorph_tab[wavemorph_k::WM_TOTAL_FRAMES]
                                          [wavemorph_k::WM_LEVELS][wavemorph_k::WM_TAB_SIZE];
// Init-only build scratch (SDRAM: keeps DTCM free).
struct WaveMorphScratch {
  float x[wavemorph_k::WM_TD_SIZE];                          // time-domain frame
  float acc[wavemorph_k::WM_TAB_SIZE];                       // level accumulator
  float A[wavemorph_k::WM_HMAX + 1], B[wavemorph_k::WM_HMAX + 1];   // sin / cos coefficients
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
  enum { kSU = 0, kSP = 1, kSD = 2, kSlots = 3 };   // slots: up, played, down (SW2 selects one)

  void Init(float sr) override {
    using namespace wavemorph_k;
    sr8_ = sr / (float)kFactor;
    sr_ = sr;
    for (int i = 0; i < WM_SIN_SIZE; i++)
      wavemorph_sin_tab[i] = sinf(6.283185307f * (float)i / (float)WM_SIN_SIZE);
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;
    static const float r[kSlots] = { 2.f, 1.f, 0.5f };          // up / played / down
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
    // Onset detector (48 kHz, pre-decimator, explorer's) + K2 filter envelope (8 kHz).
    on_env_ = on_gate_ = on_base_ = 0.f;
    on_env_a_  = 1.f - expf(-1000.f / (WM_ONSET_ENV_MS * sr));
    on_fall_a_ = 1.f - expf(-1000.f / (WM_ONSET_FALL_MS * sr));
    on_refr_ = 0;
    on_refr_len_ = (int)((float)WM_ONSET_REFRACTORY_MS * 0.001f * sr + 0.5f);
    on_pending_ = false;
    on_seen_ = false;
    fenv_.Reset();
    ln_ftau_ratio_ = logf(WM_FENV_TAU_FAST_S / WM_FENV_TAU_SLOW_S);
    // Live follower (48 kHz, raw input) + its pad gate (8 kHz).
    fol_.Init(sr);
    fol_.SetCutoff(WM_FOLLOW_HZ);
    fol_.SetPreGain(WM_FOLLOW_GAIN);
    fol_y_ = fol_f_ = fol_dir_ = 0.f;
    fol_gate_a_ = 1.f - expf(-1000.f / (WM_FOLLOW_GATE_MS * sr8_));
    for (int i = 0; i < 3; i++) { og_[i] = og_t_[i] = 0.f; }
    og_[1] = og_t_[1] = 1.f;                     // SW2 MID = played slot
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
    const int zone = Quantize(k1, WM_ZONES);  // K1: stepped base wave
    zone_t_ = zone;
    k2_t_ = k2;
    k3_t_ = k3;
    ft_t_ = 2.f * k4 - 1.f;                   // K4 post filter t
    og_t_[0] = (oct == kUp)   ? 1.f : 0.f;   // SW2 UP   -> +1 slot
    og_t_[1] = (oct == kMid)  ? 1.f : 0.f;   // SW2 MID  -> played slot
    og_t_[2] = (oct == kDown) ? 1.f : 0.f;   // SW2 DOWN -> -1 slot

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      DiagControls(cs, led1, kv, oct, zone, fenv_.e, fol_f_);
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;          // tap number (#n), 0 = automatic line
    float k[5];       // K1..K5 (remapped 0..1)
    int   zone;       // K1 zone 0..9
    int   f0;         // morph position: frame below (0-based) ...
    float ff;         // ... and the fraction toward f0 + 1
    float fe;         // K2 filter envelope e (-1..1)
    float fol;        // live follower f = min(follower, 1) at the snapshot (0..1)
    int   sw2;        // 0 UP (+1), 1 MID (-1), 2 DOWN (-2)
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
    static const char* const n[wavemorph_k::WM_ZONES] = { "SINE-SYNC", "TRI-FOLD", "SAW-ROUGH", "SQR-PWM",
                                                          "VOCAL", "PPG-CLASSIC", "SOFT-A", "SOFT-B",
                                                          "HARD-A", "HARD-B" };
    return (z >= 0 && z < wavemorph_k::WM_ZONES) ? n[z] : "?";
  }
  // Name of frame i of zone z (zone 1: the sync ratio; 4-wave zones), "" for the 16-frame zones (by number).
  static const char* WaveName(int z, int i) {
    static const char* const sync[wavemorph_k::WM_SYNC_N] = { "R1", "R1.5", "R2", "R2.7", "R4", "R4.6", "R6.5" };
    if (z == 0) return (i >= 0 && i < wavemorph_k::WM_SYNC_N) ? sync[i] : "";
    static const char* const sawr[wavemorph_k::WM_SAWR_N] = { "saw", "sinendl", "needle" };
    if (z == 2) return (i >= 0 && i < wavemorph_k::WM_SAWR_N) ? sawr[i] : "";
    static const char* const n[wavemorph_k::WM_ZONES][wavemorph_k::WM_CORNERS] = {
      { "", "", "", "" }, { "G1", "G2", "G4", "G8" }, { "", "", "", "" },
      { "", "", "", "" }, { "", "", "", "" }, { "saw8", "odd32", "bits", "sine16" },
      { "tine", "clarinet", "drawbar", "mixture" }, { "harmonium", "ghost5", "bell", "lores" },
      { "ppgres", "oboe", "brass", "bassbell" }, { "syncsaw", "fuzz", "clipper", "ppgfb" } };
    if (z < 0 || z >= wavemorph_k::WM_ZONES || i < 0 || i >= wavemorph_k::WM_CORNERS) return "";
    if (wavemorph_k::WM_ZONE_FRAMES[z] != wavemorph_k::WM_CORNERS) return "";
    return n[z][i];
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace wavemorph_k;
    const float ca = ctrl_a_;
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      for (size_t j = 0; j < kFactor; j++) {
        OnsetDetect(in[i + j]);                  // onset detector, 48 kHz |in|, pre-decimator
        fol_y_ = fol_.Process(in[i + j]);        // live follower, 48 kHz raw input
      }
      const float d = dec_(in + i);
      k2s_ += (k2_t_ - k2s_) * ca;              // K2: filter envelope knob
      if (fabsf(k2_t_ - k2s_) < WM_CTRL_SNAP) k2s_ = k2_t_;   // land exactly
      EnvUpdate();                               // filter envelope (consumes the 48 kHz onset flag)
      k3s_ += (k3_t_ - k3s_) * ca;
      for (int j = 0; j < 3; j++) og_[j] = Slide(og_[j], og_t_[j], oct_step_);

      // Zone change: start a crossfade (picked up when no fade is running).
      if (zfade_n_ == 0 && zone_t_ != zone_) {
        zfrom_ = zone_;
        zone_ = zone_t_;
        zfade_n_ = zfade_len_;
      }
      const bool fading = (zfade_n_ > 0);
      zg_ = fading ? 1.f - (float)zfade_n_ / (float)(zfade_len_ + 1) : 1.f;
      morph_ = k3s_;                             // morph m = K3 (frame position per zone in Shape)

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

      // SW2: one voice (up / played / down); on a change the old and the new slot crossfade
      // (weights slid over WM_OCT_XFADE_MS); a slot with weight 0 is not computed.
      static constexpr int   oslot[3] = { kSU, kSP, kSD };
      static constexpr float ogain[3] = { WM_GAIN_UP, 1.f, WM_GAIN_DOWN };
      float acc = 0.f;
      for (int j = 0; j < 3; j++) {
        const float g = og_[j];
        if (g != 0.f) acc += g * ogain[j] * RunSlot(oslot[j], fading);
        else slot_on_[oslot[j]] = false;
      }
      if (zfade_n_ > 0) --zfade_n_;
      int_(acc * WM_OUT_GAIN, wet + i);
    }
    DcBlock(wet, size);
    PostFilter(wet, size);  // K4: LP | bypass | HP, WM_FILT_STAGES x 12 dB/oct, Q WM_FILT_Q
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

  // K2: filter envelope e (armitage's Env, 1:1, plus the noon pad WM_ENV_PAD).
  struct FEnv {
    volatile float e = 0.f;        // envelope value (-1..1)
    float at = -1.f, a = 0.f;      // |t2| the coefficients were computed for; rise / CCW one-pole coefficient
    float fall_a = 0.f;            // CW fall one-pole coefficient (tau x WM_FENV_FALL_RATIO)
    bool  attacking = false;       // CW only: true while e is in its rise
    void Reset() { e = 0.f; at = -1.f; a = 0.f; fall_a = 0.f; attacking = false; }
    inline void Step(bool onset, bool seen, float t2, float sr8, float ln_tau_ratio) {
      using namespace wavemorph_k;
      if (onset) { e = 0.f; attacking = (t2 > 0.f); }
      const float am = fabsf(t2);
      if (am <= WM_ENV_PAD || !seen) { e = 0.f; return; }
      if (am != at) {
        at = am;
        const float sx = (am - WM_ENV_PAD) / (1.f - WM_ENV_PAD);
        const float tau = WM_FENV_TAU_SLOW_S * expf(sx * ln_tau_ratio);
        a = 1.f - expf(-1.f / (tau * sr8));
        fall_a = 1.f - expf(-1.f / (tau * WM_FENV_FALL_RATIO * sr8));
      }
      if (t2 > 0.f) {
        if (attacking) {                         // CW rise toward +1
          e += a * (1.f - e);
          if (e >= WM_FENV_PEAK) attacking = false;
        } else {
          e += fall_a * (0.f - e);               // CW fall toward 0 (tau x FALL_RATIO)
        }
      } else {
        e += a * (-1.f - e);                     // CCW: toward -1 and hold
      }
    }
  };

  inline void EnvUpdate() {
    const bool onset = on_pending_;
    on_pending_ = false;
    if (onset) on_seen_ = true;
    fenv_.Step(onset, on_seen_, 2.f * k2s_ - 1.f, sr8_, ln_ftau_ratio_);    // K4 filter envelope
    // Live follower on top: f = min(follower, 1); dir slides toward sign(t2) outside the pad, 0 inside, lands exactly.
    using namespace wavemorph_k;
    fol_f_ = (fol_y_ < 1.f) ? fol_y_ : 1.f;
    const float t2 = 2.f * k2s_ - 1.f;
    const float dir_t = (t2 > WM_ENV_PAD) ? 1.f : (t2 < -WM_ENV_PAD) ? -1.f : 0.f;
    fol_dir_ += fol_gate_a_ * (dir_t - fol_dir_);
    if (fabsf(dir_t - fol_dir_) < WM_CTRL_SNAP) fol_dir_ = dir_t;
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
      ph_[k] = FastAtan2Cycles(zi * iv, zr * iv);
    }
  }

  // Morph position of zone z: frame below + fraction, position (F_z - 1) m.
  static inline void MorphPos(int z, float m, int& f0, float& ff) {
    using namespace wavemorph_k;
    const int nf = WM_ZONE_FRAMES[z];
    const float pos = (float)(nf - 1) * m;
    f0 = (int)pos;
    if (f0 > nf - 2) f0 = nf - 2;
    if (f0 < 0) f0 = 0;
    ff = pos - (float)f0;
  }

  // sum_k w_k a_k W_k for zone z in slot o at the current morph: position (F_z - 1) m.
  float Shape(int z, int o) {
    using namespace wavemorph_k;
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict phs = ph_;
    const uint8_t* __restrict lv = lvl_[o];
    const bool played = (o == kSP);
    int f0;
    float ff;
    MorphPos(z, morph_, f0, ff);
    const int fb = zoff_[z] + f0;              // packed frame index
    const int fn = fb + 1;                     // neighbour (f0 <= F_z - 2)
    const float kScale = 1.f / WM_ZONE_I16_SCALE[z];
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) continue;
      const int16_t* t0 = wavemorph_tab[fb][lv[k]];
      const int16_t* t1 = wavemorph_tab[fn][lv[k]];
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

  void ResetSlotState(int o) { (void)o; }   // the voice keeps no per-slot state

  static inline float Slide(float v, float t, float st) {
    if (v < t) { v += st; if (v > t) v = t; }
    else if (v > t) { v -= st; if (v < t) v = t; }
    return v;
  }

  // --- Table build -----------------------------------------------------------------------
  // Time-domain definition of zone z, frame c (u = c / (F_z - 1)), phi = i / WM_TD_SIZE.
  static float TimeDomain(int z, int c, float u, float ph) {
    using namespace wavemorph_k;
    switch (z) {
      case 0: {                                    // sine | sync
        float ps = WM_SYNC_R[c] * ph;
        ps -= floorf(ps);
        return sinf(6.283185307f * ps);
      }
      case 1: {                                    // triangle | fold
        const float tri = (ph < 0.25f) ? 4.f * ph : (ph < 0.75f) ? 2.f - 4.f * ph : 4.f * ph - 4.f;
        const float v = WM_FOLD_G[c] * tri;
        float m = fmodf(v - 1.f, 4.f);
        if (m < 0.f) m += 4.f;
        return fabsf(m - 2.f) - 1.f;
      }
      case 2: {                                    // saw rough 2: sine + needle (Hann spike on the peak)
        float x = sinf(6.283185307f * ph);
        const float d = ph - WM_SNDL_POS;
        if (fabsf(d) < 0.5f * WM_SNDL_W) x += WM_SNDL_H * 0.5f * (1.f + cosf(6.283185307f * d / WM_SNDL_W));
        return x;
      }
      case 3:                                      // PWM 50 % -> silence (curve: narrow end)
        return (ph < PwmWidth(u)) ? 1.f : -1.f;
      case 5:                                      // PPG classic (explorer's PPG-style set, s == 1)
        switch (c) {
          case 0: {                                // saw: 8 steps, 8 levels
            const int st = (int)(ph * (float)WM_PPG_SAW_STEPS);
            return 1.f - 2.f * (float)st / (float)(WM_PPG_SAW_STEPS - 1);
          }
          case 1: {                                // odd-harmonic stack, 32 steps, 16 levels
            const float xs = floorf(ph * (float)WM_PPG_ODD_STEPS) / (float)WM_PPG_ODD_STEPS;
            const float y = sinf(6.283185307f * xs) + WM_PPG_ODD_A3 * sinf(6.283185307f * 3.f * xs)
                          + WM_PPG_ODD_A5 * sinf(6.283185307f * 5.f * xs);
            return roundf(y * WM_PPG_ODD_LEVELS) / WM_PPG_ODD_LEVELS;
          }
          case 2: {                                // bit pattern ((s 5) ^ (s >> 2)) & 15 over 32 steps
            const int st = (int)(ph * (float)WM_PPG_BIT_STEPS);
            return (float)(((st * 5) ^ (st >> 2)) & 15) / 7.5f - 1.f;
          }
          default:                                 // sine held in 16 steps
            return sinf(6.283185307f * floorf(ph * (float)WM_PPG_SINE_STEPS) / (float)WM_PPG_SINE_STEPS);
        }
      case 7:                                      // soft B 4: stepped lo-res sine (held per step)
        return sinf(6.283185307f * floorf((float)WM_LORES_STEPS * ph) / (float)WM_LORES_STEPS);
      default: {                                   // hard B 1: sync saw, reset each cycle
        float ps = WM_SYNCSAW_R * ph;
        ps -= floorf(ps);
        return 2.f * ps - 1.f;
      }
    }
  }
  static inline bool IsTimeDomain(int z, int c) {
    return z == 0 || z == 1 || z == 3 || z == 5 || (z == 2 && c == 1) || (z == 7 && c == 3) || (z == 9 && c == 0);
  }

  // PWM width at u: 0.5 (1 - u)^WM_PWM_CURVE (u = 1: 0, silence).
  static inline float PwmWidth(float u) {
    using namespace wavemorph_k;
    return (u >= 1.f) ? 0.f : 0.5f * powf(1.f - u, WM_PWM_CURVE);
  }

  // Spectral definition of zone z, frame c (u = c / (F_z - 1)): A_h (sin), B_h (cos) for h = 1..WM_HMAX.
  // Sine phase throughout (B = 0).
  static void Spectral(int z, int c, float u, float* A, float* B) {
    using namespace wavemorph_k;
    for (int h = 0; h <= WM_HMAX; h++) { A[h] = 0.f; B[h] = 0.f; }
    auto put = [&](int h, float amp) { if (h >= 1 && h <= WM_HMAX) A[h] += amp; };
    auto list = [&](const WmHarm* w, int n) { for (int i = 0; i < n; i++) put(w[i].h, w[i].a); };
#define WM_LIST(arr) list(arr, (int)(sizeof(arr) / sizeof(arr[0])))
    switch (z) {
      case 2:                                      // saw rough: 1 saw | 3 needle saw (2 = sine + needle, time domain)
        for (int h = 1; h <= WM_HMAX; h++) put(h, (c == 0) ? 1.f / (float)h : powf((float)h, -WM_NSAW_EXP));
        break;
      case 4: {                                    // vocal a -> e -> i -> o -> u (= dd3d5f9)
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
          put(h, amp);
        }
        break;
      }
      case 6:                                      // soft A
        switch (c) {
          case 0: WM_LIST(WM_W_TINE); break;
          case 1: WM_LIST(WM_W_CLARINET); break;
          case 2: WM_LIST(WM_W_DRAWBAR); break;
          default: WM_LIST(WM_W_MIXTURE); break;
        }
        break;
      case 7:                                      // soft B (4 = lo-res is time domain)
        switch (c) {
          case 0:                                  // harmonium
            put(1, WM_HARMONIUM_H1); put(2, WM_HARMONIUM_H2); put(3, WM_HARMONIUM_H3);
            for (int h = 4; h <= WM_HMAX; h++) put(h, WM_HARMONIUM_HN / (float)h);
            break;
          case 1: WM_LIST(WM_W_GHOST5); break;
          default: WM_LIST(WM_W_BELL); break;
        }
        break;
      case 8:                                      // hard A
        switch (c) {
          case 0:                                  // PPG resonant
            for (int h = 1; h <= WM_PPGRES_N; h++) put(h, 1.f - WM_PPGRES_SLOPE * (float)(h - 1));
            break;
          case 1:                                  // nasal oboe reed
            WM_LIST(WM_W_OBOE);
            for (int h = 9; h <= WM_HMAX; h++) put(h, WM_OBOE_TAIL * powf(8.f / (float)h, WM_OBOE_TAIL_EXP));
            break;
          case 2:                                  // formant brass
            for (int h = 1; h <= WM_HMAX; h++) {
              const float e = ((float)h - WM_BRASS_CENTER) / WM_BRASS_WIDTH;
              put(h, (1.f / (float)h) * (1.f + WM_BRASS_GAIN * expf(-e * e)));
            }
            break;
          default: WM_LIST(WM_W_BASSBELL); break;   // bass bell 7/14
        }
        break;
      default:                                     // hard B (zone 10; 1 = sync saw is time domain)
        switch (c) {
          case 1:                                  // fuzz reed
            for (int h = 1; h <= WM_HMAX; h++) put(h, powf((float)h, -WM_FUZZ_EXP));
            break;
          case 2:                                  // clipper
            put(1, WM_CLIP_H1);
            for (int h = 2; h <= WM_HMAX; h++) {
              const float kh = (float)WM_CLIP_KNEE / (float)h;
              put(h, (h <= WM_CLIP_KNEE) ? WM_CLIP_FLAT : WM_CLIP_FLAT * kh * kh);
            }
            break;
          default:                                 // PPG feedback: saw 1/n without harmonics 1 .. SKIP
            for (int h = WM_PPGFB_SKIP + 1; h <= WM_HMAX; h++) put(h, 1.f / (float)h);
            break;
        }
        break;
    }
#undef WM_LIST
  }

  // Peak of the full-band (harmonics 1..WM_HMAX) frame, synthesised at WM_TD_SIZE points.
  static float SynthPeak(const float* A, const float* B) {
    using namespace wavemorph_k;
    constexpr int N = WM_TD_SIZE, M = N - 1, Q = N / 4;
    const float* tab = wavemorph_sin_tab;
    float pk = 0.f;
    for (int i = 0; i < N; i++) {
      float v = 0.f;
      int idx = 0;
      for (int h = 1; h <= WM_HMAX; h++) {
        idx = (idx + i) & M;                     // h i mod N
        v += A[h] * tab[idx] + B[h] * tab[(idx + Q) & M];
      }
      if (fabsf(v) > pk) pk = fabsf(v);
    }
    return pk;
  }
  static float Rms(const float* A, const float* B) {   // full-band RMS from the coefficients
    float e = 0.f;
    for (int h = 1; h <= wavemorph_k::WM_HMAX; h++) e += A[h] * A[h] + B[h] * B[h];
    return sqrtf(0.5f * e);
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
    const float* tab = wavemorph_sin_tab;
    float rms_saw = 1.f;                         // RMS of the plain saw (zone 3 corner 1), set when built
    int off = 0;
    for (int z = 0; z < WM_ZONES; z++) {
      zoff_[z] = off;
      const int nf = WM_ZONE_FRAMES[z];
      off += nf;
      for (int f = 0; f < nf; f++) {
        const float u = (float)f / (float)(nf - 1);
        if (IsTimeDomain(z, f)) {
          // 1) time domain, mean removed, peak 1
          float mean = 0.f;
          for (int i = 0; i < N; i++) { x[i] = TimeDomain(z, f, u, (float)i / (float)N); mean += x[i]; }
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
          Spectral(z, f, u, A, B);
          const float pk = SynthPeak(A, B);
          const float g = (pk > 0.f) ? 1.f / pk : 0.f;
          for (int h = 1; h <= WM_HMAX; h++) { A[h] *= g; B[h] *= g; }
        }
        if (WM_ZONE_REPHASE[z]) {                     // zones 1, 2, 3, 6, 10: one phase convention (the saw's:
          for (int h = 1; h <= WM_HMAX; h++) {        // sine phase, positive), magnitudes kept exactly
            if (B[h] == 0.f && A[h] >= 0.f) continue; // already in the convention: left bit-exact
            A[h] = sqrtf(A[h] * A[h] + B[h] * B[h]);
            B[h] = 0.f;
          }
          if (!WM_ZONE_RMS_MATCH[z]) {                // peak-normalised zones: peak 1 again after re-phasing
            const float pk = SynthPeak(A, B);
            const float g = (pk > 0.f) ? 1.f / pk : 0.f;
            for (int h = 1; h <= WM_HMAX; h++) A[h] *= g;
          }
        }
        if (z == 2 && f == 0) rms_saw = Rms(A, B);   // the plain saw: RMS target of zones 3, 6..10
        if (WM_ZONE_RMS_MATCH[z]) {                   // zones 3, 6..10: equal RMS, then the storage cap
          const float r = Rms(A, B);
          float g = (r > 0.f) ? rms_saw / r : 0.f;
          for (int h = 1; h <= WM_HMAX; h++) { A[h] *= g; B[h] *= g; }
          const float pk = SynthPeak(A, B);
          const float cap = 32767.f / WM_ZONE_I16_SCALE[z];   // the zone's storage headroom
          g = (pk > cap) ? cap / pk : 1.f;
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
          int16_t* dst = wavemorph_tab[zoff_[z] + f][l];
          for (int i = 0; i < WM_TAB_SIZE; i++) {
            float v = acc[i] * WM_ZONE_I16_SCALE[z];
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
    // Zones 6 / 7: the K2 filter envelope turns the K4 knob, t_eff = t + DEPTH e (not clamped).
    // + the live follower: WM_FOLLOW_DEPTH dir f (dir = slid sign of K2's t2, 0 inside the pad).
    const float tt = ft_t_ + WM_FENV_DEPTH * fenv_.e + WM_FOLLOW_DEPTH * (fol_dir_ * fol_f_);
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
        if (WM_FILT_STAGES == 2) s1.Process(hp, lp, y);   // second stage (24 dB/oct)
        else                     y = hp;                   // one stage (12 dB/oct)
        wet[n] = x + m * (y - x);
      }
    } else {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        if (WM_FILT_STAGES == 2) s1.Process(lp, y, hp);   // second stage (24 dB/oct)
        else                     y = lp;                   // one stage (12 dB/oct)
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
                    int oct, int zone, float fe, float fol) {
    using namespace wavemorph_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.zone = zone; s.sw2 = oct;
    MorphPos(zone, kv[2], s.f0, s.ff);
    s.fe = fe;
    s.fol = fol;
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
  uint8_t lvl_[kSlots][kBands];    // table band-limit level per band and slot
  bool  slot_on_[kSlots] = { false, false, false };   // slot computed last sample

  float dc_a_ = 0.f, dc_x1_ = 0.f, dc_y1_ = 0.f;
  float sr8_ = 8000.f, sr_ = 48000.f;
  float ctrl_a_ = 0.f;
  int   zone_ = 0, zfrom_ = 0;     // current / fading-out zone
  volatile int zone_t_ = 0;        // target zone (K1, Controls)
  int   zfade_n_ = 0, zfade_len_ = 8;
  float zg_ = 1.f;                 // new-zone gain during a fade
  float morph_ = 0.f;              // morph m = K3 (smoothed, this sample)
  int   zoff_[wavemorph_k::WM_ZONES] = {};   // packed frame offset per zone (Init)
  float k2s_ = 0.5f, k3s_ = 0.f;                      // smoothed knobs
  volatile float k2_t_ = 0.5f, k3_t_ = 0.f;          // targets
  float og_[3] = { 0.f, 1.f, 0.f };            // slot weights (up, played, down), slid
  volatile float og_t_[3] = { 0.f, 1.f, 0.f };
  float oct_step_ = 0.f;
  // K2 filter envelope
  float on_env_ = 0.f, on_gate_ = 0.f, on_base_ = 0.f;   // fast meter, gate meter, slow baseline (48 kHz)
  float on_env_a_ = 0.f, on_fall_a_ = 0.f;               // meter rise / gate fall coefficients (48 kHz)
  int   on_refr_ = 0, on_refr_len_ = 4800;               // refractory samples left / length (48 kHz)
  bool  on_pending_ = false;                             // an onset fired, not yet consumed by EnvUpdate()
  bool  on_seen_ = false;                                // an onset has happened
  FEnv  fenv_;                     // K2 filter envelope (all zones)
  float ln_ftau_ratio_ = 0.f;      // ln(WM_FENV_TAU_FAST_S / WM_FENV_TAU_SLOW_S)
  // Live follower (on top of the K2 filter envelope)
  EnvFollower fol_;                // rectifier + 4-pole LP, 48 kHz raw input
  float fol_y_ = 0.f;              // follower output, latest 48 kHz sample
  volatile float fol_f_ = 0.f;     // f = min(fol_y_, 1), per 8 kHz step (DIAG reads it)
  float fol_dir_ = 0.f;            // filter gate: slid sign of K2's t2 (0 inside WM_ENV_PAD)
  float fol_gate_a_ = 0.f;         // gate one-pole coefficient (8 kHz, tau WM_FOLLOW_GATE_MS)
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
