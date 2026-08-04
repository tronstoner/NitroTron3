# armitage — as-built architecture (detection, onset, excitation, portamento)

**Read this first when touching armitage's note detection, triggering, or voicing.**
`IMPULSE_SYNTH_SPEC.md` is the original design intent; **this** file is what the code
actually does, discovered by ear + serial-log debugging on hardware. Exact values live
in `pedals/chronotron3/modules/armitage_constants.h` (the source of truth) — this doc
explains the *model* and *why*, and names the constant that tunes each part.

Files: `pedals/chronotron3/modules/armitage.h` (+ `armitage_constants.h`). SW3 DOWN.

## What armitage is

A polyphonic **synthesized-chord** voice: you play a note/chord, it detects the pitches,
and rings a bank of tuned comb resonators driven by your input. The character is a
**driven near-unity comb resonator + feedback-FM "gnarl"** (NOT self-oscillation — that
was tried and rejected as static/dynamics-free). Reference: Chase Bliss Lost+Found
Impulse Synthesizer. See memory `project_armitage_character`.

## Signal chain (audio thread, per sample)

```
in ─► onset env (full-band, env_val_) ──────────────┐  (gate / LED / level)
  │                                                  │
  ├─► chord filterbank (36→48 bins) ─► [snapshot on onset+settle] ─► note set
  │                                                  │
  └─► PRE-conditioning (tanh drive, asym) ─► × excitation-envelope guardrail ─► e
                                                      │
   note set ─► sub-harmonic fundamental filter ─► voice-leading ─► portamento targets
                                                      │
   e ─► comb resonator bank (per-voice: KS comb + feedback-FM) ─► Σ ─► ×COMB_MAKEUP·norm
                                                      │
        ─► POST-loop drive (K4, outside the loop) ─► K5 gated 4-pole LP ─► limiter ─► wet
```

## 1. Chord detection (the hard part)

A chromatic **bandpass filterbank** (`CHORD_N_BINS` bins from `CHORD_BASE_MIDI`, RBJ
biquads, `CHORD_Q`) runs continuously, each bin tracking a per-band energy envelope. On
an onset (below) a **snapshot** is scheduled after `CHORD_SNAP_DELAY_MS` — deliberately
LONG (~180 ms) so we grab the *settled* harmonic tone, not the broadband pick transient.
`ChordDetector::Snapshot()`:

1. **Candidates** = local maxima above `CHORD_CAND_THR × peak` (LOW gate — a real
   fundamental is often quieter than its own 2nd harmonic and must still qualify).
2. **Fundamental filter (sub-harmonic)** — THE key idea. Each plucked string lights up
   its whole harmonic series and the **2nd harmonic (octave) is often LOUDER than the
   fundamental**. So we cannot pick "loudest = note", nor drop overtones *above* an
   accepted note. Instead: a candidate is an **overtone → dropped** if a sub-multiple
   below it (−12/−19/−24/−28 semitones = f/2, f/3, f/4, f/5) carries `> CHORD_SUBHARM_REL`
   of its energy. This collapses each string's series to one fundamental.
3. **Fine pitch** — parabolic sub-bin interpolation on log energies (skipped at the array
   edges, or it pins a bogus ±50 c).

**Range MUST cover the instrument's fundamentals.** `CHORD_BASE_MIDI = 28` (E1, 41 Hz)
reaches bass open strings; the old E2 (82 Hz) floor left E/A/D fundamentals invisible so
the detector latched their harmonics → octave-up garbage. 5-string low B0 = 23 → lower
BASE if needed.

**Tradeoffs / tuning:** `CHORD_SUBHARM_REL` up = keep more (octave-up ghosts risk return);
down = collapse harder (a played octave folds to its root — acceptable, voicing comes from
register not detection). `CHORD_CAND_THR` up = fewer low-bin noise / sympathetic-string
ghosts but risks missing quiet real notes. `CHORD_MAX_NOTES` caps the set (6; 16 admitted
junk). Sympathetic open-string ringing is real energy — no detector can tell it from a
played note; mute unused strings when testing.

## 2. Onset / peak detection — SEPARATE from the gate

Two different things share the input but must not be conflated:

- **Gate** (`ONSET_ON/OFF` hysteresis on the full-band `env_val_`) — only opens/closes the
  **K5 filter**. Needs near-silence to re-arm.
- **Peak/onset detector** — fires the **chord snapshot** on every NEW ATTACK, even while a
  previous chord still rings (so a fresh strum always re-detects). Fires when the attack
  envelope spikes above a slow adaptive baseline (`ONSET_RISE_RATIO`), gated by an absolute
  floor (`ONSET_HP_FLOOR`) and a `ONSET_REFRACTORY_MS` guard.

**The onset detector runs on a HIGH-PASSED copy of the input** (`ONSET_HP_HZ ≈ 800 Hz`,
one-pole; classic high-frequency-content onset detection): a pick attack is broadband/HF
while the sustained tone + its low-fundamental ripple are LOW. High-passing emphasises the
transient AND removes the ripple at the source. `onset_fast_` rectifies + smooths |HPF|
into the attack envelope; `onset_ref_` is its slow baseline.

**Hard-won gotcha (do not regress):** a flux onset detector on a *raw* (full-band)
envelope **retriggers every cycle on a low fundamental** — the 41 Hz ripple keeps exceeding
the ratio. Stacked with the excitation duck (below), that starved the snapshot and made low
notes go **silent**. The HPF fixes it at the source (the earlier `ONSET_FAST_MS` smoothing
was a band-aid, now just the envelope stage). Fingerstyle bass has less HF than a pick — if
soft notes miss, lower `ONSET_HP_FLOOR` or `ONSET_HP_HZ`.

## 3. Excitation-envelope guardrail (feed around chord changes)

The input drives the combs continuously, which BURSTS: a new pluck blasts the OLD/mistuned
chord during the settle window, and keeps driving resonators while they GLIDE. So the feed
`e` is multiplied by an envelope: **DUCK to `EXC_DUCK_LEVEL` on each onset** (fast,
`EXC_DUCK_MS`), then **ATTACK back to 1 once the new chord locks** (`EXC_ATTACK_MS`, ≈ glide
time) at the snapshot. Net: pluck → brief hush → the new chord swells in already on-pitch.
`EXC_DUCK_LEVEL = 1` disables it (continuous feed = pre-guardrail behaviour). Independent of
the K5 gate. Peak level unchanged — it only reshapes the attack envelope.

## 4. Voice-leading + portamento

On each snapshot the note set is assigned to voices by **nearest-note voice-leading**
(`AssignVoices`, greedy, n ≤ 6): each currently-ringing voice glides to the *nearest* new
note (minimal musical movement), not slot-by-index (which leapt arbitrarily). Extra notes
enter at pitch; dropped notes fall silent. Then per voice the pitch **glides fixed-TIME**
(one-pole toward target, `GLIDE_COEFF`) — any interval takes the same time (not fixed rate).

## 5. Resonator, FM, drive (the voice)

- **Comb bank** — extended Karplus-Strong, per-voice `Dtot = fs/f`, two-point-average loop
  filter, loop gain `g` from the T60 relation (`K2`, up to `T60_MAX_S ≈ 30 s`). In-loop tanh
  saturator bounds it; `LOOP_BOOST = 1` = driven (breathes), not self-oscillating.
- **Feedback FM (`K3` = depth)** — each comb self-modulates its own delay-read position by
  its last output → inharmonic sidebands + a period-doubled sub. The modulator is
  **lowpassed with a PER-VOICE cutoff that tracks pitch** (`FM_MOD_TRACK_MULT × fundamental`)
  — a fixed cutoff fizzed highs and starved lows. In-loop drive is locked off; the modulator
  cutoff tracking + depth were mapped with a one-axis-at-a-time exploration pass.
- **Post-loop drive (`K4`)** — asymmetric waveshaper on the resonator OUTPUT, *outside* the
  feedback loop (so it can be pushed hard with zero stability risk — in-loop drive ran away),
  before the K5 filter. `1/√drive` auto level-compensation so K4 adds grit not volume.

## Controls (chord-detect build)

| CONTROL | FUNCTION |
|-|-|
| KNOB 1 | Register — quantised octave/fifth steps (`REGISTER_STEPS_SEMI`) |
| KNOB 2 | Damping / T60 (0.08 s … ~30 s) |
| KNOB 3 | FM depth (gnarl) |
| KNOB 4 | Post-loop drive |
| KNOB 5 | Filter attack/release (bipolar: noon = snappy; CCW = slow attack, CW = long release) |
| KNOB 6 | Dry/wet mix (shell) |
| SWITCH 1 | Free (was the FM exploration-axis selector; retired after tuning) |
| SWITCH 2 | Note-set behaviour (bypassed while `CHORD_DETECT`) |
| SWITCH 3 | Module select (shell) — DOWN = armitage |

## Debug logging

`armitage_k::DEBUG_LOG` logs each snapshot's detected note set (note name + cents) over USB
serial **from the main loop** (`main.cpp`), so you can compare detected vs played. NO
USB-state guard — libDaisy's logger is non-blocking until a terminal syncs. **Turn
`DEBUG_LOG` off for normal use.** Serial on this sealed pedal is one-shot + power-cycle pain
— see memory `reference_daisy_serial`; prefer audio-domain debugging where possible.

## The input signal-strength model (recurring gotcha)

Onset/energy thresholds live on the fast-env scale (see the block in `armitage_constants.h`
and memory `passive-bass-env-scaling`): passive bass ~0.02–0.10, **guitar much lower
(~0.0015–0.03)**. Default thresholds LOW or the effect won't trigger. The HPF onset path has
its own (smaller) scale → `ONSET_HP_FLOOR`, not `ONSET_ON`.
