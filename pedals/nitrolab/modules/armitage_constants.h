#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

#include <cstdint>

namespace armitage_k {

// --- Feedback amount (K1) -------------------------------------------------
constexpr float ARM_BETA_MAX   = 4.4f;    // rad; K1 full CW (squared taper), the Ensemble Oscillator's max
constexpr float ARM_BETA_CLAMP = 8.8f;    // rad; safety ceiling on any band's beta_k (K4 law gives at most beta, so never reached)

// --- Anti-fold limit (always on): beta_k <= max(0, ARM_FOLD_LIMIT_HZ / f'_k - 1) ---
// Carson's rule: a feedback-FM band at f'_k with index beta_k spreads to about
// (beta_k + 1) f'_k; capping beta_k keeps the generated overtones below the
// 8 kHz engine's Nyquist instead of folding back. Applied to the final per-band
// beta_k (min with the K4 law). The K3 cross-mod term is NOT capped.
constexpr bool  ARM_ANTIFOLD      = true;     // false = no cap (for A/B comparison)
constexpr float ARM_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine

// --- Shaping tilt (K4): beta_k = beta * min(1, (ARM_SHAPE_REF / f'_k)^p) ---
constexpr float ARM_SHAPE_REF   = 150.f;  // Hz; transposed band freq f'_k at/below which a band gets the full beta (never more)
constexpr float ARM_SHAPE_P_MAX = 2.f;    // tilt exponent p at K4 CCW (p = this * (1 - k4); CW p = 0 = beta on every band)

// --- Roll-off (K2): w_k = min(1, (f_k / ARM_TILT_REF)^-gamma) ---------------
constexpr float ARM_TILT_MAX   = 1.f;     // roll-off exponent gamma at K2 full CW (~ -6 dB/oct above the ref)
constexpr float ARM_TILT_REF   = 100.f;   // Hz; roll-off corner (on the band's INPUT centre f_k), never boosts

// --- Feedback smoothing (K5): per-band one-pole LP on yprev at 8 kHz --------
// cutoff_k = c * f'_k (relative to the transposed band freq), c exponential K5 CCW -> CW
constexpr float ARM_SMOOTH_REL_MAX = 8.f;     // c at K5 CCW (cutoff 8 x band freq = little damping)
constexpr float ARM_SMOOTH_REL_MIN = 0.5f;    // c at K5 CW (cutoff half the band freq = strong damping)
constexpr float ARM_SMOOTH_HZ_LO   = 20.f;    // Hz; lower clamp of any band's cutoff
constexpr float ARM_SMOOTH_HZ_HI   = 3800.f;  // Hz; upper clamp of any band's cutoff (below Nyquist of the 8 kHz rate)

// --- Feedback signal blend (SW1): yprev = out * ((1 - a) / L_ref + a * ARM_RAW_GAIN)
// L_ref = loudest band |z'| of the slot (instant attack, ARM_REL_RELEASE_MS release,
// floored at ARM_REL_FLOOR): the loudest band gets out/|z|, a band at -20 dB 1/10.
constexpr float ARM_RAW_GAIN   = 100.f;   // raw path gain: loudest band ~0.0005 soft gtr -> 0.05 (sine), ~0.012 hard bass -> 1 (clamped, saw/noise)
constexpr float ARM_REL_RELEASE_MS = 50.f;    // ms; one-pole release of L_ref (attack is instant)
constexpr float ARM_REL_FLOOR  = 1e-5f;   // L_ref floor = sqrt(ARM_SILENCE): silence never divides by ~0
constexpr float ARM_BLEND_UP   = 0.f;     // a at SW1 UP   (relative: out / L_ref, level-independent)
constexpr float ARM_BLEND_MID  = 0.5f;    // a at SW1 MID  (half relative, half raw)
constexpr float ARM_BLEND_DOWN = 1.f;     // a at SW1 DOWN (raw, level-dependent)
constexpr float ARM_BLEND_MS   = 10.f;    // ms; linear ramp of a on an SW1 change (full 0 -> 1 in this time)

// --- Cross-modulation (K3): theta_k += beta_x * m --------------------------
constexpr float ARM_ROOT_HZ    = 150.f;   // Hz; bands with input centre f_k below this form the root modulator m (bands 0..9)
constexpr float ARM_XMOD_MAX   = 6.f;     // rad; beta_x at K3 full CW (squared taper), Ensemble "Up"-mode depth

// --- Octave (SW2) ------------------------------------------------------------
constexpr float ARM_XFADE_MS   = 10.f;    // ms; linear crossfade old -> new octave path on an SW2 change
constexpr float ARM_GAIN_UP    = 0.30f;   // (2026-10-17: was 0.36 before the interpolator fix, which raised UP by +1.6 dB vs MID)
//    // octave-up makeup: unity-gain UP measured +8.3..+9.2 dB over MID (KS gtr+bass) -> -8.9 dB
constexpr float ARM_GAIN_DOWN  = 0.40f;   // (2026-10-17: was 0.38 before the interpolator fix, which lowered DOWN by -0.4 dB vs MID)
//    // octave-down makeup: unity-gain DOWN measured +8.1..+8.5 dB over MID -> -8.4 dB

// --- Guards -----------------------------------------------------------------
constexpr float ARM_SILENCE    = 1e-10f;  // |z|^2 below this -> band output 0, yprev reset (FastInvSqrt(0) guard)
constexpr float ARM_DENORM     = 1e-12f;  // |yprev| below this is flushed to 0

// --- Control smoothing (anti-zipper on the K1/K2/K3/K4/K5 targets) ----------
constexpr float ARM_CTRL_SMOOTH_MS = 10.f;  // ms; one-pole time constant at the 8 kHz rate

// --- Output -----------------------------------------------------------------
constexpr float ARM_OUT_GAIN   = 7.4f;    // (2026-10-17: was 8 before the interpolator fix, which raised MID by +0.7 dB)
//     // wet trim: K1 = 0 band sum (Re of analytic bands, band-limited) is ~0.125 x input RMS on KS plucks

// --- Sin table ----------------------------------------------------------------
constexpr int   ARM_SIN_SIZE   = 1024;    // entries per period (power of two; mask wrap)

// --- DIAG "note this setting" log (DIAG builds only; no effect otherwise) ---
constexpr float    ARM_DIAG_KNOB_EPS  = 0.02f;  // knob move (remapped 0..1) that counts as a change
constexpr uint32_t ARM_DIAG_SETTLE_MS = 600;    // ms without change before an `auto` line
constexpr uint32_t ARM_DIAG_BLINK_MS  = 150;    // ms LED1 goes off to confirm an FS1 tap

}  // namespace armitage_k
