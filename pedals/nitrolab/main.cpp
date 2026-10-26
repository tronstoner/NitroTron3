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
//                          unrelated to ChronoTron3's archived armitage;
//                          SW1 = mode pair, K1 bipolar: CCW / CW model,
//                          K5 envelope follower on the K4 cutoff, K2 the same on the K3 FM index (UP))
// SW3 MIDDLE = explorer   (sound-exploration models on a copy of armitage's band
//                          engine — see modules/explorer.h; SW1 = family, K1 = model,
//                          K2 / K3 = sweeps, K4 = post filter (SW1 UP: FM index env follower),
//                          SW2 = octave up / played / down; K5 unused)
// SW3 DOWN   = wavemorph  (morphing wavetable voice on a copy of the band engine — see
//                          modules/wavemorph.h; K1 = base wave (10 stepped zones), K2 = K4 filter
//                          env follower, K3 = morph, K4 = post filter, SW2 = octave up / played / down; K5 unused)

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"

#include "constants.h"          // pedals/nitrolab/     (-Ipedals/nitrolab)
#include "control_surface.h"    // src/core/io          (-Isrc/core/io)
#include "knob_map.h"           // src/core/util        (-Isrc/core/util)
#include "module.h"             // pedals/nitrolab/

#include "modules/armitage.h"
#include "modules/explorer.h"
#include "modules/wavemorph.h"

#include <math.h>
#include <stdio.h>

// DIAG float print, 2 decimals (newlib-nano has no %f): libDaisy's FLT_FMT(2)
// = "%c%d.%02d" (sign slot, ' ' for positives - so the format has no space
// before it). D2V feeds it like FLT_VAR, but rounded to the nearest hundredth
// via integers (FLT_VAR truncates and relies on abs()); values are >= 0.
#define D2F FLT_FMT(2)
static inline int DiagHundredths(float x) { return (x > 0.f) ? (int)(x * 100.f + 0.5f) : 0; }
#define D2V(x) ' ', DiagHundredths(x) / 100, DiagHundredths(x) % 100
// Signed variant (armitage K4 post filter t in [-1, 1]): the sign slot carries '-'.
#define D2SV(x) ((x) < 0.f ? '-' : ' '), DiagHundredths(fabsf(x)) / 100, DiagHundredths(fabsf(x)) % 100

using namespace daisy;
using clevelandmusicco::Hothouse;

// ---------------------------------------------------------------------------
// Hardware + control surface + modules
// ---------------------------------------------------------------------------
Hothouse       hw;
ControlSurface cs;
Led            led1, led2;   // Hothouse LED_1 / LED_2 (single-colour)

Armitage armitage;
Explorer explorer;
WaveMorph wavemorph;
Module*  modules[NL_MODE_COUNT] = { &armitage, &explorer, &wavemorph };

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

  // SW3 (toggle index 2): 0=UP=armitage, 1=MIDDLE=explorer, 2=DOWN=wavemorph.
  int sel = cs.Switch(2);
  if (sel < 0 || sel >= NL_MODE_COUNT) sel = 0;
  g_active = sel;
  modules[g_active]->Activate();

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  uint32_t diag_hb = 0;
  int      diag_stage = 0;          // 1 / 2 / 3 = second half of an armitage / explorer / wavemorph setting line due
  float    diag_cpu_last = 0.f;     // CPU peak of the last full 2 s window
  Armitage::DiagSnap diag_snap{};
  Explorer::DiagSnap diag_xsnap{};
  WaveMorph::DiagSnap diag_wsnap{};

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

    // DIAG build (`make PEDAL=nitrolab DIAG=1`): a CPU-load heartbeat every 2 s
    // plus armitage's "note this setting" lines (FS1 tap = `#<n>`, settled
    // controls = `auto`). Main loop only, at most ONE print per 10 ms tick: the
    // logger's line buffer is 128 bytes, so a setting line goes out in two
    // halves on consecutive ticks (Print, then PrintLine = one terminal line).
    if (NL_DIAG) {
      const uint32_t now = System::GetNow();
      if (diag_stage == 1) {
        // second half of a setting line
        static const char* const oct_name[3] = { "up", "played", "down" };
        const Armitage::DiagSnap& d = diag_snap;
        float cpu_pk = g_cpu.GetMaxCpuLoad();
        if (diag_cpu_last > cpu_pk) cpu_pk = diag_cpu_last;
        hw.seed.PrintLine("beta" D2F " fm" D2F " filt t" D2F " ef5 kn" D2F " e" D2F " ef2 kn" D2F " e" D2F
                          " g" D2F " oct %s mute %d cpu %u%%",
                          D2V(d.beta), D2V(d.fm), D2SV(d.filt_t), D2V(d.ef5_kn), D2V(d.ef5_e),
                          D2V(d.ef2_kn), D2V(d.ef2_e), D2V(d.ef2_g),
                          oct_name[(d.oct >= 0 && d.oct < 3) ? d.oct : 1],
                          d.mute ? 1 : 0,
                          (unsigned)(cpu_pk * 100.f + 0.5f));
        diag_stage = 0;
      } else if (diag_stage == 2) {
        // explorer, second half: model (+ zone), sweep A / B (derived values), mute, cpu
        const Explorer::DiagSnap& d = diag_xsnap;
        float cpu_pk = g_cpu.GetMaxCpuLoad();
        if (diag_cpu_last > cpu_pk) cpu_pk = diag_cpu_last;
        const char* zn = Explorer::ZoneName(d.model, d.zone);
        // K4: SW1 UP = FM index env follower (t, k_norm, e, gain g), MID / DOWN = post filter t
        if (d.sw1 == 0)
          hw.seed.PrintLine("%s%s%s A" D2F " B" D2F " ef t" D2F " kn" D2F " e" D2F " g" D2F " mute %d cpu %u%%",
                            Explorer::ModelName(d.model), zn[0] ? "/" : "", zn,
                            D2SV(d.a), D2SV(d.b), D2SV(d.k4t), D2V(d.efkn), D2V(d.efe), D2V(d.envg),
                            d.mute ? 1 : 0,
                            (unsigned)(cpu_pk * 100.f + 0.5f));
        else
          hw.seed.PrintLine("%s%s%s A" D2F " B" D2F " filt t" D2F " mute %d cpu %u%%",
                            Explorer::ModelName(d.model), zn[0] ? "/" : "", zn,
                            D2SV(d.a), D2SV(d.b), D2SV(d.k4t), d.mute ? 1 : 0,
                            (unsigned)(cpu_pk * 100.f + 0.5f));
        diag_stage = 0;
      } else if (diag_stage == 3) {
        // wavemorph, second half: K2 filter env follower (k_norm, e), octave, mute, cpu
        static const char* const oct_name[3] = { "up", "played", "down" };
        const WaveMorph::DiagSnap& d = diag_wsnap;
        float cpu_pk = g_cpu.GetMaxCpuLoad();
        if (diag_cpu_last > cpu_pk) cpu_pk = diag_cpu_last;
        hw.seed.PrintLine("ef kn" D2F " e" D2F " oct %s mute %d cpu %u%%",
                          D2V(d.efkn), D2V(d.efe), oct_name[(d.sw2 >= 0 && d.sw2 < 3) ? d.sw2 : 1],
                          d.mute ? 1 : 0, (unsigned)(cpu_pk * 100.f + 0.5f));
        diag_stage = 0;
      } else if (g_active == NL_MODE_WAVEMORPH && wavemorph.DiagTake(diag_wsnap)) {
        // wavemorph, first half: number / auto, zone + the two waves being blended + fraction, knobs
        const WaveMorph::DiagSnap& d = diag_wsnap;
        char tag[12];
        if (d.n > 0) snprintf(tag, sizeof(tag), "#%d", d.n);
        else         snprintf(tag, sizeof(tag), "auto");
        char wv[40];
        const char* wa = WaveMorph::WaveName(d.zone, d.f0);
        const char* wb = WaveMorph::WaveName(d.zone, d.f0 + 1);
        if (wa[0]) snprintf(wv, sizeof(wv), " %s>%s+%d%%", wa, wb, (int)(d.ff * 100.f + 0.5f));
        else       snprintf(wv, sizeof(wv), " f%d>f%d+%d%%", d.f0 + 1, d.f0 + 2, (int)(d.ff * 100.f + 0.5f));
        hw.seed.Print("WM %s %s%s K1" D2F " K2" D2F " K3" D2F " K4" D2F " K5" D2F " | ",
                      tag, WaveMorph::ZoneName(d.zone), wv,
                      D2V(d.k[0]), D2V(d.k[1]), D2V(d.k[2]), D2V(d.k[3]), D2V(d.k[4]));
        diag_stage = 3;
      } else if (g_active == NL_MODE_EXPLORER && explorer.DiagTake(diag_xsnap)) {
        // explorer, first half: number / auto, knobs, switches
        static const char* const sw_name[3] = { "UP", "MID", "DOWN" };
        const Explorer::DiagSnap& d = diag_xsnap;
        char tag[12];
        if (d.n > 0) snprintf(tag, sizeof(tag), "#%d", d.n);
        else         snprintf(tag, sizeof(tag), "auto");
        static const char* const oct_name[3] = { "up", "played", "down" };
        hw.seed.Print("XP %s K1" D2F " K2" D2F " K3" D2F " K4" D2F " K5" D2F " SW1 %s oct %s | ",
                      tag, D2V(d.k[0]), D2V(d.k[1]), D2V(d.k[2]), D2V(d.k[3]), D2V(d.k[4]),
                      sw_name[(d.sw1 >= 0 && d.sw1 < 3) ? d.sw1 : 1],
                      oct_name[(d.sw2 >= 0 && d.sw2 < 3) ? d.sw2 : 1]);
        diag_stage = 2;
      } else if (g_active == NL_MODE_ARMITAGE && armitage.DiagTake(diag_snap)) {
        // first half: number / auto, knobs, switches
        static const char* const sw_name[3] = { "UP", "MID", "DOWN" };
        // model from (SW1 pair, sign of K1); h = |2 k1 - 1|. K5 / K2 env followers (k_norm, e; K2 gain g) in the second half.
        static const char* const mode_name[5] = { "SAW", "PULSE", "ADD", "FB", "UNSHAPED" };
        const Armitage::DiagSnap& d = diag_snap;
        char tag[12];
        if (d.n > 0) snprintf(tag, sizeof(tag), "#%d", d.n);
        else         snprintf(tag, sizeof(tag), "auto");
        hw.seed.Print("%s K1" D2F " K2" D2F " K3" D2F " K4" D2F " K5" D2F " SW1 %s %s h" D2F " SW2 %s | ",
                      tag, D2V(d.k[0]), D2V(d.k[1]), D2V(d.k[2]), D2V(d.k[3]), D2V(d.k[4]),
                      sw_name[(d.sw1 >= 0 && d.sw1 < 3) ? d.sw1 : 1],
                      mode_name[(d.mode >= 0 && d.mode < 5) ? d.mode : 4], D2V(d.h),
                      sw_name[(d.sw2 >= 0 && d.sw2 < 3) ? d.sw2 : 1]);
        diag_stage = 1;
      } else if (now - diag_hb >= 2000) {
        diag_hb = now;
        diag_cpu_last = g_cpu.GetMaxCpuLoad();
        const unsigned cpu = (unsigned)(diag_cpu_last * 1000.f + 0.5f);
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
