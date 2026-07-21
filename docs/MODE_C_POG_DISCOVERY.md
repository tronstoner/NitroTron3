# Mode C — POG Simulation Discovery (SW1=MID, K4 CCW)

Status: discovery. Replaces the TS→tube-amp overdrive in the SW1=MID / K4-CCW
slot (the TS chain stays in the tree until the POG lands and wins the
audition). Successor project to the abandoned multi-dip poly tracking — see
`MULTI_DIP_POSTMORTEM.md` for why per-note tracking is off the table.

## Goal

An EHX-POG-style polyphonic octave generator: octave voices above (and
possibly below) the dry signal, fully polyphonic, organ-like when stacked,
with **zero pitch tracking**. The whole point of the POG approach is that it
transforms the *entire signal* — chords included — so there is no detection
layer, and therefore nothing that could ever need slew, fades, release, or
any other continuity guardrail (hard project boundary, reconfirmed in the
post-mortem).

What is known about the real thing: every EHX poly-octave since 2005 is a DSP
pedal (Freescale DSP56364 class), and EHX describes the algorithm as a
"real-time transpose sampler" — i.e. time-domain, delay-line based, not a
phase vocoder. Its signature slight grit/flutter on chords is the audible
crossfade of exactly this technique. That grit is not a defect for this
project — if anything it's budget for character.

## Candidate approaches

### A. Dual-tap delay-line transpose (granular) — RECOMMENDED

The classic: a circular buffer written at unity rate; two read taps replay it
at 2× (octave up) or 0.5× (octave down), each tap wrapping over a fixed
window, crossfaded so one tap is always mid-window while the other wraps.
Polyphonic by construction (it resamples the waveform, no note model).

- CPU: negligible (a few interpolated reads/sample per voice). Multiple
  instances (+1, +2, −1) are cheap.
- Latency: effectively the crossfade window (~10–30 ms perceived softening,
  not delay — output starts immediately).
- Artifacts: crossfade comb/tremolo ("warble") at a rate set by shift ratio
  and window length. **Window length is THE ear-lever**: short = tight attack
  + faster flutter, long = smoother sustain + softer attack.
- DaisySP ships a `PitchShifter` (delay-line, crossfaded taps, semitone
  transposition) usable as a starting point or reference; hand-rolling a
  fixed-ratio 2×/0.5× version is small and lets us control interpolation
  quality (aliasing on the up-octave) and crossfade shape.

### B. Phase vocoder (FFT) — rejected for now

Cleaner poly shifting, but: 512–1024-point FFTs at 48 kHz on top of the
existing Mode C chain, real latency (one hop+window), fiddly transient
smearing ("phasiness"), and a big implementation surface. The POG itself
doesn't do this, and its time-domain grit is the sound we're chasing. Only
revisit if approach A's warble is unacceptable even after window tuning.

### C. Analog-style octavers (rectifier up / flip-flop down) — rejected

Full-wave rectification doubles pitch only quasi-monophonically (chords →
intermod mush); flip-flop dividers are strictly monophonic. Cheap but not
polyphonic — not a POG.

### D. Anything pitch-tracked (PSOLA, per-note resynthesis) — dead on arrival

Violates the no-tracking boundary. Not considered.

## Proposed signal chain (slot: SW1=MID, K4 CCW)

```
                    ┌─► clean ───────────────────────────────────► × DRY ─┐
                    ├─► [×0.5 transpose, window W_SUB] ─► LP/HP ─► × SUB ─┤
bass ─► pre-HP? ────┤                                                     ├─► Σ ─► comp ─► (into SW2 filter)
                    ├─► [×2 transpose, window W_UP]  ─► LP ─────► × UP1 ─┤
                    └─► [×4 transpose, window W_UP2] ─► LP ─────► × UP2 ─┘
```

- **In-stage dry path — deliberate exception to the OD-era "clean is K6's
  job" rule.** The K4 travel starts on clean at noon and must *become* the
  octave stack, so the stage itself crossfades clean out as the first voice
  comes in (see mapping below). A wet-only stage would jump from clean to a
  lone quiet octave just off noon — contradictory. K6 remains the global
  dry/wet on top.
- Octave-up voices get a gentle LP (`_UP_LP_HZ`): 2× transposition also
  doubles pick noise and fret fizz; the POG's "organ" quality comes from
  rounding that off.
- Sub-octave gets its own window (longer — half-speed reads smear less but
  wobble more on low bass) and possibly an HP to keep the low B from turning
  to rumble.
- Optional per-voice detune (`_DETUNE_CENTS`, a few cents on UP1) fakes the
  drawbar-chorus shimmer of stacked octaves. Off by default — audition first.

## K4 CCW mapping (DECIDED 2026-07-21)

Noon = clean (bipolar K4 convention, unchanged). Travel CCW is **staged,
voices fading in one after another in fixed order — SUB (−1), then UP1 (+1),
then UP2 (+2):**

- **Segment 1:** crossfade clean → SUB. Clean fades out as the sub-octave
  fades in; by the segment boundary the dry path is silent.
- **Segment 2:** UP1 fades in on top.
- **Segment 3:** UP2 fades in on top; full CCW = the complete −1/+1/+2 organ
  stack, no dry (K6 can always re-add global dry).

Segment boundaries are ear-tunable constants (`_SEG1_END`, `_SEG2_END` as K4
travel fractions). Loudness comp across the travel via the usual
`_COMP_AT_*` pair, keeping the stack roughly level against clean (K6 A/B
honesty).

## Proposed constants (all ear-tunable, `src/constants.h`)

```
MODE_C_POG_WIN_UP_MS      // ×2 crossfade window — THE warble/attack lever
MODE_C_POG_WIN_UP2_MS     // ×4 window (if UP2 ships)
MODE_C_POG_WIN_SUB_MS     // ×0.5 window
MODE_C_POG_UP1_LEVEL      // per-voice balance at full fade-in
MODE_C_POG_UP2_LEVEL
MODE_C_POG_SUB_LEVEL
MODE_C_POG_SEG1_END       // K4 travel fraction: clean→SUB xfade complete
MODE_C_POG_SEG2_END       // K4 travel fraction: UP1 fully in (UP2 ramps after)
MODE_C_POG_UP_LP_HZ       // fizz tamer on up voices
MODE_C_POG_SUB_HP_HZ      // rumble tamer on sub (0 = off)
MODE_C_POG_DETUNE_CENTS   // organ shimmer, 0 = off
MODE_C_POG_K4_CURVE       // travel shaping
MODE_C_POG_COMP_AT_NOON / _COMP_AT_MAX
```

## Staging

Sound target: **mimic the OG POG as closely as possible — punchy, thick.**
Start raw: transpose voices + staged mapping only, no extras (no detune, no
elaborate tone shaping) until the raw engine has passed the audition.

- **P.1 — raw OG-POG core, audition first** (post-mortem lesson: judge the
  raw engine before layering anything): all three transpose voices (−1, +1,
  +2), the decided staged K4 mapping (clean→SUB xfade, then UP1, then UP2),
  fixed windows biased punchy (short), simple per-voice LP only where fizz
  demands it. Ears decide on the warble character and the punch *before*
  any further work.
- **P.2 — tone + windows:** per-voice window tuning (sub vs up), LP/HP
  voicing, per-voice balance, thickness.
- **P.3 — feel:** K4 curve, segment boundaries, loudness comp; detune
  shimmer only if the raw stack asks for it.

## Open questions (decide at P.1/P.2 auditions)

1. Detune shimmer: character or mush on bass? (Off for P.1.)
2. Does the ×4 voice need a steeper anti-alias treatment (it transposes
   content up to 8–10 kHz into fizz territory)?
3. Where do the segment boundaries sit — equal thirds of the CCW travel, or
   a long first segment (clean→SUB is the most-played region)?

## Implicit assumptions to verify on hardware (not in any demo)

- Crossfade warble rate on *low bass* fundamentals: windows tuned on guitar
  demos are usually too short for 30–60 Hz strings — expect the sub/up
  windows to land longer than literature defaults.
- Buffer memory: a few 100 ms of mono float fits in SRAM/SDRAM trivially;
  interpolated reads from SDRAM are fine at this voice count.
- Interaction with the SW2 filter stage: the POG feeds the same downstream
  chain the OD did; the Moog ladder after a full octave stack may need
  different drive staging.

## References

- EHX POG family (product pages): https://www.ehx.com/products/pog2/ ·
  https://www.ehx.com/products/micro-pog/ · https://www.ehx.com/products/pog3/
- EHX POG internals discussion (DSP56364, "real-time transpose sampler"):
  https://www.freestompboxes.org/viewtopic.php?t=491
- Time-domain granular pitch shifting (dual-buffer technique):
  https://www.kvraudio.com/forum/viewtopic.php?t=476173
- Red Panda on granular pitch/delay design on embedded DSP:
  https://www.redpandalab.com/blog/particle-history-part-1-creating-a-granular-delay/
