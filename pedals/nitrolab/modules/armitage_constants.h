#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

#include <cstdint>

namespace armitage_k {

// --- Mode selector (K5, 2026-10-20; SW1 reads nothing) -----------------------
// K5 zones CCW -> CW: [0, Z0) SAW, [Z0, Z1) PULSE, [Z1, Z2) ADD, [Z2, 1] FEEDBACK
// (4 equal fifths; the top fifth is reserved and plays FEEDBACK too). A zone change
// crossfades the band outputs of the two modes linearly over ARM_MODEL_XFADE_MS.
constexpr float ARM_MODE_ZONE[3] = { 0.2f, 0.4f, 0.6f };

// --- K1 (timbre, per mode) ----------------------------------------------------
// FB: beta = ARM_BETA_MAX * k1^2 (full travel, as before). ADD: K1 split at
// ARM_K1_SPLIT into u / v (below, unchanged). SAW: resonance (R, morph). PULSE: width.
constexpr float ARM_BETA_MAX   = 4.4f;    // rad; K1 full CW (squared taper), the Ensemble Oscillator's max
constexpr float ARM_BETA_CLAMP = 8.8f;    // rad; safety ceiling on any band's beta_k (K4 law gives at most beta, so never reached)
constexpr float ARM_K1_SPLIT   = 0.6f;    // K1 split point: below = today's ADD/PD law (scaled), above = extended range

// --- FB anti-fold limit (always on): beta_k <= max(0, ARM_FOLD_LIMIT_HZ / f'_k - 1) ---
// Carson's rule: a feedback-FM band at f'_k with index beta_k spreads to about
// (beta_k + 1) f'_k; capping beta_k keeps the generated overtones below the
// 8 kHz engine's Nyquist instead of folding back. Applied to the final per-band
// beta_k (min with the K4 law). The K3 cross-mod term is NOT capped.
constexpr bool  ARM_ANTIFOLD      = true;     // false = no cap (for A/B comparison)
constexpr float ARM_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine

// --- Shaping tilt (K4): tilt_k = min(1, (ARM_SHAPE_REF / f'_k)^p), all models ---
// FB: beta_k = beta * tilt_k. ADD / PD: amt_k = s_k * tilt_k.
constexpr float ARM_SHAPE_REF   = 150.f;  // Hz; transposed band freq f'_k at/below which a band gets the full shaping (never more)
constexpr float ARM_SHAPE_P_MAX = 2.f;    // tilt exponent p at K4 CCW (p = this * (1 - k4); CW p = 0 = full shaping on every band)

// --- K2 = micro pitch shift detune ("Eventide micro pitch" style) on the final 48 kHz wet ---
// 4 delay-based pitch shifters (voices -out, -in, +in, +out) read one circular buffer of
// the wet; voice v at ratio r_v = 2^(ARM_MP_SPREAD[v] D / 1200), two taps half a window
// apart, sin^2 / cos^2 crossfade (see armitage.h MicroPitch()). Staging along smoothed K2:
// g_in = clamp(k2 / ARM_SS_IN_END), g_out = clamp((k2 - ARM_SS_IN_END) / (ARM_SS_OUT_END - ARM_SS_IN_END)),
// D = ARM_SS_D_MIN up to ARM_SS_OUT_END, then linear to ARM_SS_D_MAX at k2 = 1.
// out = (wet + g_in (sh_-in + sh_+in) + g_out (sh_-out + sh_+out)) * (1 + 2 g_in^2 + 2 g_out^2)^-ARM_MP_COMP_EXP.
constexpr float ARM_SS_IN_END     = 0.3f;    // K2 at which the inner pair is fully in (g_in 0 -> 1 over 0 .. this)
constexpr float ARM_SS_OUT_END    = 0.6f;    // K2 at which the outer pair is fully in (g_out 0 -> 1 from ARM_SS_IN_END); D widens above
constexpr float ARM_SS_D_MIN      = 10.f;    // cents; outer-voice detune D for K2 <= ARM_SS_OUT_END (Schism's minimum)
constexpr float ARM_SS_D_MAX      = 35.f;    // cents; outer-voice detune D at K2 full CW (Schism's maximum)
constexpr float ARM_SS_ON         = 1e-4f;   // pair gain below this (whole block): that pair's shifters are skipped (-80 dB)
constexpr float ARM_MP_SPREAD[4]  = { -1.0f, -0.468f, 0.468f, 1.0f };   // x D: -out, -in, +in, +out (Schism 0.234 : 0.5, outer = D)
constexpr float ARM_MP_WIN_MS[4]  = { 41.f, 47.f, 53.f, 59.f };         // ms; window W_v of -out, -in, +in, +out (all different: crossfades do not line up)
constexpr float ARM_MP_COMP_EXP   = 0.5f;    // level compensation exponent on (1 + 2 g_in^2 + 2 g_out^2) (0.5 = uncorrelated voices)
constexpr int   ARM_MP_BUF        = 4096;    // samples (power of two, 85 ms at 48 kHz = 16 KB, D2 SRAM): longest window + margin
static_assert((ARM_MP_BUF & (ARM_MP_BUF - 1)) == 0, "ARM_MP_BUF: power of two");
static_assert(ARM_MP_WIN_MS[3] * 48.f + 2.f < (float)ARM_MP_BUF, "ARM_MP_BUF: longest window (ARM_MP_WIN_MS[3], at 48 kHz) + interpolation margin");

// --- Wet DC blocker (final 48 kHz wet, before K2 MicroPitch) ---------------
constexpr float ARM_DC_HZ = 10.f;  // wet DC blocker (FB carries DC); below the lowest bass fundamental

// --- Feedback smoothing (FB only, fixed since K5 became the mode selector):
// per-band one-pole LP on yprev at 8 kHz, cutoff_k = c * f'_k (relative to the transposed band freq).
// c = the K5-noon value of the former K5 law c = 8 * (0.5 / 8)^k5 (8 at CCW -> 0.5 at CW): 8 * 0.25 = 2.0.
constexpr float ARM_SMOOTH_REL_FIXED = 2.f;   // cutoff 2 x band freq
constexpr float ARM_SMOOTH_HZ_LO   = 20.f;    // Hz; lower clamp of any band's cutoff
constexpr float ARM_SMOOTH_HZ_HI   = 3800.f;  // Hz; upper clamp of any band's cutoff (below Nyquist of the 8 kHz rate)

// --- Fixed reference level (feedback signal + ADD/PD saw amount) ----------
// No normalisation of any kind: shaping depth follows the absolute playing level.
// FB: yprev = out / ARM_REF_LEVEL (then clamped to [-1, 1]). ADD/PD: per-band amount
// s_k = min(1, |z'_k| / ARM_REF_LEVEL). A band at this |z'| gets the full shaping;
// play harder -> more bands reach it (FB: more feedback), softer -> less.
// Value = median of the old relative reference L_ref (loudest band |z'|) over the
// sounding notes of the DI bass excerpts at passive level (peak 0.1):
// min 0.0007 / median 0.0209 / max 0.034 (p10 0.014, p90 0.029).
constexpr float ARM_REF_LEVEL  = 0.021f;  // band |z'| (8 kHz band engine units) that counts as "full"

// --- Modes (K5): SAW / PULSE (PD base), ADD, FB -----------------------------
constexpr float ARM_MODEL_XFADE_MS = 1.f;   // ms; linear crossfade old -> new mode output on a K5 zone change (was 10 ms on SW1)
// FB "replace" gain on the shaping delta (2026-10-20, as the PD base): out_k = y0_k + ARM_GAIN_FB share_k (y_k - y0_k)
// in every octave (the *_DELTA octave gains do not apply), feedback signal at full amount
// (yprev = y_k / |z'_k|, every band above ARM_SILENCE). Set by structure, not by ear (unheard).
constexpr float ARM_GAIN_FB    = 2.f;     // was 1.0 (with yprev = y_k / ARM_REF_LEVEL)
constexpr float ARM_GAIN_ADD   = 0.54f;   // ADD makeup on the shaping delta (out_k = y0_k + g_model share_k (shaped_k - y0_k); y0 unscaled, K1 = 0 = unity in every model): matched to FB wet RMS at K1 = 1 on the DI bass excerpts at passive level (peak 0.1), SW2 MID, K2 K3 0, K4 K5 noon (2026-10-20; was 1.02 on the whole output)
// SAW / PULSE "replace" gain G on the shaping delta, every octave (the *_DELTA octave gains do not apply):
// out_k = y0_k + G share_k a_k (shaped_k - y0_k). Approved on the host at 2.0048 (DI 2, var8), rounded.
constexpr float ARM_GAIN_PD    = 2.f;     // was 0.88 (HEAD PD makeup, K1 knee law)

// ADD (K5 3rd zone): additive saw per band, harmonics m = 2..ARM_ADD_MMAX of u = z'/|z'|.
// u = min(K1/ARM_K1_SPLIT, 1), v = max((K1-ARM_K1_SPLIT)/(1-ARM_K1_SPLIT), 0).
// M_eff = 1 + u*(ARM_ADD_MID-1) + v*(ARM_ADD_MMAX-ARM_ADD_MID)   (1 -> 8 -> 16)
// harmonic weight = g_m / m^s, s = 1 - (1-ARM_ADD_S_MIN)*v       (1 -> 1 -> 0.6)
constexpr int   ARM_ADD_MID    = 8;       // highest harmonic at K1 = ARM_K1_SPLIT (today's old ceiling)
constexpr int   ARM_ADD_MMAX   = 16;      // highest harmonic at K1 = 1 (extended upper range)
constexpr float ARM_ADD_S_MIN  = 0.6f;    // harmonic-weight exponent s at K1 = 1 (1/m -> 1/m^0.6: brighter top)
constexpr float ARM_ADD_FMAX   = 3800.f;  // Hz; a harmonic at m f'_k above this is dropped (anti-fold)
constexpr float ARM_ADD_FEDGE  = 400.f;   // Hz; fade width below ARM_ADD_FMAX (weight 1 at 3400 Hz -> 0 at 3800 Hz)
// Overlap share on the shaping delta (ALL models, 2026-10-20):
// out_k = y0_k + share_k * (shaped_k - y0_k),  y0_k = Re(z'_k e^{j xm}) (the unshaped band),
// share_k = |z'_k| / max(|z'_{k-1}| + |z'_k| + |z'_{k+1}|, ARM_SHARE_FLOOR)
// (edge bands: the one existing neighbour). K1 = 0: plain band sum (flat vs pitch); the
// shaping of a partial that rings in 2-3 adjacent bands counts about once (shaped overlapping
// bands stopped cancelling: +9 dB at K1 = 1). ADD: the delta is the harmonic term.
// FB: the feedback signal yprev is taken from the unscaled band output. No threshold,
// no picking, no smoothing.
constexpr float ARM_SHARE_FLOOR = 1e-5f;  // share denominator floor = sqrt(ARM_SILENCE): no 0/0 (a sounding band is >= this already)

// PD base (SAW, PULSE; 2026-10-20): every band above ARM_SILENCE at the full knee (no level
// amount s_k), the K1 = 1 values of the former K1 knee law, now fixed:
//   knee time ARM_PD_KNEE = 0.01 (= 0.5 - (0.45 + 0.04), the old depth at K1 = 1)
//   anti-fold knee floor dfloor_k = ARM_PD_KNEE + (0.5 - ARM_PD_KNEE) clamp((f'_k - ARM_PD_FOLD_HZ) / (ARM_PD_FOLD_TOP - ARM_PD_FOLD_HZ), 0, 1)
//   cap_k = (0.5 - dfloor_k) / (0.5 - ARM_PD_KNEE)   (1 up to 1 kHz, 0 at 3.8 kHz)
// SAW knee: d_k = max(0.5 - (0.5 - ARM_PD_KNEE) tilt_k, dfloor_k)  (tilt and cap inside the knee, a_k = 1)
// PULSE / resonance: shape at knee ARM_PD_KNEE, amount a_k = tilt_k cap_k
constexpr float ARM_PD_KNEE       = 0.01f;   // cycles; knee time of the saw / pulse jumps
constexpr float ARM_PD_FOLD_HZ    = 1000.f;  // Hz; full-depth anti-fold corner (the K1 = 1 value of the old 500 -> 1000 law)
constexpr float ARM_PD_FOLD_TOP   = 3800.f;  // Hz; f'_k at/above this: knee floor 0.5 = sine / no pulse (linear in between)

// SAW, K1 = resonance (CZ "reso" on the linear phase, DC removed):
//   R = ARM_RES_R_MIN + (ARM_RES_R_MAX - ARM_RES_R_MIN) k1     (linear, 1 -> 16; noon 8.5)
//   R_k = min(R, ARM_RES_FMAX / f'_k)
//   res_k = |z'_k| (sin(2 pi R_k phi) (1 - phi) - dc(R_k)),  dc(R) = 1/(2 pi R) - sin(2 pi R)/(2 pi R)^2 (exact mean)
//   out_k = y0_k + G share_k [ (1 - k1) (saw_k - y0_k) + k1 tilt_k cap_k (res_k - y0_k) ]   (= (1 - k1) out_saw + k1 out_res)
constexpr float ARM_RES_R_MIN = 1.f;
constexpr float ARM_RES_R_MAX = 16.f;
constexpr float ARM_RES_FMAX  = 3800.f;   // Hz; R_k f'_k capped here (anti-fold)

// PULSE, K1 = width: w = ARM_PUL_W_MAX - (ARM_PUL_W_MAX - ARM_PUL_W_MIN) k1 (50 % CCW -> 0 % CW),
// two independent knee steps (down at phi = 0, up at phi = w, ARM_PD_KNEE each) that may overlap;
// mean 1 - 2w removed (exact for any w in [0, 0.5]). w = 0: the steps cancel, S = +1, silence.
constexpr float ARM_PUL_W_MAX = 0.5f;
constexpr float ARM_PUL_W_MIN = 0.f;

// --- K3 = 2-op FM per band, all modes (2026-10-20) ---------------------------
// phi_mod = phi_k + I sin(phi_down_k), I = ARM_FM_INDEX_MAX * k3 (rad, linear), applied to the
// carrier phase before the shape. Modulator = the carrier's own octave-down from the band:
// SW2 MID: Down1(z_k) (half phase); UP: z_k (the played band); DOWN: Down1(z_k) = the carrier (1:1).
constexpr float ARM_FM_INDEX_MAX = 3.f;   // rad at K3 full CW

// --- Cross-modulation (former K3): theta_k += beta_x * m — off since K3 = FM ---
constexpr bool  ARM_XMOD_ENABLE = false;  // true: K3 drives beta_x = ARM_XMOD_MAX * k3^2 as well (on top of the FM)
constexpr float ARM_ROOT_HZ    = 150.f;   // Hz; bands with input centre f_k below this form the root modulator m (bands 0..13, 25 .. 148 Hz)
constexpr float ARM_XMOD_MAX   = 6.f;     // rad; beta_x at K3 full CW (squared taper), Ensemble "Up"-mode depth
constexpr float ARM_XMOD_DEN_FLOOR = 0.1f; // K3 modulator denominator floor = this * ARM_REF_LEVEL (fixed): root-band leakage near silence no longer jumps m (clicks); 1.0 = stronger, also tames K3 on notes without root content

// --- Phase-aligned band sum (SW2 MID only) ----------------------------------
// Adjacent bands of the bank are 180 deg apart at every crossover (measured: the
// rotation that aligns band k is (-1)^k within 1.5 deg), so the plain band sum
// notched between centres. ARM_ALIGN: every odd band's z_k is negated (z -> -z)
// BEFORE the shaping / K3 rotation (and before the K3 root modulator), and each band
// gets the fitted output weight
//   w_k = 10^(W_dB(x) / 20),  W_dB(x) = ARM_ALIGN_WA x^2 + ARM_ALIGN_WB x + ARM_ALIGN_WC,
//   x = log2(clamp(f_k, ARM_ALIGN_W_FLO, ARM_ALIGN_W_FHI))   (smooth, 0.96 .. 1.20)
// SW2 UP: the squaring removes the sign -> untouched. SW2 DOWN: stays unrotated
// (a rotated DOWN rippled 20.9 dB). false = the unaligned bank (HEAD 2d128a2, bit-identical;
// ARM_OUT_GAIN / ARM_GAIN_UP / ARM_GAIN_DOWN / *_DELTA revert with it).
constexpr bool  ARM_ALIGN       = true;
constexpr float ARM_ALIGN_WA    = -0.1583384f;   // dB per log2(Hz)^2 (fit 2026-10-20, DI + KS chromatic E1..E4)
constexpr float ARM_ALIGN_WB    =  1.5180589f;   // dB per log2(Hz)
constexpr float ARM_ALIGN_WC    = -1.9726554f;   // dB
constexpr float ARM_ALIGN_W_FLO =  41.2f;        // Hz; weight held below (E1)
constexpr float ARM_ALIGN_W_FHI = 330.f;         // Hz; weight held above (~E4)

// --- Octave (SW2) ------------------------------------------------------------
constexpr float ARM_XFADE_MS   = 10.f;    // ms; linear crossfade old -> new octave path on an SW2 change
constexpr float ARM_GAIN_UP    = ARM_ALIGN ? 1.139f : 0.30f;   // ALIGN (2026-10-20): unity vs MID at K1 = 0 on the DI bass (passive), 0.0 dB; unaligned 0.30 // (2026-10-20: checked on the DI bass at K1 = 0: UP -0.1 dB vs MID) (2026-10-17: was 0.36 before the interpolator fix, which raised UP by +1.6 dB vs MID)
//    // octave-up makeup: unity-gain UP measured +8.3..+9.2 dB over MID (KS gtr+bass) -> -8.9 dB
constexpr float ARM_GAIN_DOWN  = ARM_ALIGN ? 1.445f : 0.40f;   // ALIGN (2026-10-20): unity vs MID at K1 = 0 the same way, 0.0 dB; unaligned 0.40 // (2026-10-20: checked on the DI bass at K1 = 0: DOWN +0.4 dB vs MID) (2026-10-17: was 0.38 before the interpolator fix, which lowered DOWN by -0.4 dB vs MID)
//    // octave-down makeup: unity-gain DOWN measured +8.1..+8.5 dB over MID -> -8.4 dB
// Octave makeup on the shaping delta only (on top of ARM_GAIN_UP / DOWN, which scale the whole
// slot): out_k = y0_k + g_model * g_oct_delta * share_k * (shaped_k - y0_k); MID = 1.
// ARM_GAIN_UP / DOWN are set on the unshaped band sum, which gains +8..+10 dB under the
// transposition; the shaping delta does not (within -1..+1.4 dB of MID), so without this UP / DOWN sat 2..7 dB under MID at K1 = 1.
constexpr float ARM_GAIN_UP_DELTA   = ARM_ALIGN ? 1.15f : 3.59f;   // ALIGN refit 2026-10-20 (the aligned MID sum is +11.5 dB louder, ARM_OUT_GAIN / GAIN_UP rescaled): UP - MID at K1 = 1 FB +0.35 / ADD +0.07 / PD +0.50 dB (the floor: below ~1.2 UP gets louder again, not quieter). Unaligned 3.59: fit 2026-10-20: UP at K1 = 1 = MID at K1 = 1 (wet RMS, mean of FB / ADD / PD, DI bass at passive level, K2 K3 0, K4 K5 noon); residuals FB +0.0 / ADD -0.2 / PD +0.1 dB
constexpr float ARM_GAIN_DOWN_DELTA = ARM_ALIGN ? 1.05f : 2.93f;   // ALIGN refit the same way: DOWN - MID at K1 = 1 FB -0.09 / ADD +0.11 / PD +0.39 dB. Unaligned 2.93: fit the same way for DOWN; residuals FB -0.4 / ADD +0.3 / PD +0.1 dB

// --- Guards -----------------------------------------------------------------
constexpr float ARM_SILENCE    = 1e-10f;  // |z|^2 below this -> band output 0, yprev reset (FastInvSqrt(0) guard)
constexpr float ARM_DENORM     = 1e-12f;  // |yprev| below this is flushed to 0

// --- Control smoothing (anti-zipper on the K1/K2/K3/K4/K5 targets) ---
constexpr float ARM_CTRL_SMOOTH_MS = 10.f;  // ms; one-pole time constant at the 8 kHz rate

// --- Output -----------------------------------------------------------------
constexpr float ARM_OUT_GAIN   = ARM_ALIGN ? 1.487f : 5.6f;    // ALIGN (2026-10-20): the aligned MID sum is +11.5 dB louder; 1.487 = unity (+0.0 dB) at K1 = 0 the same way. Unaligned 5.6: wet trim: K1 = 0 (SW2 MID, K2 K3 0, K4 K5 noon) wet RMS = dry RMS on the DI bass
//     // excerpts at passive level (peak 0.1) (2026-10-20; was 7.4, set on KS plucks: +2.3 dB
//     // over dry on the DI; K1 = 0 is unaffected by the overlap share)

// --- Wet soft clipper (final 48 kHz wet, after K2 MicroPitch, before the FS1 mute) ---
// wet = ARM_SAT_TRIM * ARM_SAT_T * tanh(wet / ARM_SAT_T) (Pade(3,3) tanh, see armitage.h).
// Fixed post trim, no 1/drive compensation: |wet| <= ARM_SAT_TRIM * ARM_SAT_T.
constexpr float ARM_SAT_T    = 0.7f;  // knee: ~linear below |x| ~ 0.3 (-0.5 dB at 0.3), ceiling 0.7
constexpr float ARM_SAT_TRIM = 1.0f;  // fixed post trim

// --- Sin table ----------------------------------------------------------------
constexpr int   ARM_SIN_SIZE   = 1024;    // entries per period (power of two; mask wrap)

// --- FS1 wet mute (test tool: A/B against dry for unity-gain testing) ------
constexpr float ARM_MUTE_SLIDE_MS = 1.0f;   // ms; linear slide of mute_g_ to the target (0 or 1)

// --- DIAG "note this setting" log (DIAG builds only; no effect otherwise) ---
constexpr float    ARM_DIAG_KNOB_EPS  = 0.02f;  // knob move (remapped 0..1) that counts as a change
constexpr uint32_t ARM_DIAG_SETTLE_MS = 600;    // ms without change before an `auto` line
constexpr uint32_t ARM_DIAG_BLINK_MS  = 150;    // ms LED1 goes off to confirm an FS1 tap

}  // namespace armitage_k
