# vestige — Onward-influenced rework plan

Status: **plan, not yet implemented.** Supersedes the control concept in
`vestige-rework-plan.md` (whose §2.0 stays the as-built reference for the
multiband granular freeze — that engine is kept, only its *addressing* changes).

Companion docs: `dynamic-looper-concept.md` (original spec),
`DESIGN_DECISIONS.md` (bundle-wide hard rules), `mnemonic-concept.md` (the
freeze the builder considers correct).

---

## 1. Why

vestige was influenced by the Chase Bliss Onward from the start. Two things
about the current build are unsatisfying, and both are conceptual rather than
tuning problems:

1. **K3 blends loop playback into freeze.** Freeze and loop want different
   buffer lengths and different internal ranges; blending them means neither is
   at its best. The freeze that works (mnemonic SW1-DOWN) is a *fixed* capture
   window with a *fixed* band split — not a point on a morph.
2. **Nothing is rhythmic.** Captures start and stop on an envelope gate, so loop
   lengths are arbitrary and the resulting glitches have no relationship to each
   other or to what is being played.

The Onward's answer to (2) is a single master time base that *everything* is
derived from. That is the concept worth adopting.

---

## 2. What the Onward actually does

Sources: the official field guide (CBA 2024 – ONW01, 25 pp.) read in full, plus
Chase Bliss product pages. **Everything in this section is from the manual.**
Section 2.5 lists what the manual does *not* specify — we get to choose those,
and should not pretend otherwise.

### 2.1 Architecture

Two **parallel** channels sampling the same input into **separate memory**:

- **GLITCH** — samples a phrase and repeats it in pieces. Dynamic loop.
- **FREEZE** — captures one moment and sustains it as a pad.

Each has its own footswitch: tap engages, **hold locks and preserves the current
sample** so it stops resampling. Sections (Error, Shape, Effects) can be routed
to one channel, the other, or both.

Sampling is **dynamic**: "Whenever Onward detects sound at the input, it samples
that sound. If there was already a sample, it replaces it." A hidden
**SENSITIVITY** control sets how easily playing triggers it. A `MANUAL` dip
switch turns the dynamic behaviour off in favour of footswitch resampling.

### 2.2 SIZE is the master time base

> "**SIZE** sets the length of the Glitch and the overall timing of Onward's
> various parts."
>
> "The timing of Shape and Error are both linked to the **SIZE** setting."
>
> "Both **SUSTAIN** and **FADE** are linked to the setting of the **SIZE** knob
> … Smaller sizes will speed up the fades and shorten the sustain."

So SIZE is not just a buffer length — it is the clock the whole pedal runs on.
It also sets the vibrato rate (ANIMATE left), and the MIDI clock subdivision
when synced.

**Max Glitch record time is one second.** The `½ SPEED` dip doubles the sample
size and adds lo-fi colour.

**Tap tempo** is a hidden gesture: tap both footswitches twice to enter tap mode
(all LEDs blink red), tap tempo with the left footswitch, exit with the right.

### 2.3 Glitch capture behaviour

> "It will begin sampling as soon as audio is detected, and continue until it
> hits the end of the recording length. This means the first note you play will
> be the first note in the sample, and subsequent notes will be recorded as
> well."

Note what this implies: the Onward glitch sample is **always SIZE long**. A
short phrase does not give a short loop — the recorder simply keeps running to
the end of the window. The capture is *aligned to the first note*, not to a
grid, and there is no quantisation of the captured length. That is the one place
where we deliberately diverge (§4.3).

### 2.4 Shape and Error

**SUSTAIN** = how long sounds hold before fading. On the Glitch side the knob
selects a **discrete repeat count**, printed on the panel as:
`1x 2x 3x 4x 5x 6x 7x 8x 9x 10x 12x 13x 14x 16x 18x ∞`. FADE adds further
repeats on top.

**FADE** = attack/release time, *and* the crossfade time when a new sample
replaces an old one — at slow settings old and new momentarily layer. Three
positions: SLOW / USER (hidden value) / FAST.

**ERROR knob** = "sets the likelihood of an error occurring as well as its
intensity". Explicitly: it controls the *chance* for all three types, and the
*intensity* for CONDITION and PLAYBACK.

**TYPE** toggle selects one of three:

| type | what it does (manual wording) | on GLITCH | on FREEZE |
|-|-|-|-|
| **TIMING** | "Manipulates the length of the samples" | "rhythmic variation and a pattern-like feeling" | "churning textures like modulating a synth" |
| **CONDITION** | "Creates dropouts and playback failures, as well as momentary changes to the sample rate" | "more intermittent and sparse" | "bump and bite" |
| **PLAYBACK** | "Changes both the direction and speed of playback at random" — **sped up or slowed by 2x or 4x, and reversed** | longer-lasting, rhythmic | brief blips and departures |

**ERROR BLEND** (hidden) mixes in the two *unselected* types, so all three can
occur at once; the ERROR knob still sets overall chance, the hidden control sets
how likely an error is to be one of the unselected types.

Character note from the manual: errors "take on a different character for each
channel. On the Freeze side they will generally manifest as brief blips and
departures that come and go; on the Glitch side they will be longer-lasting and
more rhythmic."

### 2.5 What the manual does NOT specify

Do not invent precision here. Not documented anywhere we could find:

- the probability distribution behind "likelihood", or how ERROR maps to it;
- whether errors are scheduled on a grid derived from SIZE, or free-running and
  merely scaled by it (the manual says timing is "linked to SIZE" and no more);
- the set of sample-length multipliers TIMING chooses from;
- CONDITION's dropout lengths and sample-rate-reduction factors;
- whether PLAYBACK's 2x/4x/reverse choices are equally weighted;
- how intensity stacks with probability as the knob rises.

Two searches turned up nothing beyond the manual's wording. **These are our
design decisions, and §5 makes them explicit so they can be tuned by ear.**

---

## 3. What we take and what we leave

**Take:**

- One master time base that sample length, repeats, fades and errors all derive
  from.
- A hidden tap-tempo gesture to set that base by foot.
- The three error categories — timing / condition / playback — as a vocabulary,
  with one knob for amount and a switch for type.
- Half / double speed as a blended extra voice (their OCTAVE), which stays on
  the division grid because the ratios are powers of two.
- Their footswitch idiom: one switch engages, holding it locks the buffer.
- Dynamic (played-into) capture with a sensitivity control.
- Lock/preserve: stop resampling and keep what is there.
- Separate buffers and separate behaviour for glitch and freeze.

**Leave:**

- Stereo, spread, MIDI, CV, ramping, presets, dip switches.
- Two channels running in parallel. Ours are **exclusive**, selected by SW1 —
  one enclosure, six knobs, and the builder wants each mode at its best rather
  than both at once.
- Texture / animate — vestige is not getting a distortion or modulation section.
- ERROR BLEND (mixing all three error types at once). Not now: it is exactly the
  setting in which you cannot tell which parameter you are hearing, and these
  parameters have to be found before they are worth blending.
- "Always record the full window" (§2.3); we quantise instead (§4.3).

---

## 4. Target concept for vestige

### 4.1 Modes (SW1)

| SW1 | mode | voices | buffer |
|-|-|-|-|
| **UP** | dynamic auto capture | 1 | loop buffer |
| **MIDDLE** | dynamic auto capture, polyphonic | 6 | loop buffer |
| **DOWN** | **freeze** | 1 | its own fixed freeze buffer |

Retired: manual capture mode (auto is always on), frippertronics, the K1 voice
count, and the K3 loop↔freeze blend.

The loop side and the freeze side become **conceptually and physically separate
buffers**. Freeze stops being a position on a morph and becomes an operation.

### 4.2 Control map

| control | function | notes |
|-|-|-|
| **K1** | **playback speed blend** | a crossfade, not an added voice: CCW only half-speed · noon only clean · CW only double-speed. Sits *before* the error stage |
| **K2** | **max buffer length + direction**, bipolar | CCW reverse · noon shortest · CW forward. Matches sprawl's K2 idiom |
| **K3** | **error intensity of the type SW2 selects** | not WYSIWYG — it edits the selected type's stored value (§6) |
| **K4** | **capture sensitivity** | was K2; the gate threshold |
| **K5** | fade in / out | unchanged; also the replace-crossfade (Onward's FADE) |
| **K6** | **dry/wet mix** | now shell-owned equal-power, like the other modules. vestige stops owning its output |
| **SW1** | mode | UP 1-voice · MIDDLE 6-voice · DOWN freeze |
| **SW2** | **error type select** | picks which of TIMING / CONDITION / PLAYBACK K3 edits. All three stay active at their stored values |
| **FS1** | **tap tempo**, dedicated | with LED1 as the clock indicator |
| **FS2** | capture + playback on/off · **hold = buffer hold** | hold always toggles hold, held or not; while held, tap still toggles playback |

Three knock-on effects of this map, called out because they are losses as well
as simplifications:

- **K6 becoming a plain mix removes the dry-cut-while-recording behaviour** that
  SW2 MIDDLE gave. With auto capture always running, the dry was previously
  ducked during capture to avoid doubling. A plain mix cannot do that. See Q3.
- **K1's speed blend is grid-friendly by construction.** Half and double are
  powers of two, so a half-speed loop lasts exactly two grid periods and a
  double-speed one exactly half — all three versions stay in sync and realign
  every two periods. Same reason Onward's PLAYBACK error uses 2x and 4x rather
  than arbitrary ratios. It transposes by an octave as a side effect, which is
  the Onward OCTAVE character. Placing it before the error stage means the
  errors act on whatever speed is selected, rather than fighting it.
- **FS2 absorbs everything the old FS1/FS2 pair did.** Clear-all needs no
  control of its own: switching the effect off while the buffer is *not* held
  clears it. That is simply what hold means — if you did not ask to keep it, it
  does not persist. Only possible because manual looper mode is gone.

K4's current temporary max-loop-length test (`VESTIGE_K4_MAXLEN`) is the
prototype for K2's new job — the mechanism exists and works, it moves knob and
gains a musical grid.

### 4.3 The time base and quantisation — the core of this rework

Let **T** = the master period, set by K2 or by tap tempo.

**T is a period, not a grid.** This is the crucial distinction and it is easy to
get wrong. Tap tempo sets *how long* a division lasts; it does not establish an
absolute, free-running bar line that captures then have to fall in with. There
is no global downbeat that the player cannot move.

**The capture start is the "one".** Every capture anchors its own grid at the
moment the gate opens, and all boundaries for that capture are measured from
there. Play a phrase late and the phrase is still on its own beat one — the
effect follows you rather than making you follow it.

That is what keeps this feeling like a **delay** (timing relative to when you
played) instead of a **synced looper** (timing relative to a clock you are a
guest of). It is also what the Onward does: sampling begins the moment audio is
detected, so "the first note you play will be the first note in the sample".

Rules:

1. **T is the maximum capture length.** A capture that reaches T ends there and
   playback begins — already implemented behaviour, just re-homed.
2. **Shorter captures quantise to the NEAREST division boundary**, rounding up
   *or* down. Rounding down truncates whatever was played past the boundary, and
   that is intended — a true quantise, not a permissive one. A late release
   gets cut rather than dragging the loop long.
3. **Every capture re-anchors.** The grid is relative to that capture's start,
   never to a running clock.
4. **The division set decides the groove.** See §4.5.
5. **Everything else derives from T**: error scheduling, timing-error lengths,
   the replace-crossfade, and the fade times when K5 is low.

Two consequences worth stating, because they are the point rather than side
effects:

- **In 6-voice mode each voice has its own anchor.** Six loops, each quantised
  to a division of the same T but each with its own beat one, is exactly the
  polymetric/polyrhythmic behaviour being aimed for: they share a pulse but not
  a downbeat, so they phase against each other and realign only when their
  divisions do.
- **Very short captures need a floor.** A capture shorter than half the smallest
  division would otherwise quantise to zero; it rounds up to the smallest
  division instead.

**What LED1 shows.** Since there is no absolute grid, a free-running metronome
flash would be a lie. LED1 should flash T **anchored to the most recent capture
start** — the current "one" — so it shows the beat the effect is actually on.

### 4.5 The division set

The division set is the list of fractions of T that a captured loop is allowed
to land on. It is the single strongest influence on the rhythmic character, and
it is worth being deliberate about:

- **Binary only** — `1, 1/2, 1/4, 1/8`. Every loop length is a power-of-two
  multiple of every other, so loops that start together stay together. Tidy,
  predictable, and static.
- **Adding ternary and dotted** — `2/3, 1/3, 1/6, 3/4`. A 2/3 loop against a 1/2
  loop realigns only every six periods. Against the per-voice anchoring above,
  this is what produces grooves that evolve for a long time without repeating,
  while still sharing a pulse.

Proposed starting set:

```
1  ·  3/4  ·  2/3  ·  1/2  ·  1/3  ·  1/4  ·  1/6  ·  1/8
```

**Decided: fixed, in constants, including the ternary and dotted divisions.**
There is no control free for it and it does not want one — the point of the odd
divisions is that loops of different lengths phase against each other, and that
is a property of the instrument rather than a performance parameter. If it ever
becomes selectable, the natural home is a curated preset once the error UI is
finalised (§6, "The endgame"): "which grid" plus "which errors" is what a
*character* actually is.

### 4.4 Freeze (SW1 DOWN)

Fixed, not blended. Starting values come from mnemonic, which is the freeze the
builder signed off on:

- capture window **400 ms** (`MNEM_FREEZE_WIN_MS`);
- 3 bands, crossovers **250 Hz / 2 kHz**;
- per-band grain 150 / 80 / 40 ms, coprime scans 11987 / 8419 / 4099, spray
  480 / 240 / 120.

vestige's engine already generalises this (`VESTIGE_MB_*`); the change is that
freeze stops reading its parameters off K3 and reads them off these constants.

---

## 5. Our error model (the part the manual leaves open)

One knob, K3, sets **probability** and **intensity** together, as Onward does.
Every error event is scheduled **on the grid** — that is the whole point of the
rework, and the main deliberate difference from a free-running randomiser.

Proposed, all tunable and all to be judged by ear:

**Scheduling.** At each division boundary, roll once. `P(error) = K3` scaled so
that noon gives roughly one event every few boundaries and full CW gives one at
most boundaries. Events last a whole number of divisions.

**TIMING** — re-lengthen the loop to a different division for the next pass,
drawn from the same division set, biased toward neighbours of the current
length. Grid-locked by construction, so it produces rhythmic variation rather
than drift.

**CONDITION** — two sub-effects, intensity from K3: a **mute** for a whole
division (dropout), and a **sample-rate reduction** applied for a division.
Reuse of the existing degrade engine (`MnemDegrade`) should be considered before
writing anything new.

**PLAYBACK** — speed ×2 / ×4 / ÷2 / ÷4 and reverse, for a whole number of
divisions. Speed changes that are powers of two keep the loop grid-aligned,
which is why Onward's choice of 2x/4x is worth copying exactly.

**All three types are always live, each with its own intensity.** SW2 does not
switch a type on and off — it selects which type's intensity K3 is currently
editing (§6). Set timing to taste, flick to condition, set that, flick to
playback. Whatever you leave behind keeps running.

This lands somewhere better than the pedal it comes from: Onward needs a toggle
*and* a hidden blend knob to get all three at once, and even then the blend is a
single "how much of the others" amount. Three independent levels is a superset,
reached with controls we already have.

**Everything else is a compile-time constant**, per the repo convention that
tuning values live in `*_constants.h` and not in the docs.

---

## 6. How we find the error settings — the matrix editor

§2.5 lists what the Onward manual does not tell us, which is most of the numbers
that matter. They have to be found by ear, **on the pedal, with the knobs under
the fingers** — not by rendering variants on the host. Feel is part of what is
being judged, and a file comparison cannot give it.

> **This UI is temporary.** It is a development instrument for finding the
> values, not the shipping control scheme. See "The endgame" below before
> building anything around it.

So the discovery tool is the control surface itself, in the manner of an
old-school synth matrix editor:

- **SW2 selects a parameter** — one of the three error types.
- **K3 edits that parameter's value.** It is *not* a WYSIWYG knob: its physical
  position means nothing until you move it, and what it changes is the stored
  intensity of whichever type SW2 currently points at.
- **All three stored values stay active**, so what you hear is always the whole
  error engine, not just the slice being edited.

**Pickup behaviour: jump.** When SW2 moves, K3's position no longer matches the
newly selected value; the value snaps to the knob as soon as the knob is moved.
The safer option (catch — the value does not move until the knob passes through
it) was considered and rejected: it costs immediacy, and on a throwaway editor
the cost of a nudge wiping a value is one more turn of the knob. Needs an ADC
dead zone so noise does not count as movement.

Because all three types are always live, there is no "clean" position on SW2 and
no need for one — a setting nobody can reach from a temporary editor is not a
missing feature.

### The endgame

The editor exists to produce numbers. Once they are found they go into
`vestige_constants.h`, the same tuning workflow the rest of the bundle uses,
and the editor's job is done.

The likely shape of the final UI is **preset configurations** — SW2 selecting
between a few curated error characters rather than addressing one type at a
time, with K3 back to being an absolute knob that scales the whole engine, as
Onward's ERROR does. That restores the rule every other control here follows,
and it means the three-independent-levels capability survives as *curation*
instead of as knob-twiddling. Not decided; recorded so stage 7 knows what it is
aiming at.

---

## 7. Staged plan

Each stage is independently flashable, independently judgeable by ear, and
behind a flag where that is cheap. No stage depends on a later one sounding
good.

### Stage 0 — retirement and re-map (no new DSP)

- Retire frippertronics and the K1 voice count; SW1 becomes 1-voice / 6-voice /
  freeze.
- Retire the K3 loop↔freeze blend; freeze runs from fixed constants (§4.4).
- Retire SW2 dry routing; K6 becomes the shell's equal-power dry/wet mix and
  vestige stops owning its output, like the other two modules.
- Move capture sensitivity to K4; move max buffer length to K2 and give K2 its
  bipolar direction half (CCW = reverse playback).
- **Footswitches reworked.** The old pair existed to start and stop manual
  capture, which is gone:
  - **FS2** — tap toggles capture + playback; **hold toggles buffer hold**,
    whether currently held or not. While held, tap still toggles playback, so
    the gesture means the same thing in both states. Switching the effect off
    while NOT held clears the buffers — hold is the only thing that preserves
    them, which is what makes the gesture self-explanatory.
  - **FS1** — free, and reserved for tap tempo in stage 1. LED1 goes with it.
- K1, K3 and SW2 read but unused, documented as reserved.

Freeze mode is deliberately left alone beyond moving it to SW1 DOWN and pinning
its constants — see Q1, deferred.

*Acceptance:* each SW1 position does one thing; K2 sets length and direction;
FS2's four states (playing / stopped / held-playing / held-stopped) are all
reachable and obvious; freeze sounds like mnemonic's. Large diff, no new
algorithms — the risky part is what gets deleted, so this stage stands alone.

### Stage 1 — the time base

- Introduce T explicitly, fed by K2.
- **FS1 is dedicated tap tempo** — no mode to enter or leave, unlike Onward's
  hidden gesture, because the switch has nothing else to do. Tapping sets T and
  overrides the knob until K2 moves, the same last-gesture-wins arbitration
  sprawl already uses.
- **LED1 is the clock**, flashing once per T, so the base is visible without
  playing a note.

*Acceptance:* tapping changes max capture length audibly and LED1 agrees; K2
takes over when moved.

### Stage 2 — quantised capture

- Captures extend to the next division boundary; division set from §4.3.
- Loops therefore always sit on the grid.

*Acceptance:* short stabs and long phrases both produce loops that lock together
rhythmically; in 6-voice mode, loops of different divisions phase against each
other instead of drifting.

**Firmware translation risk, flagged early:** the gate runs in `Controls()` at
~10 ms. A 10 ms error on a division boundary is audible as flam at short T.
Capture start/end must be timestamped **sample-accurately in the audio thread**,
with the control thread only deciding policy. This is exactly the class of
divergence `agents-instructions.md` warns about.

### Stage 2.5 — the matrix editor

- SW2 selects the error type; K3 edits that type's stored intensity, jumping to
  the knob on first movement, with an ADC dead zone.
- Explicitly temporary scaffolding (§6) — do not build other behaviour on top of
  it.
- Built before the error stages so every error parameter can be dialled by ear
  from the first build that has one, rather than guessed and re-flashed.

*Acceptance:* moving SW2 and then K3 changes only the selected type's value;
values already set stay set; a nudge of K3 after switching does not wipe them.

### Stage 3 — error: TIMING

- Intensity comes from the matrix editor's TIMING slot; events re-length the
  loop on the grid.

*Acceptance:* K3 up gives rhythmic variation that stays in time. If it drifts
off the grid, the scheduler is wrong, not the tuning.

### Stage 4 — error: CONDITION

- Grid-locked mutes and sample-rate reduction, intensity from the CONDITION
  slot, running alongside whatever TIMING is set to.

*Acceptance:* sparser, more intermittent, more bite — without losing the pulse.

### Stage 5 — error: PLAYBACK

- Grid-locked ×2 / ×4 / ÷2 / ÷4 / reverse for whole divisions.

*Acceptance:* speed and direction changes land on boundaries and the loop still
lines up afterwards.

### Stage 6 — K1 playback speed blend

- CCW blends in a half-speed voice, CW a double-speed voice, noon off.
- Powers of two, so the blended voice stays on the division grid.

*Acceptance:* the extra voice locks to the loop rather than drifting against it;
noon is silent-clean.

### Stage 7 — final error UI, then polish

- Retire the matrix editor and decide the shipping control scheme; the expected
  shape is curated error presets on SW2 with K3 as an absolute master amount
  (§6, "The endgame"). Everything found in stages 3–5 becomes the raw material
  for those presets.

- Freeze × error interaction (Onward's freeze errors are brief blips, not
  rhythmic events — likely a different scheduler weighting).
- LED semantics, README controls table, layout SVGs (generated, see
  `docs/gen_layout_svg.py`), `CONTROLS.md`, Document Map.

---

## 8. Open decisions

Resolved during planning, kept here so the reasoning is not lost: **clear-all**
needs no control (effect off without hold clears — that is what hold means);
**losing the dry-cut-while-recording** is a non-issue (a full-wet mix removes the
dry entirely, by a control that explains itself); **K3 pickup is jump**, not
catch, because the editor is temporary; **SW2 needs no clean position** for the
same reason; **K1 crossfades** between half / clean / double speed rather than
adding a voice, and sits *before* the error stage; and **loops quantise to the
nearest boundary of a grid anchored at their own capture start**, truncation
included.

**Q1 — DEFERRED: what do the controls do in freeze mode, and is freeze a source
or a destination?**

Decided to defer until there is a testable state. Feeding the freeze into the
loop buffer opens a second set of problems (what triggers a capture from a
continuous pad, two granular layers in series, whether the chopping has any
transients left to bite on), and those are not worth solving before the loop
side works on its own. One step at a time.

**Until then**, freeze stays standalone with its fixed constants (§4.4), and K2
and K3 are inert in freeze mode. Stage 0 should not try to answer this.

The options, kept for when the decision comes back round:

- **(a) Parallel, as Onward.** Freeze stands alone and errors apply directly to
  its playback — on their pedal this reads as "brief blips and departures"
  rather than rhythmic events. K2's length half stays inert; its direction half
  could still reverse the scan. Simplest, but it leaves a knob dead and the
  freeze stays a pad: nothing about it becomes rhythmic.

- **(b) Series — freeze feeds the loop buffer.** The freeze engine becomes the
  *source* the loop side captures and chops, instead of the live input. Every
  control keeps the job it has in the other modes: K2 = chop length + direction,
  SW2/K3 = errors, K1 = speed blend, K4 = the sensitivity deciding when the
  freeze re-captures. A sustained pad becomes rhythmic material, which is the
  thing (a) cannot do. Open sub-question if taken: a freeze pad is continuous,
  so the envelope gate would never cycle — capture once per T on the grid
  (machine-like), or re-capture whenever the freeze itself re-captures (keeps it
  dynamic, keeps K4 meaning one thing). Also two granular layers in series, so
  CPU and mush both need checking.

- **(c) Series, pad audible underneath.** As (b) but the untouched freeze is
  still heard beneath its chopped version. Richer, but needs a balance control
  we do not have and muddies what K6's mix means.

**Q2 — is the division set fixed or selectable?**
See §4.5. Fixed in constants for now; the attractive endgame is folding it into
the curated error presets, since "which grid" plus "which errors" is what a
character is.

## 9. What this costs

Deleting frippertronics removes a tested, working behaviour the builder
described as "fine, but conceptually limiting". The source should be kept
unwired rather than deleted, as armitage was, so it can be revived cheaply — see
`impulse resonator - armitage/ARMITAGE_ARCHIVED.md` for the pattern.

The K3 blend also goes, along with the unified-engine work in
`vestige-rework-plan.md` §2.0 that made the CCW half a relaxed version of the
freeze. The *engine* stays; only the addressing changes. That doc remains the
as-built reference for how the multiband freeze works internally.
