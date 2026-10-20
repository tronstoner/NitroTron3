#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

#include <cstdint>

namespace armitage_k {

// --- Shape amount (K1) -----------------------------------------------------
// FB: beta = ARM_BETA_MAX * k1^2 (unchanged, full K1 travel). ADD / PD: K1 is
// split at ARM_K1_SPLIT into u (0..split, scaled to 0..1) and v (split..1,
// scaled to 0..1) — see armitage.h Process() and the ADD/PD sections below.
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

// --- Feedback smoothing (K5, FB model only): per-band one-pole LP on yprev at 8 kHz
// cutoff_k = c * f'_k (relative to the transposed band freq), c exponential K5 CCW -> CW
constexpr float ARM_SMOOTH_REL_MAX = 8.f;     // c at K5 CCW (cutoff 8 x band freq = little damping)
constexpr float ARM_SMOOTH_REL_MIN = 0.5f;    // c at K5 CW (cutoff half the band freq = strong damping)
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

// --- Shaping model (SW1): UP = FB, MID = ADD, DOWN = PD ----------------------
constexpr float ARM_MODEL_XFADE_MS = 10.f;  // ms; linear crossfade old -> new model output on an SW1 change
constexpr float ARM_GAIN_FB    = 1.f;     // FB makeup (on the shaping delta): the reference the other models are matched to
constexpr float ARM_GAIN_ADD   = 0.54f;   // ADD makeup on the shaping delta (out_k = y0_k + g_model share_k (shaped_k - y0_k); y0 unscaled, K1 = 0 = unity in every model): matched to FB wet RMS at K1 = 1 on the DI bass excerpts at passive level (peak 0.1), SW2 MID, K2 K3 0, K4 K5 noon (2026-10-20; was 1.02 on the whole output)
constexpr float ARM_GAIN_PD    = 0.88f;   // PD makeup on the shaping delta, matched to FB the same way (2026-10-20; was 1.01 on the whole output)

// ADD (SW1 MID): additive saw per band, harmonics m = 2..ARM_ADD_MMAX of u = z'/|z'|.
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

// PD (SW1 DOWN): Casio-CZ phase-distortion saw per band, knee d = 0.5 - (ARM_PD_DEPTH*u + ARM_PD_DEPTH_HI*v) * amt_k
// (u, v as above). At K1 <= ARM_K1_SPLIT this is exactly the old law scaled onto u.
// At K1 = 1, amt_k = 1: d = 0.5 - (0.45 + 0.04) = 0.01.
constexpr float ARM_PD_DEPTH     = 0.45f;  // knee travel vs u: d = 0.5 (sine) .. 0.05 (saw-like) at u = 1, amt_k = 1
constexpr float ARM_PD_DEPTH_HI  = 0.04f;  // extra knee travel vs v: pushes the floor 0.05 -> 0.01 at v = 1, amt_k = 1
constexpr float ARM_PD_FOLD_HZ_LO = 500.f;   // Hz; full-depth anti-fold corner at v = 0
constexpr float ARM_PD_FOLD_HZ_HI = 1000.f;  // Hz; full-depth anti-fold corner at v = 1 (moves 500 -> 1000 with v)
constexpr float ARM_PD_FOLD_TOP   = 3800.f;  // Hz; f'_k at/above this: knee floor 0.5 = sine (linear floor in between)

// --- Cross-modulation (K3): theta_k += beta_x * m --------------------------
constexpr float ARM_ROOT_HZ    = 150.f;   // Hz; bands with input centre f_k below this form the root modulator m (bands 0..13, 25 .. 148 Hz)
constexpr float ARM_XMOD_MAX   = 6.f;     // rad; beta_x at K3 full CW (squared taper), Ensemble "Up"-mode depth
constexpr float ARM_XMOD_DEN_FLOOR = 0.1f; // K3 modulator denominator floor = this * ARM_REF_LEVEL (fixed): root-band leakage near silence no longer jumps m (clicks); 1.0 = stronger, also tames K3 on notes without root content

// --- Octave (SW2) ------------------------------------------------------------
constexpr float ARM_XFADE_MS   = 10.f;    // ms; linear crossfade old -> new octave path on an SW2 change
constexpr float ARM_GAIN_UP    = 0.30f;   // (2026-10-20: checked on the DI bass at K1 = 0: UP -0.1 dB vs MID) (2026-10-17: was 0.36 before the interpolator fix, which raised UP by +1.6 dB vs MID)
//    // octave-up makeup: unity-gain UP measured +8.3..+9.2 dB over MID (KS gtr+bass) -> -8.9 dB
constexpr float ARM_GAIN_DOWN  = 0.40f;   // (2026-10-20: checked on the DI bass at K1 = 0: DOWN +0.4 dB vs MID) (2026-10-17: was 0.38 before the interpolator fix, which lowered DOWN by -0.4 dB vs MID)
//    // octave-down makeup: unity-gain DOWN measured +8.1..+8.5 dB over MID -> -8.4 dB

// --- Guards -----------------------------------------------------------------
constexpr float ARM_SILENCE    = 1e-10f;  // |z|^2 below this -> band output 0, yprev reset (FastInvSqrt(0) guard)
constexpr float ARM_DENORM     = 1e-12f;  // |yprev| below this is flushed to 0

// --- Control smoothing (anti-zipper on the K1/K2/K3/K4/K5 targets) ---
constexpr float ARM_CTRL_SMOOTH_MS = 10.f;  // ms; one-pole time constant at the 8 kHz rate

// --- Output -----------------------------------------------------------------
constexpr float ARM_OUT_GAIN   = 5.6f;    // wet trim: K1 = 0 (SW2 MID, K2 K3 0, K4 K5 noon) wet RMS = dry RMS on the DI bass
//     // excerpts at passive level (peak 0.1) (2026-10-20; was 7.4, set on KS plucks: +2.3 dB
//     // over dry on the DI; K1 = 0 is unaffected by the overlap share)

// --- Sin table ----------------------------------------------------------------
constexpr int   ARM_SIN_SIZE   = 1024;    // entries per period (power of two; mask wrap)

// --- FS1 wet mute (test tool: A/B against dry for unity-gain testing) ------
constexpr float ARM_MUTE_SLIDE_MS = 1.0f;   // ms; linear slide of mute_g_ to the target (0 or 1)

// --- DIAG "note this setting" log (DIAG builds only; no effect otherwise) ---
constexpr float    ARM_DIAG_KNOB_EPS  = 0.02f;  // knob move (remapped 0..1) that counts as a change
constexpr uint32_t ARM_DIAG_SETTLE_MS = 600;    // ms without change before an `auto` line
constexpr uint32_t ARM_DIAG_BLINK_MS  = 150;    // ms LED1 goes off to confirm an FS1 tap

}  // namespace armitage_k
