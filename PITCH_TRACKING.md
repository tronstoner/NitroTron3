# Pitch Tracking — Plan & Research

## Current Implementation

YIN pitch tracker (`pitch_tracker.h`):
- 4x decimation (48 kHz → 12 kHz) with 4-pole anti-alias LP at 400 Hz
- 2-pole HP at 25 Hz for DC blocking
- YIN difference function with cumulative mean normalization (threshold 0.15)
- Early termination on first dip below threshold
- Ring buffer 1024 samples, analysis window 400, hop 64 (~5.3 ms updates)
- Envelope gating — skips tracking during silence
- Heavy computation (RunYin) runs in main loop, not audio callback
- Output quantized to nearest MIDI semitone
- Phases 1–2 complete.

### Known Limitations

- Glitches at note beginnings — YIN needs ~2 periods before reliable detection
- No note-bend tracking — output is quantized to semitones (fine for octave-locked mode, limiting for direct tracking)
- Harmonic confusion still possible on some notes despite 400 Hz LP

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

### Lower the LP cutoff
- Current: 150 Hz. Consider 100–120 Hz for better harmonic rejection.
- At 120 Hz with 4 poles (24 dB/oct), the 2nd harmonic of low E (82 Hz) passes, 3rd harmonic (123 Hz) is at -3 dB, higher harmonics well attenuated.

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
- [ ] Parabolic interpolation for sub-sample accuracy (skipped — not needed for semitone quantization)

### Phase 3 — Onset Detection
- [ ] Detect envelope attack (rising edge above threshold)
- [ ] On onset: use first clean zero-crossing cycle for initial estimate
- [ ] Refine with YIN once 2 periods available
- [ ] Evaluate latency improvement — should fix glitches at note beginnings

### Phase 4 — Continuous Pitch for Direct Tracking
- [ ] Add unquantized pitch output to PitchTracker (raw MIDI note as float, no roundf)
- [ ] Apply smoothing to raw pitch to avoid jitter without killing bends
- [ ] Direct tracking mode (Switch 2 down) uses continuous pitch — follows bends
- [ ] Octave-locked mode (Switch 2 middle) stays quantized to semitones

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
