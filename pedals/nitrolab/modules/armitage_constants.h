#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

namespace armitage_k {

// --- Feedback amount (K1) -------------------------------------------------
constexpr float ARM_BETA_MAX   = 4.4f;    // rad; K1 full CW (squared taper), the Ensemble Oscillator's max
constexpr float ARM_BETA_CLAMP = 8.8f;    // rad; safety ceiling on any band's beta_k (K4 law gives at most beta, so never reached)

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

// --- Feedback signal blend (SW1): yprev = out * ((1 - a) * 1/|z| + a * ARM_RAW_GAIN)
constexpr float ARM_RAW_GAIN   = 100.f;   // raw path gain: loudest band ~0.0005 soft gtr -> 0.05 (sine), ~0.012 hard bass -> 1 (clamped, saw/noise)
constexpr float ARM_BLEND_UP   = 0.f;     // a at SW1 UP   (normalised: out / |z|, level-independent)
constexpr float ARM_BLEND_MID  = 0.5f;    // a at SW1 MID  (half normalised, half raw)
constexpr float ARM_BLEND_DOWN = 1.f;     // a at SW1 DOWN (raw, level-dependent)
constexpr float ARM_BLEND_MS   = 10.f;    // ms; linear ramp of a on an SW1 change (full 0 -> 1 in this time)

// --- Cross-modulation (K3): theta_k += beta_x * m --------------------------
constexpr float ARM_ROOT_HZ    = 150.f;   // Hz; bands with input centre f_k below this form the root modulator m (bands 0..9)
constexpr float ARM_XMOD_MAX   = 6.f;     // rad; beta_x at K3 full CW (squared taper), Ensemble "Up"-mode depth

// --- Octave (SW2) ------------------------------------------------------------
constexpr float ARM_XFADE_MS   = 10.f;    // ms; linear crossfade old -> new octave path on an SW2 change
constexpr float ARM_GAIN_UP    = 0.36f;   // octave-up makeup: unity-gain UP measured +8.3..+9.2 dB over MID (KS gtr+bass) -> -8.9 dB
constexpr float ARM_GAIN_DOWN  = 0.38f;   // octave-down makeup: unity-gain DOWN measured +8.1..+8.5 dB over MID -> -8.4 dB

// --- Guards -----------------------------------------------------------------
constexpr float ARM_SILENCE    = 1e-10f;  // |z|^2 below this -> band output 0, yprev reset (FastInvSqrt(0) guard)
constexpr float ARM_DENORM     = 1e-12f;  // |yprev| below this is flushed to 0

// --- Control smoothing (anti-zipper on the K1/K2/K3/K4/K5 targets) ----------
constexpr float ARM_CTRL_SMOOTH_MS = 10.f;  // ms; one-pole time constant at the 8 kHz rate

// --- Output -----------------------------------------------------------------
constexpr float ARM_OUT_GAIN   = 8.f;     // wet trim: K1 = 0 band sum (Re of analytic bands, band-limited) is ~0.125 x input RMS on KS plucks

// --- Sin table ----------------------------------------------------------------
constexpr int   ARM_SIN_SIZE   = 1024;    // entries per period (power of two; mask wrap)

}  // namespace armitage_k
