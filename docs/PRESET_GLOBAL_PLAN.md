# Global Preset System — Plan

Status: implemented through P.5; P.6 listening pass and P.7 docs in progress. As-built reference: `docs/PRESET_IMPL.md`. This document captures the original plan and notes where the implementation diverged.

Open decisions in the previous revision have been resolved:
- **Edit buffer model:** Option 1 — single global edit buffer (no per-mode buffers; manual mode is fully WYSIWYG with no hidden state).
- **Slot count:** 8 per bank (Roman LED scheme works cleanly up to VIII).
- **SW3 treatment:** uniform with SW1/SW2 — moving it on a preset marks dirty, FS1 short reverts.
- **Banks:** added in this revision (see § Banks).

Divergences from the plan during implementation (kept for the record; full as-built details in `PRESET_IMPL.md`):
- **Migration instead of factory reset.** V2 → V3 migrates existing per-mode presets into banks 1/2/3 (each slot tagged with its source mode = the SW3 position it was saved under). No data loss.
- **Bank burst is fixed-total, equal pulses.** `LED_BANK_TOTAL_MS = 1200` divided by pulse count; bank 1 = one 1200 ms pulse, bank 2 = 2 × 525 + 150 gap, bank 3 = 3 × 300 + 2 × 150 gaps. I/V duration distinction dropped (only 3 banks reachable, count alone differentiates).
- **Debounced auto-save** replaces the original 30 s fixed interval. Any state change resets a 2 s idle timer; save fires once on idle, then stops until the next change. No-change → no flash writes.
- **FS1-alone bootloader path dropped** (it was too easy to trigger by accident while preset-cycling). Both-FS held 2 s is now the sole DFU entry, with a 1.2 s alternating-LED burst as visible confirmation.
- **SW3 dirty fires on physical movement** (tracked via `last_hw_sw3_` snapshot), not on mismatch between the loaded preset's stored mode and the SW3 position.
- **Init snapshot deferred to first Tick** so it runs after `hw.StartAdc()` and captures real ADC values — otherwise the first tick after boot would interpret the ADC warm-up delta as knob movement and falsely mark dirty.

---

## Goal

Today: 3 modes × 8 slots = 24 stored presets, all stuck inside the current SW3 mode.

After this change: **8 slots × N banks** (active count = 3, designed for up to 6), each slot carrying its own mode. Any preset can recall any of the three main modes plus the full parameter set in one footswitch press. Banks expand the addressable preset space without adding new buttons. The pedal becomes a true "scene recall" pedal across modes, not three independent pedals stacked behind SW3.

The Roman-numeral LED scheme works for both preset numbers (LED 1) and bank numbers (both LEDs, burst), capping cleanly at VIII for presets and VI for banks. Going beyond either requires a different encoding — flagged as out of scope.

---

## Data structure

```cpp
static constexpr int NUM_PRESETS = 8;          // unchanged
static constexpr int NUM_KNOBS   = 6;          // unchanged
static constexpr int NUM_BANKS   = 3;          // active bank count — user-facing
static constexpr int MAX_BANKS   = 6;          // storage upper bound (Roman LED scheme goes I..VI)
// NUM_MODES = 3 stays as a Mode A/B/C enum count, but no longer drives preset storage layout

struct GlobalPresetData {
    uint8_t mode;                              // 0=A, 1=B, 2=C — IS the sw3 value
    float   knobs[NUM_KNOBS];
    uint8_t sw1;
    uint8_t sw2;
};

struct Bank {
    GlobalPresetData presets[NUM_PRESETS];
    bool             preset_saved[NUM_PRESETS];
};

struct GlobalState {
    GlobalPresetData edit_buffer;              // single global edit buffer
    Bank             banks[MAX_BANKS];         // only first NUM_BANKS reachable; extras lie dormant
    uint8_t          active_bank;              // 0..NUM_BANKS-1
    uint8_t          active_preset;            // 0 = manual, 1..8 = preset
    bool             dirty;
};

struct StorageData {
    uint32_t    version;                       // bump to 3 (single rewrite covers global + banks)
    GlobalState state;
};

static constexpr uint32_t STORAGE_VERSION = 3;
```

Sizing: `GlobalPresetData` ≈ 32 B → bank ≈ 32×8 + 8 = 264 B → MAX_BANKS=6 banks ≈ 1.6 KB. Fits comfortably in `PersistentStorage`. Storing all MAX_BANKS lets us raise `NUM_BANKS` later without another factory reset.

---

## Behavior changes

### SW3 (mode switch) becomes a soft control

The hardware switch is no longer the sole source of truth for `current_mode`. The preset's stored `mode` drives `current_mode` whenever a preset is active; SW3 is treated uniformly with SW1/SW2:

- **Manual (active_preset = 0):** hardware drives everything. Moving SW3 updates `edit_buffer.mode` and `current_mode` immediately.
- **On a preset:** moving SW3 marks the preset dirty (same rule as SW1/SW2 today) and `current_mode` / `edit_buffer.mode` follow SW3. FS1 short reverts to the preset, including its stored mode.

### Edit buffer model

**Single global edit buffer** — one buffer shared across all modes, containing knobs + sw1 + sw2 + mode. There is no hidden state outside presets: manual mode is always exactly what the panel shows. Switching modes via SW3 in manual carries the edit buffer with you (knobs preserved; the new mode just reinterprets them).

### Banks

8 slots × `NUM_BANKS` banks. Only the active bank's slots are addressable via FS1 at any given moment.

**Both-FS short tap cycles bank:** `active_bank = (active_bank + 1) % NUM_BANKS`. Per-context behavior:

- **Manual (active_preset = 0):** bank advances, edit buffer untouched, `active_preset` stays 0. Lets you keep tweaking — the slot space underneath just becomes the new bank's slots.
- **On a preset (active_preset > 0):** bank advances, then load same slot number in the new bank. Saved → values jump, dirty clears (same as a fresh FS1 cycle). Empty → empty-slot behaviour (knobs flow into edit buffer freely, LED 1 still shows the slot number).
- **In save mode:** bank advances, `save_target` slot number unchanged but now points at the new bank's slot. Save-mode LED2 pattern and target-slot LED1 pattern continue. Confirms save into the new bank when FS2 long fires. This is the cross-bank save path.

`active_bank` is persisted; boot restores into the last active bank.

### Both-FS gesture timing

Both footswitches detected pressed within ~50 ms of each other. While both are down, individual FS short and long presses are suppressed — the combo owns the gesture. On release:

| Combined hold duration | Action |
|---|---|
| < 700 ms (long-press threshold) | Bank cycle |
| 700–2000 ms | Deadband — no action |
| ≥ 2000 ms (bootloader hold) | DFU bootloader |

The deadband prevents accidental DFU entry from a sloppy bank-tap and prevents accidental bank cycles from an aborted DFU hold. Same thresholds as the existing single-FS long-press / bootloader-hold constants — no new timing knobs.

### Bank-switch indication (burst on both LEDs)

When `active_bank` changes, both LEDs play a one-shot Roman-numeral burst encoding the new bank number (I, II, III for banks 1–3; IV, V, VI reserved up to MAX_BANKS):

- **Both LEDs in sync** — visually distinguishes the burst from a preset blink (LED 1 only) or a dirty/save flash (LED 2 only).
- **Each symbol is a flicker burst, not a clean pulse.** "I" = a short-pulse-length window (`LED_SHORT_ON_MS`) filled with deterministic fast on/off chunks (e.g., 20 ms / 20 ms). "V" = a long-pulse-length window (`LED_LONG_ON_MS`) filled the same way.
- **Same element-gap** between symbols (`LED_ELEM_GAP_MS`).
- **Plays once**, then a trailing pause, then both LEDs return to their normal state (LED 1 = preset blink, LED 2 = bypass / dirty / save-mode pattern).

Two new constants:
```cpp
constexpr uint32_t LED_BANK_FLICKER_MS = 20;   // on/off chunk inside the burst
constexpr uint32_t LED_BANK_HOLD_MS    = 400;  // trailing pause after the burst before LEDs return to normal
```

### FS1 cycling

`Manual → preset 1 → … → preset 8 → Manual`, identical UX to today but the cycle now spans all modes. Stepping from preset 3 (Mode A) to preset 4 (Mode C) instantly switches mode and parameters together.

### FS1 long press → manual

Unchanged: jumps to manual, reads hardware (now including SW3 for `mode`).

### FS2 save flow

Unchanged in UX: long-press FS2 to enter save mode, FS1 to cycle target slot 1..8, FS2 long-press again to confirm. The saved slot captures the edit buffer including its current `mode`.

### Dirty detection

Same as today (knob movement above `KNOB_DIRTY_THRESHOLD`, or SW1/SW2 movement) — now extends to SW3 movement while on a saved preset.

### Boot

Restore `active_bank`, `active_preset`, and edit buffer. If `active_preset > 0`, the preset's `mode` drives audio dispatch (with `mode` field from `banks[active_bank].presets[active_preset-1]`). If `active_preset == 0` (manual), read all hardware including SW3 into the edit buffer.

---

## Audio side

`PresetSystem::GetCurrentMode()` already returns `current_mode_`. The audio callback's mode dispatch stays unchanged. The only difference: `current_mode_` now changes on preset recall, not just on SW3 move.

Mode switches already incur stale-state issues today (e.g., Mode C filter integrator state, env follower memory) when SW3 is moved. Preset recall doesn't introduce a new failure mode — same code path, same risk. If glitches show up on rapid preset-driven mode switches, deal with them per-mode (reset filter state in the mode's per-callback init) — but expect this to already be working from the existing SW3 path.

---

## Storage migration

Storage version bumps from 2 → 3. On boot, `data.version != STORAGE_VERSION` triggers `RestoreDefaults()` — existing user presets are wiped. This is a one-time cost and the simplest path; preserving old data isn't worth the migration code for a small project.

Factory boot path (untouched): reads hardware into the (single) edit buffer.

---

## Files touched

- `src/preset_system.h` — bulk of the work. Replace `ModeState modes[NUM_MODES]` with `GlobalState state` (including `banks[MAX_BANKS]` and `active_bank`). Rewrite `ProcessModeSwitchHardware()` so SW3 changes update `state.edit_buffer.mode` (manual) or trigger dirty (preset active). Update `LoadPreset()` to set `current_mode_` from the slot's `mode` field. Add both-FS gesture detection and `CycleBank()`. Add bank-burst LED routine. Update `MakeDefaults()`.
- `src/NitroTron3.cpp` — no structural change; `preset.GetCurrentMode()` and `preset.GetEditBuffer()` still drive dispatch. Only behavioral side: the callback may see `current_mode_` change without a physical SW3 move. Already legal.
- `src/constants.h` — add `LED_BANK_FLICKER_MS`, `LED_BANK_HOLD_MS`. `LED_REPEAT_GAP_MS` already bumped to 700 ms (independent change).
- `docs/PRESET_IMPL.md` — rewrite for the global + banks model. (Defer until code lands.)
- `README.md` — controls table for FS1/FS2 unchanged; FS1+FS2 short-tap behaviour added; preset semantics description updates. (Defer.)

LED 1 and LED 2 logic for normal preset / dirty / bypass / save modes untouched. The Roman-numeral patterns for slots 1..8 work identically; the bank burst is a new on-top routine that temporarily takes both LEDs.

---

## Implementation order

Each step is one buildable, flashable commit. UX prototype already lives in `docs/ux-demo.html` and reflects the full target behaviour — code follows the demo.

### P.1 — Struct + storage rewrite

Replace `ModeState modes[NUM_MODES]` with `GlobalState state` in `preset_system.h`, including `Bank banks[MAX_BANKS]` and `active_bank`. Update `MakeDefaults()`, `Init()`, `SaveToFlash()`. Bump `STORAGE_VERSION = 3`. Update all references in helpers (`LoadPreset`, `IsPresetSaved`, `ReadHardwareIntoEditBuffer`, etc.) to take an implicit `active_bank` index. At this point everything compiles and runs but SW3 still drives mode directly — slot `mode` field exists but is not used yet; banks 2/3 are reachable only via the new gesture (not yet wired).

### P.2 — Wire `mode` field on save / recall

`LoadPreset()` sets `current_mode_` from the slot's `mode`. Save captures `current_mode_` into the slot. FS1 long press (jump to manual) reads SW3 into `edit_buffer.mode`.

### P.3 — Rewrite `ProcessModeSwitchHardware()`

In manual: SW3 changes update `edit_buffer.mode` and `current_mode_`. In preset: SW3 movement marks the preset dirty, updates `current_mode_` (and `edit_buffer.mode`) to follow. Snapshot SW3 in `SnapshotHardware()` so movement is detected the same way SW1/SW2 are.

### P.4 — Both-FS gesture + bank cycle

Detect both-FS short tap (released before `FS_LONG_PRESS_MS`) and long hold (≥ `FS_BOOT_HOLD_MS`). While both are down, suppress individual FS1/FS2 actions. On short release, call `CycleBank()`:
- Advance `active_bank`.
- Manual: edit buffer untouched.
- On a preset: call `LoadPreset(active_preset)` against the new bank (saved → load + clear dirty; empty → empty-slot path).
- Save mode: `save_target` unchanged; bank context shifts.

Existing single-FS bootloader detection (`FS1+FS2 ≥ 2 s`) keeps working because the deadband sits between long-press and bootloader thresholds.

### P.5 — Bank-burst LED routine

Add a one-shot routine that drives both LEDs in sync through a Roman-numeral burst pattern with internal flicker (chunks of `LED_BANK_FLICKER_MS`), then a `LED_BANK_HOLD_MS` trailing pause, then yields LEDs back to their normal owners. Trigger it whenever `active_bank` changes.

### P.6 — Listening pass

Flash and verify:
- Saving in Mode A, then Mode C, then Mode B; cycling presets jumps modes correctly within the active bank.
- FS1 long press returns to manual + current SW3 position + current bank.
- SW3 move on a saved preset triggers dirty; FS1 short reverts.
- Both-FS short tap cycles bank correctly from manual / on-preset / in save mode; the burst plays and LEDs return cleanly.
- Both-FS held to 2 s still enters DFU.
- Boot restores active bank + active preset + the preset's mode.

### P.7 — Documentation

Rewrite `docs/PRESET_IMPL.md` and update README controls description.

---

## Open / accepted notes

- **Mode-mismatch indicator.** Not implementing — the audio difference between SW3's physical position and the preset's stored mode is itself the indicator. Revisit after living with it.
- **Boot under physical SW3 mismatch.** Preset's stored mode wins until the user moves SW3. Confirm during P.6 listening.
- **Existing preset wipe.** Version 2 → 3 triggers factory reset; all saved presets are lost. Acceptable.
- **Raising NUM_BANKS later.** Storage holds `MAX_BANKS = 6` from day one, so bumping `NUM_BANKS` later is a recompile only — no factory reset.
