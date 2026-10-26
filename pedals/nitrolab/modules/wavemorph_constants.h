#pragma once
//
// wavemorph — tuning constants (source of truth). See wavemorph.h for the control map and the
// per-zone table definitions. Engine / filter / envelope values are copies of explorer's
// (explorer_constants.h), not shared.

#include <cstdint>

namespace wavemorph_k {

// --- Zones (K1, stepped) / tables -----------------------------------------------------
constexpr int   WM_ZONES          = 7;       // K1 equal zones (snap, crossfade on a change)
constexpr int   WM_CORNERS        = 4;       // corner waves of the vector zones (1 sine sync, 2 triangle fold, 3 thin saw)
constexpr int   WM_FRAMES16       = 16;      // frames of the continuous zones (4 PWM, 5 vocal)
constexpr int   WM_SOFT_N         = 8;       // zone 6 PPG/VS soft: waves (= frames, list order)
constexpr int   WM_HARD_N         = 7;       // zone 7 PPG/VS hard: waves (= frames, list order)
// Frames per zone F_z (every zone morphs): m = K3 g -> position (F_z - 1) m, linear crossfade between
// the two adjacent frames.
constexpr int   WM_ZONE_FRAMES[WM_ZONES] = { WM_CORNERS, WM_CORNERS, WM_CORNERS, WM_FRAMES16, WM_FRAMES16,
                                             WM_SOFT_N, WM_HARD_N };
constexpr bool  WM_ZONE_RMS_MATCH[WM_ZONES] = { false, false, false, false, false, true, true };   // zones 6 / 7: equal-RMS waves
constexpr int   WM_TOTAL_FRAMES   = 3 * WM_CORNERS + 2 * WM_FRAMES16 + WM_SOFT_N + WM_HARD_N;   // packed table frames
constexpr int WmSumFrames(int i = 0) { return (i >= WM_ZONES) ? 0 : WM_ZONE_FRAMES[i] + WmSumFrames(i + 1); }
static_assert(WmSumFrames() == WM_TOTAL_FRAMES, "WM_TOTAL_FRAMES = sum of WM_ZONE_FRAMES");
constexpr float WM_ZONE_XFADE_MS  = 1.f;     // ms; linear crossfade old -> new on a K1 zone change
constexpr int   WM_TAB_SIZE       = 256;     // samples per single-cycle table (power of two; harmonics 1..127)
constexpr int   WM_HMAX           = WM_TAB_SIZE / 2 - 1;   // highest harmonic a table holds
constexpr int   WM_TD_SIZE        = 1024;    // time-domain definition resolution (oversampled 4x vs the table:
                                             // harmonics folding into 1..127 start at 897 -> ~-59 dB for 1/h spectra)
constexpr int   WM_LEVELS         = 14;      // band-limited versions per frame
// Harmonic count of each band-limited version; a band uses the largest H <= WM_TAB_FMAX / f'_k (static per band, slot).
constexpr int   WM_LEVEL_H[WM_LEVELS] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 127 };
constexpr float WM_TAB_FMAX       = 4000.f;  // Hz; band-engine ceiling for the table band limit (= EXP_WT_FMAX)
constexpr float WM_I16_SCALE      = 16384.f; // int16 storage scale (2x headroom: |table| <= 2.0)
constexpr float WM_PEAK_CAP       = 32767.f / WM_I16_SCALE;   // zones 6 / 7: equal-RMS waves capped to the storage headroom

// Zone 1 sine | sync: x = sin(2 pi frac(R phi)), R per corner (non-integer: audible sync, not octaves).
constexpr float WM_SYNC_R[WM_CORNERS]  = { 1.f, 1.5f, 2.7f, 4.6f };
// Zone 2 triangle | fold: x = fold(G tri(phi)), fold reflects into [-1, 1], G per corner.
constexpr float WM_FOLD_G[WM_CORNERS]  = { 1.f, 2.f, 4.f, 8.f };
// Zone 3 thin saw: weighted sum of saw series (sine phase) at 1x / 2x / 3x the cycle rate:
//   A_h = W1 / h + W2 / (h / 2) [h even] + W3 / (h / 3) [h multiple of 3]; mean removed, peak 1.
constexpr float WM_TSAW_W1[WM_CORNERS] = { 1.f, 1.f,  1.f,  0.3f };   // 1x saw (fundamental region; 0.3 = pulled back)
constexpr float WM_TSAW_W2[WM_CORNERS] = { 0.f, 0.7f, 0.f,  0.7f };   // 2x saw (octave up)
constexpr float WM_TSAW_W3[WM_CORNERS] = { 0.f, 0.f,  0.7f, 0.7f };   // 3x saw (octave + fifth)
// Zone 4 PWM (16 frames): x = +1 (phi < w) else -1, w = 0.5 (1 - u)^WM_PWM_CURVE, u = frame / 15:
// most of the travel at the narrow end (frames 8..15 below 10 %); frame 15 w = 0 = silence.
constexpr float WM_PWM_CURVE      = 2.2f;
// Zone 5 vocal (16 frames, = dd3d5f9): vowels a -> e -> i -> o -> u, formant frequencies interpolated
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

// Zones 6 / 7 PPG/VS waves (sine phase unless time domain). Each wave is scaled to the RMS of the plain
// saw table (zone 3 corner 1, full band), then capped to WM_PEAK_CAP. Listed harmonics {h, amp}, rest 0.
struct WmHarm { int h; float a; };
constexpr WmHarm WM_W_TINE[]     = { {1, 1.f}, {2, .3f}, {3, .08f}, {7, .12f}, {8, .18f}, {9, .15f}, {10, .08f} };
constexpr WmHarm WM_W_CLARINET[] = { {1, 1.f}, {3, .7f}, {5, .45f}, {7, .25f}, {9, .12f}, {11, .06f}, {13, .03f} };
constexpr WmHarm WM_W_DRAWBAR[]  = { {1, 1.f}, {2, 1.f}, {3, .8f}, {4, .5f}, {6, .35f}, {8, .25f} };
constexpr WmHarm WM_W_MIXTURE[]  = { {1, 1.f}, {2, .7f}, {3, .35f}, {4, .5f}, {6, .3f}, {8, .4f}, {12, .25f}, {16, .25f}, {24, .12f} };
constexpr WmHarm WM_W_GHOST5[]   = { {1, .05f}, {3, 1.f}, {5, .8f} };
constexpr WmHarm WM_W_BELL[]     = { {1, 1.f}, {2, .15f}, {4, .3f}, {7, .25f}, {11, .45f}, {12, .35f}, {17, .3f}, {18, .22f}, {23, .12f} };
constexpr WmHarm WM_W_BASSBELL[] = { {1, 1.f}, {2, .2f}, {7, .7f}, {14, .5f} };   // 2026-10-26: bell partials one octave lower (14/28 -> 7/14), was too high
// Harmonium: 1: H1, 2: H2, 3: H3, n >= 4: HN / n.
constexpr float WM_HARMONIUM_H1 = 0.6f, WM_HARMONIUM_H2 = 1.f, WM_HARMONIUM_H3 = 0.9f, WM_HARMONIUM_HN = 1.2f;
// Stepped lo-res: sine held in this many steps per cycle (time domain: sin(2 pi floor(S phi) / S)).
constexpr int   WM_LORES_STEPS    = 6;
// PPG resonant: n = 1..N: 1 - SLOPE (n - 1); n > N: 0.
constexpr int   WM_PPGRES_N       = 8;
constexpr float WM_PPGRES_SLOPE   = 0.05f;
// Nasal oboe reed: listed 1..8, n > 8: TAIL (8 / n)^TAIL_EXP.
constexpr WmHarm WM_W_OBOE[]     = { {1, .35f}, {2, .7f}, {3, .9f}, {4, 1.f}, {5, .8f}, {6, .55f}, {7, .4f}, {8, .3f} };
constexpr float WM_OBOE_TAIL      = 0.3f;
constexpr float WM_OBOE_TAIL_EXP  = 1.5f;
// Formant brass: (1 / n) (1 + GAIN exp(-((n - CENTER) / WIDTH)^2)).
constexpr float WM_BRASS_GAIN     = 3.f;
constexpr float WM_BRASS_CENTER   = 8.f;
constexpr float WM_BRASS_WIDTH    = 2.5f;
// Sync saw: time domain saw 2 frac(R phi) - 1 at R x the cycle rate, reset each cycle.
constexpr float WM_SYNCSAW_R      = 3.5f;
// Fuzz reed: all n: 1 / n^EXP.
constexpr float WM_FUZZ_EXP       = 0.6f;
// Clipper: 1: H1; n = 2..KNEE: FLAT; n > KNEE: FLAT (KNEE / n)^2.
constexpr float WM_CLIP_H1        = 0.3f;
constexpr float WM_CLIP_FLAT      = 0.8f;
constexpr int   WM_CLIP_KNEE      = 24;

// --- K2 = one-shot morph envelope (explorer's FM-index EnvGain law 1:1, plus a noon dead zone) -----
// morph = K3 g. t = 2 k2 - 1 (smoothed, lands exactly). |t| <= WM_ENV_PAD: off, g = 1 (morph = K3).
// Outside: s = (|t| - PAD) / (1 - PAD) in (0, 1], T = WM_ENV_TIME_RATIO WM_ENV_TAU_SLOW_S (FAST / SLOW)^s,
// travel = WM_ENV_DECAY_RATIO T (36 s at the pad edge -> 450 ms at the knob ends); one-pole tau =
// travel / WM_ENV_TRAVEL_TAUS, g snaps at the 98 % point. CCW: each onset g = 1 -> decays to 0, holds 0;
// CW: each onset g = 0 -> rises to 1, holds 1. Before the first onset: CCW g = 1, CW g = 0.
constexpr float WM_ENV_PAD          = 0.08f;   // t units: k2 in [0.46, 0.54] = 8 % of the knob travel around noon
constexpr float WM_ENV_TAU_SLOW_S   = 4.0f;    // s (= EXP_ENV_TAU_SLOW_S)
constexpr float WM_ENV_TAU_FAST_S   = 0.0167f; // s (2026-10-26: 0.05 -> 0.0167, fastest travel at the knob ends 450 -> 150 ms)
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
