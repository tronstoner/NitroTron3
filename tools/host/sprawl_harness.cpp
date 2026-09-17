// sprawl_harness.cpp — runs the REAL Sprawl module on the host with stubbed
// hardware (tools/host/stub/{daisy,hothouse,control_surface}.h). Drives
// Controls() every 10 ms and Process() every 48 samples exactly like the
// ChronoTron3 shell does, over a synthetic bass-pluck input, and sweeps K4/K2
// through the scenarios below.
//
// It checks each wet sample for NON-FINITE *and* for MAGNITUDE > 10 — the
// magnitude check is the one that matters: the module's own guard only sees
// nan/inf, so a huge-but-finite value (e.g. an out-of-bounds neighbour leaking
// into a read tap) walks straight past it.
//
// LIMITS: it cannot see memory-LAYOUT-dependent faults. On target the slabs sit
// in SDRAM next to each other; on the host the linker puts whatever it likes
// after them, so an out-of-bounds read may look harmless here. It also only
// checks what you tell it to. A clean run is not proof — the serial log
// (.agents/skills/serial-diag/SKILL.md) is the source of truth.
//
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <new>
#include "daisy.h"
#include "hothouse.h"
#include "control_surface.h"
#include "knob_map.h"
#include "sprawl.h"
uint32_t daisy::System::now_ms = 0;

static Sprawl sprawl;   // (slab neighbour on host is whatever the linker puts there)
static ControlSurface cs;
static daisy::Led led1, led2;

// bass-ish pluck train: 55/73/98 Hz notes with harmonics, ~0.7 s apart
static float Input(long n, float sr) {
  const float period = 0.7f;
  float t = fmodf((float)n / sr, period);
  int note = (int)(((float)n / sr) / period) % 3;
  float f0 = (note == 0) ? 55.f : (note == 1) ? 73.4f : 98.f;
  float env = expf(-t * 3.f) * (t < 0.005f ? t / 0.005f : 1.f);
  float ph = 2.f * 3.14159265f * f0 * (float)n / sr;
  return 0.25f * env * (sinf(ph) + 0.5f * sinf(2 * ph) + 0.25f * sinf(3 * ph));
}

struct Scenario { const char* name; int sw1; float k4lo, k4hi, k4period_s; bool wiggle_k2; float jitter; };

static int Run(const Scenario& sc, float seconds, bool verbose) {
  const float sr = 48000.f;
  sprawl.~Sprawl(); new (&sprawl) Sprawl();   // fresh module each scenario
  sprawl.Init(sr);
  cs = ControlSurface();
  cs.sw[0] = sc.sw1; cs.sw[1] = 0; cs.sw[2] = 2;
  cs.knob[0] = 0.5f;               // K1 noon
  cs.knob[1] = 0.75f;              // K2 3 o'clock (forward, engaged)
  cs.knob[2] = 0.5f;               // K3 noon
  cs.knob[3] = sc.k4lo;            // K4 start
  cs.knob[4] = 0.58f;              // K5 1 o'clock (feedback on)
  cs.knob[5] = 0.5f;
  daisy::System::now_ms = 0;
  sprawl.Activate();
  sprawl.Controls(cs, led1, led2);

  float in[48], wet[48];
  long total = (long)(seconds * sr);
  int faults = 0;
  long n = 0;
  while (n < total) {
    // control tick every 480 samples (10 ms)
    if (n % 480 == 0) {
      daisy::System::now_ms += 10;
      float tt = (float)n / sr;
      float tri = fabsf(fmodf(tt / sc.k4period_s, 1.f) * 2.f - 1.f);  // 0..1..0
      cs.knob[3] = sc.k4lo + tri * (sc.k4hi - sc.k4lo) + sc.jitter * ((float)rand() / (float)RAND_MAX - 0.5f);
      if (sc.wiggle_k2) cs.knob[1] = 0.72f + 0.06f * sinf(tt * 0.9f);
      sprawl.Controls(cs, led1, led2);
      SprawlDebug dd;
      if (sprawl.DebugTakeFaultSnapshot(dd)) {
        faults++;
        static const char* kP[4] = {"?", "grain-sum", "wet-bus", "post-reverb"};
        printf("  FAULT #%d at t=%.2fs stage=%s k4=%.3f k2=%.3f fb=%.3f badmask=%02x chain=%d mix=%.2f warb=%.1f\n",
               faults, tt, kP[dd.stage & 3], dd.k4, dd.k2, dd.fb_amt, dd.bad_voice_mask, dd.deg.chain, dd.deg.mix, dd.warble_int);
      }
    }
    for (int i = 0; i < 48; i++) in[i] = Input(n + i, sr);
    sprawl.Process(in, wet, 48);
    for (int i = 0; i < 48; i++) {
      if (!std::isfinite(wet[i])) { printf("  NON-FINITE ESCAPED GUARD t=%.2f\n", (float)n/sr); return 99; }
      if (fabsf(wet[i]) > 10.f) {       // MAGNITUDE check — the thing the guard cannot see
        static int shown = 0;
        if (shown++ < 6) printf("  HUGE FINITE wet=%g at t=%.2fs (k4=%.3f)\n", wet[i], (float)n/sr, cs.knob[3]);
        faults++;
        break;
      }
    }
    n += 48;
  }
  if (verbose) printf("  %s: %.0fs audio, %d fault(s)\n", sc.name, seconds, faults);
  return faults;
}

int main(int argc, char** argv) {
#if defined(__aarch64__)
  { uint64_t fpcr = __builtin_arm_rsr64("fpcr"); fpcr |= (1ull << 24); __builtin_arm_wsr64("fpcr", fpcr);
    printf("(FPCR.FZ set: flush-to-zero like the Cortex-M7 target)\n"); }
#endif
  float secs = argc > 1 ? (float)atof(argv[1]) : 60.f;
  Scenario scs[] = {
    {"MID colour, K4 parked ON the deadzone edge + ADC jitter, K5 fb on", 1, 0.435f, 0.445f, 4.0f, false, 0.012f},
    {"MID colour, K4 0.30<->0.55 + jitter",                                1, 0.30f,  0.55f,  3.0f, false, 0.012f},
    {"MID colour, K4 0.30<->0.55 + jitter + K2 wiggle",                    1, 0.30f,  0.55f,  3.0f, true,  0.012f},
    {"MID colour, K4 parked at 0.5 (noon) + jitter",                       1, 0.495f, 0.505f, 4.0f, false, 0.012f},
    {"MID colour, K4 parked near CW edge 0.555 + jitter",                  1, 0.555f, 0.565f, 4.0f, false, 0.012f},
  };
  int total = 0;
  for (auto& sc : scs) { printf("== %s\n", sc.name); total += Run(sc, secs, true); }
  printf("\nTOTAL faults: %d\n", total);
  return total ? 1 : 0;
}
