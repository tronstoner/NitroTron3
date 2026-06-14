# Global Preset System — Plan

Status: planning. Replaces the per-mode preset bank with a single global bank where each slot stores its own mode. Requires a storage version bump and one-time factory reset on first boot.

---

## Goal

Today: 3 modes × 8 slots = 24 stored presets. Cycling presets stays within the current SW3 mode.

After this change: **8 slots total**, each carrying its own mode. Any preset can recall any of the three main modes plus the full parameter set in one footswitch press. The pedal becomes a true "scene recall" pedal across modes, not three independent pedals stacked behind SW3.

Slot count stays at 8 for now (the LED 1 Roman-numeral pattern caps cleanly at VIII). Going higher is feasible — flash has plenty of room — but requires a different LED 1 encoding. Flagged as an open question, not a blocker.

---

## Data structure

The preset bank is global in both options. The split is whether there is **one edit buffer** or **one edit buffer per mode** — see "Open decision 1" below. Both are workable; storage cost difference is trivial.

```cpp
static constexpr int NUM_PRESETS = 8;          // unchanged
static constexpr int NUM_KNOBS   = 6;          // unchanged
// NUM_MODES = 3 stays as a Mode A/B/C enum count, but no longer drives preset storage layout

struct ModePresetData {                        // knobs + sw1 + sw2 (no mode field)
    float   knobs[NUM_KNOBS];
    uint8_t sw1;
    uint8_t sw2;
};

struct GlobalPresetData {                      // ModePresetData + stored mode
    uint8_t mode;                              // 0=A, 1=B, 2=C — IS the sw3 value
    float   knobs[NUM_KNOBS];
    uint8_t sw1;
    uint8_t sw2;
};

// --- Option 1: single global edit buffer ---
struct GlobalState_Opt1 {
    GlobalPresetData edit_buffer;
    GlobalPresetData presets[NUM_PRESETS];
    bool             preset_saved[NUM_PRESETS];
    uint8_t          active_preset;            // 0 = manual, 1..8 = preset
    bool             dirty;
};

// --- Option 2: per-mode edit buffers, global presets ---
struct GlobalState_Opt2 {
    ModePresetData   edit_buffers[NUM_MODES];  // one per mode, preserved across SW3
    GlobalPresetData presets[NUM_PRESETS];
    bool             preset_saved[NUM_PRESETS];
    uint8_t          active_preset;
    uint8_t          last_mode;                // also remember mode across boot
    bool             dirty;
};

struct StorageData {
    uint32_t    version;                       // bump to 3
    GlobalState state;                         // one of the two above
};

static constexpr uint32_t STORAGE_VERSION = 3;
```

Both shrink storage vs. today's ~0.8 KB layout (per-mode preset banks). Both fit comfortably.

---

## Behavior changes

### SW3 (mode switch) becomes a soft control

The hardware switch is no longer the sole source of truth for `current_mode`. Behavior depends on the edit-buffer option chosen (see Open decision 1):

**Both options, manual mode (active_preset = 0):** hardware drives everything. Moving SW3 changes mode immediately.

**Option 1 (single edit buffer), preset active:** the preset's stored `mode` drives `current_mode`. Moving SW3 while on a saved preset marks it dirty (same rule as SW1/SW2 movement today) and the mode changes to follow SW3. FS1 short reverts to the preset, including its stored mode. Keeps SW3 consistent with how SW1/SW2 are treated.

**Option 2 (per-mode edit buffers), preset active:** the preset's stored `mode` drives `current_mode`. Moving SW3 = "exit preset to manual in the new mode" — `active_preset` → 0, `current_mode` → new SW3 position, edit buffer becomes that mode's stored manual buffer. You cannot be on "preset N but in a different mode" — SW3 is the eject button. Cleaner conceptually but treats SW3 differently from SW1/SW2.

### Edit buffer model

Open. See "Open decision 1" — this is the core branch point.

### FS1 cycling

`Manual → preset 1 → … → preset 8 → Manual`, identical UX to today but the cycle now spans all modes. Stepping from preset 3 (Mode A) to preset 4 (Mode C) instantly switches mode and parameters together.

### FS1 long press → manual

Unchanged: jumps to manual, reads hardware (now including SW3 for `mode`).

### FS2 save flow

Unchanged in UX: long-press FS2 to enter save mode, FS1 to cycle target slot 1..8, FS2 long-press again to confirm. The saved slot captures the edit buffer including its current `mode`.

### Dirty detection

Same as today (knob movement above `KNOB_DIRTY_THRESHOLD`, or SW1/SW2 movement) — now extends to SW3 movement while on a saved preset.

### Boot

Restore `active_preset` and edit buffer. If `active_preset > 0`, the preset's `mode` drives audio dispatch. If `active_preset == 0` (manual), read all hardware including SW3.

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

- `src/preset_system.h` — bulk of the work. Replace `ModeState modes[NUM_MODES]` with `GlobalState state`. Rewrite `ProcessModeSwitchHardware()` so SW3 changes update `state.edit_buffer.mode` (manual) or trigger dirty (preset active). Update `LoadPreset()` to set `current_mode_` from the slot's `mode` field. Update `MakeDefaults()`.
- `src/NitroTron3.cpp` — no structural change; `preset.GetCurrentMode()` and `preset.GetEditBuffer()` still drive dispatch. Only behavioral side: the callback may see `current_mode_` change without a physical SW3 move. Already legal.
- `docs/PRESET_IMPL.md` — rewrite for the global model. (Defer until code lands.)
- `README.md` — controls table for FS1/FS2 unchanged; preset semantics description updates. (Defer.)

LED 1 and LED 2 logic untouched. The Roman-numeral patterns for slots 1..8 work identically.

---

## Implementation order

Each step is one buildable, flashable commit.

### P.1 — Struct + storage rewrite

Swap `ModeState modes[3]` for `GlobalState state` in `preset_system.h`. Update `MakeDefaults()`, `Init()`, `SaveToFlash()`. Bump `STORAGE_VERSION = 3`. Update all references in helpers (`LoadPreset`, `IsPresetSaved`, `ReadHardwareIntoEditBuffer`, etc.). At this point everything compiles and runs but SW3 still drives mode directly — slot mode field exists but is not used yet.

### P.2 — Wire `mode` field on save / recall

`LoadPreset()` sets `current_mode_` from the slot's `mode`. Save captures `current_mode_` into the slot. FS1 long press (jump to manual) reads SW3 into edit buffer's `mode`.

### P.3 — Rewrite `ProcessModeSwitchHardware()`

In manual: SW3 changes update `edit_buffer.mode` and `current_mode_`. In preset: SW3 movement marks the preset dirty, updates `current_mode_` (and `edit_buffer.mode`) to follow. Snapshot SW3 in `SnapshotHardware()` so movement is detected the same way SW1/SW2 are.

### P.4 — Listening pass

Flash and verify:
- Saving in Mode A, then Mode C, then Mode B; cycling presets jumps modes correctly.
- FS1 long press returns to manual + current SW3 position.
- SW3 move on a saved preset triggers dirty; FS1 short reverts.
- Boot restores active preset including its mode.

### P.5 — Documentation

Rewrite `docs/PRESET_IMPL.md` and update README controls description.

---

## Open decisions

### 1. Edit buffer model — single global vs. per-mode (core branch point)

**Option 1 — single global edit buffer.**
- One edit buffer shared by all modes.
- Switching modes via SW3 in manual carries the edit buffer's contents with you — moving from A to B keeps the knob positions you just had in A (which may or may not be meaningful in B).
- SW3 move while on a preset → mark dirty, mode follows SW3. Treats SW3 identically to SW1/SW2.
- Pro: simpler model, less storage state, SW3 is uniform with other switches.
- Con: in-progress tweaks in one mode are lost when SW3 moves to another mode (same as recalling a preset).

**Option 2 — per-mode edit buffers, global preset bank.**
- Three edit buffers (one per mode), preserved across SW3 moves in manual.
- Recalling a preset overwrites the active mode's edit buffer with the preset's values.
- SW3 move while on a preset → exits to manual in the new mode (no dirty/mismatch state).
- Pro: preserves today's nice "tweak in A, switch to B, switch back to A and pick up where you left off" workflow. No dirty-with-mismatched-mode awkwardness.
- Con: SW3 is treated differently from SW1/SW2 (eject vs. dirty). More state to manage.

Storage cost difference between the two is negligible. The decision is purely about which UX feels right.

### 2. Slot count

Stick with 8 (LED 1 pattern works), or go higher (12, 16) and redesign LED 1? Higher is feasible — flash has room — but the Roman-numeral scheme stops being readable above VIII. Recommend keeping 8 unless there's a concrete need for more.

### 3. Mode-mismatch indicator

Should LED 1 or LED 2 signal "preset's mode differs from current SW3 position"? Without it, the user has to read SW3's position against the audio character. Only relevant under Option 1 (Option 2 eliminates the mismatch state on SW3 move). Argues for: clarity. Argues against: adds LED states; the audio difference is itself the indicator. Recommend skipping for now and revisit after living with it.

### 4. Boot under physical SW3 mismatch

If a preset is restored that stores mode A but SW3 is physically at C: with global presets, "what the preset says" wins until the user moves SW3. Acceptable per the SW3-as-soft-control model — but flag during P.4 listening to confirm it doesn't feel wrong.

### 5. Existing preset wipe

Confirmed acceptable: version 2 → 3 triggers factory reset. Anyone with treasured presets needs to re-save them under the new model.
