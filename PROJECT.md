# DIY Digital Bass Pedal — Project Plan

## Overview

A multi-mode digital effects pedal for bass guitar, built on the Electro-Smith Daisy Seed. Three independent effect modes are selectable via a toggle switch, each with 3 preset slots. Only the active mode runs at any time — inactive modes have zero CPU cost.

**Mode A — Drone OSC** (primary, fully specced in `MODE_A_DRONE.md`)
Inspired by the Moog MoogerFooger FreqBox (MF-102). An internally generated oscillator, amplitude-controlled by an envelope follower tracking the bass input, mixed back with the dry signal. No oscillator sync, no FM modulation.

**Mode B — Granular Glitch** (placeholder, to be specced later)
Buffer-based granular processor. Modest grain count (8–16) is within the Daisy's capabilities. The 64MB SDRAM provides several minutes of audio buffer. Community precedent exists (Qu-Bit Stardust runs on Daisy).

**Mode C — Frequency Shifter** (placeholder, to be specced later)
Single sideband frequency shift via Hilbert transform. Moderate CPU load, comfortably within budget.

---

## Hardware Platform

### Core Module
- **Electro-Smith Daisy Seed** — STM32-based DSP platform, 480 MHz, 32-bit float, 96 kHz audio
- Programmable in C++/Arduino, PureData/PlugData, or Max/MSP Gen~
- Source: **Thonk (UK)** — ships to Austria, ~£31

### Pedal PCB / Kit
- **Hothouse DSP Pedal Kit** (without enclosure) by Cleveland Music Co.
- Provides: audio I/O buffering, power filtering, voltage regulation, noise filtering, all hardware controls pre-specced
- Includes: 6 potentiometers, 3 × 3-position toggle switches, 2 footswitches, jacks, LED
- Daisy Seed purchased separately and seated on the Hothouse PCB
- Source: **Reverb.com** (Cleveland Music Co. store) — ships to Europe

### Enclosure
- **Hammond 125B** format — self-drilled and finished
- The Hothouse is designed for this enclosure

### Additional Sourcing (Austria/EU)
- Components if needed: **Musikding.de** (Germany, fast shipping to AT)
- Enclosure: **Musikding.de** or **Banzai Music (DE)**

---

## Signal Chain (Mode A)

```
Input ──┬──────────────────────────────────────────────► [Mix] ──► Output
        │                                                   ▲
        └──► [Full Wave Rectify] ──► [4-pole 33Hz LP] ──► [Threshold Gate]
                                                              │
                                                           [VCA gain]
                                                              │
                                                    [PolyBLEP Oscillator]
                                                              │
                                                    [Huovilainen Ladder LP]
                                                              │
                                                           [Mix] ──────────┘
```

Full detail in `MODE_A_DRONE.md`.

---

## Multi-Mode Architecture

```cpp
switch (current_mode) {
    case MODE_DRONE:    ProcessDrone(in, out); break;
    case MODE_GRANULAR: ProcessGranular(in, out); break;
    case MODE_FREQSHIFT: ProcessFreqShift(in, out); break;
}
```

Only the active branch executes per audio block. No CPU cost for inactive modes.

---

## Global Controls

These are consistent across all modes:

| Control | Function |
|---|---|
| Toggle 3 | Mode select — Drone / Granular / Freq Shift |
| Toggle 2 | Preset select — A / B / C (per active mode) |
| Footswitch 1 | Bypass (true bypass via Hothouse relay) |
| Footswitch 2 | Short press: recall live knob state · Long press: save to active preset |

Mode-specific knob and toggle assignments are documented in each mode's spec file.

See `TUNING.md` for the tuning-mode override that repurposes controls during development.

---

## Preset System

- **9 preset slots total** — 3 per mode, independent per mode
- Stored in Daisy Seed onboard flash via DaisySP `PersistentStorage`
- Short press on FS2: jump to live knob values (no pickup mode — values jump immediately)
- Long press on FS2: save current knob state to active preset slot
- LED confirms flash save (~50 ms blink)

Per-mode preset data structures live in each mode's spec file.

---

## Gain Staging

- **Input:** Hothouse input buffer handles instrument-level signals (passive and active bass) — biases and scales to Daisy Seed ADC range (0–3.3 V). No hardware trim required.
- **Output:** Buffered back to instrument level by Hothouse output stage.
- **Internal balance:** Per-mode, calibrated via tuning mode. See `TUNING.md`.

---

## Current Status

- **Stage 0** — DONE. Hardware confirmed, clean passthrough works.
- **Stage 1** — DONE. MoogOsc class (parabolic + PolyBLEP, saw/tri/square).
- **Stage 2** — DONE. Huovilainen ladder filter with per-stage tanh saturation.
- **Stage 3** — DONE. Pitch controls: K1=semitone (12 steps), K2=octave (C-1–C5), K3=fine tune (±50 cents).
- **Stage 4** — DONE. Envelope follower (Moog topology, no gate) + VCA + dry/wet mix. Second oscillator on K5 (±12 semitone detune with center dead zone). All 6 knobs wired. Mode A is fully playable.
- **Tuning mode** — DEFERRED. USB serial (`StartLog`) freezes the pedal when a terminal connects. Ear-tuning via constants.h for now.
- **Next** — Stage 5 (preset system) or polish/tune constants by ear. See timeline below.

## Staged Development Timeline

Small iterations. Each stage produces a working, testable artifact.

### Stage 0 — Hardware bring-up ✓
Flash Hothouse blink example. Confirm clean audio passthrough with bass → Daisy → amp. No DSP. Proves the hardware before any original code.

### Stage 1 — Oscillator ✓
MoogOsc class (parabolic waveshaper + PolyBLEP). Fixed 110 Hz pitch, output to both channels. Knobs control oscillator character for ear-tuning. Waveform toggle wired.

### Stage 2 — Huovilainen ladder filter ✓
Ladder filter on oscillator output. K5 controls cutoff (80 Hz – 8 kHz, exponential). Drive and cutoff offset from compile-time constants. Per-stage tanh saturation for Moog warmth.

### Stage 3 — Pitch controls ✓
K1=semitone (12 quantized steps, C–B), K2=octave (7 positions, C-1–C5), K3=fine tune (±50 cents continuous). Oscillator plays selectable pitches through the ladder filter.

### Stage 4 — Envelope follower + VCA + mix ✓
Bass input → rectifier → 4-pole 33 Hz LP → threshold gate → VCA gain on oscillator. K4=tone (ladder cutoff), K5=mix (dry/wet), K6=envelope sensitivity. All 6 knobs wired. Mode A is fully playable.

### Stage 5 — Preset system
FS2 short/long press logic, 3 slots for Mode A, flash storage via `PersistentStorage`.

### Stage 6 — Multi-mode scaffold
Toggle 3 dispatches to `ProcessDrone()`. Modes B and C are stubs (dry passthrough). Architecture ready for future mode specs.

### Stage 7 — Enclosure
Drill Hammond 125B to Hothouse template. Finish and label.

### Deferred
- **Tuning mode** — USB serial workflow needs a fix (freezes on terminal connect). Will revisit after core effect is playable.
- **Mode B** — Granular Glitch: spec + implement
- **Mode C** — Frequency Shifter: spec + implement

---

## Development & Testing Strategy

### Phase 1 — Mac Prototype (JUCE), optional
DSP core as a JUCE `AudioProcessor` plugin. Run as AU/VST in Reaper. Tune constants in a GUI with exact value readouts before touching hardware. Useful for early iteration on oscillator character and filter tone.

### Phase 2 — Hardware
Port to Daisy. Tuning mode does final tuning against real hardware and real bass. See `TUNING.md`.

### Phase 3 — Enclosure
Drill, paint, label.

### DSP Implementation Order (within Mode A)
1. PolyBLEP oscillator (Stage 1)
2. Huovilainen ladder filter (Stage 2)
3. Moog envelope follower (Stage 3)
4. Mix and gain staging (Stage 3–4)
5. Control layer + preset layer (Stage 4–5)
6. Multi-mode scaffold (Stage 6)

---

## Key Technical References

- **Huovilainen ladder filter model** — Antti Huovilainen, "Non-linear Digital Implementation of the Moog Ladder Filter" (DAFx 2004)
- **PolyBLEP anti-aliasing** — Välimäki & Pakarinen, standard implementation widely documented
- **Moog envelope follower topology** — full wave rectifier + 4-pole 33 Hz LP, consistent across MF-101, MF-102 per circuit traces
- **Moog oscillator fundamental weight** — parabolic ramp from capacitor charging physics; see `MODE_A_DRONE.md` for modeling approach
- **DaisySP library** — https://github.com/electro-smith/DaisySP
- **Hothouse kit** — https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/
- **Daisy Seed** — https://www.thonk.co.uk/shop/electrosmith-daisy-seed/

---

## Document Map

- `PROJECT.md` — this file. Top-level plan, hardware, staging, multi-mode architecture.
- `TUNING.md` — tuning-mode spec. How to dial in constants without a display and commit them to source.
- `MODE_A_DRONE.md` — Mode A full spec: oscillator model, envelope follower, ladder filter, controls, presets.
- `MODE_B_GRANULAR.md` — *not yet written*
- `MODE_C_FREQSHIFT.md` — *not yet written*
- `CLAUDE.md` — Claude Code entry point. Routes agents to the right doc for the task.
