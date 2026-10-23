#pragma once
//
// explorer — tuning constants (source of truth). See explorer.h for the
// per-band formulas and the control map.

#include <cstdint>

namespace explorer_k {

// --- Families / models (SW1 = family, K1 = model in equal zones) -------------
constexpr int EXP_N_UP   = 5;   // SW1 UP:   ratio FM, cross-band FM, 2-op feedback FM, ring/AM, frequency shift
constexpr int EXP_N_MID  = 5;   // SW1 MID:  hard sync, CZ shapes, mirror/fold, formant pulses, exact harmonics
constexpr int EXP_N_DOWN = 4;   // SW1 DOWN: wavetable, phase quantisation, Walsh, bit/rate crush
constexpr float EXP_MODEL_XFADE_MS = 1.f;   // ms; linear crossfade old -> new model output on any model / zone change

// --- Anti-fold (armitage's practice: keep generated partials under the 8 kHz engine's Nyquist) ---
// Applied only where the model has a natural per-band limit (FM index Carson caps, sync / fold /
// formant / resonance ratio caps, CZ knee floor, harmonic drop, ring sum-term drop). false = off (A/B).
constexpr bool  EXP_ANTIFOLD      = true;
constexpr float EXP_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine (FM Carson caps, ring sum term)
constexpr float EXP_PD_FMAX       = 3800.f;   // Hz; ratio caps R_k f'_k <= this (sync, CZ resonance, fold, formant; = ARM_RES_FMAX)
constexpr float EXP_HARM_FMAX     = 3800.f;   // Hz; exact harmonic n f'_k above this is dropped (= ARM_ADD_FMAX)
constexpr float EXP_HARM_FEDGE    = 400.f;    // Hz; fade width below EXP_HARM_FMAX (= ARM_ADD_FEDGE)

// --- SW1 UP: FM & modulation --------------------------------------------------
constexpr float EXP_FM_INDEX_MAX  = 12.f;    // rad; K3 index at full CW, squared taper (models 1, 2, 3)
constexpr float EXP_RFM_RATIO_MIN = 0.5f;    // ratio FM: modulator ratio at K2 CCW (exponential to MAX)
constexpr float EXP_RFM_RATIO_MAX = 8.f;     // ratio FM: modulator ratio at K2 CW
constexpr float EXP_XB_N_MIN      = 1.f;     // cross-band FM / ring: partner offset (bands) at K2 CCW (linear, continuous)
constexpr float EXP_XB_N_MAX      = 24.f;    // cross-band FM / ring: partner offset at K2 CW
constexpr float EXP_FBFM_FB_MAX   = 1.5f;    // 2-op feedback FM: modulator self-feedback at K2 CW (linear from 0)
constexpr float EXP_FS_MAX_HZ     = 800.f;   // frequency shift: |shift| at K2 full CW / CCW
constexpr float EXP_FS_CURVE_HZ   = 1.f;     // frequency shift: s = sign(t) F0 ((MAX/F0 + 1)^|t| - 1), F0 = this (exponential-ish, 0 at noon)
constexpr float EXP_FS_REF_HZ     = 800.f;   // frequency shift, K3 proportional end: shift_k = s f'_k / this (K2 end = +-100 % of the band freq)

// --- SW1 MID: phase distortion ------------------------------------------------
constexpr float EXP_SYNC_R_MAX    = 8.f;     // hard sync: slave ratio at K2 CW (linear from 1)
constexpr float EXP_CZ_KNEE       = 0.01f;   // CZ two-segment bend: knee at K2 CW (d = 0.5 (KNEE / 0.5)^K2; = ARM_PD_KNEE)
constexpr float EXP_CZ_FOLD_HZ    = 500.f;   // CZ bend anti-fold: knee floor rises from KNEE at this f'_k ... (= ARM_PD_FOLD_HZ)
constexpr float EXP_CZ_FOLD_TOP   = 3800.f;  // ... to 0.5 (sine) at this f'_k (= ARM_PD_FOLD_TOP)
constexpr float EXP_CZ_RES_MAX    = 8.f;     // CZ resonant triangle / trapezoid: resonance ratio at K2 CW (linear from 1; = ARM_RES_R_MAX)
constexpr float EXP_FOLD_MAX      = 8.f;     // mirror / fold: folds at K2 CW (linear from 1)
constexpr float EXP_FORM_MAX      = 16.f;    // formant pulses: formant ratio x band freq at K2 CW (linear from 1)
constexpr float EXP_HARM_MAX      = 16.f;    // exact harmonics: harmonic number at K2 CW (linear from 1, continuous crossfade)

// --- SW1 DOWN: digital ----------------------------------------------------------
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
constexpr float EXP_PQ_STEPS_MAX  = 64.f;    // phase quantisation: steps per cycle at K2 CCW (exponential to MIN)
constexpr float EXP_PQ_STEPS_MIN  = 2.f;     // phase quantisation: steps per cycle at K2 CW
constexpr int   EXP_WAL_N_MAX     = 16;      // Walsh: highest function index on K2 (sequency 1..16, stepped)
constexpr float EXP_CRUSH_BITS_MAX = 12.f;   // bit crush: bits at K2 CCW (linear, continuous)
constexpr float EXP_CRUSH_BITS_MIN = 1.f;    // bit crush: bits at K2 CW
constexpr float EXP_CRUSH_RATE_MAX = 8000.f; // Hz; per-band sample & hold rate at K3 CCW (exponential)
constexpr float EXP_CRUSH_RATE_MIN = 100.f;  // Hz; at K3 CW

// --- Octave (SW2 select, K5 mix) ---------------------------------------------------
constexpr float EXP_OCT_XFADE_MS  = 10.f;    // ms; linear slide of the octave-side split on an SW2 change (= ARM_XFADE_MS)
constexpr float EXP_OCT_BOTH      = 0.5f;    // SW2 MID: up / down share of the octave side each

// --- Control smoothing / snapping ----------------------------------------------------
constexpr float EXP_CTRL_SMOOTH_MS = 10.f;   // ms; one-pole on K2..K5 at the 8 kHz rate (= ARM_CTRL_SMOOTH_MS)
constexpr float EXP_CTRL_SNAP      = 1e-6f;  // smoothed knob lands exactly on its target within this (K5 = 0 skips octave work)

// --- Band sum: phase alignment + gain staging (copied values from armitage_constants.h, ARM_ALIGN = true) ---
// Played slot: odd bands negated (z -> -z) before everything, output weight w_k = 10^(W_dB(log2 f_k) / 20).
// K4 CCW (unshaped) with K5 CCW = armitage's unshaped band sum at SW2 MID (unity vs dry).
constexpr float EXP_ALIGN_WA    = -0.1583384f;   // dB per log2(Hz)^2 (= ARM_ALIGN_WA)
constexpr float EXP_ALIGN_WB    =  1.5180589f;   // dB per log2(Hz)   (= ARM_ALIGN_WB)
constexpr float EXP_ALIGN_WC    = -1.9726554f;   // dB                (= ARM_ALIGN_WC)
constexpr float EXP_ALIGN_W_FLO =  41.2f;        // Hz; weight held below (= ARM_ALIGN_W_FLO)
constexpr float EXP_ALIGN_W_FHI = 330.f;         // Hz; weight held above (= ARM_ALIGN_W_FHI)
constexpr float EXP_GAIN_UP     = 1.139f;        // octave-up slot gain, unity vs played at K4 CCW (= ARM_GAIN_UP)
constexpr float EXP_GAIN_DOWN   = 1.445f;        // octave-down slot gain (= ARM_GAIN_DOWN)
constexpr float EXP_OUT_GAIN    = 1.487f;        // wet trim (= ARM_OUT_GAIN)
constexpr float EXP_SILENCE     = 1e-10f;        // |z|^2 below this: band silent (= ARM_SILENCE)
constexpr float EXP_DENORM      = 1e-12f;        // feedback FM state below this flushed to 0 (= ARM_DENORM)

// --- Wet output chain (copied from armitage) ---------------------------------------------
constexpr float EXP_DC_HZ     = 10.f;     // wet DC blocker (= ARM_DC_HZ)
constexpr float EXP_SAT_T     = 0.7f;     // soft clipper knee / ceiling (= ARM_SAT_T)
constexpr float EXP_SAT_TRIM  = 1.0f;     // soft clipper fixed post trim (= ARM_SAT_TRIM)
constexpr float EXP_MUTE_SLIDE_MS = 1.0f; // FS1 wet mute slide (= ARM_MUTE_SLIDE_MS)
constexpr int   EXP_SIN_SIZE  = 1024;     // shared sin table entries (power of two; = ARM_SIN_SIZE)

// --- DIAG "note this setting" log (DIAG builds only) --------------------------------------
constexpr float    EXP_DIAG_KNOB_EPS  = 0.02f;  // knob move that counts as a change
constexpr uint32_t EXP_DIAG_SETTLE_MS = 600;    // ms without change before an `auto` line
constexpr uint32_t EXP_DIAG_BLINK_MS  = 150;    // ms LED1 goes off to confirm an FS1 tap

}  // namespace explorer_k
