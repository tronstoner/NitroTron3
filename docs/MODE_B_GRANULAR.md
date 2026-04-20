# Mode B — Granular Glitch — Concept Spec

Inspired by the Chase Bliss Mood pedal's "micro-looper as collaborator" philosophy, tuned specifically for bass as input. A live rolling buffer of the bass signal is continuously scattered as grains, pitch-shifted in harmony with the detected bass note, and routed through a feedback loop that can sustain into drone territory.

**The pedal is a companion, not a doubler.** The wet output sits *above* the dry bass in the frequency spectrum and rhythmically *around* it — the bassist keeps playing bass, the pedal improvises over the top.

**The pedal reacts to how you play.** Attack sharpness and sustain length, extracted from the envelope follower, modulate the tonal-shaping stage. Short-hard plucks and long-smooth notes produce different sounds from the same knob setting.

**Not** included: freeze/capture mode (always live), fixed-interval-only pitch mode (always tracked), separate tuning-mode pages (Mode B tunes via `constants.h`).

---

## Signal Chain

```
Input ──┬──────────────────────────────────────────► [Mix] ──► Output
        │                                               ▲
        ├──► [EnvFollower] ──► amp / attack / sustain   │
        │         │                                     │
        │         └──► [Gesture Bus] ─────┐             │
        │                                 │             │
        ├──► [PitchTracker] ──► continuous MIDI ──┐     │
        │                                         │     │
        └──► [Ring Buffer, SDRAM, 8s] ◀────┐      │     │
                  │                        │      │     │
                  ▼                        │      │     │
           [Grain Scheduler] ◀─────────────┼──────┘     │
                  │                        │            │
                  ▼                        │            │
           [Grain Voices × N]              │            │
                  │                        │            │
                  ▼                        │            │
           [Shaper Bus] ◀──────────────────┘            │
                  │    (SW1 selects mode,               │
                  │     K4 intensity, gesture-driven)   │
                  ▼                                     │
             [Wet HPF]                                  │
                  │                                     │
                  ├─────────────────────────────────────┘
                  │
                  └──► [Feedback Bus] ──► feedback injection
                         │                      ▲
                         └─ [Delay] ─ [Ladder] ─┘
                                       │
                                    [Wavefold]
```

The **Gesture Bus** carries three analysis signals (amp, attack sharpness, sustain) derived from the existing `EnvFollower` — no new analyzers. It feeds both the scheduler (density modulation) and the shaper bus (intensity modulation of the active texture mode).

The **Shaper Bus** is gesture-reactive tonal shaping. Switch 1 selects one of three modes (decimator / wavefolder / ringmod); K4 sets intensity; the gesture naturally suited to that mode modulates the final amount.

The **Feedback Bus** sends shaper-bus output through delay → ladder → wavefolder, then injects it back into the ring buffer's write path — so feedback becomes new grain material. The wavefolder in the feedback bus is always present, independent of Switch 1's texture selection, giving the loop its own consistent nonlinearity.

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

### Feedback Bus

Send from shaper-bus output → delay → ladder filter → wavefolder → inject into ring-buffer write path.

- **Delay** — fractional delay, around 180 ms default
- **Ladder** — reuses Mode A's `MoogLadder`, fixed cutoff around 1.2 kHz (darker than the wet)
- **Wavefolder** — reuses Mode A's fold block, fixed drive
- **Safety HPF around 100 Hz in the feedback path** — compile-time, non-user-adjustable, non-negotiable for a bass pedal. Without it, sub content accumulates in the loop and the output runs away

**Feedback amount is on K5 (centered).** CCW of noon = negative feedback (phase-inverted injection — tight, de-correlated, no sustain buildup). Noon ±deadzone = off. CW of noon = positive feedback (can sustain into drone near full CW). Ceiling at 0.95 magnitude to keep self-oscillation controllable.

Negative and positive feedback sound genuinely different at low magnitudes — negative feedback de-correlates the loop so each grain's echo partially cancels the next, giving a tighter character. Positive feedback accumulates and sustains. Having both on a single centered knob with a clear "off" detent is the most expressive layout.

### Wet HPF

2-pole high-pass on the shaper-bus output before mix. Compile-time cutoff around 150 Hz, no user control. Keeps the wet signal from competing with the dry bass for sub-frequency real estate. This is the single most important bass-specific tuning choice in Mode B.

### Mix

Equal-power dry/wet crossfade controlled by K6.

---

## Controls — Normal Mode

| CONTROL | DESCRIPTION | NOTES |
|---|---|---|
| KNOB 1 | Interval | **Centered.** Noon = unison. CCW → −24 semi, CW → +24 semi. Interpretation depends on Switch 2 (anchor for Fixed and Scale modes, cloud weighting for Cloud mode) |
| KNOB 2 | Size / Density | **Centered.** Noon = medium (~100 ms, 10 Hz). CCW = long sparse. CW = short dense. Attack sharpness briefly bumps density at any setting |
| KNOB 3 | Scatter | Unipolar. CCW = tight/repeatable, CW = chaotic (position jitter, timing jitter, reverse probability scale together) |
| KNOB 4 | Texture amount | Unipolar. 0 = clean, 1 = full effect. Intensity of whichever texture mode Switch 1 selects. Gesture-modulated (attack / amp / sustain depending on mode) |
| KNOB 5 | Feedback | **Centered.** CCW = negative feedback (tight, de-correlated). Noon ±deadzone = off. CW = positive feedback (can sustain into drone near full CW). Ceiling at ±0.95 |
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

**Why feedback has its own knob (and is centered):** feedback is the single most performative parameter — the difference between "processor that reacts" and "instrument that sings on its own." It needs to be a knob, not a switch. Centered because negative and positive feedback are genuinely different sounds at low magnitudes, and the center detent is a meaningful musical zero.

**Why the wet HPF is baked in:** on bass, sub-frequency accumulation in the wet path muddies everything. This isn't a taste decision, it's a "the pedal sounds bad without it" decision. Keeping it fixed also saves a knob for something that's actually performance-useful.

**Why the feedback bus is its own separate wavefolder:** when the user selects decimator or ringmod on Switch 1, the feedback loop still needs a nonlinearity to keep it musically interesting and to soft-limit runaway. Having the feedback wavefolder be independent of Switch 1 gives the loop a consistent tonal signature across all texture modes.

**Why feedback injects into the ring buffer, not the wet output:** injecting into the buffer means the pedal hears itself and re-grains what it hears. At low feedback the effect is subtle (grains of grains). At high feedback the loop self-sustains as a drone. This is a more organic behavior than traditional "stack the delays" feedback — it matches the Mood's micro-looper character.

---

## Open Questions for Implementation Session

- Hann window as default — audition Tukey during tuning if grains feel too "bumpy"
- Decimator implementation: sample-and-hold (cheap, digitally ugly, good character) vs proper anti-aliased downsample (cleaner, more expensive). Lean S&H
- Ringmod carrier ratio (default 3.5, inharmonic). May want to try 2.0 (octave up, harmonic) or a user-selectable set during tuning
- Max concurrent grain voices — start at 8, profile, push higher if comfortable
- Whether the feedback-bus delay time should be fixed or tied to grain density for musical coherence — decide by ear during tuning

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
