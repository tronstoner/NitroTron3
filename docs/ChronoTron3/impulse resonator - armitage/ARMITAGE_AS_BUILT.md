# armitage — as-built architecture (detection, onset, excitation, voicing, portamento)

**Read this first when touching armitage's note detection, triggering, or voicing.**
`IMPULSE_SYNTH_SPEC.md` is the original design intent; **this** file is what the code
actually does, discovered by ear + serial-log debugging on hardware (bass & guitar,
one instrument-agnostic build). Exact values live in `armitage_constants.h` (the source
of truth) — this doc explains the *model* and *why*, and names the constant per part.

Files: `pedals/chronotron3/modules/armitage.h` (+ `armitage_constants.h`). SW3 DOWN.
**ChronoTron3 is instrument-agnostic — one firmware for bass AND guitar. Never add an
instrument #ifdef or per-instrument constants** (see memory `chronotron3-instrument-agnostic`).

## What armitage is

A polyphonic **synthesized-chord** voice: you play a note/chord, an onset-triggered
detector estimates the pitches, and a bank of tuned comb resonators — **driven by your
input signal** — rings them. Character = **driven near-unity comb + feedback-FM "gnarl"**
(NOT self-oscillation — tried, rejected as static/dynamics-free). Reference: Chase Bliss
Lost+Found Impulse Synthesizer. See memory `project_armitage_character`.

Key architectural fact that shapes everything: **the resonators only ring at frequencies
the input actually contains** (they're excited by the played signal). This is why you
cannot cheaply "add" chord tones as extra resonators — see §4.

## Signal chain (audio thread, per sample)

```
in ─► onset env (full-band, env_val_)  ─────────────► K5 gate (open/close) + LED
  │
  ├─► HPF ─► onset/attack detector ─► schedules the chord SNAPSHOT
  │
  ├─► chord filterbank (48 bins) ─► [blank attack, average settled window] ─► SNAPSHOT
  │        └► peak-pick + fundamental filter ─► voice-leading ─► glide targets
  │
  └─► PRE-conditioning (tanh drive, asym) ─► × excitation guardrail (duck/attack) ─► e
                                                    │
   e ─► comb bank (per voice: KS comb + feedback-FM, delay smoothed) × fade gain
        ─► Σ ─► ×COMB_MAKEUP·voice_norm(smoothed) ─► POST-drive (fixed) ─► K5 4-pole LP
        ─► limiter ─► wet
```

Controls today: **K1 register · K2 T60 · K3 FM depth · K4 portamento · K5 filter A/R ·
K6 mix.** (K4 was post-drive during development; the post-drive is now locked at its old
K4-noon value and K4 is the glide-time control.)

## 1. Chord detection

A chromatic **bandpass filterbank** (`CHORD_N_BINS = 48` from `CHORD_BASE_MIDI = 28` = E1,
RBJ biquads, `CHORD_Q`) runs continuously. On an onset (§2) the detector:

1. **Blanks the attack** for `CHORD_ATTACK_BLANK_MS` (~70 ms) — the pick transient is
   broadband noise with no pitch info, so it is never measured.
2. **Averages** each band's rectified output over the settled window (blank → snapshot).
   NOT an instant-attack peak-hold (that latched the attack and biased toward the loudest
   transient — it made chords "dull / 1–2 notes"). A clean sustained-level average.
3. At `CHORD_SNAP_DELAY_MS` (~180 ms) takes the **snapshot** and peak-picks:
   - **Candidates** = local maxima above `CHORD_CAND_THR × peak` (low gate — a fundamental
     is often quieter than its own 2nd harmonic and must still qualify).
   - **Fundamental filter (sub-harmonic).** Each plucked string lights up its whole
     harmonic series and the octave (2nd harmonic) is often LOUDER than the fundamental —
     so "loudest = note" and "drop overtones above" both fail. Instead a candidate is a
     **harmonic → dropped** if a sub-multiple below it carries `> CHORD_SUBHARM_REL` of its
     energy. **Only −19 (12th) and −28 (2 oct + major 3rd) are collapsed; octaves (−12) and
     2-octaves (−24) are deliberately KEPT** so octave-doubled chord voicings survive
     (dropping octaves left only ~3 distinct pitches = thin). A bass single note therefore
     also rings its octave — consonant fatness, fine post range-fix.
   - **Fine pitch** — parabolic sub-bin interpolation on log energies (skipped at array
     edges or it pins a bogus ±50 c).

**Range MUST reach the instrument's fundamentals.** E1 base covers bass open strings; the
old E2 floor left E/A/D invisible → the detector latched their harmonics → octave-up
garbage. 5-string low B0 = 23 → lower `CHORD_BASE_MIDI` if needed.

**Tuning:** `CHORD_SUBHARM_REL` up = keep more (octave-up ghosts risk return), down =
collapse harder. `CHORD_CAND_THR` up = fewer low-bin / sympathetic-string ghosts, but
risks missing quiet notes. `CHORD_ATTACK_BLANK_MS` longer = more attack dropped.
`CHORD_MAX_NOTES` caps the set (6). Sympathetic open-string ring is *real* energy — no
detector can tell it from a played note; mute unused strings when testing.

## 2. Onset / peak detection — SEPARATE from the gate

- **Gate** (`ONSET_ON/OFF` hysteresis on full-band `env_val_`) — only opens/closes the K5
  filter; needs near-silence to re-arm.
- **Peak/onset detector** — fires the **snapshot** on every NEW ATTACK, even mid-ring, so
  a fresh strum always re-detects. Fires when the attack envelope spikes above a slow
  adaptive baseline (`ONSET_RISE_RATIO`), gated by `ONSET_HP_FLOOR` + `ONSET_REFRACTORY_MS`.

**Runs on a HIGH-PASSED copy of the input** (`ONSET_HP_HZ ≈ 800 Hz`; classic HFC onset
detection): a pick attack is broadband/HF, the sustained tone + its low-fundamental ripple
are LOW — high-passing emphasises transients AND removes the ripple. `onset_fast_`
rectifies + smooths |HPF|; `onset_ref_` is its slow baseline.

**Hard-won gotcha (do not regress):** a flux detector on the *raw* full-band envelope
retriggers every cycle on a low fundamental (the ~41 Hz ripple keeps crossing the ratio);
stacked with the excitation duck that starved the snapshot and made low notes go SILENT.
The HPF fixes it at the source. Fingerstyle bass has less HF than a pick — if soft notes
miss, lower `ONSET_HP_FLOOR` or `ONSET_HP_HZ`.

## 3. Excitation-envelope guardrail

The input drives the combs continuously, which bursts around chord changes: a new pluck
blasts the OLD/mistuned chord during the settle, and keeps driving voices while they glide.
So the feed `e` is × an envelope: **DUCK to `EXC_DUCK_LEVEL` on each onset** (`EXC_DUCK_MS`,
fast), then **ATTACK back once the chord locks** (`EXC_ATTACK_MS`) at the snapshot. Net:
pluck → brief hush → the chord swells in on-pitch. `EXC_DUCK_LEVEL = 1` disables it. Peak
level unchanged — only the attack envelope is reshaped.

## 4. Voicing — currently UNISON (and a documented dead end)

**Each detected note = ONE resonator at that pitch (unison). The synth adds no intervals.**
Density comes only from what you actually play (or the chord the detector catches).

**Auto-voicing was tried and REVERTED — dead end, do not retry as-is.** Adding resonators
at intervals (octaves / fifths / stacked-fifths per detected note, on K1) sounded *weaker*,
not denser, because **an added resonator has nothing exciting it** — the input has no real
energy at that pitch, so it sits thin/starved. Confirmed by ear: playing a stacked-fifth
chord (A2·E3·B3) sounds like L+F, but auto-generating the same stack does not.
Also `1/√n` normalization thins the core as voices are added, and dense stacks hit the CPU
ceiling (~3 voices/note max).

→ **The L+F density is pitch-shifting, not resonator-voicing.** See §"Directions parked".

## 5. Voice-leading, portamento, and declicking

- **Voice-leading** (`AssignVoices`, greedy, n ≤ MAX): on each snapshot every ringing voice
  is reassigned to the **nearest** new note (minimal musical movement), not slot-by-index.
  Extra notes are new voices; fewer notes drop voices.
- **Portamento — true LINEAR fixed-time on K4.** At each retarget a constant per-update step
  = distance / (glide-time in control ticks) is set; the voice marches at constant velocity
  and STOPS exactly on the target. Every glide takes the same time regardless of interval and
  *arrives* cleanly (no exponential creep). K4 = `GLIDE_TIME_MIN_MS … GLIDE_TIME_MAX_MS`.
  `GLIDE_CTRL_MS` MUST match the shell's main-loop `DelayMs` (converts ms → step count).
- **Audio-rate delay smoothing** (`DELAY_SMOOTH_MS`, per-sample one-pole on the read delay):
  a control-rate `dfrac` jump would step a ringing line → broadband click. This micro-glides
  every retune → click-free regardless of K4. (Portamento rides on top.)
- **Per-voice fade in/out** (`FADE_MS`) + **smoothed `voice_norm`** (`VNORM_SMOOTH_MS`):
  declick voice-count changes. A dropped voice fades out (keeps ringing, then retires +
  `Silence()`s its buffer clean for reuse); an added voice fades in. The `1/√n` level ramps
  instead of stepping. Both directions click-free.

## 6. Resonator, FM, drive (the voice)

- **Comb bank** — extended Karplus-Strong, per-voice `Dtot = fs/f`, two-point-average loop
  filter, loop gain `g` from the T60 relation (`K2`, up to `T60_MAX_S ≈ 30 s`). In-loop tanh
  saturator bounds it; `LOOP_BOOST = 1` = driven (breathes), not self-oscillating.
- **Feedback FM (`K3` = depth)** — each comb self-modulates its own delay-read position →
  inharmonic sidebands + a period-doubled sub. Modulator is lowpassed with a **per-voice
  cutoff that tracks pitch** (`FM_MOD_TRACK_MULT × fundamental`; ~1.0 = on the fundamental)
  — a fixed cutoff fizzed highs / starved lows. In-loop drive locked off.
- **Post-loop drive** — asymmetric waveshaper on the resonator OUTPUT, *outside* the loop
  (push hard, zero stability risk — in-loop drive ran away), before K5, with `1/√drive`
  auto level-comp. **Locked at its former K4-noon value** (K4 is portamento now); revive as
  a control by re-mapping a free knob if wanted.

## Controls (chord-detect build)

| CONTROL | FUNCTION |
|-|-|
| KNOB 1 | Register — quantised octave/fifth steps (`REGISTER_STEPS_SEMI`, −1 oct centre) |
| KNOB 2 | Damping / T60 (0.08 s … ~30 s) |
| KNOB 3 | FM depth (gnarl) |
| KNOB 4 | **Portamento — glide time** (fixed-time, `GLIDE_TIME_*`); CCW ≈ instant, CW = long slide |
| KNOB 5 | Filter attack/release (bipolar: noon = snappy; CCW = slow attack, CW = long release) |
| KNOB 6 | Dry/wet mix (shell) |
| SWITCH 1 | Free |
| SWITCH 2 | Note-set behaviour (bypassed while `CHORD_DETECT`) |
| SWITCH 3 | Module select (shell) — DOWN = armitage |

## Debug logging

`armitage_k::DEBUG_LOG` logs each snapshot's note set (name + cents) over USB serial **from
the main loop** (`main.cpp`) — compare detected vs played. NO USB-state guard (libDaisy's
logger is non-blocking until a terminal syncs). **Turn `DEBUG_LOG` off for normal use.**
Serial on this sealed pedal is one-shot + power-cycle pain — see memory `reference_daisy_serial`;
prefer audio-domain debugging where possible. (`cat -u`/`screen` on the `usbmodem` port.)

## Directions tried / parked (for future increments)

- **Self-oscillation** (`LOOP_BOOST > 1`) — rejected: sustains forever but static, no
  dynamics. The keeper is a *driven* near-unity comb + FM gnarl.
- **Auto-voicing / interval stacks on K1** — rejected (see §4): added resonators aren't
  excited → thin; CPU-bound; `1/√n` thins the core.
- **Density via ensemble/chorus** — considered, not the ask; the target is harmonic content,
  not detuned unison.
- **PITCH-SHIFTING for density (the real L+F path)** — a polyphonic interval generator
  (POG/HOG-style) makes the fifth/octave as *pitch-shifted copies of the input*, which carry
  real harmonics, instead of unexcited resonators. Earmarked as a **separate module**, not
  bolted onto the resonator. Notes for that effort: fifths + octave only has advantages
  (cheaper, cleaner); shifting **only the exciter path** may swallow/soften shift artifacts;
  combining with FM feedback may open new territory. Project already has POG groundwork
  (see memory `project_overdrive_parked` — Mode C ERB-PS2 filterbank POG).
