# Preset System — Implementation

As-built reference for the preset system. Spec lineage: `docs/PROJECT.md` § Preset System and `docs/PRESET_GLOBAL_PLAN.md` (the migration plan from per-mode to global+banks). Timing-constant source of truth: `docs/ux-demo.html`.

---

## Model

- **Single global edit buffer.** One `ModePresetData` record holds knobs + sw1 + sw2 + mode. Manual mode is fully WYSIWYG — no hidden state outside presets.
- **Banks of presets.** `NUM_BANKS` banks (default 3, storage sized for `MAX_BANKS = 6`). Each bank has `NUM_PRESETS = 8` slots. Only the active bank is addressable via FS1 at any given moment.
- **Slot carries mode.** Every preset record stores which mode it belongs to (the SW3 position it was saved under). Cycling presets across the bank can swap mode + parameters in one footswitch press.
- **SW3 is a soft control.** Uniform with SW1/SW2: moves the edit buffer's mode; on a saved preset, marks dirty. No per-mode state preserve/restore.

---

## Data structures (`src/preset_system.h`)

```cpp
static constexpr int NUM_MODES   = 3;
static constexpr int NUM_PRESETS = 8;
static constexpr int NUM_KNOBS   = 6;
static constexpr int NUM_BANKS   = 3;   // user-facing bank count
static constexpr int MAX_BANKS   = 6;   // storage cap (Roman LED scheme caps at VI)

struct ModePresetData {
    float knobs[NUM_KNOBS];
    uint8_t sw1;
    uint8_t sw2;
    uint8_t mode;          // 0=A, 1=B, 2=C — sw3 value this slot belongs to
};

struct Bank {
    ModePresetData presets[NUM_PRESETS];
    bool preset_saved[NUM_PRESETS];
};

struct GlobalState {
    ModePresetData edit_buffer;
    Bank banks[MAX_BANKS];
    uint8_t active_bank;       // 0 .. NUM_BANKS-1
    uint8_t active_preset;     // 0 = manual, 1..NUM_PRESETS = preset
    bool dirty;
};

struct StorageData {
    uint32_t version;
    GlobalState state;
};

static constexpr uint32_t STORAGE_VERSION = 3;
```

Sizing: `GlobalState` ≈ 1.6 KB at `MAX_BANKS=6`. Fits comfortably in `PersistentStorage`.

### Legacy v2 layout (kept only for one-shot migration)

`namespace legacy_v2 { struct StorageData { ... ModeState modes[3]; uint8_t last_mode; }; }` — the pre-rewrite layout, used to read existing per-mode bank data and remap it into the new banks.

---

## Pedal state machine

Identical to the original spec — banks/global don't change this:

```
        ┌──────────┐
  ┌─────│  NORMAL  │
  │     └──────────┘
  │          │  FS2 long press
  │          ▼
  │    ┌───────────┐
  │    │ SAVE_MODE │
  │    └───────────┘
  │          │  FS2 long (confirm) / FS2 short (cancel)
  │          ▼
  │   ┌──────────────┐
  └───│ SAVE_CONFIRM │  (500 ms burst, then → NORMAL)
      └──────────────┘
```

Bank cycling is **state-independent** — it works in NORMAL and SAVE_MODE (where it lets you save into a different bank without leaving save mode).

---

## Footswitches

Single owner: `PresetSystem::ProcessFootswitches()`. The Hothouse FS1-only bootloader path (`hw.CheckResetToBootloader()`) is **dropped** — too easy to trigger accidentally while preset-cycling.

### Detection logic

Per tick (~10 ms):

1. Read FS1 / FS2 `Pressed()`.
2. Track rising edges → `fs*_down_time_`. Track `fs*_long_fired_`.
3. While both are down: suppress individual short/long actions; the combo owns the gesture.
4. Falling edge of either with combo active → evaluate combo hold time and act.

### Gesture table

| Gesture | Action |
|---|---|
| FS1 short (released before `FS_LONG_PRESS_MS`) | Save mode: cycle save target slot. Dirty + on saved preset: revert. Else: cycle Manual → 1 → … → 8 → Manual within active bank. |
| FS1 long (held past `FS_LONG_PRESS_MS`) | Jump to Manual — read all hardware including SW3 into edit buffer. |
| FS2 short | Save mode: cancel → NORMAL. Else: toggle bypass. |
| FS2 long | Save mode: confirm save (writes edit buffer to `banks[active_bank].presets[save_target-1]`, then SAVE_CONFIRM). Else: enter SAVE_MODE (target slot pre-selected to current preset, or 1 from manual). |
| Both short (released < `FS_LONG_PRESS_MS`) | Cycle bank: `active_bank = (active_bank+1) % NUM_BANKS`. Per context: manual → bank shifts, edit buffer preserved. On preset → load same slot in new bank. Save mode → bank shifts, save target unchanged. |
| Both held ≥ `FS_BOOT_HOLD_MS` (2 s) | Arm bootloader request. Main loop calls `System::ResetToBootloader()` after a 1.2 s alternating LED1/LED2 burst (`75 ms` per phase × 8 pairs). |

### Constants (in `src/constants.h`)

| Constant | Value | Description |
|---|---|---|
| `FS_LONG_PRESS_MS` | 700 | long-press / combo-short threshold |
| `FS_BOOT_HOLD_MS` | 2000 | both-FS bootloader hold |
| `KNOB_DIRTY_THRESHOLD` | 0.02 | knob movement → live takeover threshold |

---

## LED engine

### LED 1 — preset blink (Roman numeral)

Pre-computed `PRESET_PATTERNS[NUM_PRESETS]` table, one step array per slot, encoding I/V/X. Manual mode (active_preset = 0): LED off. Same as pre-rewrite.

Timing constants (LED 1):

| Constant | Value |
|---|---|
| `LED_SHORT_ON_MS` | 150 |
| `LED_LONG_ON_MS` | 950 |
| `LED_ELEM_GAP_MS` | 200 |
| `LED_REPEAT_GAP_MS` | 700 |

### LED 2 — state indicator

Modes (mutually exclusive): `SOLID`, `OFF`, `DIRTY`, `SAVE_MODE`, `SAVE_CONFIRM`.

| Mode | Pattern | Constants |
|---|---|---|
| SOLID | always on | — |
| OFF | always off (bypass) | — |
| DIRTY | rapid flash | `LED_DIRTY_ON_MS` = 50, `LED_DIRTY_OFF_MS` = 50 |
| SAVE_MODE | fast blink | `LED_SAVE_MODE_ON_MS` = 150, `LED_SAVE_MODE_OFF_MS` = 150 |
| SAVE_CONFIRM | burst | `LED_SAVE_CONFIRM_ON_MS` = 75, `LED_SAVE_CONFIRM_OFF_MS` = 75, `LED_SAVE_CONFIRM_DUR_MS` = 500 |

`UpdateLed2Mode()` selects based on `pedal_state_`, `bypass_`, `dirty_`.

### Bank-switch burst (both LEDs, on bank change)

A one-shot routine that takes over both LEDs for a fixed total duration, then yields back to LED 1 / LED 2's normal owners.

- **Both LEDs in sync** — visually distinguishes from LED 1's preset blink (single LED) and LED 2's dirty/save patterns (single LED).
- **Each pulse is internal fast flicker** — deterministic `LED_BANK_FLICKER_MS` (20 ms) on/off chunks. Reads visually distinct from a clean pulse.
- **Pulse count = Roman numeral count of new bank.** Bank 1 = 1 pulse, bank 2 = 2 pulses, bank 3 = 3 pulses.
- **Fixed total burst time.** `LED_BANK_TOTAL_MS = 1200` covers all pulses + inter-pulse gaps. Each pulse gets `(LED_BANK_TOTAL_MS - (N-1) × LED_BANK_GAP_MS) / N` of flicker; bank 1 = one 1200 ms pulse, bank 2 = two 525 ms pulses + 150 ms gap, bank 3 = three 300 ms pulses + 2 × 150 ms gaps.
- **Trailing pause** `LED_BANK_HOLD_MS = 400` before LEDs return to normal.
- I/V pulse-duration distinction is dropped — count alone differentiates 1–3. For `NUM_BANKS > 3` we'd need V back to keep IV/V/VI legible.

Constants:

| Constant | Value |
|---|---|
| `LED_BANK_FLICKER_MS` | 20 |
| `LED_BANK_TOTAL_MS` | 1200 |
| `LED_BANK_GAP_MS` | 150 |
| `LED_BANK_HOLD_MS` | 400 |

### Bootloader LED burst (NitroTron3.cpp)

When `PresetSystem::ShouldEnterBootloader()` returns true: stop ADC + audio, alternate LED 1 / LED 2 at 75 ms per phase for 8 iterations (~1.2 s total), then `System::ResetToBootloader()`.

---

## Edit buffer + knob handling

The audio callback reads from `state_.edit_buffer` via `preset.GetEditBuffer()`. The main loop (`PresetSystem::Tick` at 10 ms) runs `ProcessKnobsAndSwitches()` which mediates hardware → edit buffer.

### Knob takeover (live / frozen)

Each knob has a `knob_live_[i]` flag. After preset load or `SnapshotHardware()`, all knobs are `live=false` — the edit buffer holds the loaded value and ignores hardware until the user moves the knob past `KNOB_DIRTY_THRESHOLD`. Once a knob crosses that, it flips to `live=true` and edit buffer tracks hw at full ADC resolution every tick.

This is the "soft pickup" pattern — knob position on the panel doesn't have to match the loaded preset value, but the audio still uses the preset's value until you touch that knob.

### Switch movement (SW1, SW2, SW3)

Each switch is tracked against `last_hw_sw*_` (snapshot baseline). A change updates the edit buffer and (on a saved preset) marks dirty. SW3 follows the same pattern — preset loads may change `current_mode_` to differ from the physical SW3 position, but dirty fires only on actual physical movement.

### `SnapshotHardware()`

Reads current hw knob values and SW1/SW2/SW3 positions into `last_hw_*` baselines, and freezes all knobs (`knob_live_ = false`). Called after preset load, FS1 long, and similar baseline-reset events. Deferred to the **first Tick** (not Init) so it runs after `hw.StartAdc()` has produced real ADC values.

---

## Flash persistence

### Debounced auto-save

Every state mutation calls `MarkStateChanged()` which sets `save_pending_ = true` and zeroes `idle_since_change_ms_`. The Tick increments idle, and when it reaches `AUTOSAVE_DEBOUNCE_MS` (2000 ms) with no further changes, fires one `SaveToFlash()` and clears the pending flag.

- **No changes ever → no flash writes.**
- Replaces the original fixed 30 s interval (which fired even when nothing changed).
- Power-cycle loss window: ~2 s of the last edit. Save-confirm (FS2 long) saves immediately to bypass the debounce.

Call sites that `MarkStateChanged()`:
- `ProcessKnobsAndSwitches` (any knob takeover or SW1/SW2 movement)
- `ProcessModeSwitchHardware` (SW3 movement)
- `OnFs1ShortPress` (preset cycle / dirty revert / manual)
- `OnFs1LongPress` (jump to manual)
- `CycleBank` (bank advance)

`OnFs2LongPress` save-confirm path calls `SaveToFlash()` directly and clears `save_pending_`.

### Migration (v2 → v3)

On boot, if `data.version == 2`:
1. Reinterpret the loaded RAM buffer's first `sizeof(legacy_v2::StorageData)` bytes as `legacy_v2::StorageData` (memcpy into a stack instance).
2. Build a fresh `GlobalState` from it: each old mode m's 8 presets → `banks[m]`, with each slot's new `mode` field stamped to `m`. `active_bank = last_mode`, `active_preset / dirty / edit_buffer` come from the last active mode.
3. Write back v3 layout, save.

Banks 3..5 stay at factory defaults. Active bank starts where the pedal left off.

### Factory boot

If `PersistentStorage::GetState() == FACTORY` (first ever boot or fully wiped flash), the edit buffer reads hardware (`current_mode_ = SW3`, knobs/SW1/SW2 → buffer). All preset slots in all banks remain empty.

---

## Behavior summary (per gesture × context)

| Gesture | Manual (preset=0) | On saved preset | On empty slot | Save mode |
|---|---|---|---|---|
| Knob movement past threshold | Edit buffer updates | Dirty + LED 2 flash | Edit buffer updates (acts like manual) | Frozen — change ignored |
| SW1 / SW2 movement | Edit buffer updates | Dirty + LED 2 flash | Edit buffer updates | Frozen |
| SW3 movement | Mode + edit buffer.mode update | Dirty + LED 2 flash; mode follows SW3 | Mode + edit buffer.mode update | Frozen |
| FS1 short | Cycle to preset 1 | Dirty: revert / clean: cycle to next | Cycle to next | Cycle save target |
| FS1 long | (no-op, already manual) | Jump to manual (reads hardware) | Jump to manual | (no-op) |
| FS2 short | Toggle bypass | Toggle bypass | Toggle bypass | Cancel save |
| FS2 long | Enter save mode (target = 1) | Enter save mode (target = current) | Enter save mode (target = current) | Confirm save |
| Both short (bank cycle) | Bank advances, edit buffer preserved | Bank advances, load same slot in new bank | Bank advances, load same slot in new bank | Bank advances, save target unchanged |
| Both held 2 s | Bootloader (LED burst → reset) | Bootloader | Bootloader | Bootloader |

---

## File layout

| File | Purpose |
|---|---|
| `src/preset_system.h` | `PresetSystem` class — state machine, footswitch + knob handling, LED engine, flash storage, migration |
| `src/constants.h` | All timing constants |
| `src/NitroTron3.cpp` | `preset.Init()`, `preset.Tick(10)`, audio callback reads `preset.GetEditBuffer()`, polls `preset.ShouldEnterBootloader()` |

UI prototype: `docs/ux-demo.html` (browser playground mirroring the firmware behaviour for fast iteration on timing constants and gesture flow).
