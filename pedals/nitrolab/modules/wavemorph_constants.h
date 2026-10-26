#pragma once
//
// wavemorph — tuning constants (source of truth). See wavemorph.h for the control map and the
// per-zone table definitions. Engine / filter / envelope values are copies of explorer's
// (explorer_constants.h), not shared.

#include <cstdint>

namespace wavemorph_k {

// --- Zones (K1, stepped) / tables -----------------------------------------------------
constexpr int   WM_ZONES          = 10;      // K1 equal zones (snap, crossfade on a change)
constexpr int   WM_CORNERS        = 4;       // waves of the 4-wave zones (2, 3, 6..10)
constexpr int   WM_SYNC_N         = 7;       // zone 1 sine sync: frames (sync ratios)
constexpr int   WM_SAWR_N         = 3;       // zone 3 saw rough: frames (saw | sine + needle | needle saw)
constexpr int   WM_FRAMES16       = 16;      // frames of the continuous zones (4 PWM, 5 vocal)
// Frames per zone F_z (every zone morphs): m = K3 -> position (F_z - 1) m, linear crossfade between
// the two adjacent frames.
constexpr int   WM_ZONE_FRAMES[WM_ZONES] = { WM_SYNC_N, WM_CORNERS, WM_SAWR_N, WM_FRAMES16, WM_FRAMES16,
                                             WM_CORNERS, WM_CORNERS, WM_CORNERS, WM_CORNERS, WM_CORNERS };
// Zones 3 and 6..10: each wave scaled to the plain saw's RMS (then capped to the storage headroom).
constexpr bool  WM_ZONE_RMS_MATCH[WM_ZONES] = { false, false, true, false, false, true, true, true, true, true };
// Zones 1, 2, 3, 6, 10: every frame re-phased to ONE common convention after the DFT / definition: harmonic n
// gets magnitude |H_n| (kept exactly) at the saw's phase (zone 3 corner 1: sine phase, positive), so
// neighbouring frames cannot cancel in the blend; then the zone's level rule (peak 1 / RMS match).
constexpr bool  WM_ZONE_REPHASE[WM_ZONES]   = { true, true, true, false, false, true, false, false, false, true };
// (zone 3: only the sine + needle frame changes - the saw and needle saw are already positive sine phase and are
// left bit-exact: a harmonic already at A >= 0, B = 0 is not touched)
constexpr int   WM_TOTAL_FRAMES   = WM_SYNC_N + WM_SAWR_N + 6 * WM_CORNERS + 2 * WM_FRAMES16;   // packed table frames
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
constexpr float WM_I16_SCALE      = 16384.f; // int16 storage scale, default (2x headroom: |table| <= 2.0)
constexpr float WM_I16_SCALE_WIDE = 6554.f;  // zones 3 / 10: headroom |table| <= 5.0, so the needle corners / fuzz / clipper
                                             // (crest ~9, full-band peak ~4.6) reach the saw's RMS uncapped; LSB 2.5x coarser
// Storage scale per zone; the RMS-matched waves are capped to that zone's headroom 32767 / scale.
constexpr float WM_ZONE_I16_SCALE[WM_ZONES] = { WM_I16_SCALE, WM_I16_SCALE, WM_I16_SCALE_WIDE, WM_I16_SCALE,
                                                WM_I16_SCALE, WM_I16_SCALE, WM_I16_SCALE, WM_I16_SCALE,
                                                WM_I16_SCALE, WM_I16_SCALE_WIDE };   // zones 3, 10: wide headroom

// Zone 1 sine | sync: x = sin(2 pi frac(R phi)), R per corner (non-integer: audible sync, not octaves).
constexpr float WM_SYNC_R[WM_SYNC_N]  = { 1.f, 1.5f, 2.f, 2.7f, 4.f, 4.6f, 6.5f };   // 2026-10-26: last 7 -> 6.5 (integer = plain sine)
// Zone 2 triangle | fold: x = fold(G tri(phi)), fold reflects into [-1, 1], G per corner.
constexpr float WM_FOLD_G[WM_CORNERS]  = { 1.f, 2.f, 4.f, 8.f };
// Zone 3 saw rough, 3 frames (position 2 m), n = harmonic number:
//   1 saw 1/n | 2 sine + needle: time domain sin(2 pi phi) + SNDL_H hann(phi - 0.25, width SNDL_W) (a narrow
//   raised-cosine spike on the sine's positive peak), DFT, re-phased to the saw convention | 3 needle saw
//   1/n^NSAW_EXP. Frame 1 = the plain saw (peak 1: the RMS target of zones 3, 6..10); all frames scaled to
//   that RMS (WM_ZONE_RMS_MATCH).
constexpr float WM_SNDL_W         = 0.015f;  // sine + needle: spike width (share of the cycle, Hann bump; 2026-10-26: 0.03 -> 0.015, spikier)
constexpr float WM_SNDL_H         = 2.0f;    // sine + needle: spike height (2026-10-26: 1 -> 2, spikier; peak ~3x the sine's before re-phasing)
constexpr float WM_SNDL_POS       = 0.25f;   // sine + needle: spike centre (the sine's positive peak)
constexpr float WM_NSAW_EXP       = 0.3f;    // needle saw: upper harmonics nearly as loud as the low ones (spiky)
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

// Zone 6 PPG classic = explorer's PPG-style wavetable set (explorer.h BuildTables, s == 1), time domain, in
// this order: 8-step saw (8 levels) | stepped odd-harmonic stack (32 steps, 16 levels) | bit pattern
// ((s 5) ^ (s >> 2)) & 15 over 32 steps | 16-step held sine.
constexpr int   WM_PPG_SAW_STEPS  = 8;       // (= explorer: saw 8 steps, 8 levels)
constexpr int   WM_PPG_ODD_STEPS  = 32;      // (= explorer: sin + 0.5 sin 3 + 0.33 sin 5, 32 steps)
constexpr float WM_PPG_ODD_A3     = 0.5f;
constexpr float WM_PPG_ODD_A5     = 0.33f;
constexpr float WM_PPG_ODD_LEVELS = 7.5f;    // quantiser: round(y 7.5) / 7.5 (16 levels)
constexpr int   WM_PPG_BIT_STEPS  = 32;      // bit pattern steps
constexpr int   WM_PPG_SINE_STEPS = 16;      // held sine steps
// Zones 7..10 PPG/VS waves (sine phase unless time domain), RMS-matched to the plain saw, capped to
// the zone's storage headroom (32767 / WM_ZONE_I16_SCALE). Listed harmonics {h, amp}, rest 0.
// 7 soft A: tine, clarinet, drawbar, mixture | 8 soft B: harmonium, ghost fifths, glass bell, lo-res |
// 9 hard A: PPG resonant, oboe reed, formant brass, bass bell | 10 hard B: sync saw, fuzz reed, clipper, PPG feedback
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
// PPG feedback: saw 1/n with harmonics 1 .. N_SKIP removed.
constexpr int   WM_PPGFB_SKIP     = 2;

// --- K2 noon dead zone (filter envelope off) --------------------------------------------
constexpr float WM_ENV_PAD          = 0.08f;   // t units: k2 in [0.46, 0.54] = 8 % of the knob travel around noon

// --- Octave (SW2: UP +1, MID played, DOWN -1; one slot) --------------------------------
constexpr float WM_OCT_XFADE_MS   = 10.f;    // ms; linear slide between octave slots on an SW2 change (= EXP_OCT_XFADE_MS)

// --- K4 = post filter (explorer's law, all zones) ----------------------------------
constexpr float WM_FILT_FMIN      = 40.f;     // Hz; HP cutoff just right of noon
constexpr float WM_FILT_FMAX      = 4000.f;   // Hz; LP cutoff just left of noon
constexpr float WM_FILT_HP_MAX    = 4000.f;   // Hz; HP cutoff at K4 full CW
constexpr float WM_FILT_LP_MIN    = 180.f;    // Hz; LP cutoff at K4 full CCW
constexpr float WM_FILT_FC_MIN    = 10.f;     // Hz; stability floor on fc
constexpr float WM_FILT_FC_MAX    = 20000.f;  // Hz; stability ceiling on fc
constexpr float WM_FILT_CURVE     = 1.0f;     // |t| exponent
constexpr float WM_FILT_Q         = 0.707f;   // per-stage Q, all zones: flat (2026-10-26: 1.1 tried, coloured too much)
constexpr int   WM_FILT_STAGES    = 1;        // 1 = one TPT SVF stage (2-pole, 12 dB/oct); 2 = two cascaded stages (24 dB/oct)
static_assert(WM_FILT_STAGES == 1 || WM_FILT_STAGES == 2, "WM_FILT_STAGES: 1 or 2");
constexpr float WM_FILT_SMOOTH_MS = 5.f;      // ms; one-pole on t, per block
constexpr float WM_FILT_FADE      = 0.03f;    // |t| over which bypass crossfades into the filter
constexpr float WM_FILT_SNAP      = 1e-4f;    // |t smoothed| below this with target 0: bypass
constexpr float WM_FILT_SLIDE_MS  = 1.f;      // ms; bypass <-> filter mix slide

// --- K2 (all zones) = envelope on the K4 post filter (armitage's K5 filter envelope: Env + PostFilter t_eff) ---
// t2 = 2 k2 - 1 (smoothed), off inside WM_ENV_PAD (e = 0). Outside: s = (|t2| - PAD) / (1 - PAD),
// tau = TAU_SLOW (TAU_FAST / TAU_SLOW)^s. CW: each onset e = 0, rises toward +1 with tau; at e >= PEAK it
// falls back toward 0 as a one-pole with tau x FALL_RATIO. CCW: each onset e = 0, then one-pole toward -1 and
// holds. Before the first onset e = 0. Filter position t_eff = t_K4 + DEPTH e (not clamped to the knob ends;
// cutoff limited only by WM_FILT_FC_MIN / MAX), smoothed with t; from K4 noon the envelope pushes off noon.
constexpr float WM_FENV_TAU_SLOW_S = 4.0f;    // s; tau at the pad edge (= ARM_ENV_TAU_SLOW_S)
constexpr float WM_FENV_TAU_FAST_S = 0.05f;   // s; tau at the knob ends (= ARM_ENV_TAU_FAST_S)
constexpr float WM_FENV_FALL_RATIO = 3.0f;    // CW fall tau / rise tau (= ARM_ENV_FALL_RATIO)
constexpr float WM_FENV_PEAK       = 0.98f;   // CW: rise -> fall at this e (armitage's 0.98)
constexpr float WM_FENV_DEPTH      = 0.7f;    // K4 knob travel (t units) at |e| = 1 (= ARM_ENV_DEPTH)

// --- Onset detector (explorer's, 1:1) -------------------------------------------------------
constexpr float    WM_ONSET_ENV_MS        = 2.6f;     // ms (= EXP_ONSET_ENV_MS)
constexpr float    WM_ONSET_FALL_MS       = 40.f;     // ms (= EXP_ONSET_FALL_MS)
constexpr float    WM_ONSET_SLOW_COEF     = 0.0006f;  // (= EXP_ONSET_SLOW_COEF)
constexpr float    WM_ONSET_RISE          = 1.5f;     // (= EXP_ONSET_RISE)
constexpr float    WM_ONSET_FLOOR         = 0.0021f;  // (= EXP_ONSET_FLOOR)
constexpr uint32_t WM_ONSET_REFRACTORY_MS = 100;      // (= EXP_ONSET_REFRACTORY_MS)

// --- Touch sensitivity of the triggered envelopes (2026-10-26) ---
// Relative, no absolute level: at each onset the attack strength A = peak of the onset detector's gate
// meter over the WM_DYN_WIN_MS after it (48 kHz). Reference R = running average of recent A, updated
// once per onset after A is known: v = A / R (R before this update), then R += WM_DYN_AVG_W (A - R).
// R = A (v = 1) on the first onset, after silence (gate below the detector floor for WM_DYN_SILENCE_MS)
// and after module (re)activation. Depth multiplier m = 1 + AMOUNT (v - 1), clamped to
// [0, 1 + AMOUNT (V_MAX - 1)]. Until A is known the previous m stays; then m slides linearly to the new
// value over WM_DYN_SLIDE_MS (8 kHz). Filter envelope: t_eff = t + DEPTH m e. FM envelope: k3_eff = k3 m g
// while the envelope is on (outside noon), k3 at noon. AMOUNT 0 = m exactly 1 = the plain envelopes.
constexpr float WM_DYN_WIN_MS        = 15.f;  // ms; attack-strength window after the onset (gate-meter peak, 48 kHz)
constexpr float WM_DYN_AVG_W         = 0.25f;  // reference R one-pole weight per onset (running average of recent attacks)
constexpr float WM_DYN_REF = 0.05f;  // gate-meter attack strength that gives the normal envelope depth (m = 1); harder plucks open further, softer less (2026-10-26: fixed reference replaces the running average)
constexpr float WM_DYN_AMOUNT = 1.0f;  // 0 = not touch-sensitive (bit-identical), 1 = fully: m = v
constexpr float WM_DYN_V_MAX         = 3.f;  // velocity cap (only against extreme outliers): m <= 1 + AMOUNT (V_MAX - 1)
constexpr float WM_DYN_SLIDE_MS      = 5.f;  // ms; m slides linearly to its new value once A is known (no jump)
constexpr float WM_DYN_SILENCE_MS    = 2000.f;  // ms; gate below the onset floor this long = silence: next onset re-initialises R = A
constexpr float WM_DYN_R_MIN         = 1e-6f;  // guard: R below this -> v = 1 (R >= the onset floor in practice)

// --- Control smoothing / snapping ----------------------------------------------------------
constexpr float WM_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2, K3 at the 8 kHz rate
constexpr float WM_CTRL_SNAP      = 1e-6f;  // smoothed knob lands exactly on its target within this

// --- Band sum: alignment + gain staging (copied values from explorer_constants.h) -------------
constexpr float WM_ALIGN_WA    = -0.1583384f;   // dB per log2(Hz)^2
constexpr float WM_ALIGN_WB    =  1.5180589f;   // dB per log2(Hz)
constexpr float WM_ALIGN_WC    = -1.9726554f;   // dB
constexpr float WM_ALIGN_W_FLO =  41.2f;        // Hz; weight held below
constexpr float WM_ALIGN_W_FHI = 330.f;         // Hz; weight held above
constexpr float WM_GAIN_UP     = 1.139f;        // octave-up slot gain (= EXP_GAIN_UP)
constexpr float WM_GAIN_DOWN   = 1.445f;        // octave-down slot gain (= EXP_GAIN_DOWN)
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
