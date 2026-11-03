#pragma once
//
// The NitroLab shell (pedals/nitrolab/main.cpp) for the host simulator: all
// three modules, SW3 selection, the K6 equal-power dry/wet mix, FS1 taps,
// Controls() at the main-loop rate. Shared by armsim (offline) and armlive
// (real time) so both run the identical arithmetic.
//
//   SW3 UP     = Armitage            (modules/armitage.h, constants = host tunables)
//   SW3 MIDDLE = Explorer(false)     (modules/explorer.h, constexpr constants)
//   SW3 DOWN   = Explorer(true)      = FM LAB
//
// As on the pedal: every module is Init()ed once at start (the explorer tables
// are built by the first Init and shared, as in the firmware); only the active
// module is Activate()d and runs Process() / Controls(); an SW3 change at a
// control tick = Deactivate(old), Activate(new), then the new module's
// Controls() (main.cpp's main-loop order). Inactive modules keep their state.
//
// Include order (see armitage_host.h): host_tunables.h first; explorer.h is
// included BEFORE armitage_host.h so it (and armitage_bands.h, which both
// share) is parsed with the normal `constexpr` - only armitage.h itself is
// read with `constexpr` as `const`. EXP_FAST_BSS / DSY_SDRAM_BSS / DTCM_MEM_SECTION
// expand to nothing on the host (explorer.h's own host fallbacks).

#include "host_tunables.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "daisy.h"              // tools/host/stub
#include "hothouse.h"           // tools/host/stub
#include "control_surface.h"    // tools/host/stub
#include "constants.h"          // pedals/nitrolab
#include "knob_map.h"           // src/core/util
#include "module.h"             // pedals/nitrolab
#include "modules/explorer.h"   // pedals/nitrolab (unmodified, normal constexpr)
#include "armitage_host.h"      // armitage.h with tunables

namespace nlshell {

static const float kSR = 48000.f;                 // the shell's SAI rate
static const int   kControlEveryBlocks = 10;      // main loop: hw.DelayMs(10) -> Controls()
static const size_t kB = NL_BLOCK_SIZE;           // 48

enum { kArmitage = 0, kExplorer = 1, kLab = 2 };  // = SW3 position = NL_MODE_* index
inline const char* ModuleName(int m) { return m == kExplorer ? "explorer" : m == kLab ? "lab" : "armitage"; }
inline const char* ModuleTitle(int m) {
  return m == kExplorer ? "Explorer (SW3 MID)" : m == kLab ? "FM LAB (SW3 DOWN)" : "Armitage (SW3 UP)";
}

// Remapped knob value 0..1 -> the raw ADC value RemapKnob() maps back to it.
inline float RawKnob(float k) { return KNOB_MIN + (KNOB_MAX - KNOB_MIN) * k; }

// One DIAG snapshot, tagged with the module that wrote it.
struct DiagMsg {
  int mod;                   // kArmitage / kExplorer / kLab
  Armitage::DiagSnap a;      // valid when mod == kArmitage
  Explorer::DiagSnap x;      // valid otherwise
};

struct Shell {
  Armitage arm;
  Explorer exp{false};       // SW3 MIDDLE
  Explorer lab{true};        // SW3 DOWN = FM LAB
  Module* modules[NL_MODE_COUNT] = { &arm, &exp, &lab };
  int active = kArmitage;

  ControlSurface cs;         // knob[] raw (RawKnob), sw[0..2], fs[0]
  daisy::Led led1, led2;
  float mix_smoothed = 0.f, dry_bypass = 0.f;
  bool fs1_pending = false;

  Module* Mod() { return modules[active]; }

  // Power-up: cs must hold the start controls (cs.sw[2] = SW3). All modules
  // Init(), the SW3 one Activate()d. The K6 smoother starts AT its target (the
  // knobs have been in place since power-up; see README).
  void Start(bool fs1_at_start) {
    daisy::System::now_ms = 0;
    for (int i = 0; i < NL_MODE_COUNT; i++) modules[i]->Init(kSR);
    int sel = cs.Switch(2);
    if (sel < 0 || sel >= NL_MODE_COUNT) sel = 0;
    active = sel;
    Mod()->Activate();
    mix_smoothed = MixCurve(RemapKnob(cs.Knob(5)));
    dry_bypass = Mod()->Bypassed() ? 1.f : 0.f;
    fs1_pending = fs1_at_start;
  }

  // Re-Init of armitage only (after a tunable change; tunables are armitage's).
  void ReinitArmitage() {
    arm.Init(kSR);
    if (active == kArmitage) arm.Activate();
  }

  // The main loop's 10 ms tick (cs already holds the current controls): SW3
  // selection, then the active module's Controls() with one FS1 rising edge if
  // a tap is pending. Fresh DIAG snapshots of the active module go to `sink`
  // (callable with a const DiagMsg&) - main.cpp only takes them from the active one.
  template <class Sink>
  void ControlTick(Sink&& sink) {
    const int next = cs.Switch(2);
    if (next >= 0 && next < NL_MODE_COUNT && next != active) {
      Mod()->Deactivate();
      active = next;
      Mod()->Activate();
    }
    cs.fs[0].rising = fs1_pending;           // one tap edge
    cs.fs[0].down = fs1_pending;
    fs1_pending = false;
    Mod()->Controls(cs, led1, led2);
    cs.fs[0].rising = cs.fs[0].down = false;
    led1.Update();
    led2.Update();
    daisy::System::now_ms += 10;
    DiagMsg m;
    m.mod = active;
    if (active == kArmitage) { while (arm.DiagTake(m.a)) sink(m); }
    else {
      Explorer& e = (active == kExplorer) ? exp : lab;
      while (e.DiagTake(m.x)) sink(m);
    }
  }
  void ControlTick() { ControlTick([](const DiagMsg&) {}); }

  // The audio callback body for one 48-frame block: x -> y.
  void Process(const float* in, float* out) {
    float wet_buf[NL_BLOCK_SIZE];
    float pre_buf[NL_BLOCK_SIZE];
    Module* mod = Mod();
    for (size_t i = 0; i < kB; i++) pre_buf[i] = in[i];
    mod->PreProcess(pre_buf, kB);
    mod->Process(pre_buf, wet_buf, kB);
    if (mod->OwnsOutput()) {
      for (size_t i = 0; i < kB; i++) out[i] = wet_buf[i];
      return;
    }
    const float mix_target = MixCurve(RemapKnob(cs.Knob(5)));
    const float byp_target = mod->Bypassed() ? 1.f : 0.f;
    for (size_t i = 0; i < kB; i++) {
      mix_smoothed += (mix_target - mix_smoothed) * NL_MIX_SMOOTH;
      dry_bypass   += (byp_target - dry_bypass)  * NL_MIX_SMOOTH;
      const float wg = sqrtf(mix_smoothed);
      float dg = sqrtf(1.f - mix_smoothed);
      dg += (1.f - dg) * dry_bypass;
      out[i] = pre_buf[i] * dg + wet_buf[i] * wg;
    }
  }
};

// SW3 / module argument: up|mid|down or armitage|explorer|lab. -1 = invalid.
inline int ParseModule(const char* s) {
  if (!strcmp(s, "up") || !strcmp(s, "armitage") || !strcmp(s, "arm")) return kArmitage;
  if (!strcmp(s, "mid") || !strcmp(s, "middle") || !strcmp(s, "explorer")) return kExplorer;
  if (!strcmp(s, "down") || !strcmp(s, "lab") || !strcmp(s, "fmlab")) return kLab;
  return -1;
}

}  // namespace nlshell
