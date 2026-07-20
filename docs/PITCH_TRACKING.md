# Pitch Tracking — Plan & Research

## Current Implementation

YIN pitch tracker (`pitch_tracker.h`), voiced per instrument by the `TRACK_*`
profile block in `src/constants.h`. BASS is the default; GUITAR is selected at
build time with `make INSTRUMENT=guitar` (defines `NT3_INSTRUMENT_GUITAR`).
The default (bass) build is byte-identical to the pre-profile firmware.

| | BASS (default) | GUITAR |
|---|---|---|
| Decimation `TRACK_DEC` | 4x (48 → 12 kHz) | 2x (48 → 24 kHz) |
| Anti-alias / fundamental-isolation LP `TRACK_AA_LP_HZ` (4-pole) | 400 Hz | 1.2 kHz |
| Lag range `TRACK_MIN_LAG…MAX_LAG` | 24…400 → ≈ **30–500 Hz** | 23…360 → ≈ **67–1043 Hz** |
| Window `TRACK_WINDOW` | 400 (~33 ms) | 640 (~27 ms) |
| Hop `TRACK_HOP` | 64 (~5.3 ms) | 128 (~5.3 ms) |
| Parabolic sub-lag refine `TRACK_PARABOLIC` | **off** (output unchanged) | **on** (integer-lag steps are ~40 cents at 1 kHz) |

Shared machinery (both profiles):
- 2-pole HP at 25 Hz for DC blocking (`hp_coeff_`)
- YIN difference function with cumulative mean normalization (`TRACK_THRESHOLD = 0.15`)
- Early termination on first dip below threshold (the lowest-fundamental lock)
- Ring buffer 1024 samples (a `static_assert` guards the `W + MAX_LAG` lookback)
- Envelope gating in `Feed()` — skips tracking when `env_level < 0.001`
- Heavy computation (`RunYin`) runs in main loop, not audio callback
- **Output quantized to nearest MIDI semitone** (`midi_note_ = roundf(midi)`) via `GetMidiNote()`, plus unrounded `GetMidiNoteContinuous()`
- Parabolic interpolation around the YIN minimum refines only the already-chosen `best_tau` — the tau search and first-dip rule are untouched
- Phases 1–2 complete. GUITAR profile wired but untuned — values are starting brackets, to be ear-tuned with a guitar on hand.

### Consumers (who reads the tracker today)

| Caller | Mode | Uses | Quantized? |
|---|---|---|---|
| `DRONE_FIXED` | A, SW2 UP | nothing (fixed pitch) | n/a |
| `DRONE_TRACK` | A, SW2 MID | `GetMidiNote()` → pitch class in target octave | **yes — wants it** |
| `DRONE_TRACK_DIRECT` | A, SW2 DOWN | `GetMidiNote()` as float | yes — *but shouldn't be* |
| Synth voice | C, SW1 DOWN | `MidiToFreq(GetMidiNote())` | yes — *but shouldn't be* |

> The two "direct" consumers read the value as a float but receive an already-rounded
> number — so **direct tracking does not follow bends today.** That is the gap this plan closes.

### Known Limitations

- Glitches at note beginnings — YIN needs ~2 periods before reliable detection
- **No continuous-pitch tracking** — output is quantized to semitones. Fine for octave-locked mode; it is the missing feature for direct tracking and the Mode C synth (see "Continuous Pitch Tracking" below)
- Harmonic confusion still possible on some notes despite 400 Hz LP

---

## Continuous Pitch Tracking (Pitch-Bend / Microtonal) — Detailed Plan

**Status:** designed, not yet implemented. This is the active next feature
(branch `feature/synth-tracking-improvements`). Implement in small, ear-checked
increments — build, listen, decide before each next step.

### Goal

Track the input pitch as a **continuous quantity**, fast enough to follow bends,
slides, vibrato — and, equally, any tuning: **non-12-TET, microtonal, fretless.**

This is explicitly **not** MIDI-style "land on a note, then bend from it." There is
**no note grid anywhere** in the continuous path. We track absolute pitch and never
round it. The only path that quantizes is the separate **octave-locked** mode, which
keeps `roundf` and is left untouched.

### Design principles (agreed)

1. **No note grid in the continuous path.** Carry pitch as **log-frequency (cents
   resolution, never rounded)** — i.e. a continuous MIDI-float, which is just
   `log2(freq)` rescaled. Log-pitch is the scale-agnostic axis: a bend of N cents is
   the same distance in any register, and it works identically for any tuning system.
   (Internally storing raw Hz would behave the same; cents just makes the smoothing
   math uniform.)
2. **Stability over reach, always.** We trade tracking *range* for tracking
   *stability* every time. Do not chase high-register reach at the cost of the
   rock-solid low-fundamental lock we have today.
3. **Small increments from the current implementation.** Do **not** refactor the
   tracker's signal conditioning or tau search. That machinery is exactly what
   produces the stability we like. Each step below is additive and revertible.

### Why a slew-rate limiter, not confidence-adaptive smoothing

We considered three grid-free stabilisers: a slew-rate limiter, median-of-N outlier
rejection, and a confidence-adaptive one-pole. **Slew limiter wins** as the core
mechanism, for a concrete reason:

- A confidence-adaptive smoother is **actively wrong for fast real moves** (e.g. a
  bass whammy sweep). During a fast sweep the ~33 ms analysis window contains a
  *changing* period, so YIN's dip smears and confidence **drops** — the adaptive
  smoother would then heavy up and lag *exactly* when the pitch is moving fastest. It
  penalises real fast motion because fast motion looks like low confidence.
- A slew limiter has **one fixed, knowable maximum rate**, fully decoupled from
  confidence. Predictable is the whole point.

**The rates separate cleanly** (updates land every `HOP/dec_sr` = 64/12000 ≈ 5.33 ms):

| Event | Pitch change | Per frame |
|---|---|---|
| Typical expressive sweep (2 oct / 300–500 ms) | 2400 cents | ~26–43 cents |
| Aggressive whammy (2 oct / 150 ms) | 2400 cents | ~85 cents |
| Single-frame YIN fifth error | 700 cents | 700 cents |
| Single-frame YIN octave error | 1200 cents | 1200 cents |

A cap around **~150–200 cents/frame (~30 cents/ms)** passes even an aggressive whammy
with margin while clipping octave/fifth single-frame glitches by 4–8×. Tunable.

### What stays untouched (addresses the regression fear)

Everything that produces today's stability lives **upstream** of the one line we
change. **Unchanged, byte-for-byte:**

- 4× decimation, 400 Hz anti-alias LP, 25 Hz HP
- YIN difference function + cumulative-mean normalization
- **The first-dip-below-threshold rule** (the lowest-fundamental lock)
- `MIN_LAG` / `MAX_LAG` range, `W`, `HOP`, `THRESHOLD`
- `RunYin`'s tau search
- `roundf` → `GetMidiNote()` (octave-locked mode runs verbatim)

The continuous value is derived from the **same `best_tau`** the current code already
finds. By construction it locks onto the same fundamental with the same stability — it
simply does not round the answer. If any increment regresses the feel, revert and we
are back to today.

### Increment 1 — minimal continuous output (first build)

- Keep `RunYin()` and `roundf` as-is; `GetMidiNote()` stays the quantized getter.
- Compute one continuous value from the same `best_tau` (**no interpolation yet**),
  run it through **only the slew limiter**, store as `cont_midi_`, expose
  `GetMidiNoteContinuous()`.
- Point `DRONE_TRACK_DIRECT` (Mode A, SW2 DOWN) and the Mode C SW1=DOWN synth voice
  at `GetMidiNoteContinuous()`.
- New constant: `PITCH_SLEW_MAX_CENTS_PER_MS` (start ~30).
- **Revert = one line.** The slew limiter does nothing on a held note (no movement),
  so sustain is unaffected; it only bounds how fast pitch may travel = glitch guard.

**Then build, listen, decide.** Only proceed to the next increment if the ear calls for it.

### Increment 2 — parabolic interpolation (only if stepping is audible)

- Add parabolic interpolation around the YIN minimum (`best_tau` and its two
  neighbours' `d'` values) for sub-sample period → smooth continuous frequency.
- Does **not** change the tau search; it only refines the frequency estimate from the
  already-chosen minimum.
- **Status: the machinery landed with the guitar profile** (gated on
  `TRACK_PARABOLIC` — on for GUITAR, off for BASS so the bass output is
  unchanged). Enabling it for BASS is a one-constant flip in `constants.h`.
- Removes integer-lag stepping, which is finest at low pitch and gets coarser as pitch
  rises (and when a whammy pushes pitch up), so it matters most for the bend case.

### Increment 3 — jitter cleanup (only if held notes wobble)

- **Lenient confidence gate**: reject only clearly-garbage frames (`best_dp` above a
  *loose* threshold → hold last value). Tuned loose enough that a real sweep survives
  rather than freezing. Constant: `PITCH_CONF_THRESH`.
- **Tiny fixed one-pole** on the continuous value for residual sustain jitter — a
  *fixed* coefficient, deliberately **not** confidence-driven (keeps predictability).
  Constant: `PITCH_SMOOTH` (or similar).

### Deferred — explicitly out of scope for this feature

These are real refactors that touch the stability-critical path; do them only as
separate, opt-in passes, never bundled into the increments above.

- **Whammy top-end reach.** To track a +1/+2 octave sweep off *higher* notes (past the
  current 500 Hz ceiling), raise the anti-alias LP (~700–800 Hz) and lower `MIN_LAG`.
  Trade-off: a higher LP leaks more harmonic energy on low notes → more octave-error
  risk. Per principle #2, **stability wins by default** — only revisit if reach is
  demanded in practice.
- **Guitar profile — IMPLEMENTED** (no longer deferred). The `TRACK_*` profile
  block in `src/constants.h`, selected by `make INSTRUMENT=guitar`. `BASS` =
  today's values exactly (default build byte-identical); `GUITAR` = 1.2 kHz LP,
  `DEC = 2`, lag range ≈67–1043 Hz, parabolic refine on. Tuning still pending —
  needs a guitar on hand.
- **Onset / latency (Phase 3).** See below.

### Window-smear caveat (physics, not the smoother)

The ~33 ms analysis window inherently low-passes very fast pitch changes — during an
extreme whammy slam the period changes *within* the window, briefly softening both the
estimate and confidence before it re-locks at the destination. No smoothing setting
fixes this; the window length (and its latency / low-note trade-off) is the only dial,
and that belongs with the deferred onset/window work, not this feature.

### New constants (summary)

| Constant | Increment | Start value | Purpose |
|---|---|---|---|
| `PITCH_SLEW_MAX_CENTS_PER_MS` | 1 | ~30 | Max continuous-pitch travel rate (core stabiliser) |
| `PITCH_CONF_THRESH` | 3 | loose | Reject only clearly-garbage frames; hold otherwise |
| `PITCH_SMOOTH` | 3 | light | Fixed one-pole for residual held-note jitter |
| `TRACK_*` profile block | **done** | `BASS` default | Bass/guitar tracker profile (`make INSTRUMENT=guitar`), incl. `TRACK_PARABOLIC` |

---

## Signal Conditioning (improve any algorithm)

These improvements apply regardless of which detection algorithm is used. Signal conditioning matters as much as the algorithm itself.

### Add highpass filter
- 2-pole HP at 25 Hz removes DC offset and subsonic rumble
- Prevents zero-crossing drift from low-frequency noise

### Gate tracking on envelope
- Don't attempt pitch detection when envelope follower output is near zero
- Prevents garbage detection from string noise, fret buzz, or amplifier hum
- Hold last detected note during silence (already implemented)

### Anti-alias / fundamental-isolation LP cutoff
- **Current: `TRACK_AA_LP_HZ`, 4-pole — 400 Hz BASS / 1.2 kHz GUITAR.** Doubles as
  the decimation anti-alias filter and as harmonic rejection that isolates the fundamental.
- Lowering it (e.g. 120 Hz) sharpens low-note harmonic rejection but caps trackable
  range and kills any high-register / whammy-up content. Per the stability-over-reach
  principle this stays at 400 Hz for the BASS profile.
- The **GUITAR** profile (implemented, `make INSTRUMENT=guitar`) raises this to
  1.2 kHz so guitar fundamentals pass.

### Consider Bessel filter instead of cascaded one-pole
- Bessel has linear phase — preserves zero-crossing locations better
- Cascaded one-pole sections only approximate Butterworth, with uncontrolled phase response

---

## Algorithm Options

### YIN (Recommended Tier 1 Upgrade)

De Cheveigne & Kawahara, 2002. Based on the difference function (inverse of autocorrelation) with cumulative mean normalization.

**How it works:**
1. Difference function: `d(tau) = sum((x[j] - x[j+tau])^2)` over a window
2. Cumulative mean normalization: `d'(tau) = d(tau) / ((1/tau) * sum(d(k)))` — normalizes so the dip at the true period is below 1.0
3. Absolute threshold: pick the **first** tau where `d'(tau) < threshold` (typically 0.10–0.15) — this avoids octave errors by preferring the fundamental over subharmonics
4. Parabolic interpolation around the minimum for sub-sample accuracy

**Why it's good for bass:**
- Finds periodicity even when fundamental is weaker than harmonics (the #1 problem with zero-crossing on bass)
- The "first dip below threshold" rule prevents octave-up errors
- 0.22% gross error rate in benchmarks
- Well-understood, widely implemented, easy to tune

**Implementation on Daisy Seed:**
- Buffer: 2048 samples (covers down to ~23 Hz, sufficient for low B in drop tuning)
- Hop size: 256 samples (5.3 ms update rate)
- Threshold: 0.15 (slightly higher than standard 0.10 to tolerate bass noise)
- **FFT acceleration via CMSIS-DSP** (`arm_math.h`): 2048-point FFT takes ~100–200 μs on Cortex-M7. Two FFTs + pointwise multiply + IFFT = ~400–600 μs = ~1–2% CPU. The Daisy Seed ships with CMSIS-DSP accessible.
- Memory: 2048 float buffer = 8 KB + FFT scratch ~16 KB. Trivial vs 512 KB SRAM.

**Latency floor (physics, same for any algorithm):**

| Note | Freq | 1 Period | 2 Periods (min for YIN) |
|------|------|----------|------------------------|
| B0 (5-string) | 31 Hz | 32 ms | 65 ms |
| E1 | 41 Hz | 24 ms | 49 ms |
| A1 | 55 Hz | 18 ms | 36 ms |
| D2 | 73 Hz | 14 ms | 27 ms |
| G2 | 98 Hz | 10 ms | 20 ms |

### MPM — McLeod Pitch Method (Tier 2)

Uses normalized autocorrelation (NSDF) with peak-picking. Finds the first positive-going zero crossing of the NSDF, then the highest peak after that.

**Advantage over YIN:** extracts pitch with as few as 2 periods, better normalization for amplitude changes. Slightly better at low latency.

**Disadvantage:** slightly higher gross error rate (1.47% vs 0.22%).

**Reference implementation:** `sevagh/pitch-detection` on GitHub — clean C++, has both YIN and MPM.

### AMDF — Average Magnitude Difference Function

Replaces multiplications with subtractions. Cheaper on CPUs without FPU. The Cortex-M7 has a hardware FPU with single-cycle float multiply, so this advantage is irrelevant. Not recommended.

### Bitstream Autocorrelation (Cycfi Q Library)

Converts signal to 1-bit stream, computes autocorrelation via XOR + popcount on 64-bit integers. ~64x speedup over float ACF. The Q library is already partially in the repo under `lib/HothouseExamples/Funbox-to-Hothouse-Port/Earth/`.

**Pros:** extremely fast (~50 ns/sample), tested on bass. Confirmed to compile on Daisy Seed by the community.

**Cons:** large C++ library, extracting just the pitch detector requires pulling in the signal conditioning chain. The 1-bit quantization discards amplitude information. The library's newer "Hz" algorithm may be hard to extract cleanly.

---

## Latency Reduction: Onset Detection (Tier 2)

Independent of algorithm choice. Detects envelope attack and uses the first clean cycle for an initial pitch estimate, then refines with YIN/MPM once 2 full periods are available.

- Reduces initial tracking latency to ~1 period instead of ~2
- For low E: ~24 ms instead of ~49 ms
- The Cycfi research notes that on fretted instruments, the left hand frets the note slightly before the right hand plucks — the signal conditioner can detect this pre-pluck oscillation for even earlier tracking

---

## Implementation Plan

### Phase 1 — Signal Conditioning ✓
- [x] Add 2-pole HP at 25 Hz before the LP
- [x] Gate pitch detection on envelope follower
- [x] Anti-alias LP at 400 Hz (serves both harmonic rejection and decimation)

### Phase 2 — YIN ✓
- [x] Implement ring buffer (1024 samples at 12 kHz decimated rate)
- [x] Implement YIN difference function + cumulative mean normalization
- [x] 4x decimation for efficiency (no FFT needed — naive YIN fast enough at 12 kHz)
- [x] Early termination on first dip
- [x] Move heavy computation to main loop (fixes audio glitches)
- [x] Quantize output to semitones
- [x] Parabolic interpolation for sub-sample accuracy — implemented with the
      instrument profile, gated on `TRACK_PARABOLIC` (GUITAR on; BASS off, so the
      bass build's output is unchanged)

### Phase 4 — Continuous Pitch Tracking (ACTIVE — next feature)

Full design, rationale, and decisions in **"Continuous Pitch Tracking" above.** Build
in small ear-checked increments; stop and listen between each.

- [ ] **Increment 1** — `GetMidiNoteContinuous()` from the same `best_tau` + slew
      limiter only (`PITCH_SLEW_MAX_CENTS_PER_MS` ~30). Wire `DRONE_TRACK_DIRECT` and
      the Mode C SW1=DOWN synth to it. `GetMidiNote()` / `roundf` / octave-locked
      untouched. (Revert = one line.)
- [ ] **Increment 2** (only if stepping audible) — parabolic interpolation around the
      YIN minimum for sub-sample period.
- [ ] **Increment 3** (only if held notes wobble) — lenient confidence gate
      (`PITCH_CONF_THRESH`) + small fixed one-pole (`PITCH_SMOOTH`).

### Phase 3 — Onset Detection (deferred, separate pass)
- [ ] Detect envelope attack (rising edge above threshold)
- [ ] On onset: use first clean zero-crossing cycle for initial estimate
- [ ] Refine with YIN once 2 periods available
- [ ] Evaluate latency improvement — should fix glitches at note beginnings
- Note: this is also where the window-smear limit on very fast bends/whammy is
  addressed (window length / latency trade-off).

### Instrument profile & whammy reach
- [x] `TRACK_*` compile-time profile block (`BASS` = current values exactly, default
      build byte-identical; `GUITAR` via `make INSTRUMENT=guitar` = 1.2 kHz LP /
      `DEC = 2` / lag range ≈67–1043 Hz / parabolic refine on).
- [ ] Ear-tune the GUITAR profile values (tracker + the six profiled voicing
      constants — see the "Instrument profile" block in `src/constants.h`) with a
      guitar on hand.
- [ ] Optional whammy top-end reach for BASS (raise LP + lower `MIN_LAG`) — only if
      reach is demanded in practice; stability wins by default.

---

## References

- YIN: de Cheveigne & Kawahara, "YIN, a fundamental frequency estimator for speech and music," JASA 2002
- MPM: McLeod & Wyvill, "A smarter way to find pitch," ICMC 2005
- Cycfi Q: https://github.com/cycfi/q — bitstream autocorrelation, bass-tested
- Cycfi research posts: https://www.cycfi.com/2018/03/fast-and-efficient-pitch-detection-bitstream-autocorrelation/
- sevagh/pitch-detection: https://github.com/sevagh/pitch-detection — C++ YIN/MPM
- CMSIS-DSP: https://arm-software.github.io/CMSIS-DSP/main/
- Daisy Seed CMSIS-DSP: https://forum.electro-smith.com/t/cmsis-dsp-library-support-arm-math-h/554
- Future Impact / Panda Audio: https://www.panda-audio.com/future-impact-v4
- Akai Deep Impact SB1 used Hitachi H8 MCU (~16-20 MHz) — proves excellent tracking is achievable with well-optimized time-domain algorithms
