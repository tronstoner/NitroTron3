# Mode B — Sprawl — Concept Spec

Inspired by the Chase Bliss Mood pedal's "micro-looper as collaborator" philosophy, tuned specifically for bass as input. A live rolling buffer of the bass signal is continuously scattered as grains, pitch-shifted in harmony with the detected bass note, and routed through a feedback loop that can sustain into drone territory.

**The pedal is a companion, not a doubler.** The wet output sits *above* the dry bass in the frequency spectrum and rhythmically *around* it — the bassist keeps playing bass, the pedal improvises over the top.

**The pedal reacts to how you play.** Attack sharpness and sustain length, extracted from the envelope follower, modulate the tonal-shaping stage. Short-hard plucks and long-smooth notes produce different sounds from the same knob setting.

**Not** included: freeze/capture mode (always live), fixed-interval-only pitch mode (always tracked), separate tuning-mode pages (Mode B tunes via `constants.h`).

---

## Signal Chain

```
Input ──┬──────────────────────────────────────────► [Mix K6] ──► Output
        │                                               ▲
        ├──► [EnvFollower] ──► grain amplitude           │
        │                                                │
        ├──► [PitchTracker] ──► harmony logic            │
        │                                                │
        └──► [Ring Buffer, SDRAM, 8s] ──► [Grain Scheduler]
                    ▲                         │
                    │                   [Grain Voices × 8]
                    │                         │
                    │                   [Texture Shaper (SW1/K4)]
                    │                         │
                    │                    [Wet HPF 150 Hz]
                    │                         │
                    └── [Feedback K5] ◄───────┴──────────┘
```

When K2 is fully CCW (**direct-texture mode**), the grain engine is bypassed: input routes directly to the texture shaper, and K3 becomes a micro-stutter control. See "Direct-Texture Mode" section below.

The **Texture Shaper** applies tonal processing to the wet signal. Switch 1 selects one of three modes (decimator/wavefolder bipolar, clean, ringmod); K4 sets intensity.

**Feedback** is simple: post-HPF wet output scaled by K5 is added to the ring buffer write path. Feedback becomes new grain material.

---

## Block Descriptions

### Gesture Analysis

Three signals derived from the existing `EnvFollower` output:

- **Amplitude** — the env follower value directly
- **Attack sharpness** — peak rate-of-rise in the first ~20 ms after an onset, held until the next onset. Hard slap reads high, soft fingerstyle reads low
- **Sustain** — time since last onset, normalized against a "long note" threshold (around 1.5 s). Staccato reads low, held notes read high

Onset detection uses the env follower's rising threshold with a short refractory period to avoid double-triggers. Same threshold the pitch tracker already uses for gating.

### Ring Buffer

Mono, 8 seconds at 48 kHz, lives in SDRAM. Continuously overwritten — the pedal always has the most recent 8 seconds available for grain sourcing. The feedback bus injects into the write path, so feedback becomes new grain material the scheduler can re-scatter.

### Grain Scheduler

Emits new grains at a rate driven by K2 (base density), envelope amplitude, and attack sharpness (hard plucks briefly bump density). Each grain gets a pitch offset, a read start position behind the write head (scaled by K3 scatter), a duration, a windowing envelope, a reverse probability (also scaled by scatter), and a gain scaled by the envelope at emission time.

Voice allocation is round-robin with oldest-steal.

### Pitch & Harmony Logic

Each grain gets a pitch-shift ratio at emission, computed from the continuous MIDI output of `PitchTracker` (Phase 4 of `PITCH_TRACKING.md`) plus the harmony mode selected by Switch 2.

Three harmony modes:
- **Fixed interval** — every grain at K1 semitones above the tracked bass note
- **Scale-quantized random** — each grain picks a random interval from a compile-time scale (default minor pentatonic), centered on the bass, weighted toward K1's value
- **Harmonic cloud** — each grain picks randomly from the harmonic series of the bass note (octave, fifth, octave+third, double octave…)

All three follow the bass continuously — if you slide, the grains slide.

### Grain Voice

Each grain reads from the ring buffer at a variable rate (pitch shift) and is enveloped by a Hann window. Pitch-shift ratio is signed — negative ratios give reverse playback.

### Shaper Bus (Gesture-Reactive Tonal Shaping)

One stage, three modes selected by Switch 1, intensity set by K4 (unipolar), modulation driven by the gesture that musically fits each mode:

| Switch 1 | Effect | Gesture driver | Character |
|---|---|---|---|
| UP | Decimator (sample-rate reduction) | Attack | Lo-fi, aggressive, "digital grit" |
| MIDDLE | Wavefolder (reused from Mode A) | Amplitude | Smooth harmonic saturation, tonal |
| DOWN | Ringmod (carrier = tracked bass × inharmonic ratio) | Sustain | Bell-like, dissonant, develops over held notes |

**K4 = 0 always means clean.** No dedicated "clean" switch position needed.

The gesture modulation has a compile-time depth: at 0.0 the gesture is ignored and K4 controls the effect directly; at 1.0 the gesture fully gates K4. Default around 0.6 — gesture is a strong flavor, knob sets the ceiling.

Only the selected mode's processor runs. The other two are bypassed entirely.

### Feedback

Simple feedback injection: post-HPF wet output scaled by K5 is added to the ring buffer write path. Feedback becomes new grain material the scheduler can re-scatter.

- **K5** — unipolar. CCW = none, CW = max. Ceiling at 0.95 to prevent runaway.
- The wet HPF (150 Hz) runs before feedback injection, so sub content does not accumulate in the loop.
- No delay/ladder/wavefolder in the feedback path — may be added later if needed for taming or coloring.

### Wet HPF

2-pole high-pass on the shaper-bus output before mix. Compile-time cutoff around 150 Hz, no user control. Keeps the wet signal from competing with the dry bass for sub-frequency real estate. This is the single most important bass-specific tuning choice in Mode B.

### Mix

Equal-power dry/wet crossfade controlled by K6.

### Direct-Texture Mode (K2 fully CCW)

When K2 is at minimum (below ~2% travel), the grain engine is bypassed entirely. The dry input routes straight through the texture shaper (SW1/K4) and wet HPF. This gives direct access to decimator, wavefolder, and ringmod as standalone effects without any granular delay character.

In this mode:
- **K1** (interval) is irrelevant — no grains to pitch-shift
- **K3** becomes a **micro-stutter** control. A small capture buffer (~50 ms) continuously records the input. K3 controls the probability and duration of random stutter events — momentary freezes where the buffer loops a captured chunk. CCW = clean (no stutter), CW = frequent choppy stuttering. This is the "glitch" dimension applied directly to the input rather than to grain scheduling.
- **K4** (texture amount) and **SW1** (texture mode) work exactly as in grain mode
- **K5** (feedback) still injects the textured/stuttered signal into the ring buffer — useful because turning K2 back up reveals a buffer pre-loaded with processed material

The transition between direct-texture and grain mode is instantaneous. K2 slightly above the threshold resumes normal grain behavior and K3 reverts to character/glitch.

---

## Controls — Normal Mode

| CONTROL | DESCRIPTION | NOTES |
|---|---|---|
| KNOB 1 | Interval | **Centered.** Noon = unison. CCW → −24 semi, CW → +24 semi. Interpretation depends on Switch 2 (anchor for Fixed and Scale modes, cloud weighting for Cloud mode) |
| KNOB 2 | Buffer range | Unipolar. CCW = tight (100 ms, recent audio only). CW = deep (full 8 s, long trails) |
| KNOB 3 | Character / Glitch | Unipolar. CCW = soft, long, tight grains (200 ms, single pass, high overlap). CW = short, sharp, chaotic (20 ms, stutter loops, scatter, reverse probability). Currently merged from two conceptual parameters (grain character + glitch amount) — may split back to two knobs later |
| KNOB 4 | Texture amount | Unipolar. 0 = clean, 1 = full effect. Intensity of whichever texture mode Switch 1 selects. Gesture-modulated (attack / amp / sustain depending on mode) |
| KNOB 5 | Feedback | Unipolar. CCW = none. CW = max (0.95 ceiling). Post-HPF wet output re-injected into ring buffer write path — feedback becomes new grain material |
| KNOB 6 | Mix | Unipolar. 0 = dry, 1 = wet. Equal-power curve |
| SWITCH 1 | Texture mode | **UP** - Decimator (attack-driven)<br/>**MIDDLE** - Wavefolder (amp-driven)<br/>**DOWN** - Ringmod (sustain-driven) |
| SWITCH 2 | Harmony | **UP** - Fixed interval<br/>**MIDDLE** - Scale-quantized random<br/>**DOWN** - Harmonic cloud |
| SWITCH 3 | Mode select | **UP** - Mode A (Drone)<br/>**MIDDLE** - Mode B (Granular Glitch — this mode)<br/>**DOWN** - Mode C (Freq Shift, future) |
| FOOTSWITCH 1 | Preset | Short press: cycle presets (Manual → 1–5), reload if edited. In save mode: cycle target slot |
| FOOTSWITCH 2 | Bypass / Save | Short press: bypass. Long press: save mode (long press = confirm, short press = cancel). See `PROJECT.md` Preset System |

---

## LEDs

| LED | DESCRIPTION |
|---|---|
| LED 1 (left) | Preset indicator (morse-code blink, see `PROJECT.md`). Manual mode: grain activity — brightness proportional to instantaneous density |
| LED 2 (right) | Bypass on = active. Rapid flash = preset edited. Fast blink = save mode. Burst flash ~1 s = save confirmed |

---

## Design Rationale

**Why the two-layer pitch model (continuous tracker + per-grain harmony):** the pedal should *feel* like it's listening. Fixed intervals alone make it a harmonizer; random pitches alone make it noise. Tying every grain's pitch to the tracked bass note, with a chosen harmonic relationship, gives it the feeling of a musician improvising in the same key as you.

**Why three harmony modes instead of one "smart" one:** each mode has a distinct musical role. Fixed is a predictable parallel voice. Scale-quantized is melodic improvisation. Cloud is a chord or pad. One switch choice with three clear personalities is easier to play than one knob with a continuum.

**Why gesture-reactive texture instead of just a knob:** a fixed-amount effect sounds the same no matter how you play. Letting the gesture scale the effect means the pedal responds to *performance*, not just settings. A hard pluck that triggers more decimator feels like the pedal is reacting to you. This is the closest analog to the "collaborator" quality of the Mood.

**Why each texture mode has its own gesture driver:** decimator is a percussive effect, so attack-driven fits. Ringmod develops character over time, so sustain-driven fits. Wavefolder saturation scales naturally with input level, so amplitude-driven fits. Letting the user choose both mode *and* driver would be a dial-twiddling UX with no clear musical default. One natural pairing per mode is better.

**Why feedback is simple and unipolar:** the original spec called for centered bipolar feedback (negative/positive). In practice, negative feedback doesn't produce a useful sonic difference with the current grain architecture — the grains are too short and scattered for phase cancellation to create a distinct "tight" character. Simple unipolar (0 = off, CW = max) is clearer and saves complexity for constraints that might be needed later (filtering, wavefolder in the feedback path).

**Why feedback injects into the ring buffer, not the wet output:** injecting into the buffer means the pedal hears itself and re-grains what it hears. At low feedback the effect is subtle (grains of grains). At high feedback the loop self-sustains as a drone. This is a more organic behavior than traditional "stack the delays" feedback — it matches the Mood's micro-looper character.

**Why the wet HPF is baked in:** on bass, sub-frequency accumulation in the wet path muddies everything. This isn't a taste decision, it's a "the pedal sounds bad without it" decision. Keeping it fixed also saves a knob for something that's actually performance-useful. The HPF also runs before feedback injection, doubling as a safety filter for the feedback loop.

**Why direct-texture mode exists (K2 fully CCW):** with K2 at minimum and K3 at zero, the grains were already near-passthrough — the grain engine was doing work for no audible purpose. Making this an explicit mode unlocks the texture effects as standalone processors. Micro-stutter on K3 keeps the glitch dimension alive without grains, and feedback pre-loads the grain buffer with textured material for when K2 is turned back up.

---

## Open Questions

- Hann window as default — audition Tukey during tuning if grains feel too "bumpy"
- Whether to add constraints to the feedback path (filtering, wavefolder, delay) to tame or color the feedback loop — decide by ear
- Direct-texture mode: micro-stutter capture buffer size and stutter duration range — tune by ear
- Whether K2/K3 should eventually split back into separate grain character and glitch knobs

### Resolved

- Decimator: sample-and-hold (cheap, good character) — implemented
- Ringmod carrier: stepped ratio table (1.5×–7.3×) with tremolo region below 30% — implemented
- Max grain voices: 8 — implemented, works fine
- Feedback: simple unipolar, no delay/ladder/wavefolder chain. Negative feedback dropped — not useful for short scattered grains

---

## Component Reuse from Mode A

- `EnvFollower` — verbatim, wrapped by new gesture helper
- `PitchTracker` — verbatim, requires Phase 4 continuous-MIDI output from `PITCH_TRACKING.md`
- `MoogLadder` — verbatim, used in the feedback bus
- Wavefolder — reused in both the shaper bus (when SW1 = MIDDLE) and the feedback bus (always on)
- `MoogOsc` — not used in Mode B

## References

- **Chase Bliss Mood** — conceptual reference for "micro-looper as collaborator"
- **Qu-Bit Stardust** — Daisy Seed granular precedent
- **Roads, Curtis — "Microsound" (MIT Press)** — canonical granular synthesis reference
