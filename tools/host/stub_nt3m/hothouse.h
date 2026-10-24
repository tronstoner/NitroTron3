#pragma once
// Host stub of the Hothouse proxy for tools/host/nt3m_ab.cpp. Knobs, toggles and
// footswitches are plain fields the harness writes; StartAudio() hands the audio
// callback to the harness and DelayMs() calls back into it (HostDelayMs), which
// renders that many 1 ms audio blocks — so the pedal's own main() loop drives
// the simulation in the same order as on the hardware.
#include "daisy.h"

using daisy::AudioHandle;
using daisy::Led;
using daisy::Pin;
using daisy::SaiHandle;

void HostDelayMs(size_t ms);                        // defined by the harness
extern daisy::AudioHandle::AudioCallback g_host_cb; // defined by the harness

namespace clevelandmusicco {

struct HostSwitch {
  bool down = false;
  bool Pressed() const { return down; }
};

struct HostSeed {
  daisy::QSPIHandle qspi;
  daisy::Pin GetPin(int) { return daisy::Pin{}; }
  void StartLog(bool = false) {}
  void PrintLine(const char*, ...) {}
  void Print(const char*, ...) {}
};

class Hothouse {
 public:
  enum Switches {
    SWITCH_1_UP, SWITCH_1_DOWN, SWITCH_2_UP, SWITCH_2_DOWN, SWITCH_3_UP,
    SWITCH_3_DOWN, FOOTSWITCH_1, FOOTSWITCH_2, SWITCH_LAST,
  };
  enum Knob { KNOB_1, KNOB_2, KNOB_3, KNOB_4, KNOB_5, KNOB_6, KNOB_LAST };
  enum Led { LED_1 = 22, LED_2 = 23, LED_LAST };
  enum ToggleswitchPosition {
    TOGGLESWITCH_UP, TOGGLESWITCH_MIDDLE, TOGGLESWITCH_DOWN, TOGGLESWITCH_UNKNOWN,
  };
  enum Toggleswitch { TOGGLESWITCH_1, TOGGLESWITCH_2, TOGGLESWITCH_3 };

  void Init(bool = false) {}
  void DelayMs(size_t del) { HostDelayMs(del); }
  void StartAudio(AudioHandle::AudioCallback cb) { g_host_cb = cb; }
  void StopAudio() {}
  void SetAudioSampleRate(SaiHandle::Config::SampleRate) {}
  float AudioSampleRate() { return 48000.f; }
  void SetAudioBlockSize(size_t) {}
  void StartAdc() {}
  void StopAdc() {}
  void ProcessAllControls() {}
  float GetKnobValue(Knob k) { return knob[k]; }
  ToggleswitchPosition GetToggleswitchPosition(Toggleswitch t) {
    return static_cast<ToggleswitchPosition>(tsw[t]);
  }

  HostSeed   seed;
  HostSwitch switches[SWITCH_LAST];
  float      knob[KNOB_LAST] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  int        tsw[3] = {0, 0, 0};
};

}  // namespace clevelandmusicco
