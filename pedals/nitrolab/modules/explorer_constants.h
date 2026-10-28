#pragma once
//
// explorer — tuning constants (source of truth). See explorer.h for the
// per-band formulas and the control map.

#include <cstdint>

namespace explorer_k {

// --- Families / K1 zones (SW1 = family, K1 = equal zones) -------------------------------
// SW1 UP (FM): one zone per curated ratio (EXP_FM_RATIOS, list order) + the last zone (full CW) = XBAND.
// SW1 MID (tables): WaveMorph's 10 table zones (+ one last zone = bit table, only with EXP_BITTAB_ENABLE).
// SW1 DOWN: exact harmonics, frequency shift, interval shift.
// Bit table PARKED (2026-10-26): false = SW1 MID is the wavetable only; the bit-table code stays compiled in.
// true = an 11th K1 zone on MID after the 10 table zones (K2 waveform, K3 step shape, its step count also under the
// K3 dampener).
constexpr bool EXP_BITTAB_ENABLE = false;
constexpr int EXP_N_DOWN = 3;
constexpr float EXP_MODEL_XFADE_MS = 1.f;   // ms; linear crossfade old -> new model output on any model / zone / discrete-item change
constexpr float EXP_DISC_SLIDE_MS  = 1.f;   // ms; linear slide of a snapped numeric value (FM ratio, interval) to its new item

// --- Anti-fold (armitage's practice: keep generated partials under the 8 kHz engine's Nyquist) ---
// Applied only where the model has a natural per-band limit (FM index + feedback caps, XBAND index cap,
// harmonic drop, bit-table step count).
// false = off (A/B).
constexpr bool  EXP_ANTIFOLD      = true;
constexpr float EXP_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine (FM caps; the K3 dampener's L = min(this, fc))
constexpr float EXP_HARM_FMAX     = 3800.f;   // Hz; exact harmonic n f'_k above this is dropped (= ARM_ADD_FMAX)
constexpr float EXP_HARM_FEDGE    = 400.f;    // Hz; fade width below EXP_HARM_FMAX (= ARM_ADD_FEDGE)

// --- SW1 UP: FM ------------------------------------------------------------------
constexpr float EXP_FM_INDEX      = 12.f;    // rad; FM index, fixed (no index knob): ratio zones and XBAND
// FM ratio zones: K1 steps through these modulator ratios (list order, equal zones), snapped + EXP_DISC_SLIDE_MS.
constexpr int   EXP_FM_N_RATIOS   = 14;
constexpr float EXP_FM_RATIOS[EXP_FM_N_RATIOS] = { 0.5f, 1.f, 1.41421356f, 1.5f, 1.61803399f, 2.f, 2.41421356f,
                                                   2.71828183f, 3.f, 3.14159265f, 3.5f, 4.f, 5.f, 7.f };
constexpr int   EXP_N_UP          = EXP_FM_N_RATIOS + 1;   // SW1 UP K1 zones: the ratios + XBAND (last)
constexpr int   EXP_XB_PARTNER    = 12;      // XBAND zone: partner band offset n (bands up), fixed (no knob)
// FM modulator self-feedback variant switch (builder 2026-10-23: swap back and forth by ear):
//   true  = PUSHED (round 2, 9c48dc6): feedback 0 -> EXP_FBFM_FB_PUSHED, no anti-fold caps on feedback or index
//   false = GENTLE (4f8b820):          feedback 0 -> EXP_FBFM_FB_GENTLE, caps on index and feedback (explorer.h)
constexpr bool  EXP_FBFM_PUSHED    = false;
constexpr float EXP_FBFM_FB_GENTLE = 1.5f;    // modulator self-feedback at K2 CW (linear from 0), gentle variant
constexpr float EXP_FBFM_FB_PUSHED = 3.f;     // modulator self-feedback at K2 CW (linear from 0), pushed variant
constexpr float EXP_FBFM_FB_MAX    = EXP_FBFM_PUSHED ? EXP_FBFM_FB_PUSHED : EXP_FBFM_FB_GENTLE;
constexpr float EXP_FBFM_FB_SPAN   = 4.f;     // feedback cap b <= (L / (r f') - 1) / this (GENTLE, EXP_ANTIFOLD)

// --- SW1 DOWN: frequency / interval shift ------------------------------------------
constexpr float EXP_FS_MAX_HZ     = 800.f;   // frequency shift: |shift| at K2 full CW / CCW
constexpr float EXP_FS_CURVE_HZ   = 1.f;     // frequency shift: s = sign(t) F0 ((MAX/F0 + 1)^|t| - 1), F0 = this (exponential-ish, 0 at noon)
constexpr float EXP_FS_REF_HZ     = 800.f;   // frequency shift, K3 proportional end: shift_k = s f'_k / this (K2 end = +-100 % of the band freq)

constexpr int   EXP_IS_SEMI_MAX   = 12;      // interval shift: K2 = -this .. +this semitones, snapped (equal zones, noon = 0)
constexpr float EXP_IS_DETUNE_CENTS = 50.f;  // interval shift: K3 detune +-this cents, continuous (noon = 0)

// --- SW1 DOWN: exact harmonics / SW1 MID: tables, bit table -------------------------
constexpr float EXP_HARM_MAX      = 6.f;     // exact harmonics: harmonic number at K2 CW (linear from 1, neighbours crossfaded)

// SW1 MID tables (2026-10-26, step 2): WaveMorph's 10 zones, COPIED (not shared) from wavemorph_constants.h,
// values exact (approved, tuned there). K1 = zone (equal stepped zones, EXP_TB_ZONE_XFADE_MS crossfade on a change),
// K2 = morph m: position (F_z - 1) m through the zone's frames, linear crossfade between the two adjacent frames.
// Built at Init into SDRAM (explorer_tb, int16, packed [frame][version][sample]). See explorer.h TABLES.
constexpr int   EXP_TB_ZONES          = 10;      // K1 equal zones on SW1 MID (= WM_ZONES)
constexpr int   EXP_N_MID             = EXP_TB_ZONES + (EXP_BITTAB_ENABLE ? 1 : 0);   // SW1 MID K1 zones (+ bit table last)
constexpr int   EXP_TB_CORNERS        = 4;       // waves of the 4-wave zones 2, 6..10 (= WM_CORNERS)
constexpr int   EXP_TB_SYNC_N         = 7;       // zone 1 sine sync: frames (= WM_SYNC_N)
constexpr int   EXP_TB_SAWR_N         = 3;       // zone 3 saw rough: frames (= WM_SAWR_N)
constexpr int   EXP_TB_FRAMES16       = 16;      // frames of zones 4 PWM / 5 vocal (= WM_FRAMES16)
// Frames per zone F_z (= WM_ZONE_FRAMES).
constexpr int   EXP_TB_ZONE_FRAMES[EXP_TB_ZONES] = { EXP_TB_SYNC_N, EXP_TB_CORNERS, EXP_TB_SAWR_N, EXP_TB_FRAMES16,
                                                     EXP_TB_FRAMES16, EXP_TB_CORNERS, EXP_TB_CORNERS, EXP_TB_CORNERS,
                                                     EXP_TB_CORNERS, EXP_TB_CORNERS };
// Zones 3 and 6..10: each wave scaled to the plain saw's RMS, then capped to the storage headroom (= WM_ZONE_RMS_MATCH).
constexpr bool  EXP_TB_ZONE_RMS_MATCH[EXP_TB_ZONES] = { false, false, true, false, false, true, true, true, true, true };
// Zones 1, 2, 3, 6, 10: every frame re-phased to the saw's convention (sine phase, positive; magnitudes kept),
// then the zone's level rule again (= WM_ZONE_REPHASE).
constexpr bool  EXP_TB_ZONE_REPHASE[EXP_TB_ZONES]   = { true, true, true, false, false, true, false, false, false, true };
constexpr int   EXP_TB_TOTAL_FRAMES   = EXP_TB_SYNC_N + EXP_TB_SAWR_N + 6 * EXP_TB_CORNERS + 2 * EXP_TB_FRAMES16;   // packed (= WM_TOTAL_FRAMES)
constexpr int ExpTbSumFrames(int i = 0) { return (i >= EXP_TB_ZONES) ? 0 : EXP_TB_ZONE_FRAMES[i] + ExpTbSumFrames(i + 1); }
static_assert(ExpTbSumFrames() == EXP_TB_TOTAL_FRAMES, "EXP_TB_TOTAL_FRAMES = sum of EXP_TB_ZONE_FRAMES");
constexpr float EXP_TB_ZONE_XFADE_MS  = 1.f;     // ms; linear crossfade old -> new zone on a K1 zone change (= WM_ZONE_XFADE_MS)
constexpr int   EXP_TB_SIZE           = 256;     // samples per single-cycle table (power of two; harmonics 1..127) (= WM_TAB_SIZE)
constexpr int   EXP_TB_HMAX           = EXP_TB_SIZE / 2 - 1;   // highest harmonic a table holds (= WM_HMAX)
constexpr int   EXP_TB_TD_SIZE        = 1024;    // time-domain definition resolution, 4x oversampled (= WM_TD_SIZE)
constexpr int   EXP_TB_LEVELS         = 14;      // band-limited versions per frame (= WM_LEVELS)
// Harmonic count of each band-limited version (= WM_LEVEL_H). hf = min(EXP_TB_FMAX, fc) / f'_k (fc = K3 dampener,
// recomputed per block when K3 moved). EXP_TB_BLEND = true: linear blend of the two neighbouring versions
// (LEVEL_H[lo] <= hf < LEVEL_H[lo + 1], x = (hf - H_lo) / (H_hi - H_lo); hf < 1 -> version 0, hf >= 127 -> last).
// false: the largest H <= hf (floor H = 1), a moved pick slides over EXP_DAMP_SLIDE_MS.
constexpr int   EXP_TB_LEVEL_H[EXP_TB_LEVELS] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 127 };
constexpr float EXP_TB_FMAX           = 4000.f;  // Hz; band-engine ceiling for the table band limit (= WM_TAB_FMAX)
constexpr bool  EXP_TB_BLEND          = false;   // true = continuous version blend (2026-10-28, rejected by ear: the stepped pick has more character); false = discrete pick + slide
constexpr float EXP_TB_I16_SCALE      = 16384.f; // int16 storage scale, default: |table| <= 2.0 (= WM_I16_SCALE)
constexpr float EXP_TB_I16_SCALE_WIDE = 6554.f;  // zones 3 / 10: |table| <= 5.0 (= WM_I16_SCALE_WIDE)
// Storage scale per zone; the RMS-matched waves are capped to that zone's headroom 32767 / scale (= WM_ZONE_I16_SCALE).
constexpr float EXP_TB_ZONE_I16_SCALE[EXP_TB_ZONES] = { EXP_TB_I16_SCALE, EXP_TB_I16_SCALE, EXP_TB_I16_SCALE_WIDE,
                                                        EXP_TB_I16_SCALE, EXP_TB_I16_SCALE, EXP_TB_I16_SCALE,
                                                        EXP_TB_I16_SCALE, EXP_TB_I16_SCALE, EXP_TB_I16_SCALE,
                                                        EXP_TB_I16_SCALE_WIDE };
// Zone 1 sine | sync: x = sin(2 pi frac(R phi)), R per frame (= WM_SYNC_R).
constexpr float EXP_TB_SYNC_R[EXP_TB_SYNC_N] = { 1.f, 1.5f, 2.f, 2.7f, 4.f, 4.6f, 6.5f };
// Zone 2 triangle | fold: x = fold(G tri(phi)), G per frame (= WM_FOLD_G).
constexpr float EXP_TB_FOLD_G[EXP_TB_CORNERS] = { 1.f, 2.f, 4.f, 8.f };
// Zone 3 saw rough: saw 1/n | sine + needle (time domain) | needle saw 1/n^NSAW_EXP.
constexpr float EXP_TB_SNDL_W         = 0.015f;  // sine + needle: spike width, share of the cycle (= WM_SNDL_W)
constexpr float EXP_TB_SNDL_H         = 2.0f;    // sine + needle: spike height (= WM_SNDL_H)
constexpr float EXP_TB_SNDL_POS       = 0.25f;   // sine + needle: spike centre (= WM_SNDL_POS)
constexpr float EXP_TB_NSAW_EXP       = 0.3f;    // needle saw exponent (= WM_NSAW_EXP)
// Zone 4 PWM: x = +1 (phi < w) else -1, w = 0.5 (1 - u)^CURVE, u = frame / 15 (frame 15 = silence).
constexpr float EXP_TB_PWM_CURVE      = 2.2f;    // (= WM_PWM_CURVE)
// Zone 5 vocal: a -> e -> i -> o -> u, formants interpolated; amp = sum_f A_f / (1 + ((h F0 - F_f) / (BW_f / 2))^2).
constexpr float EXP_TB_VOX_F0         = 110.f;   // Hz; reference fundamental (= WM_VOX_F0)
constexpr int   EXP_TB_VOX_N          = 5;       // (= WM_VOX_N)
constexpr float EXP_TB_VOX_F[EXP_TB_VOX_N][3] = { { 700.f, 1220.f, 2600.f },    // a (= WM_VOX_F)
                                                  { 400.f, 2000.f, 2550.f },    // e
                                                  { 280.f, 2250.f, 2890.f },    // i
                                                  { 450.f,  800.f, 2830.f },    // o
                                                  { 325.f,  700.f, 2530.f } };  // u
constexpr float EXP_TB_VOX_BW[3]      = { 80.f, 90.f, 120.f };   // Hz (= WM_VOX_BW)
constexpr float EXP_TB_VOX_A[3]       = { 1.f, 0.5f, 0.25f };    // (= WM_VOX_A)
// Zone 6 PPG classic: 8-step saw | stepped odd-harmonic stack | bit pattern | 16-step held sine (time domain).
constexpr int   EXP_TB_PPG_SAW_STEPS  = 8;       // (= WM_PPG_SAW_STEPS)
constexpr int   EXP_TB_PPG_ODD_STEPS  = 32;      // (= WM_PPG_ODD_STEPS)
constexpr float EXP_TB_PPG_ODD_A3     = 0.5f;    // (= WM_PPG_ODD_A3)
constexpr float EXP_TB_PPG_ODD_A5     = 0.33f;   // (= WM_PPG_ODD_A5)
constexpr float EXP_TB_PPG_ODD_LEVELS = 7.5f;    // (= WM_PPG_ODD_LEVELS)
constexpr int   EXP_TB_PPG_BIT_STEPS  = 32;      // (= WM_PPG_BIT_STEPS)
constexpr int   EXP_TB_PPG_SINE_STEPS = 16;      // (= WM_PPG_SINE_STEPS)
// Zones 7..10 (sine phase unless time domain), listed harmonics {h, amp}, rest 0 (= WM_W_*).
struct ExpTbHarm { int h; float a; };
constexpr ExpTbHarm EXP_TB_W_TINE[]     = { {1, 1.f}, {2, .3f}, {3, .08f}, {7, .12f}, {8, .18f}, {9, .15f}, {10, .08f} };
constexpr ExpTbHarm EXP_TB_W_CLARINET[] = { {1, 1.f}, {3, .7f}, {5, .45f}, {7, .25f}, {9, .12f}, {11, .06f}, {13, .03f} };
constexpr ExpTbHarm EXP_TB_W_DRAWBAR[]  = { {1, 1.f}, {2, 1.f}, {3, .8f}, {4, .5f}, {6, .35f}, {8, .25f} };
constexpr ExpTbHarm EXP_TB_W_MIXTURE[]  = { {1, 1.f}, {2, .7f}, {3, .35f}, {4, .5f}, {6, .3f}, {8, .4f}, {12, .25f}, {16, .25f}, {24, .12f} };
constexpr ExpTbHarm EXP_TB_W_GHOST5[]   = { {1, .05f}, {3, 1.f}, {5, .8f} };
constexpr ExpTbHarm EXP_TB_W_BELL[]     = { {1, 1.f}, {2, .15f}, {4, .3f}, {7, .25f}, {11, .45f}, {12, .35f}, {17, .3f}, {18, .22f}, {23, .12f} };
constexpr ExpTbHarm EXP_TB_W_BASSBELL[] = { {1, 1.f}, {2, .2f}, {7, .7f}, {14, .5f} };
// Harmonium: 1: H1, 2: H2, 3: H3, n >= 4: HN / n (= WM_HARMONIUM_*).
constexpr float EXP_TB_HARMONIUM_H1 = 0.6f, EXP_TB_HARMONIUM_H2 = 1.f, EXP_TB_HARMONIUM_H3 = 0.9f, EXP_TB_HARMONIUM_HN = 1.2f;
constexpr int   EXP_TB_LORES_STEPS    = 6;       // stepped lo-res sine steps (= WM_LORES_STEPS)
constexpr int   EXP_TB_PPGRES_N       = 8;       // PPG resonant: n = 1..N: 1 - SLOPE (n - 1) (= WM_PPGRES_N)
constexpr float EXP_TB_PPGRES_SLOPE   = 0.05f;   // (= WM_PPGRES_SLOPE)
constexpr ExpTbHarm EXP_TB_W_OBOE[]     = { {1, .35f}, {2, .7f}, {3, .9f}, {4, 1.f}, {5, .8f}, {6, .55f}, {7, .4f}, {8, .3f} };
constexpr float EXP_TB_OBOE_TAIL      = 0.3f;    // n > 8: TAIL (8 / n)^TAIL_EXP (= WM_OBOE_TAIL)
constexpr float EXP_TB_OBOE_TAIL_EXP  = 1.5f;    // (= WM_OBOE_TAIL_EXP)
constexpr float EXP_TB_BRASS_GAIN     = 3.f;     // (1 / n) (1 + GAIN exp(-((n - CENTER) / WIDTH)^2)) (= WM_BRASS_GAIN)
constexpr float EXP_TB_BRASS_CENTER   = 8.f;     // (= WM_BRASS_CENTER)
constexpr float EXP_TB_BRASS_WIDTH    = 2.5f;    // (= WM_BRASS_WIDTH)
constexpr float EXP_TB_SYNCSAW_R      = 3.5f;    // sync saw 2 frac(R phi) - 1 (= WM_SYNCSAW_R)
constexpr float EXP_TB_FUZZ_EXP       = 0.6f;    // fuzz reed 1 / n^EXP (= WM_FUZZ_EXP)
constexpr float EXP_TB_CLIP_H1        = 0.3f;    // clipper: 1: H1; 2..KNEE: FLAT; above: FLAT (KNEE / n)^2 (= WM_CLIP_H1)
constexpr float EXP_TB_CLIP_FLAT      = 0.8f;    // (= WM_CLIP_FLAT)
constexpr int   EXP_TB_CLIP_KNEE      = 24;      // (= WM_CLIP_KNEE)
constexpr int   EXP_TB_PPGFB_SKIP     = 2;       // PPG feedback: saw 1/n without harmonics 1 .. this (= WM_PPGFB_SKIP)
// Bit table (BITTAB, Ataraxic Iteritas style): 16-step pattern per band cycle.
// (PARKED, EXP_BITTAB_ENABLE.) K2 STEPS through EXP_BT_WAVES waveforms (equal zones): the 16 LFSR patterns, then SQR n = 1 .. EXP_BT_SQR_N_MAX;
// a step = EXP_MODEL_XFADE_MS crossfade, no blend. K3 = step shape, continuous: CCW triangle (linear to the
// next step) -> noon saw (ramps on rising steps only, falls as a hard edge) -> CW pulse (hold, hard steps).
constexpr int      EXP_BT_STEPS     = 16;       // steps per band cycle (pattern length)
constexpr int      EXP_BT_PATTERNS  = 16;       // LFSR tap sets (K2 first half)
constexpr uint16_t EXP_BT_SEED      = 0xACE1u;  // fixed LFSR seed (every pattern starts here)
// 16-bit Fibonacci LFSR tap masks: new bit = parity(state & mask), state = (state >> 1) | (bit << 15),
// the pattern = the first 16 new bits (1 -> +1, 0 -> -1).
constexpr uint16_t EXP_BT_TAPS[EXP_BT_PATTERNS] = {
  0xB400u, 0xD008u, 0xA011u, 0x8016u, 0x9C00u, 0xE100u, 0xC014u, 0x8805u,
  0xF000u, 0xA801u, 0x8402u, 0xD300u, 0x9009u, 0xB002u, 0xC600u, 0xE008u };
constexpr int      EXP_BT_SQR_N_MAX = 16;       // SQR waveforms: square x harmonic n = 1 .. this
constexpr int      EXP_BT_WAVES     = EXP_BT_PATTERNS + EXP_BT_SQR_N_MAX;   // 32 stepped waveforms on K2
// Anti-fold (EXP_ANTIFOLD): steps per band S = largest of 16 / 8 / 4 / 2 with (S / 2) f'_k <= EXP_FOLD_LIMIT_HZ
// (the pattern's step Nyquist fits under the ceiling = at most one step per 8 kHz engine sample);
// the pattern is subsampled (every 16 / S-th step). Limit min(EXP_FOLD_LIMIT_HZ, fc) (K3 dampener),
// re-picked per block when K3 moved.
constexpr int      EXP_BT_STEPS_MIN = 2;        // fewest steps a band can drop to

// --- Octave (SW2 select: UP +1, MID played, DOWN -1; one slot) ----------------------------
constexpr float EXP_OCT_XFADE_MS  = 10.f;    // ms; linear slide between octave slots on an SW2 change (= ARM_XFADE_MS)

// --- K4 post filter: PARKED (2026-10-26). false = not run, K4 not read: the wet goes straight from the DC blocker
// to the soft clipper. true = as before: K4 on SW1 MID / DOWN = this filter (not on SW1 UP). ---
constexpr bool  EXP_POSTFILT_ENABLE = false;
// K4 post filter on the final 48 kHz wet (armitage's K4 law, no envelopes, no resonance):
// t = 2 k4 - 1, tmag = |t|^EXP_FILT_CURVE; noon = true bypass; t < 0 LP fc = FMAX (LP_MIN / FMAX)^tmag,
// t > 0 HP fc = FMIN (HP_MAX / FMIN)^tmag; EXP_FILT_STAGES TPT SVF stages (1: 12, 2: 24 dB/oct), Q fixed per stage.
// wet = dry + m (filtered - dry), m -> min(|t| / FADE, 1), slid at most 1 per EXP_FILT_SLIDE_MS.
constexpr float EXP_FILT_FMIN      = 40.f;     // Hz; HP cutoff just right of noon (= ARM_FILT_FMIN)
constexpr float EXP_FILT_FMAX      = 4000.f;   // Hz; LP cutoff just left of noon (= ARM_FILT_FMAX)
constexpr float EXP_FILT_HP_MAX    = 4000.f;   // Hz; HP cutoff at K4 full CW (= ARM_FILT_HP_MAX)
constexpr float EXP_FILT_LP_MIN    = 180.f;    // Hz; LP cutoff at K4 full CCW (= ARM_FILT_LP_MIN)
constexpr float EXP_FILT_FC_MIN    = 10.f;     // Hz; stability floor on fc (= ARM_FILT_FC_MIN)
constexpr float EXP_FILT_FC_MAX    = 20000.f;  // Hz; stability ceiling on fc (= ARM_FILT_FC_MAX)
constexpr float EXP_FILT_CURVE     = 1.0f;     // |t| exponent (= ARM_FILT_CURVE)
constexpr float EXP_FILT_Q         = 0.707f;   // per-stage Q: flat, no resonance
constexpr int   EXP_FILT_STAGES    = 1;        // 1 = one TPT SVF stage (2-pole, 12 dB/oct); 2 = two cascaded stages (24 dB/oct)
static_assert(EXP_FILT_STAGES == 1 || EXP_FILT_STAGES == 2, "EXP_FILT_STAGES: 1 or 2");
constexpr float EXP_FILT_SMOOTH_MS = 5.f;      // ms; one-pole on t, per block (= ARM_FILT_SMOOTH_MS)
constexpr float EXP_FILT_FADE      = 0.03f;    // |t| over which bypass crossfades into the filter (= ARM_FILT_FADE)
constexpr float EXP_FILT_SNAP      = 1e-4f;    // |t smoothed| below this with target 0: bypass (= ARM_FILT_SNAP)
constexpr float EXP_FILT_SLIDE_MS  = 1.f;      // ms; bypass <-> filter mix slide (= ARM_MUTE_SLIDE_MS as armitage uses it)

// --- K3 DAMPENER on SW1 UP / MID (2026-10-26; was the K5 CCW harmonic lowpass) -----------------
// fc = DAMP_FMIN (DAMP_FMAX / DAMP_FMIN)^(k3^DAMP_CURVE) (k3 = smoothed K3 0..1; k3 = 1 -> exactly DAMP_FMAX),
// evaluated once per audio block (only when the smoothed K3 moved). Every per-band harmonic ceiling C (on f'_k,
// after SW2) becomes min(C, fc): FM caps (L), table versions (EXP_TB_FMAX; blended, EXP_TB_BLEND), bit-table
// steps S. Each band keeps its level and fundamental. A discrete pick that moves (bit table; tables with
// EXP_TB_BLEND = false) slides over DAMP_SLIDE_MS (linear crossfade old -> new per band). SW1 DOWN: not applied.
constexpr float EXP_DAMP_FMAX     = 4000.f;   // Hz; fc at K3 full CW (open)
constexpr float EXP_DAMP_FMIN     = 150.f;    // Hz; fc at K3 full CCW (dark: every band above it plain fundamental)
constexpr float EXP_DAMP_CURVE    = 0.5f;     // exponent on k3 (2026-10-28; 1 = the former plain exponential law):
                                              // < 1 spends more of the knob at the top (noon: fc ~ 1.5 kHz, was 775 Hz)
constexpr float EXP_DAMP_SLIDE_MS = 1.f;      // ms; linear crossfade old -> new discrete pick per band (see above)

// --- K4 on SW1 UP = triggered envelope on the FM index: PARKED (2026-10-26) ---------------------------
// false = onset detector, live follower and envelope not run, K4 not read: I = EXP_FM_INDEX.
// true = as before (copied 1:1 from armitage's K2 FM envelope), now scaling the fixed index:
//   I = EXP_FM_INDEX e^2, e = g + EXP_FOLLOW_FM_DEPTH on f (ratio zones and XBAND; feedback unaffected).
constexpr bool     EXP_ENV_ENABLE          = false;
// Onset detector per 48 kHz input sample, before the decimator (armitage OnsetDetect / ARM_ONSET_*):
//   fast meter (ENV_MS) -> gate meter (up at once, falls with FALL_MS) -> slow baseline (SLOW_COEF);
//   onset = gate > RISE x baseline && gate > FLOOR, then REFRACTORY_MS without onsets.
// Envelope (armitage EnvGain, 8 kHz): a gain g (0..1), e = g (+ follower), I = EXP_FM_INDEX e^2.
//   t = 2 k4 - 1 (smoothed, lands exactly on its target): t = 0 -> off, g = 1.
//   T = EXP_ENV_FM_TIME_RATIO EXP_ENV_TAU_SLOW_S (EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S)^|t|, travel time
//   = EXP_ENV_FM_DECAY_RATIO T (36 s just off noon -> 450 ms at the ends); one-pole tau = travel / EXP_ENV_TRAVEL_TAUS,
//   g snaps at the 98 % point. CCW: each onset g = 1, decays to 0, holds 0. CW: each onset g = 0, rises to 1,
//   holds 1. Before the first onset: CCW g = 1, CW g = 0.
constexpr float    EXP_ONSET_ENV_MS        = 2.6f;     // ms; input |in| one-pole (= ARM_ONSET_ENV_MS)
constexpr float    EXP_ONSET_FALL_MS       = 40.f;     // ms; gate-meter fall (= ARM_ONSET_FALL_MS)
constexpr float    EXP_ONSET_SLOW_COEF     = 0.0006f;  // baseline one-pole at 48 kHz (= ARM_ONSET_SLOW_COEF)
constexpr float    EXP_ONSET_RISE          = 1.5f;     // gate > baseline x this = an attack (= ARM_ONSET_RISE)
constexpr float    EXP_ONSET_FLOOR         = 0.0021f;  // gate-meter floor, input units (= ARM_ONSET_FLOOR)
constexpr uint32_t EXP_ONSET_REFRACTORY_MS = 100;      // one pluck = one onset (= ARM_ONSET_REFRACTORY_MS)
constexpr float    EXP_ENV_TAU_SLOW_S      = 4.0f;     // s; time law just off noon (= ARM_ENV_TAU_SLOW_S)
constexpr float    EXP_ENV_TAU_FAST_S      = 0.05f;    // s; at the knob ends (= ARM_ENV_TAU_FAST_S)
constexpr float    EXP_ENV_TRAVEL_TAUS     = 3.9f;     // one-pole time constants per full travel (= ARM_ENV_TRAVEL_TAUS)
constexpr float    EXP_ENV_FM_TIME_RATIO   = 3.0f;     // T = this x the time law (= ARM_ENV_FM_TIME_RATIO)
constexpr float    EXP_ENV_FM_DECAY_RATIO  = 3.0f;     // decay (CCW) / rise (CW) time / T (= ARM_ENV_FM_DECAY_RATIO)
constexpr float    EXP_ENV_FM_SLIDE_MS     = 1.0f;     // ms; one-pole tau (8 kHz) on g before it scales the index, lands exactly (= ARM_ENV_FM_SLIDE_MS)
// Live follower (2026-10-26, armitage's ARM_FOLLOW_*): a subtle term ON TOP of the triggered FM envelope (unchanged).
// EnvFollower (env_follower.h: |x EXP_FOLLOW_GAIN| -> 4 cascaded one-poles at EXP_FOLLOW_HZ) on the raw 48 kHz input,
// once per 48 kHz sample next to the onset detector; no normalisation. f = min(follower, 1), read per 8 kHz step.
//   UP: e = g + EXP_FOLLOW_FM_DEPTH on f, on -> 1 with K4 off noon, 0 at noon (g = 1 there)
//   (CCW right after a pluck g = 1, so e can exceed 1 by up to EXP_FOLLOW_FM_DEPTH).
//   on: one-pole at 8 kHz, tau EXP_FOLLOW_GATE_MS, lands exactly (EXP_CTRL_SNAP). EXP_FOLLOW_FM_DEPTH 0 = bit-identical.
constexpr float    EXP_FOLLOW_HZ           = 20.f;     // Hz; follower 4-pole LP cutoff (= ARM_FOLLOW_HZ)
constexpr float    EXP_FOLLOW_GAIN         = 10.f;     // follower pre-gain, x10 scale (= ARM_FOLLOW_GAIN)
constexpr float    EXP_FOLLOW_FM_DEPTH     = 0.15f;    // added to the FM gain g at f = 1 (= ARM_FOLLOW_FM_DEPTH)
constexpr float    EXP_FOLLOW_GATE_MS      = 1.0f;     // ms; one-pole tau of the on gate at a K4 noon crossing (= ARM_FOLLOW_GATE_MS)

// --- Control smoothing / snapping ----------------------------------------------------
constexpr float EXP_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2, K3 (sweep / dampener), K4 (parked UP envelope t) at the 8 kHz rate (= ARM_CTRL_SMOOTH_MS)
constexpr float EXP_CTRL_SNAP      = 1e-6f;  // smoothed knob lands exactly on its target within this

// --- Band sum: phase alignment + gain staging (copied values from armitage_constants.h, ARM_ALIGN = true) ---
// Played slot: odd bands negated (z -> -z) before everything, output weight w_k = 10^(W_dB(log2 f_k) / 20).
// The unshaped played band sum with these weights = armitage's unshaped band sum at SW2 MID (unity vs dry).
constexpr float EXP_ALIGN_WA    = -0.1583384f;   // dB per log2(Hz)^2 (= ARM_ALIGN_WA)
constexpr float EXP_ALIGN_WB    =  1.5180589f;   // dB per log2(Hz)   (= ARM_ALIGN_WB)
constexpr float EXP_ALIGN_WC    = -1.9726554f;   // dB                (= ARM_ALIGN_WC)
constexpr float EXP_ALIGN_W_FLO =  41.2f;        // Hz; weight held below (= ARM_ALIGN_W_FLO)
constexpr float EXP_ALIGN_W_FHI = 330.f;         // Hz; weight held above (= ARM_ALIGN_W_FHI)
constexpr float EXP_GAIN_UP     = 1.139f;        // octave-up slot gain, unity vs played on the unshaped band sum (= ARM_GAIN_UP)
constexpr float EXP_GAIN_DOWN   = 1.445f;        // octave-down slot gain (= ARM_GAIN_DOWN)
constexpr float EXP_OUT_GAIN    = 1.487f;        // wet trim (= ARM_OUT_GAIN)
constexpr float EXP_SILENCE     = 1e-10f;        // |z|^2 below this: band silent (= ARM_SILENCE)
constexpr float EXP_DENORM      = 1e-12f;        // FM self-feedback state below this flushed to 0 (= ARM_DENORM)

// --- Wet output chain (copied from armitage) ---------------------------------------------
constexpr float EXP_DC_HZ     = 10.f;     // wet DC blocker (= ARM_DC_HZ)
constexpr float EXP_SAT_T     = 1.0f;     // soft clipper knee / ceiling (2026-10-24: 0.7 -> 1.0, attacks were squashed; ARM_SAT_T stays 0.7)
constexpr float EXP_SAT_TRIM  = 1.0f;     // soft clipper fixed post trim (= ARM_SAT_TRIM)
constexpr float EXP_MUTE_SLIDE_MS = 1.0f; // FS1 wet mute slide (= ARM_MUTE_SLIDE_MS)
constexpr int   EXP_SIN_SIZE  = 1024;     // shared sin table entries (power of two; = ARM_SIN_SIZE)

// --- DIAG "note this setting" log (DIAG builds only) --------------------------------------
constexpr float    EXP_DIAG_KNOB_EPS  = 0.02f;  // knob move that counts as a change
constexpr uint32_t EXP_DIAG_SETTLE_MS = 600;    // ms without change before an `auto` line
constexpr uint32_t EXP_DIAG_BLINK_MS  = 150;    // ms LED1 goes off to confirm an FS1 tap

}  // namespace explorer_k
