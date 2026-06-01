# Mode B — Texture Ideas for Future Exploration

A parking lot of synthetic / glitchy / digital-error textures considered for SW1 MIDDLE (or future texture slots).

The chorale formant filter currently lives on SW1 MIDDLE but is too uncontrolled, peaky, and resonant to be musically useful — listed here are alternatives flagged for follow-up exploration.

---

## 1. Bit-mangler

XOR the audio sample's integer representation with a slow square-wave mask. Sounds like data corruption / failed UART. K4 = mask depth.

Could be paired with bit-shift / rotation: rotate the sample's bits left/right by K4 amount. Wraps the MSB onto the LSB → bizarre digital squelch.

Bipolar K4 candidate: CCW = XOR with rhythmic pattern, CW = bit-rotation. The kind of glitch that's clearly *digital* in origin.

Pure digital aesthetic, costs almost nothing, big sonic territory.

## 2. Foldback decimation

Downsample without anti-alias, then upsample. Aliases fold back into the audible range. Different from the existing decimator (which holds samples) — this is wraparound aliasing.

## 3. Self-modulating delay

Short delay line, the audio modulates its own tap position. K4 = self-mod depth. Squelchy chaotic feedback, gets weird fast.

Small effort, chaotic and animated, very synth-like. At low settings it's a phaser-ish thing, at high settings it goes full Throbbing Gristle.

## 4. Spectral filter glitches

### Spectral hole-punch

Two narrow notch filters that randomly relocate in frequency. K4 = depth / rate. Sounds like a comms link dropping bands.

### Resonant filter FM

Sweep a high-Q bandpass at audio rate using the audio itself as the modulator. Generates inharmonic sidebands. Acid-glitch territory.
