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
// true = an 11th K1 zone on MID after the 10 table zones (K2 waveform, K3 step shape + the MID ladder cutoff; step
// count static at the open anti-fold pick, 2026-10-28: no dampener on MID).
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
// Harmonic count of each band-limited version (= WM_LEVEL_H). hf = min(EXP_TB_FMAX, fc_damp) / f'_k (the K3
// dampener, re-picked per block when the smoothed K3 moved; back on tables 2026-10-28 after the ladder episode,
// unmodulated there). EXP_TB_BLEND = true: linear blend of the two neighbouring versions
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
// the pattern is subsampled (every 16 / S-th step). Limit = min(EXP_FOLD_LIMIT_HZ, fc_damp) (the unmodulated K3
// dampener on MID, re-picked per block).
constexpr int      EXP_BT_STEPS_MIN = 2;        // fewest steps a band can drop to

// --- Octave (SW2 select: UP +1, MID played, DOWN -1; one slot) ----------------------------
constexpr float EXP_OCT_XFADE_MS  = 10.f;    // ms; linear slide between octave slots on an SW2 change (= ARM_XFADE_MS)

// --- K4 post filter: PARKED (2026-10-26). false = not run, K4 not read: the wet goes straight from the DC blocker
// to the soft clipper. true = as before: K4 on SW1 MID / DOWN = this filter (not on SW1 UP) - K4 then ALSO drives the
// envelope (K4 / K5 modulation, below): both read the same knob. ---
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
// 2026-10-28 (late): the dampener is BACK on SW1 MID (tables), exactly as before the ladder (stepped version pick
// hf = min(4000, fc) / f'_k, re-pick when the smoothed K3 moves, 1 ms per-band pick slide, EXP_TB_BLEND off) but
// NOT modulated there: on tables it follows only the smoothed K3 (no envelope / LFO / loop offset). SW1 UP (FM caps):
// the modulated fc as before. The 2-pole lowpass below (EXP_TF_*) sits on top of it on tables.

// --- K3 LADDER LOWPASS on SW1 MID (tables): PARKED (2026-10-28 late). EXP_LAD_ENABLE = false: the ladder is not
// run, the 2-pole SVF lowpass (EXP_TF_*, below) takes its place; true: the ladder path below instead of the SVF
// (same position, same mix m / modulation laws with EXP_LAD_* ranges). The K3 dampener stays on tables either way.
constexpr bool  EXP_LAD_ENABLE   = false;
// (as it ran 2026-10-28, both instances:)
// MoogLadder (src/core/blocks/moog_ladder.h, used as is) = NitroTron3 Mode A's ladder: Huovilainen 4-pole
// (24 dB/oct) lowpass, tanh (Pade) on the input and every stage, single rate (no oversampling), at 48 kHz on the
// wet AFTER interpolation + DC blocker, BEFORE the soft clipper. LOWPASS only, no makeup gain.
// Cutoff (plain exponential on the smoothed K3, NOT the dampener's sqrt curve):
//   fc_lad = LAD_FMIN (LAD_FMAX / LAD_FMIN)^k3   (k3 = 0 -> 250 Hz, noon -> 1.4 kHz, k3 = 1 -> 8 kHz)
// computed per 8 kHz sample (from the 10 ms-smoothed K3 and the 1 ms-slid envelope values), applied to the ladder
// once per 8 kHz sample (6 x 48 kHz samples; SetCutoff only when fc changed).
//   SW3 MID (ONE-SHOT) K4 pluck <-> swell: log2 fc = log2 LAD_FMIN + g_s (log2 fc_lad_knob - log2 LAD_FMIN).
//   SW3 DOWN (LOOPING) K5-CW target: log2 fc = log2 fc_lad_knob + d3 v log2(LAD_FMAX / LAD_FMIN), clamped
//   [LAD_FMIN, LAD_FMAX].
// Family change (SW1 to / from MID): wet = x + m (ladder(x) - x), m slid linearly 0 <-> 1 over LAD_XFADE_MS;
// ladder state cleared when it starts from m = 0; not run at all while m = 0 (SW1 UP / DOWN settled).
constexpr float EXP_LAD_FMIN     = 250.f;     // Hz; ladder cutoff at K3 full CCW (2026-10-28: 80 -> 250, 10:00 was nearly silent)
constexpr float EXP_LAD_FMAX     = 8000.f;    // Hz; ladder cutoff at K3 full CW
constexpr float EXP_LAD_RES      = 0.0f;      // MoogLadder resonance (feedback k = 4 res; 1.0 = self-oscillation
                                              // threshold). 0 = Mode A's (0.3 tried 2026-10-28: muddy, rejected).
constexpr float EXP_LAD_DRIVE    = 1.8f;      // MoogLadder input gain (= Mode A LADDER_DRIVE, its lightest drive, at
                                              // noon..CW). Small-signal passband gain = drive: +5.1 dB vs the
                                              // unfiltered wet (1.0 = the block's neutral, unity).
// FIXED post trim after the ladder (0.765 at res 0.3): 1 / drive, times the resonance's passband loss undone
// (the half-sample feedback gives DC gain (1 + 2 res) / (1 + 4 res)); passband level = the unfiltered wet / FM;
// the drive's saturation stays (fixed, not tied to a knob).
constexpr float EXP_LAD_TRIM     = (1.f / EXP_LAD_DRIVE) * (1.f + 4.f * EXP_LAD_RES) / (1.f + 2.f * EXP_LAD_RES);
constexpr float EXP_LAD_XFADE_MS = 10.f;      // ms; linear slide of the ladder mix m on an SW1 family change (also the SVF's)

// --- K3 2-POLE LOWPASS on SW1 MID (tables), both instances (2026-10-28 late; on top of the K3 dampener) ----------
// The parked K4 post filter's SVF (ExpSVF: TPT, ONE stage = 2-pole 12 dB/oct, Q EXP_FILT_Q 0.707, no resonance),
// LOWPASS output only, at 48 kHz on the wet AFTER interpolation + DC blocker, BEFORE the soft clipper (the ladder's
// position). No drive, no trim (unity passband).
// Cutoff (plain exponential on the smoothed K3):
//   fc_f = TF_FMIN (TF_FMAX / TF_FMIN)^k3   (k3 = 0 -> 250 Hz, noon -> 2 kHz, k3 = 1 -> 16 kHz = practically open)
// computed per 8 kHz sample (from the 10 ms-smoothed K3 and the 1 ms-slid envelope values), coefficients set once
// per 8 kHz sample (6 x 48 kHz samples; only when fc changed) -> no zipper.
// Modulation moves ONLY this cutoff on tables (the dampener stays on the knob):
//   SW3 MID (ONE-SHOT) K4 pluck <-> swell: log2 fc = log2 TF_FMIN + g_s (log2 fc_f_knob - log2 TF_FMIN).
//   SW3 DOWN (LOOPING) K5-CW target: log2 fc = log2 fc_f_knob + d3 v log2(TF_FMAX / TF_FMIN), clamped
//   [TF_FMIN, TF_FMAX].   (K5 LFO on SW3 MID stays on the morph.)
// Family change (SW1 to / from MID): wet = x + m (lp(x) - x), m slid linearly 0 <-> 1 over EXP_LAD_XFADE_MS (10 ms);
// SVF state cleared when it starts from m = 0; not run while m = 0 (SW1 UP / DOWN settled).
constexpr float EXP_TF_FMIN      = 250.f;     // Hz; lowpass cutoff at K3 full CCW (envelope bottom: EXP_TF_ENV_BOTTOM)
constexpr float EXP_TF_FMAX      = 16000.f;   // Hz; lowpass cutoff at K3 full CW (practically open)
constexpr float EXP_TF_ENV_BOTTOM = 150.f;    // Hz; SW3 MID pluck <-> swell bottom of the tables LP (2026-10-28: 250 -> 40 -> 150;
                                              // the decay did not close enough); the K3 knob range stays 250 Hz..16 kHz
constexpr float EXP_TF_ENV_BOTTOM_OCT = 0.737f; // the envelope bottom rises with K3: bottom = ENV_BOTTOM 2^(k3 this) (150 Hz CCW, ~194 noon, 250 CW; 2 was too much)
constexpr float EXP_TF_Q         = 1.5f;      // tables LP Q (2026-10-28: wet 2-pole, = K5 at ~2:00 in the Q test; 0.707 / 0.9 / 0.5 tried)
constexpr float EXP_TB_SAT_DRIVE = 3.0f;      // tables: drive into tanh after the LP (2026-10-28, level cheat / compression)
constexpr float EXP_TB_SAT_TRIM  = 0.6f;      // tables: FIXED post trim after the tanh (not 1/drive: the saturation stays)
constexpr bool  EXP_TF_Q_ON_K5   = false;     // TEMP, off: the LFO is back on K5 (2026-10-28): SW3 MID tables K5 = LP Q instead of the LFO (FM keeps the LFO)
constexpr float EXP_TF_Q_MIN     = 0.3f;      // Q at K5 full CCW (soft, veiled)
constexpr float EXP_TF_Q_MAX     = 3.0f;      // Q at K5 full CW (clear peak); exponential, noon ~0.95
constexpr bool  EXP_TB_DAMP_ENABLE = false;   // tables: K3 dampener (2026-10-28: off again by ear; static open picks)

// --- SW3 MID (ONE-SHOT instance, Explorer(false)): K4 = pluck <-> swell (2026-10-28) ----------------------------
// Target fixed = the K3 fc: SW1 UP the dampener fc, SW1 MID the 2-pole lowpass cutoff (EXP_TF_*; parked ladder:
// EXP_LAD_*), NOT the tables' dampener; SW1 DOWN: K4 inert.
// K5 = LFO (below). g in [0, 1] per 8 kHz sample: log2 fc = log2 FMIN + g_s (log2 fc_knob - log2 FMIN), clamped
// [FMIN, FMAX] (UP: EXP_DAMP_*, 150 Hz..4 kHz; MID: EXP_TF_*, 250 Hz..16 kHz) (g = 1: the K3 value, g = 0: the bottom); g_s = g one-pole slid over
// EXP_ENV_FM_SLIDE_MS (lands exactly). t4 = 2 k4 - 1 (smoothed, lands exactly).
//   CCW (t4 < 0)  DECAY: target 0, T_d = DEC_FAST (DEC_SLOW / DEC_FAST)^(1 - |t4|); each onset g = 1.
//                 EXP_PS_DEC_2POLE: 2-pole toward 0 (two equal one-poles, tau T / EXP_PS_DEC_2P_TAUS each: g = (1 + t/tau)
//                 e^(-t/tau), rounded start, 98 % at T, snaps within 0.02); else one-pole; EXP_PS_DEC_LINEAR: linear.
//   CW / noon     RISE:  target 1, T_r = RISE_FAST (RISE_SLOW / RISE_FAST)^|t4|; each onset with t4 > 0: g = 0
//                 (noon: an onset does nothing). One-pole toward 1, tau = T / EXP_ENV_TRAVEL_TAUS, snaps exactly
//                 to it within 0.02 (98 %), holds.
// K4 turns never reset g (only the target side and T change, at once); only onsets do. g = 1 before the first onset.
// LEAVE: a side change clears "plucked", an onset that resets g sets it; unplucked T = max(T_side, EXP_PS_LEAVE_S)
// (no near-jump when K4 changes side while the value rests), plucked T = T_side.
constexpr float    EXP_PS_DEC_FAST_S       = 0.45f;    // s (back to 0.45 with the one-pole decay; 1.0 + linear was not snappy); decay time T_d at K4 full CCW (pluck)
constexpr bool     EXP_PS_DEC_2POLE        = true;     // decay shape D (2026-10-28): 2-pole, rounded start, still snappy (false: one-pole)
constexpr float    EXP_PS_DEC_2P_TAUS      = 5.834f;   // per-stage taus per travel: (1 + x) e^-x = 0.02 at x = 5.834 (98 % at T)
constexpr bool     EXP_PS_DEC_LINEAR       = false;    // decay shape: false = one-pole (snappy, as before afc62a9), true = linear in octaves (rejected by ear)
constexpr float    EXP_PS_DEC_SLOW_S       = 8.0f;     // s (2026-10-28: 60 -> 8, bass notes rarely sustain that long); decay time T_d just CCW of noon (exponential over |t4|)
constexpr float    EXP_PS_RISE_FAST_S      = 0.01f;    // s; rise time T_r at noon (~ the knob value at once)
constexpr float    EXP_PS_RISE_SLOW_S      = 4.0f;     // s (2026-10-28: 36 -> 4); rise time T_r at K4 full CW (swell)
constexpr float    EXP_PS_LEAVE_S          = 0.5f;     // s (2026-10-28); minimum travel time after a K4 side change until the next onset

// --- SW3 MID (ONE-SHOT instance only): K5 = LFO (2026-10-28) -------------------------------------------------
// t5 = 2 k5 - 1 (smoothed, lands exactly), u = |t5|. Noon (t5 == 0) = off: no modulation (applied value exactly 0).
// Shape by side: CCW (t5 < 0) = triangle, bipolar -1..+1; CW (t5 > 0) = downward staircase EXP_LFO_STEPS (+1 / 0 / -1,
// equal thirds of the cycle, repeating; 2026-10-28, was S&H). One shared LFO, per 8 kHz sample, free-running phase
// (never reset by onsets). Law (armitage ARM_PWM_* / mnemonic MNEM_PRE_* travel):
//   depth = min(u / EXP_LFO_DEPTH_FRAC, 1)                         (fade-in over the first 30 % of each side)
//   rate  = EXP_LFO_HZ_MIN (EXP_LFO_HZ_MAX / EXP_LFO_HZ_MIN)^u      (one exponential over the whole side travel)
//   u:    0     .2    .4    .6    .8    1
//   Hz:   0.20  0.50  1.26  3.17  7.96  20
// Applied l = depth lfo, one-pole slid (tau EXP_ENV_FM_SLIDE_MS, lands exactly): the staircase steps don't click; a side
// change (noon crossing) passes through depth 0.
// Target fixed per SW1 family, swinging +- around the knob value in the parameter's own unit, clamped:
//   UP FM ratio zones   K2 feedback b = b_knob + l EXP_LFO_FB_SWING, [0, EXP_FBFM_FB_MAX]; XBAND: no effect
//   MID tables          K2 morph m = m_knob + l EXP_LFO_MORPH_SWING, [0, 1]   (parked BITTAB: no effect)
//   DOWN                no effect
// Coexists with K4 (pluck <-> swell on the K3 fc / ladder cutoff): K4 moves fc, K5 moves feedback / morph.
constexpr float    EXP_LFO_DEPTH_FRAC      = 0.3f;     // share of each K5 side over which the depth fades in
constexpr float    EXP_LFO_HZ_MIN          = 0.2f;     // Hz; LFO rate at u = 0 (just off noon)
constexpr float    EXP_LFO_HZ_MAX          = 20.f;     // Hz; LFO rate at u = 1 (K5 at either end)
constexpr float    EXP_LFO_FB_SWING        = 0.75f;    // UP FM: feedback b swing at full depth (+-, b units; FB_MAX 1.5)
constexpr float    EXP_LFO_MORPH_SWING     = 0.15f;    // MID tables: morph m swing at full depth (+-, m units 0..1; 2026-10-28: 0.5 -> 0.15, travelled too far)
constexpr uint32_t EXP_LFO_SEED            = 0x2545F491u;   // (unused since the staircase replaced S&H, 2026-10-28)
// K5 CW shape (2026-10-28, replaces S&H): downward staircase 100 % / 50 % / 0 % of the swing (+1 / 0 / -1 around the
// knob value), equal parts of the cycle, repeating. A square = { +1, -1 } with N 2.
constexpr int      EXP_LFO_N_STEPS         = 3;   // CW: number of steps per cycle = number of values below
constexpr float    EXP_LFO_STEPS[EXP_LFO_N_STEPS] = { 1.f, 0.f, -1.f };   // CW (2026-10-28): downward staircase with 3 levels (+1 / 0 / -1)

// --- SW3 DOWN (LOOPING instance, Explorer(true)): K4 / K5 = generic envelope modulation (2026-10-28) -----------
// (The ONE-SHOT e law below is what SW3 MID ran before the glide / pluck-swell; its code stays in ExpEnv, not run.)
// Envelope e (0..1), driven by the onset detector (below, always run; also the SW3 MID pluck-swell's trigger). t4 = 2 k4 - 1 (smoothed, lands exactly),
// dir = sign(t4) (CW = up, CCW = down). Before the first onset e = 0. The envelope runs independently of noon;
// K4 at noon (ONE-SHOT: t4 == 0; LOOPING: |t4| <= EXP_LOOP_PAD) only gates the APPLIED value to 0.
//   ONE-SHOT: each onset e = 0, one-pole rise toward 1, tau = travel / EXP_ENV_TRAVEL_TAUS, snaps to 1 at 0.98,
//             then holds 1. travel = EXP_ENV_FM_DECAY_RATIO EXP_ENV_FM_TIME_RATIO EXP_ENV_TAU_SLOW_S
//             (EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S)^|t4| (36 s just off noon -> 450 ms at the ends).
//   LOOPING:  linear triangle 0 -> 1 -> 0 -> ..., reflecting at 0 / 1 (wavemorph fdc9927's LoopEnv); each onset
//             e = 0 rising. One ramp T = EXP_LOOP_T_SLOW_S (EXP_LOOP_T_FAST_S / EXP_LOOP_T_SLOW_S)^s,
//             s = max(0, (|t4| - EXP_LOOP_PAD) / (1 - EXP_LOOP_PAD)) (inside the pad it runs at the slow T).
// Applied: v = dir e (0 when gated), one-pole slid (tau EXP_ENV_FM_SLIDE_MS at 8 kHz, lands exactly): an onset
// restart, a K4 noon / pad crossing (incl. the sign flip) glides instead of stepping.
// K5: t5 = 2 k5 - 1 (smoothed, lands exactly); CCW d2 = -t5 -> the K2 parameter, CW d3 = t5 -> the K3 parameter
// (the other one 0; noon = off). Offset in the parameter's own unit: p = clamp(p_knob + d v span_p, range_p),
// span = the full natural range (per model in explorer.h, MODULATION).
// Onset detector per 48 kHz input sample, before the decimator (armitage OnsetDetect / ARM_ONSET_*):
//   fast meter (ENV_MS) -> gate meter (up at once, falls with FALL_MS) -> slow baseline (SLOW_COEF);
//   onset = gate > RISE x baseline && gate > FLOOR, then REFRACTORY_MS without onsets.
constexpr float    EXP_ONSET_ENV_MS        = 2.6f;     // ms; input |in| one-pole (= ARM_ONSET_ENV_MS)
constexpr float    EXP_ONSET_FALL_MS       = 40.f;     // ms; gate-meter fall (= ARM_ONSET_FALL_MS)
constexpr float    EXP_ONSET_SLOW_COEF     = 0.0006f;  // baseline one-pole at 48 kHz (= ARM_ONSET_SLOW_COEF)
constexpr float    EXP_ONSET_RISE          = 1.3f;     // gate > baseline x this = an attack (2026-10-28: 1.5 -> 1.3, more sensitive; ARM 1.5)
constexpr float    EXP_ONSET_FLOOR         = 0.0007f;  // gate-meter floor, input units (2026-10-28: 0.0021 -> 0.0007, more sensitive; ARM 0.0021)
constexpr uint32_t EXP_ONSET_REFRACTORY_MS = 100;      // one pluck = one onset (= ARM_ONSET_REFRACTORY_MS)
// ONE-SHOT time law (the former FM envelope's, = ARM_ENV_*):
constexpr float    EXP_ENV_TAU_SLOW_S      = 4.0f;     // s; time law just off noon (= ARM_ENV_TAU_SLOW_S)
constexpr float    EXP_ENV_TAU_FAST_S      = 0.05f;    // s; at the knob ends (= ARM_ENV_TAU_FAST_S)
constexpr float    EXP_ENV_TRAVEL_TAUS     = 3.9f;     // one-pole time constants per full travel (= ARM_ENV_TRAVEL_TAUS)
constexpr float    EXP_ENV_FM_TIME_RATIO   = 3.0f;     // T = this x the time law (= ARM_ENV_FM_TIME_RATIO)
constexpr float    EXP_ENV_FM_DECAY_RATIO  = 3.0f;     // rise travel time / T (= ARM_ENV_FM_DECAY_RATIO)
constexpr float    EXP_ENV_FM_SLIDE_MS     = 1.0f;     // ms; one-pole tau (8 kHz) on the applied v = dir e, lands exactly (= ARM_ENV_FM_SLIDE_MS)
// LOOPING (wavemorph fdc9927's WM_ENV_PAD / WM_LOOP_T_*):
constexpr float    EXP_LOOP_PAD            = 0.08f;    // t4 units: k4 in [0.46, 0.54] = off (applied 0)
constexpr float    EXP_LOOP_T_SLOW_S       = 4.0f;     // s; one ramp just outside the pad
constexpr float    EXP_LOOP_T_FAST_S       = 0.03f;    // s; one ramp at the knob ends
// Live follower: PARKED (EXP_FOLLOW_ENABLE = false: not run). true = EnvFollower (env_follower.h: |x EXP_FOLLOW_GAIN|
// -> 4 cascaded one-poles at EXP_FOLLOW_HZ) on the raw 48 kHz input, next to the onset detector; f = min(follower, 1)
// per 8 kHz step, added on top of the envelope before the gate / sign: e + EXP_FOLLOW_DEPTH f (can exceed 1;
// the parameter clamps hold).
constexpr bool     EXP_FOLLOW_ENABLE       = false;
constexpr float    EXP_FOLLOW_HZ           = 20.f;     // Hz; follower 4-pole LP cutoff (= ARM_FOLLOW_HZ)
constexpr float    EXP_FOLLOW_GAIN         = 10.f;     // follower pre-gain, x10 scale (= ARM_FOLLOW_GAIN)
constexpr float    EXP_FOLLOW_DEPTH        = 0.15f;    // added to e at f = 1 (= ARM_FOLLOW_FM_DEPTH)
// Modulation spans (full natural range of each parameter, its own unit):
//   UP FM feedback b: EXP_FBFM_FB_MAX, [0, FB_MAX]; UP dampener fc: log2(EXP_DAMP_FMAX / EXP_DAMP_FMIN) octaves,
//   [FMIN, FMAX]; MID 2-pole LP fc: log2(EXP_TF_FMAX / EXP_TF_FMIN) octaves, [TF_FMIN, TF_FMAX] (tables' dampener: not modulated); MID morph m: 1, [0, 1]; DOWN harmonics n: EXP_HARM_MAX - 1, [1, MAX], blend 1, [0, 1];
//   freq shift g = log2(|s| / EXP_FS_CURVE_HZ + 1): log2(EXP_FS_MAX_HZ / EXP_FS_CURVE_HZ + 1), [0, that], sign from
//   the knob, scale blend 1, [0, 1]; interval: 2 EXP_IS_SEMI_MAX semitones, [-MAX, +MAX] (rounded, slid
//   EXP_DISC_SLIDE_MS), cents 2 EXP_IS_DETUNE_CENTS, [-DETUNE, +DETUNE].

// --- Control smoothing / snapping ----------------------------------------------------
constexpr float EXP_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2, K3, K4 (envelope t4), K5 (t5) at the 8 kHz rate (= ARM_CTRL_SMOOTH_MS)
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
