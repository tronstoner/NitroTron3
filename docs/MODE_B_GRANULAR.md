# Mode B — Sprawl — Spec

Granular texture/soundscape engine for bass, inspired by the Chase Bliss Mood's
"micro-looper as collaborator" idea. A live 8 s rolling buffer of the input is
re-emitted as grains, pitch-shifted in harmony with the tracked bass note, and
can be fed back into itself to sustain into drone territory.

This doc reflects **shipped behaviour**. `src/constants.h` (Mode B block) and
`ProcessGranular` in `src/NitroTron3.cpp` are the source of truth; the README
and `docs/USER_MANUAL.md` carry the player-facing control tables.

---

## Signal Chain

```
Input ──┬───────────────────────────────────────────────► [Mix K6] ──► Output
        │                                                    ▲
        ├──► [EnvFollower] ──► note-on detect + duckers      │
        ├──► [PitchTracker] ──► harmony logic                │
        │                                                    │
        └──► [Ring Buffer, 8 s SDRAM] ──► [Grain Scheduler]
                  ▲                            │
                  │                      [Grain Voices × 8]
                  │                            │
                  │                      [Texture Shaper (SW1 / K4)]
                  │                            │
                  │                      [Freq Shifter (SW2=DOWN only)]
                  │                            │
                  │                            ├───► [Clouds Reverb @ 32 kHz] (K5 CCW)
                  │                            │
                  │   [Build-up ducker] ◄──────┤
                  │   [On-play ducker] ◄── EnvFollower
                  │            │               │
                  └── [120 Hz HPF → tanh saturator, K5 CW feedback] ◄────────┘
```

At **K2 noon** (direct-texture), grains read at ~0 delay, so a forward-unison
grain is effectively a live passthrough into the texture shaper; K3 still
applies (CW = micro-stutter). See "Direct-Texture Mode".

---

## Block Descriptions

### Ring Buffer
Mono, 8 s at 48 kHz, in SDRAM, continuously overwritten. The K5-CW feedback
return is injected into the write path, so feedback becomes new grain material.

### Grain Scheduler
Emits grains on a timer: `base_interval = grain_len / overlap`, where `grain_len`
is set by K3 (× the K2 `k2_scale` timescale) and `overlap` by the neutral anchor
(denser for the SW2-MID echo bloom). Each grain gets a read start behind the
write head (K3-CW scatter; ~0 at noon/cloud), a length, a Tukey window, a
direction (K2 sign, with occasional K3-CW flips), a pitch ratio, and a gain =
pitch compensation `1/sqrt(ratio)`. Voice allocation scans for an **inactive**
voice and never steals a live one — at the 8-voice ceiling, new grains are
dropped rather than cutting one off.

A note-on (rising envelope edge) fires an extra grain **burst** anchored to the
freshly-played note on the K3-CW glitch side, so the engine answers your attack.

### Grain Voice
Reads the ring at a variable (signed) rate for pitch shift / reverse, windowed by
an adaptive Tukey window (full Hann is the smooth-grain special case). Supports
length-coupled looping for the K3-CW stutter.

### Pitch & Harmony (SW2)
Each grain's pitch ratio is computed at emission from the `PitchTracker` MIDI
output plus the SW2 mode:
- **UP — fixed interval**: every grain K1 semitones above the tracked note (±12).
- **MIDDLE — Harmonic cloud**: grains lock onto randomly-picked nearby harmonics; K1 spans a ±36-semitone scan. The pick re-rolls every grain across
  neutral + CW, holding longer as K3 goes CCW.
- **DOWN — Bode SSB frequency shifter** on the wet bus, inside the feedback loop
  (each pass cascades the shift). Grain buffer-read pitch is forced to unison, so
  K1 is only the shift amount (±1 kHz, exponential).

### Texture Shaper (SW1 / K4)
One stage, three modes; only the selected one runs. K4 is **bipolar (noon =
clean)** for UP and MIDDLE, unipolar for DOWN:
- **UP — decimator (CCW) / wavefolder (CW)**.
- **MIDDLE — event-driven digital glitch** (`GlitchEvents`): CCW = random XOR
  bit-flip events, CW = random timing events (freeze / short-loop stutter /
  reverse). Env-gated; a note-on forces an event; a reserved travel zone near
  noon fires only on note-ons before auto events ramp in. See
  `docs/MODE_B_TEXTURE_IDEAS.md`.
- **DOWN — ringmod**: K4 0–30 % = tremolo (1–15 Hz), 30–100 % = stepped bell
  partials, pitch-tracked with a keytracked LPF.

### K5 — Reverb (CCW) / Feedback (CW), bipolar, ±5 % deadzone
- **CCW — Clouds reverb** (0 → 1): vendored MI Clouds wet-path reverb (Griesinger
  topology), native 32 kHz via a polyphase 48↔32 resampler, mono in / stereo out
  collapsed to mono at the final mix (one removable line for future stereo).
  Amount smoothed to kill zipper. Runs on the wet path, in parallel with — not
  into — the feedback; the tail does **not** feed the ring buffer.
- **CW — ring-buffer feedback** (0 → `FEEDBACK_MAX` = 2.0): the wet output, passed
  through a 120 Hz HPF and a `tanh` saturator (`FB_SAT_DRIVE = 8`), is added to
  the ring write. Early saturation plus two duckers (build-up on the loop level,
  on-play on the dry envelope) keep it a controlled drone rather than a runaway —
  the 2.0 gain is a raw pre-saturation drive, not a linear ceiling.

### Mix (K6)
Equal-power dry/wet crossfade.

### Direct-Texture Mode (K2 noon)
Within the ±6 % K2 deadzone the grain read-back depth is ~0, so the grain engine
tracks the live input (functional passthrough) into the texture shaper. K1 is
irrelevant, K3-CW gives micro-stutter on the live signal, and K5-CW feedback
still pre-loads the ring so turning K2 up reveals a buffer of processed material.
Crossing out of the deadzone resumes normal buffered grain behaviour.

---

## Controls — Normal Mode

| CONTROL | DESCRIPTION | NOTES |
|---|---|---|
| KNOB 1 | Pitch | Meaning follows SW2. UP: fixed interval, ±12 semi. MID: harmonic-cloud pick, K1 spans the ±36-semi scan. DOWN: Bode SSB shift, bipolar ±2 % deadzone, ±1 kHz exponential; grain pitch forced to unison |
| KNOB 2 | Buffer | Noon (±6 %) = direct-texture passthrough. Off noon = buffer depth 100 ms → 8 s + timescale (`k2_scale` 0.5×–2×); **sign = playback direction** (CW forward, CCW backward) |
| KNOB 3 | Character | Noon (±6 %) = single coherent stream. **CCW = cloud**: grains grow ~0.3 → 2 s (× K2 timescale), overlap held so the emission rate falls — a long slow granular multi-tap smear on the deep buffer; pitch re-rolls every grain (SW2 MID). **CW = glitch**: per-grain length variation (audio-rate stutter buzzes), scatter, jitter, random reverse, note-on bursts |
| KNOB 4 | Texture | Bipolar (noon = clean) for SW1 UP/MID, unipolar for DOWN. Intensity of the SW1 texture mode |
| KNOB 5 | Reverb / Feedback | CCW = Clouds reverb (0→1). Center (±5 %) = off. CW = ring-buffer feedback (0→`FEEDBACK_MAX`=2.0), tanh-saturated + ducked. Reverb tail does not enter the ring buffer |
| KNOB 6 | Mix | 0 = dry, 1 = wet. Equal-power curve |
| SWITCH 1 | Texture | **UP** - Decimator (CCW) / Wavefolder (CW)<br/>**MIDDLE** - Event-driven digital glitch (XOR CCW / timing CW)<br/>**DOWN** - Ringmod |
| SWITCH 2 | Harmony | **UP** - Fixed interval<br/>**MIDDLE** - Harmonic cloud<br/>**DOWN** - Bode SSB frequency shifter |
| SWITCH 3 | Mode select | **UP** - Mode A (Bordun)<br/>**MIDDLE** - Mode B (Sprawl — this mode)<br/>**DOWN** - Mode C (Schism) |
| FOOTSWITCH 1 | Preset | Short: cycle presets / reload if edited. Save mode: cycle target slot |
| FOOTSWITCH 2 | Bypass / Save | Short: bypass. Long: save mode. See `PROJECT.md` Preset System |

---

## LEDs

| LED | DESCRIPTION |
|---|---|
| LED 1 (left) | Preset indicator (see `PROJECT.md`). Manual mode: grain activity |
| LED 2 (right) | Bypass state; edited/save-mode/confirm blinks (see `PROJECT.md`) |

---

## Design Rationale

**Two-layer pitch (continuous tracker + per-grain harmony):** tying every grain
to the tracked note makes the pedal feel like it's improvising in your key rather
than harmonising or making noise.

**K3 as one bipolar character axis:** noon is a clean coherent stream; the two
halves are opposite personalities — a long slow smear cloud (CCW) that leans on
the deep buffer, and an audio-rate glitch (CW). One knob, two clear identities.

**Feedback injects into the ring, not the wet output:** the pedal re-grains what
it hears — subtle at low feedback, self-sustaining drone at high. The 120 Hz HPF
on the return keeps sub content from accumulating in the loop.

**Reverb and feedback share K5 (bipolar):** they are mutually-exclusive ways to
thicken the wet, so one bipolar knob is cleaner than two.

---

## Component Reuse from Mode A

- `EnvFollower` — note-on detection and the duckers.
- `PitchTracker` — continuous MIDI for grain harmony.

## References

- **Chase Bliss Mood** — "micro-looper as collaborator".
- **Qu-Bit Stardust** — Daisy Seed granular precedent.
