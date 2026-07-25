#pragma once
//
// core/io/control_surface.h
//
// Policy-free wrapper over the Hothouse hardware. Provides:
//   - live (pass-through) smoothed knob values 0..1
//   - live toggle-switch positions 0/1/2
//   - per-footswitch EVENTS (down / rising / falling / held_ms)
//   - BothHeld(ms) convenience for the reserved bootloader gesture
//
// This is the reusable input layer for any pedal in the family. It reads the
// hardware and detects footswitch edges/holds; it holds NO policy (what a
// footswitch DOES, presets, LEDs, mode selection all live in a pedal/module).
//
// Requires daisy.h + hothouse.h included before this file (hothouse.h has no
// include guard, so we cannot include it here).
//
// Timing contract:
//   * Knob()/Switch() read live on every call (no per-Tick caching). The audio
//     callback refreshes the Hothouse controls concurrently (~1 ms); callers
//     may read several times per Tick and expect the current value.
//   * Footswitch Pressed() is read ONCE per Tick(); edges/held_ms derive from
//     that single read. held_ms / combo timing use System::GetNow() (wall
//     clock), not the accumulated tick_ms.

#include <cstdint>

using clevelandmusicco::Hothouse;
using daisy::System;

struct FootswitchEvent {
    bool     down    = false;  // pressed this Tick
    bool     rising  = false;  // just went down this Tick
    bool     falling = false;  // just went up this Tick
    uint32_t held_ms = 0;      // 0 when up; (now - down_time) while held
};

class ControlSurface {
public:
    static constexpr int kNumKnobs = 6;
    static constexpr int kNumFoot  = 2;

    void Init(Hothouse& hw) {
        hw_ = &hw;
        for (int f = 0; f < kNumFoot; f++) {
            fs_[f]           = FootswitchEvent{};
            fs_down_time_[f] = 0;
            fs_last_[f]      = false;
        }
        both_down_time_ = 0;
        both_now_       = 0;
    }

    // Poll footswitches + recompute edges/hold. Call ONCE at the top of the
    // owner's Tick, before any policy runs. tick_ms is unused for FS timing
    // (kept for API symmetry / future helpers).
    void Tick(uint32_t /*tick_ms*/) {
        uint32_t now = System::GetNow();

        bool now_down[kNumFoot];
        now_down[0] = hw_->switches[Hothouse::FOOTSWITCH_1].Pressed();
        now_down[1] = hw_->switches[Hothouse::FOOTSWITCH_2].Pressed();

        for (int f = 0; f < kNumFoot; f++) {
            bool rising  =  now_down[f] && !fs_last_[f];
            bool falling = !now_down[f] &&  fs_last_[f];
            if (rising)  fs_down_time_[f] = now;
            if (falling) fs_down_time_[f] = 0;

            fs_[f].down    = now_down[f];
            fs_[f].rising  = rising;
            fs_[f].falling = falling;
            fs_[f].held_ms = now_down[f] ? (now - fs_down_time_[f]) : 0;

            fs_last_[f] = now_down[f];
        }

        // Combo timer for BothHeld(): latch when both first observed down,
        // clear when either is up.
        if (now_down[0] && now_down[1]) {
            if (both_down_time_ == 0) both_down_time_ = now;
        } else {
            both_down_time_ = 0;
        }
        both_now_ = now;
    }

    // LIVE pass-through — do not cache.
    float Knob(int i) const {
        return hw_->GetKnobValue(static_cast<Hothouse::Knob>(i));
    }

    // LIVE pass-through — 0/1/2 (UP/MIDDLE/DOWN).
    int Switch(int sw) const {
        switch (hw_->GetToggleswitchPosition(
                    static_cast<Hothouse::Toggleswitch>(sw))) {
            case Hothouse::TOGGLESWITCH_UP:     return 0;
            case Hothouse::TOGGLESWITCH_MIDDLE: return 1;
            case Hothouse::TOGGLESWITCH_DOWN:   return 2;
            default:                            return 0;
        }
    }

    const FootswitchEvent& Foot(int fs) const { return fs_[fs]; }

    // Both currently down AND combo held >= ms. Level query; the one-shot
    // "fire once" latch stays in policy.
    bool BothHeld(uint32_t ms) const {
        return fs_[0].down && fs_[1].down && both_down_time_ != 0 &&
               (both_now_ - both_down_time_) >= ms;
    }

private:
    Hothouse* hw_ = nullptr;

    FootswitchEvent fs_[kNumFoot];
    uint32_t fs_down_time_[kNumFoot] = {0, 0};
    bool     fs_last_[kNumFoot]      = {false, false};

    uint32_t both_down_time_ = 0;
    uint32_t both_now_       = 0;
};
