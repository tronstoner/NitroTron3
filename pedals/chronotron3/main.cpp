// ChronoTron3 — bundle shell.
//
// A curated bundle of three modules (vestige / mnemonic / sprawl) on the shared
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
#include "modules/sprawl.h"

#include <math.h>
#include <cstdio>    // snprintf (integer formats only — the sprawl diag lines)

using namespace daisy;
using clevelandmusicco::Hothouse;

// ---------------------------------------------------------------------------
// Sprawl diagnostics over USB serial (observation only). DIAG BUILDS ONLY:
// everything below is gated on CT3_DIAG (constants.h), a constexpr false in the
// shipping firmware, so the emitter block is dead-code eliminated and the
// linker drops these helpers. Build it with `make PEDAL=chronotron3 DIAG=1`.
// newlib-nano has no %f, so floats are fixed-point formatted here, and nan/inf
// are spelled out — that is the whole point of this log. The logger line buffer
// is 128 bytes, and the USB TX is non-blocking, so a snapshot is emitted ONE
// short line per control tick (10 ms) instead of a burst that would be dropped.
// ---------------------------------------------------------------------------
[[maybe_unused]] static const char* F3(float x) {
  static char pool[16][16]; static int pi = 0;
  char* b = pool[pi++ & 15];
  if (x != x) { snprintf(b, 16, "nan"); return b; }
  if (x > 3.4e38f || x < -3.4e38f) { snprintf(b, 16, x > 0.f ? "inf" : "-inf"); return b; }
  const bool neg = x < 0.f; const float a = neg ? -x : x;
  if (a >= 1.0e6f) { snprintf(b, 16, "%s%de6", neg ? "-" : "", (int)(a * 1e-6f)); return b; }
  int ip = (int)a; int fp = (int)((a - (float)ip) * 1000.f + 0.5f);
  if (fp >= 1000) { ip++; fp -= 1000; }
  snprintf(b, 16, "%s%d.%03d", neg ? "-" : "", ip, fp);
  return b;
}
[[maybe_unused]] static constexpr int kSprawlDiagLines = 19;

// ---------------------------------------------------------------------------
// Hardware + control surface + modules
// ---------------------------------------------------------------------------
Hothouse       hw;
ControlSurface cs;
Led            led1, led2;   // Hothouse LED_1 / LED_2 (single-colour)

Vestige  vestige;
Mnemonic mnemonic;
Sprawl   sprawl;
Module*  modules[CT3_MODE_COUNT] = { &vestige, &mnemonic, &sprawl };

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
  hw.seed.StartLog(false);   // non-blocking USB serial log, available for module debugging
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

  // SW3 (toggle index 2): 0=UP=vestige, 1=MIDDLE=mnemonic, 2=DOWN=sprawl.
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

    // Sprawl diagnostics: a fault picture (taken in the ISR at the offending
    // sample) has priority; otherwise a heartbeat every 2 s. One line per tick.
    if (CT3_DIAG && g_active == CT3_MODE_SPRAWL) {
      static SprawlDebug snap;
      static int  line = -1;                 // -1 = idle
      static uint32_t last_hb = 0;
      static int  silent_hbs = 0;
      static bool banner = false;
      const uint32_t now = System::GetNow();
      if (line < 0) {
        if (sprawl.DebugTakeFaultSnapshot(snap)) { line = 0; }
        else if (now - last_hb >= 2000) {
          sprawl.DebugFillLive(snap); last_hb = now; line = 0;
          // "wet silent while input present and feedback engaged" — the second
          // class of event reported by ear, which is NOT a non-finite fault.
          const bool silent = (snap.in_pk > 0.02f && snap.wet_pk < 0.0005f && snap.fb_amt > 0.05f);
          silent_hbs = silent ? silent_hbs + 1 : 0;
          if (silent_hbs >= 2) snap.stage = -1;     // marks the HB line
        }
      }
      if (line >= 0) {
        const SprawlDebug& d = snap;
        switch (line) {
        case 0:
          if (!banner) { hw.seed.PrintLine("SP DIAG online (DIAG=1 build)"); banner = true; }
          hw.seed.PrintLine("SP %s t=%u n=%u",
              d.stage == -1 ? "HB *** WET SILENT ***" : d.stage == 1 ? "FAULT @grain-sum" :
              d.stage == 2 ? "FAULT @wet-bus" : d.stage == 3 ? "FAULT @post-reverb" : "HB",
              (unsigned)d.t_ms, (unsigned)d.fault_count);
          break;
        case 1: hw.seed.PrintLine("ctl k=%s %s %s %s %s sw=%d%d tap=%s", F3(d.k1), F3(d.k2), F3(d.k3), F3(d.k4), F3(d.k5), d.sw1, d.sw2, F3(d.tap)); break;
        case 2: hw.seed.PrintLine("st  frz=%d byp=%d pan=%s snd=%s", d.frozen, d.bypassed, F3(d.panic), F3(d.send)); break;
        case 3: hw.seed.PrintLine("prm fb=%s rv=%s k2s=%s k3m=%s gl=%s cl=%d lv=%d tex=%d hm=%d", F3(d.fb_amt), F3(d.rev_amt), F3(d.k2_scale), F3(d.k3mag), F3(d.glitch), d.cloud, d.live, d.tex, d.harmony); break;
        case 4: hw.seed.PrintLine("rng maxr=%u base=%u glen=%u bint=%u wpos=%u", (unsigned)d.max_range, (unsigned)d.base_delay, (unsigned)d.grain_len, (unsigned)d.base_interval, (unsigned)d.wpos); break;
        case 5: hw.seed.PrintLine("grn act=%d tmr=%d nxt=%d hold=%d ratio=%s bad=%02x", d.active_voices, d.grain_timer, d.next_voice, d.hold_ctr, F3(d.cached_ratio), d.bad_voice_mask); break;
        case 6: case 7: case 8: case 9: {
          const int a = (line - 6) * 2, b = a + 1;
          hw.seed.PrintLine("v%d a%d d%u l%u r%d n%d p%s o=%s | v%d a%d d%u l%u r%d n%d p%s o=%s",
              a, d.v[a].active, (unsigned)d.v[a].delay, (unsigned)d.v[a].len, d.v[a].rev, d.v[a].loops, F3(d.v[a].rate), F3(d.v[a].last_out),
              b, d.v[b].active, (unsigned)d.v[b].delay, (unsigned)d.v[b].len, d.v[b].rev, d.v[b].loops, F3(d.v[b].rate), F3(d.v[b].last_out));
          break; }
        case 10: hw.seed.PrintLine("fbk duck=%s onp=%s hp=%s,%s", F3(d.duck_env), F3(d.onplay_env), F3(d.hp0), F3(d.hp1)); break;
        case 11: hw.seed.PrintLine("env pw=%s env=%s slow=%s", F3(d.prev_wet), F3(d.grain_env), F3(d.trans_slow)); break;
        case 12: hw.seed.PrintLine("deg ch=%d/%d mix=%s d=%s/%s warb=%s ng=%s sg=%s", d.deg.chain, d.deg.tgt_chain, F3(d.deg.mix), F3(d.deg.d), F3(d.deg.d_tgt), F3(d.warble_int), F3(d.deg.noise_gate), F3(d.deg.noise_sgate)); break;
        case 13: hw.seed.PrintLine("deg nz=%s env=%s fclk=%s fold=%s/%s", F3(d.deg.nz_det), F3(d.deg.env), F3(d.deg.f_clk), F3(d.deg.fold_blend), F3(d.deg.fold_drive)); break;
        case 14: hw.seed.PrintLine("bbd hl=%d hold=%s inz=%s,%s recz=%s,%s", d.deg.hold_len, F3(d.deg.bbd_hold), F3(d.deg.in_z1), F3(d.deg.in_z2), F3(d.deg.rec_z1), F3(d.deg.rec_z2)); break;
        case 15: hw.seed.PrintLine("bbd lossz=%s dc=%s,%s", F3(d.deg.loss_z), F3(d.deg.dc_x1), F3(d.deg.dc_y1)); break;
        case 16: hw.seed.PrintLine("tap sx1=%s lpz=%s hp=%s,%s hb=%s,%s", F3(d.deg.sat_x1), F3(d.deg.tape_lp_z), F3(d.deg.hp_x1), F3(d.deg.hp_y1), F3(d.deg.hb_z1), F3(d.deg.hb_z2)); break;
        case 17: hw.seed.PrintLine("tap drop=%s snag=%s", F3(d.deg.drop_g), F3(d.deg.snag)); break;
        case 18: hw.seed.PrintLine("met in=%s grn=%s tex=%s wet=%s dec=%s rml=%s", F3(d.in_pk), F3(d.grain_pk), F3(d.tex_pk), F3(d.wet_pk), F3(d.decim_hold), F3(d.ringmod_lp)); break;
        }
        if (++line >= kSprawlDiagLines) line = -1;
      }
    }

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
