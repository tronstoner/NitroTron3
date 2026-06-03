# Mode B — Texture Ideas

## Decided design for SW1 MIDDLE — Event-Driven Digital Glitch

K4 is bipolar with noon = clean. Glitching is **event-driven** rather
than continuous: random triggers fire at a rate proportional to K4
magnitude, and each event picks random payload parameters. K4 alone
controls density and intensity — no envelope coupling. Buchla
Source-of-Uncertainty model: timing randomness in *when* things happen,
not white noise in *what* comes out.

Replaces the prior stateless per-sample XOR/rotate zone design, which
was inaudible on the CCW (XOR) side and decoupled-from-input on the CW
(rotate) side.

### Constraints honoured

1. **K4 alone controls the effect.** Density (event rate) and chain
   probability (continuous vs sparse) both scale with K4 magnitude.
   Wet-mix envelope coupling was tried and dropped — it muffled the
   effect during note release in a way that felt irritating rather
   than musical. The envelope follower is now used only as a hard
   noise gate on event arming (see "Noise gate" below), not as a
   continuous wet-level modulator.
2. **Timing randomness.** All event timing (when triggers fire, event
   duration, per-event payload parameters) is randomised — the same
   knob position never produces the same glitch twice.
3. **Continuous at the extreme.** At full K4, event chaining drives
   the processor to fire back-to-back events so the dry signal is no
   longer audible between glitches.

### Per-side payload

| Side | Knob travel | Payload | Behaviour |
|---|---|---|---|
| **CCW** | 0.5 → 0.0 (away from noon) | **Bit-flip events** | Each event picks a random bit in `[MAX_BIT_FLOOR, MAX_BIT_FLOOR + effect_pos × (GLITCH_XOR_MAX_BIT − MAX_BIT_FLOOR)]`, XORs the wet signal with that bit for the event duration. Floor = 8 so the first click past the deadzone is already audible; linear interp up to bit 13 at full CCW. Pick is biased toward the high end of the range. |
| **CW** | 0.5 → 1.0 (away from noon) | **Timing events** | Each event randomly picks one of `{freeze, stutter loop, reverse}` and snapshots the ring buffer 1–20 ms behind the write head. Reads from that snapshot for the event duration. |

The three CW timing payloads:
- **Freeze** — hold a single sample. Glitchy silence-with-tone.
- **Stutter** — loop a 1–4 ms window. Buzzes / micro-stutters.
- **Reverse** — walk backward through the buffer. Brief retro-grade.

Selection is uniform-random per event.

### Trigger generator

Per-sample probability check, side-specific max rate (CW timing
events feel faster than CCW bit-flips at the same knob position):
```
rate_max = (side == CW) ? GLITCH_EVENT_RATE_HZ_MAX_CW : GLITCH_EVENT_RATE_HZ_MAX
rate_hz  = effect_pos × rate_max
p        = rate_hz / 48000
fire     = (env > GLITCH_ENV_GATE) && rand() < p
```

When `state == IDLE`, the gate is open, and a trigger fires,
`StartEvent()` picks the payload and event duration. Event duration's
lower bound also scales with `effect_pos` so each event is longer at
the extreme.

When an event ends, the processor chains into another event with
probability `effect_pos²` — also gated by env, so silence ends the
run cleanly. At full K4 this is ~1.0 so events fire back-to-back;
near the deadzone it is near zero so each event is isolated.

### Noise gate

The envelope follower acts as a hard gate on event *arming* only. In-flight
events always play to completion so the gate-close edge cannot click. While
`env ≤ GLITCH_ENV_GATE` no new events start and no chains continue, so a
silent input → silent output. Passive-bass `env_val` sits around 0.02–0.1
while played, so the default threshold of 0.01 opens immediately on the
first note and closes cleanly into silence.

### Wet/dry mix

```
target = (state == ACTIVE) ? 1 : 0
ramp_  → target over GLITCH_RAMP_SAMPLES   // 1 ms linear ramp
out    = lerp(dry, glitched, ramp_)
```

Full wet during events. The 1 ms ramp kills clicks at event boundaries.

### Effective-position calculation (unchanged)

```
magnitude   = |K4 - 0.5| * 2                          // 0..1
side        = (K4 < 0.5) ? CCW(bit-flip) : CW(timing)
effect_pos  = max(0, (magnitude - deadzone) / (1 - deadzone))
```

The deadzone (±5%) still snaps to clean / no triggers.

### Mechanics

- Stateful processor (`GlitchEvents` class in `src/glitch_zones.h`).
  Owns a small ring buffer for the CW payload, event state, and an
  xorshift32 RNG.
- Memory: `GLITCH_BUFFER_SAMPLES × sizeof(float)` = ~9.6 KB in regular
  RAM (50 ms ring buffer).
- Always writes input to the ring buffer (regardless of side) so the
  CW snapshot can read recent history without ramp-up.

### Constants (live in `src/constants.h`)

```
GLITCH_DEADZONE              = 0.05f   // ±5% around noon → clean
GLITCH_XOR_MAX_BIT           = 13      // bit-flip ceiling (±0.25 fs)
GLITCH_EVENT_RATE_HZ_MAX     = 25.0f   // CCW: events/sec at full effect_pos
GLITCH_EVENT_RATE_HZ_MAX_CW  = 50.0f   // CW: 2× CCW at full deflection
GLITCH_EVENT_DUR_MIN_SAMPLES = 240     // 5 ms (effect_pos raises this floor)
GLITCH_EVENT_DUR_MAX_SAMPLES = 2400    // 50 ms
GLITCH_BUFFER_SAMPLES        = 2400    // 50 ms ring buffer (CW)
GLITCH_RAMP_SAMPLES          = 48      // 1 ms wet/dry ramp
GLITCH_ENV_GATE              = 0.01f   // raw env_val — no new events below
```

Bit-floor (inside `glitch_zones.h`):
```
MAX_BIT_FLOOR = 8     // CCW lower bound — first click past deadzone is audible
```

### Open questions for listening (when pedal is back)

- Whether the per-event durations feel right or want to be punchier
  (drop max to ~25 ms) or more sustained (push max to ~100 ms).
- Whether the three CW types feel like one "messed up audio" character
  or like three distinct modes — consider K4-position weighting if
  they want zoning (e.g., freeze near the deadzone, reverse at the
  extreme).

---

## Other future ideas (not for SW1 MIDDLE)

### Self-modulating delay

Short delay line, the audio modulates its own tap position. K4 = self-mod
depth. Squelchy chaotic feedback, gets weird fast.

Small effort, chaotic and animated, very synth-like. At low settings it's
a phaser-ish thing, at high settings it goes full Throbbing Gristle.

### Spectral filter glitches

**Spectral hole-punch:** two narrow notch filters that randomly relocate
in frequency. K4 = depth / rate. Sounds like a comms link dropping bands.

**Resonant filter FM:** sweep a high-Q bandpass at audio rate using the
audio itself as the modulator. Generates inharmonic sidebands. Acid-glitch
territory.
