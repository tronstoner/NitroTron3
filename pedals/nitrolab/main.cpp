// NitroLab — test-bench pedal shell.
//
// Scaffolded from the ChronoTron3 shell (pedals/chronotron3/) for trying out
// modules in isolation. The shell owns only: SW3 mode selection, the K6
// dry/wet mix, and the reserved both-footswitch bootloader gesture. Each
// active module owns everything else on the surface — including both
// footswitches. Build: `make PEDAL=nitrolab`.
//
// SW3 UP     = armitage   (polyphonic feedback-FM voice on a private copy of
//                          the 80-band POG engine — see modules/armitage.h;
//                          unrelated to ChronoTron3's archived armitage)
// SW3 MIDDLE = empty      (pass-through placeholder)
// SW3 DOWN   = empty      (pass-through placeholder)

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"

#include "constants.h"          // pedals/nitrolab/     (-Ipedals/nitrolab)
#include "control_surface.h"    // src/core/io          (-Isrc/core/io)
#include "knob_map.h"           // src/core/util        (-Isrc/core/util)
#include "module.h"             // pedals/nitrolab/

#include "modules/armitage.h"
#include "modules/empty.h"

#include <math.h>

using namespace daisy;
using clevelandmusicco::Hothouse;

// ---------------------------------------------------------------------------
// Hardware + control surface + modules
// ---------------------------------------------------------------------------
Hothouse       hw;
ControlSurface cs;
Led            led1, led2;   // Hothouse LED_1 / LED_2 (single-colour)

Armitage armitage;
Empty    empty_b;
Empty    empty_c;
Module*  modules[NL_MODE_COUNT] = { &armitage, &empty_b, &empty_c };

// Active mode index. Written in the main loop (SW3), read in the audio ISR.
volatile int g_active = NL_MODE_ARMITAGE;

static float wet_buf[NL_BLOCK_SIZE];
static float pre_buf[NL_BLOCK_SIZE];   // input after the module's pre-effect (dry + send)
static float mix_smoothed = 0.f;   // smoothed K6 target (0 = dry, 1 = wet)
static float dry_bypass = 0.f;     // smoothed bypass dry-lift (0 = K6 mix, 1 = clean untouched)

// ---------------------------------------------------------------------------
// Audio callback — dispatch to the active module, then K6 dry/wet mix
// ---------------------------------------------------------------------------
// DIAG builds: audio-callback CPU load (peak per heartbeat).
static CpuLoadMeter g_cpu;
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  if (NL_DIAG) g_cpu.OnBlockStart();
  // FPU flush-to-zero in the audio IRQ context: denormals on the M7 hit a slow
  // software path that scales with the number of decaying filters — flushing
  // them removes that penalty. Setting it in main() alone does not reliably
  // apply here.
  __set_FPSCR(__get_FPSCR() | (1UL << 24));
  hw.ProcessAllControls();

  // Pre-effect ("a pedal in front"): processes the input for BOTH the module
  // and the dry path below. Default modules leave it untouched.
  for (size_t i = 0; i < size; i++) pre_buf[i] = in[0][i];
  modules[g_active]->PreProcess(pre_buf, size);
  modules[g_active]->Process(pre_buf, wet_buf, size);

  // Modules that own their output have already produced the final signal —
  // pass it straight through.
  if (modules[g_active]->OwnsOutput()) {
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = wet_buf[i];
    if (NL_DIAG) g_cpu.OnBlockEnd();
    return;
  }

  // K6 = mix. Equal-power crossfade, one-pole smoothed to kill zipper noise.
  const float mix_target = MixCurve(RemapKnob(cs.Knob(5)));  // K6 → index 5
  const float byp_target = modules[g_active]->Bypassed() ? 1.f : 0.f;
  for (size_t i = 0; i < size; i++) {
    mix_smoothed += (mix_target - mix_smoothed) * NL_MIX_SMOOTH;
    dry_bypass    += (byp_target - dry_bypass)  * NL_MIX_SMOOTH;
    const float wg = sqrtf(mix_smoothed);
    float dg = sqrtf(1.f - mix_smoothed);
    dg += (1.f - dg) * dry_bypass;                 // ramp dry -> unity in bypass
    out[0][i] = out[1][i] = pre_buf[i] * dg + wet_buf[i] * wg;
  }
  if (NL_DIAG) g_cpu.OnBlockEnd();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  // CPU clock: 480 MHz (libDaisy's "boost") on silicon revision V, which has
  // the VOS0 core voltage it needs; any other revision (the early rev Y) stays
  // at the 400 MHz default. Checked at boot, so an unexpected chip can never
  // fail to start — the pedal is sealed and the DFU gesture is the only way in.
  // (+20 % CPU; the SDRAM and audio clocks do not change: bit-identical audio.)
  const uint32_t revid = HAL_GetREVID();
  const bool boost = NL_CPU_BOOST && (revid == REV_ID_V);
  hw.Init(boost);
  hw.seed.StartLog(false);   // non-blocking USB serial log, available for module debugging
  // Enable FPU flush-to-zero: denormals on the Cortex-M7 hit a slow software path
  // that spikes the audio callback (starving the UI/main loop) once many filters
  // decay toward zero. Flushing denormals to zero removes that penalty.
  __set_FPSCR(__get_FPSCR() | (1UL << 24));   // FPSCR.FZ = 1
  hw.SetAudioBlockSize(NL_BLOCK_SIZE);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  const float sr = hw.AudioSampleRate();
  if (NL_DIAG) g_cpu.Init(sr, NL_BLOCK_SIZE);

  cs.Init(hw);
  led1.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led2.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  for (int i = 0; i < NL_MODE_COUNT; i++) modules[i]->Init(sr);

  // SW3 (toggle index 2): 0=UP=armitage, 1=MIDDLE=empty, 2=DOWN=empty.
  int sel = cs.Switch(2);
  if (sel < 0 || sel >= NL_MODE_COUNT) sel = 0;
  g_active = sel;
  modules[g_active]->Activate();

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  uint32_t diag_hb = 0;

  while (true) {
    hw.DelayMs(10);
    cs.Tick(10);

    // SW3 mode selection (shell-owned, uniform across the bundle).
    int next = cs.Switch(2);
    if (next >= 0 && next < NL_MODE_COUNT && next != g_active) {
      modules[g_active]->Deactivate();
      g_active = next;
      modules[g_active]->Activate();
    }

    // Active module's control-rate hook (its footswitches, knobs, LEDs).
    modules[g_active]->Controls(cs, led1, led2);
    led1.Update();
    led2.Update();

    // DIAG build: a plain CPU-load heartbeat every 2 s (no module-specific
    // instrumentation — this is a test bench, not a tuned module needing a
    // fault log). `make PEDAL=nitrolab DIAG=1`.
    if (NL_DIAG) {
      const uint32_t now = System::GetNow();
      if (now - diag_hb >= 2000) {
        diag_hb = now;
        const unsigned cpu = (unsigned)(g_cpu.GetMaxCpuLoad() * 1000.f + 0.5f);
        g_cpu.Reset();
        hw.seed.PrintLine("NL HB t=%u cpu=%u mode=%d", (unsigned)now, cpu, g_active);
      }
    }

    // Reserved gesture: both footswitches held → Daisy bootloader (DFU).
    // The pedal is sealed; this is the only entry path.
    if (cs.BothHeld(NL_BOOTLOADER_HOLD_MS)) {
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
