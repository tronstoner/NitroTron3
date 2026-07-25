# Impulse Synth — Concept (working spec)

Platform: Hothouse (Daisy Seed). 6 knobs, 3× 3-position toggles, 2 footswitches, 2 LEDs. Toggle switch 3 is reserved for module selection. This document covers one module.

Design reference: the Impulse Synthesizer mode of the Chase Bliss Lost + Found. Behaviour there was established from documentation, demos and owner reports; the mechanism is not published and the architecture below is an independent design, not a recreation.

## Concept

A polyphonic synth voice that multiplies on the incoming audio. The input excites a bank of tuned resonators, so playing dynamics and timbre pass through and modulate the result — harder playing excites harder, a different pickup or attack gives a different tone.

The input is conditioned before it reaches the bank. A raw plucked note is a sparse line spectrum, so only resonators sitting near an existing partial receive energy, and which ones those are depends on the overtone content of that particular note — this is the source of unstable, note-dependent response. Conditioning fills the gaps. Measurement (see Findings) establishes that the mechanism is an **asymmetric saturation** at low drive: it lifts excitation of non-octave resonators by ~13 dB and brings every bin into play, at no measurable cost in dynamic range. Sustained input remains a stability concern for the bank and is a stage-1 test item.

Onsets and pitches are detected from the input and set which resonators are active and at what tuning. The input therefore does two jobs: it carries the excitation energy, and it carries the note information.

Voiced for chords and slower playing. Timbre is expected to be dominated by frequency-dependent loop damping: bright and complex on the attack, thinning toward a rounder core while level holds.

## Hypothesis under test

Conditioned input excitation, combined with onset-gated quantised polyphonic note assignment and frequency-dependent loop damping, produces a resonator voice that stays responsive to playing dynamics and timbre while remaining stable across notes and attack types.

Falsifiable at stage 1: if conditioning does not measurably stabilise excitation relative to raw input, the character lives elsewhere.

## Findings

Established offline against a synthetic plucked-string source (110 Hz, 14 partials, per-partial decay) driving a 25-bin semitone-spaced comb bank spanning two octaves. Metrics: **lift** = mean level gain of the 22 non-octave bins relative to raw input; **dyn** = output level change for a 20 dB quieter input, where 20 dB means dynamics fully preserved.

### F1 — The T60 formula is exact

For a comb of round-trip length D with a two-point-average loop filter, `|L(f)| = cos(πf/fs)` and

```
T60(f) = -3D / (fs · log10 |g·L(f)|)          g = 10^(-3D / (T60·fs))
```

Measured against a 12-partial impulse response: **0.14 % mean error, 1.04 % worst case**. Frequency-dependent damping needs no tuning by ear, and the inverse gives the per-voice gain correction for any register directly.

### F2 — Linear conditioning cannot work

LPC whitening with bandwidth expansion (`a_k·λ^k`) was tested and rejected. Full whitening lifted non-octave bins by only 2.8 dB, and the effect was not a continuum: crest factor held at 4.9 for λ = 0 through 0.75, then jumped to 48.7 at λ = 1 — useless as a control law.

Two reasons, the second decisive:

1. Usable filter orders cannot resolve partial structure. Order ≈ 2·fs/f0 ≈ 870 would be needed at 110 Hz.
2. **No linear operator can fill a spectral gap.** Whitening, tilt, EQ and per-band gain rescale what is present. Between the partials of a plucked note there is nothing to rescale.

This rules out the per-band dynamic gain approach as a gap-filling mechanism, though per-band normalisation remains valid for onset detection.

### F3 — Asymmetry is the operative parameter, not drive

Only a nonlinearity generates components where none existed. Sweeping a tanh blended toward full-wave rectification, `(1−a)·tanh(kx) + a·|tanh(kx)|`:

| asymmetry | lift dB | bins > −20 dB | spectral entropy | dyn dB |
|---|---|---|---|---|
| 0.0 | −0.1 | 12 / 25 | 0.567 | 19.8 |
| 0.1 | 3.5 | 23 / 25 | 0.647 | 19.8 |
| 0.2 | 7.6 | 25 / 25 | 0.797 | 19.9 |
| 0.3 | 11.2 | 25 / 25 | 0.891 | 19.9 |
| 0.5 | 13.0 | 25 / 25 | 0.924 | 19.9 |
| 0.7 | 12.7 | 25 / 25 | 0.917 | 19.8 |
| 1.0 | 12.4 | 25 / 25 | 0.913 | 19.8 |

Monotonic to 0.5, then a plateau with a slight decline. Useful control range is **0 to 0.5**.

Drive is nearly irrelevant to lift and actively harmful to dynamics: drive 1 and drive 4 differ by under 0.4 dB of lift at any asymmetry, while symmetric shapers (tanh, hard clip, cubic, wavefolder) need drive 16–64 to reach even 3–5 dB of lift, by which point dyn has collapsed to 4–6 dB. Drive is therefore a constant, not a control.

### F4 — Crest factor is a guard rail, not a target

Crest rises from 4.65 raw to a peak of 6.8 at asymmetry 0.5–0.7, then falls; it collapses to ~1.1 under heavy drive. The transient becomes *peakier* under the useful settings, which is consistent with the narrow-impulse character the design is after. Values below about 3 indicate the conditioning is compressing rather than enriching, and are a stage-1 warning sign.

### F5 — Gap filling and dynamics are not in tension

The trade only appears at high drive. At drive 1 and asymmetry 0.5 the design gets full gap filling with dyn = 19.9 of 20 dB. This satisfies the requirement that playing dynamics and timbre pass through.

## Design principles

1. **Parameters are pitch-referred, not absolute.** Damping is a decay time, register is an octave multiplier, structure is a set of ratios. Nothing is expressed in absolute Hz where a relative form exists.
2. **One control set for all instruments.** No per-instrument build profiles or switches for this module. The design should extend to further instruments without changes to the control surface.
3. **Tuning constants live in a dedicated constants file.** Anything not on a control is a named constant, not a literal buried in the DSP.

## Architecture

```
in ─┬─► [onset detector] ──────────────┐
    │                                  ▼
    ├─► [filterbank analysis] ─► [note-set estimator] ─► [voice allocator]
    │                                                          │
    │                                                          ▼
    ├─► [input conditioning] ──────────┐            [tuning + register]
    │   asymmetric saturation          │                       │
    │                                  └───────────┬───────────┘
    │                                              ▼
    │                                   [resonator bank, loop damping]
    │                                              ▼
    ├─► [env follower] ─────────────────► [env-coupled filter]
    │                                              ▼
    └──────────────────────────────────────► [mix] ─► [limiter] ─► out
```

Onset detection derives from filterbank band energies with per-band normalisation. No separate detection signal path and no absolute threshold: the bands provide both the frequency weighting and the level reference, and detection shares one snapshot with note-set estimation so the two cannot disagree.

Analysis covers a fixed band count spanning 2–3 octaves at a fixed window position. Notes outside the window produce no assignment.

### Blocks and failure signatures

| Block | Key parameters | Failure signature |
|---|---|---|
| Onset detector | fast/slow env ratio, threshold, retrigger lockout | misses strums, or fires on sustain |
| Note-set estimator | band count, window position, harmonic sieve depth, snapshot window | octave ghosts, unstable sets |
| Voice allocator | voice count, replacement policy, register | churn on arpeggios |
| Input conditioning | asymmetry (control), drive (constant) | crest below ~3 means it is compressing, not enriching |
| Resonator bank | core type, damping, dispersion, nonlinearity | metallic, or will not sustain |

## Interface contract

Frozen before implementation starts. Both workstreams code against it.

**Onset event:** sample index, strength 0–1. A separate silence event from the envelope follower.

**Note set:** up to N entries, each a quantised MIDI note plus weight 0–1, and a replacement/addition flag. Emitted on onset only, never continuously.

**Stub source:** a timed fake-note-set generator plus manual chord entry, so the synthesis side can reach stage 1 with no dependency on the analysis side.

## Register

Resonator tuning comes from the allocator, not from the excitation spectrum, so register is free to sit anywhere relative to the played note. Register is a multiplier on the resonator tuning: ×2 delay length for one octave down, ÷2 for one octave up. Sub, unison and upper registers cost the same.

Each detected note can drive more than one resonator, so registers may be stacked from a single exciter burst. Cost is linear in total resonators.

Two consequences that are requirements, not optimisations:

- **Damping must be specified as a target decay time**, with the per-voice loop coefficient derived from the actual delay length. A shorter delay passes the loop filter more often per second, so a shared raw coefficient chokes high registers and leaves low ones ringing indefinitely.
- **Fractional-delay interpolation quality** is a stage-1 test item at the upper registers. Interpolation error is proportionally larger at short delay lengths, causing sharp tuning and dulled tone.

## Controls

Proposed mapping. Toggle 2 is provisional and expected to be reassigned once the resonator core is settled.

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Register | Bipolar. CCW = sub, centre = unison, CW = upper. Stacking behaviour and whether travel is stepped or a spread decided at stage 1 |
| KNOB 2 | Damping | Decay time, short → long. Primary timbre control |
| KNOB 3 | Structure | Inharmonicity / dispersion for the comb core, interval spread for the modal core |
| KNOB 4 | Asymmetry | Excitation enrichment, 0 → 0.5 of the shaper blend. Sole conditioning control (F3) |
| KNOB 5 | Envelope | Bipolar attack/release of the output filter. Fast ↔ slow |
| KNOB 6 | Mix | Equal-power, dry → wet |
| SWITCH 1 | Resonator core | **UP** comb · **DOWN** modal · MIDDLE unassigned. Provisional, for the A/B |
| SWITCH 2 | Unassigned | Discussed candidate: note-set policy, replace on onset versus accumulate. Not decided |
| SWITCH 3 | Module select | Reserved |

Knob 3 and knob 4 may prove to be the same perceptual axis. If stage 1 shows that, one slot frees up.

### Not on controls

| Item | Reason |
|---|---|
| Conditioning drive | Near-irrelevant to excitation lift, costs dynamics (F3) |
| Band count | CPU/resolution trade, not a musical parameter |
| Window position | Promote to a control only if stage 1 shows moving it is musical |
| Onset sensitivity | Per-band normalisation should remove the need |
| Voice count | CPU lever |
| Portamento | Deferred until the allocator exists; revisit at stage 4 |
| Limiter LF corner | Tracks the lowest active voice |

Auto-placement of the analysis window is not in scope. Revisit only if the fixed window proves too limiting.

## Resonator cores

| | Comb / extended Karplus-Strong | Modal bandpass bank |
|---|---|---|
| Cost per voice | Low — delay line, one-pole, allpass | High — N biquads |
| Frequency-dependent damping | Native, from the loop filter | Requires per-mode decay rates |
| Character | Stringy, accepts nonlinearity, dispersion available | Bell and plate, cleaner |

Both are built for a controlled comparison. Comb is the expected winner on cost and on natural overtone decay; the modal core is built to test that expectation rather than to assume it.

### A/B method

- Both cores behind one parameter surface: frequency, damping, structure, position, excitation input. Same call shape, same ranges, same normalisation.
- Shared conditioning stage, allocator, output stage and limiter. Neither core carries its own gain staging.
- Runtime switchable and level-matched, switchable mid-note.
- Recorded against identical performance takes.

Judged on: overtone decay while level holds; consistency across notes and dynamics; response to loop nonlinearity; measured CPU per voice.

Both cores stay in the binary during evaluation — flash and SRAM cost only, since one runs at a time. The loser is removed before stage 3.

## Tuning constants

Named constants, not literals in the DSP. Values marked TBD are set at stage 1.

| Constant | Value | Source |
|---|---|---|
| Conditioning drive | ~1.0 | F3 — higher costs dynamics for no lift |
| Asymmetry knob range | 0.0 – 0.5 | F3 — plateau beyond 0.5 |
| Crest factor floor | 3.0 | F4 — warning threshold |
| Analysis band count | TBD, 2–3 octaves | CPU/resolution trade |
| Analysis window position | TBD | promotable to a control |
| Onset fast/slow env ratio | TBD | — |
| Retrigger lockout | TBD | — |
| Harmonic sieve depth | TBD | — |
| Portamento time | TBD | deferred to stage 4 |
| Voice count | TBD | stage 1 CPU figures |
| Loop filter | two-point average | F1 — analytic T60 |

## Workstreams

Two independent streams, joined at stage 3.

| | Analysis | Synthesis |
|---|---|---|
| Environment | Desktop harness, recorded material | Hardware |
| Owns | Onset detector, filterbank, note-set estimator | Exciter, both cores, allocator, output stage |
| Depends on | Contract, recordings | Contract, stub note-set source |

## Staging

| Stage | Content | Gate |
|---|---|---|
| 0 | Offline harness: recording set, analysis runner, decision dumps | Correct note sets on strummed triads ≥80% of onsets, both instruments, no octave ghosts on open low notes |
| 1 | Conditioning plus both cores at fixed tuning, played into directly. No analysis | Target timbre reached. Conditioned versus raw excitation compared for consistency across notes and attack types. Sustained-input stability assessed. All registers auditioned on bass and guitar with identical settings. Core chosen. Interpolation quality assessed at upper registers |
| 2 | Mono onset detection driving a single voice | Onset to audible under ~30 ms, no double-triggers on picked notes |
| 3 | Polyphonic estimator replaces the stub | Strum behaviour matches the offline harness. CPU headroom measured |
| 4 | Portamento, damping mapping, env-coupled filter | — |
| 5 | Control mapping finalised, preset integration | — |

Stage 1 is the decision point for the hypothesis. If it fails, stop rather than proceed to analysis work.

## Work packages

1. **Contract and stub** — interface, fake note-set source, constants file skeleton. Prerequisite for all others.
2. **Offline harness** — recording set (strums, arpeggios, high chords, single notes, hard and soft, bass and guitar), analysis runner, decision dump format.
3. **Onset detector** — fast/slow differential on normalised band energies, retrigger lockout.
4. **Note-set estimator** — filterbank, semitone binning, harmonic sieve, snapshot window.
5. **Input conditioning and comb core** — conditioning stage, extended Karplus-Strong with loop damping, dispersion allpass, optional nonlinearity.
6. **Modal core** — bandpass bank behind the same parameter surface.

Packages 3 and 4 require 1 and 2. Packages 5 and 6 require only 1.

## Open items

- Whether the shaper blend is the right asymmetry formulation, or whether a DC-offset shaper gives a better feel across the same range.
- Register travel: stepped versus continuous spread, and whether stacking is default.
- Whether structure and exciter character are one perceptual axis.
- Switch 2 assignment after the core is chosen. Candidate: note-set policy, replace on onset versus accumulate.
- Voice count, pending stage 1 CPU figures.
