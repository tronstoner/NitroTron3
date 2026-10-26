#pragma once
//
// explorer — tuning constants (source of truth). See explorer.h for the
// per-band formulas and the control map.

#include <cstdint>

namespace explorer_k {

// --- Families / models (SW1 = family, K1 = model in equal zones) -------------
constexpr int EXP_N_UP   = 4;   // SW1 UP (FM):      ratio FM, ratio FM curated, cross-band FM, 2-op feedback FM
constexpr int EXP_N_MID  = 2;   // SW1 MID (tables): wavetable, bit table (zones used only with EXP_BITTAB_ENABLE)
// Bit table PARKED (2026-10-26): false = SW1 MID is the wavetable only (K1 does nothing there); the bit-table
// code stays compiled in and returns exactly as 0a42043 with true (MID = 2 K1 zones again).
constexpr bool EXP_BITTAB_ENABLE = false;
constexpr int EXP_N_DOWN = 3;   // SW1 DOWN:         exact harmonics, frequency shift, interval shift
constexpr float EXP_MODEL_XFADE_MS = 1.f;   // ms; linear crossfade old -> new model output on any model / zone / discrete-item change
constexpr float EXP_DISC_SLIDE_MS  = 1.f;   // ms; linear slide of a snapped numeric value (curated FM ratio, interval) to its new item

// --- Anti-fold (armitage's practice: keep generated partials under the 8 kHz engine's Nyquist) ---
// Applied only where the model has a natural per-band limit (FM index Carson caps on ratio / curated /
// cross-band FM, feedback + index caps on the 2-op feedback FM, harmonic drop, bit-table step count).
// false = off (A/B).
constexpr bool  EXP_ANTIFOLD      = true;
constexpr float EXP_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine (FM Carson caps, ring sum term)
constexpr float EXP_HARM_FMAX     = 3800.f;   // Hz; exact harmonic n f'_k above this is dropped (= ARM_ADD_FMAX)
constexpr float EXP_HARM_FEDGE    = 400.f;    // Hz; fade width below EXP_HARM_FMAX (= ARM_ADD_FEDGE)

// --- SW1 UP: FM ------------------------------------------------------------------
constexpr float EXP_FM_INDEX_MAX  = 12.f;    // rad; K3 index at full CW, squared taper (ratio FM, curated, cross-band, feedback)
constexpr float EXP_RFM_RATIO_MIN = 0.5f;    // ratio FM: modulator ratio at K2 CCW (exponential to MAX)
constexpr float EXP_RFM_RATIO_MAX = 8.f;     // ratio FM: modulator ratio at K2 CW
// Ratio FM curated: K2 steps through these modulator ratios (ascending, equal zones), snapped + EXP_DISC_SLIDE_MS.
constexpr int   EXP_FM_N_RATIOS   = 14;
constexpr float EXP_FM_RATIOS[EXP_FM_N_RATIOS] = { 0.5f, 1.f, 1.41421356f, 1.5f, 1.61803399f, 2.f, 2.41421356f,
                                                   2.71828183f, 3.f, 3.14159265f, 3.5f, 4.f, 5.f, 7.f };
constexpr int   EXP_XB_N_MAX      = 24;      // cross-band FM: partner offset n = 1 .. this bands, integer, snapped (equal K2 zones)
// 2-op feedback FM variant switch (builder 2026-10-23: swap back and forth by ear):
//   true  = PUSHED (round 2, 9c48dc6): feedback 0 -> EXP_FBFM_FB_PUSHED, no anti-fold caps on feedback or index
//   false = GENTLE (4f8b820):          feedback 0 -> EXP_FBFM_FB_GENTLE, caps b <= L/f' - 1, I <= L/f' - 2
constexpr bool  EXP_FBFM_PUSHED    = false;
constexpr float EXP_FBFM_FB_GENTLE = 1.5f;    // modulator self-feedback at K2 CW (linear from 0), gentle variant
constexpr float EXP_FBFM_FB_PUSHED = 3.f;     // modulator self-feedback at K2 CW (linear from 0), pushed variant
constexpr float EXP_FBFM_FB_MAX    = EXP_FBFM_PUSHED ? EXP_FBFM_FB_PUSHED : EXP_FBFM_FB_GENTLE;

// --- SW1 DOWN: frequency / interval shift ------------------------------------------
constexpr float EXP_FS_MAX_HZ     = 800.f;   // frequency shift: |shift| at K2 full CW / CCW
constexpr float EXP_FS_CURVE_HZ   = 1.f;     // frequency shift: s = sign(t) F0 ((MAX/F0 + 1)^|t| - 1), F0 = this (exponential-ish, 0 at noon)
constexpr float EXP_FS_REF_HZ     = 800.f;   // frequency shift, K3 proportional end: shift_k = s f'_k / this (K2 end = +-100 % of the band freq)

constexpr int   EXP_IS_SEMI_MAX   = 12;      // interval shift: K2 = -this .. +this semitones, snapped (equal zones, noon = 0)
constexpr float EXP_IS_DETUNE_CENTS = 50.f;  // interval shift: K3 detune +-this cents, continuous (noon = 0)

// --- SW1 DOWN: exact harmonics / SW1 MID: wavetable, bit table -------------------------
constexpr float EXP_HARM_MAX      = 6.f;     // exact harmonics: harmonic number at K2 CW (linear from 1, neighbours crossfaded)

constexpr int   EXP_WT_SETS       = 4;       // wavetable sets (K3 zones): analog, PPG-style, vocal, noisy
constexpr int   EXP_WT_WAVES      = 4;       // waves per set (K2 scans and interpolates)
constexpr int   EXP_WT_SIZE       = 256;     // samples per single-cycle table (power of two; holds harmonics 1..127)
constexpr int   EXP_WT_HMAX       = EXP_WT_SIZE / 2 - 1;   // highest harmonic a table can hold
constexpr int   EXP_WT_LEVELS     = 14;      // band-limited versions per wave (see EXP_WT_LEVEL_H)
// Harmonic count of each band-limited version; a band uses the largest H <= EXP_WT_FMAX / f'_k (static per band).
constexpr int   EXP_WT_LEVEL_H[EXP_WT_LEVELS] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 127 };
constexpr float EXP_WT_FMAX       = 4000.f;  // Hz; band-engine ceiling for the table band limit
constexpr float EXP_WT_I16_SCALE  = 16384.f; // int16 storage scale (peak-1 waves, 2x headroom for band-limit overshoot)
constexpr uint32_t EXP_WT_SEED    = 0x5EEDu; // LCG seed for the noisy set (deterministic tables)
// Vocal set: formants (Hz) / bandwidths (Hz) / amplitudes per vowel at fundamental EXP_WT_VOX_F0.
constexpr float EXP_WT_VOX_F0     = 110.f;   // Hz; reference fundamental the formant positions are laid out for
constexpr float EXP_WT_VOX_F[4][3]  = { { 700.f, 1220.f, 2600.f },    // a
                                        { 400.f, 2000.f, 2550.f },    // e
                                        { 280.f, 2250.f, 2890.f },    // i
                                        { 450.f,  800.f, 2830.f } };  // o
constexpr float EXP_WT_VOX_BW[3]  = { 80.f, 90.f, 120.f };   // Hz; formant bandwidths
constexpr float EXP_WT_VOX_A[3]   = { 1.f, 0.5f, 0.25f };    // formant amplitudes
// Bit table (BITTAB, Ataraxic Iteritas style): 16-step pattern per band cycle.
// K2 STEPS through EXP_BT_WAVES waveforms (equal zones): the 16 LFSR patterns, then SQR n = 1 .. EXP_BT_SQR_N_MAX;
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
// the pattern is subsampled (every 16 / S-th step). Static per band centre and slot.
constexpr int      EXP_BT_STEPS_MIN = 2;        // fewest steps a band can drop to

// --- Octave (SW2 select: UP +1, MID played, DOWN -1; one slot) ----------------------------
constexpr float EXP_OCT_XFADE_MS  = 10.f;    // ms; linear slide between octave slots on an SW2 change (= ARM_XFADE_MS)

// --- K4 = post filter on the final 48 kHz wet (armitage's K4 law, no envelopes, no resonance) ---
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

// --- K4 on SW1 UP = triggered envelope on the K3 FM index (copied 1:1 from armitage's K2 FM envelope) ---
// Onset detector per 48 kHz input sample, before the decimator (armitage OnsetDetect / ARM_ONSET_*):
//   fast meter (ENV_MS) -> gate meter (up at once, falls with FALL_MS) -> slow baseline (SLOW_COEF);
//   onset = gate > RISE x baseline && gate > FLOOR, then REFRACTORY_MS without onsets.
// Envelope (armitage EnvGain, 8 kHz): a gain g (0..1) on the K3 knob, k3_eff = k3 g, I = EXP_FM_INDEX_MAX k3_eff^2.
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
// --- Onset detector mode + spectral flux detector (2026-10-26) ---------------------------------
// MODE 0 = the level detector above (48 kHz gate meter vs slow baseline); 1 = spectral flux on the
// 84-band bank at the 8 kHz band rate (raw bank magnitudes |z_k|, independent of octave / model / knobs):
//   m_k = one-pole of |z_k| (FLUX_SMOOTH_MS); F = sum_k w_k max(0, |z_k| - m_k_prev), w_k = 1 + HF_TILT k / (K - 1)
//   (K = 84 bands: weight 1 at the lowest band rising linearly to 1 + HF_TILT at the top, so pick clicks count more);
//   mean_F = one-pole of F (FLUX_MEAN_MS); onset when F > FLUX_K mean_F (previous mean) AND F > FLUX_FLOOR AND
//   FLUX_REFRACTORY_MS elapsed. Latches the same onset flag the envelopes consume. Only the selected mode runs.
constexpr int EXP_ONSET_MODE = 1;   // 0 = level detector, 1 = spectral flux
constexpr float EXP_ONSET_FLUX_SMOOTH_MS = 10.f;   // ms; per-band magnitude smoother
constexpr float EXP_ONSET_FLUX_HF_TILT = 1.f;   // band weight w_k = 1 + TILT k / (K - 1)
constexpr float EXP_ONSET_FLUX_MEAN_MS = 150.f;   // ms; running mean of F (adaptive threshold, no absolute level)
constexpr float EXP_ONSET_FLUX_K = 2.5f;   // onset: F > K x mean_F
constexpr float EXP_ONSET_FLUX_FLOOR = 1e-3f;   // F floor (band-magnitude units): ~20 dB under the median F of a ringing-string re-pluck at 0.1 peak (~1e-2; a fresh pluck ~5e-2), above -60 dBFS input hiss (max ~6e-4)
constexpr float EXP_ONSET_FLUX_REFRACTORY_MS = 60.f;   // ms; min. time between onsets
constexpr float    EXP_ENV_TAU_SLOW_S      = 4.0f;     // s; time law just off noon (= ARM_ENV_TAU_SLOW_S)
constexpr float    EXP_ENV_TAU_FAST_S      = 0.05f;    // s; at the knob ends (= ARM_ENV_TAU_FAST_S)
constexpr float    EXP_ENV_TRAVEL_TAUS     = 3.9f;     // one-pole time constants per full travel (= ARM_ENV_TRAVEL_TAUS)
constexpr float    EXP_ENV_FM_TIME_RATIO   = 3.0f;     // T = this x the time law (= ARM_ENV_FM_TIME_RATIO)
constexpr float    EXP_ENV_FM_DECAY_RATIO  = 3.0f;     // decay (CCW) / rise (CW) time / T (= ARM_ENV_FM_DECAY_RATIO)

// --- Control smoothing / snapping ----------------------------------------------------
constexpr float EXP_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2, K3, K4 (UP envelope t) at the 8 kHz rate (= ARM_CTRL_SMOOTH_MS)
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
constexpr float EXP_DENORM      = 1e-12f;        // feedback FM state below this flushed to 0 (= ARM_DENORM)

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
