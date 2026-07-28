# ChronoTron3 — Control Layout (discovery)

> **Discovery snapshot, provisional.** This mirrors what the current firmware
> actually does — not the manual or README (those come after we've iterated).
> Assignments will move. Source of truth: `pedals/chronotron3/`.

## Shell — applies in every mode

| Control | Function |
|---|---|
| **SW3** | **Mode select** — UP = *vestige* · MIDDLE = *mnemonic* · DOWN = *Armitage* |
| **K6** | **Dry/wet mix** — equal-power (mnemonic, Armitage). *vestige overrides it as looper volume — see below.* |
| **Both footswitches held ~2 s** | Enter Daisy bootloader (DFU). The only entry path (sealed pedal). |

The mode owns everything else — including both footswitches. There is no
dedicated bypass footswitch yet (K6 fully dry = effectively bypassed).

---

## vestige — SW3 UP · grain looper / freeze

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Voice count / topology | Padded noon = 1 (parallel) · CCW → up to 6 voiced (FIFO, auto age-fade fixed by count) · CW → frippertronics: just past noon = shortest decay (~1 repeat) · fully CW = infinite sustain. Live; never resets playback |
| KNOB 2 | Auto-capture threshold | Used in continuous-auto capture; spare in manual |
| KNOB 3 | Scan / freeze | **CCW** = normal forward loop · **CCW→noon** = backward auto-scrub decelerating to a **halt at noon** · **noon→CW** = frozen, with the freeze point sweeping **live** across the **whole buffer** — beginning (noon) to a grain-scan-range in from the **END** (full CW). Grain diffusion stays alive throughout |
| KNOB 4 | Texture | Bipolar, clean at noon: CCW tape saturation (gain-compensated) · CW decimation → digital glitch |
| KNOB 5 | Loop fade in/out | CCW = instant → CW = max, **both as real bounded durations on one scale** (no runaway tail). **Attack** = convex swell (slow start → full) over `ATTACK_MAX_S`; **release** = concave dies-away over `RELEASE_MAX_S`. Default 3 s : 3 s (1:1); ratio is set by those two constants. Applied on loop start / stop / mute |
| KNOB 6 | Looper volume | *vestige owns its output* — additive: `out = dry + K6·looper`. CCW = silent · noon = unity · CW = boost (+6 dB). The clean (dry) is routed by SW2, not by K6 |
| SWITCH 1 | Capture mode | UP = manual (hold-record) · MIDDLE = continuous-auto · DOWN = → manual (TBD) |
| SWITCH 2 | Dry (clean) routing | UP = clean always on (loop plays on top) · MIDDLE = clean on, but cut while recording or auto-armed · DOWN = clean off (loop only) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Stop | Tap = mute/pause (material kept; fades via K5) · Hold = clear all |
| FOOTSWITCH 2 | Engage | Manual: hold = record, release = set loop end · Auto: record-arm toggle · from muted: resume |
| LED 1 | Play state | solid = playing · slow-blink = muted · off |
| LED 2 | Record state | solid = recording · fast-blink = auto-armed · off |

---

## mnemonic — SW3 MIDDLE · tap-tempo tape/BBD delay

Spec: `mnemonic-concept.md` + `mnemonic-impl-plan.md`. As-built first pass
(M0–M9) — every value is a starting bracket, untested by ear.

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Delay time / division | **SW2 UP** = absolute delay time (exp 20 ms–3 s), turning it **glides** = varispeed pitch bend. **SW2 MID** = tap division, 11 stops, noon = 1/1 (quarter = tap): CCW 3/4·2/3·1/2·1/3·1/4 shorter · CW 4/3·3/2·2/1·3/1·4/1 longer. **SW2 DOWN** = stretch/shrink the captured rhythm |
| KNOB 2 | Feedback | 0 (CCW) → bounded self-oscillation (CW). Runs into always-on tape saturation + build-up ducker |
| KNOB 3 | Degrade | Bipolar, clean at noon: **CCW** BBD (sample-rate decimation + gentle crush + rounding) · **CW** tape (extra drive + wow/flutter warble + progressive HF loss) |
| KNOB 4 | Tone tilt | Bipolar, flat at noon (cut-only): **CCW** LPF (rolls off highs, dark) · **CW** HPF (rolls off lows, thin). Sets where the delay sits |
| KNOB 5 | Resonance / EQ | Resonant peak at K4's corner (Q rises CCW→CW toward BPF-ish). Emphasises a band into the tape drive. In the feedback loop, so it ages the repeats |
| KNOB 6 | Dry/wet mix | *(shell — equal-power. Dry is never processed/limited)* |
| SWITCH 1 | FS1 **hold** gesture | **UP** = tape spin-up (hold → time↓/pitch↑ + feedback↑, slewed; release slews back) · **MIDDLE** = hold/loop (press record, release play) · **DOWN** = tape slow-down (hold → time↑/pitch↓ + feedback↑) |
| SWITCH 2 | Time mode | **UP** = knob time · **MIDDLE** = tap tempo (FS1 taps) · **DOWN** = rhythmic taps (capture-the-rhythm multi-tap) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Tap / gesture (hold-then-commit) | **Short tap** (release < ~300 ms) = tempo/rhythm tap — works in *every* SW1 position · **Long hold** (> ~450 ms) = the SW1-latched sustained gesture: MID loop record · UP spin-up · DOWN slow-down. Downpress is the timing reference; deadzone between = no-op |
| FOOTSWITCH 2 | Bypass / kill | **Tap** = bypass toggle — gates the send + loop, but the delay **trail rings out** · **Hold** = kill (clears the delay line + deletes the loop) |
| LED 1 | Delay clock | Blinks at the effective delay timing (tempo × division) |
| LED 2 | Bypass / loop state | Active = solid · bypassed = off · recording = solid · loop armed = rapid flash · loop running while bypassed = dim slow flash |

**Loop (SW1 = MID).** Recorded from the clean signal; plays back *into* the delay
line in parallel with the live input. A short tap sets tempo/rhythm and never
disturbs a playing loop; a long hold records into a scratch buffer and **commits
on release** (pointer-swap, **replaces** the old loop — no overdub). Recording
starts on down-press for an accurate start; buffer-full auto-ends. Bypass pauses
the loop, kill (FS2 hold) deletes it.

---

## Armitage — SW3 DOWN · impulse synth / resonator / drone

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Register | Bipolar: CCW sub · noon unison · CW upper (±1 octave, continuous) |
| KNOB 2 | Damping | Decay time (T60), short/plucky → long drone. Primary timbre |
| KNOB 3 | Structure | Comb: allpass dispersion · Modal: inharmonic partial spread |
| KNOB 4 | Asymmetry | Excitation enrichment, 0 → 1.0 (full rectification; fills spectral gaps). Sole conditioning control |
| KNOB 5 | Filter envelope | Gated AR → 4-pole 24 dB/oct non-resonant LP; closed = muted. Fast onset detect (hysteresis crossing) retriggers the sweep per note; sustains while the note rings; releases on note-off. Bipolar: noon = snappy attack+release · CCW = longer attack · CW = longer release. Perceived decay = release, decoupled from K2 (CCW cuts the tail fast, CW lets it ring out) |
| KNOB 6 | Dry/wet mix | *(shell)* |
| SWITCH 1 | Unused | Free — modal core dropped, comb is the keeper; reassignment TBD |
| SWITCH 2 | **Note-set behaviour (A/B)** | UP = fixed dense bank (25-note semitone comb) · MIDDLE = mono-tracked voice (follows played pitch) · DOWN = key-quantised multivoice (arpeggiate to stack an in-key chord) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 / 2 | Unused | (bootloader gesture still reserved) |
| LED 1 | Input activity | brightness follows what you play (play indicator) |
| LED 2 | Filter envelope | openness of the K5 filter |

**How to smoketest each SW2 behaviour** (play into the pedal — the resonators are
*excited* by your signal):

- **UP fixed bank** — rings to anything you play, incl. chords. Judge the core
  itself: timbre, K2 damping range, K1 register, K3 structure, K4 asymmetry,
  drone character.
- **MIDDLE mono** — play single notes/lines; it tunes to the pitch and rings.
  Judge tracking across the range, register, the "voice" feel. (Chords → picks
  one pitch — that's expected; poly detection is deferred.)
- **DOWN key-quant** — play an arpeggio; distinct in-key notes stack into a
  chord (key A / minor pentatonic, a constant for now). Judge the pseudo-poly
  feel and whether the key/scale defaults work.

Deferred (needs the offline harness): true polyphonic *chord* detection.
