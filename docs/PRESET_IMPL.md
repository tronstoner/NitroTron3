# Preset System — Implementation Plan (Stage 5)

Spec: `PROJECT.md` § Preset System. Source of truth for timing constants: `docs/ux-demo.html`.

---

## Scope

Implement the full preset system for NitroTron3:
- FS1 preset navigation (short press cycle, long press → manual)
- FS2 bypass (short press) + save mode (long press → confirm/cancel)
- Bootloader: Phase 1 keeps `CheckResetToBootloader()` (FS1 held 2 s). Phase 2 swaps to FS1+FS2 dual-hold after flash test confirms recovery works.
- LED 1: Roman numeral blink pattern (presets 1–8), off in manual
- LED 2: solid (active), off (bypassed), rapid flash (dirty), fast blink (save mode), burst (save confirm)
- Edit buffer + 8 stored presets per mode, dirty tracking
- Flash persistence via `PersistentStorage` — survives power cycles
- Mode switching (SW3) saves/restores full per-mode state

---

## Timing Constants (from ux-demo.html)

These are the authoritative values. All go in `constants.h`.

### Preset LED (LED 1) — Roman numeral encoding

| Constant | Value | Description |
|---|---|---|
| `LED_SHORT_ON_MS` | 150 | I symbol on duration |
| `LED_LONG_ON_MS` | 950 | V symbol on duration |
| `LED_ELEM_GAP_MS` | 200 | gap between symbols |
| `LED_REPEAT_GAP_MS` | 600 | gap before pattern repeats |

### Dirty indicator (LED 2)

| Constant | Value | Description |
|---|---|---|
| `LED_DIRTY_ON_MS` | 50 | dirty flash on time |
| `LED_DIRTY_OFF_MS` | 50 | dirty flash off time |

### Save mode (LED 2)

| Constant | Value | Description |
|---|---|---|
| `LED_SAVE_MODE_ON_MS` | 150 | save mode blink on |
| `LED_SAVE_MODE_OFF_MS` | 150 | save mode blink off |

### Save confirm (LED 2)

| Constant | Value | Description |
|---|---|---|
| `LED_SAVE_CONFIRM_DUR_MS` | 500 | total burst duration |
| `LED_SAVE_CONFIRM_ON_MS` | 75 | burst on time |
| `LED_SAVE_CONFIRM_OFF_MS` | 75 | burst off time |

### Footswitch timing

| Constant | Value | Description |
|---|---|---|
| `FS_LONG_PRESS_MS` | 700 | long press threshold |
| `FS_BOOT_HOLD_MS` | 2000 | bootloader entry hold time |
| `KNOB_DIRTY_THRESHOLD` | 0.02 | knob movement threshold for dirty detection |

---

## PROJECT.md Discrepancy

PROJECT.md says save-confirm flash lasts "~1 second" but the UX demo uses 500 ms. The demo is the source of truth — update PROJECT.md to say "~500 ms" and add a full timing constants table.

---

## Architecture Overview

### New files

| File | Purpose |
|---|---|
| `src/preset_system.h` | PresetSystem class — state machine, LED engine, flash storage |

### Modified files

| File | Changes |
|---|---|
| `src/constants.h` | Add all timing constants above |
| `src/NitroTron3.cpp` | Replace FS/LED logic with PresetSystem calls |
| `docs/PROJECT.md` | Update status, fix save-confirm duration |
| `README.md` | Update controls/LEDs tables |

### Not changed

`env_follower.h`, `moog_ladder.h`, `moog_osc.h`, `pitch_tracker.h` — no DSP changes.

---

## Data Structures

### ModePresetData — what gets stored per mode

```cpp
struct ModePresetData {
    float knobs[6];          // 6 knob values (0.0–1.0)
    uint8_t sw1;             // Switch 1 position (0/1/2)
    uint8_t sw2;             // Switch 2 position (0/1/2)
};
```

### ModeState — full per-mode state (in RAM and flash)

```cpp
struct ModeState {
    ModePresetData edit_buffer;              // current working state
    ModePresetData presets[8];               // 8 stored presets
    uint8_t active_preset;                   // 0 = manual, 1–8 = preset
    bool dirty;                              // edit buffer diverged from preset
};
```

### StorageData — the PersistentStorage template struct

```cpp
struct StorageData {
    uint32_t version;                        // schema version for migration
    ModeState modes[3];                      // A, B, C
    uint8_t last_mode;                       // which mode was active at shutdown

    bool operator!=(const StorageData& o) const;
};
```

**Size estimate:** `ModePresetData` = 6×4 + 2 = 26 bytes. `ModeState` = 26 + 26×8 + 1 + 1 = 236 bytes. `StorageData` = 4 + 236×3 + 1 = 713 bytes. Well within flash page limits.

---

## Pedal State Machine

```
        ┌──────────┐
  ┌─────│  NORMAL  │
  │     └──────────┘
  │          │
  │   FS2 long press
  │          │
  │          ▼
  │    ┌───────────┐
  │    │ SAVE_MODE │
  │    └───────────┘
  │          │
  │  FS2 long (confirm)
  │  FS2 short (cancel)
  │          │
  │          ▼
  │   ┌──────────────┐
  └───│ SAVE_CONFIRM │  (500 ms burst, then → NORMAL)
      └──────────────┘
```

Bootloader entry is outside the state machine — handled by `hw.CheckResetToBootloader()` (FS1 held 2 s). Phase 2 will replace with FS1+FS2 dual-hold.

States:
- **NORMAL** — default operating state. FS1 navigates presets, FS2 toggles bypass.
- **SAVE_MODE** — entered via FS2 long press. FS1 cycles target slot, FS2 long confirms, FS2 short cancels.
- **SAVE_CONFIRM** — 500 ms LED burst after successful save, then auto-returns to NORMAL.

---

## Footswitch Handling

**Cannot use `hw.CheckResetToBootloader()`** — it only checks FS1 and consumes the switch state. We must implement our own footswitch processing.

### Detection approach

In the main loop (every 10 ms tick):
1. Read `hw.switches[FOOTSWITCH_1].Pressed()` and `.TimeHeldMs()`
2. Read `hw.switches[FOOTSWITCH_2].Pressed()` and `.TimeHeldMs()`
3. Detect rising/falling edges and hold durations ourselves

### Edge detection + long press

```
FS pressed → start timer
  ├── Single FS held ≥ 700 ms → fire long press, set flag
  └── FS released < 700 ms and no long fired → fire short press
```

Key rules:
- **Long press fires on threshold crossing** (not on release). This matches the UX demo behavior.
- **Short press fires on release** (if long press didn't fire).
- **Bootloader:** Phase 1 keeps `hw.CheckResetToBootloader()` call in main loop. FS1 held 2 s still enters DFU. Since FS1 long press fires at 700 ms (well before 2 s), there's no conflict — user gets "jump to manual" at 700 ms, and only reaches bootloader if they keep holding past 2 s. Phase 2 will replace with FS1+FS2 dual-hold after verifying flash works.

### FS1 actions (NORMAL state)

| Event | Condition | Action |
|---|---|---|
| Short press | save mode | Cycle save target (1→2→…→8→1) |
| Short press | dirty, preset loaded | Reload current preset (revert) |
| Short press | not dirty | Cycle: Manual→1→2→…→8→Manual |
| Long press | not save mode | Jump to Manual (read hardware knobs) |

### FS2 actions

| Event | State | Action |
|---|---|---|
| Short press | save mode | Cancel save → NORMAL |
| Short press | normal | Toggle bypass |
| Long press | save mode | Confirm save → SAVE_CONFIRM |
| Long press | normal | Enter SAVE_MODE |

---

## LED Blink Engine

### LED 1 — Preset pattern

Pre-computed lookup table: each preset (1–8) maps to a sequence of `{duration_ms, led_on}` steps.

```cpp
struct BlinkStep {
    uint16_t duration_ms;
    bool led_on;
};

// Example: Preset 4 = IV = [150ms on, 200ms off, 950ms on, 600ms off]
```

The engine:
- Stores the current step array and index
- In the main loop tick, decrements a countdown timer
- When timer expires, advances to next step (wrapping), sets LED, loads next duration
- `StartPattern(preset_num)` resets to step 0
- `StopPattern()` turns LED off

In **manual mode** (preset 0): LED 1 is off. No waveform indicator — waveform is audible.

### LED 2 — State indicator

Multiple mutually-exclusive patterns:
- **Solid on** — active (not bypassed, not dirty)
- **Off** — bypassed
- **Rapid flash** (50/50 ms) — dirty (preset edited)
- **Fast blink** (150/150 ms) — save mode
- **Burst flash** (75/75 ms for 500 ms) — save confirmed

Same step-based engine, but simpler patterns. The save-confirm burst uses a separate countdown to auto-return to normal after 500 ms.

---

## Knob / Switch Handling Changes

### Current behavior (no preset system)

Knobs are read directly in `AudioCallback` via `hw.GetKnobValue()`. There's no edit buffer — the audio callback reads hardware directly every block.

### New behavior (with preset system)

**The edit buffer is the DSP's input.** The audio callback reads from the edit buffer, not from hardware knobs.

Flow:
1. **Manual mode:** Every knob change writes directly to edit buffer. DSP always matches hardware.
2. **Preset loaded:** Edit buffer holds preset values. Knob movement overwrites that parameter in the edit buffer and sets dirty flag.
3. **Preset load:** Copy stored preset into edit buffer. All 6 knobs + 2 switches jump immediately.

### Where knob reading happens

Option A: Read knobs in audio callback (current), compare to edit buffer
Option B: Read knobs in main loop, update edit buffer, audio callback reads edit buffer

**Recommended: Option B.** The main loop already runs every 10 ms — plenty fast for knob response. This keeps the audio callback clean and avoids race conditions with the preset system.

Implementation:
- Main loop reads all 6 knobs + SW1 + SW2 every tick
- If any value changed significantly (dead zone to avoid jitter), update edit buffer
- Audio callback reads from a `current_params` struct that the main loop writes

### Switch 3 (mode select)

SW3 is **not** part of the edit buffer — it's a global control. Mode switching logic runs in the main loop.

---

## Flash Persistence

### When to save to flash

1. **Mode switch** — save departing mode's full state (edit buffer + active preset + dirty)
2. **Save confirm** — write edit buffer to target preset slot
3. **Periodic auto-save** — save edit buffer state every ~30 seconds if changed (protects against power loss)

### PersistentStorage usage

```cpp
PersistentStorage<StorageData> flash(hw.seed.qspi);

// Init with factory defaults
StorageData defaults = {};
defaults.version = 1;
// All modes: manual, clean, knobs at 0.5, switches at UP
flash.Init(defaults);

// Load on boot
StorageData& data = flash.GetSettings();
// Restore state.mode, edit buffers, presets, etc.

// Save
auto& data = flash.GetSettings();
// Copy current state into data
flash.Save();
```

### First boot (factory state)

`PersistentStorage::GetState()` returns `FACTORY`. All modes start in Manual with knobs read from current hardware positions.

---

## Implementation Steps

### Step 1: Constants and data structures

- Add all timing constants to `constants.h`
- Create `src/preset_system.h` with:
  - `ModePresetData`, `ModeState`, `StorageData` structs
  - `BlinkStep` struct and pre-computed pattern tables
  - `PresetSystem` class declaration

### Step 2: LED blink engine

- Implement `BlinkEngine` (or inline in PresetSystem):
  - `StartPresetPattern(uint8_t preset)` — loads step array, resets timer
  - `StopPresetPattern()` — LED 1 off
  - `SetLed2Mode(Led2Mode mode)` — switches LED 2 pattern
  - `Tick(uint32_t elapsed_ms)` — advances both LED timers, sets LED hardware

### Step 3: Footswitch state machine

- Implement footswitch processing:
  - Track press start times, long-press-fired flags
  - Bootloader dual-hold detection (highest priority)
  - Dispatch short/long press events to handlers
- Implement all FS1/FS2 handlers per the state table above

### Step 4: Edit buffer integration

- Add `current_params` struct readable by audio callback
- Move knob/switch reading from audio callback to main loop
- Audio callback reads from `current_params` instead of `hw.GetKnobValue()`
- Dirty detection: any knob/switch change while a preset is loaded

### Step 5: Flash storage

- Define `StorageData` with `operator!=`
- Init `PersistentStorage` in `main()`
- Load state on boot (or read hardware on factory boot)
- Save on mode switch, save confirm, and periodic auto-save

### Step 6: Mode switching

- SW3 change detection in main loop
- Save departing mode state, restore incoming mode state
- Update LEDs to reflect restored state

### Step 7: Integration and cleanup

- Remove old LED waveform indicator logic
- Remove old `bypass ^= RisingEdge()` from audio callback
- **Keep** `hw.CheckResetToBootloader()` in main loop (Phase 1 — FS1 held 2 s still works)
- Wire everything together in main loop

### Step 8: Documentation

- Update `PROJECT.md`: Current Status, Next, fix save-confirm duration
- Update `README.md`: controls and LEDs tables for the new system

---

## Design Decisions (Resolved)

1. **LED 1 in manual mode:** Off. No waveform indicator. Waveform is audible.
2. **Knob dead zone for dirty detection:** 0.02 (2% of travel). Avoids ADC jitter false triggers.
3. **Save mode timeout:** None. Save mode stays active until explicitly confirmed or cancelled.

---

## Pre-computed Blink Patterns

Reference for implementation. Each row ends with the repeat gap (off).

| Preset | Symbols | Steps |
|---|---|---|
| 1 (I) | `[150 on, 600 off]` |
| 2 (II) | `[150 on, 200 off, 150 on, 600 off]` |
| 3 (III) | `[150 on, 200 off, 150 on, 200 off, 150 on, 600 off]` |
| 4 (IV) | `[150 on, 200 off, 950 on, 600 off]` |
| 5 (V) | `[950 on, 600 off]` |
| 6 (VI) | `[950 on, 200 off, 150 on, 600 off]` |
| 7 (VII) | `[950 on, 200 off, 150 on, 200 off, 150 on, 600 off]` |
| 8 (VIII) | `[950 on, 200 off, 150 on, 200 off, 150 on, 200 off, 150 on, 600 off]` |

Max steps: 8 (preset 8). Total pattern length for preset 8: 950+200+150+200+150+200+150+600 = 2600 ms.
