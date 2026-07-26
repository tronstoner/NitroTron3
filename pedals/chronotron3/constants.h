#pragma once
//
// ChronoTron3 — bundle-global constants.
//
// Only things that are genuinely shell-wide live here. Per-module tuning lives
// in each module's own constants file (modules/<name>_constants.h). ChronoTron3
// is instrument-agnostic by design (armitage: "one control set for all
// instruments"), so there is no INSTRUMENT profile here.

#include <cstdint>

// Audio
static constexpr int   CT3_BLOCK_SIZE      = 48;      // frames per audio block
static constexpr float CT3_SAMPLE_RATE_HZ  = 48000.f; // nominal; real value from hw

// Shell: reserved both-footswitch gesture → Daisy bootloader (DFU).
static constexpr uint32_t CT3_BOOTLOADER_HOLD_MS = 2000;

// Mode slots on SW3 (UP / MIDDLE / DOWN). Order per project decision
// (2026-07-26): A=vestige, B=mnemonic, C=armitage.
enum Ct3Mode {
  CT3_MODE_VESTIGE  = 0,  // SW3 UP
  CT3_MODE_MNEMONIC = 1,  // SW3 MIDDLE
  CT3_MODE_ARMITAGE = 2,  // SW3 DOWN
  CT3_MODE_COUNT    = 3
};

// K6 dry/wet mix smoothing (one-pole, per audio block) to avoid zipper noise.
static constexpr float CT3_MIX_SMOOTH = 0.002f;

// ---------------------------------------------------------------------------
// Pitch-tracker profile. Used by Armitage's tracked behaviours through
// core/blocks/pitch_tracker.h, which `#include "constants.h"` and reads these
// global TRACK_* names. ChronoTron3 is instrument-agnostic, so it defines ONE
// broad profile: ~30–600 Hz fundamentals (full bass + low/mid guitar).
// Fundamentals above ~600 Hz won't lock — acceptable for a resonator voice;
// tunable. `NT3_GUITAR` here is NOT an instrument switch — it just enables the
// tracker's robust path (parabolic sub-lag refine + global-minimum fallback).
// Constraint (pitch_tracker.h static_assert): TRACK_WINDOW + TRACK_MAX_LAG ≤ 1024.
// ---------------------------------------------------------------------------
constexpr bool  NT3_GUITAR         = true;   // robust tracking path on
constexpr int   TRACK_DEC          = 4;      // 48 kHz → 12 kHz analysis rate
constexpr float TRACK_AA_LP_HZ     = 500.f;  // fundamental-isolation / anti-alias LP
constexpr int   TRACK_MIN_LAG      = 20;     // 12000/20  = 600 Hz (highest lock)
constexpr int   TRACK_MAX_LAG      = 400;    // 12000/400 =  30 Hz (lowest lock)
constexpr int   TRACK_WINDOW       = 400;    // YIN window (400+400 = 800 ≤ 1024)
constexpr int   TRACK_HOP          = 64;     // samples between YIN runs (~5 ms)
constexpr float TRACK_THRESHOLD    = 0.15f;  // YIN first-dip threshold
constexpr float TRACK_FALLBACK_MIN = 0.5f;   // accept global-min dip below this
constexpr bool  TRACK_PARABOLIC    = true;   // sub-lag parabolic refine
