# Stutter Mode — Behavioral Spec & Implementation Plan

## Purpose

Recreate the stutter behavior from commit `62a0971` (the "good sound") using the
click-free two-voice Hann crossfade architecture described in
`docs/STUTTER_IMPLEMENTATION_NOTES.md`.

The original implementation had the right *feel* but used linear fades and
buffer-modification hacks that caused clicks. This spec captures the sonic
behavior as a target and pairs it with the correct engine architecture.

## Target Behavior (derived from 62a0971)

K3 is a single "chaos" knob. Low = occasional long repeats, high = rapid
glitchy stutter with cut-outs and reverse.

### Parameters

| Parameter | Value | Scaling with K3 |
|-|-|-|
| Chunk length | 200 ms (CCW) → 50 ms (CW) | Linear, randomized ±40 % per event |
| Event trigger | Probability per sample: `k3² / 1440` | Quadratic — wide usable range before chaos |
| Reps per event | 1 at low K3, 1–5 at full CW | `1 + rand * (1 + k3 * 4)` |
| Cut-out probability | 0 % (CCW) → 40 % (CW) | Linear |
| Cut-out duration | 10–50 ms | Random within range |
| Reverse probability | 0 % (CCW) → 60 % (CW) | Linear |
| Capture buffer | 200 ms (9600 samples at 48 kHz) | Fixed |
| Min chunk length | 50 ms (2400 samples) | Hard floor |
| Write-head | Freezes during event, resumes after | — |

### Dry/wet transition

Complement crossfade: during the first voice's ramp-in, dry fades out
proportionally. During the last voice's ramp-out, dry fades back in. No
hard switch between dry and wet ever.

## Engine Architecture (from STUTTER_IMPLEMENTATION_NOTES.md)

### Two-voice Hann crossfade

- Two playback voices, ping-ponging.
- Each voice plays its slice through a full Hann envelope.
- Before voice A's envelope reaches zero, voice B starts on the next rep
  — overlap region keeps the sum ≈ 1.0.
- Parameters (length, start, reverse) latched at voice start, immutable
  for the slice's lifetime.
- Minimum 3–5 ms ramp per side (~144–240 samples).

### Cut-outs

Cut-outs are silence events. They still use the two-voice architecture:
voice output is zero (or near-zero), so the complement crossfade ducks
the dry signal smoothly for the cut-out duration.

### Write-head

- When no event is active: capture buffer writes continuously.
- When event triggers: freeze writes. Stutter operates on a static snapshot.
- When event ends: resume writes.

## Implementation Steps

Each step is a separate build → flash → listen cycle. Do not advance until
the current step is click-free. One commit per step.

### Step 1 — Single voice, fixed params, no randomness

- Separate `stutter_buf[]` with continuous write when idle.
- Single voice with **Tukey window** (5 ms cosine taper at edges, flat middle).
- Fixed chunk length (e.g. 100 ms), speed 1.0, forward, no cut-outs.
- K3 > threshold triggers one event, waits for it to finish.
- Complement crossfade: `dry * (1 - window) + voice`.
- **Goal:** click-free single-voice playback with smooth dry transitions.

### Step 2 — Two-voice crossfade

- Add second voice. Voice B triggers during voice A's **tail taper**
  (`phase >= length - TAPER_LEN`), NOT at the midpoint.
- Use `>=` with a "triggered next" flag, not exact `==` match.
- Sum both voices; complement crossfade uses combined envelope.
- **CRITICAL:** overlap is 5 ms at the tail, not half the chunk.
  See `STUTTER_IMPLEMENTATION_NOTES.md` § "Window shape and overlap placement".
- **Goal:** click-free multi-rep playback, smooth overlap.

### Step 3 — Multiple reps per event

- Add rep counter. Each new rep triggers the next voice on the same
  latched chunk.
- Last rep's voice finishes naturally (no next voice triggered),
  complement crossfade returns to dry.
- **Goal:** 1–N reps sound clean, event boundaries are seamless.

### Step 4 — Periodic random events

- Add per-sample probability trigger (`k3² / 1440`).
- Randomize chunk length (base ±40 %), rep count (`1 + rand*(1+k3*4)`).
- **Goal:** random events fire and sound correct.

### Step 5 — Reverse

- Add reverse probability (linear with K3, max 60 %).
- Latch direction at voice start.
- **Goal:** reverse slices are click-free.

### Step 6 — Cut-outs

- Add cut-out probability (linear with K3, max 40 %).
- Cut-out = event where voice output is silence, 10–50 ms duration.
- Complement crossfade ducks dry signal smoothly.
- **Goal:** cut-outs feel like rhythmic gating, no clicks.

### Step 7 — Polish

- Verify full K3 range: gentle at CCW, chaotic at CW.
- Tune any constants that don't match the original feel.
- Slew-limit K3 if parameter sweeps click (~20 ms).

### Future — Gesture-reactive stutter

Make event density react to the input envelope: harder playing = more
erratic stutter, quiet passages = sparser events. The envelope follower
(`grain_env`) already exists. Scale event rate or probability by envelope
level for dynamics-responsive behavior. This ties the stutter to the
player's feel rather than just the knob position.
