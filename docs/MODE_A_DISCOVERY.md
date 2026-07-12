# Mode A — Bordun — Discovery Notes

Status: working notes for the K5 / K4 rework on `feature/synth-tracking-improvements`
(commits `8ef8578`, `9285e68`). The formal spec `docs/MODE_A_DRONE.md` and the
README/manual are **not** yet updated to match — do that as a deliberate pass
once these changes settle by ear. This file records the design decisions and
tuning levers so the eventual doc update has a source of truth.

The original Mode A concept (FreqBox-inspired: env follower → VCA → oscillator →
ladder → mix) is unchanged. What changed this session is what K5 does and how K4
drives the ladder.

---

## K5 — retired the second oscillator, now bipolar per-waveform

**Before:** K5 was a second oscillator (`osc2`) at a tunable semitone interval,
mixed 50/50, muted in a noon dead-zone. Judged not useful in practice, so the
whole interval/harmony feature was dropped. **Any saved Mode A preset's K5 value
now means cloud/PWM/FM, not a semitone interval.**

**After:** K5 is bipolar (matching the K2/K3 idiom — center detent, ± meaning).
The CW half is always audio-rate FM; the CCW half depends on the SW1 waveform.

| Waveform | K5 CCW | K5 noon (± dead-zone) | K5 CW |
|---|---|---|---|
| **Saw** | Detuned unison cloud (Mode C hypersaw staging) | Single clean saw | FM (center osc) |
| **Triangle** | FM'd unison cloud — cloud thickens **and** FM deepens together, FM on every voice | Single clean triangle | FM (single osc) |
| **Square** | PWM (duty sweep, Mode C rect behaviour) | Clean 50% square | FM (single osc) |

Engine: `src/synth_osc_a.h` (`DroneOsc`), instantiated once, replacing `osc1`/`osc2`.
It owns a 7-voice `MoogOsc` bank + the FM modulator conditioning + the PWM LFO.
Pitch/VCA/ladder/mix stay at the call site (`ProcessDrone`), as before.

### Unison cloud
Reuses the Mode C hypersaw approach but with its own, gentler constants (a bass
drone wants less spread than a Mode C lead). Voices fade in by pair (innermost →
middle → outermost) along the CCW travel, then detune widens; RMS-normalized so
perceived level stays flat as pairs enter. Voices boot phase-decorrelated
(irrational stagger) to avoid slow flange.

### Square PWM
Reuses the **exact** Mode C rect constants (`MODE_C_SYNTH_PWM_*`) so it feels
identical to Mode C's PWM: depth ramps in over the first fraction of travel, then
the LFO rate speeds up. Triangle LFO on the duty cycle, PolyBLEP-corrected pulse.
At noon the pulse is a clean 50% square at the same `OSC_SQR_GAIN` level as the
old square — continuity preserved.

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

**Oscillator support:** `MoogOsc` gained bidirectional phase wrap and abs-width
PolyBLEP so it survives negative frequencies. No-op on the normal freq > 0 path,
so Mode C's saw voices are unaffected.

**Triangle-cloud FM:** for triangle only (`fm_all`), FM multiplies every voice's
frequency, and FM depth ramps with the CCW cloud travel (so cloud + FM deepen
together). Saw FM's only the center voice; square FM's the pulse. The CW half is
single-osc FM for all three (cloud collapsed / PWM off).

---

## K4 — ladder drive ramps up toward CCW

K4's cutoff sweep is unchanged (`MapCutoff`: 80 Hz full-CCW → 800 Hz noon →
8 kHz full-CW). What's new: at noon the tone felt "already very dampening," and
the request was to *drive* the filter harder as it closes. So `LADDER_DRIVE`
(1.8) now ramps up across the CCW half to `MODE_A_LADDER_DRIVE_CCW_MAX` (8.0) at
full CCW — closed settings read as fat/saturated (tanh warmth + bite) rather than
just muffled. Noon and the whole CW half are unchanged. Keyed on raw K4, so it
applies for all waveforms (CCW = dark for all).

---

## Constants added / changed (`src/constants.h`)

```
// Unison cloud (Mode A copy of the Mode C hypersaw staging, gentler)
MODE_A_UNISON_VOICES = 7
MODE_A_UNISON_DETUNE_CENTS_MIN = 6      MODE_A_UNISON_DETUNE_CENTS_MAX = 22
MODE_A_UNISON_V3_END = 0.30  V5_END = 0.60  V7_END = 0.85
MODE_A_UNISON_SPREAD[7] = {-1, -0.5, -0.234, 0, 0.234, 0.5, 1}
MODE_A_K5_DEADZONE = 0.04               // ± noon = single clean osc

// FM
MODE_A_FM_LP_HZ = 200                    // fundamental-round LP
MODE_A_FM_DRIVE = 1.5                    // tanh pre-gain
MODE_A_FM_DEPTH_MAX = 3.0                // ±300% swing (through-zero)
MODE_A_FM_NORM = 0.35                    // 0 = level-tracking, 1 = full AGC
MODE_A_FM_FLOOR = 0.05                   // quiet-end modulator scale
MODE_A_FM_DC_HZ = 8.0                    // modulator DC-block → stable pitch

// Ladder
MODE_A_LADDER_DRIVE_CCW_MAX = 8.0        // K4 full-CCW drive (ramps from LADDER_DRIVE=1.8 at noon)
```

Square PWM reuses `MODE_C_SYNTH_PWM_*` (no new PWM constants).

---

## Open questions / possible follow-ups (not started)

- **Triangle full-CCW may be too chaotic.** It stacks max through-zero FM on 7
  detuned voices. If the CCW sweep isn't usable throughout, split out a separate,
  gentler depth ceiling for the triangle cloud instead of reusing
  `MODE_A_FM_DEPTH_MAX`.
- **FM LP is fixed at 200 Hz** — over-rounds high notes, under-rounds low ones.
  Could track the fundamental, but that would couple FM to the pitch tracker
  (only some SW2 modes). Left fixed on purpose; revisit if the modulator sounds
  wrong across the range.
- **FLASH at 97.0%** after this work — watch headroom before adding more.
- **Docs:** `MODE_A_DRONE.md` controls table, README control tables, and the
  manual + K5 SVG label all still describe the old osc2-interval K5. Update as a
  deliberate pass once the sound is locked.
