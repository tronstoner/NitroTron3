# Multi-Dip YIN Pseudo-Polyphonic Tracking — Post-Mortem

**Status:** Abandoned 2026-07-21 after audition. Judged a total failure by ear.
**Archive:** full implementation lives on the parked branch
`feature/multi-dip-yin-tracking` (final commit `d4008da`, guardrails-off raw
state). Discovery/decision docs (`MULTI_DIP_TRACKING.md`) exist only there.
Nothing was merged to `main` except the Daisy-bootloader flash workflow and
unrelated tunings that happened to land on that branch.

## What was attempted

Extend the mono YIN tracker to pseudo-polyphony by scanning the **full lag
range** every hop and extracting up to 3 dips (deepest-first, deduped,
sub-octave aliases rejected) instead of taking the first dip under threshold
(M.1). A `PolyVoices` matcher (M.2) turned raw per-hop dips into "stable"
voices: hop-to-hop nearest matching within a semitone window, spawn/kill
salience hysteresis, slew-limited pitch glide, LP-smoothed salience → gain,
attack/release fades, release-hold with revival through transients. Voices
were wired into the Mode C synth and the Mode A octave-locked drone (M.3+M.4).

## Why it failed

1. **Raw multi-dip candidates are inherently unstable.** Secondary YIN dips of
   real bass/guitar chords wander, swap ranks, alias between octaves, and beat
   against each other hop to hop. The dip extractor never delivered candidates
   clean enough to sound like notes.

2. **The matcher's guardrails were the wrong cure.** To mask candidate
   instability, M.2 piled on continuity machinery: hysteresis, slew, smoothing,
   fades, release-hold. That machinery **violated boundaries this project had
   already settled through hard ear-tuning: no slew, no fades, no release.**
   The result was the worst of both worlds — still unstable, now also laggy
   and mushy ("absolutely unstable, unusable and laggy").

3. **No tuning point existed between the failure modes.** Loosening the
   guardrails (spawn 0.30→0.15, kill 0.20→0.08, match ±1→±3 st, salience LP
   50→15 ms) traded lag for churn. Turning them **fully off** (slew/attack/
   release/smoothing deactivated, raw dips straight through) exposed the bare
   engine — and it still "did everything not wanted." The failure is in the
   candidate quality (point 1), not in matcher tuning, so no constants bundle
   could have saved it.

## Lessons for future pitch/poly work

- The mono tracker's contract — instant, direct, no smoothing between
  detection and sound — is a **hard project boundary**, not a tunable. Any
  design whose stability depends on slew/fades/release-holds is dead on
  arrival; it must be stable at the candidate level first.
- Auditionable early: the guardrails looked reasonable on paper (all decided
  in the discovery round, `ef462da`) but the very first audition condemned
  them. Prototype the raw detector output before building continuity layers
  on top.
- If polyphonic detection is revisited, the effort belongs in **better
  candidates** (e.g. spectral methods, harmonic-product, per-string tricks),
  not in post-hoc voice management of noisy YIN dips.

## Superseded by

Mode C SW1=MID direction moves from the Tube Screamer→amp chain to a **POG
simulation** (polyphonic octave generator) — polyphony via octave shifting of
the full signal rather than per-note tracking.
