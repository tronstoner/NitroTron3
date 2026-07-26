#pragma once
//
// armitage — tuning constants.  Impulse synth / resonator / drone.  SW3 DOWN.
//
// Design principle 3 (spec): anything not on a control is a NAMED constant here,
// never a literal buried in the DSP. Values that the spec marks TBD are set at
// stage 1 and flagged // STAGE-1 GUESS so the integrating engineer can find them.
//
// Spec: docs/ChronoTron3/impulse resonator - armitage/IMPULSE_SYNTH_SPEC.md
// Reference math: saturation.py / validate.py (same directory).
//
// Included from armitage.h. Requires <cmath> / <cstdint> (pulled in by armitage.h).

#include <cstdint>

namespace armitage_k {

// ---------------------------------------------------------------------------
// Input conditioning — asymmetric saturation  (Findings F3)
//   e = (1-a)*tanh(k*x) + a*|tanh(k*x)|
// a = asymmetry (K4), k = drive (CONSTANT, not a control).
// ---------------------------------------------------------------------------
static constexpr float DRIVE       = 1.0f;   // F3: higher costs dynamics, no lift
static constexpr float ASYM_MIN    = 0.0f;   // F3 knob range floor
static constexpr float ASYM_MAX    = 1.0f;   // extended past F3's 0.5 plateau to full
                                             // rectification — K4 was too subtle (discovery)
static constexpr float CREST_FLOOR = 3.0f;   // F4 warn threshold (informational)
static constexpr float DC_BLOCK_R  = 0.9975f;// one-pole DC blocker on exciter
                                             // (|tanh| adds DC that must go)
static constexpr float EXCITE_GAIN = 1.0f;   // makeup after conditioning
                                             // STAGE-1 GUESS — tune by ear

// ---------------------------------------------------------------------------
// Note-set behaviours (SW2 A/B). The active tunings are set per behaviour at
// control rate — no longer a single fixed chord:
//   SW2 UP   = fixed dense bank  (semitone comb; the spec's validation bed)
//   SW2 MID  = mono track        (one voice at the tracked pitch)
//   SW2 DOWN = key-quant multi   (accumulate the last N in-key notes played)
// Register (K1) shifts whatever set is active by +/- REGISTER_OCT octaves.
// Only poly *chord detection* is deferred (the hard note-set estimator).
// ---------------------------------------------------------------------------

// Fixed dense bank — semitone comb over ~2 octaves (matches the 25-bin bank the
// Findings were validated against). This is the voice/CPU/SDRAM ceiling.
static constexpr int   BANK_NOTE_COUNT = 25;      // resonators in the fixed bank
static constexpr float BANK_BASE_MIDI  = 33.0f;   // A1 (55 Hz)
static constexpr float BANK_STEP_SEMI  = 1.0f;    // semitone spacing → 2 octaves

static constexpr int   MAX_VOICES = BANK_NOTE_COUNT;  // array/SDRAM ceiling (25)

// Mono track — single voice following the continuous tracked pitch.
static constexpr int   MONO_VOICES = 1;

// Key-quantised multivoice — accumulate distinct in-key notes into a stack
// (play an arpeggio → build a chord). Poor-man's poly, no estimator.
static constexpr int   QUANT_MAX_VOICES = 6;
static constexpr float QUANT_ROOT_MIDI  = 33.0f;  // key root (A)
static constexpr int   QUANT_SCALE_LEN  = 5;
static constexpr int   QUANT_SCALE[5]   = {0, 3, 5, 7, 10};  // A minor pentatonic
static constexpr float QUANT_GATE_ENV   = 0.02f;  // input env to accept a new note

// ---------------------------------------------------------------------------
// Comb / extended Karplus-Strong core (SW1 UP)
// Round-trip Dtot = fs/f (one period); the loop reads at Dfrac and Dfrac+1 and
// averages them — the two-point-average loop filter, |L(f)| = cos(pi f/fs).
// g derived per voice from the F1 T60 relation AND the actual Dtot (register-
// dependent) so high registers do not choke (spec Register section, REQUIRED).
// ---------------------------------------------------------------------------
static constexpr int   COMB_MAX_SAMPLES = 2048;  // >= longest Dtot; 2048 -> ~23 Hz
static constexpr float G_MAX            = 0.9995f;// stability clamp on loop gain
static constexpr float COMB_MAKEUP      = 0.35f; // STAGE-1 GUESS — level match vs modal

// Dispersion allpass chain in the comb loop (K3 = structure for comb).
// First-order allpasses add inharmonicity / stringy detune. K3 0->1 scales the
// coefficient 0 -> DISP_MAX across DISP_STAGES stages.
static constexpr int   DISP_STAGES = 2;
static constexpr float DISP_MAX    = 0.6f;   // STAGE-1 GUESS — max allpass coeff

// ---------------------------------------------------------------------------
// Modal bandpass bank core (SW1 DOWN)
// N two-pole resonators per note. Pole radius r from the SAME T60 target as the
// comb (r = 10^(-3/(T60*fs))), so decay is level-matched between cores.
// Per-mode decay derived from that target: higher modes ring shorter (frequency-
// dependent damping, to mirror the comb loop filter's native behaviour).
// ---------------------------------------------------------------------------
// 6 partials (was 3): with fundamentals A1–A3 a 3-partial bank topped out ~660 Hz
// → muted/dull. More partials reach into the mid/high for a fair A/B vs comb.
static constexpr int   MODES_PER_VOICE = 6;
// Base (harmonic) partial ratios; K3 (structure) stretches them toward
// inharmonic bell/plate spreads: ratio_k -> ratio_k * (1 + spread*(k-1)).
static constexpr float MODAL_BASE_RATIOS[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
static constexpr float MODAL_SPREAD_MAX     = 0.5f;   // K3 max inharmonic stretch
static constexpr float MODAL_DAMP_EXP       = 0.3f;   // higher modes decay faster (0.6
                                                      // was too dark): T60_k = T60/ratio^EXP
static constexpr float MODAL_MAKEUP         = 1.0f;   // STAGE-1 GUESS — level match
static constexpr float R_MAX                = 0.99995f;// stability clamp on pole radius

// ---------------------------------------------------------------------------
// Damping (K2) — target decay time, the primary timbre control.
// Wide range on purpose (discovery ethos: long drones welcome).
// ---------------------------------------------------------------------------
static constexpr float T60_MIN_S = 0.08f;   // short, plucky
static constexpr float T60_MAX_S = 8.0f;    // long drone

// ---------------------------------------------------------------------------
// Register (K1) — bipolar octave multiplier on resonator tuning.
// Continuous +/- REGISTER_OCT octaves (CCW sub, noon unison, CW upper).
// Stage 1: single shift, NO stacking (bounded voice count). See return notes.
// ---------------------------------------------------------------------------
static constexpr float REGISTER_OCT = 1.0f;  // +/- 1 octave of continuous travel

// ---------------------------------------------------------------------------
// Output filter (K5) — a GATED AR envelope (its own generator, gated by the
// input, NOT a follower) drives a steep 4-pole (24 dB/oct) non-resonant lowpass
// on the resonator output. Closed = muted. Full note-on/off cycle:
//   input rises past ONSET_ON  → note-on  → attack toward open (retriggers each note)
//   held above ONSET_OFF       → sustain at open (NO release while signal present)
//   falls below ONSET_OFF      → note-off → release toward closed
// K5 is bipolar: noon = shortest attack AND release; CCW stretches the attack,
// CW stretches the release. Perceived decay = filter release, decoupled from
// resonator damping (K2). Inspired by the Lost+Found topology (not a replica).
// ---------------------------------------------------------------------------
static constexpr float FENV_ATK_MIN_MS = 1.0f;    // noon: snappy attack
static constexpr float FENV_ATK_MAX_MS = 1000.0f; // full CCW: slow swell
static constexpr float FENV_REL_MIN_MS = 5.0f;    // noon: snappy release
static constexpr float FENV_REL_MAX_MS = 3000.0f; // full CW: long tail
static constexpr float OUTFILT_CLOSED_HZ = 40.0f;   // env=0: filter shut → mutes
static constexpr float OUTFILT_OPEN_HZ   = 9000.0f; // env=1: filter open
static constexpr float ONSET_ON          = 0.02f;   // input env above → note-on (gate)
static constexpr float ONSET_OFF         = 0.008f;  // input env below → note-off (hysteresis)

// ---------------------------------------------------------------------------
// Output limiter (in-spec). Simple mono soft-asymptote peak limiter; the
// NitroTron3 peak_limiter.h is unavailable here (it pulls MODE_C_* constants),
// so armitage carries its own. LF corner "tracks the lowest active voice" is a
// stage-4 item — fixed here.
// ---------------------------------------------------------------------------
static constexpr float LIMIT_THR       = 0.9f;
static constexpr float LIMIT_RATIO_INV = 0.25f;   // soft knee slope above thr
static constexpr float LIMIT_ATK_MS    = 1.0f;
static constexpr float LIMIT_REL_MS    = 120.0f;

// ---------------------------------------------------------------------------
// Param smoothing (control-rate targets -> per-block). Kills zipper on K4/K3/K5.
// K1/K2 change coefficients (delay length / decay) and are recomputed at control
// rate without smoothing — fast sweeps may glitch pitch (stage-1 acceptable).
// ---------------------------------------------------------------------------
static constexpr float PARAM_SMOOTH = 0.05f;

}  // namespace armitage_k
