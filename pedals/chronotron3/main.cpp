// ChronoTron3 — bundle shell.
//
// A curated bundle of three modules (vestige / mnemonic / armitage) on the shared
// NitroTron3 platform (src/core/). The shell owns only: SW3 mode selection
// (uniform across the family), the K6 dry/wet mix, and the reserved both-
// footswitch bootloader gesture. Each active module owns everything else on the
// surface — including both footswitches. Build: `make PEDAL=chronotron3`.

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"

#include "constants.h"          // pedals/chronotron3/  (-Ipedals/chronotron3)
#include "control_surface.h"    // src/core/io          (-Isrc/core/io)
#include "knob_map.h"           // src/core/util        (-Isrc/core/util)
#include "module.h"             // pedals/chronotron3/

#include "modules/vestige.h"
#include "modules/mnemonic.h"
#include "modules/armitage.h"

#include <math.h>

using namespace daisy;
using clevelandmusicco::Hothouse;

// ---------------------------------------------------------------------------
// Hardware + control surface + modules
// ---------------------------------------------------------------------------
Hothouse       hw;
ControlSurface cs;
Led            led1, led2;   // Hothouse LED_1 / LED_2 (single-colour)

Vestige  vestige;
Mnemonic mnemonic;
Armitage    armitage;
Module*  modules[CT3_MODE_COUNT] = { &vestige, &mnemonic, &armitage };

// Active mode index. Written in the main loop (SW3), read in the audio ISR.
volatile int g_active = CT3_MODE_VESTIGE;

static float wet_buf[CT3_BLOCK_SIZE];
static float mix_smoothed = 0.f;   // smoothed K6 target (0 = dry, 1 = wet)
static float dry_bypass = 0.f;     // smoothed bypass dry-lift (0 = K6 mix, 1 = clean untouched)

// ---------------------------------------------------------------------------
// Audio callback — dispatch to the active module, then K6 dry/wet mix
// ---------------------------------------------------------------------------
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  // FPU flush-to-zero in the audio IRQ context: denormals on the M7 hit a slow
  // software path that scales with the number of decaying filters (per-grain
  // biquads etc.) — flushing them removes that penalty. Setting it in main()
  // alone does not reliably apply here.
  __set_FPSCR(__get_FPSCR() | (1UL << 24));
  hw.ProcessAllControls();

  modules[g_active]->Process(in[0], wet_buf, size);

  // Modules that own their output (e.g. vestige: looper volume + dry routing on
  // K6/SW2) have already produced the final signal — pass it straight through.
  if (modules[g_active]->OwnsOutput()) {
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = wet_buf[i];
    return;
  }

  // K6 = mix. Equal-power crossfade, one-pole smoothed to kill zipper noise.
  const float mix_target = MixCurve(RemapKnob(cs.Knob(5)));  // K6 → index 5
  const float byp_target = modules[g_active]->Bypassed() ? 1.f : 0.f;
  for (size_t i = 0; i < size; i++) {
    mix_smoothed += (mix_target - mix_smoothed) * CT3_MIX_SMOOTH;
    dry_bypass    += (byp_target - dry_bypass)  * CT3_MIX_SMOOTH;
    const float wg = sqrtf(mix_smoothed);
    float dg = sqrtf(1.f - mix_smoothed);
    dg += (1.f - dg) * dry_bypass;                 // ramp dry -> unity in bypass
    out[0][i] = out[1][i] = in[0][i] * dg + wet_buf[i] * wg;
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  hw.Init();
  // Enable FPU flush-to-zero: denormals on the Cortex-M7 hit a slow software path
  // that spikes the audio callback (starving the UI/main loop) once many filters
  // decay toward zero — e.g. the per-grain band biquads in the multiband freeze.
  // Flushing denormals to zero removes that penalty. Helps every filter/DSP block.
  __set_FPSCR(__get_FPSCR() | (1UL << 24));   // FPSCR.FZ = 1
  hw.SetAudioBlockSize(CT3_BLOCK_SIZE);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  const float sr = hw.AudioSampleRate();

  cs.Init(hw);
  led1.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led2.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  for (int i = 0; i < CT3_MODE_COUNT; i++) modules[i]->Init(sr);

  // SW3 (toggle index 2): 0=UP=vestige, 1=MIDDLE=mnemonic, 2=DOWN=armitage.
  int sel = cs.Switch(2);
  if (sel < 0 || sel >= CT3_MODE_COUNT) sel = 0;
  g_active = sel;
  modules[g_active]->Activate();

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);
    cs.Tick(10);

    // SW3 mode selection (shell-owned, uniform across the bundle).
    int next = cs.Switch(2);
    if (next >= 0 && next < CT3_MODE_COUNT && next != g_active) {
      modules[g_active]->Deactivate();
      g_active = next;
      modules[g_active]->Activate();
    }

    // Active module's control-rate hook (its footswitches, knobs, LEDs).
    modules[g_active]->Controls(cs, led1, led2);
    led1.Update();
    led2.Update();

    // Reserved gesture: both footswitches held → Daisy bootloader (DFU).
    // The pedal is sealed; this is the only entry path.
    if (cs.BothHeld(CT3_BOOTLOADER_HOLD_MS)) {
      hw.StopAdc();
      hw.StopAudio();
      for (int i = 0; i < 8; i++) {
        led1.Set(1.f); led2.Set(0.f); led1.Update(); led2.Update();
        System::Delay(75);
        led1.Set(0.f); led2.Set(1.f); led1.Update(); led2.Update();
        System::Delay(75);
      }
      System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
    }
  }
  return 0;
}
