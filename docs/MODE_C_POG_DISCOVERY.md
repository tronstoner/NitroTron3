# Mode C — POG Simulation Discovery (SW1=MID, K4 CCW)

Status: implemented on `feature/pog-simulation` (commit `d514ecd`), sound
confirmed by ear against the dual-tap prototype. Replaces the TS→tube-amp
overdrive in the SW1=MID / K4-CCW slot (the TS chain stays in the tree behind
`MODE_C_POG_ENABLE` until the POG wins for good). Successor project to the
abandoned multi-dip poly tracking — see `MULTI_DIP_POSTMORTEM.md` for why
per-note tracking is off the table.

## Goal

An EHX-POG-style polyphonic octave generator: octave voices above and below
the dry signal, fully polyphonic, organ-like when stacked, with **zero pitch
tracking**. The whole point of the POG approach is that it transforms the
*entire signal* — chords included — so there is no detection layer, and
therefore nothing that could ever need slew, fades, release, or any other
continuity guardrail (hard project boundary, reconfirmed in the post-mortem).

## What the real POG actually is

The discovery phase started from the EHX "real-time transpose sampler" quote
(every EHX poly-octave since 2005 is a Freescale DSP56364-class pedal) and
read it as time-domain, delay-line based. That reading did not survive
contact with the audition — or with the hardware evidence:

- The original POG factory schematic shows external RAM marked **"OMIT from
  production"** — the shipped POG runs entirely in the DSP56364's ~2.5K words
  of internal RAM. That rules out long-window delay-line approaches outright
  and is consistent with a filterbank (near-zero state memory).
- Thuillier's ERB-PS2 algorithm ("Real-Time Polyphonic Octave Doubling for
  the Guitar") was developed from analysing the original POG, and the
  schult/terrarium-poly-octave project implements it on embedded hardware —
  the closest public analysis-based recreation of the POG sound.

## Approaches

### A. Dual-tap delay-line transpose (granular) — AUDITIONED AND REJECTED

The classic: a circular buffer written at unity rate; two read taps replay it
at 2× (octave up) or 0.5× (octave down), each tap wrapping over a fixed
window, crossfaded so one tap is always mid-window while the other wraps.
Polyphonic by construction, negligible CPU. This was the original
recommendation, was implemented first, and **failed the audition at every
window setting**:

- **Long windows (60–100 ms):** laggy, generic pitch-shifter sound — soft
  attack, obvious smearing. Not a POG.
- **Mid windows (~25 ms):** detune/chorus character instead of a solid
  octave voice.
- **Short windows (8 ms):** ring modulator — the crossfade rate enters the
  audio range and produces AM sidebands.

There is **no usable window**; the failure modes tile the whole range.
Conclusion: the POG is not a dual-tap shifter, and "window length is THE
ear-lever" was the wrong frame — the lever only selects which artifact you
get. Kept here as negative knowledge.

### B. ERB-PS2 quadrature filterbank — SHIPPED

The engine that shipped: `src/poly_octave.h`, adapted from
schult/terrarium-poly-octave (MIT License, Steven Schulteis), which
implements Thuillier's ERB-PS2 algorithm. Architecture:

1. **Decimate 48 kHz → 8 kHz** (two-stage FIR, passband 0–1.8 kHz). The wet
   path is band-limited by design — part of the POG sound.
2. **80 complex (analytic) bandpass biquads** with quasi-log-spaced centers
   ~60 Hz–1.7 kHz (Audio EQ Cookbook LPF prototype rotated into a complex
   bandpass per Noga).
3. **Per-band phase scaling** of each band's analytic signal:
   `out = in · (in/|in|)^(g−1)` — g=2 doubles the phase (octave up), g=1/2
   halves it (octave down, with sign bookkeeping across phase wraps). The
   +2-oct voice is the g=2 step **applied twice to the up-1 signal, with its
   own normalization**.
4. **Per-voice interpolation back to 48 kHz** (two-stage FIR); shifted bands
   are summed per voice, and the caller mixes the three wet buffers with its
   own staged gains.

The band-limited wet path and the imperfect filterbank reconstruction *are*
the POG character — not defects to engineer away.

**Gotcha (documented in the source):** an inv³ formulation of the +2-oct
voice (`y³ · |y|⁻³` computed as the band signal times the cubed inverse
norm) NaN-muted the whole mode on silent input — `FastInvSqrt(0)` is a huge
finite number whose cube overflows to inf, and `0 · inf = NaN`, which then
sticks in every downstream filter state. Always compute up-2 from the up-1
signal with its own normalization.

### C. Phase vocoder (FFT) — rejected

Cleaner poly shifting, but: 512–1024-point FFTs at 48 kHz on top of the
existing Mode C chain, real latency (one hop+window), fiddly transient
smearing ("phasiness"), and a big implementation surface. The POG doesn't do
this either.

### D. Analog-style octavers (rectifier up / flip-flop down) — rejected

Full-wave rectification doubles pitch only quasi-monophonically (chords →
intermod mush); flip-flop dividers are strictly monophonic. Cheap but not
polyphonic — not a POG.

### E. Anything pitch-tracked (PSOLA, per-note resynthesis) — dead on arrival

Violates the no-tracking boundary. Not considered.

## Signal chain (slot: SW1=MID, K4 CCW)

```
                    ┌─► clean ──────────────────────────────► × DRY ─┐
                    │        ┌─► SUB (−1 oct, 8k) ─► interp ─► × SUB ─┤
bass ─► decimate ───┤ 80-band├─► UP1 (+1 oct, 8k) ─► interp ─► × UP1 ─┼─► Σ ─► (into SW2 filter)
        48k→8k      │ shifter└─► UP2 (+2 oct, 8k) ─► interp ─► × UP2 ─┘
                    └────────┘
```

- **In-stage dry path — deliberate exception to the OD-era "clean is K6's
  job" rule.** The K4 travel starts on clean at noon and must *become* the
  octave stack, so the stage itself crossfades clean out as the first voice
  comes in (see mapping below). A wet-only stage would jump from clean to a
  lone quiet octave just off noon — contradictory. K6 remains the global
  dry/wet on top.
- No per-voice LP/HP or detune stages exist: the filterbank's restricted
  analysis range already rounds off the up voices (nothing above ~3.4 kHz in
  UP1, ~6.8 kHz in UP2 by construction), and the raw stack passed the
  audition without shimmer extras.

## K4 CCW mapping (DECIDED 2026-07-21, shipped as decided)

Noon = clean (bipolar K4 convention, unchanged). Travel CCW is **staged,
voices fading in one after another in fixed order — SUB (−1), then UP1 (+1),
then UP2 (+2):**

- **Segment 1:** equal-power crossfade clean → SUB (`sqrt` curves on both
  legs). Clean fades out as the sub-octave fades in; by the segment boundary
  the dry path is silent.
- **Segment 2:** UP1 fades in on top.
- **Segment 3:** UP2 fades in on top; full CCW = the complete −1/+1/+2 organ
  stack, no dry (K6 can always re-add global dry).

Segment boundaries are ear-tunable constants (`_SEG1_END`, `_SEG2_END` as K4
travel fractions). **Gain staging is POG-style: plain voice sum, no loudness
compensation** — the `_COMP_AT_*` pair is inert at 1.0. Voice hierarchy by
ear: SUB 2.5 > UP1 1.8 > UP2 1.2.

## Constants (all ear-tuned, `pedals/nitrotron3/constants.h`)

```
MODE_C_POG_ENABLE         // false = restore the TS→amp OD on this travel
MODE_C_POG_SUB_LEVEL      // 2.5 — per-voice balance at full fade-in
MODE_C_POG_UP1_LEVEL      // 1.8
MODE_C_POG_UP2_LEVEL      // 1.2
MODE_C_POG_SEG1_END       // 0.40 — K4 travel fraction: clean→SUB xfade complete
MODE_C_POG_SEG2_END       // 0.70 — UP1 fully in (UP2 ramps after)
MODE_C_POG_COMP_AT_NOON   // 1.0 — inert (POG-style plain sum)
MODE_C_POG_COMP_AT_MAX    // 1.0 — inert; trim below 1 only if the stack runs hot vs clean
```

The dual-tap-era `_WIN_*`, `_UP_LP_HZ`, `_SUB_HP_HZ`, `_DETUNE_CENTS`, and
`_K4_CURVE` constants no longer exist — they were levers of the rejected
approach.

## Open items / future rounds

1. **Grittier sub.** The bank tops out at 1.7 kHz, so sub content caps
   around ~850 Hz — the SUB voice is smooth/rounded. Levers if more grit is
   wanted: per-voice saturation on the sub, or widening the bank + decimator
   passband.
2. **Per-voice EQ.** The original POG has per-voice EQ; Schulteis
   post-processes with shelves (−11 dB high-shelf @ 140 Hz + 5 dB low-shelf
   @ 160 Hz) which we did NOT port. Candidate if voicing rounds ask for it.
3. **Bass instrument profile.** The band curve floors at ~60 Hz, but low B
   is 31 Hz — the `CenterFreq` curve becomes an instrument-profile lever
   when the bass build gets retuned.
4. **Latency** is a few ms (multirate FIRs + band group delay) — inaudible
   so far, but it exists; keep in mind if the slot ever feeds anything
   timing-critical.

## References

- EHX POG family (product pages): https://www.ehx.com/products/pog2/ ·
  https://www.ehx.com/products/micro-pog/ · https://www.ehx.com/products/pog3/
- EHX POG internals discussion (DSP56364, "real-time transpose sampler",
  factory schematic with external RAM marked "OMIT from production"):
  https://www.freestompboxes.org/viewtopic.php?t=491
- schult/terrarium-poly-octave (MIT, Steven Schulteis) — the ported engine:
  https://github.com/schult/terrarium-poly-octave
- E. Thuillier, "Real-Time Polyphonic Octave Doubling for the Guitar"
  (ERB-PS2, developed from analysing the POG):
  https://core.ac.uk/download/pdf/80719011.pdf
- A. J. Noga, "Complex Band-Pass Filters for Analytic Signal Generation and
  Their Application": https://apps.dtic.mil/sti/tr/pdf/ADA395963.pdf
- Time-domain granular pitch shifting (dual-buffer technique — the rejected
  Approach A): https://www.kvraudio.com/forum/viewtopic.php?t=476173
- Red Panda on granular pitch/delay design on embedded DSP:
  https://www.redpandalab.com/blog/particle-history-part-1-creating-a-granular-delay/
