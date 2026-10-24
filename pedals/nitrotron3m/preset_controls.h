#pragma once
//
// NitroTron3m — the control source the modules read: the PRESET SYSTEM's edit
// buffer, not the live hardware.
//
// The Module interface (module.h) hands every module a `const ControlSurface&`.
// ChronoTron3 / NitroLab compile src/core/io/control_surface.h, which reads the
// knobs and switches live from the Hothouse. This pedal does NOT include that
// file; this header defines a class of the same name and the same public API
// (Knob / Switch / Foot / BothHeld), backed by PresetSystem::GetEditBuffer():
//
//   Knob(i)    = edit_buffer.knobs[i]   raw 0..1 (modules apply RemapKnob)
//                — so knob takeover, preset recall and manual WYSIWYG all
//                reach a module exactly as they reach the shipping modes.
//   Switch(0)  = edit_buffer.sw1, Switch(1) = edit_buffer.sw2,
//   Switch(2)  = edit_buffer.mode (the active slot, = SW3 in manual).
//   Foot(n)    = always idle: both footswitches belong to the preset system
//                (presets / banks / save / bypass / bootloader gesture), so a
//                module's own footswitch behaviour (e.g. armitage's FS1 wet
//                mute) never fires here.
//   BothHeld() = always false (the bootloader gesture is the preset system's).
//
// Reads are live (no caching), the same contract as the core class. One class
// per translation unit, so there is no clash with the core header (which this
// pedal never includes). Modules need no change and no #ifdef.
//
// Requires preset_system.h (PresetSystem, ModePresetData) included before.

#include <cstdint>

struct FootswitchEvent {
  bool     down    = false;
  bool     rising  = false;
  bool     falling = false;
  uint32_t held_ms = 0;
};

class ControlSurface {
 public:
  static constexpr int kNumKnobs = 6;
  static constexpr int kNumFoot  = 2;

  void Init(const PresetSystem& ps) { ps_ = &ps; }

  float Knob(int i) const { return ps_->GetEditBuffer().knobs[i]; }

  int Switch(int sw) const {
    const ModePresetData& eb = ps_->GetEditBuffer();
    switch (sw) {
      case 0:  return eb.sw1;
      case 1:  return eb.sw2;
      case 2:  return eb.mode;
      default: return 0;
    }
  }

  const FootswitchEvent& Foot(int /*fs*/) const { return idle_; }

  bool BothHeld(uint32_t /*ms*/) const { return false; }

 private:
  const PresetSystem* ps_ = nullptr;
  FootswitchEvent     idle_;
};
