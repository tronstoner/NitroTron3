#pragma once
//
// explorer_tables.h — the SW3 MID SW1 MID AUDITION BANK (EXP_TB_LEGACY = false; 2026-11-06): 16 data-driven table
// presets, K1 = 16 equal stepped zones in this list order (EXP_TB_ZONE_XFADE_MS crossfade on a change). ONE place:
// a new preset / journey is a new stage list + one EXP_TB_PRESETS entry, no code. Built at Init by BuildBank
// (explorer.h) through the legacy table pipeline; see explorer.h TABLES for the render and the K2 / K5 law per kind.
//
// PRESET = name, kind, frame count, key stages, level rules (re-phase, RMS match, int16 scale), share.
//   kind   LFO      K2 = morph through the frames, K5 = the K5 LFO on the morph (+- EXP_LFO_MORPH_SWING), as legacy
//          JOURNEY  K2 = morph start S, K5 = direction + constant speed of the travel from S (EXP_TB_JRN_*, explorer.h
//                   JourneyUpdate): CCW backward, noon still, CW forward; restarts at S on every onset
//          ANALOG   one frame; K2 = 2-pole LP resonance Q (EXP_TF_Q_MIN .. EXP_TF_Q_MAX, exponential), K5 = the K5 LFO
//                   on the LP cutoff (the SW3 MID FM placement: DampFmUpdate's octaves, explorer.h)
//          LOFI     K2 = static morph position (no LFO / journey, nothing restarts), K5 = no job; the four LOFI stages
//                   are set by their constants (explorer_constants.h EXP_TB_LOFI_*, explorer.h TABLES LOFI)
//   (every kind but ANALOG: K2 stepped through the key stages with EXP_TB_K2_DISCRETE / EXP_TB_LOFI_K2_DISCRETE)
//   share  >= 0: the preset reads that (earlier) preset's frames instead of building its own; -1 = own frames.
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
// LOFI (2026-11-06; replaces the per-band LOFI mode of 57f7bfc and, later the same day, the four LOFI BITS / RATE / TABLE
//   / TBITS presets): ONE preset at the CW end of K1 (K1 16), on LOFI SPECTRA (16 key stages CCW sparse -> CW dense, 31
//   frames: each stage one frame, one in-between frame between neighbours). EXP_TB_LOFI_ENABLE = false: it reads clean
//   (all four stages bypassed).

#include "explorer_constants.h"

namespace explorer_k {

enum : uint8_t { kTbLfo = 0, kTbJourney = 1, kTbAnalog = 2, kTbLofi = 3 };  // preset kind
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
struct ExpTbPreset {
  const char* name;
  uint8_t kind;            // kTbLfo / kTbJourney / kTbAnalog / kTbLofi
  int frames;              // frames built (ANALOG: 1)
  const ExpTbStage* st;    // key stages
  int nst;
  bool rephase;            // legacy re-phase rule (sine phase, positive; magnitudes kept)
  bool rms;                // legacy RMS match to the plain saw (+ cap to the int16 headroom)
  float scale;             // int16 storage scale
  int share;               // -1 = own frames; >= 0 = read that earlier preset's frames (same frames / level rules)
};
#define EXP_TB_L(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))
#define EXP_TB_S(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))

// --- new harmonic lists (the legacy ones, EXP_TB_W_*, are in explorer_constants.h) ---
constexpr ExpTbHarm EXP_TB_W_GLASS_BELL[] = { {1, 1.f}, {7, .12f}, {9, .15f}, {11, .2f}, {13, .12f}, {17, .18f}, {18, .15f} };
constexpr ExpTbHarm EXP_TB_W_GLASS_BRIT[] = { {1, .45f}, {7, .3f}, {9, .35f}, {11, .5f}, {12, .3f}, {13, .4f}, {17, .5f},
                                              {18, .45f}, {23, .35f}, {24, .3f}, {29, .25f}, {31, .2f} };
constexpr ExpTbHarm EXP_TB_W_MARIMBA[]    = { {1, 1.f}, {4, .35f}, {10, .12f} };
constexpr ExpTbHarm EXP_TB_W_TINESTACK[]  = { {1, .5f}, {3, .12f}, {7, .6f}, {11, .5f}, {13, .35f}, {17, .45f}, {19, .3f},
                                              {23, .35f}, {29, .25f}, {31, .2f}, {37, .15f} };
// LOFI SPECTRA sparse stages (equal amplitudes 1)
constexpr ExpTbHarm EXP_TB_W_SINE[]       = { {1, 1.f} };
constexpr ExpTbHarm EXP_TB_W_ODD135[]     = { {1, 1.f}, {3, 1.f}, {5, 1.f} };
constexpr ExpTbHarm EXP_TB_W_EVEN124[]    = { {1, 1.f}, {2, 1.f}, {4, 1.f} };
constexpr ExpTbHarm EXP_TB_W_GAPS[]       = { {1, 1.f}, {7, 1.f}, {11, 1.f} };
constexpr ExpTbHarm EXP_TB_W_BEATING[]    = { {1, 1.f}, {11, 1.f}, {12, 1.f} };

// --- key stages per preset: { pos, generator, p, list, n, tone } ---
constexpr ExpTbStage EXP_TB_ST_SAW[]    = { { 0.f, kTbgSaw, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_PULSE[]  = { { 0.f, kTbgOdd, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_PWM[]    = { { 0.f, kTbgPwm, 0.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgPwm, 1.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_SYNC[]   = { { 0.f, kTbgSync, 1.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgSync, 6.f, nullptr, 0, 1.f } };
constexpr ExpTbStage EXP_TB_ST_VOCAL[]  = { { 0.f, kTbgVocal, 0.f, nullptr, 0, 1.f },
                                            { 1.f, kTbgVocal, 1.f, nullptr, 0, 1.f } };
// GLASS: the sparse high glass partials (bell list, held to .5, then dB-interpolated to the brilliant list).
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
// RASPY: saw -> fuzz reed (exponent sweep) -> clipper.
constexpr ExpTbStage EXP_TB_ST_RASPY[]  = { { 0.f,  kTbgSaw,  1.f,             nullptr, 0, 1.f },
                                            { .5f,  kTbgSaw,  EXP_TB_FUZZ_EXP, nullptr, 0, 1.f },
                                            { 1.f,  kTbgClip, 0.f,             nullptr, 0, 1.f } };
// BREATH: hollow tube / flute (ghost fifths: odd h 3 / 5, h1 weak) at every stage - the same frame all along
// (its journey only moved the former blow amount; with BLOW gone K2 / K5 travel through identical frames).
constexpr ExpTbStage EXP_TB_ST_BREATH[] = { { 0.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f },
                                            { .5f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f },
                                            { 1.f,  kTbgList, 0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f } };
// AIR: vowel formants (the VOCAL recipe at its 110 Hz reference, p 0 a .. 1 u), drifting oo -> ah -> eh -> oo
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

// LOFI SPECTRA (the LOFI slot, 2026-11-06): 16 key stages at i / 15, CCW sparse -> CW dense; 31 frames = each
// stage one frame + one in-between frame (all neighbours differ in generator / list -> per-harmonic dB interpolation).
// Existing recipes: ghost fifths / glass bell / drawbar / clarinet lists, VOCAL p 0 ('a'), the nasal oboe reed (legacy
// HARD A 2), formant brass at its legacy centre EXP_TB_BRASS_CENTER, pulse 50 % = the square (odd 1 / h, as PULSE),
// saw 1 / h, sync saw at the legacy ratio EXP_TB_SYNCSAW_R, clipper.
constexpr ExpTbStage EXP_TB_ST_LOFISPEC[] = {
  {  0.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_SINE), 1.f },         //  1 sine
  {  1.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_ODD135), 1.f },       //  2 odd h1 h3 h5
  {  2.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_EVEN124), 1.f },      //  3 even h1 h2 h4
  {  3.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_GAPS), 1.f },         //  4 gaps h1 h7 h11
  {  4.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_BEATING), 1.f },      //  5 beating h1 h11 h12
  {  5.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_GHOST5), 1.f },       //  6 ghost fifths
  {  6.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_GLASS_BELL), 1.f },   //  7 glass bell
  {  7.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_DRAWBAR), 1.f },      //  8 drawbar organ
  {  8.f / 15.f, kTbgList,  0.f, EXP_TB_L(EXP_TB_W_CLARINET), 1.f },     //  9 hollow clarinet
  {  9.f / 15.f, kTbgVocal, 0.f, nullptr, 0, 1.f },                      // 10 vowel ah
  { 10.f / 15.f, kTbgOboe,  0.f, nullptr, 0, 1.f },                      // 11 reed (nasal oboe reed)
  { 11.f / 15.f, kTbgBrass, EXP_TB_BRASS_CENTER, nullptr, 0, 1.f },      // 12 brass
  { 12.f / 15.f, kTbgOdd,   1.f, nullptr, 0, 1.f },                      // 13 pulse 50 % (square)
  { 13.f / 15.f, kTbgSaw,   1.f, nullptr, 0, 1.f },                      // 14 saw
  { 14.f / 15.f, kTbgSync,  EXP_TB_SYNCSAW_R, nullptr, 0, 1.f },         // 15 sync saw
  { 15.f / 15.f, kTbgClip,  0.f, nullptr, 0, 1.f } };                    // 16 clipper

// --- the bank, K1 order (CCW -> CW); the LOFI slot at the CW end (2026-11-06) ---
// level rules: PWM / VOCAL keep their legacy zones' rules (peak 1, no re-phase, scale EXP_TB_I16_SCALE: PWM falls to
// silence at the narrow end) -> their frames are the legacy zone 4 / 5 frames; every other preset (AIR and LOFI SPECTRA
// too): re-phase + RMS match to the plain saw, scale EXP_TB_I16_SCALE_WIDE (headroom 5.0, as legacy zones 3 / 10).
constexpr ExpTbPreset EXP_TB_PRESETS[EXP_TB_BANK_N] = {
  { "SAW",    kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_SAW),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "PULSE",  kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_PULSE),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "PWM",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PWM),    false, false, EXP_TB_I16_SCALE,      -1 },
  { "SYNC",   kTbLfo,    16, EXP_TB_S(EXP_TB_ST_SYNC),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "VOCAL",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_VOCAL),  false, false, EXP_TB_I16_SCALE,      -1 },
  { "WOOD",   kTbJourney,16, EXP_TB_S(EXP_TB_ST_WOOD),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "REED",   kTbJourney,16, EXP_TB_S(EXP_TB_ST_REED),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "BRASS",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_BRASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "ORGAN",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_ORGAN),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "PPG",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PPG),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "METAL",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_METAL),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "GLASS",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_GLASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "RASPY",  kTbJourney,16, EXP_TB_S(EXP_TB_ST_RASPY),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "BREATH", kTbJourney,16, EXP_TB_S(EXP_TB_ST_BREATH), true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "AIR",    kTbJourney,16, EXP_TB_S(EXP_TB_ST_AIR),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1 },
  { "LOFI",   kTbLofi,   31, EXP_TB_S(EXP_TB_ST_LOFISPEC), true, true, EXP_TB_I16_SCALE_WIDE, -1 },
};
#undef EXP_TB_L
#undef EXP_TB_S

// Packed frame count of the bank (shared presets add none) and of the selected bank (explorer_tb's size).
constexpr int ExpTbBankFrames(int i = 0) {
  return (i >= EXP_TB_BANK_N) ? 0 : ((EXP_TB_PRESETS[i].share < 0) ? EXP_TB_PRESETS[i].frames : 0) + ExpTbBankFrames(i + 1);
}
constexpr int EXP_TB_BANK_FRAMES  = ExpTbBankFrames();
constexpr int EXP_TB_TOTAL_FRAMES = EXP_TB_LEGACY ? EXP_TB_LEGACY_TOTAL_FRAMES : EXP_TB_BANK_FRAMES;
// Data checks: stages start at 0, end at 1, ascend; ANALOG = 1 frame / 1 stage; generators / lists valid; share: an
// earlier own-frames preset with the same frames / stages / level rules.
constexpr bool ExpTbPresetOk(const ExpTbPreset& P, int z) {
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
  if (P.share >= 0) {
    if (P.share >= z) return false;
    const ExpTbPreset& Q = EXP_TB_PRESETS[P.share];
    if (Q.share >= 0 || Q.frames != P.frames || Q.st != P.st || Q.nst != P.nst || Q.rephase != P.rephase ||
        Q.rms != P.rms || Q.scale != P.scale) return false;
  }
  return true;
}
constexpr bool ExpTbBankOk(int i = 0) { return (i >= EXP_TB_BANK_N) ? true : ExpTbPresetOk(EXP_TB_PRESETS[i], i) && ExpTbBankOk(i + 1); }
static_assert(ExpTbBankOk(), "EXP_TB_PRESETS: invalid stage data (positions 0 .. 1 ascending, ANALOG 1 frame / stage, lists, share)");
static_assert(EXP_TB_BANK_N < 64, "VarKey holds the zone in 6 bits");
// The LOFI stages run on preset z (a LOFI preset, EXP_TB_LOFI_ENABLE, the audition bank, in range).
TUNABLE_CONSTEXPR_FN bool ExpTbLofiOn(int z) {   // (inline in DIAG builds: EXP_TB_LOFI_ENABLE is a live tunable)
  return !EXP_TB_LEGACY && EXP_TB_LOFI_ENABLE && z >= 0 && z < EXP_TB_BANK_N && EXP_TB_PRESETS[z].kind == kTbLofi;
}

}  // namespace explorer_k
