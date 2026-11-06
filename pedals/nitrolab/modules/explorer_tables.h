#pragma once
//
// explorer_tables.h — the SW3 MID SW1 MID AUDITION BANK (EXP_TB_LEGACY = false; 2026-11-06): 15 data-driven table
// presets, K1 = 15 equal stepped zones in this list order (EXP_TB_ZONE_XFADE_MS crossfade on a change). ONE place:
// a new preset / journey is a new stage list + one EXP_TB_PRESETS entry, no code. Built at Init by BuildBank
// (explorer.h) through the legacy table pipeline; see explorer.h TABLES for the render and the K2 / K5 law per kind.
//
// PRESET = name, kind, frame count, key stages, level rules (re-phase, RMS match, int16 scale), LOFI mode.
//   kind   LFO      K2 = morph through the frames, K5 = the K5 LFO on the morph (+- EXP_LFO_MORPH_SWING), as legacy
//          JOURNEY  K2 = morph start S, K5 = direction + constant speed of the travel from S (EXP_TB_JRN_*, explorer.h
//                   JourneyUpdate): CCW backward, noon still, CW forward; restarts at S on every onset
//          ANALOG   one frame; K2 = 2-pole LP resonance Q (EXP_TF_Q_MIN .. EXP_TF_Q_MAX, exponential), K5 = the K5 LFO
//                   on the LP cutoff (the SW3 MID FM placement: DampFmUpdate's octaves, explorer.h)
// STAGE  = position on the morph (0..1, first 0, last 1, ascending), generator + parameter p (or a harmonic list),
//          tonal gain (render-time factor on the table output, 1 = the frame at the preset's level).
//   Frame f of F sits at u = f / (F - 1). Between stage i and i + 1 (t = (u - pos_i) / (pos_i+1 - pos_i)):
//     same generator (and the same list) -> the parameter p = p_i + t (p_i+1 - p_i), generated directly (a sweep);
//     different generators -> per harmonic h: a_h = exp((1 - t) ln max(a_ih, F) + t ln max(a_i+1,h, F)) with each
//                             stage's magnitudes at RMS 1 and F = EXP_TB_JOURNEY_FLOOR_DB (-60 dB); <= F -> 0;
//                             sine phase (dB interpolation of the harmonic amplitudes, not a wave crossfade).
//     u exactly on a stage: that stage, generated directly. tone: linear in t (per frame, then linear between the two
//     frames at the morph position, per 8 kHz sample).
//   Then the preset's level rules (legacy pipeline): peak 1 -> re-phase (sine phase, positive; magnitudes kept) ->
//   RMS match to the plain saw + cap to the int16 headroom 32767 / scale -> 14 band-limited versions.
// LOFI (2026-11-06; replaces BLOW): per preset { foldover fo, bits N, no-interp, roll-off F_LO / F_HI } (laws:
//   explorer_constants.h EXP_TB_LOFI_*; read: explorer.h ShapeK<kWT>). All 0 / off (EXP_TB_LOFI_OFF) = the clean read;
//   EXP_TB_LOFI_ENABLE = false = every preset clean. The tables themselves are the same with or without lofi.
//   The lofi presets sit together at the CW end of K1: GLASS RASPY BREATH AIR.

#include "explorer_constants.h"

namespace explorer_k {

enum : uint8_t { kTbLfo = 0, kTbJourney = 1, kTbAnalog = 2 };              // preset kind
// Generators: spectral (harmonic amplitudes, sine phase) or time domain (DFT at EXP_TB_TD_SIZE, as legacy). Legacy
// recipes are reused 1:1 (explorer.h TbStageSpec maps them onto the legacy TbSpectral / TbTimeDomain cases).
enum : uint8_t {
  kTbgSaw = 0,     // spectral: a_h = h^-p, every h (p = 1: exactly 1 / h, the raw saw; p = EXP_TB_FUZZ_EXP: fuzz reed)
  kTbgOdd,         // spectral: a_h = h^-p, odd h only (p = 1: exactly 1 / h, the raw square)
  kTbgList,        // spectral: the harmonic list {h, a} (list / n)
  kTbgHarmonium,   // spectral: legacy SOFT B 1 (EXP_TB_HARMONIUM_*)
  kTbgOboe,        // spectral: legacy HARD A 2, nasal oboe reed (EXP_TB_W_OBOE + tail)
  kTbgBrass,       // spectral: legacy HARD A 3 formant brass, formant centre = p (legacy: EXP_TB_BRASS_CENTER 8)
  kTbgClip,        // spectral: legacy HARD B 3, clipper (EXP_TB_CLIP_*)
  kTbgPpgRes,      // spectral: legacy HARD A 1, PPG resonant (EXP_TB_PPGRES_*)
  kTbgPpgFb,       // spectral: legacy HARD B 4, PPG feedback (saw without harmonics 1 .. EXP_TB_PPGFB_SKIP)
  kTbgVocal,       // spectral: legacy zone 5 vocal at position p (0 a .. 1 u)
  kTbgPwm,         // time: legacy zone 4 PWM at position p (width 0.5 (1 - p)^EXP_TB_PWM_CURVE; p = 1: silence)
  kTbgSync,        // time: hard-sync saw 2 frac(p phi) - 1 (ratio p; legacy HARD B 1 at p = EXP_TB_SYNCSAW_R)
  kTbgPpgBit,      // time: legacy PPG CLASSIC 3, bit pattern
  kTbgLoRes,       // time: legacy SOFT B 4, stepped lo-res sine (EXP_TB_LORES_STEPS)
  kTbgCount
};

struct ExpTbStage {
  float pos;               // position on the morph 0..1
  uint8_t gen;             // generator
  float p;                 // generator parameter (see the enum; 0 = unused)
  const ExpTbHarm* list;   // kTbgList: harmonic list
  int n;                   //           its length
  float tone;              // tonal gain at this stage
};
struct ExpTbLofi {         // LOFI mode of a preset (EXP_TB_LOFI_*; all 0 / false = clean)
  uint8_t fo;              // FOLDOVER: versions brighter than the anti-fold pick (0 = off; clamped to the brightest)
  uint8_t bits;            // BITS: the lofi read rounded to 2^(bits - 1) steps of the full scale (0 = off; 1..16)
  bool nn;                 // NO INTERPOLATION: nearest-sample read (false = linear)
  float flo, fhi;          // ROLL-OFF (Hz): lofi weight 1 at / below flo, 0 at / above fhi, log-linear in between
};
struct ExpTbPreset {
  const char* name;
  uint8_t kind;            // kTbLfo / kTbJourney / kTbAnalog
  int frames;              // frames built (ANALOG: 1)
  const ExpTbStage* st;    // key stages
  int nst;
  bool rephase;            // legacy re-phase rule (sine phase, positive; magnitudes kept)
  bool rms;                // legacy RMS match to the plain saw (+ cap to the int16 headroom)
  float scale;             // int16 storage scale
  ExpTbLofi lofi;          // LOFI mode (EXP_TB_LOFI_OFF = clean)
};
constexpr ExpTbLofi EXP_TB_LOFI_OFF = { 0, 0, false, EXP_TB_LOFI_F_LO, EXP_TB_LOFI_F_HI };
#define EXP_TB_L(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))
#define EXP_TB_S(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))

// --- new harmonic lists (the legacy ones, EXP_TB_W_*, are in explorer_constants.h) ---
constexpr ExpTbHarm EXP_TB_W_GLASS_BELL[] = { {1, 1.f}, {7, .12f}, {9, .15f}, {11, .2f}, {13, .12f}, {17, .18f}, {18, .15f} };
constexpr ExpTbHarm EXP_TB_W_GLASS_BRIT[] = { {1, .45f}, {7, .3f}, {9, .35f}, {11, .5f}, {12, .3f}, {13, .4f}, {17, .5f},
                                              {18, .45f}, {23, .35f}, {24, .3f}, {29, .25f}, {31, .2f} };
constexpr ExpTbHarm EXP_TB_W_MARIMBA[]    = { {1, 1.f}, {4, .35f}, {10, .12f} };
constexpr ExpTbHarm EXP_TB_W_TINESTACK[]  = { {1, .5f}, {3, .12f}, {7, .6f}, {11, .5f}, {13, .35f}, {17, .45f}, {19, .3f},
                                              {23, .35f}, {29, .25f}, {31, .2f}, {37, .15f} };

// --- key stages per preset: { pos, generator, p, list, n, tone } ---
constexpr ExpTbStage EXP_TB_ST_SAW[]    = { { 0.f, kTbgSaw, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_PULSE[]  = { { 0.f, kTbgOdd, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_PWM[]    = { { 0.f, kTbgPwm, 0.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgPwm, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_SYNC[]   = { { 0.f, kTbgSync, 1.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgSync, 6.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_VOCAL[]  = { { 0.f, kTbgVocal, 0.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgVocal, 1.f, nullptr, 0, 1.f } };
// GLASS (LOFI): the sparse high glass partials (bell list, held to .5, then dB-interpolated to the brilliant list).
constexpr ExpTbStage EXP_TB_ST_GLASS[]  = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GLASS_BELL), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GLASS_BELL), 1.f },
                                            { 1.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GLASS_BRIT), 1.f } };
constexpr ExpTbStage EXP_TB_ST_WOOD[]   = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_CLARINET), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_MARIMBA), 1.f },
                                            { 1.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_BASSBELL), 1.f } };
constexpr ExpTbStage EXP_TB_ST_REED[]   = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_CLARINET), 1.f },
                                            { .5f,  kTbgOboe, 0.f, nullptr, 0, 1.f },
                                            { 1.f,  kTbgSaw,  EXP_TB_FUZZ_EXP, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_BRASS[]  = { { 0.f,  kTbgHarmonium, 0.f,  nullptr, 0, 1.f },
                                            { .2f,  kTbgBrass,     4.f,  nullptr, 0, 1.f },
                                            { .8f,  kTbgBrass,     12.f, nullptr, 0, 1.f },
                                            { 1.f,  kTbgClip,      0.f,  nullptr, 0, 1.f } };
// RASPY (LOFI): saw -> fuzz reed (exponent sweep) -> clipper.
constexpr ExpTbStage EXP_TB_ST_RASPY[]  = { { 0.f,  kTbgSaw,  1.f,             nullptr, 0, 1.f },
                                            { .5f,  kTbgSaw,  EXP_TB_FUZZ_EXP, nullptr, 0, 1.f },
                                            { 1.f,  kTbgClip, 0.f,             nullptr, 0, 1.f } };
// BREATH (LOFI): hollow tube / flute (ghost fifths: odd h 3 / 5, h1 weak) at every stage - the same frame all along
// (its journey only moved the former blow amount; with BLOW gone K2 / K5 travel through identical frames).
constexpr ExpTbStage EXP_TB_ST_BREATH[] = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f },
                                            { 1.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f } };
// AIR (LOFI): vowel formants (the VOCAL recipe at its 110 Hz reference, p 0 a .. 1 u), drifting oo -> ah -> eh -> oo
// (the recipe's parameter sweep: u -> a passes o / i / e, e -> u passes i / o).
constexpr ExpTbStage EXP_TB_ST_AIR[]    = { { 0.f,       kTbgVocal, 1.f,   nullptr, 0, 1.f },
                                            { 1.f / 3.f, kTbgVocal, 0.f,   nullptr, 0, 1.f },
                                            { 2.f / 3.f, kTbgVocal, .25f,  nullptr, 0, 1.f },
                                            { 1.f,       kTbgVocal, 1.f,   nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_ORGAN[]  = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_DRAWBAR), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_MIXTURE), 1.f },
                                            { 1.f,  kTbgHarmonium, 0.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_PPG[]    = { { 0.f,         kTbgPpgRes, 0.f, nullptr, 0, 1.f },
                                            { 1.f / 3.f,   kTbgPpgFb,  0.f, nullptr, 0, 1.f },
                                            { 2.f / 3.f,   kTbgPpgBit, 0.f, nullptr, 0, 1.f },
                                            { 1.f,         kTbgLoRes,  0.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_METAL[]  = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_BELL), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_BASSBELL), 1.f },
                                            { 1.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_TINESTACK), 1.f } };

// --- the bank, K1 order (CCW -> CW); the LOFI presets grouped at the CW end (2026-11-06) ---
// level rules: PWM / VOCAL keep their legacy zones' rules (peak 1, no re-phase, scale EXP_TB_I16_SCALE: PWM falls to
// silence at the narrow end) -> their frames are the legacy zone 4 / 5 frames; every other preset (AIR too: its vowel
// frames are RMS-matched like the other journeys): re-phase + RMS match to the plain saw, scale EXP_TB_I16_SCALE_WIDE
// (headroom 5.0, as legacy zones 3 / 10).
// LOFI { foldover, bits, no-interp, F_LO Hz, F_HI Hz } - starting values 2026-11-06, tuned by ear.
constexpr ExpTbPreset EXP_TB_PRESETS[EXP_TB_BANK_N] = {
  { "SAW",    kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_SAW),    true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "PULSE",  kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_PULSE),  true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "PWM",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PWM),    false, false, EXP_TB_I16_SCALE,      EXP_TB_LOFI_OFF },
  { "SYNC",   kTbLfo,    16, EXP_TB_S(EXP_TB_ST_SYNC),   true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "VOCAL",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_VOCAL),  false, false, EXP_TB_I16_SCALE,      EXP_TB_LOFI_OFF },
  { "WOOD",   kTbJourney,16, EXP_TB_S(EXP_TB_ST_WOOD),   true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "REED",   kTbJourney,16, EXP_TB_S(EXP_TB_ST_REED),   true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "BRASS",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_BRASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "ORGAN",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_ORGAN),  true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "PPG",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PPG),    true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "METAL",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_METAL),  true,  true,  EXP_TB_I16_SCALE_WIDE, EXP_TB_LOFI_OFF },
  { "GLASS",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_GLASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, { 2, 8, true,  400.f, 2000.f } },
  { "RASPY",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_RASPY),  true,  true,  EXP_TB_I16_SCALE_WIDE, { 2, 6, true,  400.f, 2000.f } },
  { "BREATH", kTbJourney,16, EXP_TB_S(EXP_TB_ST_BREATH), true,  true,  EXP_TB_I16_SCALE_WIDE, { 1, 6, false, 400.f, 2000.f } },
  { "AIR",    kTbJourney,16, EXP_TB_S(EXP_TB_ST_AIR),    true,  true,  EXP_TB_I16_SCALE_WIDE, { 3, 5, true,  300.f, 1500.f } },
};
#undef EXP_TB_L
#undef EXP_TB_S

// Packed frame count of the bank and of the selected bank (explorer_tb's size).
constexpr int ExpTbBankFrames(int i = 0) { return (i >= EXP_TB_BANK_N) ? 0 : EXP_TB_PRESETS[i].frames + ExpTbBankFrames(i + 1); }
constexpr int EXP_TB_BANK_FRAMES  = ExpTbBankFrames();
constexpr int EXP_TB_TOTAL_FRAMES = EXP_TB_LEGACY ? EXP_TB_LEGACY_TOTAL_FRAMES : EXP_TB_BANK_FRAMES;
// Data checks: stages start at 0, end at 1, ascend; ANALOG = 1 frame / 1 stage; generators / lists valid; lofi: bits
// 0..16, 0 < F_LO < F_HI.
constexpr bool ExpTbPresetOk(const ExpTbPreset& P) {
  if (P.nst < 1 || P.frames < 1 || P.st[0].pos != 0.f) return false;
  if (P.nst > 1 && P.st[P.nst - 1].pos != 1.f) return false;
  if (P.frames > 1 && P.nst < 2) return false;
  if (P.kind == kTbAnalog && (P.frames != 1 || P.nst != 1)) return false;
  if (P.kind != kTbAnalog && P.frames < 2) return false;
  for (int i = 0; i < P.nst; i++) {
    if (P.st[i].gen >= kTbgCount) return false;
    if (P.st[i].gen == kTbgList && (P.st[i].list == nullptr || P.st[i].n < 1)) return false;
    if (i > 0 && !(P.st[i].pos > P.st[i - 1].pos)) return false;
  }
  if (P.lofi.bits > 16 || !(P.lofi.flo > 0.f) || !(P.lofi.fhi > P.lofi.flo)) return false;
  return true;
}
constexpr bool ExpTbBankOk(int i = 0) { return (i >= EXP_TB_BANK_N) ? true : ExpTbPresetOk(EXP_TB_PRESETS[i]) && ExpTbBankOk(i + 1); }
static_assert(ExpTbBankOk(), "EXP_TB_PRESETS: invalid stage data (positions 0 .. 1 ascending, ANALOG 1 frame / stage, lists, lofi)");
static_assert(EXP_TB_BANK_N < 64, "VarKey holds the zone in 6 bits");
// LOFI on for a preset: the global switch, and any lever set (foldover / bits / no-interp).
constexpr bool ExpTbLofiOn(const ExpTbLofi& L) { return EXP_TB_LOFI_ENABLE && (L.fo > 0 || L.bits > 0 || L.nn); }
// Lofi slot of each preset (index into the per-band weights, explorer.h explorer_tb_rt.lw; -1 = clean) and the count.
struct ExpTbLofiMap { int8_t slot[EXP_TB_BANK_N]; int n; };
constexpr ExpTbLofiMap ExpTbLofiMapMake() {
  ExpTbLofiMap m{};
  m.n = 0;
  for (int i = 0; i < EXP_TB_BANK_N; i++) m.slot[i] = ExpTbLofiOn(EXP_TB_PRESETS[i].lofi) ? (int8_t)(m.n++) : (int8_t)-1;
  return m;
}
constexpr ExpTbLofiMap EXP_TB_LOFI_MAP = ExpTbLofiMapMake();
constexpr int EXP_TB_LOFI_N = (EXP_TB_LOFI_MAP.n > 0) ? EXP_TB_LOFI_MAP.n : 1;   // weight array size (min 1)

}  // namespace explorer_k
