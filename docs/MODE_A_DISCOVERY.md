# Mode A — Bordun — Discovery Notes

Status: working notes for the K5 / K4 / octave-tracking rework on
`feature/synth-tracking-improvements` (commits `8ef8578`, `9285e68`, `b65c7ce`,
`f1daac4`). The formal spec `docs/MODE_A_DRONE.md` and the README/manual are
**not** yet updated to match — do that as a deliberate pass once these changes
settle by ear. This file records the design decisions and tuning levers so the
eventual doc update has a source of truth.

The original Mode A concept (FreqBox-inspired: env follower → VCA → oscillator →
ladder → mix) is unchanged. What changed this session: what K5 does per waveform,
K4 becoming a bipolar low-pass/high-pass filter, and the octave handling of the
pitch-tracking sub-modes.

---

## K5 — retired the second oscillator, now bipolar per-waveform

**Before:** K5 was a second oscillator (`osc2`) at a tunable semitone interval,
mixed 50/50, muted in a noon dead-zone. Judged not useful in practice, so the
whole interval/harmony feature was dropped. **Any saved Mode A preset's K5 value
now means ensemble/PWM/FM, not a semitone interval.**

**After:** K5 is bipolar (matching the K2/K3 idiom — center detent, ± meaning).
The CW half is always audio-rate FM; the CCW half depends on the SW1 waveform.

| Waveform | K5 CCW | K5 noon (± dead-zone) | K5 CW |
|---|---|---|---|
| **Saw** | Detuned unison cloud (Mode C hypersaw staging) | Single clean saw | FM |
| **Triangle** | Just-intonation ensemble stack (see below) | Single clean triangle | FM |
| **Square** | PWM (duty sweep, Mode C rect behaviour) | Clean 50% square | FM |

Engine: `src/synth_osc_a.h` (`DroneOsc`), instantiated once, replacing `osc1`/`osc2`.
It owns a 7-voice `MoogOsc` bank + the FM modulator conditioning + the PWM LFO +
the ensemble gating. Pitch/VCA/ladder/HPF/mix stay at the call site
(`ProcessDrone`), as before.

### Saw — unison cloud
Reuses the Mode C hypersaw approach with its own, gentler constants (a bass drone
wants less spread than a Mode C lead). Voices fade in by pair (innermost → middle
→ outermost) along the CCW travel, then detune widens; RMS-normalized so
perceived level stays flat as pairs enter. Voices boot phase-decorrelated
(irrational stagger) to avoid slow flange. FM (CW) applies to the center voice.

### Triangle — Haible-style just-intonation ensemble
Triangle CCW is **not** the unison cloud. The 7 voices become a just-intonation
ensemble (the "Second just ratio scale", Haible ensemble osc): `MODE_A_HARM_RATIO`
= **1:1, 5:4, 4:3, 3:2, 5:3, 7:4, 2:1** within one octave (root, maj3, 4th, 5th,
maj6, harmonic-7th, octave). Root always on; the upper chord tones **gate in**
(stepped, not a slow fade) at evenly-spaced knob thresholds as you sweep CCW, each
switch slewed ~3 ms so it's near-instant but click-free. Amplitude = `1/ratio^ROLLOFF`
(gentle rolloff up the stack), RMS-normalized. The whole series is shifted **up
one octave** (`MODE_A_HARM_OCTAVE`). No FM on the CCW/ensemble side — FM there
buried the chord; FM is CW-only for triangle.

History of this slot (all by ear in one session): integer harmonic series →
just chord (down-voiced, "sinks below") → open ascending across octaves →
circle-of-fifths quintal stack → landed on the one-octave "Second just ratio
scale." The `MODE_A_HARM_RATIO[]` table is the single lever to try other scales.

### Square — PWM
Reuses the **exact** Mode C rect constants (`MODE_C_SYNTH_PWM_*`) so it feels
identical to Mode C's PWM: depth ramps in over the first fraction of travel, then
the LFO rate speeds up. Triangle LFO on the duty cycle, PolyBLEP-corrected pulse.
At noon the pulse is a clean 50% square at the same `OSC_SQR_GAIN` level as the
old square — continuity preserved. FM (CW) applies to the pulse.

---

## FM engine — the part that took iterating

Goal: reintroduce the FreqBox's "input frequency-modulates the oscillator"
without glitching on loud input, and with a modulator rounded toward a sine.

**Modulator conditioning** (built into `DroneOsc::Process`, from the dry input):
1. **Fundamental-isolation LP** (2-pole, `MODE_A_FM_LP_HZ` = 200 Hz) — rounds the
   harmonically-rich bass toward a near-sine. Kept independent of the pitch
   tracker so FM works in all SW2 sub-modes.
2. **Partial normalization** — `divisor = FLOOR + NORM·env`. Full AGC (÷env)
   flattened dynamics; partial normalization lets FM intensity **grow with
   playing level** (loud = more FM). `MODE_A_FM_NORM` = 0 → level-tracking,
   1 → constant.
3. **tanh soft-clip** (`MODE_A_FM_DRIVE`) — bounds the modulator and adds grit
   when slammed.
4. **DC block** (`MODE_A_FM_DC_HZ` = 8 Hz) — **the key to stable pitch.** See below.

**FM law: linear through-zero.** `freq = f0·(1 + depth·mod)`.

- Started with **exponential** FM (`f0·2^(depth·mod)`) mislabelled as
  "through-zero safe." Wrong: exponential FM *sharpens the pitch* as depth rises
  (2^x is convex, so even a symmetric modulator raises the mean frequency). The
  pitch-stable form is **linear** FM — with a zero-mean modulator the ±Hz swings
  average back to f0.
- Linear FM is only pitch-stable if the modulator is truly zero-mean. The LP
  passes DC and tanh of an asymmetric bass fundamental adds bias → pitch drifted.
  The **DC block on the modulator** fixes it; this was the actual bug behind
  "pitch is very unstable."
- "Through-zero": at `depth > 1` the multiplier goes negative and the oscillator
  phase runs **backward** through zero rather than rectifying — clangorous but in
  tune, and no rectification artifacts. This is how we get violent FM without
  glitching. `MODE_A_FM_DEPTH_MAX` = 3.0 (±300% swing).

**Depth curve.** FM depth is power-curved along the CW travel:
`depth = MAX · travel^MODE_A_FM_DEPTH_CURVE` (3.0). Linear ramp cramped the
subtle amounts near noon; the cube puts most of the knob in the low-FM range and
compresses the violent end into the last stretch (full CW still hits the max).

**Oscillator support:** `MoogOsc` gained bidirectional phase wrap and abs-width
PolyBLEP so it survives negative frequencies. No-op on the normal freq > 0 path,
so Mode C's saw voices are unaffected.

**Which voices get FM:** CW half only. Saw → center voice; square → the pulse;
triangle → the single fundamental (the ensemble is CCW, so no FM there). An
earlier iteration FM'd the whole triangle ensemble on CCW but it buried the
chord, so FM was pulled to CW-only.

---

## K4 — bipolar low-pass (CCW) / high-pass (CW) filter

K4 was a plain low-pass sweep; it's now bipolar (saw/square). Serial chain is
`ladder (LP) → ModeAHpf (HPF)` for all waveforms — parameters differ per side, no
per-path filter instances (`src/mode_a_hpf.h`).

- **noon → CCW (low-pass):** ladder cutoff sweeps `MODE_A_LP_MAX_HZ` (8 kHz, open,
  at noon) → `MODE_A_LP_FLOOR_HZ` (250 Hz, full CCW). The floor is raised well
  above the old 80 Hz — the fully-closed bottom quarter was never used. Ladder
  **drive** ramps `LADDER_DRIVE` (1.8) → `MODE_A_LADDER_DRIVE_CCW_MAX` (8.0) as it
  closes, so dark settings read fat/saturated rather than muffled.
- **noon:** neutral — ladder wide open, HPF transparent.
- **noon → CW (high-pass):** ladder held wide open; a 2-pole HPF fades in
  `MODE_A_HPF_MIN_HZ` (20 Hz, transparent) → `MODE_A_HPF_MAX_HZ` (2 kHz, full CW),
  thinning the low end. No drive/loudness compensation on this side. Click-free
  via continuity at noon (both filters transparent there) + per-block HPF cutoff
  smoothing (`MODE_A_HPF_SMOOTH`).
- **Triangle** keeps its own path (CCW ladder LP sweep + CW wavefold, drive ramp
  on CCW). Its HPF stays at `MODE_A_HPF_MIN_HZ` (below bass range → transparent),
  which is why the shared serial LP→HPF chain costs triangle nothing.

---

## Octave-locked / direct tracking — pitch register fixes

Tuning reference (by ear): **fixed** mode (SW2 UP), K1/K2 noon, open A →
**A3 (220 Hz)** is correct. The two tracking modes were made to match.

- **Direct (SW2 DOWN):** octave offset `Quantize(K2,7) - 1` so K2 noon = the
  played octave, landing A3 for a played A (was two octaves off after earlier
  passes).
- **Octave-locked (SW2 MID):** folds the continuous pitch into K2's octave.
  - `TRACKING_OCTAVE_SHIFT` = 0 (lands A3 at noon, matching fixed).
  - **Fold boundary at G# (`TRACKING_FOLD_NOTE` = 8), one semitone below A.** A is
    a note you actually play; with the boundary *on* A, a slightly-flat A folds to
    the top of the octave and jumps up. Referencing the fold to G# keeps a played
    A a semitone inside the octave (folds to +1) so it's stable, while still
    landing A → A3. `TRACKING_WRAP_NOTE` (9 = A) stays as fixed mode's base-A
    reference, unaffected.
  - **Hysteresis removed** (`PITCH_FOLD_HYSTERESIS_SEMI` = 0): the fold is now
    stateless, so the octave is a pure function of the played pitch and doesn't
    depend on which direction you approached the boundary. (The dead-band had
    made octave placement direction-dependent and confused octave testing.)

`drone_fold_k` in the octave-locked branch is now effectively a stateless modulo
(hysteresis = 0); it could be simplified to a plain fold for clarity later.

---

## Constants added / changed (`src/constants.h`)

```
// Saw unison cloud (Mode A copy of the Mode C hypersaw staging, gentler)
MODE_A_UNISON_VOICES = 7
MODE_A_UNISON_DETUNE_CENTS_MIN = 6      MODE_A_UNISON_DETUNE_CENTS_MAX = 22
MODE_A_UNISON_V3_END = 0.30  V5_END = 0.60  V7_END = 0.85
MODE_A_UNISON_SPREAD[7] = {-1, -0.5, -0.234, 0, 0.234, 0.5, 1}
MODE_A_K5_DEADZONE = 0.04               // ± noon = single clean osc

// Triangle just-intonation ensemble (K5 CCW)
MODE_A_HARM_RATIO[7] = {1, 5/4, 4/3, 3/2, 5/3, 7/4, 2}   // Second just ratio scale
MODE_A_HARM_ROLLOFF = 1.0               // amp = 1/ratio^ROLLOFF (0 = equal)
MODE_A_HARM_GATE_MS = 3.0               // stepped voice on/off slew (click-free)
MODE_A_HARM_OCTAVE  = 1.0               // whole series shifted up N octaves

// FM
MODE_A_FM_LP_HZ = 200                    // fundamental-round LP
MODE_A_FM_DRIVE = 1.5                    // tanh pre-gain
MODE_A_FM_DEPTH_MAX = 3.0                // ±300% swing (through-zero)
MODE_A_FM_DEPTH_CURVE = 3.0              // depth = MAX·travel^curve (fine near noon)
MODE_A_FM_NORM = 0.35                    // 0 = level-tracking, 1 = full AGC
MODE_A_FM_FLOOR = 0.05                   // quiet-end modulator scale
MODE_A_FM_DC_HZ = 8.0                    // modulator DC-block → stable pitch

// K4 bipolar filter
MODE_A_LP_FLOOR_HZ = 250   MODE_A_LP_MAX_HZ = 8000      // ladder LP (CCW)
MODE_A_HPF_MIN_HZ = 20     MODE_A_HPF_MAX_HZ = 2000     // HPF (CW)
MODE_A_HPF_SMOOTH = 0.25                                // per-block cutoff slew
MODE_A_LADDER_DRIVE_CCW_MAX = 8.0        // K4 full-CCW drive (from LADDER_DRIVE=1.8)

// Pitch tracking octave/fold
TRACKING_WRAP_NOTE = 9     // A — fixed-mode base reference
TRACKING_FOLD_NOTE = 8     // G# — octave-locked fold boundary (below A)
TRACKING_OCTAVE_SHIFT = 0  // octave-locked register (A3 at noon)
PITCH_FOLD_HYSTERESIS_SEMI = 0.0         // stateless fold
```

Square PWM reuses `MODE_C_SYNTH_PWM_*` (no new PWM constants). New file
`src/mode_a_hpf.h` (`ModeAHpf`).

---

## Open questions / possible follow-ups (not started)

- **Triangle noon/CW are now an octave above saw/square** (the ensemble octave
  shift moves the whole triangle path). Intended for the "high ensemble" voice,
  but if noon should re-align with the other waveforms, scope the octave shift to
  the CCW ensemble voices only (with a smooth handoff at the dead-zone edge).
- **FM LP is fixed at 200 Hz** — over-rounds high notes, under-rounds low ones.
  Could track the fundamental, but that would couple FM to the pitch tracker
  (only some SW2 modes). Left fixed on purpose; revisit if it sounds wrong.
- **Ensemble ratio scale** is one lever (`MODE_A_HARM_RATIO[]`) — other just
  scales / octave-wrapped voicings are trivial to try.
- **FLASH at 97.4%** after this work — headroom is getting tight; watch before
  adding more.
- **Octave-locked fold boundary at G#** trades the A-straddle for a G#-straddle;
  the wrap point on any played note is inherently ambiguous. The longer-term
  option is relocating/dropping the wrap point entirely (see the parked
  wrap-point discussion).
- **Docs:** `MODE_A_DRONE.md` controls table, README control tables, and the
  manual + K5 SVG label still describe the old osc2-interval K5 and the old K4
  low-pass-only tone knob. Update as a deliberate pass once the sound is locked.
