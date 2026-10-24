// nt3m_ab.cpp — bit-exact A/B of the NitroTron3m carve against shipping
// NitroTron3.
//
// The SAME harness is compiled three ways (tools/host/run.sh does it):
//   -DAB_SHIP  includes pedals/nitrotron3/main.cpp   (shipping pedal)
//   -DAB_M     includes pedals/nitrotron3m/main.cpp  (the module rebuild;
//              add -DNT3M_SLOT_B_ARMITAGE for the armitage smoke run)
//   (neither)  compare tool: reads two renders and reports per section.
//
// Each pedal runs its OWN main(): hardware is stubbed (tools/host/stub_nt3m/),
// StartAudio() hands the audio callback to the harness, and every hw.DelayMs(10)
// renders ten 48-sample (1 ms) blocks before returning to the pedal's loop — so
// PresetSystem::Tick, the mode dispatch, the modules' Controls() and the main-
// loop YIN run in the pedal's own order. The real PresetSystem reads the
// stubbed knobs / toggles / footswitches the script below writes.
//
// Input: a synthetic BASS performance (Karplus-Strong plucked strings: E1..C3,
// varied velocity, muted note ends, double-stops, rests, a low noise floor) —
// no sines. Script: every SW1 x SW2 position of all three modes with a knob
// sweep each, a fast SW3 mode-switch run, FS2 bypass taps, and a preset
// save / dirty / revert / manual / recall sequence (presets carry their mode).
//
// PASS (compare) = identical length, every sample bitwise identical (max abs
// diff 0), and every section carries signal. The armitage smoke run (-DAB_M
// -DNT3M_SLOT_B_ARMITAGE) only checks finite, |x| <= 10 and L == R.
//
// LIMITS: host x86 float, no FTZ, no FMA contraction (no -march) on BOTH sides,
// so this proves the carve computes the same arithmetic; it does not model the
// M7's flush-to-zero (on in nitrotron3m, off in shipping) or codegen-dependent
// FMA contraction. Run: tools/host/run.sh.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "daisy.h"
#include "hothouse.h"

uint32_t daisy::System::now_ms = 0;
daisy::AudioHandle::AudioCallback g_host_cb = nullptr;

#if defined(AB_SHIP) || defined(AB_M)
#define main pedal_main
#if defined(AB_SHIP)
#include "../../pedals/nitrotron3/main.cpp"
#else
#include "../../pedals/nitrotron3m/main.cpp"
#endif
#undef main
#endif

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------
enum Group { G_A = 0, G_B, G_C, G_SWITCH, G_BYPASS, G_PRESET, G_COUNT };
static const char* const kGroupName[G_COUNT] = {
  "mode A (bordun)  9 x SW1/SW2", "mode B (sprawl)  9 x SW1/SW2",
  "mode C (schism)  9 x SW1/SW2", "SW3 mode switching", "FS2 bypass",
  "presets (save/dirty/revert/manual/recall)" };

struct Seg {
  Group    group;
  uint32_t t0, dur;          // ms
  float    k[6];
  int      sw[3];            // SW1, SW2, SW3 (0 UP / 1 MID / 2 DOWN)
  int      sweep;            // knob index swept linearly over the segment, -1 none
  float    s_from, s_to;
};
struct Press { uint32_t t0, hold; int fs; };   // footswitch press (fs 0 = FS1, 1 = FS2)

static std::vector<Seg>   g_segs;
static std::vector<Press> g_press;
static uint32_t           g_total = 0;

static void AddSeg(Group g, uint32_t dur, const float k[6], int sw1, int sw2, int sw3,
                   int sweep = -1, float a = 0.f, float b = 1.f) {
  Seg s;
  s.group = g; s.t0 = g_total; s.dur = dur;
  for (int i = 0; i < 6; i++) s.k[i] = k[i];
  s.sw[0] = sw1; s.sw[1] = sw2; s.sw[2] = sw3;
  s.sweep = sweep; s.s_from = a; s.s_to = b;
  g_segs.push_back(s);
  g_total += dur;
}

static void BuildScript() {
  g_segs.clear(); g_press.clear(); g_total = 0;
  // 1) every SW1 x SW2 of each mode; knob sweeps rotate over K1..K5.
  for (int m = 0; m < 3; m++) {
    for (int s1 = 0; s1 < 3; s1++) {
      for (int s2 = 0; s2 < 3; s2++) {
        const int n = s1 * 3 + s2;
        float k[6] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.75f };
        if (m == 0) { k[0] = 0.55f; k[1] = 0.4f; k[3] = 0.35f + 0.05f * s2; k[4] = (n & 1) ? 0.2f : 0.8f; }
        if (m == 1) { k[1] = (n % 3 == 0) ? 0.5f : (n % 3 == 1 ? 0.85f : 0.15f);
                      k[2] = (n & 1) ? 0.8f : 0.25f; k[3] = 0.7f; k[4] = (n & 1) ? 0.85f : 0.2f; }
        if (m == 2) { k[0] = 0.45f; k[1] = 0.6f; k[2] = (n & 1) ? 0.85f : 0.2f; k[3] = (n & 1) ? 0.2f : 0.8f; k[4] = 0.65f; }
        AddSeg(static_cast<Group>(G_A + m), 1500, k, s1, s2, m, n % 5, 0.f, 1.f);
      }
    }
  }
  // 2) fast mode switching (SW3), switches and K4 moving too.
  {
    static const int seq[] = { 0, 1, 2, 1, 0, 2, 0, 1, 2, 2, 1, 0, 1, 2, 0, 0, 2, 1, 0, 1 };
    for (int i = 0; i < 20; i++) {
      float k[6] = { 0.6f, 0.7f, 0.6f, 0.3f + 0.03f * i, 0.75f, 0.8f };
      AddSeg(G_SWITCH, 150 + 10 * (i % 4), k, i % 3, (i / 3) % 3, seq[i]);
    }
  }
  // 3) bypass: Mode B with feedback, FS2 taps (short press = bypass toggle).
  {
    float k[6] = { 0.7f, 0.8f, 0.6f, 0.5f, 0.85f, 0.8f };
    const uint32_t t = g_total;
    AddSeg(G_BYPASS, 3000, k, 0, 1, 1);
    g_press.push_back({ t + 500,  100, 1 });
    g_press.push_back({ t + 1500, 100, 1 });
    float kc[6] = { 0.4f, 0.5f, 0.8f, 0.8f, 0.6f, 0.7f };
    const uint32_t t2 = g_total;
    AddSeg(G_BYPASS, 1500, kc, 1, 0, 2);
    g_press.push_back({ t2 + 400, 100, 1 });
    g_press.push_back({ t2 + 900, 100, 1 });
  }
  // 4) presets: save in Mode C, SW3 to A (dirty), FS1 revert (back to C),
  //    move a knob (takeover), FS1 long = manual (A), FS1 = preset 1 again (C).
  {
    float kc[6] = { 0.3f, 0.7f, 0.85f, 0.25f, 0.7f, 0.75f };
    uint32_t t = g_total;
    AddSeg(G_PRESET, 2400, kc, 2, 0, 2);
    g_press.push_back({ t + 300,  800, 1 });   // FS2 long: save mode
    g_press.push_back({ t + 1400, 800, 1 });   // FS2 long: confirm -> slot 1 (mode C)
    float ka[6] = { 0.6f, 0.45f, 0.5f, 0.6f, 0.3f, 0.7f };
    t = g_total;
    AddSeg(G_PRESET, 1200, ka, 0, 2, 0);       // SW3 -> A, knobs moved: dirty
    g_press.push_back({ t + 900, 100, 0 });    // FS1 short: revert to preset 1 (C)
    t = g_total;
    AddSeg(G_PRESET, 1500, ka, 0, 2, 0, 3, 0.6f, 0.95f);   // K4 moves: takeover
    t = g_total;
    AddSeg(G_PRESET, 1200, ka, 0, 2, 0);
    g_press.push_back({ t + 200, 800, 0 });    // FS1 long: manual (mode A from SW3)
    t = g_total;
    AddSeg(G_PRESET, 1500, ka, 0, 2, 0);
    g_press.push_back({ t + 300, 100, 0 });    // FS1 short: preset 1 (mode C)
  }
}

static int GroupAt(uint32_t t_ms) {
  for (const Seg& s : g_segs)
    if (t_ms >= s.t0 && t_ms < s.t0 + s.dur) return s.group;
  return -1;
}

// ---------------------------------------------------------------------------
// Input: synthetic bass performance (Karplus-Strong plucks)
// ---------------------------------------------------------------------------
static uint32_t g_rng = 0x1234567u;
static float Rnd() {   // 0..1
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return (float)(g_rng >> 8) * (1.f / 16777216.f);
}

struct KsString {
  std::vector<float> d; size_t n = 0, pos = 0; float decay = 0.f, z = 0.f; bool on = false;
  void Pluck(float f, float vel) {
    n = (size_t)(48000.f / f + 0.5f); d.assign(n, 0.f); pos = 0; z = 0.f;
    float lp = 0.f;
    for (size_t i = 0; i < n; i++) {              // pick-position-ish filtered burst
      lp += 0.5f * ((Rnd() * 2.f - 1.f) - lp);
      d[i] = lp * vel;
    }
    decay = 0.9985f; on = true;
  }
  void Mute() { decay = 0.93f; }
  float Tick() {
    if (!on) return 0.f;
    const size_t j = (pos + 1) % n;
    const float y = d[pos];
    const float nv = decay * 0.5f * (d[pos] + d[j]);
    z = y;
    d[pos] = nv;
    pos = j;
    return y;
  }
};

static std::vector<float> MakeInput(size_t samples) {
  std::vector<float> x(samples, 0.f);
  static const float notes[] = { 41.20f, 55.00f, 73.42f, 98.00f, 61.74f, 82.41f, 130.81f, 46.25f, 110.0f, 65.41f };
  KsString str[2];
  size_t next = 2400, mute_at = 0, note = 0;
  for (size_t i = 0; i < samples; i++) {
    if (i == next) {
      const float vel = 0.12f + 0.5f * Rnd();
      str[0].Pluck(notes[note % 10], vel);
      if (note % 7 == 3) str[1].Pluck(notes[(note + 2) % 10] * 1.5f, vel * 0.8f);   // double-stop
      const size_t len = 9600 + (size_t)(Rnd() * 24000.f);                         // 200..700 ms
      mute_at = i + len;
      next = mute_at + ((note % 5 == 4) ? 24000 : (size_t)(Rnd() * 2400.f));       // rests
      note++;
    }
    if (i == mute_at) { str[0].Mute(); str[1].Mute(); }
    x[i] = str[0].Tick() + str[1].Tick() + 2e-5f * (Rnd() * 2.f - 1.f);
  }
  return x;
}

#if defined(AB_SHIP) || defined(AB_M)
// ---------------------------------------------------------------------------
// Run mode
// ---------------------------------------------------------------------------
static std::vector<float> g_in, g_out;
static size_t   g_pos = 0;
static uint32_t g_t = 0;
static const char* g_out_path = nullptr;
static size_t   g_lr_mismatch = 0;

static void ApplyScript(uint32_t t) {
  for (const Seg& s : g_segs) {
    if (t >= s.t0 && t < s.t0 + s.dur) {
      for (int i = 0; i < 6; i++) hw.knob[i] = s.k[i];
      if (s.sweep >= 0)
        hw.knob[s.sweep] = s.s_from + (s.s_to - s.s_from) * (float)(t - s.t0) / (float)s.dur;
      for (int i = 0; i < 3; i++) hw.tsw[i] = s.sw[i];
      break;
    }
  }
  bool fs[2] = { false, false };
  for (const Press& p : g_press)
    if (t >= p.t0 && t < p.t0 + p.hold) fs[p.fs] = true;
  hw.switches[Hothouse::FOOTSWITCH_1].down = fs[0];
  hw.switches[Hothouse::FOOTSWITCH_2].down = fs[1];
}

static void Finish() {
  FILE* f = std::fopen(g_out_path, "wb");
  if (!f) { std::perror(g_out_path); std::exit(2); }
  std::fwrite(g_out.data(), sizeof(float), g_out.size(), f);
  std::fclose(f);
  size_t bad = 0; float pk = 0.f;
  for (float v : g_out) { if (!std::isfinite(v) || std::fabs(v) > 10.f) bad++; else if (std::fabs(v) > pk) pk = std::fabs(v); }
  std::printf("  rendered %zu samples (%.1f s), peak %.3f, non-finite/>10: %zu, L!=R: %zu\n",
              g_out.size(), g_out.size() / 48000.0, pk, bad, g_lr_mismatch);
  std::exit((bad == 0 && g_lr_mismatch == 0) ? 0 : 1);
}

void HostDelayMs(size_t ms) {
  float l[48], r[48];
  for (size_t b = 0; b < ms; b++) {
    const float* in_ch[2] = { &g_in[g_pos], &g_in[g_pos] };
    float* out_ch[2] = { l, r };
    g_host_cb(in_ch, out_ch, 48);
    for (int i = 0; i < 48; i++) {
      if (std::memcmp(&l[i], &r[i], sizeof(float)) != 0) g_lr_mismatch++;
      g_out.push_back(l[i]);
    }
    g_pos += 48;
    daisy::System::now_ms++;
  }
  g_t += (uint32_t)ms;
  if (g_t >= g_total) Finish();
  ApplyScript(g_t);
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s <out.f32>\n", argv[0]); return 2; }
  g_out_path = argv[1];
  BuildScript();
  g_in = MakeInput((size_t)(g_total + 100) * 48);
  g_out.reserve((size_t)g_total * 48);
  ApplyScript(0);
  pedal_main();   // never returns: Finish() exits at the end of the script
  return 4;
}

#else
// ---------------------------------------------------------------------------
// Compare mode
// ---------------------------------------------------------------------------
static std::vector<float> Load(const char* p) {
  std::vector<float> v;
  FILE* f = std::fopen(p, "rb");
  if (!f) { std::perror(p); std::exit(2); }
  float buf[4096]; size_t n;
  while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0) v.insert(v.end(), buf, buf + n);
  std::fclose(f);
  return v;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s <ship.f32> <m.f32>\n", argv[0]); return 2; }
  BuildScript();
  const std::vector<float> a = Load(argv[1]), b = Load(argv[2]);
  if (a.size() != b.size() || a.empty()) {
    std::printf("  length mismatch: %zu vs %zu\n", a.size(), b.size());
    return 1;
  }
  double maxd[G_COUNT] = {}, ss[G_COUNT] = {};
  size_t ndiff[G_COUNT] = {}, cnt[G_COUNT] = {};
  bool fail = false;
  for (size_t i = 0; i < a.size(); i++) {
    const int g = GroupAt((uint32_t)(i / 48));
    if (g < 0) continue;
    const bool same = std::memcmp(&a[i], &b[i], sizeof(float)) == 0;
    if (!same) { ndiff[g]++; fail = true; }
    double d = std::fabs((double)a[i] - (double)b[i]);
    if (!(d <= 1e30)) d = 1e30;   // nan
    if (d > maxd[g]) maxd[g] = d;
    ss[g] += (double)a[i] * a[i]; cnt[g]++;
  }
  for (int g = 0; g < G_COUNT; g++) {
    const double rms = cnt[g] ? std::sqrt(ss[g] / cnt[g]) : 0.0;
    if (rms < 1e-4) fail = true;   // a silent section would prove nothing
    std::printf("  %-44s max|diff| %.3g  differing samples %zu / %zu  (signal rms %.4f)\n",
                kGroupName[g], maxd[g], ndiff[g], cnt[g], rms);
  }
  std::printf("  %s\n", fail ? "NOT bit-exact (or a silent section)" : "bit-exact: every sample identical");
  return fail ? 1 : 0;
}
#endif
