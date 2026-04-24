# DIY Digital Bass Pedal — Project Plan

## Overview

A multi-mode digital effects pedal for bass guitar, built on the Electro-Smith Daisy Seed. Three independent effect modes are selectable via Switch 3, each with an edit buffer and 8 stored presets. Only the active mode runs at any time — inactive modes have zero CPU cost.

**Mode A — Drone OSC** (fully specced in `MODE_A_DRONE.md`)
Inspired by the Moog MoogerFooger FreqBox (MF-102). An internally generated oscillator, amplitude-controlled by an envelope follower tracking the bass input, mixed back with the dry signal. No oscillator sync, no FM modulation.

**Mode B — Granular Glitch** (fully specced in `MODE_B_GRANULAR.md`)
Inspired by the Chase Bliss Mood's "micro-looper as collaborator" philosophy. Rolling buffer → grain scheduler → pitch-shifted voices → gesture-reactive tonal shaping → feedback loop. The wet output sits above the dry bass in frequency and rhythmically around it. Reuses EnvFollower, PitchTracker, MoogLadder, and wavefolder from Mode A.

**Mode C — Frequency Shifter** (placeholder, to be specced later)
Single sideband frequency shift via Hilbert transform. Moderate CPU load, comfortably within budget.

---

## Hardware Platform

### Core Module
- **Electro-Smith Daisy Seed** — STM32-based DSP platform, 480 MHz, 32-bit float, 96 kHz audio
- **Memory:** 128 KB internal flash, 8 MB QSPI flash, 512 KB SRAM
- Programmable in C++/Arduino, PureData/PlugData, or Max/MSP Gen~
- Source: **Thonk (UK)** — ships to Austria, ~£31

**QSPI migration plan:** The firmware currently targets 128 KB internal flash (77% used after Stage 5). When adding Modes B and C pushes usage past ~90%, switch the linker script from `STM32H750IB_flash.lds` to `STM32H750IB_qspi.lds` (one-line Makefile change). This moves code to the 8 MB QSPI flash — effectively unlimited for this project. The Cortex-M7 instruction cache (16 KB) keeps audio-hot code at full speed; the tight DSP loops that run every sample will stay cached permanently. Requires the Daisy bootloader (already in use for DFU flashing).

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
| Switch 3 | Mode select — Drone / Granular / Freq Shift |
| Footswitch 1 | Preset navigation (see Preset System below) |
| Footswitch 2 | Bypass (true bypass via Hothouse relay). Long press: save mode |
| FS1 + FS2 held 2 s | Enter DFU bootloader for flashing |

Switches 1–2 and knobs 1–6 are mode-specific — see each mode's spec file.

See `TUNING.md` for the tuning-mode override that repurposes controls during development.

---

## Preset System

Inspired by the EHX Ring Thing UX, adapted for a two-LED, two-footswitch interface.

### Slots

- **Edit buffer** — the live working state. One per mode, persisted to flash for power-cycle recall. Each edit buffer includes the **active preset number** (0 = Manual, 1–8 = preset) and **dirty flag**, so the full mode state survives power cycles and mode switches.
- **8 stored presets per mode** — 24 slots total across three modes. Independent per mode: switching modes loads that mode's own edit buffer, active preset, and dirty state.

Stored in Daisy Seed onboard flash via DaisySP `PersistentStorage`. Per-mode preset data structures live in each mode's spec file.

### Navigation (Normal Mode)

**FS1 short press** cycles through: Manual → Preset 1 → … → Preset 8 → Manual → …

**FS1 long press** jumps directly to Manual mode from any preset, reading hardware knob positions into the edit buffer. This is a quick escape — no need to cycle through all presets to get back to manual.

Loading a saved preset copies its stored values into the edit buffer. Knob values jump immediately — no pickup mode.

**Empty (unsaved) slots** act like manual mode: hardware knob positions are read into the edit buffer. No dirty tracking — knobs write through freely. LED 1 still shows the slot number. Saving to a slot marks it as saved; subsequent loads will use the stored values.

**Manual mode** = the current edit buffer with no preset loaded. On first-ever boot (factory state), the pedal enters manual mode and reads the current knob positions into the edit buffer. On subsequent boots, the pedal restores the last active state (preset number + edit buffer) from flash.

### Dirty State (Preset Edited)

When a preset is loaded and any knob is moved, the edit buffer diverges from the stored preset. This is the **dirty** state:

- **LED 2** switches from solid on to a **rapid flash** pattern, indicating the loaded preset has been altered. (Inspired by EHX pedals that flash LEDs to indicate edited presets.)
- The stored preset slot is not modified — only the edit buffer changes.

While dirty:
- **FS1 short press** → **reloads the current preset**, reverting the edit buffer to the stored values. This is a performance feature: tweak parameters live, then instantly reset to the saved state. LED 2 returns to solid.
- **FS1 short press** does **not** cycle to the next preset while dirty. You must reload first, then press again to cycle.

### LED Preset Indication (LED 1)

LED 1 indicates the active preset using a Roman-numeral-inspired encoding with three blink types: **I** (short blink), **V** (long blink), **X** (rapid flicker).

| State | Symbol | LED 1 pattern |
|---|---|---|
| Manual (no preset) | — | Off (mode-specific use, e.g. waveform indicator in Mode A) |
| Preset 1 | I | short |
| Preset 2 | II | short, short |
| Preset 3 | III | short, short, short |
| Preset 4 | IV | short, long |
| Preset 5 | V | long |
| Preset 6 | VI | long, short |
| Preset 7 | VII | long, short, short |
| Preset 8 | VIII | long, short, short, short |

Default timing (source of truth: `docs/ux-demo.html`):

| Constant | Value | Description |
|---|---|---|
| I (short) on | 150 ms | Short blink duration |
| V (long) on | 950 ms | Long blink duration |
| Element gap | 200 ms | Gap between symbols |
| Repeat gap | 600 ms | Gap before pattern repeats |
| Dirty flash | 50/50 ms | On/off time for dirty indicator (LED 2) |
| Save mode blink | 150/150 ms | On/off time for save mode (LED 2) |
| Save confirm burst | 75/75 ms for 500 ms | Rapid flash after save confirmed (LED 2) |
| Long press threshold | 700 ms | FS1/FS2 long press detection |
| Bootloader hold | 2000 ms | FS1 held to enter DFU |

Selecting a preset always restarts the blink pattern from the beginning.

**Implementation note:** firmware should use a pre-computed lookup table of `{duration_ms, led_on}` step arrays per preset — no runtime pattern logic. The interactive demo (`docs/ux-demo.html`) can be used to tune timing constants before generating the table.

### Save Mode

**FS2 long press** → enters save mode.

- **Target slot is pre-selected** to the currently loaded preset. If you loaded Preset 3 and tweaked it, save targets Preset 3 by default — no need to remember or cycle.
- **LED 2** blinks fast (distinct from the dirty-state flash) to indicate save mode.
- **LED 1** shows the target slot's blink pattern.

While in save mode:
- **FS1 short press** → **cycles through target slots** (1 → 2 → … → 8 → 1 → …). LED 1 updates to show the new target. Starts at the pre-selected slot, so one press moves to the next.
- **FS2 long press** → **confirms save**. Edit buffer is written to the selected slot. LED 2 flashes rapidly for ~500 ms to confirm success. Returns to normal mode (preset now clean).
- **FS2 short press** → **cancels save**. Returns to normal mode, no write performed.

Save mode also times out after a few seconds of inactivity (returns to normal mode, no save).

**Saving from manual mode:** FS2 long press enters save mode with target defaulting to Preset 1. Use FS1 to cycle to the desired slot, then FS2 long press to confirm. The current edit buffer is always what gets saved.

### Mode Switching

Switching modes via Switch 3 **preserves and restores the full state of each mode**:

1. The current mode's edit buffer, active preset number, and dirty flag are saved.
2. The new mode's edit buffer, active preset number, and dirty flag are restored.
3. LEDs update to reflect the restored state (preset blink pattern, dirty indicator).

This means if you're on Preset III (dirty) in Mode A, switch to Mode B, then back to Mode A — you return to Preset III (dirty) with the same knob values. Each mode remembers exactly where you left it.

### Power Cycle Behavior

On boot, the pedal restores the last active mode and each mode's full state (edit buffer + active preset + dirty flag) from flash. The pedal comes back exactly as it was when powered off — same mode, same preset, same parameters.

**First boot (factory state):** all modes start in Manual with knobs read from panel positions.

### Knob Behavior

- **On preset load:** values jump immediately to stored values. No pickup, no interpolation.
- **After load:** moving any knob overrides that parameter in the edit buffer. The stored preset is not modified (dirty state).
- **Manual mode on startup:** all knob positions are read and applied to the edit buffer.

### Bootloader Entry

**Hold both FS1 and FS2 simultaneously for 2 seconds** → enters DFU bootloader for flashing. This combination cannot be triggered accidentally during normal preset or bypass operation because:
- FS1 and FS2 are never both actionable at the same time in normal use (FS1 = preset, FS2 = bypass)
- The 2-second hold window rejects accidental simultaneous taps
- Detection requires both switches to be held continuously — if either is released early, the timer resets

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
- **Stage 4** — DONE. Envelope follower (Moog topology, no gate) + VCA + equal-power dry/wet mix. Second oscillator on K5 (±12 semitone detune with center dead zone). All 6 knobs wired. Mode A is fully playable.
- **Pitch tracking** — DONE. YIN algorithm with 4x decimation, anti-alias LP at 400 Hz. Three drone sub-modes on Switch 2: fixed pitch, octave-locked tracking (pitch class in target octave), direct tracking. Wrap point set by K1 in fixed mode (default A). See `PITCH_TRACKING.md` for research and improvement plan.
- **Wavefolding** — DONE. Triangle mode: K4 noon→CW applies wavefolding. Envelope subtly modulates fold amount and filter cutoff for dynamic response.
- **Per-waveform gains** — DONE. Independent level trim for saw/tri/square in constants.h.
- **Serial logging** — DONE. `StartLog(false)` + raw knob values printed every ~2 s from main loop using `FLT_FMT3` macros. Safe: no printing from audio callback.
- **Tuning mode** — DEFERRED. Depends on working serial logging. Ear-tuning via constants.h for now.
- **Mode B spec** — DONE. Granular Glitch fully specced in `MODE_B_GRANULAR.md`.
- **Knob remap** — DONE. Measured physical range 0.000–0.968, calibrated `RemapKnob()` with named constants.
- **Preset system** — DONE. FS1 preset navigation (edit buffer + 8 presets per mode), Roman numeral LED blink encoding (I/V patterns), save mode via FS2 long press + confirm. Dirty tracking, flash persistence via `PersistentStorage`, mode switching saves/restores per-mode state. Auto-save every ~30 s. Bootloader via FS1 held 2 s (Phase 1; Phase 2 will add FS1+FS2 dual-hold). Implementation plan in `docs/PRESET_IMPL.md`.
- **Stage 6** — DONE. Multi-mode scaffold: Switch 3 dispatches `ProcessDrone()` / `ProcessGranular()` / `ProcessFreqShift()`. Modes B and C are dry passthrough stubs. Bypass handled once in `AudioCallback`, mode functions only run when active. Pitch tracker conditional on Mode A.
- **Mode B** — IN PROGRESS. Granular engine working: ring buffer (8 s SDRAM), grain scheduler, 8 voices, Hann windowing, pitch-tracked harmony (fixed interval + resonance modes), texture shaper (decimator/wavefolder bipolar on SW1 UP, clean on MID, ringmod on DOWN), wet HPF 150 Hz, equal-power mix. Knob layout: K1=interval, K2=buffer range, K3=character/glitch (merged, splittable), K4=texture amount, K5=feedback (unipolar, 0.95 ceiling, wet→ring buffer injection), K6=mix. Direct-texture mode (K2 fully CCW): grain engine bypassed, input→texture shaper, K3=micro-stutter.
- **Next** — Feedback path constraints (filtering/coloring) by ear. FS1+FS2 dual-hold bootloader (Phase 2 after flash test). Pitch tracking improvements deferred.

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

### Stage 4 — Envelope follower + VCA + mix + tracking + wavefold ✓
Envelope follower (Moog topology, no gate) + VCA + equal-power mix. Three drone sub-modes (Switch 2): fixed pitch, octave-locked tracking, direct tracking. YIN pitch tracker with 4x decimation. Wavefolding on triangle (K4 noon→CW). Envelope modulates filter cutoff and fold amount. Second oscillator (K5 detune). Per-waveform gain constants. Octave-locked wrap point fixed to A (compile-time constant `TRACKING_WRAP_NOTE`). All 6 knobs + Switch 1/2 wired. Mode A is fully playable.

### Stage 5 — Preset system ✓
FS1 preset navigation (edit buffer + 8 slots per mode), save mode (FS2 long press + FS2 confirm), Roman numeral LED blink encoding on LED 1, flash storage via `PersistentStorage`. Audio callback reads from edit buffer, not hardware. Dirty tracking with 2% knob threshold. Mode switching preserves/restores full per-mode state. Bootloader: FS1 held 2 s (Phase 1).

### Stage 6 — Multi-mode scaffold ✓
Switch 3 dispatches to `ProcessDrone()`, `ProcessGranular()`, `ProcessFreqShift()`. Modes B and C are dry passthrough stubs. Bypass handled once in `AudioCallback`. Pitch tracker runs only in Mode A. Architecture ready for mode implementation.

### Stage 7 — Enclosure
Drill Hammond 125B to Hothouse template. Finish and label.

### Deferred
- **Tuning mode** — USB serial workflow needs a fix (freezes on terminal connect). Will revisit after core effect is playable.
- **Mode B direct-texture mode** — DONE. K2 fully CCW bypasses grain engine, K3 = micro-stutter.
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
- `MODE_B_GRANULAR.md` — Mode B full spec: granular glitch processor, signal chain, controls.
- `MODE_C_FREQSHIFT.md` — *not yet written*
- `AGENTS.md` — AI agent entry point. Routes agents to the right doc for the task.
