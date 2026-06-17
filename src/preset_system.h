#pragma once

#include <cstring>
// Requires daisy.h, hothouse.h, and constants.h to be included before this file.
// hothouse.h lacks an include guard, so we cannot include it here.
#include "util/PersistentStorage.h"

using clevelandmusicco::Hothouse;
using daisy::Led;
using daisy::System;

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

static constexpr int NUM_MODES   = 3;
static constexpr int NUM_PRESETS = 8;
static constexpr int NUM_KNOBS   = 6;
static constexpr int NUM_BANKS   = 3;   // user-facing bank count
static constexpr int MAX_BANKS   = 6;   // storage cap (Roman LED scheme caps at VI)

// A preset / edit buffer record. Carries mode alongside knobs+switches so
// any slot can recall any mode in one footswitch press.
struct ModePresetData {
    float knobs[NUM_KNOBS];
    uint8_t sw1;   // Switch 1 position (0/1/2)
    uint8_t sw2;   // Switch 2 position (0/1/2)
    uint8_t mode;  // 0=A, 1=B, 2=C — the sw3 value this slot belongs to
};

struct Bank {
    ModePresetData presets[NUM_PRESETS];
    bool preset_saved[NUM_PRESETS];
};

// Single global state: one edit buffer shared across modes; banks hold the
// addressable preset space (only first NUM_BANKS are reachable via the
// both-FS gesture, storage holds MAX_BANKS so raising the count later is a
// recompile only).
struct GlobalState {
    ModePresetData edit_buffer;
    Bank banks[MAX_BANKS];
    uint8_t active_bank;     // 0 .. NUM_BANKS-1
    uint8_t active_preset;   // 0 = manual, 1..NUM_PRESETS = preset
    bool dirty;
};

struct StorageData {
    uint32_t version;
    GlobalState state;

    bool operator!=(const StorageData& o) const {
        return memcmp(this, &o, sizeof(StorageData)) != 0;
    }
};

static constexpr uint32_t STORAGE_VERSION = 3;

// ---------------------------------------------------------------------------
// Legacy v2 storage layout (kept only for one-shot migration to v3).
// Per-mode preset banks, no slot.mode field. Maps cleanly to the new
// banks model: each mode's 8 slots become one of the first three banks,
// with the slot's mode field stamped to its source mode (= SW3 position
// the preset was saved under).
// ---------------------------------------------------------------------------

namespace legacy_v2 {

struct ModePresetData {
    float knobs[NUM_KNOBS];
    uint8_t sw1;
    uint8_t sw2;
};

struct ModeState {
    ModePresetData edit_buffer;
    ModePresetData presets[NUM_PRESETS];
    bool preset_saved[NUM_PRESETS];
    uint8_t active_preset;
    bool dirty;
};

struct StorageData {
    uint32_t version;
    ModeState modes[NUM_MODES];
    uint8_t last_mode;
};

}  // namespace legacy_v2

// ---------------------------------------------------------------------------
// LED blink pattern tables
// ---------------------------------------------------------------------------

struct BlinkStep {
    uint16_t duration_ms;
    bool led_on;
};

// Max steps for any preset pattern (preset 8 = VIII = 8 steps)
static constexpr int MAX_BLINK_STEPS = 8;

struct PresetPattern {
    BlinkStep steps[MAX_BLINK_STEPS];
    uint8_t num_steps;
};

// Pre-computed patterns for presets 1–8 (index 0 = preset 1)
static const PresetPattern PRESET_PATTERNS[NUM_PRESETS] = {
    // Preset 1: I
    {{{LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 2},
    // Preset 2: II
    {{{LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 4},
    // Preset 3: III
    {{{LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 6},
    // Preset 4: IV
    {{{LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_LONG_ON_MS, true},  {LED_REPEAT_GAP_MS, false}}, 4},
    // Preset 5: V
    {{{LED_LONG_ON_MS, true},  {LED_REPEAT_GAP_MS, false}}, 2},
    // Preset 6: VI
    {{{LED_LONG_ON_MS, true},  {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 4},
    // Preset 7: VII
    {{{LED_LONG_ON_MS, true},  {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 6},
    // Preset 8: VIII
    {{{LED_LONG_ON_MS, true},  {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_ELEM_GAP_MS, false},
      {LED_SHORT_ON_MS, true}, {LED_REPEAT_GAP_MS, false}}, 8},
};

// ---------------------------------------------------------------------------
// Pedal state
// ---------------------------------------------------------------------------

enum class PedalState {
    NORMAL,
    SAVE_MODE,
    SAVE_CONFIRM,
};

enum class Led2Mode {
    SOLID,       // active (not bypassed, not dirty)
    OFF,         // bypassed
    DIRTY,       // rapid flash (50/50 ms)
    SAVE_MODE,   // fast blink (150/150 ms)
    SAVE_CONFIRM // burst flash (75/75 ms for 500 ms)
};

// ---------------------------------------------------------------------------
// PresetSystem
// ---------------------------------------------------------------------------

class PresetSystem {
public:
    void Init(Hothouse& hw, Led& led1, Led& led2,
              daisy::PersistentStorage<StorageData>& storage) {
        hw_      = &hw;
        led1_    = &led1;
        led2_    = &led2;
        storage_ = &storage;

        // Init flash storage with factory defaults
        storage_->Init(MakeDefaults());

        // Load from flash
        StorageData& data = storage_->GetSettings();
        if (data.version == 2) {
            // One-shot migration from per-mode banks → global+banks model.
            // sizeof(StorageData) > sizeof(legacy_v2::StorageData), so the
            // first sizeof(legacy_v2::StorageData) bytes of the buffer hold
            // the old layout verbatim — safe to reinterpret-and-copy.
            legacy_v2::StorageData old;
            std::memcpy(&old, &data, sizeof(old));
            MigrateV2ToV3(old, data);
            data.version = STORAGE_VERSION;
            storage_->Save();
        } else if (data.version != STORAGE_VERSION) {
            // Unknown / corrupt — reset to factory
            storage_->RestoreDefaults();
        }

        // Copy saved state into RAM
        StorageData& saved = storage_->GetSettings();
        state_ = saved.state;
        if (state_.active_bank >= NUM_BANKS) state_.active_bank = 0;

        // current_mode_ derives from edit_buffer.mode (single source of truth
        // for "what mode is loaded"). Slot's mode field is not yet wired in P.1.
        current_mode_ = state_.edit_buffer.mode;
        if (current_mode_ >= NUM_MODES) current_mode_ = 0;

        // Check if factory boot (no preset has been saved in any bank)
        bool factory = (storage_->GetState() ==
                        daisy::PersistentStorage<StorageData>::State::FACTORY);
        if (factory) {
            // Read current hardware into the edit buffer (mode from SW3)
            current_mode_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
            state_.edit_buffer.mode = current_mode_;
            ReadHardwareIntoEditBuffer();
        }

        bypass_        = false;
        pedal_state_   = PedalState::NORMAL;

        // Init footswitch tracking
        fs1_pressed_last_ = false;
        fs2_pressed_last_ = false;
        fs1_down_time_    = 0;
        fs2_down_time_    = 0;
        fs1_long_fired_   = false;
        fs2_long_fired_   = false;

        // Init LED state
        UpdateLed1Pattern();
        UpdateLed2Mode();

        // SnapshotHardware deferred to the first Tick. The ADC isn't started
        // by hw.StartAdc() until after Init returns, so a snapshot here
        // would capture zeros — and the first real ADC reading in
        // ProcessKnobsAndSwitches would then exceed KNOB_DIRTY_THRESHOLD
        // against that zero baseline and mark the loaded preset dirty.
        needs_init_snapshot_ = true;
    }

    // Call from main loop every tick (10 ms)
    void Tick(uint32_t tick_ms) {
        tick_ms_ = tick_ms;

        if (needs_init_snapshot_) {
            // ADC has been running through ≥1 audio block by now — knob
            // values are real, safe to snapshot as the baseline.
            SnapshotHardware();
            needs_init_snapshot_ = false;
        }

        ProcessFootswitches();
        ProcessKnobsAndSwitches();
        ProcessModeSwitchHardware();
        TickLeds(tick_ms);

        // Debounced auto-save: fires AUTOSAVE_DEBOUNCE_MS after the last
        // tracked state change. No changes → no flash write.
        if (save_pending_) {
            idle_since_change_ms_ += tick_ms;
            if (idle_since_change_ms_ >= AUTOSAVE_DEBOUNCE_MS) {
                SaveToFlash();
                save_pending_ = false;
                idle_since_change_ms_ = 0;
            }
        }
    }

    // Read current edit buffer for audio callback
    const ModePresetData& GetEditBuffer() const {
        return state_.edit_buffer;
    }

    bool IsBypassed() const { return bypass_; }
    uint8_t GetCurrentMode() const { return current_mode_; }

    // Set when the both-FS gesture has been held continuously past
    // FS_BOOT_HOLD_MS. Cleared on read. Main loop polls this and calls
    // System::ResetToBootloader() so the audio side controls the reset
    // timing (after a Tick rather than inside the gesture handler).
    bool ShouldEnterBootloader() {
        bool req = bootloader_request_;
        bootloader_request_ = false;
        return req;
    }

private:
    Hothouse* hw_ = nullptr;
    Led* led1_ = nullptr;
    Led* led2_ = nullptr;
    daisy::PersistentStorage<StorageData>* storage_ = nullptr;

    // Global state (RAM working copy)
    GlobalState state_ = {};
    uint8_t current_mode_ = 0;
    bool bypass_ = false;

    PedalState pedal_state_ = PedalState::NORMAL;
    uint8_t save_target_ = 1;

    // Footswitch state
    bool fs1_pressed_last_ = false;
    bool fs2_pressed_last_ = false;
    uint32_t fs1_down_time_ = 0;
    uint32_t fs2_down_time_ = 0;
    bool fs1_long_fired_ = false;
    bool fs2_long_fired_ = false;
    // Both-FS combo gesture. Set when both FS are detected pressed. While
    // active, individual FS1/FS2 short and long presses are suppressed.
    // On release, hold duration determines: short = bank cycle.
    // Held past FS_BOOT_HOLD_MS arms the bootloader request, polled and
    // executed by the main loop via ShouldEnterBootloader().
    uint32_t both_down_time_ = 0;
    bool bootloader_request_ = false;
    bool boot_armed_ = false;  // true once the hold threshold is crossed
    bool needs_init_snapshot_ = false;  // defer first SnapshotHardware to first Tick (ADC warm-up)

    // LED 1: preset pattern
    int8_t led1_pattern_ = -1;  // -1 = off (manual), 0–7 = preset index
    uint8_t led1_step_ = 0;
    uint32_t led1_timer_ = 0;

    // LED 2: state indicator
    Led2Mode led2_mode_ = Led2Mode::SOLID;
    bool led2_phase_ = true;  // current on/off state in blink
    uint32_t led2_timer_ = 0;
    uint32_t led2_confirm_remaining_ = 0;

    // Bank-switch burst (both LEDs together). Takes over both LEDs for the
    // burst duration, then yields back via UpdateLed1Pattern / UpdateLed2Mode.
    // Sized for worst-case Roman pattern up to VIII: V (~75 flicker steps) +
    // 3×I (~50 each) + gaps + trailing ≈ 230. 256 leaves headroom.
    static constexpr int MAX_BANK_BURST_STEPS = 256;
    BlinkStep bank_burst_steps_[MAX_BANK_BURST_STEPS];
    uint16_t bank_burst_total_ = 0;
    uint16_t bank_burst_step_ = 0;
    uint32_t bank_burst_timer_ = 0;
    bool bank_burst_active_ = false;

    // Hardware tracking for dirty detection (detect movement, not difference from stored)
    float last_hw_knobs_[NUM_KNOBS];
    // Per-knob "live" flag. False after preset load / snapshot: edit buffer
    // holds the loaded value, ignoring hw position until the user moves the
    // knob past KNOB_DIRTY_THRESHOLD (knob takeover). True after takeover:
    // edit buffer tracks hw at full ADC resolution every tick.
    bool knob_live_[NUM_KNOBS] = {false};
    uint8_t last_hw_sw1_ = 0;
    uint8_t last_hw_sw2_ = 0;
    uint8_t last_hw_sw3_ = 0;

    // Debounced auto-save. Any state change resets idle_since_change_ms_ to 0
    // and sets save_pending_. When idle reaches AUTOSAVE_DEBOUNCE_MS with no
    // further changes, the save fires. If nothing ever changes, no save.
    static constexpr uint32_t AUTOSAVE_DEBOUNCE_MS = 2000;
    bool save_pending_ = false;
    uint32_t idle_since_change_ms_ = 0;

    // Tick duration for this cycle
    uint32_t tick_ms_ = 10;

    // ── Helpers ─────────────────────────────────────────────────

    // v2 → v3 migration. Each old mode m's 8 slots become bank m's 8 slots,
    // with each slot's new mode field set to m (= SW3 position the preset
    // was saved under). active_bank starts at the old last_mode so the
    // pedal comes back exactly where it left off.
    static void MigrateV2ToV3(const legacy_v2::StorageData& old, StorageData& out) {
        out = MakeDefaults();
        uint8_t last_mode = (old.last_mode < NUM_MODES) ? old.last_mode : 0;

        // Carry each mode's preset bank into the corresponding new bank.
        // Modes A/B/C map to banks 1/2/3 (indices 0/1/2). Banks beyond
        // NUM_MODES remain at factory defaults from MakeDefaults().
        for (int m = 0; m < NUM_MODES && m < MAX_BANKS; m++) {
            for (int p = 0; p < NUM_PRESETS; p++) {
                const legacy_v2::ModePresetData& src = old.modes[m].presets[p];
                ModePresetData& dst = out.state.banks[m].presets[p];
                for (int k = 0; k < NUM_KNOBS; k++) dst.knobs[k] = src.knobs[k];
                dst.sw1 = src.sw1;
                dst.sw2 = src.sw2;
                dst.mode = static_cast<uint8_t>(m);
                out.state.banks[m].preset_saved[p] = old.modes[m].preset_saved[p];
            }
        }

        // Edit buffer, active preset and dirty come from the last active mode.
        const legacy_v2::ModePresetData& src_eb = old.modes[last_mode].edit_buffer;
        for (int k = 0; k < NUM_KNOBS; k++)
            out.state.edit_buffer.knobs[k] = src_eb.knobs[k];
        out.state.edit_buffer.sw1 = src_eb.sw1;
        out.state.edit_buffer.sw2 = src_eb.sw2;
        out.state.edit_buffer.mode = last_mode;
        out.state.active_bank = last_mode;
        out.state.active_preset = old.modes[last_mode].active_preset;
        out.state.dirty = old.modes[last_mode].dirty;
    }

    static StorageData MakeDefaults() {
        StorageData d = {};
        d.version = STORAGE_VERSION;
        // Default edit buffer
        for (int k = 0; k < NUM_KNOBS; k++) d.state.edit_buffer.knobs[k] = 0.5f;
        d.state.edit_buffer.sw1 = 0;
        d.state.edit_buffer.sw2 = 0;
        d.state.edit_buffer.mode = 0;
        d.state.active_bank = 0;
        d.state.active_preset = 0;
        d.state.dirty = false;
        // Empty preset slots in every bank
        for (int b = 0; b < MAX_BANKS; b++) {
            for (int p = 0; p < NUM_PRESETS; p++) {
                for (int k = 0; k < NUM_KNOBS; k++)
                    d.state.banks[b].presets[p].knobs[k] = 0.5f;
                d.state.banks[b].presets[p].sw1 = 0;
                d.state.banks[b].presets[p].sw2 = 0;
                d.state.banks[b].presets[p].mode = 0;
                d.state.banks[b].preset_saved[p] = false;
            }
        }
        return d;
    }

    void ReadHardwareIntoEditBuffer() {
        for (int i = 0; i < NUM_KNOBS; i++) {
            state_.edit_buffer.knobs[i] = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
        }
        state_.edit_buffer.sw1 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_1);
        state_.edit_buffer.sw2 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_2);
        // mode field left to caller — typically current_mode_ / SW3
    }

    uint8_t ReadSwitchPosition(Hothouse::Toggleswitch sw) {
        switch (hw_->GetToggleswitchPosition(sw)) {
            case Hothouse::TOGGLESWITCH_UP:     return 0;
            case Hothouse::TOGGLESWITCH_MIDDLE: return 1;
            case Hothouse::TOGGLESWITCH_DOWN:   return 2;
            default: return 0;
        }
    }

    // ── Footswitch processing ───────────────────────────────────

    void ProcessFootswitches() {
        bool fs1_now = hw_->switches[Hothouse::FOOTSWITCH_1].Pressed();
        bool fs2_now = hw_->switches[Hothouse::FOOTSWITCH_2].Pressed();
        uint32_t now = System::GetNow();

        // FS1 rising edge
        if (fs1_now && !fs1_pressed_last_) {
            fs1_down_time_ = now;
            fs1_long_fired_ = false;
        }
        // FS1 long press detection (fires on threshold, not release).
        // Suppressed while both-FS combo is active — the combo owns the gesture.
        if (fs1_now && !fs1_long_fired_ && fs1_down_time_ > 0 && both_down_time_ == 0) {
            if (now - fs1_down_time_ >= FS_LONG_PRESS_MS) {
                fs1_long_fired_ = true;
                OnFs1LongPress();
            }
        }

        // FS2 rising edge
        if (fs2_now && !fs2_pressed_last_) {
            fs2_down_time_ = now;
            fs2_long_fired_ = false;
        }
        // FS2 long press detection — also suppressed during combo.
        if (fs2_now && !fs2_long_fired_ && fs2_down_time_ > 0 && both_down_time_ == 0) {
            if (now - fs2_down_time_ >= FS_LONG_PRESS_MS) {
                fs2_long_fired_ = true;
                OnFs2LongPress();
            }
        }

        // Combo detection: both pressed → arm. Mark individual presses
        // as "long-fired" so the falling-edge handlers below don't fire
        // their short-press actions.
        if (fs1_now && fs2_now && both_down_time_ == 0) {
            both_down_time_ = now;
            fs1_long_fired_ = true;
            fs2_long_fired_ = true;
            boot_armed_ = false;
            OnBothDown();
        }

        // Bootloader: both held past threshold → arm request. Main loop
        // polls ShouldEnterBootloader() and resets there.
        if (fs1_now && fs2_now && both_down_time_ != 0 && !boot_armed_) {
            if (now - both_down_time_ >= FS_BOOT_HOLD_MS) {
                boot_armed_ = true;
                bootloader_request_ = true;
            }
        }

        // FS1 falling edge → short press (suppressed if combo active or fired)
        if (!fs1_now && fs1_pressed_last_) {
            if (both_down_time_ != 0) {
                OnComboReleased(now - both_down_time_);
                both_down_time_ = 0;
            } else if (!fs1_long_fired_ && fs1_down_time_ > 0) {
                OnFs1ShortPress();
            }
            fs1_down_time_ = 0;
        }

        // FS2 falling edge → short press (same gating)
        if (!fs2_now && fs2_pressed_last_) {
            if (both_down_time_ != 0) {
                OnComboReleased(now - both_down_time_);
                both_down_time_ = 0;
            } else if (!fs2_long_fired_ && fs2_down_time_ > 0) {
                OnFs2ShortPress();
            }
            fs2_down_time_ = 0;
        }

        fs1_pressed_last_ = fs1_now;
        fs2_pressed_last_ = fs2_now;
    }

    // ── Both-FS combo actions ──────────────────────────────────

    void OnBothDown() {
        // Hook for future use (e.g. start a visual countdown). Hothouse
        // handles the 2 s bootloader detection externally; nothing else to
        // do here yet.
    }

    void OnComboReleased(uint32_t held_ms) {
        // Short tap (< long-press threshold) → bank cycle. Held longer is
        // the deadband / bootloader region; Hothouse already grabbed it at
        // 2 s if applicable, so we do nothing in code.
        if (held_ms < FS_LONG_PRESS_MS) {
            CycleBank();
        }
    }

    // ── Bank cycling ───────────────────────────────────────────

    void CycleBank() {
        state_.active_bank = (state_.active_bank + 1) % NUM_BANKS;

        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Save target slot number unchanged; bank context shifts so
            // the next FS2 long-press writes into the new bank's slot.
            // LEDs unchanged underneath the burst.
        } else if (state_.active_preset > 0) {
            // Load same slot in the new bank — saved → loads + clears dirty,
            // empty → manual-like (hardware read).
            LoadPreset(state_.active_preset);
            state_.dirty = false;
        }
        // Manual (active_preset == 0): edit buffer preserved.

        // Burst takes over both LEDs; UpdateLed1Pattern / UpdateLed2Mode
        // will be called when the burst finishes (see TickLeds).
        StartBankBurst(state_.active_bank + 1);
        MarkStateChanged();
    }

    // ── Bank burst LED routine ─────────────────────────────────

    void StartBankBurst(uint8_t bank_num) {
        if (bank_num < 1 || bank_num > NUM_PRESETS) return;  // PRESET_PATTERNS holds I..VIII

        // Count the number of pulses (I/V symbols) in the Roman pattern.
        // Total burst = LED_BANK_TOTAL_MS, split into N equal pulses with
        // fixed gaps between them: N*pulse + (N-1)*gap = TOTAL.
        const PresetPattern& roman = PRESET_PATTERNS[bank_num - 1];
        uint32_t n_pulses = 0;
        for (uint8_t i = 0; i < roman.num_steps; i++) {
            if (roman.steps[i].led_on) n_pulses++;
        }
        if (n_pulses == 0) return;
        uint32_t total_gap = (n_pulses > 1) ? (n_pulses - 1) * LED_BANK_GAP_MS : 0;
        uint32_t pulse_ms = (LED_BANK_TOTAL_MS > total_gap)
                            ? (LED_BANK_TOTAL_MS - total_gap) / n_pulses
                            : LED_BANK_FLICKER_MS;

        int n = 0;
        uint32_t pulses_emitted = 0;
        for (uint8_t i = 0; i < roman.num_steps && n < MAX_BANK_BURST_STEPS; i++) {
            const BlinkStep& s = roman.steps[i];
            if (s.led_on) {
                // Fill this pulse with on/off flicker chunks.
                uint32_t remaining = pulse_ms;
                bool on = true;
                while (remaining > 0 && n < MAX_BANK_BURST_STEPS) {
                    uint32_t chunk = (remaining < LED_BANK_FLICKER_MS)
                                     ? remaining : LED_BANK_FLICKER_MS;
                    bank_burst_steps_[n++] = {static_cast<uint16_t>(chunk), on};
                    remaining -= chunk;
                    on = !on;
                }
                pulses_emitted++;
                // Insert gap if more pulses follow.
                if (pulses_emitted < n_pulses && n < MAX_BANK_BURST_STEPS) {
                    bank_burst_steps_[n++] = {static_cast<uint16_t>(LED_BANK_GAP_MS), false};
                }
            }
            // Roman element-gap and repeat-gap steps are ignored; spacing is
            // handled by the explicit inter-pulse gap above.
        }
        // Trailing hold before LEDs return to normal.
        if (n < MAX_BANK_BURST_STEPS) {
            bank_burst_steps_[n++] = {static_cast<uint16_t>(LED_BANK_HOLD_MS), false};
        }
        bank_burst_total_ = static_cast<uint16_t>(n);
        bank_burst_step_ = 0;
        bank_burst_timer_ = bank_burst_steps_[0].duration_ms;
        bool on = bank_burst_steps_[0].led_on;
        led1_->Set(on ? 1.f : 0.f);
        led2_->Set(on ? 1.f : 0.f);
        led1_->Update();
        led2_->Update();
        bank_burst_active_ = true;
    }

    // ── FS1 actions ─────────────────────────────────────────────

    void OnFs1ShortPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Cycle save target: 1→2→…→8→1
            save_target_ = (save_target_ % NUM_PRESETS) + 1;
            UpdateLed1Pattern();  // show target
            return;
        }

        if (state_.dirty && state_.active_preset > 0 && IsPresetSaved(state_.active_preset)) {
            // Reload current saved preset (revert)
            LoadPreset(state_.active_preset);
            state_.dirty = false;
            UpdateLed2Mode();
            return;
        }

        // Cycle: Manual→1→2→…→8→Manual
        state_.active_preset = (state_.active_preset + 1) % (NUM_PRESETS + 1);
        state_.dirty = false;

        if (state_.active_preset == 0) {
            // Manual mode: read hardware (mode tracks SW3)
            current_mode_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
            state_.edit_buffer.mode = current_mode_;
            ReadHardwareIntoEditBuffer();
            SnapshotHardware();
        } else {
            LoadPreset(state_.active_preset);
        }

        UpdateLed1Pattern();
        UpdateLed2Mode();
        MarkStateChanged();
    }

    void OnFs1LongPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) return;

        // Jump to manual — read hardware (knobs + switches incl. SW3)
        state_.active_preset = 0;
        state_.dirty = false;
        current_mode_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
        state_.edit_buffer.mode = current_mode_;
        ReadHardwareIntoEditBuffer();
        SnapshotHardware();

        UpdateLed1Pattern();
        UpdateLed2Mode();
        MarkStateChanged();
    }

    // ── FS2 actions ─────────────────────────────────────────────

    void OnFs2ShortPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Cancel save
            pedal_state_ = PedalState::NORMAL;
            SnapshotHardware();
            UpdateLed1Pattern();
            UpdateLed2Mode();
            return;
        }

        // Toggle bypass
        bypass_ = !bypass_;
        UpdateLed2Mode();
    }

    void OnFs2LongPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Confirm save — write edit buffer to target slot in active bank
            Bank& bank = state_.banks[state_.active_bank];
            bank.presets[save_target_ - 1] = state_.edit_buffer;
            bank.preset_saved[save_target_ - 1] = true;

            // Now on this preset, clean
            state_.active_preset = save_target_;
            state_.dirty = false;

            // Fresh knob baseline so dirty doesn't trigger immediately
            SnapshotHardware();

            // Save to flash immediately (save-confirm IS the commit point)
            // and clear the debounce so we don't write again right after.
            SaveToFlash();
            save_pending_ = false;
            idle_since_change_ms_ = 0;

            // Show confirm burst, then return to normal
            pedal_state_ = PedalState::SAVE_CONFIRM;
            SetLed2(Led2Mode::SAVE_CONFIRM);
            UpdateLed1Pattern();
            return;
        }

        // Enter save mode
        pedal_state_ = PedalState::SAVE_MODE;
        save_target_ = (state_.active_preset > 0) ? state_.active_preset : 1;

        SetLed2(Led2Mode::SAVE_MODE);
        // LED 1 shows save target
        StartLed1Pattern(save_target_);
    }

    // ── Preset loading ──────────────────────────────────────────

    bool IsPresetSaved(uint8_t preset_num) const {
        if (preset_num < 1 || preset_num > NUM_PRESETS) return false;
        return state_.banks[state_.active_bank].preset_saved[preset_num - 1];
    }

    void LoadPreset(uint8_t preset_num) {
        if (preset_num < 1 || preset_num > NUM_PRESETS) return;
        if (IsPresetSaved(preset_num)) {
            // Saved slot: load stored values (including mode). current_mode_
            // follows the slot's stored mode → audio dispatch swaps with the
            // preset, not with SW3.
            state_.edit_buffer =
                state_.banks[state_.active_bank].presets[preset_num - 1];
            current_mode_ = state_.edit_buffer.mode;
            if (current_mode_ >= NUM_MODES) current_mode_ = 0;
        } else {
            // Empty slot: act like manual — read hardware (mode from SW3)
            current_mode_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
            state_.edit_buffer.mode = current_mode_;
            ReadHardwareIntoEditBuffer();
        }
        SnapshotHardware();
    }

    void SnapshotHardware() {
        for (int i = 0; i < NUM_KNOBS; i++) {
            last_hw_knobs_[i] = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
            // Freeze knobs — edit buffer ignores hw until next movement
            // past threshold. Preserves loaded preset values; in manual
            // mode the edit buffer already matches hw, so no visible change.
            knob_live_[i] = false;
        }
        last_hw_sw1_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_1);
        last_hw_sw2_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_2);
        last_hw_sw3_ = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
    }

    // ── Knob / switch processing (main loop) ───────────────────

    void ProcessKnobsAndSwitches() {
        if (pedal_state_ == PedalState::SAVE_MODE ||
            pedal_state_ == PedalState::SAVE_CONFIRM) return;

        ModePresetData& eb = state_.edit_buffer;
        bool any_changed = false;

        for (int i = 0; i < NUM_KNOBS; i++) {
            float hw_val = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));

            if (knob_live_[i]) {
                // Already taken over — pass every ADC sample through so
                // smooth knob motion reaches audio at full resolution.
                eb.knobs[i] = hw_val;
                last_hw_knobs_[i] = hw_val;
            } else {
                // Frozen since last snapshot (preset load, mode switch,
                // bypass etc.). Hold edit buffer at its loaded value until
                // the user moves the knob past the threshold — then flip
                // live and start tracking hw.
                float delta = hw_val - last_hw_knobs_[i];
                if (delta < 0) delta = -delta;
                if (delta > KNOB_DIRTY_THRESHOLD) {
                    knob_live_[i]      = true;
                    eb.knobs[i]        = hw_val;
                    last_hw_knobs_[i]  = hw_val;
                    any_changed        = true;
                }
            }
        }

        // Switches: detect movement from snapshot, not difference from stored
        uint8_t sw1 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_1);
        uint8_t sw2 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_2);
        if (sw1 != last_hw_sw1_) {
            eb.sw1 = sw1;
            last_hw_sw1_ = sw1;
            any_changed = true;
        }
        if (sw2 != last_hw_sw2_) {
            eb.sw2 = sw2;
            last_hw_sw2_ = sw2;
            any_changed = true;
        }

        // Mark dirty only for saved presets — empty slots act like manual
        if (any_changed && state_.active_preset > 0 && !state_.dirty &&
            IsPresetSaved(state_.active_preset)) {
            state_.dirty = true;
            UpdateLed2Mode();
        }
        if (any_changed) MarkStateChanged();
    }

    // ── Mode switching (SW3) ────────────────────────────────────
    // SW3 is uniform with SW1/SW2: in manual it just drives the edit buffer;
    // on a saved preset it additionally marks the preset dirty. Ignored
    // entirely during save / save-confirm (matches the SW1/SW2 freeze in
    // ProcessKnobsAndSwitches — save target reflects the buffer at
    // save-mode entry, not later twiddling).

    void ProcessModeSwitchHardware() {
        if (pedal_state_ == PedalState::SAVE_MODE ||
            pedal_state_ == PedalState::SAVE_CONFIRM) return;

        // Detect physical SW3 movement (against snapshot), not mismatch with
        // current_mode_. A preset whose stored mode differs from the physical
        // SW3 position is fine — only an actual switch flip should mark dirty.
        uint8_t sw3 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
        if (sw3 == last_hw_sw3_) return;
        last_hw_sw3_ = sw3;

        current_mode_ = sw3;
        state_.edit_buffer.mode = sw3;

        if (state_.active_preset > 0 && !state_.dirty &&
            IsPresetSaved(state_.active_preset)) {
            state_.dirty = true;
        }

        UpdateLed1Pattern();
        UpdateLed2Mode();
        MarkStateChanged();
    }

    // ── Flash persistence ───────────────────────────────────────

    // Arm the debounced auto-save. Call from every code path that mutates
    // persisted state (knobs, switches, navigation, mode change, dirty).
    void MarkStateChanged() {
        save_pending_ = true;
        idle_since_change_ms_ = 0;
    }

    void SaveToFlash() {
        StorageData& data = storage_->GetSettings();
        data.version = STORAGE_VERSION;
        data.state = state_;
        storage_->Save();
    }

    // ── LED 1: preset pattern engine ────────────────────────────

    void UpdateLed1Pattern() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            StartLed1Pattern(save_target_);
        } else {
            StartLed1Pattern(state_.active_preset);
        }
    }

    void StartLed1Pattern(uint8_t preset_num) {
        if (preset_num == 0) {
            led1_pattern_ = -1;
            led1_->Set(0.f);
            led1_->Update();
            return;
        }
        led1_pattern_ = preset_num - 1;
        led1_step_ = 0;
        led1_timer_ = PRESET_PATTERNS[led1_pattern_].steps[0].duration_ms;
        bool on = PRESET_PATTERNS[led1_pattern_].steps[0].led_on;
        led1_->Set(on ? 1.f : 0.f);
        led1_->Update();
    }

    // ── LED 2: state mode ───────────────────────────────────────

    void UpdateLed2Mode() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            SetLed2(Led2Mode::SAVE_MODE);
        } else if (bypass_) {
            SetLed2(Led2Mode::OFF);
        } else if (state_.dirty && state_.active_preset > 0) {
            SetLed2(Led2Mode::DIRTY);
        } else {
            SetLed2(Led2Mode::SOLID);
        }
    }

    void SetLed2(Led2Mode mode) {
        led2_mode_ = mode;
        led2_timer_ = 0;
        led2_phase_ = true;

        switch (mode) {
            case Led2Mode::SOLID:
                led2_->Set(1.f);
                break;
            case Led2Mode::OFF:
                led2_->Set(0.f);
                break;
            case Led2Mode::DIRTY:
            case Led2Mode::SAVE_MODE:
                led2_->Set(1.f);
                break;
            case Led2Mode::SAVE_CONFIRM:
                led2_confirm_remaining_ = LED_SAVE_CONFIRM_DUR_MS;
                led2_->Set(1.f);
                break;
        }
        led2_->Update();
    }

    // ── LED tick (called every main loop iteration) ─────────────

    void TickLeds(uint32_t elapsed_ms) {
        // Bank burst owns both LEDs for its duration. Other LED state
        // machines are quiescent until the burst finishes.
        if (bank_burst_active_) {
            if (bank_burst_timer_ <= elapsed_ms) {
                bank_burst_step_++;
                if (bank_burst_step_ >= bank_burst_total_) {
                    bank_burst_active_ = false;
                    // Yield LEDs back to their owners.
                    UpdateLed1Pattern();
                    UpdateLed2Mode();
                } else {
                    bank_burst_timer_ = bank_burst_steps_[bank_burst_step_].duration_ms;
                    bool on = bank_burst_steps_[bank_burst_step_].led_on;
                    led1_->Set(on ? 1.f : 0.f);
                    led2_->Set(on ? 1.f : 0.f);
                }
            } else {
                bank_burst_timer_ -= elapsed_ms;
            }
            led1_->Update();
            led2_->Update();
            return;
        }

        // LED 1: pattern
        if (led1_pattern_ >= 0) {
            if (led1_timer_ <= elapsed_ms) {
                const PresetPattern& pat = PRESET_PATTERNS[led1_pattern_];
                led1_step_ = (led1_step_ + 1) % pat.num_steps;
                led1_timer_ = pat.steps[led1_step_].duration_ms;
                led1_->Set(pat.steps[led1_step_].led_on ? 1.f : 0.f);
            } else {
                led1_timer_ -= elapsed_ms;
            }
        }
        led1_->Update();

        // LED 2: blinking modes
        switch (led2_mode_) {
            case Led2Mode::SOLID:
            case Led2Mode::OFF:
                // No tick needed
                break;

            case Led2Mode::DIRTY:
                led2_timer_ += elapsed_ms;
                if (led2_phase_ && led2_timer_ >= LED_DIRTY_ON_MS) {
                    led2_phase_ = false;
                    led2_timer_ = 0;
                    led2_->Set(0.f);
                } else if (!led2_phase_ && led2_timer_ >= LED_DIRTY_OFF_MS) {
                    led2_phase_ = true;
                    led2_timer_ = 0;
                    led2_->Set(1.f);
                }
                break;

            case Led2Mode::SAVE_MODE:
                led2_timer_ += elapsed_ms;
                if (led2_phase_ && led2_timer_ >= LED_SAVE_MODE_ON_MS) {
                    led2_phase_ = false;
                    led2_timer_ = 0;
                    led2_->Set(0.f);
                } else if (!led2_phase_ && led2_timer_ >= LED_SAVE_MODE_OFF_MS) {
                    led2_phase_ = true;
                    led2_timer_ = 0;
                    led2_->Set(1.f);
                }
                break;

            case Led2Mode::SAVE_CONFIRM:
                led2_timer_ += elapsed_ms;
                if (led2_phase_ && led2_timer_ >= LED_SAVE_CONFIRM_ON_MS) {
                    led2_phase_ = false;
                    led2_timer_ = 0;
                    led2_->Set(0.f);
                } else if (!led2_phase_ && led2_timer_ >= LED_SAVE_CONFIRM_OFF_MS) {
                    led2_phase_ = true;
                    led2_timer_ = 0;
                    led2_->Set(1.f);
                }
                // Auto-return to normal after burst duration
                led2_confirm_remaining_ =
                    (led2_confirm_remaining_ > elapsed_ms)
                    ? led2_confirm_remaining_ - elapsed_ms : 0;
                if (led2_confirm_remaining_ == 0) {
                    pedal_state_ = PedalState::NORMAL;
                    SnapshotHardware();
                    UpdateLed1Pattern();
                    UpdateLed2Mode();
                }
                break;
        }
        led2_->Update();
    }
};
