#pragma once
#include <cstdint>

// --- Oscillator (Tuning Page 1) ---
constexpr float OSC_K            = 0.480f;  // parabolic curve: 0=linear saw, 0.5+=very round
constexpr float OSC_DC_TRIM      = 0.000f;  // fine DC offset after shaping
constexpr float OSC_FOLD_AMT     = 0.150f;  // triangle-core fold (reserved)
constexpr float OSC_PEAK_GAIN    = 1.000f;  // pre-filter oscillator trim
constexpr float OSC_SAW_GAIN     = 1.500f;  // per-waveform level trim
constexpr float OSC_TRI_GAIN     = 1.400f;  // reference level
constexpr float OSC_SQR_GAIN     = 1.400f;

// --- Envelope follower (Tuning Page 2) --- (Stage 3)
constexpr float ENV_LP_CUTOFF_HZ = 50.0f;   // envelope LP cutoff (higher = faster tracking)
constexpr float ENV_PRE_GAIN     = 1.000f;   // input gain before rectifier
constexpr float ENV_ATTACK_BIAS  = 1.000f;   // filter asymmetry, attack
constexpr float ENV_RELEASE_BIAS = 1.000f;   // filter asymmetry, release


// --- Envelope modulation ---
constexpr float ENV_FILTER_MOD   = 0.500f;   // envelope → filter cutoff (subtle opening)
constexpr float ENV_FOLD_MOD     = 0.250f;   // envelope → wavefold amount (×5 internally)

// --- Preset system timing (from ux-demo.html) ---

// LED 1: Roman numeral preset blink
constexpr uint32_t LED_SHORT_ON_MS     = 150;   // I symbol on duration
constexpr uint32_t LED_LONG_ON_MS      = 950;   // V symbol on duration
constexpr uint32_t LED_ELEM_GAP_MS     = 200;   // gap between symbols
constexpr uint32_t LED_REPEAT_GAP_MS   = 700;   // gap before pattern repeats

// LED 2: dirty indicator
constexpr uint32_t LED_DIRTY_ON_MS     = 50;    // dirty flash on time
constexpr uint32_t LED_DIRTY_OFF_MS    = 50;    // dirty flash off time

// LED 2: save mode
constexpr uint32_t LED_SAVE_MODE_ON_MS  = 150;  // save mode blink on
constexpr uint32_t LED_SAVE_MODE_OFF_MS = 150;  // save mode blink off

// LED 2: save confirm burst
constexpr uint32_t LED_SAVE_CONFIRM_DUR_MS = 500; // total burst duration
constexpr uint32_t LED_SAVE_CONFIRM_ON_MS  = 75;  // burst on time
constexpr uint32_t LED_SAVE_CONFIRM_OFF_MS = 75;  // burst off time

// Bank-switch burst (both LEDs in sync, fast flicker).
// Total burst time is fixed; the Roman numeral count of the new bank
// determines how many flicker pulses fit inside that window. Bank 1 = one
// long pulse, bank 2 = two shorter pulses, bank 3 = three even shorter.
// I/V duration distinction is dropped — pulse count alone differentiates.
// Only valid for NUM_BANKS ≤ 3 (for 4/5/6 we'd need V back to keep them
// distinguishable from II/I/II).
constexpr uint32_t LED_BANK_FLICKER_MS = 20;    // deterministic on/off chunk inside each pulse
constexpr uint32_t LED_BANK_TOTAL_MS   = 1200;  // total burst duration (sum of pulses + gaps)
constexpr uint32_t LED_BANK_GAP_MS     = 150;   // fixed pause between pulses
constexpr uint32_t LED_BANK_HOLD_MS    = 400;   // trailing pause before LEDs return to normal

// Footswitch timing
constexpr uint32_t FS_LONG_PRESS_MS    = 700;   // long press threshold
constexpr uint32_t FS_BOOT_HOLD_MS     = 2000;  // both-FS hold → DFU bootloader

// Knob dirty detection
constexpr float KNOB_DIRTY_THRESHOLD   = 0.02f; // 2% travel to trigger dirty

// Pitch tracking
constexpr int TRACKING_WRAP_NOTE       = 9;     // octave-locked wrap point (9 = A)

// --- Stage / mix / ladder (Tuning Page 3) --- (Stage 2–3)
constexpr float OSC_GAIN         = 1.500f;   // final osc level into mix
constexpr float LADDER_DRIVE     = 1.800f;   // ladder input gain (higher = more tanh warmth)
constexpr float LADDER_CUTOFF_OFFSET = 0.000f; // tone knob trim
constexpr float DRY_TRIM         = 1.000f;   // dry path level trim

// --- Mode C — Schism ---
// SW1=UP drive — K4 bipolar around noon. NOON = clean.
//   CW  half: sine wavefolder (fold amount 0 → max).
//   CCW half: Chebyshev waveshaper (octave-up / metallic harmonic generator).
// Sine wavefolder compensation curve ear-tuned in C.2.
constexpr float SINEFOLD_DRIVE_MAX   = 35.0f;  // pre-sin drive at K4 full CW (1× at noon)
constexpr float SINEFOLD_COMP_AT_MAX = 0.55f;  // post-fold gain at K4 full CW (1.0 at noon)
// Chebyshev waveshaper (K4 full CCW). Drives the input toward [-1,1], then sums
// Chebyshev polynomials T2..T5. Each Tn maps a sinusoid to its n-th harmonic
// (T2 = 2nd / octave-up, T3 = 3rd, T4 = 4th, T5 = 5th), so the H* mix sets a
// metallic / ring-mod-ish spectrum. Value at x=0 subtracted out (T2/T4 carry
// constant terms) so silence stays silent.
constexpr float MODE_C_CHEBY_DRIVE_MAX = 30.0f; // pre-shaper drive at K4 full CCW (1× at noon)
constexpr float MODE_C_CHEBY_COMP      = 0.5f;  // post-shaper output gain
constexpr float MODE_C_CHEBY_H2        = 1.f;   // 2nd harmonic weight (octave up)
constexpr float MODE_C_CHEBY_H3        = 0.3f;  // 3rd harmonic weight
constexpr float MODE_C_CHEBY_H4        = 0.7f;  // 4th harmonic weight
constexpr float MODE_C_CHEBY_H5        = 0.2f;  // 5th harmonic weight
// Pre-shaper low-pass (2-pole) — feeds the octave generator a near-sine so T2
// produces a clean octave instead of intermod mush from the bass's own
// harmonics (the Octavia trick). Lower = cleaner/stronger octave but darker;
// raise toward 600+ to let more of the bass's brightness/metallic content in.
constexpr float MODE_C_CHEBY_LP_HZ     = 250.0f;

// Moog ladder (SW2=UP, K1 cutoff / K2 resonance / K3 env amount).
constexpr float MODE_C_LADDER_RES_MAX = 1.2f;  // pushed past ~1.0 self-osc threshold; in-loop tanh bounds it
constexpr float MODE_C_ENV_MOD_RANGE  = 120.0f; // depth on normalized env; lift = 1 + env_scaled * |k3| * range
constexpr float MODE_C_ENV_SCALE      = 10.0f; // passive-bass env normalization (matches Mode A's effective ×10)
constexpr float MODE_C_CUTOFF_MAX_HZ  = 10000.f; // top clamp for env-modulated cutoff (keeps Huovilainen stable)
constexpr float K3_DEADZONE           = 0.05f; // bipolar K3 noon ±deadzone → env amount = 0
// Audio-rate cutoff self-FM (SW2=UP Moog). K5 noon→CW fades in modulation of
// the ladder cutoff by the filter input signal: mod_cutoff *= 1 + depth·k5·in.
// The input's harmonics splatter sidebands around the resonant peak so it reads
// gritty/vocal instead of a sterile sine. Cutoff stays clamped to [MIN,MAX] Hz.
constexpr float MODE_C_MOOG_FM_DEPTH  = 3.0f;  // FM depth at K5 full CW (linear, ×filter input)

// Mode C filter drive (K5 — bipolar around noon, applies across all SW2 filter modes).
// CCW → attenuate, NOON → unity (1.0), CW → boost. Piecewise linear in dB-ish space.
constexpr float MODE_C_DRIVE_MIN      = 0.25f; // K5 full CCW → ~−12 dB attenuation
constexpr float MODE_C_DRIVE_MAX      = 8.0f;  // K5 full CW  → very hot pre-filter saturation

// Per-filter internal input pads — apply BEFORE the filter's own saturator.
// Moog and Grendel are spectrum-shaping filters with sweet spots at line-level
// (~0.2–0.3 amplitude). K5 noon (1.0) × pad puts them in their clean zone, while
// K5 CW still drives them hard. Phaser is a clean parallel-BPF structure
// without internal saturation; same pad keeps it in linear range at K5 noon.
constexpr float MODE_C_MOOG_INPUT_PAD    = 0.3f;
constexpr float MODE_C_GRENDEL_INPUT_PAD = 0.3f;
constexpr float MODE_C_PHASER_INPUT_PAD  = 0.3f;

// Per-filter post-filter makeup gains — apply AFTER the filter Process. A small
// lift only: K5 deliberately keeps its double duty (level + filter drive
// character — the "analog" feel), so we do NOT fully compensate the input pad
// (full unity at noon = 2.6). 1.4 nudges the floor up a touch without shifting
// the whole K5 loudness curve out from under its sweet spot. Ear-tune per filter.
constexpr float MODE_C_MOOG_MAKEUP    = 1.4f;
constexpr float MODE_C_GRENDEL_MAKEUP = 1.4f;
constexpr float MODE_C_PHASER_MAKEUP  = 1.4f;

// Amp-env VCA — final wet-path gate (same pattern as Mode A's drone gating).
// Multiplies wet by env_val × MODE_C_VCA_GAIN so the wet path is silent when the
// bass is silent (kills self-resonance ringing alone), and opens up as you play.
// Passive bass env peaks ~0.06–0.1, so gain 12 puts the gate "fully open"
// around typical playing dynamics with mild boost on hard plucks.
constexpr float MODE_C_VCA_GAIN = 12.0f;

// Mode C SW1=DOWN — pitch-tracked synth oscillator engine.
// K4 morphs through five zones (hypersaw → saw plateau → saw/sq crossfade →
// square plateau → square+PWM). VCA is the raw shared env follower, applied
// before the SW2 filter (Mode A style direct multiply).
// K4 layout: discrete noon split. CCW half = saw, CW half = rect.
//   K4 = 0.00   : max hypersaw (all voices at full detune)
//   K4 → 0.50   : hypersaw modulation fades out toward single saw
//   K4 = 0.50   : pure single saw at noon edge, pure single rect just past
//   K4 → 1.00   : PWM modulation fades in (depth first, then rate)
//   K4 = 1.00   : max PWM (sweet-spot depth, fastest rate)
// Within each half, side-voice gain / PWM depth ramps in fast (first
// _GAIN_FRAC / _DEPTH_FRAC of travel) so the modulated timbre is "fully on"
// early; the remaining travel only widens detune / speeds the LFO.
constexpr int   MODE_C_SYNTH_UNISON_VOICES    = 7;     // hypersaw voice count (1 center + 3 symmetric pairs)
constexpr float MODE_C_SYNTH_DETUNE_CENTS_MIN = 10.f;  // outer-voice detune just past plateau (already incoherent so RMS norm is accurate from the first sample)
constexpr float MODE_C_SYNTH_DETUNE_CENTS_MAX = 35.f;  // outer-voice detune at K4=0 — toned down at the extreme so the ensemble stays musical
// Hypersaw voice staging — three pairs fade in sequentially along CCW travel
// (single saw → dual detuned → 5-voice → full 7-voice ensemble), then the
// final tail past V7_END only widens detune. Values are positions along
// the normalized hypersaw axis t = (plateau_lo − k4) / plateau_lo, where
// t = 0 at the plateau edge and t = 1 at K4 = 0.
constexpr float MODE_C_SYNTH_HYPER_V3_END     = 0.30f; // innermost pair (v=2,4) fully in — center + 1 pair = "dual detuned"
constexpr float MODE_C_SYNTH_HYPER_V5_END     = 0.60f; // middle pair (v=1,5) fully in — 5-voice
constexpr float MODE_C_SYNTH_HYPER_V7_END     = 0.85f; // outermost pair (v=0,6) fully in — full 7-voice ensemble; remaining travel widens detune

// Per-voice detune-spread coefficients, applied as
//   voice_freq = f0 * 2^(spread * detune_cents / 1200)
// Non-uniform JP-8000-style ratios (0.234 / 0.500 / 1.000): with linear
// spreads (1/3, 2/3, 1) all pair-to-pair beat frequencies coincided,
// summing into one audible intermodulation tone. Breaking the multiplicative
// relationships between pairs scatters those beats and restores the dense,
// uncorrelated ensemble wash.
constexpr float MODE_C_SYNTH_VOICE_SPREAD[MODE_C_SYNTH_UNISON_VOICES] = {
    -1.000f, -0.500f, -0.234f, 0.000f, 0.234f, 0.500f, 1.000f,
};
constexpr float MODE_C_SYNTH_SAW_PLATEAU      = 0.04f; // single-saw sweet-spot plateau width just below noon (K4 ∈ [0.46, 0.50] holds pure saw)
constexpr float MODE_C_SYNTH_PWM_LFO_HZ_MIN   = 0.2f;  // PWM rate just past noon (very slow start)
constexpr float MODE_C_SYNTH_PWM_LFO_HZ_MAX   = 2.f;   // PWM rate at K4=1
constexpr float MODE_C_SYNTH_PWM_DEPTH_MAX    = 0.40f; // max ± duty deviation — duty reaches 0.10/0.90 at triangle peaks (fundamental ≈ 31% of max). 0.5 = full silent-at-peak gating
constexpr float MODE_C_SYNTH_PWM_DEPTH_FRAC   = 0.4f;  // fraction of rect half over which depth reaches max; remaining CW travel only speeds the LFO up
constexpr float MODE_C_SYNTH_VCA_GAIN         = 12.f;  // passive-bass env normalization, matches MODE_C_VCA_GAIN convention

// Filter-env smoother — Mode C only. Shape switches with K3 direction:
//
// K3 CW  → peak follower: instant attack, one-pole release (RELEASE_MS).
//          Snappy filter opening on transients, decaying tail.
// K3 CCW → slow-rise env: one-pole attack (ATTACK_MS), instant snap-back when
//          env drops. Gradual filter opening (swell), clean return to K1.
constexpr float MODE_C_FILTER_ENV_ATTACK_MS  = 600.0f;  // CCW slow-rise time constant (ladder)
constexpr float MODE_C_FILTER_ENV_RELEASE_MS = 150.0f;  // CW release time constant (ladder)

// Grendel env smoother — same shape pattern as the ladder (asymmetric per
// K3 sign), at symmetric 400 ms values so CW release and CCW attack feel
// identical, only the direction of the vowel offset differs.
//   CW  → peak follower: instant attack, slow release (uses RELEASE_MS)
//   CCW → slow-rise: slow attack (uses ATTACK_MS), instant snap-back
constexpr float MODE_C_GRENDEL_ENV_ATTACK_MS  = 400.0f;  // always slow rise (both K3 directions)

// Direct env→offset gain. At hard pluck (env≈0.1) and K3 max, offset = ±2.0
// → vowel_path reaches just past the natural table edge; lighter plucks stay
// inside or partway. Tuned against passive-bass env range.
constexpr float MODE_C_GRENDEL_TARGET_GAIN = 10.0f;

// Env modulation depth on K2 size_scale. Coupled to K3 (same sign convention
// as vowel_path): CCW K3 → size nudges up on attack (mouth tightens), CW K3
// → size nudges down (mouth opens). At full K3 and hard pluck (env≈0.1),
// size_scale is multiplied by 1 ± 0.2 → ±20% swing. Pre-scaled to compensate
// for passive-bass env range (~0.02–0.1), same convention as TARGET_GAIN.
constexpr float MODE_C_GRENDEL_SIZE_MOD_AMT = 2.0f;

// Mode C ladder K1 cutoff range — extended below Mode A's MapCutoff (80 Hz)
// so K1 fully CCW can really shut. Top matches Mode A and the env-mod clamp.
constexpr float MODE_C_CUTOFF_MIN_HZ = 30.0f;
constexpr float MODE_C_CUTOFF_K1_MAX_HZ = 8000.0f;

// Post-filter linear gain, applied before the limiter. Slight lift so the
// limiter has something to grab on quieter notes without driving the VCA
// stage. Keep close to unity to leave headroom for resonant peaks.
constexpr float MODE_C_POST_FILTER_GAIN = 1.3f;

// SW1=MID drive — K4 bipolar around noon. NOON = clean.
//   CW  half: bit-flipper (XOR bit position, gated).
//   CCW half: tanh overdrive (Mode B feedback-drive character; drive 1 → max).
//
// Bit-flipper (CW). Deterministic XOR of a chosen Q15 bit on every sample —
// same mechanism as Mode B SW1 MIDDLE CCW, without the random event timing.
// K4 noon→CW sweeps the XOR bit position from 0 (LSB, inaudible) to MAX_BIT.
// Bit 15 is the sign bit → flips polarity, output snaps to a near-full-scale
// square (loudest, least dynamic). Gate keys the wet/dry off the envelope so
// silent input stays silent (passive bass env ≈0.02–0.1).
constexpr int   MODE_C_BITCRUSH_MAX_BIT      = 15;      // K4 full CW → flip bit 15 (sign bit, full-scale square)
constexpr float MODE_C_BITCRUSH_ENV_GATE     = 0.01f;   // raw env_val gate threshold
constexpr int   MODE_C_BITCRUSH_RAMP_SAMPLES = 48;      // 1 ms click-free gate edge
// Per-bit loudness comp table (index = flipped bit, 0..15). The flipper picks a
// discrete bit, so the wet level jumps in discrete steps (and bit 15, the sign-
// flip square, is an outlier no smooth curve fits) — a table lets each step be
// leveled independently by ear. The wet (flipped) output is multiplied by
// TABLE[bit]. Low bits ≈ clean (1.0); higher bits get pulled down. Tune each
// entry against the dry reference until the K4 sweep holds an even loudness.
constexpr float MODE_C_BITCRUSH_COMP_TABLE[16] = {
    1.00f, 1.10f, 1.20f, 1.30f, 1.40f, 1.50f, 1.40f, 1.30f,  // bits 0–7  (inaudible → faint)
    1.20f, 1.10f, 1.00f, 0.80f, 0.70f, 0.60f, 0.50f, 0.45f,  // bits 8–15 (audible → sign-flip square)
};

// Digital wraparound (CCW half — PARKED, tanh OD is active in the slot).
// Overdriven signal wraps modulo [-1,1] like an overflowing DAC instead of
// clamping — each rail crossing jumps to the opposite rail. Harsh buzzy
// "broken digital" character. Kept so the CCW branch can switch back to it.
constexpr float MODE_C_WRAP_DRIVE_MAX = 8.0f;  // pre-wrap drive at K4 full CCW (1× at noon)
constexpr float MODE_C_WRAP_COMP      = 0.50f; // post-wrap gain (output is full-scale sawtooth)

// Tanh overdrive (CCW half — ACTIVE). K4 full CCW. Gain-compensated soft-clip
// (Mode B feedback-saturator character); COMP keeps wet loudness roughly flat.
constexpr float MODE_C_OD_DRIVE_MAX   = 80.0f; // pre-tanh drive at K4 full CCW (1× at noon)
constexpr float MODE_C_OD_COMP_AT_MAX = 0.60f; // post-tanh gain at K4 full CCW (1.0 at noon)
// Asymmetric bias as a fixed offset in the tanh-input domain: tanh(dry·drive +
// bias) with tanh(bias) subtracted back out (no DC leak). Constant, mild
// asymmetry that colors the crossover/low-level region → even harmonics,
// "tube"/asymmetric-amp flavor. Independent of drive, so it never collapses the
// small-signal response (a signal-domain bias × high drive half-wave-mutes).
// 0 = symmetric. Higher = more lopsided; back off if one polarity chokes.
constexpr float MODE_C_OD_BIAS        = 1.2f;

// Post-filter peak limiter (Mode C only, all SW2 modes).
// 2-band split: LF (≤ SPLIT_HZ) passes through untouched so bass fundamentals
// don't duck when resonance peaks fire the limiter. HF is soft-knee limited.
// "Warmth-when-working" — gain reduction modulates a touch of tanh saturation,
// so peaks gain mild character without harmonics on quiet/clean signals.
constexpr float MODE_C_LIMIT_SPLIT_HZ  = 160.f; // 2-band crossover (one-pole)
constexpr float MODE_C_LIMIT_THR       = 0.6f;  // amplitude threshold (linear)
constexpr float MODE_C_LIMIT_RATIO_INV = 0.5f;  // 1/ratio — 0.5 ≈ 2:1 soft slope
constexpr float MODE_C_LIMIT_ATK_MS    = 2.0f;  // catches resonance transients
constexpr float MODE_C_LIMIT_REL_MS    = 60.0f; // slow enough to avoid sideband mod
constexpr float MODE_C_LIMIT_WARM_DRV  = 1.5f;  // tanh drive at full GR
constexpr float MODE_C_LIMIT_WARM_MIX  = 0.35f; // tanh blend at full GR

// Grendel formant filter (SW2=MID). K1 = vowel path, K2 = size, K3 = env on path.
constexpr int   GRENDEL_NUM_FORMANTS = 4;
constexpr int   GRENDEL_NUM_VOWELS   = 5;
constexpr float GRENDEL_FORMANT_Q    = 12.0f;  // mid-high Q for vowel-like ringing
constexpr float GRENDEL_OUT_GAIN     = 4.0f;   // BPFs attenuate heavily; ear-tune in C.5

// Vowel path: oo → oh → ah → eh → ee, K1 CCW (=oo, dark/closed) → CW
// (=ee, bright/open). K3 CCW → env pushes path toward ee (low→hi, auto-wah
// opens brighter on attack). K3 CW → env pushes path toward oo (hi→low,
// "anti-wah" closes darker on attack). (F1..F4 in Hz, adult-male typicals.)
constexpr float GRENDEL_VOWELS[GRENDEL_NUM_VOWELS][GRENDEL_NUM_FORMANTS] = {
  {300.f,  870.f, 2240.f, 3200.f},  // 0: oo /u/
  {570.f,  840.f, 2410.f, 3300.f},  // 1: oh /o/
  {730.f, 1090.f, 2440.f, 3400.f},  // 2: ah /ɑ/
  {530.f, 1840.f, 2480.f, 3500.f},  // 3: eh /ɛ/
  {270.f, 2290.f, 3010.f, 3500.f},  // 4: ee /i/
};
constexpr float GRENDEL_FORMANT_GAIN[GRENDEL_NUM_FORMANTS] = {1.0f, 0.85f, 0.6f, 0.35f};

constexpr float GRENDEL_SIZE_MIN       = 0.5f;  // K2=0 → centers × 0.5 (large mouth)
constexpr float GRENDEL_SIZE_MAX       = 1.6f;  // K2=1 → centers × 1.6 (small mouth)
constexpr float GRENDEL_ENV_PATH_RANGE = 1.2f;  // overshoot factor — env can push 20% past the available-travel boundary (clamp absorbs)

// Phaser (SW2=DOWN). 4-stage allpass chain modeled on EHX Small Stone.
// All stages share a modulated allpass corner ω; output = 0.5·(dry + wet).
// Two notches sweep in tandem at ω · 0.414 and ω · 2.414 (ratio ≈ 5.83,
// matches Small Stone's measured ~5.5). K1 → ω center (exp); K2 →
// feedback (Color analog); K3 bipolar → LFO rate (mag, exp) + shape
// (sign: CCW triangle, CW sample-and-hold). K3 = 0 → LFO off, static
// notches at K1.
constexpr float PHASER_F1_HZ_MIN       = 100.f;   // ω fully CCW → notches at ~41 / 241 Hz
constexpr float PHASER_F1_HZ_MAX       = 4000.f;  // ω fully CW  → notches at ~1660 / 9660 Hz
constexpr float PHASER_SWEEP_OCT       = 1.5f;    // LFO depth: ±1.5 octaves (3-octave total sweep, matches Small Stone)
// Triangle LFO range: ambient drift → near sub-audio (sideband-generating).
constexpr float PHASER_LFO_TRI_HZ_MIN  = 0.02f;   // 50-second cycle
constexpr float PHASER_LFO_TRI_HZ_MAX  = 80.f;    // near sub-audio
// S&H rate range: one event per 2 s → 40 events/sec. No audio-rate;
// S&H character lives well below the triangle's top end.
constexpr float PHASER_LFO_SH_HZ_MIN   = 0.5f;
constexpr float PHASER_LFO_SH_HZ_MAX   = 40.f;
constexpr float PHASER_FB_MAX          = 0.95f;   // feedback ceiling — close to but below 4-stage self-oscillation

// --- Mode B SW1 MIDDLE — Event-Driven Digital Glitch ---
// Bipolar K4: noon = clean. CCW = bit-flip events, CW = timing events
// (freeze / stutter / reverse). Buchla SoU style: stochastic trigger
// timing, randomised per-event parameters, env-gated mix (silence-in →
// silence-out). See docs/MODE_B_TEXTURE_IDEAS.md.
constexpr float GLITCH_DEADZONE             = 0.05f; // ±5% around noon → clean
constexpr int   GLITCH_XOR_MAX_BIT          = 13;    // highest bit flipped at full CCW (±0.25 of full scale)
constexpr float GLITCH_EVENT_RATE_HZ_MAX    = 25.0f; // CCW: events/sec at full effect_pos × full env
constexpr float GLITCH_EVENT_RATE_HZ_MAX_CW = 50.0f; // CW: 2× CCW rate at full deflection — timing events feel faster
constexpr int   GLITCH_EVENT_DUR_MIN_SAMPLES = 240;  // 5 ms at 48 kHz
constexpr int   GLITCH_EVENT_DUR_MAX_SAMPLES = 2400; // 50 ms at 48 kHz
constexpr int   GLITCH_BUFFER_SAMPLES       = 2400;  // 50 ms ring buffer for CW timing payload
constexpr int   GLITCH_RAMP_SAMPLES         = 48;    // 1 ms click-free wet/dry ramp
constexpr float GLITCH_ENV_GATE             = 0.01f; // raw env_val noise gate — below this, no new events arm (passive bass ≈0.02–0.1 while played)

// --- Mode B SW2 DOWN — Bode frequency shifter (K1 bipolar, unison at noon) ---
// K1 ranges ±1 kHz with an exponential taper around unison: fine sub-Hz
// resolution near center, full kHz at the extremes. Center deadzone holds
// pure unison. Shifter sits inside the feedback loop (post-grain, post-SW3
// FX, pre-wet-HPF) so each loop pass cascades the shift — pile-up by design.
// Grain buffer-read pitch is forced to 1.0 in this sub-mode.
constexpr float FREQ_SHIFT_MAX_HZ    = 1000.f;
constexpr float FREQ_SHIFT_DEADZONE  = 0.02f;  // |k1_norm| < this → 0 Hz
constexpr float FREQ_SHIFT_CURVE     = 6.0f;   // taper exponent (higher = more weighted near unison)

// --- Mode B reverb + bipolar K5 ---
constexpr float K5_CENTER_DEADZONE = 0.05f;  // ±5% deadzone around center
constexpr float REVERB_INPUT_GAIN  = 0.40f;  // gain into the Clouds reverb
constexpr float REVERB_TIME        = 0.70f;  // reverb decay (krt in Clouds)
constexpr float REVERB_MAX_FEEDBACK = 0.95f; // CW side: existing ring-buffer feedback ceiling
constexpr float REVERB_AMT_SMOOTH_COEF = 0.002f; // one-pole on K5 reverb amount, ~10 ms tc
