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

## mnemonic — SW3 MIDDLE · tap-tempo delay *(unspecced placeholder)*

Passthrough for now — no spec yet. All controls unused pending design.

| Control | Function |
|---|---|
| KNOB 1–6 | Unused (K6 still the shell mix) |
| SWITCH 1 / 2 | Unused |
| FOOTSWITCH 1 / 2 | Unused |
| LED 1 / 2 | Off |

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
