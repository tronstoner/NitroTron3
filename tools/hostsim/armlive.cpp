// armlive — real-time host version of armsim for the NitroLab modules:
// armitage (SW3 UP), explorer (SW3 MID), FM LAB (SW3 DOWN), switchable live.
//
// Same engine path as armsim.cpp (nl_shell.h: unmodified modules, 48 kHz,
// 48-frame blocks, SW3 selection, PreProcess -> Process -> the shell's K6
// equal-power mix, FS1 tap, Controls() every 10th block), but driven by an audio device callback (miniaudio, default
// output; input = a looped WAV or the default capture device, first channel).
// A localhost WebSocket server (ws_server.h) takes control messages from
// ui.html (served over HTTP); constants are live-editable tunables.
//
// Threads:
//   audio  (miniaudio callback): runs the engine. No allocation, no locks.
//          Reads knobs / switches / source / gains from atomics; drains a
//          single-producer ring of events (FS1 tap, constant set / reset) at
//          each control tick; publishes meters, DIAG snapshots and op acks
//          through single-producer rings the other way.
//   main   : WebSocket + HTTP server, JSON protocol, status every 100 ms.
// Constants (the tunable globals) are written ONLY by the audio thread; the
// server reads them only when every op it sent has been acknowledged.
//
// Usage: see tools/hostsim/README.md, or `armlive --help`.

#include "nl_shell.h"
#include "const_docs.h"
#include "json_mini.h"
#include "wav.h"
#include "ws_server.h"

#include "third_party/miniaudio.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <vector>

uint32_t daisy::System::now_ms = 0;

using nlshell::kSR;
static const size_t B = NL_BLOCK_SIZE;            // 48
static const int    kMeterFrames = 4800;          // meter window: 100 ms

// ---------------------------------------------------------------------------
// Lock-free single-producer / single-consumer ring.
template <class T, size_t N>
struct Spsc {
  T buf[N];
  std::atomic<size_t> head{0}, tail{0};
  bool Push(const T& v) {
    const size_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= N) return false;
    buf[h % N] = v;
    head.store(h + 1, std::memory_order_release);
    return true;
  }
  bool Pop(T& v) {
    const size_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    v = buf[t % N];
    tail.store(t + 1, std::memory_order_release);
    return true;
  }
};

enum OpType { OP_TAP = 0, OP_SET = 1, OP_RESET = 2 };
struct Op { int type; uint32_t seq; char name[64]; char val[48]; };
struct Ack { uint32_t seq; const char* err; };      // err: static string or nullptr
struct Meter { float peak, rms, in_peak, cpu; uint32_t reinits; };

// Shared state (server -> audio): continuous controls as atomics.
struct Shared {
  std::atomic<float> knob[6];
  std::atomic<int>   sw[3];               // SW1, SW2, SW3 (SW3 = module: 0 armitage, 1 explorer, 2 FM LAB)
  std::atomic<int>   active{0};           // audio -> server: the module the engine runs now
  std::atomic<int>   src{0};            // 0 = WAV, 1 = input device
  std::atomic<float> wav_gain{1.f};     // = level / file peak (1 = as is)
  std::atomic<float> in_gain{1.f};
  Spsc<Op, 256>  ops;                   // server -> audio
  Spsc<Ack, 1024> acks;                 // audio -> server (> ops: never full in practice)
  Spsc<Meter, 64> meters;               // audio -> server
  Spsc<nlshell::DiagMsg, 64> diags;     // audio -> server (tagged with the module)
};

// ---------------------------------------------------------------------------
// The shell (identical arithmetic to armsim.cpp's block loop).
struct Engine {
  Shared* sh = nullptr;
  nlshell::Shell* shell = nullptr;
  size_t blk = 0;
  uint32_t reinits = 0;
  // block FIFO: 48 frames of latency between the callback and the engine
  float in_acc[NL_BLOCK_SIZE] = {};
  float out_q[NL_BLOCK_SIZE] = {};
  size_t fill = 0;
  // WAV source
  const float* wav = nullptr;
  size_t wav_len = 0, wav_pos = 0;
  // meters
  float m_peak = 0.f, m_in_peak = 0.f;
  double m_ss = 0.0, m_proc_s = 0.0;
  int m_n = 0;

  void LoadControls() {
    for (int i = 0; i < 6; i++) shell->cs.knob[i] = nlshell::RawKnob(sh->knob[i].load(std::memory_order_relaxed));
    for (int i = 0; i < 3; i++) shell->cs.sw[i] = sh->sw[i].load(std::memory_order_relaxed);
  }

  void Start(bool fs1_at_start) {
    LoadControls();
    shell->Start(fs1_at_start);
    sh->active.store(shell->active);
  }

  // Applies one constant op on the audio thread. Returns nullptr or an error.
  static const char* ApplyTunable(const Op& op) {
    hostsim::Tunable* t = hostsim::Find(op.name);
    if (!t) return "unknown constant";
    if (t->fixed) return "fixed at compile time (array size / table mask), not settable";
    const double old_v = hostsim::Get(*t);
    const bool old_set = t->set;
    if (op.type == OP_SET) {
      if (t->ty == hostsim::Ty::F32) *(float*)t->ptr = strtof(op.val, nullptr);
      else hostsim::Put(*t, strtod(op.val, nullptr));
      t->set = true;
    } else {
      t->set = false;
    }
    hostsim::ApplyDefaults();                  // derived defaults follow
    if (const char* e = ValidateTunablesC()) {
      hostsim::Put(*t, old_v);
      t->set = old_set;
      hostsim::ApplyDefaults();
      return e;
    }
    return nullptr;
  }

  void ControlTick() {
    Op op;
    bool reinit = false;
    while (sh->ops.Pop(op)) {
      const char* err = nullptr;
      if (op.type == OP_TAP) shell->fs1_pending = true;
      else { err = ApplyTunable(op); if (!err) reinit = true; }
      sh->acks.Push({op.seq, err});
    }
    if (reinit) {                              // armitage constants read in Init() take effect
      shell->ReinitArmitage();
      reinits++;
    }
    LoadControls();
    // --- the shell's main-loop tick (SW3, Controls(), DIAG snapshots) ---
    shell->ControlTick([this](const nlshell::DiagMsg& m) { sh->diags.Push(m); });
    sh->active.store(shell->active, std::memory_order_relaxed);
  }

  // One 48-frame block: x -> y (armsim's loop body).
  void Block(const float* in, float* out) {
    if (blk % nlshell::kControlEveryBlocks == 0) ControlTick();
    blk++;
    shell->Process(in, out);
  }

  // The device callback body. cap: interleaved capture (cap_ch channels, may be
  // null); out: interleaved output (out_ch channels, mono copied to each).
  void Render(const float* cap, int cap_ch, float* out, int out_ch, size_t frames) {
    const auto t0 = std::chrono::steady_clock::now();
    const int src = sh->src.load(std::memory_order_relaxed);
    const float wg = sh->wav_gain.load(std::memory_order_relaxed);
    const float ig = sh->in_gain.load(std::memory_order_relaxed);
    for (size_t f = 0; f < frames; f++) {
      float x = 0.f;
      if (src == 1) {
        if (cap) x = cap[f * (size_t)cap_ch] * ig;
      } else if (wav_len) {
        x = wav[wav_pos] * wg;
        if (++wav_pos >= wav_len) wav_pos = 0;
      }
      in_acc[fill] = x;
      const float y = out_q[fill];
      if (++fill == B) { Block(in_acc, out_q); fill = 0; }
      for (int c = 0; c < out_ch; c++) out[f * (size_t)out_ch + c] = y;
      const float ax = fabsf(x), ay = fabsf(y);
      if (ax > m_in_peak) m_in_peak = ax;
      if (ay > m_peak) m_peak = ay;
      m_ss += (double)y * y;
      if (++m_n >= kMeterFrames) {
        const double audio_s = (double)m_n / kSR;
        sh->meters.Push({m_peak, (float)sqrt(m_ss / m_n), m_in_peak, (float)(m_proc_s / audio_s), reinits});
        m_peak = m_in_peak = 0.f;
        m_ss = 0.0;
        m_proc_s = 0.0;
        m_n = 0;
      }
    }
    m_proc_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  }
};

static Shared  g_sh;
static Engine  g_eng;
static nlshell::Shell g_shell;                   // large: static
static std::atomic<bool> g_quit{false};

static void DataCallback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
  const bool has_cap = (dev->type == ma_device_type_duplex) && in;
  g_eng.Render(has_cap ? (const float*)in : nullptr, has_cap ? (int)dev->capture.channels : 0,
               (float*)out, (int)dev->playback.channels, frames);
}

// ---------------------------------------------------------------------------
static int ParseSwitch(const std::string& s) {
  if (s == "up") return 0;
  if (s == "mid" || s == "middle") return 1;
  if (s == "down") return 2;
  return -1;
}
static const char* SwName(int v) { return v == 0 ? "up" : v == 2 ? "down" : "mid"; }
static const char* ModeName(int m) {
  switch (m) { case 0: return "SAW"; case 1: return "PULSE"; case 2: return "ADD";
               case 3: return "FB"; case 4: return "UNSHAPED"; }
  return "?";
}

// Explorer / FM LAB DIAG second half, as pedals/nitrolab/main.cpp prints it
// (same branches and names; %.2f instead of the firmware's FLT_FMT(2) macros).
static std::string ExplorerDiagLine(const Explorer::DiagSnap& d) {
  char env[128], line[256];
  const char* lfo = d.lfo == 1 ? "tri" : d.lfo == 2 ? "stp" : "off";
  const char* dir = d.dir < 0 ? "dec" : "rise";
  if (d.loop)
    snprintf(env, sizeof env, " loop e%.2f %s d%.2f %s", d.env, d.dir > 0 ? "up" : d.dir < 0 ? "down" : "off", d.d,
             d.tgt == 2 ? "K2" : d.tgt == 3 ? "K3" : "-");
  else if (d.specz >= 0)
    snprintf(env, sizeof env, " g%.2f %s T%.2f d%.2f L%.2f o%.2f", d.env, dir, d.T, d.se, d.sL, d.so);
  else if (d.labz >= 0)
    snprintf(env, sizeof env, " ps g%.2f %s T%.2f lfo %s d%.2f >%.2f %.2fHz", d.env, dir, d.T, lfo, d.lfod, d.llfd, d.lfohz);
  else if (d.sw1 == 0 || d.addz >= 0)
    snprintf(env, sizeof env, " ps g%.2f %s T%.2f fce %d lfo %s d%.2f %.2fHz", d.env, dir, d.T, (int)(d.fce + 0.5f),
             lfo, d.lfod, d.lfohz);
  else if (d.sw1 == 1)
    snprintf(env, sizeof env, " ps g%.2f %s T%.2f lfo %s d%.2f %.2fHz", d.env, dir, d.T, lfo, d.lfod, d.lfohz);
  else
    snprintf(env, sizeof env, " ps - lfo -");
  const char* zn = Explorer::ZoneName(d.model, d.zone);
  const char* mu = d.mute ? "1" : "0";
  if (d.addz >= 0 && Explorer::AddK2Name(d.addz)[0] == 'u')
    snprintf(line, sizeof line, "LAB %s u%.2f bt%.2f %s %s%.2f fc %d%s mute %s", Explorer::AddZoneName(d.addz), d.a, d.b,
             d.bm == 1 ? "prop" : d.bm == 2 ? "hz" : "off", Explorer::AddK3Name(d.addz), d.c3, (int)(d.fc + 0.5f), env, mu);
  else if (d.addz >= 0)
    snprintf(line, sizeof line, "LAB %s %s%.2f >%.2f %s%.2f fc %d%s mute %s", Explorer::AddZoneName(d.addz),
             Explorer::AddK2Name(d.addz), d.a, d.b, Explorer::AddK3Name(d.addz), d.c3, (int)(d.fc + 0.5f), env, mu);
  else if (d.specz >= 0)
    snprintf(line, sizeof line, "LAB SPEC bank %d/9 %s nu%.2f k2%.2f lpf %s sub %s%s mute %s", (int)d.sn + 1,
             Explorer::SpecBankName((int)d.sn), d.a, d.b,
             d.slp > 0.f ? std::to_string((int)(d.slp + 0.5f)).c_str() : "off",
             d.ssub > 0.f ? std::to_string(d.ssub).substr(0, 4).c_str() : "off", env, mu);
  else if (d.labz >= 0)
    snprintf(line, sizeof line, "LAB %s r%.2f %s fc %d b%.2f u%.2f%s mute %s", Explorer::LabZoneName(d.labz), d.a,
             Explorer::LabSideName(d.lside), (int)(d.b + 0.5f), d.lb, d.lue, env, mu);
  else if (d.model == Explorer::kFM)
    snprintf(line, sizeof line, "FM r%.2f b%.2f fc %d%s mute %s", d.a, d.b, (int)(d.fc + 0.5f), env, mu);
  else if (d.model == Explorer::kXFM)
    snprintf(line, sizeof line, "XBAND-FM n%d I%.2f fc %d%s mute %s", (int)(d.a + 0.5f), d.b, (int)(d.fc + 0.5f), env, mu);
  else if (d.model == Explorer::kWT)
    snprintf(line, sizeof line, "WT/%s m%.2f pos%.2f dmp %d lp %d eff %d%s mute %s", zn, d.b, d.a, (int)(d.dfc + 0.5f),
             (int)(d.fc + 0.5f), (int)(d.lfe + 0.5f), env, mu);
  else if (d.sw1 == 1)
    snprintf(line, sizeof line, "%s%s%s A%.2f B%.2f dmp %d lp %d eff %d%s mute %s", Explorer::ModelName(d.model),
             zn[0] ? "/" : "", zn, d.a, d.b, (int)(d.dfc + 0.5f), (int)(d.fc + 0.5f), (int)(d.lfe + 0.5f), env, mu);
  else
    snprintf(line, sizeof line, "%s A%.2f B%.2f%s mute %s", Explorer::ModelName(d.model), d.a, d.b, env, mu);
  return line;
}

// The selection name K1 picked (zone / model), for the page.
static std::string ExplorerZone(const Explorer::DiagSnap& d) {
  if (d.addz >= 0) return Explorer::AddZoneName(d.addz);
  if (d.specz >= 0) return Explorer::SpecZoneName(d.specz);
  if (d.labz >= 0) return Explorer::LabZoneName(d.labz);
  const char* zn = Explorer::ZoneName(d.model, d.zone);
  return std::string(Explorer::ModelName(d.model)) + (zn[0] ? std::string("/") + zn : std::string());
}

// Shortest text that reads back as the same value (float: as Dump()).
static std::string FmtTunable(const hostsim::Tunable& t, double v) {
  char buf[64];
  if (t.ty == hostsim::Ty::F32) {
    for (int prec = 6; prec <= 9; prec++) {
      snprintf(buf, sizeof buf, "%.*g", prec, v);
      if (strtof(buf, nullptr) == (float)v) break;
    }
  } else if (t.ty == hostsim::Ty::BOOL) {
    snprintf(buf, sizeof buf, "%s", v != 0.0 ? "true" : "false");
  } else {
    snprintf(buf, sizeof buf, "%.0f", v);
  }
  return buf;
}

// Comments of armitage_constants.h (section / doc block / inline note per name),
// read once at startup (const_docs.h). Empty when the file was not found.
static constdocs::Docs g_docs;

// {"tunables":[...], "constants_file": path} — only call when no op is pending
// (values are stable).
static std::string TunablesJson() {
  std::string s = "{\"constants_file\":" + json::Esc(g_docs.path) + ",\"tunables\":[";
  bool first = true;
  for (auto& t : hostsim::Registry()) {
    const double v = hostsim::Get(t);
    const double d = t.fixed ? t.fixed_val : t.def();
    s += first ? "" : ",";
    first = false;
    s += "{\"name\":" + json::Esc(t.name) + ",\"type\":" + json::Esc(hostsim::TyName(t.ty)) +
         ",\"value\":" + FmtTunable(t, v) +
         ",\"default\":" + FmtTunable(t, d) + ",\"expr\":" + json::Esc(t.def_text) +
         ",\"fixed\":" + (t.fixed ? "true" : "false") + ",\"set\":" + (t.set ? "true" : "false");
    auto it = g_docs.by_name.find(t.name);
    const constdocs::Entry none;
    const constdocs::Entry& e = it == g_docs.by_name.end() ? none : it->second;
    s += ",\"section\":" + json::Esc(e.section) + ",\"doc\":" + json::Esc(e.doc) + ",\"doc_of\":" + json::Esc(e.doc_of) +
         ",\"note\":" + json::Esc(e.note) + "}";
  }
  return s + "]}";
}

static std::string ReadFile(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
  fclose(f);
  return s;
}

static void Usage(FILE* f) {
  fprintf(f,
    "armlive [--in in.wav [--level P] | --input] [options]\n"
    "  --in FILE         WAV input, looped (48 kHz; channel 0)\n"
    "  --level P         scale the WAV so its peak is P (e.g. 0.18); default: as is\n"
    "  --input           start on the default capture device (first channel) instead of the WAV\n"
    "  --gain G          linear gain on the capture input (default 1)\n"
    "  --k1 .. --k6 V    start knob positions 0..1 (default K1 .5, K2 .5, K3 0, K4 .5, K5 .5, K6 1)\n"
    "  --sw1/--sw2 up|mid|down   start switch positions (default mid / mid)\n"
    "  --sw3 up|mid|down start module: up = armitage (default), mid = explorer, down = FM LAB\n"
    "  --module armitage|explorer|lab   same as --sw3\n"
    "  --fs1 0|1         1 = FS1 tapped once at the start\n"
    "  --set NAME=VALUE  start value of an armitage_constants.h tunable (repeatable; armitage only)\n"
    "  --ws-port N       WebSocket port (default 8765)\n"
    "  --http-port N     HTTP port serving ui.html (default 8766)\n"
    "  --ui FILE         the page to serve (default: ui.html next to the executable)\n"
    "  --constants FILE  armitage_constants.h to read the comments from (default:\n"
    "                    pedals/nitrolab/modules/armitage_constants.h of the repo the\n"
    "                    executable sits in, i.e. <exe dir>/../.., else of the cwd)\n"
    "  --null-audio      miniaudio's null backend: real-time callback, no sound\n"
    "                    (server / protocol tests without the speakers)\n"
    "  --selftest        no device, no server: render --seconds (default 2) offline\n"
    "                    through the same callback path (irregular callback sizes),\n"
    "                    the 48-frame FIFO latency removed; write --out-f32 FILE\n"
    "  --seconds S       selftest length (default 2)\n"
    "  --out-f32 FILE    selftest output, raw float32 mono\n");
}

static void OnSignal(int) { g_quit = true; }

int main(int argc, char** argv) {
  const char* in_path = nullptr;
  const char* f32_path = nullptr;
  float level = -1.f, gain = 1.f, seconds = 2.f;
  bool start_input = false, selftest = false, null_audio = false;
  float knob[6] = {0.5f, 0.5f, 0.f, 0.5f, 0.5f, 1.f};
  int sw[3] = {1, 1, nlshell::kArmitage};
  int fs1 = 0, ws_port = 8765, http_port = 8766;
  std::string ui_path, constants_path;
  std::vector<const char*> sets;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) { fprintf(stderr, "armlive: %s needs a value\n", what); exit(2); }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") { Usage(stdout); return 0; }
    else if (a == "--in") in_path = need("--in");
    else if (a == "--level") level = strtof(need("--level"), nullptr);
    else if (a == "--input") start_input = true;
    else if (a == "--gain") gain = strtof(need("--gain"), nullptr);
    else if (a.size() == 4 && a.compare(0, 3, "--k") == 0 && a[3] >= '1' && a[3] <= '6')
      knob[a[3] - '1'] = strtof(need(a.c_str()), nullptr);
    else if (a == "--sw1" || a == "--sw2") {
      const int v = ParseSwitch(need(a.c_str()));
      if (v < 0) { fprintf(stderr, "armlive: %s expects up|mid|down\n", a.c_str()); return 2; }
      sw[a[4] - '1'] = v;
    }
    else if (a == "--sw3" || a == "--module") {
      sw[2] = nlshell::ParseModule(need(a.c_str()));
      if (sw[2] < 0) { fprintf(stderr, "armlive: %s expects up|mid|down or armitage|explorer|lab\n", a.c_str()); return 2; }
    }
    else if (a == "--fs1") fs1 = atoi(need("--fs1"));
    else if (a == "--set") sets.push_back(need("--set"));
    else if (a == "--ws-port") ws_port = atoi(need("--ws-port"));
    else if (a == "--http-port") http_port = atoi(need("--http-port"));
    else if (a == "--ui") ui_path = need("--ui");
    else if (a == "--constants") constants_path = need("--constants");
    else if (a == "--selftest") selftest = true;
    else if (a == "--null-audio") null_audio = true;
    else if (a == "--seconds") seconds = strtof(need("--seconds"), nullptr);
    else if (a == "--out-f32") f32_path = need("--out-f32");
    else { fprintf(stderr, "armlive: unknown option %s\n", a.c_str()); Usage(stderr); return 2; }
  }
  for (const char* s : sets) {
    const std::string err = hostsim::SetFromString(s);
    if (!err.empty()) { fprintf(stderr, "armlive: %s\n", err.c_str()); return 2; }
  }
  hostsim::ApplyDefaults();
  if (const char* e = ValidateTunablesC()) { fprintf(stderr, "armlive: %s\n", e); return 2; }
  for (int i = 0; i < 6; i++)
    if (!(knob[i] >= 0.f && knob[i] <= 1.f)) { fprintf(stderr, "armlive: --k%d must be in 0..1\n", i + 1); return 2; }
  if (!in_path && !start_input) { fprintf(stderr, "armlive: give --in FILE and/or --input\n"); Usage(stderr); return 2; }
  if (selftest && !in_path) { fprintf(stderr, "armlive: --selftest needs --in\n"); return 2; }

  // --- WAV source (same scaling as armsim: sc = level / peak, as a float) ---
  std::vector<float> src;
  float wav_peak = 0.f;
  if (in_path) {
    wav::Info info;
    const std::string err = wav::Read(in_path, src, info);
    if (!err.empty()) { fprintf(stderr, "armlive: %s\n", err.c_str()); return 1; }
    if (info.rate != 48000) { fprintf(stderr, "armlive: input is %d Hz; the pedal runs at 48000 (no resampling)\n", info.rate); return 1; }
    if (info.channels > 1) fprintf(stderr, "armlive: %d-channel input, using channel 0\n", info.channels);
    if (src.empty()) { fprintf(stderr, "armlive: empty input\n"); return 1; }
    for (float v : src) wav_peak = fmaxf(wav_peak, fabsf(v));
  }
  auto WavGain = [&](float lvl) -> float {
    if (lvl <= 0.f || wav_peak <= 0.f) return 1.f;
    return (float)((double)lvl / (double)wav_peak);
  };

  for (int i = 0; i < 6; i++) g_sh.knob[i].store(knob[i]);
  for (int i = 0; i < 3; i++) g_sh.sw[i].store(sw[i]);
  g_sh.src.store(start_input ? 1 : 0);
  g_sh.wav_gain.store(WavGain(level));
  g_sh.in_gain.store(gain);
  g_eng.sh = &g_sh;
  g_eng.shell = &g_shell;
  g_eng.wav = src.empty() ? nullptr : src.data();
  g_eng.wav_len = src.size();

  // ------------------------------------------------------------------ selftest
  if (selftest) {
    const auto ti0 = std::chrono::steady_clock::now();
    g_eng.Start(fs1 != 0);
    const double init_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ti0).count();
    const size_t n = (size_t)(seconds * kSR + 0.5f);
    std::vector<float> out(n + B, 0.f);
    static const size_t chunks[] = {512, 37, 1000, 48, 333, 1, 4096, 95};
    std::vector<float> st(2 * 4096);
    size_t pos = 0, ci = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (pos < out.size()) {
      size_t c = chunks[ci++ % (sizeof chunks / sizeof chunks[0])];
      if (c > out.size() - pos) c = out.size() - pos;
      g_eng.Render(nullptr, 0, st.data(), 2, c);
      for (size_t f = 0; f < c; f++) {
        if (st[2 * f] != st[2 * f + 1]) { fprintf(stderr, "armlive: channel mismatch\n"); return 1; }
        out[pos + f] = st[2 * f];
      }
      pos += c;
    }
    const double run_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // Remove the FIFO latency (exactly one block).
    std::vector<float> y(out.begin() + B, out.begin() + B + n);
    float peak = 0.f;
    double ss = 0.0;
    for (float v : y) { peak = fmaxf(peak, fabsf(v)); ss += (double)v * v; }
    fprintf(stderr, "armlive selftest: %zu samples (%.2f s), latency %zu frames removed, out peak %.4f, rms %.4f\n",
            y.size(), y.size() / 48000.0, B, peak, sqrt(ss / (double)y.size()));
    fprintf(stderr, "armlive selftest: Init() %.3f ms, render %.1f x real time on this host\n",
            init_ms, (y.size() / 48000.0) / run_s);
    if (f32_path) {
      FILE* f = fopen(f32_path, "wb");
      if (!f) { fprintf(stderr, "armlive: cannot write %s\n", f32_path); return 1; }
      fwrite(y.data(), sizeof(float), y.size(), f);
      fclose(f);
    }
    return 0;
  }

  // ------------------------------------------------------------------ live
  if (ui_path.empty()) {
    std::string exe = argv[0];
    const size_t sl = exe.rfind('/');
    ui_path = (sl == std::string::npos ? std::string(".") : exe.substr(0, sl)) + "/ui.html";
  }
  if (ReadFile(ui_path).empty()) fprintf(stderr, "armlive: warning: cannot read %s (HTTP will 404)\n", ui_path.c_str());

  // --- constant comments (section / doc / note), read from the source file ---
  {
    std::vector<std::string> cands;
    if (!constants_path.empty()) cands.push_back(constants_path);
    else {
      const std::string rel = "pedals/nitrolab/modules/armitage_constants.h";
      std::string exe = argv[0];
      const size_t sl = exe.rfind('/');
      const std::string dir = sl == std::string::npos ? std::string(".") : exe.substr(0, sl);
      cands.push_back(dir + "/../../" + rel);   // the executable lives in tools/hostsim/
      cands.push_back(rel);                     // run from the repo root
    }
    bool ok = false;
    for (auto& c : cands) if (constdocs::Load(c, g_docs)) { ok = true; break; }
    if (!ok) {
      fprintf(stderr, "armlive: warning: cannot read %s - the page shows the constants without comments\n",
              cands[0].c_str());
    } else {
      int missing = 0, extra = 0, nodoc = 0, nonote = 0;
      for (auto& t : hostsim::Registry()) {
        auto it = g_docs.by_name.find(t.name);
        if (it == g_docs.by_name.end()) {
          if (missing++ < 5) fprintf(stderr, "armlive: warning: %s is not declared in %s (no comments)\n", t.name, g_docs.path.c_str());
          continue;
        }
        if (it->second.doc.empty()) nodoc++;
        if (it->second.note.empty()) nonote++;
      }
      for (auto& n : g_docs.order) if (!hostsim::Find(n.c_str())) extra++;
      fprintf(stderr, "armlive: comments from %s: %zu tunables, %d without a doc block, %d without an inline note%s\n",
              g_docs.path.c_str(), hostsim::Registry().size() - missing, nodoc, nonote,
              missing || extra ? " (file and build differ: rebuild?)" : "");
      if (extra) fprintf(stderr, "armlive: warning: %d constant(s) in the file are not in this build (stale binary?)\n", extra);
    }
  }

  g_eng.Start(fs1 != 0);

  ma_device dev;
  ma_context ctx;
  ma_context* pctx = nullptr;
  if (null_audio) {
    const ma_backend nb[] = { ma_backend_null };
    if (ma_context_init(nb, 1, nullptr, &ctx) != MA_SUCCESS) { fprintf(stderr, "armlive: null backend unavailable\n"); return 1; }
    pctx = &ctx;
  }
  bool input_ok = true;
  {
    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = 48000;                     // miniaudio converts if the hardware runs otherwise
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 0;                   // native; we take the first channel
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = DataCallback;
    if (ma_device_init(pctx, &cfg, &dev) != MA_SUCCESS) {
      input_ok = false;
      fprintf(stderr, "armlive: no duplex device (no input?) - output only, the input source is unavailable\n");
      cfg = ma_device_config_init(ma_device_type_playback);
      cfg.sampleRate = 48000;
      cfg.playback.format = ma_format_f32;
      cfg.playback.channels = 2;
      cfg.performanceProfile = ma_performance_profile_low_latency;
      cfg.dataCallback = DataCallback;
      if (ma_device_init(pctx, &cfg, &dev) != MA_SUCCESS) { fprintf(stderr, "armlive: cannot open the audio output\n"); return 1; }
    }
  }
  if (!input_ok && start_input) { fprintf(stderr, "armlive: --input requested but no capture device\n"); return 1; }
  fprintf(stderr, "armlive: output \"%s\"%s%s%s, 48000 Hz engine (device %u Hz), period %u frames\n",
          dev.playback.name, input_ok ? ", input \"" : "", input_ok ? dev.capture.name : "", input_ok ? "\"" : "",
          dev.playback.internalSampleRate, dev.playback.internalPeriodSizeInFrames);

  ws::Server srv;
  {
    std::string err;
    if (!srv.Listen(ws_port, http_port, err)) { fprintf(stderr, "armlive: %s\n", err.c_str()); ma_device_uninit(&dev); return 1; }
  }

  // Server-side mirror of the continuous controls (the UI's start state).
  float lvl = level;
  float k_now[6];
  for (int i = 0; i < 6; i++) k_now[i] = knob[i];
  uint32_t sent_seq = 0, acked_seq = 0;
  bool tunables_dirty = false;
  bool want_tunables_for_new = false;
  std::vector<int> new_clients;
  std::vector<Op> backlog;                     // ops the full ring could not take yet
  Meter meter{0.f, 0.f, 0.f, 0.f, 0};
  nlshell::DiagMsg diag{};
  bool have_diag = false;
  auto diag_t = std::chrono::steady_clock::now();
  const auto t_start = diag_t;

  auto PushOp = [&](Op op) {
    op.seq = ++sent_seq;
    if (!backlog.empty() || !g_sh.ops.Push(op)) backlog.push_back(op);
  };
  auto StateJson = [&]() {
    std::string s = "{\"state\":{\"k\":[";
    for (int i = 0; i < 6; i++) s += (i ? "," : "") + json::Num(k_now[i], 9);
    s += "],\"sw1\":\"" + std::string(SwName(g_sh.sw[0].load())) + "\",\"sw2\":\"" + SwName(g_sh.sw[1].load()) +
         "\",\"sw3\":\"" + SwName(g_sh.sw[2].load()) + "\",\"module\":\"" + nlshell::ModuleName(g_sh.sw[2].load()) +
         "\",\"src\":\"" + (g_sh.src.load() ? "input" : "wav") + "\",\"level\":" + json::Num(lvl) +
         ",\"gain\":" + json::Num(g_sh.in_gain.load()) + ",\"wav\":" + json::Esc(in_path ? in_path : "") +
         ",\"input_ok\":" + (input_ok ? "true" : "false") + "}}";
    return s;
  };

  srv.http_body = [&]() { return ReadFile(ui_path); };
  srv.on_open = [&](ws::Client& c) {
    srv.Send(c, StateJson());
    if (sent_seq == acked_seq) srv.Send(c, TunablesJson());
    else want_tunables_for_new = true;          // sent once the pending ops are acked
  };
  srv.on_text = [&](ws::Client& c, const std::string& line) {
    json::Value v;
    std::string err;
    if (!json::Parser(line).Parse(v, err) || v.t != json::Value::Obj) {
      srv.Send(c, "{\"error\":" + json::Esc("bad message: " + (err.empty() ? "expected an object" : err)) + "}");
      return;
    }
    auto Err = [&](const std::string& e) { srv.Send(c, "{\"error\":" + json::Esc(e) + "}"); };
    for (auto& kv : v.o) {
      const std::string& key = kv.first;
      const json::Value& x = kv.second;
      if (key == "k") {
        if (x.t != json::Value::Arr || x.a.size() > 6) { Err("k expects an array of up to 6 numbers"); continue; }
        for (size_t i = 0; i < x.a.size(); i++) {
          if (x.a[i].t != json::Value::Num) continue;   // null = leave that knob
          float k = (float)x.a[i].n;
          k = k < 0.f ? 0.f : k > 1.f ? 1.f : k;
          k_now[i] = k;
          g_sh.knob[i].store(k, std::memory_order_relaxed);
        }
      } else if (key == "sw1" || key == "sw2" || key == "sw3") {
        const int s = (x.t == json::Value::Str) ? ParseSwitch(x.s) : -1;
        if (s < 0) { Err(key + " expects up|mid|down"); continue; }
        g_sh.sw[key[2] - '1'].store(s, std::memory_order_relaxed);
      } else if (key == "module") {
        const int s = (x.t == json::Value::Str) ? nlshell::ParseModule(x.s.c_str()) : -1;
        if (s < 0) { Err("module expects armitage|explorer|lab"); continue; }
        g_sh.sw[2].store(s, std::memory_order_relaxed);
      } else if (key == "fs1") {
        Op op{};
        op.type = OP_TAP;
        PushOp(op);
      } else if (key == "src") {
        if (x.t != json::Value::Str || (x.s != "wav" && x.s != "input")) { Err("src expects wav|input"); continue; }
        if (x.s == "input" && !input_ok) { Err("no capture device"); continue; }
        if (x.s == "wav" && !in_path) { Err("no WAV loaded (start with --in FILE)"); continue; }
        g_sh.src.store(x.s == "input" ? 1 : 0);
      } else if (key == "level") {
        if (x.t != json::Value::Num || !(x.n > 0.0)) { Err("level expects a number > 0 (WAV peak)"); continue; }
        lvl = (float)x.n;
        g_sh.wav_gain.store(WavGain(lvl));
      } else if (key == "gain") {
        if (x.t != json::Value::Num || !(x.n >= 0.0)) { Err("gain expects a number >= 0"); continue; }
        g_sh.in_gain.store((float)x.n);
      } else if (key == "set") {
        if (x.t != json::Value::Obj) { Err("set expects an object {NAME: value}"); continue; }
        for (auto& nv : x.o) {
          const hostsim::Tunable* t = hostsim::Find(nv.first.c_str());
          if (!t) { Err("unknown constant: " + nv.first); continue; }
          if (t->fixed) { Err(nv.first + " is fixed at compile time, not settable"); continue; }
          std::string val;
          if (t->ty == hostsim::Ty::BOOL) {
            if (nv.second.t == json::Value::Bool) val = nv.second.b ? "1" : "0";
            else if (nv.second.t == json::Value::Num) val = nv.second.n != 0.0 ? "1" : "0";
          } else if (nv.second.t == json::Value::Num && std::isfinite(nv.second.n)) {
            if (t->ty != hostsim::Ty::F32 && nv.second.n != floor(nv.second.n)) { Err(nv.first + " is an integer"); continue; }
            if (t->ty == hostsim::Ty::U32 && nv.second.n < 0) { Err(nv.first + " is unsigned"); continue; }
            val = nv.second.s;   // the number's source text
          }
          if (val.empty() || val.size() >= sizeof(Op::val)) { Err("bad value for " + nv.first); continue; }
          Op op{};
          op.type = OP_SET;
          snprintf(op.name, sizeof op.name, "%s", t->name);
          snprintf(op.val, sizeof op.val, "%s", val.c_str());
          PushOp(op);
        }
      } else if (key == "reset") {
        std::vector<std::string> names;
        if (x.t == json::Value::Str) names.push_back(x.s);
        else if (x.t == json::Value::Arr) for (auto& e : x.a) if (e.t == json::Value::Str) names.push_back(e.s);
        for (auto& nm : names) {
          const hostsim::Tunable* t = hostsim::Find(nm.c_str());
          if (!t || t->fixed) { Err("cannot reset " + nm); continue; }
          Op op{};
          op.type = OP_RESET;
          snprintf(op.name, sizeof op.name, "%s", t->name);
          PushOp(op);
        }
      } else {
        Err("unknown key: " + key);
      }
    }
  };

  signal(SIGINT, OnSignal);
  signal(SIGTERM, OnSignal);
  signal(SIGPIPE, SIG_IGN);
  if (ma_device_start(&dev) != MA_SUCCESS) { fprintf(stderr, "armlive: cannot start the audio device\n"); ma_device_uninit(&dev); return 1; }
  fprintf(stderr, "armlive: running. Open http://localhost:%d/  (WebSocket ws://localhost:%d)  Ctrl-C quits.\n",
          http_port, ws_port);

  auto next_status = std::chrono::steady_clock::now();
  while (!g_quit) {
    srv.Poll(10);
    // backlog -> ring
    while (!backlog.empty() && g_sh.ops.Push(backlog.front())) backlog.erase(backlog.begin());
    // acks
    Ack ack;
    while (g_sh.acks.Pop(ack)) {
      acked_seq = ack.seq;
      if (ack.err) srv.Broadcast("{\"error\":" + json::Esc(ack.err) + "}");
      tunables_dirty = true;
    }
    if (sent_seq == acked_seq && (tunables_dirty || want_tunables_for_new)) {
      srv.Broadcast(TunablesJson());
      tunables_dirty = want_tunables_for_new = false;
    }
    Meter m;
    while (g_sh.meters.Pop(m)) meter = m;
    nlshell::DiagMsg d;
    while (g_sh.diags.Pop(d)) { diag = d; have_diag = true; diag_t = std::chrono::steady_clock::now(); }
    const int active = g_sh.active.load(std::memory_order_relaxed);
    if (have_diag && diag.mod != active) have_diag = false;   // another module's snapshot: stale

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_status) {
      next_status = now + std::chrono::milliseconds(100);
      if (srv.OpenCount()) {
        std::string s = "{\"t\":" + json::Num(std::chrono::duration<double>(now - t_start).count(), 6) +
                        ",\"cpu\":" + json::Num(meter.cpu, 4) + ",\"peak\":" + json::Num(meter.peak, 5) +
                        ",\"rms\":" + json::Num(meter.rms, 5) + ",\"in_peak\":" + json::Num(meter.in_peak, 5) +
                        ",\"reinits\":" + std::to_string(meter.reinits) +
                        ",\"src\":\"" + (g_sh.src.load() ? "input" : "wav") + "\"";
        s += ",\"active\":\"" + std::string(nlshell::ModuleName(active)) + "\"";
        if (have_diag) {
          s += ",\"diag_mod\":\"" + std::string(nlshell::ModuleName(diag.mod)) + "\"" +
               ",\"diag_age_ms\":" + std::to_string((long)std::chrono::duration_cast<std::chrono::milliseconds>(now - diag_t).count());
          if (diag.mod == nlshell::kArmitage) {
            const Armitage::DiagSnap& a = diag.a;
            s += ",\"n\":" + std::to_string(a.n) + ",\"mode\":\"" + ModeName(a.mode) + "\",\"h\":" + json::Num(a.h, 4) +
                 ",\"k\":[" + json::Num(a.k[0], 4) + "," + json::Num(a.k[1], 4) + "," + json::Num(a.k[2], 4) + "," +
                 json::Num(a.k[3], 4) + "," + json::Num(a.k[4], 4) + "],\"sw1\":\"" + SwName(a.sw1) + "\",\"sw2\":\"" + SwName(a.sw2) +
                 "\",\"beta\":" + json::Num(a.beta, 4) + ",\"filt_t\":" + json::Num(a.filt_t, 4) +
                 ",\"fm\":" + json::Num(a.fm, 4) + ",\"env_t2\":" + json::Num(a.env_t2, 4) + ",\"env_e\":" + json::Num(a.env_e, 4) +
                 ",\"envf_t2\":" + json::Num(a.envf_t2, 4) + ",\"envf_e\":" + json::Num(a.envf_e, 4) + ",\"fol\":" + json::Num(a.fol, 4) +
                 ",\"oct\":" + std::to_string(a.oct) + ",\"mute\":" + (a.mute ? "true" : "false");
          } else {
            const Explorer::DiagSnap& x = diag.x;
            auto N = [](float v) { return json::Num(v, 5); };
            const bool add = x.addz >= 0;
            s += ",\"n\":" + std::to_string(x.n) + ",\"k\":[" + N(x.k[0]) + "," + N(x.k[1]) + "," + N(x.k[2]) + "," +
                 N(x.k[3]) + "," + N(x.k[4]) + "],\"sw1\":\"" + SwName(x.sw1) + "\",\"sw2\":\"" + SwName(x.sw2) + "\"" +
                 ",\"model\":" + json::Esc(Explorer::ModelName(x.model)) + ",\"zone\":" + json::Esc(ExplorerZone(x)) +
                 ",\"line\":" + json::Esc(ExplorerDiagLine(x)) +
                 ",\"labz\":" + std::to_string(x.labz) + ",\"specz\":" + std::to_string(x.specz) + ",\"addz\":" + std::to_string(x.addz) +
                 ",\"k2n\":" + json::Esc(add ? Explorer::AddK2Name(x.addz) : "") + ",\"k3n\":" + json::Esc(add ? Explorer::AddK3Name(x.addz) : "") +
                 ",\"a\":" + N(x.a) + ",\"b\":" + N(x.b) + ",\"slp\":" + N(x.slp) + ",\"ssub\":" + N(x.ssub) + ",\"c3\":" + N(x.c3) + ",\"bm\":" + std::to_string(x.bm) +
                 ",\"fc\":" + N(x.fc) + ",\"lfe\":" + N(x.lfe) + ",\"dfc\":" + N(x.dfc) +
                 ",\"lb\":" + N(x.lb) + ",\"lue\":" + N(x.lue) + ",\"llfd\":" + N(x.llfd) + ",\"lside\":" + std::to_string(x.lside) +
                 ",\"se\":" + N(x.se) + ",\"sn\":" + N(x.sn) + ",\"sL\":" + N(x.sL) + ",\"so\":" + N(x.so) +
                 ",\"env\":" + N(x.env) + ",\"dir\":" + std::to_string(x.dir) + ",\"T\":" + N(x.T) + ",\"fce\":" + N(x.fce) +
                 ",\"lfo\":" + std::to_string(x.lfo) + ",\"lfod\":" + N(x.lfod) + ",\"lfohz\":" + N(x.lfohz) +
                 ",\"loop\":" + (x.loop ? "true" : "false") + ",\"mute\":" + (x.mute ? "true" : "false");
          }
        }
        srv.Broadcast(s + "}");
      }
    }
    srv.Flush();
  }
  fprintf(stderr, "\narmlive: stopping\n");
  ma_device_uninit(&dev);
  if (pctx) ma_context_uninit(pctx);
  return 0;
}
