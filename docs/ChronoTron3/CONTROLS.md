# ChronoTron3 — Control Layout (discovery)

> **Discovery snapshot, provisional.** This mirrors what the current firmware
> actually does — not the manual or README (those come after we've iterated).
> Assignments will move. Source of truth: `pedals/chronotron3/`.

## Shell — applies in every mode

| Control | Function |
|---|---|
| **SW3** | **Mode select** — UP = *vestige* · MIDDLE = *mnemonic* · DOWN = *Armitage* |
| **K6** | **Dry/wet mix** — equal-power, always the mix (every mode) |
| **Both footswitches held ~2 s** | Enter Daisy bootloader (DFU). The only entry path (sealed pedal). |

The mode owns everything else — including both footswitches. There is no
dedicated bypass footswitch yet (K6 fully dry = effectively bypassed).

---

## vestige — SW3 UP · grain looper / freeze

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Voice count / topology | 1 = parallel · 2–6 = voiced (FIFO, age-fade) · > ~0.86 = frippertronics. Live; never resets playback |
| KNOB 2 | Auto-capture threshold | Used in continuous-auto capture; spare in manual |
| KNOB 3 | Smoothness | CCW = looper (long grains, ordered) → CW = freeze (short grains, full-buffer scatter) |
| KNOB 4 | Texture | Bipolar, clean at noon: CCW tape saturation/degrade · CW decimation → digital glitch |
| KNOB 5 | Fade / decay | Age-fade slope (voiced) · overdub decay (frippertronics) |
| KNOB 6 | Dry/wet mix | *(shell)* |
| SWITCH 1 | Capture mode | UP = manual (hold-record) · MIDDLE = continuous-auto · DOWN = → manual (TBD) |
| SWITCH 2 | Unused | Free |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Stop | Tap = mute/pause (material kept) · Hold = clear all |
| FOOTSWITCH 2 | Engage | Manual: hold = record, release = set loop end · Auto: record-arm toggle · from muted: resume |
| LED 1 | Record state | solid = recording · fast-blink = auto-armed · off |
| LED 2 | Play state | solid = playing · slow-blink = muted · off |

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
| KNOB 4 | Asymmetry | Excitation enrichment, 0 → 0.5 (fills spectral gaps, F3). Sole conditioning control |
| KNOB 5 | Envelope | Bipolar attack/release of the env-coupled output filter (fast ↔ slow) |
| KNOB 6 | Dry/wet mix | *(shell)* |
| SWITCH 1 | Resonator core | UP = comb (Karplus-Strong) · MIDDLE → comb · DOWN = modal (bandpass) |
| SWITCH 2 | **Note-set behaviour (A/B)** | UP = fixed dense bank (25-note semitone comb) · MIDDLE = mono-tracked voice (follows played pitch) · DOWN = key-quantised multivoice (arpeggiate to stack an in-key chord) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 / 2 | Unused | (bootloader gesture still reserved) |
| LED 1 | Resonator core | dim = comb · bright = modal |
| LED 2 | Output level | env-follower brightness |

**How to smoketest each SW2 behaviour** (play into the pedal — the resonators are
*excited* by your signal):

- **UP fixed bank** — rings to anything you play, incl. chords. Judge the core
  itself: timbre, K2 damping range, K1 register, K3 structure, K4 asymmetry,
  comb-vs-modal (SW1), drone character.
- **MIDDLE mono** — play single notes/lines; it tunes to the pitch and rings.
  Judge tracking across the range, register, the "voice" feel. (Chords → picks
  one pitch — that's expected; poly detection is deferred.)
- **DOWN key-quant** — play an arpeggio; distinct in-key notes stack into a
  chord (key A / minor pentatonic, a constant for now). Judge the pseudo-poly
  feel and whether the key/scale defaults work.

Deferred (needs the offline harness): true polyphonic *chord* detection.
