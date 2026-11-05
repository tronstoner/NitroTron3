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
//                          K5 onset-triggered envelope on the K4 cutoff, K2 the same on the K3 FM index (UP))
// SW3 MIDDLE = explorer   (sound-exploration models on a copy of armitage's band
//                          engine — see modules/explorer.h; SW1 = family, K1 = selection
//                          (UP: FM ratio zones + XBAND, MID: table zone (WaveMorph's 10, copied),
//                          DOWN: model), K2 = tone (MID: morph),
//                          K3 = UP: dampener (per-band harmonic ceiling 150 Hz -> 4 kHz); MID: the same dampener
//                          (knob only, never modulated) + a 2-pole SVF lowpass (12 dB/oct, Q 0.707, 250 Hz -> 16 kHz;
//                          Moog ladder parked); DOWN: sweep,
//                          SW2 = octave up / played / down;
//                          K4 = pluck <-> swell on the K3 fc per onset (UP dampener, MID 2-pole LP only; inert on DOWN):
//                          CCW pluck (onset at the K3 fc, decays (2-pole: rounded start, snappy) to the bottom 150 / 250 Hz,
//                          0.45 s at the end -> 8 s at noon), noon plain K3, CW swell (onset at the bottom, rises to the
//                          K3 fc, 10 ms -> 4 s); after a K4 side change, until the next onset, travel >= 0.5 s;
//                          K5 = LFO on K2's parameter (UP FM feedback +-0.75, MID morph +-0.15; XBAND / DOWN none):
//                          CCW triangle | noon off | CW staircase 100/50/0 %; depth fades in over the first 30 %,
//                          rate 0.2 -> 20 Hz exponential over each side)
// SW3 DOWN   = FM LAB     (the same module, second instance Explorer(true): SW3 MIDDLE's one-shot engine with its OWN
//                          SW1 families (SW3 MIDDLE's FM / tables / harmonics & shift not offered here):
//                          SW1 UP = 2-OP FM: K1 = 3 stepped modes HARMONIC / DISHARMONIC / FREE (1 ms ratio slide),
//                          K2 = ratio (15 stepped zones, noon 1 / 1.414; FREE 0.125 -> 8 continuous), K3 = bipolar
//                          character, pitch-dependent index (SW3 MID's law: index fc 150 -> 4000 Hz over |K3 - noon|,
//                          index 12 capped per band; CCW hollow | noon pure sine | CW + feedback 0 -> 1.5 from noon), K4 pluck <-> swell along K3 (sine <-> the K3 position), K5 LFO on b
//                          (+-0.3, faded in with the swell); no XBAND.
//                          SW1 MID = SPECTRAL (Odessa-style): the additive SAW series per band (16 partials, no FM),
//                          weights tilt x comb in the partial-number domain (RMS-normalised), K1 = comb shape
//                          (exponent 0.25 narrow -> 1 cosine -> 4 peaky, continuous), K2 = comb density (0 none -> every
//                          second partial), K3 = tilt (dull -> flat at noon -> upper partials louder), K4 = partial
//                          shift (+-0.5 f', noon harmonic), K5 = partials bipolar: noon -> CW count N 1 -> 16, CCW of noon solo partial 1 -> 16 (stepped, 1 ms slide);
//                          no envelope / LFO. SW1 DOWN = ADDITIVE: one harmonic series per band (FM on its fundamental), K1 = 12
//                          stepped zones SAW / ODD / OCT / SUB in model A (K2 harmonics, K3 index) and B (K2 index, K3
//                          harmonics), UNI-SAW / UNI-ODD / UNI-SAW-H (K2 unison amount, K5 beat speed), STRETCH (K2
//                          stretch); K4 pluck <-> swell on K3, K5 LFO on K2 (+-0.3; unison: beat speed). The former
//                          LOOPING variant is retired
//                          (EXP_LOOP_ENABLE, explorer_constants.h). WaveMorph (modules/wavemorph.h) is unwired, source kept)

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"

#include "constants.h"          // pedals/nitrolab/     (-Ipedals/nitrolab)
#include "control_surface.h"    // src/core/io          (-Isrc/core/io)
#include "diag_link.h"          // src/core/io: DIAG `?` -> BUILD line
#include "knob_map.h"           // src/core/util        (-Isrc/core/util)
#include "module.h"             // pedals/nitrolab/

#include "modules/armitage.h"
#include "modules/explorer.h"

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

// SW3 DOWN = slot 2 (constants.h still names it NL_MODE_WAVEMORPH): the FM LAB explorer.
static constexpr int NL_MODE_EXPLORER_LAB = NL_MODE_WAVEMORPH;

Armitage armitage;
Explorer explorer;                 // SW3 MIDDLE: ONE-SHOT = K4 pluck <-> swell on the K3 fc (UP dampener / MID 2-pole LP)
Explorer explorer_lab(true);       // SW3 DOWN:   FM LAB = SW3 MIDDLE's engine, own SW1 families (UP 2-OP, MID SPECTRAL, DOWN ADDITIVE)
Module*  modules[NL_MODE_COUNT] = { &armitage, &explorer, &explorer_lab };

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
  DIAG_LINK_INIT();          // DIAG builds: answer `?` with the BUILD line
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

  // SW3 (toggle index 2): 0=UP=armitage, 1=MIDDLE=explorer (one-shot), 2=DOWN=explorer (FM LAB).
  int sel = cs.Switch(2);
  if (sel < 0 || sel >= NL_MODE_COUNT) sel = 0;
  g_active = sel;
  modules[g_active]->Activate();

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  uint32_t diag_hb = 0;
  int      diag_stage = 0;          // 1 / 2 = second half of an armitage / explorer (either instance) setting line due
  float    diag_cpu_last = 0.f;     // CPU peak of the last full 2 s window
  Armitage::DiagSnap diag_snap{};
  Explorer::DiagSnap diag_xsnap{};

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

    DIAG_LINK_POLL(hw.seed, "nitrolab");   // DIAG builds: BUILD line on `?`

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
        hw.seed.PrintLine("beta" D2F " fm" D2F " filt t" D2F " env t2" D2F " e" D2F " fmenv t2" D2F " e" D2F
                          " fol" D2F " oct %s mute %d cpu %u%%",
                          D2V(d.beta), D2V(d.fm), D2SV(d.filt_t), D2SV(d.env_t2), D2SV(d.env_e),
                          D2SV(d.envf_t2), D2SV(d.envf_e), D2V(d.fol),
                          oct_name[(d.oct >= 0 && d.oct < 3) ? d.oct : 1],
                          d.mute ? 1 : 0,
                          (unsigned)(cpu_pk * 100.f + 0.5f));
        diag_stage = 0;
      } else if (diag_stage == 2) {
        // explorer (either instance), second half: model (+ set), derived values (knob values, no envelope
        // offset), K3 fc (knob; SW1 UP `fc` dampener; MID `dmp` the tables' dampener fc (knob, unmodulated), `lp` the
        // 2-pole LP cutoff at the knob, `eff` the effective LP cutoff), envelope (retired LOOPING `loop`: e, K4 dir, K5 amount
        // d + target; MID `ps`: pluck <-> swell g, side dec|rise, time T in use (s), applied fc_eff `fce` in Hz (SW1 UP
        // only; MID: = `eff`, left out to fit 128 chars), K5 LFO shape tri|stp|off, depth, rate Hz;
        // `ps - lfo -` on SW1 DOWN), mute, cpu. SW3 DOWN (FM LAB): UP `LAB 2OP-HARM|DISH|FREE r<ratio> hol|sine|fb
        // fc<index fc Hz> b<feedback> (knob, K4 g 1, before the caps) u<u_eff now>` + ` ps g.. dec|rise T.. lfo .. d<knob depth> ><x swell fade> ..Hz`, MID `LAB SPEC bank n/9 NAME nu<applied> k2<knob>
        // g<envelope> dec|rise T<s> d<depth>` (no fc / lfo part), DOWN `LAB ADD-<zone> <K2 name> <knob>
        // > <with LFO> <K3 name> <knob value> fc ..` (names h harmonics / I index rad / u unison / d stretch; UNISON:
        // `u <knob> bt <beat Hz> prop|hz|off`); + the UP `ps` / `lfo` part (fce = the applied index fc; DOWN)
        const Explorer::DiagSnap& d = diag_xsnap;
        float cpu_pk = g_cpu.GetMaxCpuLoad();
        if (diag_cpu_last > cpu_pk) cpu_pk = diag_cpu_last;
        const unsigned cpu = (unsigned)(cpu_pk * 100.f + 0.5f);
        const char* zn = Explorer::ZoneName(d.model, d.zone);
        char env[128];
        if (d.loop)                                           // retired LOOPING variant: generic K4 / K5 envelope
          snprintf(env, sizeof(env), " %s e" D2F " %s d" D2F " %s", "loop", D2V(d.env),
                   d.dir > 0 ? "up" : d.dir < 0 ? "down" : "off", D2V(d.d),
                   d.tgt == 2 ? "K2" : d.tgt == 3 ? "K3" : "-");
        else if (d.specz >= 0)                                // FM LAB MID SPECTRAL: K4 pluck <-> swell on nu (g, side, T s, depth)
          snprintf(env, sizeof(env), " g" D2F " %s T" D2F " d" D2F, D2V(d.env), d.dir < 0 ? "dec" : "rise", D2V(d.T), D2V(d.se));
        else if (d.labz >= 0)                                 // FM LAB UP: pluck <-> swell (g, side, T s), LFO depth > x fade
          snprintf(env, sizeof(env), " ps g" D2F " %s T" D2F " lfo %s d" D2F " >" D2F " " D2F "Hz", D2V(d.env),
                   d.dir < 0 ? "dec" : "rise", D2V(d.T),
                   d.lfo == 1 ? "tri" : d.lfo == 2 ? "stp" : "off", D2V(d.lfod), D2V(d.llfd), D2V(d.lfohz));
        else if (d.sw1 == 0 || d.addz >= 0)                   // SW1 UP / FM LAB DOWN: K4 pluck <-> swell (g, side, T s, fc_eff Hz)
          snprintf(env, sizeof(env), " ps g" D2F " %s T" D2F " fce %d lfo %s d" D2F " " D2F "Hz", D2V(d.env),
                   d.dir < 0 ? "dec" : "rise", D2V(d.T), (int)(d.fce + 0.5f),
                   d.lfo == 1 ? "tri" : d.lfo == 2 ? "stp" : "off", D2V(d.lfod), D2V(d.lfohz));
        else if (d.sw1 == 1)                                  // SW3 MID / DOWN, SW1 MID: the same without fce (= eff)
          snprintf(env, sizeof(env), " ps g" D2F " %s T" D2F " lfo %s d" D2F " " D2F "Hz", D2V(d.env),
                   d.dir < 0 ? "dec" : "rise", D2V(d.T),
                   d.lfo == 1 ? "tri" : d.lfo == 2 ? "stp" : "off", D2V(d.lfod), D2V(d.lfohz));
        else                                                  // SW3 MID / DOWN, SW1 DOWN: K4 / K5 inert
          snprintf(env, sizeof(env), " ps - lfo -");
        if (d.addz >= 0 && Explorer::AddK2Name(d.addz)[0] == 'u')   // FM LAB DOWN unison: u, beat Hz + mode, K3
          hw.seed.PrintLine("LAB %s u" D2F " bt" D2F " %s %s" D2F " fc %d%s mute %d cpu %u%%",
                            Explorer::AddZoneName(d.addz), D2V(d.a), D2V(d.b),
                            d.bm == 1 ? "prop" : d.bm == 2 ? "hz" : "off", Explorer::AddK3Name(d.addz), D2V(d.c3),
                            (int)(d.fc + 0.5f), env, d.mute ? 1 : 0, cpu);
        else if (d.addz >= 0)                                 // FM LAB DOWN: zone, K2 knob > with LFO, K3 knob, index fc
          hw.seed.PrintLine("LAB %s %s" D2F " >" D2F " %s" D2F " fc %d%s mute %d cpu %u%%",
                            Explorer::AddZoneName(d.addz), Explorer::AddK2Name(d.addz), D2V(d.a), D2V(d.b),
                            Explorer::AddK3Name(d.addz), D2V(d.c3), (int)(d.fc + 0.5f), env, d.mute ? 1 : 0, cpu);
        else if (d.specz >= 0)                                // FM LAB MID: K1 bank, applied nu, K2 knob + the envelope part
          hw.seed.PrintLine("LAB SPEC bank %d/9 %s nu" D2F " k2" D2F "%s mute %d cpu %u%%",
                            (int)d.sn + 1, Explorer::SpecBankName((int)d.sn), D2V(d.a), D2V(d.b), env, d.mute ? 1 : 0, cpu);
        else if (d.labz >= 0)                                 // FM LAB UP: mode, ratio, K3 side, index fc, feedback, u_eff
          hw.seed.PrintLine("LAB %s r" D2F " %s fc %d b" D2F " u" D2F "%s mute %d cpu %u%%",
                            Explorer::LabZoneName(d.labz), D2V(d.a), Explorer::LabSideName(d.lside), (int)(d.b + 0.5f),
                            D2V(d.lb), D2V(d.lue), env, d.mute ? 1 : 0, cpu);
        else if (d.model == Explorer::kFM)                    // A = ratio r, B = feedback b
          hw.seed.PrintLine("FM r" D2F " b" D2F " fc %d%s mute %d cpu %u%%",
                            D2V(d.a), D2V(d.b), (int)(d.fc + 0.5f), env, d.mute ? 1 : 0, cpu);
        else if (d.model == Explorer::kXFM)                   // A = partner n, B = index I
          hw.seed.PrintLine("XBAND-FM n%d I" D2F " fc %d%s mute %d cpu %u%%",
                            (int)(d.a + 0.5f), D2V(d.b), (int)(d.fc + 0.5f), env, d.mute ? 1 : 0, cpu);
        else if (d.model == Explorer::kWT)                    // MID tables: zone, morph m (B), frame position (A),
          hw.seed.PrintLine("WT/%s m" D2F " pos" D2F " dmp %d lp %d eff %d%s mute %d cpu %u%%",   // damp fc, LP fc
                            zn, D2V(d.b), D2V(d.a), (int)(d.dfc + 0.5f), (int)(d.fc + 0.5f),   // knob / effective
                            (int)(d.lfe + 0.5f), env, d.mute ? 1 : 0, cpu);
        else if (d.sw1 == 1)                                  // parked bit table: A = waveform, B = K3, damp / LP fc
          hw.seed.PrintLine("%s%s%s A" D2F " B" D2F " dmp %d lp %d eff %d%s mute %d cpu %u%%",
                            Explorer::ModelName(d.model), zn[0] ? "/" : "", zn, D2SV(d.a), D2SV(d.b),
                            (int)(d.dfc + 0.5f), (int)(d.fc + 0.5f), (int)(d.lfe + 0.5f), env, d.mute ? 1 : 0, cpu);
        else                                                  // DOWN: no dampener
          hw.seed.PrintLine("%s A" D2F " B" D2F "%s mute %d cpu %u%%",
                            Explorer::ModelName(d.model), D2SV(d.a), D2SV(d.b), env, d.mute ? 1 : 0, cpu);
        diag_stage = 0;
      } else if ((g_active == NL_MODE_EXPLORER && explorer.DiagTake(diag_xsnap)) ||
                 (g_active == NL_MODE_EXPLORER_LAB && explorer_lab.DiagTake(diag_xsnap))) {
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
        // model from (SW1 pair, sign of K1); h = |2 k1 - 1|. K5 env + K2 FM env (t2, e) in the second half.
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
