#pragma once
//
// explorer_tables.h — the SW3 MID SW1 MID AUDITION BANK (EXP_TB_LEGACY = false; 2026-11-06): 16 data-driven table
// presets, K1 = 16 equal stepped zones in this list order (EXP_TB_ZONE_XFADE_MS crossfade on a change). ONE place:
// a new preset / journey is a new stage list + one EXP_TB_PRESETS entry, no code. Built at Init by BuildBank
// (explorer.h) through the legacy table pipeline; see explorer.h TABLES for the render and the K2 / K5 law per kind.
//
// PRESET = name, kind, frame count, key stages, level rules (re-phase, RMS match, int16 scale), share, MOD (the K5 / touch
//          env targets, EXP_TB_MOD; live per preset: see "Presets" below).
//   kind   LFO      K2 = TIMBRE = morph through the frames, K5 = the K5 LFO (as SW3 MID FM: CCW triangle, noon off, CW
//                   steps) on the preset's targets (MOD below)
//          ANALOG   one frame; K2 = 2-pole LP resonance Q (EXP_TF_Q_MIN .. EXP_TF_Q_MAX, exponential), K5 = the K5 LFO on
//                   the LP cutoff (the SW3 MID FM placement, explorer.h LfoFcOct; depths = MOD filter / filter swell)
//          LOFI     K2 = static morph position, K5 = no job with the default MOD (all depths 0); the Lofi engine
//                   (explorer_constants.h EXP_TB_LOFI_*, explorer.h TABLES LOFI) runs on it by its default Lofi flag
//   (every kind but ANALOG: K2 stepped through the key stages when the preset's Stepped flag is on, MOD stepped)
//   (JOURNEY, K5 = direction + speed of a morph travel, was removed 2026-11-06: its presets are LFO presets now.)
//   share  >= 0: the preset reads that (earlier) preset's frames instead of building its own; -1 = own frames.
//   MOD    EXP_TB_MOD(timbre, filter, filter swell, pitch, rate CCW, rate CW, env, stepped, lofi) - the DEFAULTS of the per-preset live
//          tunables (section Presets below; explorer.h K5 / TABLES MOD for the laws):
//            timbre        morph swing at full depth (+-, morph units 0..1)
//            filter        LP cutoff swing, octaves: PLUCK side (K4 CCW of the morph zone) upward 0 .. filter x depth x k3
//                          x (1 - o_d); with env: the ENV depth (+ filter x o octaves)
//            filter swell  LP cutoff swing, octaves: SWELL side downward 0 .. -filter swell x depth x o_r (LFO only)
//            pitch         vibrato depth, cents (+- c = pitch x l; env: + pitch x o)
//            rate CCW / CW K5 LFO top rate (Hz) at K5 full CCW (triangle) / full CW (steps); the bottom = EXP_LFO_HZ_MIN
//            env           false = the K5 LFO l drives the targets; true = the K4 touch env openness o (0..1) drives them
//                          instead (m = K2 + timbre o, cutoff + filter o octaves, pitch + pitch o cents); K5 then no effect
//            stepped       K2 DISCRETE: true = the morph snaps to the preset's key stages (the defined waves, not the
//                          generated in-between frames; one equal K2 zone per key stage, raw knob), a step slides over
//                          EXP_DISC_SLIDE_MS (1 ms); the MOD timbre offset still swings around it. ANALOG: no effect.
//                          false = continuous morph (explorer.h TbDiscMorph)
//            lofi          true = the Lofi engine's four stages (explorer_constants.h Lofi engine, global values) run on
//                          this preset (with EXP_TB_LOFI_ENABLE, the master); the zone crossfade weights them per side
#include "explorer_constants.h"

namespace explorer_k {

enum : uint8_t { kTbLfo = 0, kTbAnalog = 2, kTbLofi = 3 };  // preset kind (1 was JOURNEY, removed 2026-11-06)
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
// Per-preset K5 / touch env targets (defaults of the per-preset live tunables, section Presets below).
struct ExpTbMod {
  float timbre;            // morph swing, morph units
  float filter;            // LP cutoff, octaves: pluck side up (LFO) / the ENV depth
  float filter_swell;      // LP cutoff, octaves: swell side down (LFO)
  float pitch;             // vibrato, cents
  float rate_ccw;          // K5 LFO top rate at K5 full CCW, Hz
  float rate_cw;           // K5 LFO top rate at K5 full CW, Hz
  bool  env;               // source: false = K5 LFO, true = K4 touch env openness o
  bool  stepped;           // K2 DISCRETE: K2 snaps to the key stages (1 ms slide)
  bool  lofi;              // the Lofi engine runs on this preset
};
#define EXP_TB_MOD(td, fd, fs, pd, rccw, rcw, env, stp, lofi) ExpTbMod{ td, fd, fs, pd, rccw, rcw, env, stp, lofi }
struct ExpTbPreset {
  const char* name;
  uint8_t kind;            // kTbLfo / kTbAnalog / kTbLofi
  int frames;              // frames built (ANALOG: 1)
  const ExpTbStage* st;    // key stages
  int nst;
  bool rephase;            // legacy re-phase rule (sine phase, positive; magnitudes kept)
  bool rms;                // legacy RMS match to the plain saw (+ cap to the int16 headroom)
  float scale;             // int16 storage scale
  int share;               // -1 = own frames; >= 0 = read that earlier preset's frames (same frames / level rules)
  ExpTbMod mod;            // K5 / touch env targets: the per-preset tunables' defaults (EXP_TB_MOD)
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
// (K2 / the timbre LFO travel through identical frames; filter / pitch depths are what move it).
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
  { "SAW",    kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_SAW),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(0.f,  2.6f, 1.f,  0.f, 20.f, 20.f, false, false, false) },
  { "PULSE",  kTbAnalog,  1, EXP_TB_S(EXP_TB_ST_PULSE),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(0.f,  2.6f, 1.f,  0.f, 20.f, 20.f, false, false, false) },
  { "PWM",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PWM),    false, false, EXP_TB_I16_SCALE,      -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "SYNC",   kTbLfo,    16, EXP_TB_S(EXP_TB_ST_SYNC),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "VOCAL",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_VOCAL),  false, false, EXP_TB_I16_SCALE,      -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "WOOD",   kTbLfo,    16, EXP_TB_S(EXP_TB_ST_WOOD),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "REED",   kTbLfo,    16, EXP_TB_S(EXP_TB_ST_REED),   true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "BRASS",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_BRASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "ORGAN",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_ORGAN),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "PPG",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_PPG),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "METAL",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_METAL),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "GLASS",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_GLASS),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "RASPY",  kTbLfo,    16, EXP_TB_S(EXP_TB_ST_RASPY),  true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "BREATH", kTbLfo,    16, EXP_TB_S(EXP_TB_ST_BREATH), true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.15f, 0.f,  0.f,  0.f, 20.f, 20.f, false, false, false) },
  { "AIR",    kTbLfo,    16, EXP_TB_S(EXP_TB_ST_AIR),    true,  true,  EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.047f, .54f, .64f, 15.f, 5.228f, 20.f, false, false, true) },
  { "LOFI",   kTbLofi,   31, EXP_TB_S(EXP_TB_ST_LOFISPEC), true, true, EXP_TB_I16_SCALE_WIDE, -1, EXP_TB_MOD(.114f, 0.f,  0.f,  0.f, .4552f, 20.f, false, true,  false) },
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
  if (!(P.mod.rate_ccw > 0.f) || !(P.mod.rate_cw > 0.f) || P.mod.pitch < 0.f) return false;   // MOD: rates > 0
  if (P.share >= 0) {
    if (P.share >= z) return false;
    const ExpTbPreset& Q = EXP_TB_PRESETS[P.share];
    if (Q.share >= 0 || Q.frames != P.frames || Q.st != P.st || Q.nst != P.nst || Q.rephase != P.rephase ||
        Q.rms != P.rms || Q.scale != P.scale) return false;
  }
  return true;
}
constexpr bool ExpTbBankOk(int i = 0) { return (i >= EXP_TB_BANK_N) ? true : ExpTbPresetOk(EXP_TB_PRESETS[i], i) && ExpTbBankOk(i + 1); }
static_assert(ExpTbBankOk(), "EXP_TB_PRESETS: invalid stage data (positions 0 .. 1 ascending, ANALOG 1 frame / stage, lists, share, MOD rates)");
static_assert(EXP_TB_BANK_N < 64, "VarKey holds the zone in 6 bits");

// --- Presets ---
// @section Presets
// @at 2/wt
// Per-preset live settings of the K5 LFO / touch env targets (2026-11-06): one tunable per preset and field, named
// EXP_TB_<PRESET>_<FIELD>, declared by EXP_TB_MOD_DECLARE for every preset of EXP_TB_PRESET_MODS (K1 order = the
// EXP_TB_PRESETS rows; a static_assert checks the names). DEFAULT = that field of the preset's EXP_TB_MOD(...) in
// EXP_TB_PRESETS above (the single source of truth); normal builds: constexpr, DIAG builds: live. Laws: explorer.h
// TABLES MOD. tools/devtool/registry.py reads the @family / @field lines and the EXP_TB_MOD rows (field order = the
// EXP_TB_MOD arguments) and shows one panel per preset.
// @family EXP_TB_ EXP_TB_MOD EXP_TB_PRESET_MODS
// @field TIMBRE float @unit 0..1 @range 0 1 @taper lin @level live @why the morph m = K2 + offset is clamped to 0 .. 1: a swing of 1 already spans it all @target timbre  Timbre depth: morph swing at full LFO depth (+-) / x o (env)
// @field FILTER float @unit oct @range 0 8.06 @taper lin @level live @why the LP cutoff is capped at EXP_TF_FMAX (16 kHz); from its lowest point, the touch env bottom EXP_TF_ENV_BOTTOM (60 Hz), that is log2(16000 / 60) = 8.06 octaves @target filter  Filter depth: LP cutoff, pluck side upward (LFO) / + this x o (env)
// @field FILTER_SWELL float @unit oct @range 0 8.06 @taper lin @level live @why downward from the cap EXP_TF_FMAX (16 kHz) to the touch env bottom EXP_TF_ENV_BOTTOM (60 Hz) = 8.06 octaves; deeper leaves the cutoff law (uncapped below, toward 0 Hz) @target filter  Filter depth, swell side: LP cutoff downward (LFO only)
// @field PITCH float @unit cents @range 0 9960 @taper lin @level live @why the bands run at 8 kHz (Nyquist 4 kHz): at + 9960 cents even the lowest band of any slot (12.7 Hz, down slot) is past 4 kHz - every band folds @target pitch  Pitch depth: vibrato +- this x l cents / + this x o (env)
// @field RATE_CCW float @unit Hz @range 0.001 4000 @taper log @level live @why the LFO runs per 8 kHz sample: 4000 Hz = Nyquist of the control rate; min: phase += f / 8000 in float stalls below 0.0005 Hz (0.001 = 2 ulps) @target pitch @pair LFO_rate ccw  LFO rate at K5 full CCW (triangle)
// @field RATE_CW float @unit Hz @range 0.001 4000 @taper log @level live @why the LFO runs per 8 kHz sample: 4000 Hz = Nyquist of the control rate; min: phase += f / 8000 in float stalls below 0.0005 Hz (0.001 = 2 ulps) @target pitch @pair LFO_rate cw  LFO rate at K5 full CW (steps)
// @field ENV bool @unit bool @level live @target env @states LFO env  Source ENV: the K4 touch env drives the targets instead of the K5 LFO
// @field STEPPED bool @unit bool @level live @target timbre @states smooth stepped  K2 snaps to the preset's key stages (1 ms slide); ANALOG: no effect
// @field LOFI bool @unit bool @level live @target lofi @states off on  The Lofi engine's four stages run on this preset (global values, section Lofi engine; master EXP_TB_LOFI_ENABLE)
constexpr bool ExpTbStrEq(const char* a, const char* b) {
  return (*a != *b) ? false : (*a == 0) ? true : ExpTbStrEq(a + 1, b + 1);
}
#define EXP_TB_PRESET_MODS(X) \
  X(0, SAW)                   \
  X(1, PULSE)                 \
  X(2, PWM)                   \
  X(3, SYNC)                  \
  X(4, VOCAL)                 \
  X(5, WOOD)                  \
  X(6, REED)                  \
  X(7, BRASS)                 \
  X(8, ORGAN)                 \
  X(9, PPG)                   \
  X(10, METAL)                \
  X(11, GLASS)                \
  X(12, RASPY)                \
  X(13, BREATH)               \
  X(14, AIR)                  \
  X(15, LOFI)
#define EXP_TB_MOD_DECLARE(Z, N)                                                                      \
  static_assert(ExpTbStrEq(EXP_TB_PRESETS[Z].name, #N), "EXP_TB_PRESET_MODS: name / index mismatch"); \
  TUNABLE(float, EXP_TB_##N##_TIMBRE, EXP_TB_PRESETS[Z].mod.timbre);                                  \
  TUNABLE(float, EXP_TB_##N##_FILTER, EXP_TB_PRESETS[Z].mod.filter);                                  \
  TUNABLE(float, EXP_TB_##N##_FILTER_SWELL, EXP_TB_PRESETS[Z].mod.filter_swell);                      \
  TUNABLE(float, EXP_TB_##N##_PITCH, EXP_TB_PRESETS[Z].mod.pitch);                                    \
  TUNABLE(float, EXP_TB_##N##_RATE_CCW, EXP_TB_PRESETS[Z].mod.rate_ccw);                              \
  TUNABLE(float, EXP_TB_##N##_RATE_CW, EXP_TB_PRESETS[Z].mod.rate_cw);                                \
  TUNABLE(bool, EXP_TB_##N##_ENV, EXP_TB_PRESETS[Z].mod.env);                                        \
  TUNABLE(bool, EXP_TB_##N##_STEPPED, EXP_TB_PRESETS[Z].mod.stepped);                                \
  TUNABLE(bool, EXP_TB_##N##_LOFI, EXP_TB_PRESETS[Z].mod.lofi);
EXP_TB_PRESET_MODS(EXP_TB_MOD_DECLARE)
static_assert(EXP_TB_BANK_N == 16, "EXP_TB_PRESET_MODS lists 16 presets");
// The current values of every preset's MOD tunables (explorer.h: Init, and DIAG TunableRefresh after a set).
inline void ExpTbModRead(ExpTbMod* out) {
#define EXP_TB_MOD_READ(Z, N)                                                                             \
  out[Z] = ExpTbMod{ EXP_TB_##N##_TIMBRE, EXP_TB_##N##_FILTER, EXP_TB_##N##_FILTER_SWELL, EXP_TB_##N##_PITCH, \
                     EXP_TB_##N##_RATE_CCW, EXP_TB_##N##_RATE_CW, EXP_TB_##N##_ENV, EXP_TB_##N##_STEPPED,     \
                     EXP_TB_##N##_LOFI };
  EXP_TB_PRESET_MODS(EXP_TB_MOD_READ)
#undef EXP_TB_MOD_READ
}
// The per-preset flags of preset z straight from their tunables (constexpr normally, live in DIAG): Stepped (K2
// DISCRETE) and Lofi. Out of range: false.
TUNABLE_CONSTEXPR_FN bool ExpTbStepFlag(int z) {
  switch (z) {
#define EXP_TB_STEP_CASE(Z, N) case Z: return EXP_TB_##N##_STEPPED;
    EXP_TB_PRESET_MODS(EXP_TB_STEP_CASE)
#undef EXP_TB_STEP_CASE
    default: return false;
  }
}
TUNABLE_CONSTEXPR_FN bool ExpTbLofiFlag(int z) {
  switch (z) {
#define EXP_TB_LOFI_CASE(Z, N) case Z: return EXP_TB_##N##_LOFI;
    EXP_TB_PRESET_MODS(EXP_TB_LOFI_CASE)
#undef EXP_TB_LOFI_CASE
    default: return false;
  }
}
// The Lofi engine runs on preset z: the audition bank, EXP_TB_LOFI_ENABLE (master), z in range, its Lofi flag on.
TUNABLE_CONSTEXPR_FN bool ExpTbLofiOn(int z) {   // (inline in DIAG builds: live tunables)
  return !EXP_TB_LEGACY && EXP_TB_LOFI_ENABLE && z >= 0 && z < EXP_TB_BANK_N && ExpTbLofiFlag(z);
}

}  // namespace explorer_k
