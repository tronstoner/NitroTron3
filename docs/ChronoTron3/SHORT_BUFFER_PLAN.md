# ChronoTron3 / vestige — Short-buffer artifacts plan

Very short loops (quick FS2 taps, short auto phrases, ~5–100 ms) click/buzz and
freeze incoherently. This is the plan to fix it. **Vestige-side only — no
changes to the shared `grain_voice.h` / `ring_buffer.h` (nitrotron3 untouched).**

## Root causes (ranked)

1. **Loop-seam discontinuity (dominant).** A captured buffer's content at the end
   rarely matches the start, so looping puts a step at the boundary. The
   wrap-guard hard-copies the loop head after the end — it copies the
   discontinuity too. On a short loop this click repeats at the loop rate → an
   audible buzz / pitched artifact.
2. **Spray incoherence.** In freeze the ±25 ms position spray exceeds a tiny
   buffer and wraps → grains jump randomly around the whole short loop → smear.
3. **Grain–buffer degeneracy.** When `grain_len ≈ L`, granular collapses — the
   window is the whole loop and overlap-add can't smooth a seam it reads through.
4. **Scheduler density.** Tiny `hop` (clamped to 32) → very high grain rate →
   pool pressure + buzzy retriggering.

## Fix

### A. Seam crossfade, overhang-based (primary — fixes #1 at every length)

Record a short **overhang** — keep writing `XF` samples *past* the record window
(manual: after FS2 release; auto: after the phrase-end). The loop stays exactly
the record window `[0, L)`, so **period `L` and timing are exact** (this is the
whole point of the overhang: the crossfade material comes from the extra tail,
not from stealing loop length).

At the seam, the wrap-guard becomes an equal-power crossfade instead of a hard
copy: the recorded overhang (the natural continuation of the loop tail) fades
**out** while the loop head fades **in** over `XF` samples. A grain crossing the
boundary hears `…tail → overhang → head` with no step.

- `XF = min(VESTIGE_SEAM_XFADE_MAX ≈ 3–5 ms, L/2)` — minimal, just enough to
  defeat the sample-level click; scales down for tiny loops.
- Only the loop's first `XF` ms are heard *blended* at the seam (we smear the
  **head**, not the attack/tail).
- Buffer-full capture (no room for overhang) falls back to the hard guard.

### B. Short-buffer param clamps (secondary — fixes #2–#4)

Below a length threshold (`L < VESTIGE_SHORT_LEN ≈ 100 ms`), applied **per-slot
at emit time** (voiced loops have independent lengths): clamp `spray` to a
fraction of `L` (so it can't wrap), pull `overlap` toward 2 (exact Hann COLA),
keep `hop` above a floor. Result: a clean overlap-add of the now-seamless short
loop.

### C. Small-buffer bypass (only if A+B aren't enough)

Below a very small length, skip the grain scheduler and play a simple crossfaded
hard loop. Extra code path; deferred unless needed.

## Implementation

- **Constants:** `VESTIGE_SEAM_XFADE_MAX` (~3–5 ms), `VESTIGE_SHORT_LEN` (~100 ms),
  spray/overlap clamp factors.
- **Record lifecycle:** the record-end triggers set `pending_len_ = L` and enter
  an overhang phase (`overhang_ctr_ = XF`); recording keeps writing for `XF`
  samples, then flags commit. `CommitRecording` uses `pending_len_` as `L` and
  copies `L + XF` (loop + overhang) so the guard can crossfade.
- **`WriteGuard`:** first `XF` guard samples = equal-power(overhang → head);
  remainder = loop repeated.
- **`EmitGrain`/`ServiceSlot`:** per-slot short-buffer clamps on
  `spray`/`overlap`/`hop`.
- **No edits to `src/core/blocks/`.**

## Testing (hardware, by ear)

- Lengths: quick FS2 tap (~10–30 ms), short auto phrase, ~100 ms, >1 s.
- Content: steady tone, hard pluck, noise.
- Check: no click/buzz at any length; freeze on a short tap = clean sustained
  tone; scan/scrub across a short buffer clean; no level bumps; loop period not
  audible as a pitch; **timing exact** (no drift vs. the record window).

## Risks / open

- Seam crossfade slightly smears the loop **head** at the seam — mitigated by a
  minimal `XF`; the tail/attack is untouched.
- `spray`/`overlap` become per-slot (small refactor from the current global).
- Overhang needs buffer room past `L`; buffer-full captures fall back to the
  hard guard (rare).
