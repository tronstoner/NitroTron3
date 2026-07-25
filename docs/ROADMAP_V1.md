# Road to v1 — collected ideas, ordered for upcoming increments

Status: idea collection captured 2026-07-21 (post-v0.5), sorted into
increments. Each item is written to be picked up standalone in a later
session. Ear-tuning rules apply as everywhere in this project: implement the
lever, tune by ear, one concern per iteration.

Out of scope here: `docs/ChronoTron3/` (formerly `docs/NEW/`) holds the docs for
the **next pedal** — ChronoTron3, 3 modes, time-based, footswitches dedicated to
tap tempo / looper instead of the preset system. It builds on this platform but
is its own project. Ignore it while working through this roadmap.

---

## 1. Mode C phaser: notch↔bandpass character morph (K2)

**Problem:** K2 (resonance/feedback) was useful when the phaser was
bandpass-based; since the move to notch it does nearly nothing audible.

**Idea:** repurpose K2 as a **character morph** — blend between notch and
bandpass responses (or crossfade the all-pass mix polarity/depth, which is
the same lever seen from the filter topology side). CCW = pure notch
(current), CW = increasingly bandpass/vocal, so the knob truly changes the
filter's character instead of feeding a feedback path that no longer bites.

**Why first:** smallest scope of the collected ideas, self-contained in the
phaser, immediately audible, no new infrastructure.

## 2. Mode A triangle, K5 CCW: dynamics-driven ensemble ("stepped glissando")

**Current:** K5 CCW on triangle = just-intonation ensemble; the chord stack
is a static function of knob position.

**Idea:** make playing **dynamics** determine the spectral content. The
envelope doesn't just scale loudness — it *moves through the ensemble
voices*: soft playing = few/low voices, digging in walks the stack upward,
voice by voice, creating a **stepped glissando** through the just-intonation
intervals as notes bloom and decay. The knob shifts from "how many voices"
to "how far the dynamics can reach / how the env maps onto the voice
ladder" (depth/range lever; curve and per-voice fade behavior to be shaped
by ear).

**Notes:** stepped is the point — voices enter discretely (weird, audible
steps), not as a smooth crossfade wash. Env→step mapping needs hysteresis
per step so a hovering envelope doesn't flutter a voice on/off (loudness
hysteresis is fine — this is not pitch-tracking guardrail territory).

## 3. Shared infrastructure: euclidean rhythm engine + ratio table

Both remaining ideas (4 and 5) quantize event timing onto musically
interesting grids. Build once, use twice:

- **Euclidean pattern generator** E(k, n) with rotation — cheap,
  deterministic.
- **A curated ratio table** of "important" divisions/multipliers: straight
  (÷4 … ×4), **dotted relations**, and selected euclidean densities. Table
  is a compile-time constant list — a preselected menu, not a free
  parameter.
- **Reference-pattern principle** (important): a euclidean rhythm is best
  *perceived* when run against a second, distinguishable pattern — two
  euclidean rhythms with different accents against each other, or one
  against a plain quarter-note base (the quarter base itself being the
  trivial euclidean case). Any musical use below should therefore run TWO
  patterns (or pattern-vs-base), not one in isolation.
- **Tempo sources:** Mode B derives tempo from **buffer length** (the delay
  tempo already determines event tempo); Mode C derives it from **env peaks**
  (playing tempo). Both feed the same divider/multiplier machinery.

## 4. Mode C phaser, K3 CW: euclidean playing-follower (replaces S&H random)

**Problem:** K3 CW (sample-and-hold random LFO) is sub-par — not on the same
functional level as the rest of the modes. (K3 CCW triangle-LFO mode is fine
and stays.)

**Idea:** make the random movement **follow the playing**. Sketch:

- **Tempo** comes from env peaks (note onsets) — the playing sets the clock.
- **K3 CW travel** = tempo divider/multiplier + euclidean **density**
  operator (one knob, two coupled parameters along the travel).
- **Two euclidean rhythms** run against each other, placing **emphasis on
  frequencies**: pattern hits select/step the phaser's center frequency
  through a set of positions, accents distinguishable (e.g. one pattern
  picks the frequency step, the other adds emphasis/depth).
- Result: rhythmically interesting, pattern-cycling movement that stays
  locked to what's being played — "random" in effect but structured in time.

Alternative kept open: incoming note information (tracker pitch) selecting a
pattern to cycle through. The euclidean/env-peak variant is the more
concrete sketch; decide at implementation time.

## 5. Mode B character knob: euclidean/ratio quantization of random events

**Current:** all glitch/grain random events come from a source of
uncertainty or time-based randomness — good and working; unpredictability
must be retained.

**Idea:** keep the randomness, quantize only **when** events land (and the
random **grain lengths**) onto musically meaningful grids:

- Quantize event times onto a euclidean grid and/or snap random durations
  into mathematically interesting divisions/multipliers of a base.
- **Base = buffer length** (the delay tempo already determines the event
  tempo), scaled through the shared ratio table (dotted relations
  explicitly wanted).
- Grid selection from the **preselected ratio table** (item 3), not
  free-running.

**Method requirement (do this first):** build a **deterministic simulation
model** of the current behavior — same RNG, seeded, offline — so current
events vs. quantized events can be generated side by side and observed.
Goal: verify the quantization only moves events in time ("we just quantise
for rhythm") and matches the current randomness statistics as closely as
possible (event counts, length distribution). Only then port to the pedal.

## 6. PLL mode (discovery) — possibly a "hidden" mode

**Idea:** a CD4046-style **phase-locked loop** voice — the classic DIY
guitar-synth glitch machine: a square oscillator chasing the input's pitch
through an analog-style loop filter, with all the musically productive
misbehavior that implies (lock glitches, octave mis-locks, overshoot swoops,
losing lock on decays). Distinct from the existing tracker-driven synth:
the PLL *is* the tracker, and its instability is the sound.

**Placement:** undecided — somewhere in Mode A or Mode C. Current favorite:
a **hidden mode** at an extreme of the synth voice, e.g. Mode C SW1=DOWN
(synth osc) square side, **K4 fully CW** — the last bit of travel tips from
max PWM into PLL behavior. Alternatively Mode A square. Decide during
discovery.

**Base reference:** Parasit Studio **Ray Gun Youth** (Fredrik Lyxzén) —
<https://parasitstudio.com/pedals/raygunyouth/> — a CD4046 PLL pedal Ralf has
built; one of its selectable modes produces exactly the intended glitches.
Start the discovery from its schematic/mode structure. (Parasit Studio's
Eagle Claw already inspired the Mode C XOR octave-fuzz — see README
acknowledgments.)

**Discovery questions:** loop filter behavior at audio rate (capture range,
lock time as ear-levers), divider ratios for octave up/down/fifths, whether
the comparator input needs the tracker's conditioned signal or raw input,
and how "hidden" the entry should be (dead-zone plateau before it engages?).
Note: PLL lock dynamics are feedback behavior, not detection-layer
smoothing — the no-slew/no-fades boundary doesn't apply to the loop itself;
its wobble is the point.

---

## Suggested increment order

| # | Item | Scope | Depends on |
|-|-|-|-|
| 1 | Phaser K2 notch↔bandpass morph | small | — |
| 2 | Mode A triangle dynamic ensemble | medium | — |
| 3 | Euclidean engine + ratio table | small, no audio | — |
| 4 | Phaser K3 CW euclidean follower | medium | 3 |
| 5 | Mode B quantized randomness | large (needs offline sim first) | 3 |
| 6 | PLL mode discovery | discovery doc first, placement open | — |

1 and 2 are independent quick wins in either order. 3 is pure scaffolding
and can land silently. 4 before 5: it exercises the euclidean engine with a
simpler consumer before the Mode B work, and 5 additionally needs the
simulation tooling.
