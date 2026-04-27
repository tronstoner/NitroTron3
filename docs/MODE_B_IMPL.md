# Mode B — Sprawl — Implementation Plan

Staged implementation of Mode B per `MODE_B_GRANULAR.md`. Each stage produces a flashable, testable build.

---

## B1 — Ring buffer + single grain voice

- Ring buffer: mono, 8 seconds at 48 kHz, in SDRAM
- One grain voice: reads from ring buffer with Hann window envelope
- No pitch shift (playback rate = 1.0)
- Fixed grain size (~100 ms) and fixed read offset behind write head
- K6 = dry/wet mix (equal-power)
- All other knobs/switches ignored

**Test:** flip Switch 3 to Mode B. You should hear a delayed, windowed copy of your input mixed with dry.

---

## B2 — Grain scheduler + multiple voices

- Grain scheduler emits grains at a configurable rate
- K2 (centered) = density/size: CCW = long sparse, noon = medium (~100 ms, 10 Hz), CW = short dense
- K3 = scatter: position jitter behind write head, timing jitter, reverse probability
- 8 grain voices, round-robin allocation with oldest-steal
- Envelope follower running (reuse from Mode A) for per-grain amplitude scaling

**Test:** playing bass produces a cloud of scattered grain fragments. K2 and K3 audibly reshape the texture.

---

## B3 — Pitch shifting + harmony

- Variable-rate grain playback (fractional read increment for pitch shift)
- Wire pitch tracker into Mode B (already shared, just needs to run in main loop for Mode B too)
- Switch 2 = harmony mode:
  - UP: fixed interval (K1 semitones above tracked note)
  - MIDDLE: scale-quantized random (minor pentatonic, weighted toward K1)
  - DOWN: harmonic cloud (random from harmonic series of tracked note)
- K1 (centered) = interval: noon = unison, CCW = -24 semi, CW = +24 semi

**Test:** grains pitch-shift to follow the bass. Switch 2 changes the harmonic character. K1 sets the interval.

---

## B4 — Gesture analysis + shaper bus

- Onset detection from envelope follower (rising threshold + refractory period)
- Gesture signals: amplitude (direct), attack sharpness (peak rise rate), sustain (time since onset)
- Switch 1 = texture mode:
  - UP: decimator (sample-rate reduction), attack-driven
  - MIDDLE: wavefolder (reuse from Mode A), amplitude-driven
  - DOWN: ringmod (carrier = tracked bass x inharmonic ratio), sustain-driven
- K4 = texture amount (0 = clean, gesture modulates toward ceiling)
- Only the selected texture processor runs

**Test:** Switch 1 changes the tonal character. K4 dials in intensity. Playing style (hard pluck vs smooth sustain) audibly shapes the effect.

---

## B5 — Feedback bus + wet HPF

- Feedback path: shaper output -> fractional delay (~180 ms) -> MoogLadder (fixed ~1.2 kHz) -> wavefold (fixed drive) -> inject into ring buffer write path
- Safety HPF ~100 Hz in feedback path (non-negotiable for bass)
- K5 (centered) = feedback amount: CCW = negative (phase-inverted), noon = off, CW = positive (can sustain/drone). Ceiling at +/-0.95
- Wet HPF: 2-pole high-pass at ~150 Hz on shaper output before mix

**Test:** K5 CW builds feedback into sustained textures. K5 CCW gives tight, de-correlated character. No sub-frequency runaway.

---

## B6 — Integration + polish

- Verify all 6 knobs + Switch 1 + Switch 2 are wired and responding
- LED 1 in manual mode: grain activity (brightness proportional to instantaneous density)
- Final gain staging against Mode A (comparable output levels)
- Update README controls table for Mode B
- Update PROJECT.md status

**Test:** full Mode B is playable. Switch between Mode A and Mode B — levels match, preset system works across both modes.
