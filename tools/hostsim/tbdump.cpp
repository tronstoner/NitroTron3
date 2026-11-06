// tbdump — the SW1 MID audition bank's wavetables, exactly as the pedal builds them, as JSON on stdout
// (for the devtool's preset editor: tools/devtool/devtool.py GET /api/tables).
//
// Compiles explorer.h unmodified through the hostsim shell headers (the same include layout and flags as
// armsim), runs Explorer(false).Init(48000) - the SW3 MID instance's Init, which builds explorer_sin_tab and
// then the bank (BuildBank) into explorer_tb - and reads explorer_tb back. Nothing is re-implemented here: the
// packed frame offsets follow Init's rule (own frames packed in list order; share >= 0 = that preset's offset).
//
// Output (one line of JSON):
//   {"hash": "<FNV-1a 64 of the whole explorer_tb array, hex>", "size": 128, "version": 13, "harmonics": 127,
//    "presets": [{"index": 0, "name": "SAW", "kind": "ANALOG"|"LFO"|"LOFI", "frames": F, "scale": S,
//                 "share": -1, "key_stages": [...], "waves": [[128 floats] x F]}, ...]}
//   waves[f]   frame f's full-band version (version 13 of 14 = harmonics 1..127), as the engine reads it:
//              int16 * (1 / scale) (Derive's p1 = 1 / TbScale), i.e. the preset's int16 storage scale undone - all
//              presets on one amplitude axis (RMS-matched presets at the plain saw's RMS, PWM / VOCAL at peak 1).
//              Before the per-frame tonal gain (explorer_tb_tone). 256 -> 128 points by plain decimation
//              (samples 0, 2, 4, .., 254). 4 decimals.
//   key_stages the preset's key stages as frame positions: stage pos x (F - 1) (may be fractional, e.g. 7.5 =
//              between frames 7 and 8; K2 discrete snaps to these positions). ANALOG: [0].
//   share      >= 0: the preset reads that preset's frames (waves = that preset's frames, repeated here).

#include "nl_shell.h"

#include <cstdio>

uint32_t daisy::System::now_ms = 0;

static Explorer exp_mid(false);     // SW3 MIDDLE (its Init builds the shared tables)

int main() {
  using namespace explorer_k;
  static_assert(!EXP_TB_LEGACY, "tbdump reads the audition bank (EXP_TB_LEGACY = false)");
  exp_mid.Init(nlshell::kSR);

  uint64_t hsh = 1469598103934665603ULL;
  {
    const uint8_t* p = (const uint8_t*)&explorer_tb[0][0][0];
    const size_t n = sizeof(explorer_tb);
    for (size_t i = 0; i < n; i++) { hsh ^= p[i]; hsh *= 1099511628211ULL; }
  }
  int off[EXP_TB_BANK_N];
  {
    int o = 0;
    for (int z = 0; z < EXP_TB_BANK_N; z++) {
      const int sh = EXP_TB_PRESETS[z].share;
      if (sh >= 0) { off[z] = off[sh]; continue; }
      off[z] = o; o += EXP_TB_PRESETS[z].frames;
    }
    if (o != EXP_TB_TOTAL_FRAMES) { fprintf(stderr, "tbdump: packed %d frames, explorer_tb has %d\n", o, EXP_TB_TOTAL_FRAMES); return 1; }
  }
  constexpr int V = EXP_TB_LEVELS - 1;            // full band: harmonics 1..EXP_TB_LEVEL_H[V] (127)
  constexpr int D = 2;                            // decimation 256 -> 128
  printf("{\"hash\":\"%016llx\",\"size\":%d,\"version\":%d,\"harmonics\":%d,\"presets\":[",
         (unsigned long long)hsh, EXP_TB_SIZE / D, V, EXP_TB_LEVEL_H[V]);
  for (int z = 0; z < EXP_TB_BANK_N; z++) {
    const ExpTbPreset& P = EXP_TB_PRESETS[z];
    const char* kind = (P.kind == kTbAnalog) ? "ANALOG" : (P.kind == kTbLofi) ? "LOFI" : "LFO";
    printf("%s{\"index\":%d,\"name\":\"%s\",\"kind\":\"%s\",\"frames\":%d,\"scale\":%g,\"share\":%d,\"key_stages\":[",
           z ? "," : "", z, P.name, kind, P.frames, (double)P.scale, P.share);
    for (int i = 0; i < P.nst; i++) printf("%s%g", i ? "," : "", (double)(P.st[i].pos * (float)(P.frames - 1)));
    printf("],\"waves\":[");
    const float inv = 1.f / P.scale;
    for (int f = 0; f < P.frames; f++) {
      const int16_t* w = explorer_tb[off[z] + f][V];
      printf("%s[", f ? "," : "");
      for (int i = 0; i < EXP_TB_SIZE; i += D) {
        float v = (float)w[i] * inv;
        if (v > -0.00005f && v < 0.00005f) v = 0.f;   // no "-0.0000"
        printf("%s%.4f", i ? "," : "", (double)v);
      }
      printf("]");
    }
    printf("]}");
  }
  printf("]}\n");
  return 0;
}
