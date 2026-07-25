#pragma once
//
// ChronoTron3 — bundle-global constants.
//
// Only things that are genuinely shell-wide live here. Per-module tuning lives
// in each module's own constants file (modules/<name>_constants.h). ChronoTron3
// is instrument-agnostic by design (ignis: "one control set for all
// instruments"), so there is no INSTRUMENT profile here.

#include <cstdint>

// Audio
static constexpr int   CT3_BLOCK_SIZE      = 48;      // frames per audio block
static constexpr float CT3_SAMPLE_RATE_HZ  = 48000.f; // nominal; real value from hw

// Shell: reserved both-footswitch gesture → Daisy bootloader (DFU).
static constexpr uint32_t CT3_BOOTLOADER_HOLD_MS = 2000;

// Mode slots on SW3 (UP / MIDDLE / DOWN). Order per project decision
// (2026-07-26): A=vestige, B=mnemonic, C=ignis.
enum Ct3Mode {
  CT3_MODE_VESTIGE  = 0,  // SW3 UP
  CT3_MODE_MNEMONIC = 1,  // SW3 MIDDLE
  CT3_MODE_IGNIS    = 2,  // SW3 DOWN
  CT3_MODE_COUNT    = 3
};

// K6 dry/wet mix smoothing (one-pole, per audio block) to avoid zipper noise.
static constexpr float CT3_MIX_SMOOTH = 0.002f;
