// armsim — offline renderer for the NitroLab modules: armitage (SW3 UP),
// explorer (SW3 MID), FM LAB (SW3 DOWN; --sw3 / --module).
//
// Runs the unmodified modules (pedals/nitrolab/modules/armitage.h, explorer.h) the way the
// NitroLab shell (pedals/nitrolab/main.cpp) does: 48 kHz, 48-frame audio
// blocks, PreProcess -> Process -> the shell's K6 equal-power dry/wet mix
// (one-pole NL_MIX_SMOOTH per sample, MixCurve(RemapKnob(K6))), Controls()
// at the main-loop rate (every 10 ms = every 10th block). Mono in / mono out.
// Armitage's constants are runtime tunables: --set NAME=value (see --dump-constants);
// explorer's are constexpr (no overrides). The shell itself is nl_shell.h.
//
// Usage: see tools/hostsim/README.md, or `armsim --help`.

#include "nl_shell.h"
#include "wav.h"

#include <string>
#include <vector>

uint32_t daisy::System::now_ms = 0;


static void Usage(FILE* f) {
  fprintf(f,
    "armsim --in in.wav --out out.wav [options]\n"
    "  --in FILE         WAV input (16/24/32-bit PCM or 32-bit float, 48 kHz; channel 0)\n"
    "  --out FILE        24-bit mono WAV output (the shell's output, after the K6 mix)\n"
    "  --out-f32 FILE    also write the output as raw float32 (no quantisation)\n"
    "  --loop N          play the input N times back to back (default 1)\n"
    "  --level P         scale the input so its peak is P (e.g. 0.18); default: as is\n"
    "  --k1 .. --k6 V    knob positions 0..1 (default K1 .5, K2 .5, K3 0, K4 .5, K5 .5, K6 1)\n"
    "  --sw1/--sw2 up|mid|down   (default mid / mid)\n"
    "  --sw3 up|mid|down module: up = armitage (default), mid = explorer, down = FM LAB\n"
    "  --module armitage|explorer|lab   same as --sw3\n"
    "  --fs1 0|1         1 = FS1 tapped once at the start (all three modules: wet muted)\n"
    "  --set NAME=VALUE  override an armitage_constants.h tunable (repeatable; armitage only)\n"
    "  --dump-constants  print every constant (after --set) and exit\n");
}

static int ParseSwitch(const char* s) {
  if (!strcmp(s, "up")) return 0;
  if (!strcmp(s, "mid") || !strcmp(s, "middle")) return 1;
  if (!strcmp(s, "down")) return 2;
  return -1;
}

int main(int argc, char** argv) {
  const char* in_path = nullptr;
  const char* out_path = nullptr;
  const char* f32_path = nullptr;
  int loops = 1;
  float level = -1.f;
  float knob[6] = {0.5f, 0.5f, 0.f, 0.5f, 0.5f, 1.f};
  int sw[2] = {1, 1};
  int fs1 = 0;
  int module = nlshell::kArmitage;
  bool dump = false;
  std::vector<const char*> sets;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) { fprintf(stderr, "armsim: %s needs a value\n", what); exit(2); }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") { Usage(stdout); return 0; }
    else if (a == "--in") in_path = need("--in");
    else if (a == "--out") out_path = need("--out");
    else if (a == "--out-f32") f32_path = need("--out-f32");
    else if (a == "--loop") loops = atoi(need("--loop"));
    else if (a == "--level") level = strtof(need("--level"), nullptr);
    else if (a.size() == 4 && a.compare(0, 3, "--k") == 0 && a[3] >= '1' && a[3] <= '6')
      knob[a[3] - '1'] = strtof(need(a.c_str()), nullptr);
    else if (a == "--sw1" || a == "--sw2") {
      const int v = ParseSwitch(need(a.c_str()));
      if (v < 0) { fprintf(stderr, "armsim: %s expects up|mid|down\n", a.c_str()); return 2; }
      sw[a[4] - '1'] = v;
    }
    else if (a == "--sw3" || a == "--module") {
      module = nlshell::ParseModule(need(a.c_str()));
      if (module < 0) { fprintf(stderr, "armsim: %s expects up|mid|down or armitage|explorer|lab\n", a.c_str()); return 2; }
    }
    else if (a == "--fs1") fs1 = atoi(need("--fs1"));
    else if (a == "--set") sets.push_back(need("--set"));
    else if (a == "--dump-constants") dump = true;
    else { fprintf(stderr, "armsim: unknown option %s\n", a.c_str()); Usage(stderr); return 2; }
  }

  // Constants: overrides first, then every non-overridden default is re-evaluated
  // (derived defaults follow), then the runtime form of the module's static_asserts.
  for (const char* s : sets) {
    const std::string err = hostsim::SetFromString(s);
    if (!err.empty()) { fprintf(stderr, "armsim: %s\n", err.c_str()); return 2; }
  }
  hostsim::ApplyDefaults();
  {
    const std::string err = ValidateTunables();
    if (!err.empty()) { fprintf(stderr, "armsim: %s\n", err.c_str()); return 2; }
  }
  if (dump) { hostsim::Dump(stdout); return 0; }

  if (!in_path || !out_path) { Usage(stderr); return 2; }
  for (int i = 0; i < 6; i++)
    if (!(knob[i] >= 0.f && knob[i] <= 1.f)) { fprintf(stderr, "armsim: --k%d must be in 0..1\n", i + 1); return 2; }
  if (loops < 1) { fprintf(stderr, "armsim: --loop must be >= 1\n"); return 2; }

  std::vector<float> src;
  wav::Info info;
  {
    const std::string err = wav::Read(in_path, src, info);
    if (!err.empty()) { fprintf(stderr, "armsim: %s\n", err.c_str()); return 1; }
  }
  if (info.rate != 48000) { fprintf(stderr, "armsim: input is %d Hz; the pedal runs at 48000 (no resampling)\n", info.rate); return 1; }
  if (info.channels > 1) fprintf(stderr, "armsim: %d-channel input, using channel 0\n", info.channels);
  if (src.empty()) { fprintf(stderr, "armsim: empty input\n"); return 1; }

  if (level > 0.f) {
    float peak = 0.f;
    for (float v : src) peak = fmaxf(peak, fabsf(v));
    if (peak <= 0.f) { fprintf(stderr, "armsim: input is silent, cannot scale to --level\n"); return 1; }
    const float sc = (float)((double)level / (double)peak);
    for (float& v : src) v *= sc;
  }

  std::vector<float> x;
  x.reserve(src.size() * loops);
  for (int l = 0; l < loops; l++) x.insert(x.end(), src.begin(), src.end());
  const size_t n_out = x.size();
  const size_t B = NL_BLOCK_SIZE;
  x.resize((n_out + B - 1) / B * B, 0.f);   // last partial block zero-padded, cut afterwards

  // --- the shell (nl_shell.h = pedals/nitrolab/main.cpp) -------------------
  static nlshell::Shell sh;                  // large: keep it off the stack
  for (int i = 0; i < 6; i++) sh.cs.knob[i] = nlshell::RawKnob(knob[i]);
  sh.cs.sw[0] = sw[0];
  sh.cs.sw[1] = sw[1];
  sh.cs.sw[2] = module;                      // SW3: 0 UP armitage, 1 MID explorer, 2 DOWN FM LAB
  sh.Start(fs1 != 0);

  std::vector<float> y(x.size(), 0.f);
  for (size_t blk = 0; blk * B < x.size(); blk++) {
    if (blk % nlshell::kControlEveryBlocks == 0) sh.ControlTick();
    sh.Process(&x[blk * B], &y[blk * B]);
  }
  y.resize(n_out);

  size_t clipped = 0;
  {
    const std::string err = wav::Write24(out_path, y, 48000, &clipped);
    if (!err.empty()) { fprintf(stderr, "armsim: %s\n", err.c_str()); return 1; }
  }
  if (f32_path) {
    FILE* f = fopen(f32_path, "wb");
    if (!f) { fprintf(stderr, "armsim: cannot write %s\n", f32_path); return 1; }
    fwrite(y.data(), sizeof(float), y.size(), f);
    fclose(f);
  }
  float peak = 0.f;
  double ss = 0.0;
  for (float v : y) { peak = fmaxf(peak, fabsf(v)); ss += (double)v * v; }
  fprintf(stderr, "armsim: %s, %zu samples (%.2f s), out peak %.4f, rms %.4f%s\n",
          nlshell::ModuleTitle(module), y.size(), y.size() / 48000.0, peak, sqrt(ss / (double)(y.empty() ? 1 : y.size())),
          clipped ? " (24-bit clipped samples present)" : "");
  if (clipped) fprintf(stderr, "armsim: %zu samples clipped at the 24-bit WAV write\n", clipped);
  return 0;
}
