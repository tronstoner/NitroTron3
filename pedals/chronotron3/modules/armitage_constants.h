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
static constexpr float ASYM_MAX    = 0.5f;   // F3 plateau — full useful travel
static constexpr float CREST_FLOOR = 3.0f;   // F4 warn threshold (informational)
static constexpr float DC_BLOCK_R  = 0.9975f;// one-pole DC blocker on exciter
                                             // (|tanh| adds DC that must go)
static constexpr float EXCITE_GAIN = 1.0f;   // makeup after conditioning
                                             // STAGE-1 GUESS — tune by ear

// ---------------------------------------------------------------------------
// Voicing / chord (the STUB note set — no analysis at stage 1)
// Root A2 = MIDI 45 = 110 Hz, which is the reference F0 the Findings were
// measured at. Minor triad {root, m3, P5} for a moody drone. Easy to change.
// ---------------------------------------------------------------------------
static constexpr float CHORD_ROOT_MIDI          = 45.0f;   // A2, 110 Hz
static constexpr int   CHORD_NOTE_COUNT         = 3;
static constexpr float CHORD_INTERVALS_SEMI[3]  = {0.f, 3.f, 7.f};  // minor triad

// Voice budget. Stage 1 = no register stacking, so active voices == chord notes.
// Arrays are sized for MAX so future stacking needs no reshape. (spec: voice
// count is a CPU lever, TBD at stage 1 — chosen 6, log in return.)
static constexpr int MAX_VOICES    = 6;   // per-core allocation ceiling
static constexpr int ACTIVE_VOICES = CHORD_NOTE_COUNT;  // stage 1: 3

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
static constexpr int   MODES_PER_VOICE = 3;
// Base (harmonic) partial ratios; K3 (structure) stretches them toward
// inharmonic bell/plate spreads: ratio_k -> ratio_k * (1 + spread*(k-1)).
static constexpr float MODAL_BASE_RATIOS[3] = {1.0f, 2.0f, 3.0f};
static constexpr float MODAL_SPREAD_MAX     = 0.5f;   // K3 max inharmonic stretch
static constexpr float MODAL_DAMP_EXP       = 0.6f;   // higher modes decay faster:
                                                      // T60_k = T60 / ratio_k^EXP
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
// Env-coupled output filter (K5) — env follower -> lowpass cutoff on the wet.
// K5 is bipolar attack/release: CCW = fast attack / slow release (percussive
// filter pop), CW = slow attack / fast release (swell). Noon ~ symmetric medium.
// Interpretation of "bipolar attack/release" is a STAGE-1 GUESS — see return.
// ---------------------------------------------------------------------------
static constexpr float ENV_ATK_FAST_MS = 1.0f;    // K5 CCW attack
static constexpr float ENV_ATK_SLOW_MS = 300.0f;  // K5 CW  attack
static constexpr float ENV_REL_SLOW_MS = 800.0f;  // K5 CCW release
static constexpr float ENV_REL_FAST_MS = 30.0f;   // K5 CW  release
static constexpr float ENV_SENS        = 10.0f;   // passive-bass scaling (env ~0.02-0.1)
static constexpr float OUTFILT_BASE_HZ  = 200.0f; // cutoff floor (env = 0)
static constexpr float OUTFILT_RANGE_HZ = 6000.0f;// added at env = 1

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
