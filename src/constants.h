#pragma once
#include <cstdint>
#include <cstddef>  // size_t

// --- Instrument profile (bass / guitar) ---
// The pedal is voiced for BASS by default. `make INSTRUMENT=guitar` defines
// NT3_INSTRUMENT_GUITAR, which retunes the pitch tracker (TRACK_* block below)
// and the frequency constants marked with a `NT3_GUITAR ? guitar : bass`
// ternary. The default (bass) build keeps today's ear-validated values
// exactly. Switching profiles rebuilds automatically (instrument stamp file
// in the Makefile). All GUITAR values are starting brackets, to be ear-tuned
// with a guitar plugged in.
#ifdef NT3_INSTRUMENT_GUITAR
constexpr bool NT3_GUITAR = true;
#else
constexpr bool NT3_GUITAR = false;
#endif

// --- Oscillator (Tuning Page 1) ---
constexpr float OSC_K            = 0.480f;  // parabolic curve: 0=linear saw, 0.5+=very round
constexpr float OSC_DC_TRIM      = 0.000f;  // fine DC offset after shaping
constexpr float OSC_FOLD_AMT     = 0.150f;  // triangle-core fold (reserved)
constexpr float OSC_PEAK_GAIN    = 1.000f;  // pre-filter oscillator trim
constexpr float OSC_SAW_GAIN     = 1.500f;  // per-waveform level trim
constexpr float OSC_TRI_GAIN     = 1.400f;  // reference level
constexpr float OSC_SQR_GAIN     = 1.400f;

// --- Envelope follower (Tuning Page 2) --- (Stage 3)
// Envelope LP cutoff (higher = faster tracking). The LP must smooth rectifier
// ripple at 2× the fundamental; guitar's ripple starts an octave above bass's,
// so the guitar profile tracks snappier without added wobble.
constexpr float ENV_LP_CUTOFF_HZ = NT3_GUITAR ? 80.0f : 50.0f;
constexpr float ENV_PRE_GAIN     = 1.000f;   // input gain before rectifier
constexpr float ENV_ATTACK_BIAS  = 1.000f;   // filter asymmetry, attack
constexpr float ENV_RELEASE_BIAS = 1.000f;   // filter asymmetry, release


// --- Envelope modulation ---
constexpr float ENV_FILTER_MOD   = 0.500f;   // envelope → filter cutoff (subtle opening)
constexpr float ENV_FOLD_MOD     = 0.250f;   // envelope → wavefold amount (×5 internally)

// --- Env → VCA downward expander (Mode A drone + Mode C SW1=DOWN synth) ---
// NOT a gate: above THRESH the envelope passes unchanged (full touch
// sensitivity); below it the env is scaled toward zero by a power curve, so a
// rig's noise floor can't hold the VCA open (phantom tones) or smear note-offs.
// Deals with different noise floors / input levels without changing behaviour
// over time. THRESH is on the raw env scale (passive bass plays ~0.02–0.1).
// RATIO = 1 → off (linear, prior behaviour, for A/B); higher = more decisive
// pull-down of the floor region (steeper expansion below THRESH).
constexpr float ENV_VCA_EXP_THRESH = 0.020f;
constexpr float ENV_VCA_EXP_RATIO  = 2.0f;

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
constexpr int TRACKING_WRAP_NOTE       = 9;     // base A reference (9 = A) for fixed-mode octave
// Octave-locked tracking (Mode A SW2 MID) folds the CONTINUOUS pitch into K2's
// octave (microtonal — no semitone quantizing). The fold boundary is placed one
// semitone BELOW A (G# = 8), not on A: A is a note you actually play, and a note
// sitting on the boundary flips octaves when it drifts slightly flat/sharp.
// Referencing the fold to G# keeps a played A a semitone inside the octave (folds
// to +1), so it's stable — while still landing A → A3 at K2 noon (matching fixed).
constexpr int TRACKING_FOLD_NOTE       = 8;     // G#, one semitone below A
// 0 = stateless fold: the octave is a pure function of the played pitch. A
// non-zero dead-band (semitones) would add hysteresis at the boundary, but that
// makes the octave direction-dependent.
constexpr float PITCH_FOLD_HYSTERESIS_SEMI = 0.0f;
// Octave-locked register shift (octaves), on top of the fold. 0 = A3 at K2 noon.
constexpr int TRACKING_OCTAVE_SHIFT    = 0;

// Pitch tracker instrument profile (consumed by pitch_tracker.h).
// BASS — today's values exactly: 4x decimation (12 kHz analysis rate), 4-pole
//   fundamental-isolation LP at 400 Hz, lag range ≈30–500 Hz, ~33 ms window.
// GUITAR — range opened upward: 2x decimation (24 kHz analysis rate) for
//   finer lag resolution up high, LP at 1.2 kHz so guitar fundamentals pass,
//   lag range ≈67 Hz (below drop-D) – 1043 Hz (high-E fret 20), ~27 ms window
//   (≈2 periods of the lowest note), hop doubled to keep the same ~5.3 ms
//   update cadence. Parabolic interpolation ON — integer-lag stepping is
//   ~40 cents at 1 kHz, inaudible on bass but out-of-tune on guitar.
constexpr int   TRACK_DEC       = NT3_GUITAR ? 2      : 4;      // decimation 48 kHz → analysis rate
constexpr float TRACK_AA_LP_HZ  = NT3_GUITAR ? 1200.f : 400.f;  // 4-pole anti-alias + fundamental-isolation LP
constexpr int   TRACK_MIN_LAG   = NT3_GUITAR ? 23     : 24;     // shortest period → highest trackable pitch
constexpr int   TRACK_MAX_LAG   = NT3_GUITAR ? 360    : 400;    // longest period → lowest trackable pitch
constexpr int   TRACK_WINDOW    = NT3_GUITAR ? 640    : 400;    // YIN analysis window (samples at analysis rate)
constexpr int   TRACK_HOP       = NT3_GUITAR ? 128    : 64;     // samples between YIN runs (~5.3 ms both profiles)
constexpr float TRACK_THRESHOLD = 0.15f;                        // YIN first-dip threshold (shared)
constexpr bool  TRACK_PARABOLIC = NT3_GUITAR;                   // sub-lag parabolic refine (BASS off = today's output)

// Multi-dip pseudo-polyphonic tracking (docs/MULTI_DIP_TRACKING.md). The
// tracker scans the full lag range and exposes up to TRACK_POLY_VOICES raw dip
// candidates (deepest-first, deduped) via GetPoly*(); the mono outputs keep
// the first-dip semantics exactly. The master switch is a preprocessor flag —
// not just a constexpr — because the OFF build must also drop the tracker's
// poly members (the object lives in .data; extra members alone shift the
// binary). NT3_TRACK_POLY 0 = byte-identical to the mono-only build (verified
// against v0.4).
#define NT3_TRACK_POLY 1
constexpr bool  TRACK_POLY_ENABLE      = NT3_TRACK_POLY != 0;  // poly = always-on, no user control
constexpr int   TRACK_POLY_VOICES      = 3;      // voice cap — dips drown beyond 3 equal-loud notes
constexpr float TRACK_POLY_DIP_MAX     = 0.75f;  // acceptance ceiling for candidate dips
constexpr float TRACK_POLY_DUP_TOL     = 0.03f;  // near-duplicate lag rejection (ratio tolerance)
constexpr bool  TRACK_POLY_HARM_DEDUPE = true;   // reject sub-octave aliases (defeat = experiment lever)
constexpr float TRACK_POLY_HARM_TOL    = 0.035f; // integer-multiple rejection tolerance (relative)

// --- Stage / mix / ladder (Tuning Page 3) --- (Stage 2–3)
constexpr float OSC_GAIN         = 1.500f;   // final osc level into mix
constexpr float LADDER_DRIVE     = 1.800f;   // ladder input gain at noon..CW (higher = more tanh warmth)
constexpr float MODE_A_LADDER_DRIVE_CCW_MAX = 8.000f; // K4 full-CCW ladder drive; ramps up from LADDER_DRIVE at noon so closed settings are fat/saturated, not just muffled
constexpr float LADDER_CUTOFF_OFFSET = 0.000f; // tone knob trim
constexpr float DRY_TRIM         = 1.000f;   // dry path level trim

// --- Mode A K4 bipolar filter (saw / square) ---
// noon → CCW: Moog ladder low-pass, cutoff LP_MAX (open, at noon) → LP_FLOOR
//   (full CCW). FLOOR is raised well above the old 80 Hz — the fully-closed
//   quarter was never used. Drive still ramps up toward CCW (see above).
// noon → CW : ladder held wide open + a 2-pole HPF fades in HPF_MIN (transparent,
//   at noon) → HPF_MAX (full CW), thinning the low end. No drive/loudness comp.
// Triangle keeps its own path (CCW LP sweep + CW wavefold); its HPF stays at
// HPF_MIN (below bass range → transparent), so the serial LP→HPF chain is shared.
constexpr float MODE_A_LP_FLOOR_HZ = 250.f;   // ladder cutoff at K4 full CCW (saw/square)
constexpr float MODE_A_LP_MAX_HZ   = 8000.f;  // ladder cutoff at noon (wide open)
constexpr float MODE_A_HPF_MIN_HZ  = 20.f;    // HPF cutoff at noon (transparent, low end intact)
constexpr float MODE_A_HPF_MAX_HZ  = 2000.f;  // HPF cutoff at K4 full CW (thin)
constexpr float MODE_A_HPF_SMOOTH  = 0.25f;   // per-block cutoff slew (click-free travel)

// --- Mode A bipolar K5 — unison cloud (CCW) + audio-rate FM (CW) ---
// K5 CCW→noon: detuned unison "cloud" thickens toward full CCW, collapses to a
// single clean osc at noon. Same staged fade-in as the Mode C hypersaw, but for
// whatever waveform SW1 selects (saw / tri / square). Noon±deadzone = single osc.
// K5 noon→CW: the input frequency-modulates the osc (exponential / through-zero
// safe). Values are gentler than Mode C's lead ensemble — this is a bass drone.
constexpr int   MODE_A_UNISON_VOICES = 7;      // 1 center + 3 symmetric pairs
constexpr float MODE_A_UNISON_DETUNE_CENTS_MIN = 6.f;   // detune at cloud onset (just past noon)
constexpr float MODE_A_UNISON_DETUNE_CENTS_MAX = 22.f;  // detune at K5 full CCW
constexpr float MODE_A_UNISON_V3_END = 0.30f;  // innermost pair (v=2,4) fully in — "dual detuned"
constexpr float MODE_A_UNISON_V5_END = 0.60f;  // middle pair (v=1,5) fully in — 5-voice
constexpr float MODE_A_UNISON_V7_END = 0.85f;  // outermost pair (v=0,6) fully in — full 7-voice
constexpr float MODE_A_UNISON_SPREAD[MODE_A_UNISON_VOICES] = {
    -1.000f, -0.500f, -0.234f, 0.000f, 0.234f, 0.500f, 1.000f,  // JP-8000-style non-uniform ratios
};
constexpr float MODE_A_K5_DEADZONE   = 0.04f;  // ± around noon that holds a single clean osc

// Triangle K5 CCW is NOT the unison cloud — the 7 voices become a Haible
// ensemble-style just-intonation stack (voice v = MODE_A_HARM_RATIO[v] × f0).
// "Second just ratio scale" — 7 just ratios within one octave, one per voice:
// 1:1, 5:4, 4:3, 3:2, 5:3, 7:4, 2:1 (root, maj3, 4th, 5th, maj6, harm-7th, oct).
// Ascending, played note the bottom. Upper voices gate in toward full CCW;
// amplitude = 1/ratio^ROLLOFF (1.0 = gentle rolloff up the stack; 0 = equal-level).
constexpr float MODE_A_HARM_RATIO[MODE_A_UNISON_VOICES] = {
    1.0f, 1.25f, 1.3333333f, 1.5f, 1.6666667f, 1.75f, 2.0f,
};
constexpr float MODE_A_HARM_ROLLOFF  = 1.0f;
constexpr float MODE_A_HARM_GATE_MS  = 3.0f;   // ensemble voice on/off slew (ms): stepped, near-instant, click-free
constexpr float MODE_A_HARM_OCTAVE   = 1.0f;   // shift the whole triangle series up N octaves (1 = one octave)

// FM modulator conditioning: input → fundamental-isolation LP (→ near-sine) →
// partial normalization → tanh soft-clip → DC block. Then LINEAR THROUGH-ZERO
// FM: freq = f0 · (1 + depth · mod). Pitch-stable because the DC block forces a
// zero-mean modulator (the ±Hz deviations average back to f0). With DEPTH_MAX>1
// the multiplier can go negative — the oscillator phase runs backward through
// zero rather than rectifying, which is the clean, violent, in-tune form of FM.
constexpr float MODE_A_FM_LP_HZ     = NT3_GUITAR ? 500.f : 200.f;  // fundamental-round LP cutoff (2-pole; guitar fundamentals sit higher)
constexpr float MODE_A_FM_DRIVE     = 1.5f;    // tanh pre-gain (bound + sine-round + grit when slammed)
constexpr float MODE_A_FM_DEPTH_MAX = 3.0f;    // max frequency swing at K5 full CW (±300%, through-zero)
constexpr float MODE_A_FM_DEPTH_CURVE = 3.0f;  // depth = MAX·travel^curve; >1 = finer control near noon, intensity builds toward CW
// Partial normalization of the modulator so FM intensity tracks how hard you
// play. divisor = FLOOR + NORM·env:  NORM=0 → amplitude follows playing level
// (loud = more FM); NORM=1 → constant AGC (level-independent). FLOOR sets the
// quiet-end scale (smaller = more FM on soft notes).
constexpr float MODE_A_FM_NORM      = 0.35f;   // 0 = full level-tracking, 1 = full AGC
constexpr float MODE_A_FM_FLOOR     = 0.05f;   // divisor floor (quiet-playing modulator scale)
constexpr float MODE_A_FM_DC_HZ     = 8.0f;    // modulator DC-block cutoff → zero-mean → stable pitch

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
// Guitar: fundamentals above the bass value would be filtered out before the
// octave generator (effect dies up the neck), so the LP rises with the range.
constexpr float MODE_C_CHEBY_LP_HZ     = NT3_GUITAR ? 600.0f : 250.0f;

// Moog ladder (SW2=UP, K1 cutoff / K2 resonance / K3 env amount).
constexpr float MODE_C_LADDER_RES_MAX = 1.2f;  // pushed past ~1.0 self-osc threshold; in-loop tanh bounds it
constexpr float MODE_C_ENV_MOD_RANGE  = 420.0f; // depth on normalized env; lift = 1 + env_scaled * |k3| * range
constexpr float MODE_C_ENV_SCALE      = 10.0f; // passive-bass env normalization (matches Mode A's effective ×10)
constexpr float MODE_C_CUTOFF_MAX_HZ  = 10000.f; // top clamp for env-modulated cutoff (keeps Huovilainen stable)
constexpr float K3_DEADZONE           = 0.05f; // bipolar K3 noon ±deadzone → env amount = 0
constexpr float MODE_C_K3_CURVE       = 2.0f;  // response curve on |k3| env amount: >1 = fine near noon, coarse toward extremes (endpoints unchanged)
// Audio-rate cutoff self-FM (SW2=UP Moog). Fades in from MODE_C_MOOG_FM_START
// (a bit before K5 noon — a touch of FM is always welcome) up to full CW,
// modulating the ladder cutoff by the filter input signal:
// mod_cutoff *= 1 + depth·amt·in. The input's harmonics splatter sidebands
// around the resonant peak so it reads gritty/vocal instead of a sterile sine.
// Cutoff stays clamped to [MIN,MAX] Hz.
constexpr float MODE_C_MOOG_FM_START  = 0.4f;  // K5 position where FM begins fading in (<0.5 = before noon)
constexpr float MODE_C_MOOG_FM_DEPTH  = 1.5f;  // FM depth at K5 full CW (linear, ×filter input)

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
// CW release time shortens with K3 travel (snappier decay toward RELEASE_MIN_MS).
// CCW attack (swell) is a FIXED time constant — scaling it fought the depth/clamp
// coupling (deeper mod outruns longer tau) and felt wrong, so the swell stays put.
constexpr float MODE_C_FILTER_ENV_ATTACK_MS      = 600.0f;  // CCW slow-rise swell (constant)
constexpr float MODE_C_FILTER_ENV_RELEASE_MS     = 150.0f;  // CW release at noon (ladder)
constexpr float MODE_C_FILTER_ENV_RELEASE_MIN_MS = 40.0f;   // CW release at full CW (shortest)

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
constexpr float MODE_C_CUTOFF_MIN_HZ = 20.0f;
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

// Overdrive (CCW half — ACTIVE). K4 full CCW. Models a Tube Screamer into a tube
// amp: tunable pre-clip HPF → pedal saturation stage → a touch of low-passed
// clean summed back (TS body) → amp saturation stage. One shared symmetric
// primitive Saturate(x) = x/(1+|x|); asymmetry comes ONLY from a bias offset
// (Saturate(x+bias) − Saturate(bias), DC removed — never baked into the curve).
// Gain staging is reset vs the old single-stage tanh — all values ear-tunable.
constexpr float MODE_C_OD_HP_HZ       = NT3_GUITAR ? 350.0f : 180.0f; // pre-clip high-pass into the pedal stage (anti-mud). Higher = crunchier/more mid-focused; lower = fuller into the clip (a real TS corner is ~720 Hz — guitar can sit tighter)
constexpr float MODE_C_OD_DRIVE_MAX   = 60.0f;  // pedal-stage drive at K4 full CCW (1× at noon)
constexpr float MODE_C_OD_BIAS        = 0.6f;   // pedal-stage asymmetry (bias offset only). 0 = symmetric; higher = more lopsided
constexpr float MODE_C_OD_CLEAN_MIX   = 0.4f;   // low-passed clean summed in before the amp stage (TS body/character)
constexpr float MODE_C_OD_CLEAN_LP_HZ = 300.0f; // low-pass on that clean blend (keeps lows/body, no fizz)
constexpr float MODE_C_OD_AMP_DRIVE   = 4.0f;   // amp-stage gain after the pedal stage (the "into a tube amp" stack)
constexpr float MODE_C_OD_AMP_BIAS    = 0.3f;   // amp-stage asymmetry (bias offset only)
// Output makeup interpolates across the CCW travel from COMP_AT_NOON (just off
// noon — a BOOST, because the HPF strips the fundamental so the min-drive wet is
// much quieter than the clean dry; this matches levels so there's no drop/click)
// down to COMP_AT_MAX at full CCW (a CUT, to tame the loud top). Both ear-tune.
constexpr float MODE_C_OD_COMP_AT_NOON = 4.00f; // makeup just off noon — boosts the quiet min-drive wet up to ≈ clean level (no transition drop)
constexpr float MODE_C_OD_COMP_AT_MAX  = 0.65f; // makeup at K4 full CCW — cut to tame the loud top end (bumped from 0.40 to cover the cubic Saturate's ±2/3 ceiling)
constexpr float MODE_C_OD_K4_CURVE    = 3.0f;   // taper on K4-CCW → PEDAL drive: >1 = finer near noon + max packs into less travel at the top (full CCW max unchanged). 1 = linear
constexpr float MODE_C_OD_AMP_KNEE    = 0.72f;  // K4-CCW position where the AMP stage starts ramping in (below = unity, so pedal/TS gain builds first; amp only enters over the top of the travel)

// Post-filter peak limiter (Mode C only, all SW2 modes).
// 2-band split: LF (≤ SPLIT_HZ) passes through untouched so bass fundamentals
// don't duck when resonance peaks fire the limiter. HF is soft-knee limited.
// "Warmth-when-working" — gain reduction modulates a touch of tanh saturation,
// so peaks gain mild character without harmonics on quiet/clean signals.
constexpr float MODE_C_LIMIT_SPLIT_HZ  = NT3_GUITAR ? 250.f : 160.f; // 2-band crossover (one-pole; sits just under the instrument's fundamental range)
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
constexpr float PHASER_FB_MAX          = 0.98f;   // feedback ceiling — wide open; tanh-in-loop bounds runaway and detune keeps it from going sterile, so K2 full CW reaches into bounded self-oscillation
constexpr float PHASER_STAGE_SPREAD    = 0.04f;   // per-stage allpass coeff detune; breaks perfect notch alignment (organic, less "digital"). 0 = all stages identical

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
// Auto (stochastic) event onset along K4 travel. Below this fraction of
// effect_pos, NO auto events fire — that early travel is reserved for note-on
// triggered events only. Above it, the auto amount ramps in (squared curve, so
// it starts slow) up to full rate at the extreme. Reactive triggering is
// unaffected: it fires from the moment K4 leaves the deadzone.
constexpr float GLITCH_AUTO_ONSET           = 0.15f; // fraction of travel reserved for triggered-only events

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
// Wet high-pass (2-pole) — keeps the wet bus above the dry instrument's low
// range so the wet sits on top instead of fighting the fundamentals. Guitar's
// dry range starts an octave up, so the shelf rises with it.
constexpr float WET_HPF_FREQ = NT3_GUITAR ? 200.f : 120.f;
constexpr float K5_CENTER_DEADZONE = 0.05f;  // ±5% deadzone around center
constexpr float REVERB_INPUT_GAIN  = 0.40f;  // gain into the Clouds reverb
constexpr float REVERB_TIME        = 0.70f;  // reverb decay (krt in Clouds)
constexpr float FEEDBACK_MAX = 2.0f;  // K5-CW ring-buffer feedback ceiling (tanh limiter + duckers keep it a controlled drone)
constexpr float FB_UNISON_SCALE     = 0.60f; // feedback scale at unison (K1=0). Unison piles up coherently, so it's cut vs intervals; ramps to 1.0 by +3 semi. Raise toward 1.0 for stronger unison feedback.
constexpr float REVERB_AMT_SMOOTH_COEF = 0.002f; // one-pole on K5 reverb amount, ~10 ms tc
constexpr float PARAM_SMOOTH_COEF      = 0.002f; // generic control smoother (Smoother), ~10 ms tc — de-zipper audio-rate knob gains

// --- Mode B bipolar K2 (buffer / direction) ---
// K2 is bipolar around noon. |K2-0.5| (past the deadzone) = buffer length +
// timescale, as the old unipolar K2 did. Sign = global grain playback
// direction: CW = forward, CCW = backward. Noon deadzone = direct-texture
// passthrough (grain engine bypassed). See docs/MODE_B_DISCOVERY.md.
constexpr float GRAIN_K2_DEADZONE = 0.06f;  // ±6% around noon → passthrough
// Bias of the CW-character random reverse (SW2-independent). On the forward
// (CW) K2 side a grain reverses with P = k3·this; on the backward (CCW) side
// the bias flips (grains mostly reverse, occasionally play forward).
constexpr float GRAIN_REVERSE_BIAS = 0.6f;

// --- Mode B bipolar K3 (character / Clouds density) ---
// K3 is bipolar around noon. Noon deadzone = neutral single coherent stream;
// CW = character/glitch, CCW = MI-Clouds-style deterministic density. Both
// sides are one continuous grain axis through the noon origin (see below).
// See docs/MODE_B_DISCOVERY.md.
constexpr float GRAIN_K3_DEADZONE  = 0.06f; // ±6% around noon → neutral stream
// K3 is a single grain axis through the noon origin (GRAIN_NEUTRAL_LEN,
// GRAIN_NEUTRAL_OVERLAP). Both sides depart from that same anchor so crossing
// noon is seamless:
//  CCW  — lengthen/slow: k3mag lerps length neutral→CLOUD_LEN_MAX, overlap held
//         at neutral so the emission RATE falls as grains grow — a long, slow
//         smear that leans on the deep buffer (cloud).
//  CW   — shorten with chaos: gc_sq lerps length neutral→GRAIN_MIN_LEN, overlap
//         thins neutral→1×, scatter/jitter/loops/reverse rise (glitch).
// grain_len = k2_scale · length, so K2's timescale is an overall size zoom.
constexpr float GRAIN_NEUTRAL_LEN     = 14400.f; // noon base grain length (300 ms, ×k2_scale)
constexpr float GRAIN_NEUTRAL_OVERLAP = 2.0f;    // default overlap anchor — smooth minimum for Hann grains; sparse/glitch-friendly
constexpr float GRAIN_OVERLAP_MID_ECHO = 6.0f;   // richer anchor for echo (K2 engaged) + SW2 MID — lets that mode bloom
constexpr float CLOUD_LEN_MAX         = 96000.f; // full-CCW base length (2 s, ×k2_scale) — long slow smear

// SW2 MID pitch re-roll: how many grains share a random pitch before a new one
// is rolled from the ±1 resonance window. Baseline is change-EVERY-grain
// (interval 1), which holds across all of neutral + the CW glitch half — the
// shimmer re-rolls every grain there. Going CCW (cloud) the pitch is HELD longer
// and longer, interval ramping 1 → GRAIN_PITCH_HOLD_MAX at full CCW, so the slow
// smear settles onto stable, tonal pitches. (=1 disables the hold: change
// every grain across the whole range, CCW cloud included.)
constexpr int   GRAIN_PITCH_HOLD_MAX   = 1;

// Per-grain length variation (K3-CW character). Each grain's length is scattered
// BELOW the coarse block base by a skewed random factor, so the loop/stutter
// FREQUENCY (≈ 1/length) feels random instead of locking to the knob value. The
// shortening is GEOMETRIC toward an absolute audio-rate floor (LEN_MIN), so as a
// grain gets short its repeat cycle crosses ~20 Hz into the audio range and the
// CD-hang becomes a pitched buzz (brrr ≈ 100 Hz → kriii ≈ 1 kHz). Skew keeps
// these rare at low K3; DEPTH·glitch_amount sets how far toward LEN_MIN the
// knob reaches — so the knob raises the CEILING (higher, more frequent buzzes)
// while the typical grain stays coarse — contrast, not a uniform speed-up.
//  Repeats are COUPLED to the length: repeats ≈ base_len / this_len, so a short
//  grain gets many reps (sustains the buzz), a long grain gets one.
constexpr float GRAIN_CW_LEN_FLOOR      = 4800.f; // coarse block-base short-end (~100 ms) — the typical grain
constexpr float GRAIN_STUTTER_LEN_MIN   = 32.f;   // shortest per-grain length (~1.5 kHz repeat) — audio-rate buzz
constexpr float GRAIN_LEN_VAR_DEPTH     = 1.0f;   // how far toward LEN_MIN full CW reaches (1 = all the way)
constexpr float GRAIN_STUTTER_REACH_GAMMA = 3.0f; // knob→reach curve: >1 keeps mid-CW percussive, bends to audio-rate only near full CW
constexpr float GRAIN_LEN_VAR_SKEW      = 1.5f;   // >1 → short/buzzing grains stay rare (raises ceiling not floor)
constexpr int   GRAIN_STUTTER_MAX_LOOPS = 200;    // cap on length-coupled repeats (lets a short grain sustain a tone)

// Transient-responsive triggering (Mode B grain engine, K3-CW glitch side only
// — off on the CCW cloud and the K3-neutral passthrough, which stay a clean
// free-running wash). A note-on — a rising edge on the input envelope — fires an
// extra grain burst anchored to the freshly-played note, so the glitch engine
// answers our playing (env-mode feel). Burst grains read the newest ring
// content (delay 0 → the per-grain
// safety floor sets the physical minimum), so they are grains of the note just
// played, pitched by SW2/K1 and shaped by K3 like any grain. Detector thresholds
// are relative to the passive-bass env scale (env_val ≈ 0.02–0.1) — ear-tune.
constexpr float TRANSIENT_SLOW_COEF     = 0.0006f; // baseline follower speed (lower = slower reference level)
constexpr float TRANSIENT_RISE          = 1.8f;    // env must exceed baseline × this to count as an attack
constexpr float TRANSIENT_GATE          = 0.02f;   // min env to trigger (noise-floor guard)
constexpr int   TRANSIENT_REFRACTORY    = 2400;    // re-trigger lockout in samples (~50 ms @ 48k)
constexpr int   TRANSIENT_BURST         = 2;       // grains fired per attack
constexpr int   TRANSIENT_BURST_SPACING = 1600;    // samples between burst grains (~33 ms)

// Hard floor on grain length (safety clamp). The character sweep's own short
// end is 480 samples (≈10 ms), set in the grain_len formula.
constexpr size_t GRAIN_MIN_LEN = 64;

// Note: K2-noon + K3-CCW (the old "metronomic granulation") is now handled by
// the main grain engine on the live ring with ~0 read delay — see live_grain
// in ProcessGranular. No separate metro constants needed.
