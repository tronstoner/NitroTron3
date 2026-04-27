# Stutter / Glitch Mode — Implementation Guidance

Quick reference for debugging crackles and building a click-free stutter engine.

## Diagnosis First

Crackles in a stutter engine are almost never an envelope-tuning problem. They're **signal discontinuities**, and there are exactly four places they come from:

1. **Sample-value jumps at loop points** — stop reading at position B (non-zero value), jump to A (another non-zero value). One-sample step = full-bandwidth click. An outer envelope can't fix a discontinuity that's already *inside* the waveform.
2. **Slope discontinuities** — linear fades and ADSR ramps reach zero amplitude but not zero slope. Hann windows work because both endpoint *value* and *slope* are zero. This is why `line~` / `adsr~` fail on transients in the Max forum threads.
3. **Mid-grain parameter updates** — if `length`, `speed`, `start`, or `reverse` can mutate while a slice is playing, the read index jumps. Parameters must be **latched at slice start** and held immutable until the slice ends.
4. **Read/write-head interference** — live writes colliding with stuttered reads on the same buffer region. Fix: freeze the write head when stutter engages; resume on disengage.

## Core Architecture — Two-Voice Crossfade (Gen~ Chucker Pattern)

The canonical solution from the Max/MSP community and every shipping stutter plugin.

- **Two playback voices**, ping-ponging. At any moment, up to two voices are active.
- Voice A plays current slice through a Hann envelope. Before A's envelope reaches zero, voice B starts ramping up on the next slice — overlap 5–20 ms.
- New slice parameters (length, speed, direction, start position) are picked at voice start and latched for the slice's lifetime.
- Envelopes sum to ~1.0 across the crossfade region. Use √Hann on each voice for equal-power overlap.

### CRITICAL: Window shape and overlap placement

**Use a Tukey window (flat middle, cosine taper at edges), NOT a full Hann
over the entire slice.** A full Hann means the slice is at full volume only
at the midpoint — the rest is faded. This makes reps sound too short and
changes the stutter character entirely.

The correct approach:
- **Tukey window**: 5 ms cosine taper at each edge (240 samples at 48 kHz),
  flat 1.0 in the middle. Each rep plays at full volume for nearly its
  entire duration.
- **Overlap at the TAIL, not the midpoint.** Voice B starts during voice A's
  5 ms tail taper (`phase >= length - TAPER_LEN`). This is a brief handoff,
  not a half-chunk blend.
- "Overlap 5–20 ms" means the crossfade region is 5–20 ms total, placed at
  the end of each voice. It does NOT mean voice B starts at voice A's
  midpoint.

**This mistake has been made twice.** Do not repeat it. The midpoint-overlap
full-Hann approach produces a completely different sonic character (shorter
perceived reps, volume pumping) and causes event-boundary clicks.

**Structurally this is the same as the Mode B (Sprawl) grain scheduler.** A stutter slice is just a grain with coarser parameters and deterministic scheduling. **Reuse the Mode B grain engine rather than writing a parallel stutter subsystem.** Add a "slice mode" scheduler policy alongside the existing "cloud mode."

## Rules the Engine Must Follow

### Grain / slice boundaries
- Every slice is windowed. Hann is the default (Tukey or Gaussian are acceptable alternatives; rectangular is never acceptable).
- Minimum ramp of **3–5 ms on each side** (~144–240 samples at 48 kHz). Shorter and the window itself becomes audible as a click.
- For very short slices (< 20 ms), use Tukey with a small flat region, or accept the slice is a windowed transient.

### Parameter latching
- At slice start: snapshot `length`, `speed`, `start_position`, `reverse`, `pitch` onto the voice struct. Do not read them again until the voice requests its next slice.
- Randomness happens at snapshot time. `rng.next()` fires once per slice, not per sample.

### Write-head behavior
- Intensity = 0: buffer writes continuously, reads track the write head at unity → passthrough.
- Intensity > 0: **freeze writes** the moment a slice is captured. The stutter operates on a static snapshot.
- Intensity → 0 again: resume writes and crossfade the final stuttered slice into the live signal.

### Interpolation
- Non-unity playback speed requires fractional read indices with linear interpolation minimum. Integer reads at fractional speeds click every few samples. Same rule as Mode B grain voices.

## Gradual Engagement (the "increase amount and intensity" goal)

Do **not** gate stutter on/off. Scale three continuous parameters off the intensity knob:

1. **Per-slice repeat probability** — at intensity 0, every slice is passthrough (length = natural, speed = 1, forward). At intensity 1, every slice is a manipulation.
2. **Randomization depth** — range of length jitter, speed jitter, reverse probability all scale from 0 to full at intensity 1.
3. **Per-voice wet/dry blend** — even at probability 1, blend the stutter voice with live signal for "gentle intrusion" territory.

This mirrors Mode B's Scatter (K3) semantics. Probably reuse that pattern.

### Intensity parameter smoothing
- Slew-limit the intensity scalar (~20 ms) so parameter sweeps don't step mid-block.
- If the pedal clicks only during intensity ramping, this is the likely cause.

## Crossfade Curves

- Voice-to-voice handoff: √Hann envelopes → equal-power overlap, constant perceived loudness.
- Wet/dry mix: equal-power (cos/sin pair), not linear. Linear dips ~3 dB at the midpoint.

## Debugging Order (when clicks appear)

Each step isolates a different cause. Do not skip.

1. **Kill all randomness.** Fixed length, speed = 1.0, no reverse, fixed start offset. If clicks disappear → latching bug. If they remain → voice engine bug.
2. **One voice, no crossfade, Hann-windowed repeat only.** If this clicks, the window is wrong — wrong shape, wrong length, or applied wrong. Dump first 200 output samples to serial and plot.
3. **Add second voice with crossfade.** If step 2 was clean and this clicks, crossfade timing is wrong (voice B late, or envelopes don't sum to ~1).
4. **Re-introduce randomness one parameter at a time.** Whichever one clicks = bad latching on that parameter.
5. **Add gradual engagement last.** If clicks only appear during intensity ramping, slew-limit the intensity scalar.

## Key References

- **Curtis Roads — *Microsound* (MIT Press, 2001)** — canonical granular synthesis reference. Already cited in `MODE_B_GRANULAR.md`.
- **Truax — "Real-time granular synthesis with a digital signal processor"**, Computer Music Journal 12(2), 1988 — foundational real-time granular paper.
- **Max/MSP `gen.chucker~`** — reference implementation of live-input stutter with crossfade. Ships in Max's Gen~ examples. Readable Gen code.
- **Mutable Instruments Clouds source** — https://github.com/pichenettes/eurorack/tree/master/clouds — production-quality C++ granular processor. `granular_processor.cc` and `grain.h` show the windowing, voice pooling, and parameter latching patterns cleanly. **Single most useful code reference for this task.**
- **Qu-Bit Stardust** — Daisy-based granular, same SDRAM + ring-buffer pattern on the exact hardware.
- **Cycling '74 forum thread** — `cycling74.com/forums/how-do-i-create-an-envelope-for-a-stutter-effect-with-buffer-and-play~` — the double-buffer-with-crossfade answer.

## Anti-Patterns to Avoid

- Adding bigger envelopes to fix clicks. Clicks inside the waveform can't be windowed away.
- Using linear fades or ADSR with linear segments on grain boundaries.
- Reading parameters directly from knobs inside the voice's per-sample loop.
- Letting writes continue into the buffer region being stuttered.
- Single-voice architecture with a hard jump at slice boundaries, then trying to mask the jump with a short fade-in.
- Fixing each click locally with a steeper ramp. The fix is structural: two voices, hard latching, Hann overlap.
- **Full Hann window over the entire slice with midpoint overlap.** This is the most common misreading of the two-voice architecture. A full Hann means the slice is only at full volume at its midpoint — reps sound half their actual length, the character is wrong, and event boundaries click. The correct window is **Tukey** (flat middle, short cosine taper at edges). The overlap is 5–20 ms at the **tail**, not at the midpoint. This mistake was made twice in this project (commits `9bac01b`, `e93ac1c`).
