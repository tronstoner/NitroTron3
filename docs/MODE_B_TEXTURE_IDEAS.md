# Mode B — Texture Ideas

## Decided design for SW1 MIDDLE — Zoned Digital Glitch

K4 is bipolar with noon = clean. Walking CCW progressively corrupts the
signal by XOR'ing bit positions of increasing significance; walking CW
progressively corrupts via right-rotation of an increasing number of
bits. Matches Mode B's K1 noon-centered "more-effect-away-from-center"
convention.

Replaces the previous chorale formant filter on SW1 MIDDLE, which was
too uncontrolled / peaky / resonant to be musically useful on bass.

### Zone layout

| K4 | Zone | Mechanism | Character |
|---|---|---|---|
| 0.00 – 0.25 | **Squelch** (CCW extreme) | XOR bits 4–10 | overt data corruption |
| 0.25 – 0.45 | **Hash** | XOR bits 0–3 | subtle digital grain |
| 0.45 – 0.55 | **Dead** (noon) | clean | ±5% center detent |
| 0.55 – 0.75 | **Wobble** | bit-rotate right, 1–6 positions | amplitude-coupled |
| 0.75 – 1.00 | **Broken** (CW extreme) | bit-rotate right, 6–14 positions | broken-data extreme |

The "zones" are descriptive labels for the perceptual character of
adjacent knob ranges — the underlying mapping is monotonic in `|K4-0.5|`
within each side (XOR or rotate), so the knob always feels like
"more = more broken."

### Effective-position calculation

```
magnitude   = |K4 - 0.5| * 2                          // 0..1
side        = (K4 < 0.5) ? XOR : ROTATE
effect_pos  = max(0, (magnitude - deadzone) / (1 - deadzone))
```

The deadzone (±5%) snaps to clean. Outside it, `effect_pos` drives the
bit position (XOR side) or rotation count (rotate side) linearly from
the deadzone edge to the knob extreme.

### Per-sample crossfade

To avoid clicks when the knob walks across integer bit-position or
rotation-count thresholds, the output is a linear blend between the two
adjacent integer values:

```
bit_f      = effect_pos * 10                          // walks 0..10
bit_lo     = floor(bit_f)
frac       = bit_f - bit_lo
out        = lerp(XOR(in, 1 << bit_lo),
                  XOR(in, 1 << (bit_lo + 1)),
                  frac)
```

Same pattern for rotation (`rot_f` walks 1..14).

### Mechanics

- Float sample → q15 int16 → XOR or right-rotate raw 16-bit → int16 → float.
- XOR mask never touches the sign bit (bit 15); max bit flipped is 10.
- Rotation is full 16-bit; sign-bit movement at high rotation counts is
  part of the "broken" character at the CW extreme.
- Stateless. ~6 integer ops per sample plus two float↔int casts. No
  per-block work other than computing `side`, `effect_pos`, and an
  env-scaled magnitude.

### Gesture coupling

Amplitude-driven (per spec for SW1 MIDDLE — same gesture driver as the
spec'd wavefolder). The env follower scales `magnitude` (not K4
directly): soft notes pull toward clean, hard hits drive toward whichever
extreme K4 is pointing at. Compile-time depth = 0.6 — at env = 0 the
effective magnitude is 0.4× knob position, at env = 1 it reaches full
knob position.

### Constants (live in `src/constants.h`)

```
GLITCH_DEADZONE     = 0.05f    // ±5% around noon → clean
GLITCH_XOR_MAX_BIT  = 10       // highest bit flipped at full CCW
GLITCH_ROT_MAX      = 14       // max right-rotation at full CW
GLITCH_ENV_DEPTH    = 0.6f     // env share of effective magnitude
```

### Open questions for listening (when pedal is back)

- Whether Hash and Squelch read as two distinct sub-zones or as one
  smooth XOR ramp. Same question for Wobble vs Broken on the rotate side.
- Whether `GLITCH_ENV_DEPTH = 0.6` makes the effect too sleepy at low
  picking dynamics. Tune by ear.
- Whether sign-bit rotation clicks read as "musical broken data" or as
  unwanted artifact at the CW extreme. If unmusical, restrict rotation
  to the bottom 15 bits.

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
