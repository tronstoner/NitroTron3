// test_mnemonic.cpp — host test for the real mnemonic module (locked 2026-10-09).
//
// Drives Controls() every 10 ms and PreProcess() + Process() every 48 samples
// like the shell, over plucked-string input (Karplus-Strong, guitar + bass
// ranges — never sines, see the memory notes). Locks in:
//   K3 pre-effect (SW2 UP): noon / bypass bit-exact, 0.1..20 Hz exponential
//     rate for phaser and flanger, the 10 % fade-in, click-free switching,
//     levels near the input, and the flanger's one-past-the-end read fix.
//   Degrade (K4) with sprawl's voicing: no self-oscillation with feedback
//     (silent input), level flat across K4 (post-loop makeup).
//   Read-side placement: K4 and K1 are heard at once, not one delay later.
//   PLL (SW2 DOWN): leaving PLL mode does not jump the running echo (#18).
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <random>
#include "daisy.h"
#include "hothouse.h"
#include "control_surface.h"
#include "knob_map.h"
#include "constants.h"
#define private public
#include "mnemonic.h"
#undef private
uint32_t daisy::System::now_ms = 0;

static const float SR = 48000.f;
static Mnemonic m;
static ControlSurface cs;
static daisy::Led led1, led2;
static int failures = 0;
static void Check(bool ok, const char* what) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}
// Raw knob value that RemapKnob turns into v.
static float Raw(float v) { return KNOB_MIN + v * (KNOB_MAX - KNOB_MIN); }

struct KS {
  std::vector<float> b; size_t i = 0;
  void Pluck(float f, std::mt19937& r) {
    b.assign((size_t)(SR / f), 0.f);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    for (auto& x : b) x = u(r);
    i = 0;
  }
  float Tick() {
    if (b.empty()) return 0.f;
    size_t j = (i + 1) % b.size();
    float y = 0.5f * (b[i] + b[j]) * 0.996f, o = b[i];
    b[i] = y; i = j; return o;
  }
};
static const float kGuitar[4] = {82.4f, 110.f, 196.f, 329.6f};
static const float kBass[4]   = {41.2f, 55.f, 73.4f, 98.f};

// Plucked input: a new note every `every` s until `stop` s, then silence.
struct Player {
  std::mt19937 r{5}; KS ks; float lp = 0.f; int ni = 0;
  const float* notes; float amp, every, stop;
  Player(const float* n, float a, float e = 0.5f, float s = 1e9f) : notes(n), amp(a), every(e), stop(s) {}
  float Next(long k) {
    if ((float)k / SR < stop && k % (long)(SR * every) == 0) ks.Pluck(notes[ni++ % 4], r);
    float v = ((float)k / SR < stop) ? ks.Tick() : 0.f;
    lp += (v - lp) * 0.5f;
    return lp * amp;
  }
};

static long n_ = 0;
static void Reset(int sw2, float k1, float k3, float k4, float k5) {
  m.Init(SR); m.Activate(); n_ = 0;
  // Init() does not touch the transport / pre-effect state (pedal power-up does):
  m.bypassed_ = false; m.kill_fired_ = false; m.panic_active_ = false;
  m.pre_kind_ = 0; m.pre_kind_tgt_ = 0; m.pre_g_ = 0.f; m.pre_mix_ = 0.f; m.pre_mix_tgt_ = 0.f;
  cs = ControlSurface();
  cs.sw[1] = sw2; cs.knob[0] = Raw(k1); cs.knob[2] = Raw(k3); cs.knob[3] = Raw(k4);
  cs.knob[4] = Raw(k5); cs.knob[5] = Raw(1.f);
}
// One block: Controls on the 10 ms tick, then PreProcess + Process (= the shell).
static void Block(float* in, float* pre, float* wet) {
  if (n_ % 480 == 0) { daisy::System::now_ms += 10; m.Controls(cs, led1, led2); }
  for (int i = 0; i < 48; i++) pre[i] = in[i];
  m.PreProcess(pre, 48);
  m.Process(pre, wet, 48);
  n_ += 48;
}
static void Tap(int fs) {   // a short footswitch tap (pressed for 3 ticks)
  for (int t = 0; t < 4; t++) {
    FootswitchEvent& e = cs.fs[fs];
    const bool down = t < 3;
    e.rising = down && !e.down; e.falling = !down && e.down; e.down = down;
    e.held_ms = down ? (uint32_t)(t * 10) : 0;
    float in[48] = {}, pre[48], wet[48];
    for (int b = 0; b < 10; b++) Block(in, pre, wet);
  }
  cs.fs[fs] = FootswitchEvent();   // edges are one-tick events
}
// Treble share (energy above ~3 kHz over total), dB.
struct Tr {
  double ss = 0, hs = 0; float z = 0, x1 = 0;
  void Add(float w) {
    const float a = 1.f / (1.f + 6.2831853f * 3000.f / SR);
    z = a * (z + w - x1); x1 = w; ss += w * w; hs += z * z;
  }
  double Db() const { return 10.0 * log10(hs / (ss + 1e-30) + 1e-30); }
  double Lvl() const { return 10.0 * log10(ss + 1e-30); }
};

// ---------------------------------------------------------------------------
static void TestPreEffect() {
  printf("K3 pre-effect (SW2 UP)\n");
  // Noon: bit-exact at the physical noon (K3 is read raw by the pre-effect).
  {
    Reset(0, 0.5f, 0.5f, 0.5f, 0.5f);
    cs.knob[2] = 0.5f;
    Player p(kGuitar, 0.03f); bool exact = true;
    for (int b = 0; b < 2000; b++) {
      float in[48], pre[48], wet[48];
      for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
      Block(in, pre, wet);
      for (int i = 0; i < 48; i++) if (pre[i] != in[i]) exact = false;
    }
    Check(exact, "K3 noon: input passes bit-exact (dry + send untouched)");
  }
  // Bypass: bit-exact even with the phaser dialled in.
  {
    Reset(0, 0.5f, 0.2f, 0.5f, 0.5f);
    Player p(kGuitar, 0.03f);
    float in[48], pre[48], wet[48];
    for (int b = 0; b < 400; b++) { for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i); Block(in, pre, wet); }
    Tap(1);   // FS2 tap = bypass
    for (int b = 0; b < 20; b++) { for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i); Block(in, pre, wet); }
    bool exact = m.bypassed_;
    printf("        bypassed after the FS2 tap: %d\n", (int)m.bypassed_);
    for (int b = 0; b < 1000; b++) {
      for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
      Block(in, pre, wet);
      for (int i = 0; i < 48; i++) if (pre[i] != in[i]) exact = false;
    }
    Check(exact, "bypass: phaser off, input passes bit-exact");
  }
  // Rate law: 0.1 Hz just past the dead zone, 20 Hz at the end, exponential between.
  {
    auto rates = [](float u, float& ph, float& fl) {
      const float off = MNEM_PRE_DEADZONE + u * (0.5f - MNEM_PRE_DEADZONE);
      // (the pre-effect reads K3 RAW — no RemapKnob end zones)
      Reset(0, 0.5f, 0.5f, 0.5f, 0.5f); cs.knob[2] = 0.5f - off; m.Controls(cs, led1, led2); ph = m.phaser_.lfo_rate_hz_;
      Reset(0, 0.5f, 0.5f, 0.5f, 0.5f); cs.knob[2] = 0.5f + off; m.Controls(cs, led1, led2); fl = m.flanger_.inc_ * SR;
    };
    bool ok = true;
    for (float u : {0.001f, 0.25f, 0.5f, 0.75f, 0.85f, 0.925f, 1.f}) {
      float ph, fl; rates(u, ph, fl);
      const float knee = 0.1f * powf(200.f, 0.85f);   // ~9 Hz
      const float want = (u <= 0.85f) ? 0.1f * powf(200.f, u)
                                      : knee * powf(200.f / knee, (u - 0.85f) / 0.15f);
      if (fabsf(ph / want - 1.f) > 0.02f || fabsf(fl / want - 1.f) > 0.02f) ok = false;
      printf("        u=%.2f  want %.3f Hz  phaser %.3f  flanger %.3f\n", u, want, ph, fl);
    }
    Check(ok, "rate: 0.1 -> ~9 Hz over 85 % (plain exponential), then on to 200 Hz; phaser = flanger");
  }
  // Fade-in over the first 10 % past the dead zone.
  {
    auto mix = [](float u) {
      Reset(0, 0.5f, 0.5f, 0.5f, 0.5f);
      cs.knob[2] = 0.5f - (MNEM_PRE_DEADZONE + u * (0.5f - MNEM_PRE_DEADZONE));   // raw
      m.Controls(cs, led1, led2); return m.pre_mix_tgt_;
    };
    Check(fabsf(mix(0.05f) - 0.5f) < 0.02f && mix(0.2f) == 1.f && mix(0.001f) < 0.02f,
          "fade-in: 0 at the dead-zone edge, half at 5 %, full from 10 %");
  }
  // Levels and click-free switching.
  {
    struct Sc { const char* name; float a, b; };
    const Sc sc[] = {{"phaser", 0.25f, 0.25f}, {"flanger", 0.75f, 0.75f},
                     {"off->phaser", 0.5f, 0.25f}, {"phaser->flanger", 0.25f, 0.75f}, {"flanger->off", 0.75f, 0.5f}};
    bool lvl_ok = true, click_ok = true;
    for (const Sc& s : sc) for (int inst = 0; inst < 2; inst++) {
      Reset(0, 0.5f, s.a, 0.5f, 0.5f);
      Player p(inst ? kBass : kGuitar, inst ? 0.1f : 0.03f);
      double si = 0, so = 0; float prev = 0.f, step_sw = 0.f, step_ref = 0.f;
      for (long b = 0; b < (long)(SR * 6) / 48; b++) {
        if (n_ >= (long)(SR * 4)) cs.knob[2] = Raw(s.b);
        float in[48], pre[48], wet[48];
        for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
        const long n0 = n_;
        Block(in, pre, wet);
        for (int i = 0; i < 48; i++) {
          const long k = n0 + i; const float add = pre[i] - in[i], d = fabsf(add - prev); prev = add;
          if (k > SR && k < SR * 4) { si += in[i] * in[i]; so += pre[i] * pre[i]; }
          if (k > SR * 3.95 && k < SR * 4.3) step_sw = fmaxf(step_sw, d);
          else if ((k > SR && k < SR * 3.9) || (k > SR * 4.4)) step_ref = fmaxf(step_ref, d);
        }
      }
      const double db = 10.0 * log10(so / si);
      if (s.a == s.b && fabs(db) > 1.5) lvl_ok = false;
      const bool switched = s.a != s.b;
      if (switched && step_sw > fmaxf(2.f * step_ref, 0.01f)) click_ok = false;
      printf("        %-16s %s  level %+5.1f dB  step at switch %.4f (playing %.4f)\n",
             s.name, inst ? "bass  " : "guitar", db, step_sw, step_ref);
    }
    Check(lvl_ok, "phaser and flanger within +-1.5 dB of the input");
    // The switch itself is a crossfade: at least ~4 ms between 0 and full.
    {
      Reset(0, 0.5f, 0.5f, 0.5f, 0.5f);
      float in[48] = {}, pre[48], wet[48];
      for (int b = 0; b < 50; b++) Block(in, pre, wet);
      cs.knob[2] = Raw(0.25f);
      daisy::System::now_ms += 10; m.Controls(cs, led1, led2);
      int ramp = 0;
      for (int k = 0; k < 4800; k++) {
        float x = 0.01f; m.PreProcess(&x, 1);
        if (m.pre_g_ > 0.f && m.pre_g_ < 1.f) ramp++;
      }
      Check(ramp >= (int)(0.004f * SR), "switching on ramps over >= 4 ms (no hard switch)");
    }
    Check(click_ok, "on / off / phaser<->flanger: no step larger than 2x the playing's");
  }
  // Flanger: the read position can round up to exactly kLen (min delay 0.5 ms).
  {
    Flanger f; f.Init(SR, 0.5f, 8.f, 0.5f);
    float mx = 0.f;
    for (int k = 0; k < 4800; k++) mx = fmaxf(mx, fabsf(f.Process(0.01f)));
    Check(mx < 0.1f, "flanger: no read one past the end (min-delay wrap)");
  }
}

// ---------------------------------------------------------------------------
static void TestDegrade() {
  printf("K4 degrade (sprawl voicing, post-loop makeup)\n");
  // No self-oscillation with feedback: a few plucks, then silence.
  {
    bool ok = true;
    for (float k4 : {0.f, 0.25f, 0.75f, 1.f}) {
      Reset(0, 0.5f, 0.5f, k4, 0.75f);
      Player p(kGuitar, 0.03f, 0.5f, 2.f);
      double ss = 0; long cnt = 0;
      for (long b = 0; b < (long)(SR * 12) / 48; b++) {
        float in[48], pre[48], wet[48];
        for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
        const long n0 = n_;
        Block(in, pre, wet);
        for (int i = 0; i < 48; i++) if (n0 + i > SR * 10) { ss += wet[i] * wet[i]; cnt++; }
      }
      const double rms = sqrt(ss / cnt);
      if (rms > 1e-3) ok = false;
      printf("        K4 %.2f  K5 ~14:30  tail rms 8-10 s after the last note: %.6f\n", k4, rms);
    }
    Check(ok, "feedback + K4 anywhere: the tail dies out (no self-oscillation)");
  }
  // Level flat across K4 (one repeat), against the PHYSICAL noon (raw 0.5 reads
  // ~0.52: the engine is engaged there — the makeup table was measured that way).
  {
    auto lvl = [](float k4, int inst) {
      Reset(0, 0.5f, 0.5f, 0.5f, 0.5f);
      cs.knob[3] = k4;   // raw knob value
      Player p(inst ? kBass : kGuitar, inst ? 0.1f : 0.03f);
      double ss = 0;
      for (long b = 0; b < (long)(SR * 8) / 48; b++) {
        float in[48], pre[48], wet[48];
        for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
        const long n0 = n_;
        Block(in, pre, wet);
        for (int i = 0; i < 48; i++) if (n0 + i > SR * 2) ss += wet[i] * wet[i];
      }
      return 10.0 * log10(ss);
    };
    bool ok = true;
    for (int inst = 0; inst < 2; inst++) {
      const double ref = lvl(0.5f, inst);
      for (float k4 : {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f}) {
        const double d = lvl(k4, inst) - ref;
        if (fabs(d) > 2.0) ok = false;
        printf("        %s K4 %.1f  %+5.1f dB vs noon\n", inst ? "bass  " : "guitar", k4, d);
      }
    }
    Check(ok, "wet level within +-2 dB of K4 noon across the travel");
  }
}

// ---------------------------------------------------------------------------
// A knob step must change the wet within ~100 ms — not one delay time later.
static void TestReadSide() {
  printf("read-side placement (K1 / K4 heard at once)\n");
  auto step = [](int knob, float from, float to) {
    Reset(0, 0.5f, 0.5f, 0.5f, 0.6f);
    cs.knob[knob] = Raw(from);
    Player p(kGuitar, 0.03f, 0.25f);
    Tr before, after;
    const long t = (long)(SR * 4);
    for (long b = 0; b < (long)(SR * 4.3) / 48; b++) {
      if (n_ >= t) cs.knob[knob] = Raw(to);
      float in[48], pre[48], wet[48];
      for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
      const long n0 = n_;
      Block(in, pre, wet);
      for (int i = 0; i < 48; i++) {
        const long k = n0 + i;
        if (k >= t - (long)(SR * 0.2) && k < t) before.Add(wet[i]);
        if (k >= t + (long)(SR * 0.03) && k < t + (long)(SR * 0.23)) after.Add(wet[i]);
      }
    }
    return std::make_pair(after.Db() - before.Db(), m.read_delay_ * 1000.f / SR);
  };
  auto k4 = step(3, 0.5f, 0.f);   // noon -> full BBD (dark)
  auto k1 = step(0, 0.5f, 0.f);   // flat -> full LPF tilt (dark)
  printf("        delay %.0f ms; treble change 30-230 ms after the step: K4 %+5.1f dB, K1 %+5.1f dB\n",
         k4.second, k4.first, k1.first);
  Check(k4.second > 250.f, "(setup) the delay is longer than the measuring window (230 ms)");
  Check(k4.first < -3.0, "K4 step heard at once (degrade on the read side)");
  Check(k1.first < -3.0, "K1 step heard at once (tone on the read side)");
}

// ---------------------------------------------------------------------------
// #18: leaving PLL mode must not jump the running echo (trim before the delay).
static void TestPllSwitch() {
  printf("PLL mode switch (#18)\n");
  auto run = [](int to_sw2, int inst) {
    Reset(2, 0.5f, 0.4f, 0.5f, 0.7f);
    Player p(inst ? kBass : kGuitar, inst ? 0.1f : 0.03f, 0.5f, 6.f);
    double a = 0, b = 0; const long tog = (long)(SR * 7);
    for (long blk = 0; blk < (long)(SR * 8) / 48; blk++) {
      if (n_ >= tog) cs.sw[1] = to_sw2;
      float in[48], pre[48], wet[48];
      for (int i = 0; i < 48; i++) in[i] = p.Next(n_ + i);
      const long n0 = n_;
      Block(in, pre, wet);
      for (int i = 0; i < 48; i++) {
        const long k = n0 + i;
        if (k >= tog - (long)(SR * 0.5) && k < tog) a += wet[i] * wet[i];
        if (k >= tog + 480 && k < tog + (long)(SR * 0.5)) b += wet[i] * wet[i];
      }
    }
    return 10.0 * log10(b / a);
  };
  bool ok = true;
  for (int inst = 0; inst < 2; inst++) {
    const double base = run(2, inst), up = run(0, inst);
    if (fabs(up - base) > 1.5) ok = false;
    printf("        %s  DOWN->UP vs staying: %+5.1f dB\n", inst ? "bass  " : "guitar", up - base);
  }
  Check(ok, "PLL -> UP at K4 noon: the running echo keeps its level (+-1.5 dB)");
}

int main() {
  TestPreEffect();
  TestDegrade();
  TestReadSide();
  TestPllSwitch();
  printf(failures ? "%d FAILED\n" : "all checks passed\n", failures);
  return failures ? 1 : 0;
}
