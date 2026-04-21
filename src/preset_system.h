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

struct ModePresetData {
    float knobs[NUM_KNOBS];
    uint8_t sw1;  // Switch 1 position (0/1/2)
    uint8_t sw2;  // Switch 2 position (0/1/2)
};

struct ModeState {
    ModePresetData edit_buffer;
    ModePresetData presets[NUM_PRESETS];
    bool preset_saved[NUM_PRESETS];  // true if slot has been saved to
    uint8_t active_preset;  // 0 = manual, 1–8 = preset
    bool dirty;
};

struct StorageData {
    uint32_t version;
    ModeState modes[NUM_MODES];
    uint8_t last_mode;

    bool operator!=(const StorageData& o) const {
        // Compare raw bytes — all fields are POD
        return memcmp(this, &o, sizeof(StorageData)) != 0;
    }
};

static constexpr uint32_t STORAGE_VERSION = 2;

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
        if (data.version != STORAGE_VERSION) {
            // Version mismatch or corrupt — reset to factory
            storage_->RestoreDefaults();
        }

        // Restore state
        StorageData& saved = storage_->GetSettings();
        current_mode_ = saved.last_mode;
        if (current_mode_ >= NUM_MODES) current_mode_ = 0;

        // Copy saved mode states into RAM
        for (int m = 0; m < NUM_MODES; m++) {
            mode_state_[m] = saved.modes[m];
        }

        // Check if factory boot (all presets zeroed = never saved)
        bool factory = (storage_->GetState() ==
                        daisy::PersistentStorage<StorageData>::State::FACTORY);
        if (factory) {
            // Read current hardware into all mode edit buffers
            ReadHardwareIntoEditBuffer();
            for (int m = 1; m < NUM_MODES; m++) {
                mode_state_[m].edit_buffer = mode_state_[0].edit_buffer;
            }
        }

        active_preset_ = mode_state_[current_mode_].active_preset;
        dirty_         = mode_state_[current_mode_].dirty;
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

        // Snapshot hardware knobs for dirty detection
        for (int i = 0; i < NUM_KNOBS; i++) {
            last_hw_knobs_[i] = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
        }
    }

    // Call from main loop every tick (10 ms)
    void Tick(uint32_t tick_ms) {
        tick_ms_ = tick_ms;

        ProcessFootswitches();
        ProcessKnobsAndSwitches();
        ProcessModeSwitchHardware();
        TickLeds(tick_ms);

        // Periodic auto-save (every ~30 s)
        save_timer_ += tick_ms;
        if (save_timer_ >= 30000) {
            save_timer_ = 0;
            SaveToFlash();
        }
    }

    // Read current edit buffer for audio callback
    const ModePresetData& GetEditBuffer() const {
        return mode_state_[current_mode_].edit_buffer;
    }

    bool IsBypassed() const { return bypass_; }
    uint8_t GetCurrentMode() const { return current_mode_; }

private:
    Hothouse* hw_ = nullptr;
    Led* led1_ = nullptr;
    Led* led2_ = nullptr;
    daisy::PersistentStorage<StorageData>* storage_ = nullptr;

    // Mode state (RAM working copy)
    ModeState mode_state_[NUM_MODES];
    uint8_t current_mode_ = 0;
    uint8_t active_preset_ = 0;  // mirrors mode_state_[current_mode_].active_preset
    bool dirty_ = false;
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

    // LED 1: preset pattern
    int8_t led1_pattern_ = -1;  // -1 = off (manual), 0–7 = preset index
    uint8_t led1_step_ = 0;
    uint32_t led1_timer_ = 0;

    // LED 2: state indicator
    Led2Mode led2_mode_ = Led2Mode::SOLID;
    bool led2_phase_ = true;  // current on/off state in blink
    uint32_t led2_timer_ = 0;
    uint32_t led2_confirm_remaining_ = 0;

    // Knob tracking for dirty detection
    float last_hw_knobs_[NUM_KNOBS];

    // Auto-save timer
    uint32_t save_timer_ = 0;

    // Tick duration for this cycle
    uint32_t tick_ms_ = 10;

    // ── Helpers ─────────────────────────────────────────────────

    static StorageData MakeDefaults() {
        StorageData d = {};
        d.version = STORAGE_VERSION;
        d.last_mode = 0;
        for (int m = 0; m < NUM_MODES; m++) {
            for (int k = 0; k < NUM_KNOBS; k++)
                d.modes[m].edit_buffer.knobs[k] = 0.5f;
            d.modes[m].edit_buffer.sw1 = 0;
            d.modes[m].edit_buffer.sw2 = 0;
            d.modes[m].active_preset = 0;
            d.modes[m].dirty = false;
            for (int p = 0; p < NUM_PRESETS; p++) {
                for (int k = 0; k < NUM_KNOBS; k++)
                    d.modes[m].presets[p].knobs[k] = 0.5f;
                d.modes[m].presets[p].sw1 = 0;
                d.modes[m].presets[p].sw2 = 0;
                d.modes[m].preset_saved[p] = false;
            }
        }
        return d;
    }

    void ReadHardwareIntoEditBuffer() {
        ModePresetData& eb = mode_state_[current_mode_].edit_buffer;
        for (int i = 0; i < NUM_KNOBS; i++) {
            eb.knobs[i] = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
        }
        eb.sw1 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_1);
        eb.sw2 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_2);
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
        // FS1 long press detection (fires on threshold, not release)
        if (fs1_now && !fs1_long_fired_ && fs1_down_time_ > 0) {
            if (now - fs1_down_time_ >= FS_LONG_PRESS_MS) {
                fs1_long_fired_ = true;
                OnFs1LongPress();
            }
        }
        // FS1 falling edge → short press
        if (!fs1_now && fs1_pressed_last_) {
            if (!fs1_long_fired_ && fs1_down_time_ > 0) {
                OnFs1ShortPress();
            }
            fs1_down_time_ = 0;
        }

        // FS2 rising edge
        if (fs2_now && !fs2_pressed_last_) {
            fs2_down_time_ = now;
            fs2_long_fired_ = false;
        }
        // FS2 long press detection
        if (fs2_now && !fs2_long_fired_ && fs2_down_time_ > 0) {
            if (now - fs2_down_time_ >= FS_LONG_PRESS_MS) {
                fs2_long_fired_ = true;
                OnFs2LongPress();
            }
        }
        // FS2 falling edge → short press
        if (!fs2_now && fs2_pressed_last_) {
            if (!fs2_long_fired_ && fs2_down_time_ > 0) {
                OnFs2ShortPress();
            }
            fs2_down_time_ = 0;
        }

        fs1_pressed_last_ = fs1_now;
        fs2_pressed_last_ = fs2_now;
    }

    // ── FS1 actions ─────────────────────────────────────────────

    void OnFs1ShortPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Cycle save target: 1→2→…→8→1
            save_target_ = (save_target_ % NUM_PRESETS) + 1;
            UpdateLed1Pattern();  // show target
            return;
        }

        if (dirty_ && active_preset_ > 0 && IsPresetSaved(active_preset_)) {
            // Reload current saved preset (revert)
            LoadPreset(active_preset_);
            dirty_ = false;
            mode_state_[current_mode_].dirty = false;
            UpdateLed2Mode();
            return;
        }

        // Cycle: Manual→1→2→…→8→Manual
        active_preset_ = (active_preset_ + 1) % (NUM_PRESETS + 1);
        mode_state_[current_mode_].active_preset = active_preset_;
        dirty_ = false;
        mode_state_[current_mode_].dirty = false;

        if (active_preset_ == 0) {
            // Manual mode: read hardware
            ReadHardwareIntoEditBuffer();
            SnapshotHwKnobs();
        } else {
            LoadPreset(active_preset_);
        }

        UpdateLed1Pattern();
        UpdateLed2Mode();
    }

    void OnFs1LongPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) return;

        // Jump to manual — read hardware knobs
        active_preset_ = 0;
        mode_state_[current_mode_].active_preset = 0;
        dirty_ = false;
        mode_state_[current_mode_].dirty = false;

        ReadHardwareIntoEditBuffer();
        SnapshotHwKnobs();

        UpdateLed1Pattern();
        UpdateLed2Mode();
    }

    // ── FS2 actions ─────────────────────────────────────────────

    void OnFs2ShortPress() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            // Cancel save
            pedal_state_ = PedalState::NORMAL;
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
            // Confirm save — write edit buffer to target slot
            mode_state_[current_mode_].presets[save_target_ - 1] =
                mode_state_[current_mode_].edit_buffer;
            mode_state_[current_mode_].preset_saved[save_target_ - 1] = true;

            // Now on this preset, clean
            active_preset_ = save_target_;
            mode_state_[current_mode_].active_preset = save_target_;
            dirty_ = false;
            mode_state_[current_mode_].dirty = false;

            // Save to flash
            SaveToFlash();

            // Show confirm burst, then return to normal
            pedal_state_ = PedalState::SAVE_CONFIRM;
            SetLed2(Led2Mode::SAVE_CONFIRM);
            UpdateLed1Pattern();
            return;
        }

        // Enter save mode
        pedal_state_ = PedalState::SAVE_MODE;
        save_target_ = (active_preset_ > 0) ? active_preset_ : 1;

        SetLed2(Led2Mode::SAVE_MODE);
        // LED 1 shows save target
        StartLed1Pattern(save_target_);
    }

    // ── Preset loading ──────────────────────────────────────────

    bool IsPresetSaved(uint8_t preset_num) const {
        if (preset_num < 1 || preset_num > NUM_PRESETS) return false;
        return mode_state_[current_mode_].preset_saved[preset_num - 1];
    }

    void LoadPreset(uint8_t preset_num) {
        if (preset_num < 1 || preset_num > NUM_PRESETS) return;
        if (IsPresetSaved(preset_num)) {
            // Saved slot: load stored values
            mode_state_[current_mode_].edit_buffer =
                mode_state_[current_mode_].presets[preset_num - 1];
        } else {
            // Empty slot: act like manual — read hardware
            ReadHardwareIntoEditBuffer();
        }
        SnapshotHwKnobs();
    }

    void SnapshotHwKnobs() {
        for (int i = 0; i < NUM_KNOBS; i++) {
            last_hw_knobs_[i] = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
        }
    }

    // ── Knob / switch processing (main loop) ───────────────────

    void ProcessKnobsAndSwitches() {
        if (pedal_state_ == PedalState::SAVE_MODE ||
            pedal_state_ == PedalState::SAVE_CONFIRM) return;

        ModePresetData& eb = mode_state_[current_mode_].edit_buffer;
        bool any_changed = false;

        for (int i = 0; i < NUM_KNOBS; i++) {
            float hw_val = hw_->GetKnobValue(
                static_cast<Hothouse::Knob>(i));
            float delta = hw_val - last_hw_knobs_[i];
            if (delta < 0) delta = -delta;

            if (delta > KNOB_DIRTY_THRESHOLD) {
                eb.knobs[i] = hw_val;
                last_hw_knobs_[i] = hw_val;
                any_changed = true;
            }
        }

        // Switches always override edit buffer immediately
        uint8_t sw1 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_1);
        uint8_t sw2 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_2);
        if (sw1 != eb.sw1) { eb.sw1 = sw1; any_changed = true; }
        if (sw2 != eb.sw2) { eb.sw2 = sw2; any_changed = true; }

        // Mark dirty only for saved presets — empty slots act like manual
        if (any_changed && active_preset_ > 0 && !dirty_ &&
            IsPresetSaved(active_preset_)) {
            dirty_ = true;
            mode_state_[current_mode_].dirty = true;
            UpdateLed2Mode();
        }
    }

    // ── Mode switching (SW3) ────────────────────────────────────

    void ProcessModeSwitchHardware() {
        uint8_t sw3 = ReadSwitchPosition(Hothouse::TOGGLESWITCH_3);
        if (sw3 == current_mode_) return;

        // Save departing mode
        mode_state_[current_mode_].active_preset = active_preset_;
        mode_state_[current_mode_].dirty = dirty_;

        // Cancel save mode on switch
        pedal_state_ = PedalState::NORMAL;

        // Switch to new mode
        current_mode_ = sw3;

        // Restore incoming mode
        active_preset_ = mode_state_[current_mode_].active_preset;
        dirty_         = mode_state_[current_mode_].dirty;

        // Snapshot knobs for dirty detection in new mode
        SnapshotHwKnobs();

        UpdateLed1Pattern();
        UpdateLed2Mode();

        // Save to flash (mode switch)
        SaveToFlash();
    }

    // ── Flash persistence ───────────────────────────────────────

    void SaveToFlash() {
        StorageData& data = storage_->GetSettings();
        data.version = STORAGE_VERSION;
        data.last_mode = current_mode_;
        for (int m = 0; m < NUM_MODES; m++) {
            data.modes[m] = mode_state_[m];
        }
        storage_->Save();
    }

    // ── LED 1: preset pattern engine ────────────────────────────

    void UpdateLed1Pattern() {
        if (pedal_state_ == PedalState::SAVE_MODE) {
            StartLed1Pattern(save_target_);
        } else {
            StartLed1Pattern(active_preset_);
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
        if (bypass_) {
            SetLed2(Led2Mode::OFF);
        } else if (dirty_ && active_preset_ > 0) {
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
                    UpdateLed1Pattern();
                    UpdateLed2Mode();
                }
                break;
        }
        led2_->Update();
    }
};
