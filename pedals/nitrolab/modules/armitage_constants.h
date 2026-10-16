#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

namespace armitage_k {

// --- Feedback amount (K1) -------------------------------------------------
constexpr float ARM_BETA_MAX   = 4.4f;    // rad; K1 full CW (squared taper), the Ensemble Oscillator's max
constexpr float ARM_BETA_CLAMP = 8.8f;    // rad; hard ceiling on any band's beta_k (2 x ARM_BETA_MAX)

// --- Fold-back (K4): beta_k = beta * (ARM_F_REF / f_played)^p, p = 1 - k4 --
constexpr float ARM_F_REF      = 200.f;   // Hz; played frequency where beta_k == beta (below gets MORE, clamped)

// --- High-band roll-off (K2): w_k = min(1, (f_k / ARM_TILT_REF)^-gamma) ----
constexpr float ARM_TILT_MAX   = 3.f;     // roll-off exponent gamma at K2 full CW (~ -18 dB/oct above the ref)
constexpr float ARM_TILT_REF   = 200.f;   // Hz; roll-off corner (on the band's input centre f_k), never boosts

// --- Feedback smoothing (K5): one-pole LP on yprev at 8 kHz ----------------
constexpr float ARM_SMOOTH_HZ_MAX = 4000.f;  // Hz; K5 CCW cutoff (= effectively none, raw chatter stays)
constexpr float ARM_SMOOTH_HZ_MIN = 100.f;   // Hz; K5 CW cutoff (very smooth), exponential in between

// --- Feedback signal (always raw since step 2) -----------------------------
constexpr float ARM_RAW_GAIN   = 100.f;   // yprev = out * this; loudest band ~0.0005 soft gtr -> 0.05 (sine), ~0.012 hard bass -> 1 (clamped, saw/noise)

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

// --- Control smoothing (anti-zipper on K1/K2/K3/K4/K5 targets) --------------
constexpr float ARM_CTRL_SMOOTH_MS = 10.f;  // ms; one-pole time constant at the 8 kHz rate

// --- Output -----------------------------------------------------------------
constexpr float ARM_OUT_GAIN   = 8.f;     // wet trim: K1 = 0 band sum (Re of analytic bands, band-limited) is ~0.125 x input RMS on KS plucks

// --- Sin table ----------------------------------------------------------------
constexpr int   ARM_SIN_SIZE   = 1024;    // entries per period (power of two; mask wrap)

}  // namespace armitage_k
