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

// Excitation = the conditioned INPUT signal ONLY. The pulse / noise-burst trigger
// was dropped (2026-08): the resonator is driven continuously by what you play —
// there is no struck impulse. Timbre is shaped by two conditioning stages:
//   PRE  — asymmetric tanh drive INTO the resonator (DRIVE, PULSE_ASYM below), and
//   POST — the K4 post-loop drive on the OUTPUT (POST_DRIVE_* in the comb section).
// K4 is now the post-drive knob (was an excitation morph).
static constexpr float PULSE_ASYM = 0.3f;   // fixed PRE-conditioning asymmetry (input side):
                                            // e = (1-a)*tanh(k*x) + a*|tanh(k*x)|

// Excitation envelope — a guardrail on the resonator FEED around chord changes. The
// input drives the combs continuously, which BURSTS: a new pluck blasts the OLD/
// mistuned chord during the settle window before the snapshot, and keeps driving the
// resonators while they GLIDE to the new pitches. So we DUCK the feed on each onset and
// ATTACK it back in once the new chord is locked → the excitation swells into the
// settled chord instead of blasting the transition. (Independent of the K5 output gate.)
static constexpr float EXC_DUCK_LEVEL = 0.0f;   // feed gain while ducked (0 = fully muted; raise a
                                                // little for residual sustain through the change)
static constexpr float EXC_DUCK_MS    = 3.0f;   // duck speed on each onset (fast mute)
static constexpr float EXC_ATTACK_MS  = 120.0f; // feed ramp-in AFTER the snapshot locks the chord
                                                // (set ≈ glide time so the swell covers the glide)

// ---------------------------------------------------------------------------
// STAGE-1 TEST BUILD (spec stage 1: "both cores at fixed tuning, played into
// directly, no analysis"). When STAGE1_TEST is on, armitage runs ONE struck
// voice at a fixed sub-octave pitch — no SW2 behaviours, no YIN tracking — and
// the K5 filter gate is keyed off the RESONATOR ring instead of the input, so a
// struck note rings out instead of being cut off. This isolates the strike +
// resonator character for tuning; flip off to restore the full behaviour build.
// K1 (register) still shifts the fixed pitch +/- 1 octave.
// ---------------------------------------------------------------------------
static constexpr bool  STAGE1_TEST      = true;
static constexpr bool  STAGE1_TRACK     = true;   // pitch follows the played note (mono
                                                  // YIN) so positions/octaves can be
                                                  // judged; false = fixed STAGE1_TEST_MIDI
static constexpr float STAGE1_TEST_MIDI = 45.0f;  // fixed fallback pitch (A2); K1 +/- 1 oct
// Detuned stack on the fixed pitch: near-unison voices (cents apart, in fractional
// semitones) beat/shimmer; + a fifth and an octave partial thicken it into a wall.
static constexpr int   STAGE1_STACK_N   = 2;      // thin — hear the pulse, not a cloud
static constexpr float STAGE1_STACK[STAGE1_STACK_N] = {
    0.0f,    // unison
  -12.0f,    // sub-octave — the fat deep low
};

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
// Comb / extended Karplus-Strong — the resonator core (A/B decided: comb wins,
// modal dropped). Round-trip Dtot = fs/f (one period); the loop reads at Dfrac
// and Dfrac+1 and averages them — two-point-average loop filter, |L(f)| =
// cos(pi f/fs). g derived per voice from the F1 T60 relation AND the actual
// Dtot (register-dependent) so high registers do not choke (spec, REQUIRED).
// ---------------------------------------------------------------------------
static constexpr int   COMB_MAX_SAMPLES = 2048;  // >= longest Dtot; 2048 -> ~23 Hz
static constexpr float G_MAX            = 0.9995f;// (unused — see SELF_OSC_G_MAX)
static constexpr float COMB_MAKEUP      = 0.35f; // STAGE-1 GUESS — output level

// Self-oscillation. The loop is no longer purely dissipative: an in-loop tanh
// saturator lets loop gain sit AT/ABOVE unity so the resonator SELF-OSCILLATES —
// builds to a sustained level and holds (the "infinite, gated by the filter"
// character) instead of decaying. LOOP_BOOST scales the T60-derived g so the K2
// CW end goes over unity (short T60 still decays = plucky). The saturator bounds
// the runaway; DRIVE*TRIM ≈ 1 keeps small-signal loop gain ≈ g (plucky T60 intact)
// while large signals saturate → harmonics + amplitude bound. All STAGE-1 GUESS.
static constexpr float LOOP_BOOST      = 1.0f;   // 1.0 = driven resonator (long ring,
                                                 // breathes with playing); >1 = self-osc
                                                 // (infinite but STATIC, no dynamics).
                                                 // The self-osc<->dynamic lever.
static constexpr float SELF_OSC_G_MAX  = 1.10f;  // hard clamp on boosted loop gain
static constexpr float COMB_SAT_DRIVE  = 2.0f;   // in-loop tanh drive (harmonics)
static constexpr float COMB_SAT_TRIM   = 0.5f;   // FIXED post-tanh trim (level)

// ---------------------------------------------------------------------------
// Post-loop drive (K4) — the "driven / gnarl" character. An asymmetric waveshaper
// on the resonator OUTPUT, before the K5 filter (amp topology: drive → tone). It
// sits OUTSIDE the feedback loop, so unlike in-loop saturation it can be pushed
// hard with ZERO stability risk (in-loop drive ran the resonator away). K4 maps the
// drive amount (exp taper → fine control at the clean/low end). A fixed bias adds
// even-harmonic "saw" fatness; the static tanh(bias) DC is subtracted so no offset
// reaches the filter.
// ---------------------------------------------------------------------------
static constexpr float POST_DRIVE_MIN    = 1.0f;   // K4 CCW — ~clean (near-unity)
static constexpr float POST_DRIVE_MAX    = 30.0f;  // K4 CW  — heavily driven / gnarly
static constexpr float POST_DRIVE_BIAS   = 0.4f;   // asymmetric bias → even harmonics (saw body)
static constexpr float POST_DRIVE_MAKEUP = 1.0f;   // TRIM on top of the automatic 1/√drive
                                                   // level compensation. 1.0 = clean end at
                                                   // unity. Drop (→0.5) if still hot; raise for
                                                   // more output. (Auto comp keeps loudness ~flat
                                                   // across K4 so cranking it adds grit, not volume.)

// Feedback FM (K3 = depth) — the "gnarl" (and, at depth, a period-doubled SUB-OCTAVE).
// Each sample modulates the loop's own delay-read position by its LOWPASSED last output
// → phase modulation → sidebands + subharmonic. Depth is a FRACTION OF THE DELAY PERIOD
// (pitch-independent). LOCKED from the SW1 exploration pass (2026-08):
//   • Modulator cutoff TRACKS pitch per voice: cutoff = FM_MOD_TRACK_MULT × the voice
//     fundamental (low notes → low cutoff/deep, high notes → high cutoff). Keeps the
//     modulator on the fundamental at every pitch — a FIXED cutoff fizzled the highs
//     and starved the lows. Computed per voice in RecomputeVoices.
//   • In-loop drive OFF (gentlest saturator — just bounds the loop, no added harmonics).
//   • K3 sweeps depth; sweet spot landed ~8:45.
static constexpr float FM_DEPTH_FRAC_MAX = 0.40f;  // K3 0..1 → 0..this (fraction of the period).
                                                   // Sweet ~8:45; top = the overshoot/"needle" edge.
static constexpr float FM_MOD_TRACK_MULT = 1.0f;   // modulator cutoff = this × voice fundamental.
                                                   // Higher = brighter modulator per note (toward
                                                   // raw/fizz); lower = darker/deeper. Low boundary
                                                   // by ear was ~fundamental (9:00 on the sweep).
static constexpr float FM_DRIVE_OFF      = 1.0f;   // in-loop drive "off" = gentlest that still
                                                   // bounds the loop (SW1-DOWN full CCW).
static constexpr float COMB_SAT_UNITY    = 1.0f;   // DRIVE*TRIM invariant → small-signal loop gain = g

// ---------------------------------------------------------------------------
// Onset chord detector — the struck-chord grab. A chromatic bandpass filterbank
// runs continuously; on a strum onset (after a short settle past the broadband
// pick transient) the band energies are snapshotted, peak-picked, harmonic-sieved
// (drop octave/12th/2-octave overtones of stronger notes), and the top few bins
// become the resonator chord. Discrete per-strum grab — NOT continuous poly
// tracking. When on, this is the note source (supersedes the SW2 behaviours /
// stage-1 fixed pitch). All STAGE-1 GUESS — tune by ear.
static constexpr bool  CHORD_DETECT        = true;
static constexpr int   CHORD_N_BINS        = 48;    // chromatic bins (4 octaves): E1 .. D#5
static constexpr float CHORD_BASE_MIDI     = 28.0f; // E1 (~41 Hz) — MUST reach bass fundamentals.
                                                    // Bass open E1/A1/D2 sit BELOW the old E2 (40)
                                                    // floor, so the detector could only see their
                                                    // harmonics → octave-up garbage. Now covers
                                                    // bass + most guitar. (5-string low B0=23 →
                                                    // lower BASE if needed.)
static constexpr float CHORD_Q             = 12.0f; // bandpass selectivity per bin
static constexpr float CHORD_ATTACK_BLANK_MS = 70.0f;// DROP the attack: after an onset, ignore this
                                                    // long (the pick transient = broadband noise, no
                                                    // pitch info) before the detector starts MEASURING.
                                                    // Band energies are then the AVERAGE over the
                                                    // settled window (blank → snapshot), not a peak-
                                                    // hold — so sustained chord notes read evenly.
static constexpr float CHORD_SNAP_DELAY_MS = 180.0f;// settle after onset before snapshot.
                                                    // Deliberately LONG: this is a drone/swell
                                                    // voice, never a snappy bass synth, so we
                                                    // trade latency for STABILITY — wait past the
                                                    // guitar's broadband pick transient (which
                                                    // lights up every band = ghost notes) and grab
                                                    // the settled harmonic tone. Same lesson as
                                                    // vestige's K3 2:00-2:30 freeze sweetspot. The
                                                    // latency hides under a slow K5 attack (swell).
static constexpr int   CHORD_MAX_NOTES     = 6;     // max simultaneous chord notes (real bass/guitar
                                                    // chords are few; 16 just admitted harmonic junk)
static constexpr float CHORD_CAND_THR      = 0.12f; // local-max candidate gate (× peak). Low so a
                                                    // fundamental (often quieter than its own 2nd
                                                    // harmonic) still qualifies — but not so low it
                                                    // admits low-bin noise / sympathetic-string ghosts.
static constexpr float CHORD_SUBHARM_REL   = 0.30f; // a candidate is an OVERTONE (dropped) if a
                                                    // sub-multiple below it (−12/−19/−24/−28 semi)
                                                    // carries > this × its energy. Raised from 0.12:
                                                    // a real fundamental is ~0.35–0.7 of its own 2nd
                                                    // harmonic (still collapses), but a FAINT octave
                                                    // below (sympathetic open string, noise) no longer
                                                    // wrongly folds a real note down an octave. Lower
                                                    // = kill octave-up ghosts harder; higher = risk
                                                    // octave-up ghosts return.
static constexpr float CHORD_ABS_FLOOR     = 0.003f;// peak below this = no detection.
                                                    // Absolute (band-energy scale) so it
                                                    // MUST track the guitar signal model
                                                    // above — 0.02 rejected quiet guitar.
// Debug: serial-log each chord snapshot from the MAIN loop (never the audio thread) as
// "CHORD n=… / <note><oct> <±cents>", so we can compare detected vs played. USB-guarded
// (skips when no host) + naturally throttled to the snapshot rate. Flip false to silence.
static constexpr bool  DEBUG_LOG           = true;

// Dispersion allpass chain in the comb loop (K3 = structure).
// First-order allpasses add inharmonicity / stringy detune. K3 0->1 scales the
// coefficient 0 -> DISP_MAX across DISP_STAGES stages.
static constexpr int   DISP_STAGES = 2;
static constexpr float DISP_MAX    = 0.6f;   // STAGE-1 GUESS — max allpass coeff

// Modal bandpass core: DROPPED — comb is the keeper (A/B decided 2026). SW1 is
// now free (reassignment TBD, next increment).

// ---------------------------------------------------------------------------
// Damping (K2) — target decay time, the primary timbre control.
// Wide range on purpose (discovery ethos: long drones welcome).
// ---------------------------------------------------------------------------
static constexpr float T60_MIN_S = 0.08f;   // short, plucky
static constexpr float T60_MAX_S = 30.0f;   // very long drone (near-held). g asymptotes to unity
                                            // (~0.997 here) — longer T60 keeps extending the ring
                                            // but can't reach true-infinite without LOOP_BOOST>1
                                            // (rejected: static/no dynamics). g<1 so it can't run away.

// ---------------------------------------------------------------------------
// Register (K1) — bipolar octave multiplier on resonator tuning.
// Continuous +/- REGISTER_OCT octaves (CCW sub, noon unison, CW upper).
// Stage 1: single shift, NO stacking (bounded voice count). See return notes.
// ---------------------------------------------------------------------------
static constexpr float REGISTER_OCT        = 1.0f;   // (legacy; range now set by the steps)
static constexpr float REGISTER_CENTER_OCT = -1.0f;  // register origin (added to each step)
// K1 is QUANTISED to meaningful register intervals so retuning LOCKS onto octaves
// instead of gliding to arbitrary detuned pitches. Steps in semitones, relative to
// CENTER. Octaves at -12/0/+12 → -2/-1/0 oct (CENTER=-1); the +/-7 steps are fifths.
// Edit the set to taste (e.g. {-12,0,12} for octaves-only).
static constexpr int   REGISTER_STEPS_N = 5;
static constexpr int   REGISTER_STEPS_SEMI[REGISTER_STEPS_N] = { -12, -7, 0, 7, 12 };

// ---------------------------------------------------------------------------
// Legato / portamento (Lost+Found style). When the chord detector snaps a new note set,
// each voice GLIDES from its current pitch toward its new target instead of jumping.
// TRUE FIXED-TIME, LINEAR: at each target change we set a constant per-update step =
// distance / (glide-time in control-ticks), march at that constant velocity, and STOP
// exactly on the target. So every glide takes the same time T regardless of interval AND
// arrives cleanly (no exponential deceleration/creep). K4 sets T directly in ms. Runs in
// RecomputeVoices (control rate); the step is (re)computed in AssignVoices on each snap.
// A fresh voice enters at pitch (step 0). Audio-rate de-click (below) rides underneath.
// ---------------------------------------------------------------------------
static constexpr float GLIDE_TIME_MIN_MS = 8.0f;    // K4 CCW — ~instant (de-clicked by DELAY_SMOOTH)
static constexpr float GLIDE_TIME_MAX_MS = 1200.0f; // K4 CW  — long slide
static constexpr float GLIDE_CTRL_MS     = 10.0f;   // control-update period — MUST match the shell's
                                                    // main-loop DelayMs (sets the glide step count)

// Audio-rate delay smoothing (click suppression, SEPARATE from K4 portamento). The
// comb read delay is retargeted at CONTROL rate; a hard jump (K4 CCW / new chord) makes
// a ringing delay line read at a new length in ONE block → a broadband click that sounds
// like extra excitation. A small per-SAMPLE one-pole on the actual read delay micro-
// glides every retune → click-free regardless of K4. The musical portamento rides on top.
static constexpr float DELAY_SMOOTH_MS = 6.0f;   // ~6 ms; long enough to de-click a hard retune,
                                                 // short enough not to smear a fast musical glide.

// Voice-count level smoothing: the 1/√n normalization steps when the detected note COUNT
// changes at a snapshot → a level pop. Ramp it over a few ms to de-click (K4-independent).
static constexpr float VNORM_SMOOTH_MS = 12.0f;  // level-normalization ramp on count changes

// Per-voice fade in/out: a voice's output gain ramps 0→1 when it's activated and 1→0
// when dropped (keeps ringing while it fades, then retires) — declicks BOTH the "voice
// added" pop and the "voice dropped" cut. Linear, so it arrives/retires cleanly.
static constexpr float FADE_MS = 10.0f;          // voice fade in/out time

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
// Onset detection matches the proven nitrotron3 attack-sync: a fast 4-pole
// EnvFollower + hysteresis crossing on the raw passive-bass env scale
// (~0.02–0.1). A fresh crossing (armed → above ON) retriggers the sweep; the
// gate holds open while ONSET_OFF is exceeded, so the natural ring-out survives
// and the filter only releases near silence. OFF is set low for that reason.
static constexpr float ONSET_ENV_HZ = 80.0f;   // follower cutoff (fast/snappy)
// INPUT SIGNAL-STRENGTH MODEL (recurring gotcha — keep this realistic!). This is
// the fast-env (ONSET_ENV_HZ 4-pole) magnitude of `x`, NOT the raw sample peak.
//   passive bass, firm pluck : ~0.02 - 0.10
//   passive bass, soft       : ~0.008 - 0.02
//   GUITAR, normal picking    : ~0.004 - 0.03   <-- much lower than bass
//   GUITAR, soft / high frets : ~0.0015 - 0.006  <-- easily missed
// Onset thresholds MUST sit well below the softest note you want to catch, or the
// effect simply won't trigger (this bit us on the vestige looper trigger too).
// Err LOW; noise-floor false-triggers are the lesser evil during play.
static constexpr float ONSET_ON     = 0.0025f; // GATE on: env rises past → filter note-on
static constexpr float ONSET_OFF    = 0.0008f; // GATE off: env falls below → filter release

// PEAK / onset detection for the chord snapshot — SEPARATE from the gate above. The
// gate (ON/OFF hysteresis) only opens/closes the K5 filter and needs near-silence to
// re-arm. The chord grab must instead fire on every NEW ATTACK, even while a previous
// chord still rings, so a fresh strum always re-detects. An attack = the fast env
// spiking above a slow baseline (which tracks the current sustained level).
// The onset detector runs on a HIGH-PASSED copy of the input (classic HFC onset
// detection): a pick attack is broadband/HF while the sustained tone + its ripple are
// mostly LOW, so high-passing emphasises the transient AND removes the low-fundamental
// ripple at the source (no more per-cycle retriggering on low notes). A ringing chord
// also has little HF, so a new strum's attack stands out sharply → clean re-strums.
// This is SEPARATE from env_val_ (the full-band env still drives the K5 gate / LED).
static constexpr float ONSET_HP_HZ         = 800.0f;// high-pass cutoff for the onset path (above the
                                                    // highest fundamental we track ~660 Hz, so the
                                                    // fundamental ripple is gone; keeps the attack HF).
static constexpr float ONSET_HP_FLOOR      = 0.001f;// absolute floor on the HP-env (its own smaller
                                                    // scale). Err LOW — the RATIO does the real
                                                    // gating; this only blocks firing on near-silence.
static constexpr float ONSET_RISE_RATIO    = 1.5f;  // HP-env must exceed the baseline by this to count
                                                    // as a new attack. Higher = only strong new hits
                                                    // re-trigger; lower = soft re-plucks do too.
static constexpr float ONSET_FAST_MS       = 15.0f; // envelope smoothing on the RECTIFIED high-passed
                                                    // input (turns |HPF| into a smooth attack env).
static constexpr float ONSET_REF_MS        = 40.0f; // baseline follower time constant. The attack
                                                    // spikes above it; it catches up during sustain.
static constexpr float ONSET_REFRACTORY_MS = 100.0f;// min gap between onsets — one attack's messy
                                                    // rise must not fire the snapshot repeatedly.
// Gate hold: a slow-release peak follower keeps the note-gate OPEN through the
// note's natural decay (esp. guitar, whose fast env dips mid-note) so it isn't
// cut too soon. Instant attack, slow release. Playability stopgap — smarter
// env detection is a later fine-tuning pass.
static constexpr float GATE_HOLD_MS = 700.0f;

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
