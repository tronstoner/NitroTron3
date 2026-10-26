#pragma once
//
// wavemorph — tuning constants (source of truth). See wavemorph.h for the control map and the
// per-zone table definitions. Engine / filter / envelope values are copies of explorer's
// (explorer_constants.h), not shared.

#include <cstdint>

namespace wavemorph_k {

// --- Zones (K1, stepped) / vector-morph tables -----------------------------------------
constexpr int   WM_ZONES          = 10;      // K1 equal zones (snap, crossfade on a change)
constexpr int   WM_CORNERS        = 4;       // corner waves of the vector zones (1, 2, 3, 7, 10)
constexpr int   WM_FRAMES16       = 16;      // frames of the continuous zones (4, 5, 6, 8, 9)
// Frames per zone F_z: morph m -> position (F_z - 1) m, linear crossfade between adjacent frames.
constexpr int   WM_ZONE_FRAMES[WM_ZONES] = { WM_CORNERS, WM_CORNERS, WM_CORNERS, WM_FRAMES16, WM_FRAMES16,
                                             WM_FRAMES16, WM_CORNERS, WM_FRAMES16, WM_FRAMES16, WM_CORNERS };
constexpr int   WM_TOTAL_FRAMES   = 5 * WM_CORNERS + 5 * WM_FRAMES16;   // packed table frames (sum of WM_ZONE_FRAMES)
constexpr int WmSumFrames(int i = 0) { return (i >= WM_ZONES) ? 0 : WM_ZONE_FRAMES[i] + WmSumFrames(i + 1); }
static_assert(WmSumFrames() == WM_TOTAL_FRAMES, "WM_TOTAL_FRAMES = sum of WM_ZONE_FRAMES");
constexpr float WM_ZONE_XFADE_MS  = 1.f;     // ms; linear crossfade old -> new zone on a K1 zone change
constexpr int   WM_TAB_SIZE       = 256;     // samples per single-cycle table (power of two; harmonics 1..127)
constexpr int   WM_HMAX           = WM_TAB_SIZE / 2 - 1;   // highest harmonic a table holds
constexpr int   WM_TD_SIZE        = 1024;    // time-domain definition resolution (oversampled 4x vs the table:
                                             // harmonics folding into 1..127 start at 897 -> ~-59 dB for 1/h spectra)
constexpr int   WM_LEVELS         = 14;      // band-limited versions per corner
// Harmonic count of each band-limited version; a band uses the largest H <= WM_TAB_FMAX / f'_k (static per band, slot).
constexpr int   WM_LEVEL_H[WM_LEVELS] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 127 };
constexpr float WM_TAB_FMAX       = 4000.f;  // Hz; band-engine ceiling for the table band limit (= EXP_WT_FMAX)
constexpr float WM_I16_SCALE      = 16384.f; // int16 storage scale (peak-1 corners, 2x headroom for band-limit overshoot)
constexpr uint32_t WM_SEED        = 0x5EEDu; // LCG seed (glass / rough phases and roughness, deterministic)

// Zone 1 sine | sync: x = sin(2 pi frac(R phi)), R per corner.
constexpr float WM_SYNC_R[WM_CORNERS]  = { 1.f, 2.f, 4.f, 7.f };
// Zone 2 triangle | fold: x = fold(G tri(phi)), fold reflects into [-1, 1], G per corner.
constexpr float WM_FOLD_G[WM_CORNERS]  = { 1.f, 2.f, 4.f, 8.f };
// Zone 3 saw | resonant saw (armitage's SAW push): x = (1 - mix) saw + mix sin(2 pi R phi) (1 - phi).
constexpr float WM_RSAW_R[WM_CORNERS]   = { 1.f, 2.f, 4.f, 8.f };
constexpr float WM_RSAW_MIX[WM_CORNERS] = { 0.f, 0.9f, 0.9f, 0.9f };   // corner 1 = the pure saw; 0.9 = ARM_RES_MIX_MAX
// Zone 4 PWM (16 frames): x = +1 (phi < w) else -1, w = 0.5 (1 - u)^WM_PWM_CURVE, u = frame / 15:
// most of the travel at the narrow end (frames 8..15 below 10 %); frame 15 w = 0 = silence.
constexpr float WM_PWM_CURVE      = 2.2f;
// Zone 5 PPG glass (soft, 16 frames): fundamental + body (2nd harmonic) + three Gaussian clusters at c x
// WM_GLASS_RATIO with amplitudes WM_GLASS_A, further rolled off by (c / h)^WM_GLASS_ROLL above c;
// c = C0 + (C1 - C0) u, continuous.
constexpr float WM_GLASS_FUND     = 1.f;     // fundamental amplitude (body)
constexpr float WM_GLASS_BODY     = 0.4f;    // 2nd harmonic amplitude (body)
constexpr float WM_GLASS_C0       = 3.f;     // cluster centre (harmonic) at frame 0
constexpr float WM_GLASS_C1       = 10.f;    // cluster centre at frame 15
constexpr float WM_GLASS_RATIO[3] = { 1.f, 1.593f, 2.405f };        // bell-like inharmonic ratios (rounded onto harmonics)
constexpr float WM_GLASS_A[3]     = { 0.6f, 0.3f, 0.15f };          // cluster amplitudes
constexpr float WM_GLASS_SIGMA    = 0.8f;    // cluster width (harmonics)
constexpr float WM_GLASS_ROLL     = 1.f;     // extra roll-off exponent above the centre: x (c / h)^ROLL for h > c
// Zone 6 PPG air (soft, 16 frames): fundamental + 3rd harmonic body + WM_AIR_N sparse partials moving
// continuously along the ODD harmonics: odd index q_j = Q0_j + (Q1_j - Q0_j) u (h = 2 q + 1), each partial
// split linearly between the two neighbouring odd harmonics; amplitude WM_AIR_PART (3 / h)^WM_AIR_ROLL.
constexpr float WM_AIR_FUND       = 1.f;
constexpr float WM_AIR_BODY       = 0.35f;   // 3rd harmonic (odd body, hollow)
constexpr float WM_AIR_PART       = 0.4f;
constexpr float WM_AIR_ROLL       = 0.75f;
constexpr int   WM_AIR_N          = 3;
constexpr float WM_AIR_Q0[WM_AIR_N] = { 2.f, 3.f, 4.f };    // odd index at frame 0: h 5, 7, 9
constexpr float WM_AIR_Q1[WM_AIR_N] = { 4.f, 7.f, 12.f };   // odd index at frame 15: h 9, 15, 25
// Zone 7 PPG spectral: saw spectrum 1/h (sine phase) | every 2nd harmonic removed | every 3rd and 4th
// removed | sparse (only WM_SPEC_SPARSE).
constexpr int   WM_SPEC_SPARSE_N  = 5;
constexpr int   WM_SPEC_SPARSE[WM_SPEC_SPARSE_N] = { 1, 3, 7, 13, 21 };
// Zone 8 vocal (16 frames, = dd3d5f9): vowels a -> e -> i -> o -> u, formant frequencies interpolated
// linearly between adjacent vowels (4 segments over the 16 frames);
// harmonic amp = sum_f A_f / (1 + ((h F0 - F_f) / (BW_f / 2))^2), sine phase.
constexpr float WM_VOX_F0         = 110.f;   // Hz; reference fundamental (= EXP_WT_VOX_F0)
constexpr int   WM_VOX_N          = 5;
constexpr float WM_VOX_F[WM_VOX_N][3] = { { 700.f, 1220.f, 2600.f },    // a (= explorer)
                                          { 400.f, 2000.f, 2550.f },    // e (= explorer)
                                          { 280.f, 2250.f, 2890.f },    // i (= explorer)
                                          { 450.f,  800.f, 2830.f },    // o (= explorer)
                                          { 325.f,  700.f, 2530.f } };  // u (added)
constexpr float WM_VOX_BW[3]      = { 80.f, 90.f, 120.f };   // Hz (= EXP_WT_VOX_BW)
constexpr float WM_VOX_A[3]       = { 1.f, 0.5f, 0.25f };    // (= EXP_WT_VOX_A)
// Zone 9 formant (16 frames): x = 0.5 (1 - cos(2 pi phi)) cos(2 pi Fr phi) (one full-width Hann burst per
// cycle), Fr = MIN (MAX / MIN)^u.
constexpr float WM_FORM_MIN       = 0.5f;
constexpr float WM_FORM_MAX       = 8.f;
// Zone 10 rough: amp_h = h^E (1 - Q + Q r_h), r_h fixed random in [0, 1), fixed random phase per harmonic.
constexpr float WM_ROUGH_E[WM_CORNERS] = { -1.f, -0.5f, 0.f, 0.25f };   // spectral tilt per corner
constexpr float WM_ROUGH_Q[WM_CORNERS] = { 0.f, 0.5f, 0.75f, 1.f };     // roughness (random amplitude) depth per corner

// --- K2 = one-shot morph envelope (explorer's FM-index EnvGain law 1:1, plus a noon dead zone) -----
// morph = K3 g. t = 2 k2 - 1 (smoothed, lands exactly). |t| <= WM_ENV_PAD: off, g = 1 (morph = K3).
// Outside: s = (|t| - PAD) / (1 - PAD) in (0, 1], T = WM_ENV_TIME_RATIO WM_ENV_TAU_SLOW_S (FAST / SLOW)^s,
// travel = WM_ENV_DECAY_RATIO T (36 s at the pad edge -> 450 ms at the knob ends); one-pole tau =
// travel / WM_ENV_TRAVEL_TAUS, g snaps at the 98 % point. CCW: each onset g = 1 -> decays to 0, holds 0;
// CW: each onset g = 0 -> rises to 1, holds 1. Before the first onset: CCW g = 1, CW g = 0.
constexpr float WM_ENV_PAD          = 0.08f;   // t units: k2 in [0.46, 0.54] = 8 % of the knob travel around noon
constexpr float WM_ENV_TAU_SLOW_S   = 4.0f;    // s (= EXP_ENV_TAU_SLOW_S)
constexpr float WM_ENV_TAU_FAST_S   = 0.05f;   // s (= EXP_ENV_TAU_FAST_S)
constexpr float WM_ENV_TRAVEL_TAUS  = 3.9f;    // (= EXP_ENV_TRAVEL_TAUS)
constexpr float WM_ENV_TIME_RATIO   = 3.0f;    // (= EXP_ENV_FM_TIME_RATIO)
constexpr float WM_ENV_DECAY_RATIO  = 3.0f;    // (= EXP_ENV_FM_DECAY_RATIO)

// --- Octave (SW2: UP +1, MID -1, DOWN -2; K5 mix) -------------------------------------
constexpr float WM_OCT_XFADE_MS   = 10.f;    // ms; linear slide between octave slots on an SW2 change (= EXP_OCT_XFADE_MS)

// --- K4 = post filter (explorer's, Q 0.707, all zones) ----------------------------------
constexpr float WM_FILT_FMIN      = 40.f;     // Hz; HP cutoff just right of noon
constexpr float WM_FILT_FMAX      = 4000.f;   // Hz; LP cutoff just left of noon
constexpr float WM_FILT_HP_MAX    = 4000.f;   // Hz; HP cutoff at K4 full CW
constexpr float WM_FILT_LP_MIN    = 180.f;    // Hz; LP cutoff at K4 full CCW
constexpr float WM_FILT_FC_MIN    = 10.f;     // Hz; stability floor on fc
constexpr float WM_FILT_FC_MAX    = 20000.f;  // Hz; stability ceiling on fc
constexpr float WM_FILT_CURVE     = 1.0f;     // |t| exponent
constexpr float WM_FILT_Q         = 0.707f;   // per-stage Q, both stages: flat
constexpr float WM_FILT_SMOOTH_MS = 5.f;      // ms; one-pole on t, per block
constexpr float WM_FILT_FADE      = 0.03f;    // |t| over which bypass crossfades into the filter
constexpr float WM_FILT_SNAP      = 1e-4f;    // |t smoothed| below this with target 0: bypass
constexpr float WM_FILT_SLIDE_MS  = 1.f;      // ms; bypass <-> filter mix slide

// --- Onset detector (explorer's, 1:1) -------------------------------------------------------
constexpr float    WM_ONSET_ENV_MS        = 2.6f;     // ms (= EXP_ONSET_ENV_MS)
constexpr float    WM_ONSET_FALL_MS       = 40.f;     // ms (= EXP_ONSET_FALL_MS)
constexpr float    WM_ONSET_SLOW_COEF     = 0.0006f;  // (= EXP_ONSET_SLOW_COEF)
constexpr float    WM_ONSET_RISE          = 1.5f;     // (= EXP_ONSET_RISE)
constexpr float    WM_ONSET_FLOOR         = 0.0021f;  // (= EXP_ONSET_FLOOR)
constexpr uint32_t WM_ONSET_REFRACTORY_MS = 100;      // (= EXP_ONSET_REFRACTORY_MS)

// --- Control smoothing / snapping ----------------------------------------------------------
constexpr float WM_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2, K3, K5 at the 8 kHz rate
constexpr float WM_CTRL_SNAP      = 1e-6f;  // smoothed knob lands exactly on its target within this

// --- Band sum: alignment + gain staging (copied values from explorer_constants.h) -------------
constexpr float WM_ALIGN_WA    = -0.1583384f;   // dB per log2(Hz)^2
constexpr float WM_ALIGN_WB    =  1.5180589f;   // dB per log2(Hz)
constexpr float WM_ALIGN_WC    = -1.9726554f;   // dB
constexpr float WM_ALIGN_W_FLO =  41.2f;        // Hz; weight held below
constexpr float WM_ALIGN_W_FHI = 330.f;         // Hz; weight held above
constexpr float WM_GAIN_UP     = 1.139f;        // octave-up slot gain (= EXP_GAIN_UP)
constexpr float WM_GAIN_DOWN   = 1.445f;        // octave-down slot gain (= EXP_GAIN_DOWN)
constexpr float WM_GAIN_DOWN2  = WM_GAIN_DOWN * WM_GAIN_DOWN;   // -2 slot gain (= EXP_GAIN_DOWN2)
constexpr float WM_OUT_GAIN    = 1.487f;        // wet trim (= EXP_OUT_GAIN)
constexpr float WM_SILENCE     = 1e-10f;        // |z|^2 below this: band silent

// --- Wet output chain ---------------------------------------------------------------------
constexpr float WM_DC_HZ       = 10.f;     // wet DC blocker
constexpr float WM_SAT_T       = 1.0f;     // soft clipper knee / ceiling (= EXP_SAT_T)
constexpr float WM_SAT_TRIM    = 1.0f;     // soft clipper fixed post trim
constexpr float WM_MUTE_SLIDE_MS = 1.0f;   // FS1 wet mute slide
constexpr int   WM_SIN_SIZE    = 1024;     // sin table entries (power of two; = WM_TD_SIZE so table builds index exactly)

// --- DIAG "note this setting" log (DIAG builds only) ------------------------------------------
constexpr float    WM_DIAG_KNOB_EPS  = 0.02f;
constexpr uint32_t WM_DIAG_SETTLE_MS = 600;
constexpr uint32_t WM_DIAG_BLINK_MS  = 150;

}  // namespace wavemorph_k
