#pragma once
//
// armitage — tuning constants (source of truth). See armitage.h for the
// per-band formula and the control map.

#include <cstdint>

namespace armitage_k {

// --- Mode selector (SW1 = mode pair, sign of K1 = model; 2026-10-22; K5 = K4 post-filter resonance) ---
// SW1 UP = FEEDBACK (K1 CCW half) / ADD (K1 CW half); MID = SAW (CCW) / PULSE (CW);
// DOWN = unshaped band sum on both halves (placeholder). t1 = 2 k1 - 1: t1 < 0 -> the CCW
// model, t1 >= 0 -> the CW model. Any model change (SW1 or the sign of K1) crossfades the
// band outputs of the two models linearly over ARM_MODEL_XFADE_MS.
//
// --- K1 bipolar: h = |t1| in [0, 1] (noon = unshaped), remapped to each model's old K1 law ---
//   K1old(h, split) = split * h / ARM_K1_FADE_FRAC                                    (h <= fade frac)
//                   = split + (1 - split) (h - ARM_K1_FADE_FRAC) / (1 - ARM_K1_FADE_FRAC)  (above)
//   split = ARM_K1_SPLIT_PD (SAW / PULSE: knee fade-in, then push), ARM_K1_SPLIT (ADD),
//           ARM_K1_FB_SPLIT (FB, beta = ARM_BETA_MAX K1old^2). No dead zone at noon.
constexpr float ARM_K1_FADE_FRAC = 0.2f;   // share of each K1 half that carries the fade-in (lower part of the old law)
constexpr float ARM_K1_FB_SPLIT  = 0.5f;   // FB: old K1 reached at the end of the fade-in part

// --- K1 (timbre, per mode; values = the old unipolar K1, see the remap above) ---
// FB: beta = ARM_BETA_MAX * k1^2. ADD: K1 split at
// ARM_K1_SPLIT into u / v (below, unchanged). SAW: resonance (R, morph). PULSE: width.
constexpr float ARM_BETA_MAX   = 4.4f;    // rad; K1 full CW (squared taper), the Ensemble Oscillator's max
constexpr float ARM_BETA_CLAMP = 8.8f;    // rad; safety ceiling on any band's beta_k (beta_k = beta <= ARM_BETA_MAX, so never reached)
constexpr float ARM_K1_SPLIT   = 0.6f;    // ADD K1 split point: below = today's ADD law (scaled), above = extended range
// SAW / PULSE K1 split: x = min(K1 / split, 1) is the knee depth, shape only at every K1:
// d(x) = 0.5 (ARM_PD_KNEE / 0.5)^x, exponential in K1 (0.5 = the sine at K1 = 0 -> 0.19 -> 0.07 ->
// 0.027 -> ARM_PD_KNEE, full saw / square w = 0.5, at the split; x = 1 sets d = ARM_PD_KNEE exactly);
// SAW: d_k = max(d(x), dfloor_k) (K2 CCW: formant (R) modulation, see below);
// PULSE: D = d(x) on both edges, amount 1 - x (1 - cap_k) (linear x, unchanged). p = max((K1 - split) / (1 - split), 0) pushes it (SAW resonance R 1 -> 4, mix 0 -> 0.6; PULSE w 50 -> 0 %).
constexpr float ARM_K1_SPLIT_PD = 0.5f;

// --- FB anti-fold limit (always on): beta_k <= max(0, ARM_FOLD_LIMIT_HZ / f'_k - 1) ---
// Carson's rule: a feedback-FM band at f'_k with index beta_k spreads to about
// (beta_k + 1) f'_k; capping beta_k keeps the generated overtones below the
// 8 kHz engine's Nyquist instead of folding back. Applied to the final per-band
// beta_k. The K3 cross-mod term is NOT capped.
constexpr bool  ARM_ANTIFOLD      = true;     // false = no cap (for A/B comparison)
constexpr float ARM_FOLD_LIMIT_HZ = 4000.f;   // Hz; Nyquist of the 8 kHz band engine

// --- K4 = post filter on the final 48 kHz wet (after the DC blocker, before the soft clipper) ---
// Copied law from mnemonic's tone filter (MNEM_FILT_*): two cascaded TPT SVF stages (24 dB/oct),
// t = 2 k4 - 1, tmag = |t|^ARM_FILT_CURVE.
//   t = 0 (noon): true bypass (filter not run, wet bit-identical).
//   t < 0: low-pass,  fc = ARM_FILT_FMAX (ARM_FILT_LP_MIN / ARM_FILT_FMAX)^tmag   (4 kHz -> 180 Hz)
//   t > 0: high-pass, fc = ARM_FILT_FMIN (ARM_FILT_HP_MAX / ARM_FILT_FMIN)^tmag   (40 Hz -> 4 kHz)
//   Q (both stages) = ARM_FILT_Q_MIN (ARM_FILT_Q_MAX / ARM_FILT_Q_MIN)^k5   (K5 = resonance, exponential;
//   k5 smoothed like t; K4 noon = bypass: K5 does nothing there)
// No makeup gain. t and k5 are smoothed (one-pole, ARM_FILT_SMOOTH_MS, per audio block); cutoff
// follows the smoothed t, Q the smoothed k5. Bypass <-> filter: wet = dry + m (filtered - dry), m = min(|t| / ARM_FILT_FADE, 1)
// (linear crossfade over the first ARM_FILT_FADE of travel each side of noon; there the filter
// sits at its range end: LP ~20 kHz / HP ~20 Hz). |t| snaps to 0 below ARM_FILT_SNAP when the
// target is noon (filter off again, no CPU).
constexpr float ARM_FILT_FMIN      = 40.f;     // Hz; HP cutoff just right of noon
constexpr float ARM_FILT_FMAX      = 4000.f;   // Hz; LP cutoff just left of noon (engine ceiling, ARM_FOLD_LIMIT_HZ)
constexpr float ARM_FILT_HP_MAX    = 4000.f;   // Hz; HP cutoff at K4 full CW (thin)
constexpr float ARM_FILT_LP_MIN    = 180.f;    // Hz; LP cutoff at K4 full CCW (dark)
constexpr float ARM_FILT_CURVE     = 1.0f;     // |t| exponent: 1 = cutoff moves linearly in octaves with the knob
constexpr float ARM_FILT_Q_MIN     = 0.707f;   // per-stage Q at K5 full CCW (flat, the start value)
constexpr float ARM_FILT_Q_MAX     = 6.0f;     // per-stage Q at K5 full CW (resonant peak; replaces the old K4-driven ARM_FILT_Q_PEAK ramp)
constexpr float ARM_FILT_SMOOTH_MS = 5.f;      // ms; one-pole on t
constexpr float ARM_FILT_FADE      = 0.03f;    // |t| over which bypass crossfades into the filter
constexpr float ARM_FILT_SNAP      = 1e-4f;    // |t_smoothed| below this with target 0: bypass

// --- K2 = PWM (SAW / PULSE only; ADD / FB: K2 unused), bipolar t2 = 2 k2 - 1 ---------------
// t2 >= 0 (noon -> CW): nothing (reserved). t2 < 0: u = -t2 in (0, 1], one triangle LFO in [-1, 1]
// shared by all bands (phase at 8 kHz). Travel = mnemonic's K3 pre-effect law (MNEM_PRE_*):
//   depth = min(u / ARM_PWM_DEPTH_FRAC, 1)                          (fade-in over the first 10 %)
//   rate  = ARM_PWM_HZ_MIN (ARM_PWM_HZ_MAX / ARM_PWM_HZ_MIN)^ub, ub = min(u, ARM_PWM_TOP_KNEE)
//           (exponential from u = 0, 0.1 -> ~1.8 Hz at the knee), then for u > ARM_PWM_TOP_KNEE:
//           rate *= (ARM_PWM_TOP_HZ / rate)^((u - knee) / (1 - knee))  (exponential leg to ARM_PWM_TOP_HZ)
//   u:    0     .2     .4     .6     .85    .9     1
//   Hz:   0.10  0.20   0.39   0.77   1.80   4.70   32
//   PULSE: w = w_K1 + s lfo, s = depth min(ARM_PWM_W_SWING, w_K1 + x),
//          x = ARM_PWM_SIL_X clamp((ARM_PWM_SIL_W - w_K1) / ARM_PWM_SIL_W, 0, 1)
//          (symmetric around K1's width, the centre never moves; for w_K1 >= ARM_PWM_SIL_W the sweep
//          only touches 0, never silent; below it the undershoot grows linearly: w_K1 0.05 -> w
//          -0.05..0.15, w_K1 0 -> -0.10..0.10). NO clamp, no floor: w <= 0 = the shape is silent
//          (the clean mix-back stays), continuous at 0; DC term 1 - 2w follows w per sample.
//   SAW:   formant (resonance R) modulation around K1's setting (one saw, no copy):
//          R_mod = max(R_K1 2^(ARM_RMOD_OCT depth lfo), 1)     (+-1 octave at full depth)
//          R_k   = min(R_mod, ARM_RES_FMAX / f'_k)             (the per-band anti-fold cap)
//          Below R = 1 there is no formant (it would sit under the fundamental): R = 1 is the floor,
//          so at p = 0 (R_K1 = 1) only the rising half of the LFO moves R (1 -> 2). The morph mix
//          stays K1's (not modulated): at p = 0 the mix is 0, the sweep is not heard there.
//          dc(R) follows R_mod per 8 kHz sample (one shared value for all uncapped bands; capped
//          bands use their fixed dc(R cap)) -> exact per band per sample.
constexpr float ARM_PWM_DEPTH_FRAC = 0.10f;   // share of the CCW half over which the depth fades in
constexpr float ARM_PWM_HZ_MIN     = 0.1f;    // Hz; LFO rate at u = 0 (10 s cycle)
constexpr float ARM_PWM_HZ_MAX     = 3.0f;    // Hz; the exponential law's end point (u = 1, reached only below the knee)
constexpr float ARM_PWM_TOP_KNEE   = 0.85f;   // u where the top leg starts (~1.8 Hz)
constexpr float ARM_PWM_TOP_HZ     = 32.f;    // Hz; LFO rate at K2 full CCW (builder-tuned on the host sim 2026-10-22, was 200)
constexpr float ARM_PWM_W_SWING    = 0.40f;   // PULSE: max duty swing at full depth (+-, Schism MODE_C_SYNTH_PWM depth max); else limited to w_K1 + x
constexpr float ARM_PWM_SIL_W      = 0.10f;   // PULSE: K1 base width below which the sweep crosses into silence
constexpr float ARM_PWM_SIL_X      = 0.10f;   // PULSE: undershoot below 0 at base width 0 (linear from 0 at ARM_PWM_SIL_W)
constexpr float ARM_RMOD_OCT       = 1.0f;    // SAW: formant (R) swing at full depth, octaves (+-) around K1's R

// --- Wet DC blocker (final 48 kHz wet, before the K4 post filter) ----------
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

// --- Models (SW1 pair + sign of K1): SAW / PULSE (PD base), ADD, FB, unshaped ---
constexpr float ARM_MODEL_XFADE_MS = 1.f;   // ms; linear crossfade old -> new mode output on a model change (SW1 or the sign of K1; was a K5 zone change)
// FB "replace" gain on the shaping delta (2026-10-20, as the PD base): out_k = y0_k + ARM_GAIN_FB share_k (y_k - y0_k)
// in every octave (the *_DELTA octave gains do not apply), feedback signal at full amount
// (yprev = y_k / |z'_k|, every band above ARM_SILENCE). Set by structure, not by ear (unheard).
constexpr float ARM_GAIN_FB    = 2.f;     // was 1.0 (with yprev = y_k / ARM_REF_LEVEL)
constexpr float ARM_GAIN_ADD   = 0.54f;   // ADD makeup on the shaping delta (out_k = y0_k + g_model share_k (shaped_k - y0_k); y0 unscaled, K1 = 0 = unity in every model): matched to FB wet RMS at K1 = 1 on the DI bass excerpts at passive level (peak 0.1), SW2 MID, K2 K3 0, K4 K5 noon (2026-10-20; was 1.02 on the whole output)
// SAW / PULSE gain G on the shape, every octave (the *_DELTA octave gains do not apply), shape only
// (no clean term): out_k = G share_k a_k shaped_k + c y0_k. Approved on the host at 2.0048 (DI 2, var8), rounded.
constexpr float ARM_GAIN_PD    = 2.f;     // was 0.88 (HEAD PD makeup, K1 knee law)
// SAW / PULSE clean fundamental: fixed mix-back c y0_k of the clean band (no knob; K4 no longer
// controls it). No level compensation.
constexpr float ARM_PD_CLEAN_MIX = 0.5f;   // was ARM_PD_CLEAN_MIX_MAX (1 - k4) (0.5 = the old K4 CCW amount)

// ADD (SW1 UP, K1 CW half): additive saw per band, harmonics m = 2..ARM_ADD_MMAX of u = z'/|z'|.
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
//   cap_k = (0.5 - dfloor_k) / (0.5 - ARM_PD_KNEE)   (1 up to 500 Hz, 0 at 3.8 kHz)
// SAW knee: d_k = max(d(x), dfloor_k), d(x) = 0.5 (ARM_PD_KNEE / 0.5)^x  (x = K1 knee depth; cap inside the knee, a_k = 1)
// PULSE: knee D = d(x), amount a_k = 1 - x (1 - cap_k); resonance: a_k = cap_k
constexpr float ARM_PD_KNEE       = 0.01f;   // cycles; knee time of the saw / pulse jumps
constexpr float ARM_PD_FOLD_HZ    = 500.f;   // Hz; full-depth anti-fold corner; bands above it get the softer knee floor (2026-10-22: 1000 -> 500, less fizz from the upper partials' saws)
constexpr float ARM_PD_FOLD_TOP   = 3800.f;  // Hz; f'_k at/above this: knee floor 0.5 = sine / no pulse (linear in between)

// SAW, K1 = resonance (CZ "reso" on the linear phase, DC removed):
//   R = ARM_RES_R_MIN + (ARM_RES_R_MAX - ARM_RES_R_MIN) p      (p = K1 push above ARM_K1_SPLIT_PD, linear, 1 -> 4)
//   (K2 CCW: R -> R_mod, see K2 above), R_k = min(R, ARM_RES_FMAX / f'_k)
//   res_k = |z'_k| (sin(2 pi R_k phi) (1 - phi) - dc(R_k)),  dc(R) = 1/(2 pi R) - sin(2 pi R)/(2 pi R)^2 (exact mean)
//   mix = ARM_RES_MIX_MAX p   (linear; K1 full: 60 % res / 40 % saw)
//   shape_k = G share_k [ (1 - mix) saw_k + mix cap_k res_k ]   (morph on the shape only)
constexpr float ARM_RES_R_MIN   = 1.f;
constexpr float ARM_RES_R_MAX   = 4.f;     // R at K1 full (was 16; 2026-10-22 subtler push)
constexpr float ARM_RES_MIX_MAX = 0.6f;    // saw -> res morph at K1 full (was 1.0 = all res)
constexpr float ARM_RES_FMAX    = 3800.f;  // Hz; R_k f'_k capped here (anti-fold: the formant stays below the 4 kHz Nyquist of the 8 kHz engine)

// PULSE, K1 push = width: w = ARM_PUL_W_MAX - (ARM_PUL_W_MAX - ARM_PUL_W_MIN) p (50 % at K1 split -> 0 % CW),
// two independent knee steps (down at phi = 0, up at phi = w, ARM_PD_KNEE each) that may overlap;
// mean 1 - 2w removed (exact for any w in [0, 1); an up-step not finished by phi = 1 wraps into
// the next cycle). w = 0: the steps cancel, S = +1, silence.
constexpr float ARM_PUL_W_MAX = 0.5f;
constexpr float ARM_PUL_W_MIN = 0.f;

// --- K3 = 2-op FM per band, ADD and FEEDBACK only (SAW / PULSE: K3 does nothing) ---
// phi_mod = phi_k + I sin(phi_down_k), I = ARM_FM_INDEX_MAX * k3^2 (rad, squared taper), applied to the
// carrier phase before the shape. Modulator = the carrier's own octave-down from the band:
// SW2 MID: Down1(z_k) (half phase); UP: z_k (the played band); DOWN: Down1(z_k) = the carrier (1:1).
constexpr float ARM_FM_INDEX_MAX = 12.f;  // rad at K3 full CW (was 3, linear; old max now at K3 = 0.5)

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

// --- Wet soft clipper (final 48 kHz wet, after the K4 post filter, before the FS1 mute) ---
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
