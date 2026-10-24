// NitroTron3m — PROTOTYPE: NitroTron3 rebuilt on the Module system.
//
// Same pedal as pedals/nitrotron3 (which stays untouched and ships): three
// modes, the NitroTron3 preset system (presets, banks, knob takeover, save,
// bypass, bootloader gesture — pedals/nitrotron3/preset_system.h, included
// unchanged, same flash layout / version / offset, so presets carry over in both
// directions), but each mode is a Module:
//
//   A  bordun   modules/bordun.h   NitroTron3 Mode A, carved verbatim
//   B  sprawl   modules/sprawl.h   NitroTron3 Mode B, carved verbatim
//      — or —   armitage           pedals/nitrolab/modules/armitage.h (as is),
//                                  build flag NT3M_SLOT_B=armitage
//      — or —   explorer           pedals/nitrolab/modules/explorer.h (as is),
//                                  build flag NT3M_SLOT_B=explorer
//   C  schism   modules/schism.h   NitroTron3 Mode C, carved verbatim
//
// Build: `make PEDAL=nitrotron3m [NT3M_SLOT_B=sprawl|armitage]
//         [INSTRUMENT=bass|guitar] [DIAG=1]`.
//
// The shell around the modules:
//   * PresetSystem owns both footswitches, both LEDs, flash, bypass, the mode
//     (SW3 in manual, the slot's mode on a recalled preset) and the both-FS
//     bootloader request — exactly as in pedals/nitrotron3/main.cpp.
//   * Modules read the preset system's EDIT BUFFER through preset_controls.h
//     (a ControlSurface stand-in): knob takeover / recall reach them as they
//     reach the shipping modes. Their footswitch events are idle and the LEDs
//     they are handed are sinks that are never shown.
//   * The env follower and pitch tracker are shared (NT3Shared, Attach() before
//     Init()); the YIN search runs in the main loop as in the shipping pedal.
//   * The three NitroTron3 modules own their output (their own K6 law); a module
//     that does not (armitage, explorer) gets the NitroLab shell's K6 equal-power mix,
//     reading K6 from the edit buffer.
//   * CPU: 480 MHz on silicon rev V + FPU flush-to-zero (the NitroLab shell's
//     setup). Neither is in the shipping pedal (400 MHz, no FTZ).

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"

#include "constants.h"                     // pedals/nitrotron3m (-> ../nitrotron3/constants.h)
#include "knob_map.h"                      // src/core/util
#include "../nitrotron3/preset_system.h"   // shipping preset system, unchanged
#include "preset_controls.h"               // ControlSurface stand-in (edit buffer)
#include "module.h"
#include "nt3_shared.h"

#include "modules/bordun.h"
#if defined(NT3M_SLOT_B_ARMITAGE) && defined(NT3M_SLOT_B_EXPLORER)
#error "NT3M_SLOT_B: choose one of sprawl / armitage / explorer"
#endif
#if defined(NT3M_SLOT_B_ARMITAGE)
#include "../nitrolab/modules/armitage.h"  // NitroLab's module, included as is
using SlotB = Armitage;
#define NT3M_SLOT_B_NITROLAB 1
#elif defined(NT3M_SLOT_B_EXPLORER)
#include "../nitrolab/modules/explorer.h"  // NitroLab's module, included as is
using SlotB = Explorer;
#define NT3M_SLOT_B_NITROLAB 1
#else
#include "modules/sprawl.h"
using SlotB = Sprawl;
#endif
#include "modules/schism.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;

Hothouse hw;

// ---------------------------------------------------------------------------
// Modules + shared state
// ---------------------------------------------------------------------------
NT3Shared shared;            // env follower + pitch tracker (A / B / C)
Bordun    bordun;
SlotB     slot_b;
Schism    schism;
Module*   modules[NT3M_MODE_COUNT] = { &bordun, &slot_b, &schism };

// Active mode index. Written in the main loop (after PresetSystem::Tick), read
// in the audio ISR.
volatile int g_active = NT3M_MODE_A;

// ---------------------------------------------------------------------------
// Preset system + flash storage (as pedals/nitrotron3/main.cpp:257-258)
// ---------------------------------------------------------------------------
PresetSystem preset;
daisy::PersistentStorage<StorageData> flash_storage(hw.seed.qspi);
ControlSurface cs;           // the modules' view of the edit buffer

Led led_bypass;
Led led_status;
// Sinks for the modules' LED writes: never initialised to a pin, never
// Update()d — the preset system owns the real LEDs.
Led led_sink1, led_sink2;

static float wet_buf[NT3M_BLOCK_SIZE];
static float pre_buf[NT3M_BLOCK_SIZE];
static float mix_smoothed = 0.f;   // K6 (only for modules that don't own their output)
static float dry_bypass   = 0.f;

// DIAG builds: audio-callback CPU load (peak per heartbeat).
static daisy::CpuLoadMeter g_cpu;

// ---------------------------------------------------------------------------
// Audio callback
// ---------------------------------------------------------------------------
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  if (NT3M_DIAG) g_cpu.OnBlockStart();
  // FPU flush-to-zero in the audio IRQ context (as the NitroLab shell).
  __set_FPSCR(__get_FPSCR() | (1UL << 24));
  hw.ProcessAllControls();

  // Bypass: as pedals/nitrotron3/main.cpp:1596-1600 (no module runs).
  if (preset.IsBypassed()) {
    for (size_t i = 0; i < size; i++)
      out[0][i] = out[1][i] = in[0][i];
    if (NT3M_DIAG) g_cpu.OnBlockEnd();
    return;
  }

  Module* m = modules[g_active];
  for (size_t i = 0; i < size; i++) pre_buf[i] = in[0][i];
  m->PreProcess(pre_buf, size);
  m->Process(pre_buf, wet_buf, size);

  if (m->OwnsOutput()) {
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = wet_buf[i];
    if (NT3M_DIAG) g_cpu.OnBlockEnd();
    return;
  }

  // K6 = mix (NitroLab shell law), K6 read from the edit buffer.
  const float mix_target = MixCurve(RemapKnob(cs.Knob(5)));
  const float byp_target = m->Bypassed() ? 1.f : 0.f;
  for (size_t i = 0; i < size; i++) {
    mix_smoothed += (mix_target - mix_smoothed) * NT3M_MIX_SMOOTH;
    dry_bypass   += (byp_target - dry_bypass)   * NT3M_MIX_SMOOTH;
    const float wg = sqrtf(mix_smoothed);
    float dg = sqrtf(1.f - mix_smoothed);
    dg += (1.f - dg) * dry_bypass;
    out[0][i] = out[1][i] = pre_buf[i] * dg + wet_buf[i] * wg;
  }
  if (NT3M_DIAG) g_cpu.OnBlockEnd();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  // 480 MHz on silicon revision V only (as pedals/nitrolab/main.cpp).
  const uint32_t revid = HAL_GetREVID();
  const bool boost = NT3M_CPU_BOOST && (revid == REV_ID_V);
  hw.Init(boost);
  if (NT3M_DIAG) hw.seed.StartLog(false);   // non-blocking USB serial log
  __set_FPSCR(__get_FPSCR() | (1UL << 24));   // FPSCR.FZ = 1
  hw.SetAudioBlockSize(NT3M_BLOCK_SIZE);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  float sr = hw.AudioSampleRate();
  if (NT3M_DIAG) g_cpu.Init(sr, NT3M_BLOCK_SIZE);

  // Shared state (pedals/nitrotron3/main.cpp:1629-1630, 1641).
  shared.env.Init(sr);
  shared.env.SetCutoff(ENV_LP_CUTOFF_HZ);
  shared.tracker.Init(sr);

  bordun.Attach(&shared);
#ifndef NT3M_SLOT_B_NITROLAB
  slot_b.Attach(&shared);   // NitroLab modules use neither env nor tracker
#endif
  schism.Attach(&shared);
  for (int i = 0; i < NT3M_MODE_COUNT; i++) modules[i]->Init(sr);

  led_status.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  // Init preset system (owns LEDs, footswitches, flash)
  preset.Init(hw, led_status, led_bypass, flash_storage);
  cs.Init(preset);

  int sel = preset.GetCurrentMode();
  if (sel < 0 || sel >= NT3M_MODE_COUNT) sel = NT3M_MODE_A;
  modules[sel]->Activate();
  modules[sel]->Controls(cs, led_sink1, led_sink2);
  g_active = sel;

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  uint32_t diag_hb = 0;

  while (true) {
    hw.DelayMs(10);

    // Preset system: footswitches, knobs, LEDs, mode switching, flash
    preset.Tick(10);

    // Mode (SW3, or the recalled preset's slot). The entering module reads the
    // edit buffer BEFORE it is published to the audio ISR, so its first block
    // already runs on the current values.
    int next = preset.GetCurrentMode();
    if (next < 0 || next >= NT3M_MODE_COUNT) next = NT3M_MODE_A;
    if (next != g_active) {
      modules[g_active]->Deactivate();
      modules[next]->Activate();
      modules[next]->Controls(cs, led_sink1, led_sink2);
      g_active = next;
    } else {
      modules[g_active]->Controls(cs, led_sink1, led_sink2);
    }

    // Pitch tracker: run YIN in main loop (fed by bordun / sprawl / schism).
    shared.tracker.Update();

    // DIAG builds: CPU heartbeat (peak audio-callback load per window).
    if (NT3M_DIAG) {
      const uint32_t now = System::GetNow();
      if (now - diag_hb >= NT3M_DIAG_HB_MS) {
        diag_hb = now;
        const unsigned cpu = (unsigned)(g_cpu.GetMaxCpuLoad() * 1000.f + 0.5f);
        g_cpu.Reset();
        hw.seed.PrintLine("NT3M HB t=%u cpu=%u mode=%d byp=%d", (unsigned)now, cpu,
                          (int)g_active, preset.IsBypassed() ? 1 : 0);
      }
    }

    // Bootloader: both footswitches held 2 s (as pedals/nitrotron3/main.cpp:1688-1713).
    if (preset.ShouldEnterBootloader()) {
      hw.StopAdc();
      hw.StopAudio();
      for (int i = 0; i < 8; i++) {
        led_status.Set(1.f);
        led_bypass.Set(0.f);
        led_status.Update();
        led_bypass.Update();
        daisy::System::Delay(75);
        led_status.Set(0.f);
        led_bypass.Set(1.f);
        led_status.Update();
        led_bypass.Update();
        daisy::System::Delay(75);
      }
      daisy::System::ResetToBootloader(
          daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
    }
  }

  return 0;
}
