#pragma once
//
// NitroLab — bundle-global constants.
//
// NitroLab is a test-bench pedal target scaffolded from the ChronoTron3 shell
// (SW3 mode select, K6 equal-power dry/wet, bootloader gesture, DIAG switch,
// CPU boost). It carries armitage (SW3 UP, a polyphonic feedback-FM voice on
// a private copy of the POG band engine — see modules/armitage.h) plus two
// empty pass-through placeholder slots. Only
// things that are genuinely shell-wide live here. Per-module tuning lives in
// each module's own constants file (modules/<name>_constants.h).

#include <cstdint>

// Audio
static constexpr int   NL_BLOCK_SIZE      = 48;      // frames per audio block
static constexpr float NL_SAMPLE_RATE_HZ  = 48000.f; // nominal; real value from hw

// Shell: reserved both-footswitch gesture → Daisy bootloader (DFU).
static constexpr uint32_t NL_BOOTLOADER_HOLD_MS = 2000;

// Mode slots on SW3 (UP / MIDDLE / DOWN).
enum NlMode {
  NL_MODE_ARMITAGE  = 0,  // SW3 UP   — polyphonic feedback-FM band voice
  NL_MODE_EMPTY_B   = 1,  // SW3 MIDDLE — pass-through placeholder
  NL_MODE_EMPTY_C   = 2,  // SW3 DOWN   — pass-through placeholder
  NL_MODE_COUNT     = 3
};

// K6 dry/wet mix smoothing (one-pole, per audio block) to avoid zipper noise.
static constexpr float NL_MIX_SMOOTH = 0.002f;

// ---------------------------------------------------------------------------
// Diagnostics build. OFF in the shipping firmware; `make PEDAL=nitrolab DIAG=1`
// turns it on. The root Makefile defines the macro CT3_DIAG_BUILD literally
// (it is a pedal-agnostic build flag, not renamed here — the Makefile is
// shared across all pedals/<name> targets and is not pedal-specific code), so
// this gate checks that same macro. Every site using the resulting NL_DIAG
// constexpr bool is a plain `if (NL_DIAG)`, so with DIAG=0 the compiler
// deletes the code outright — no runtime cost, no branch.
// ---------------------------------------------------------------------------
#ifdef CT3_DIAG_BUILD
constexpr bool NL_DIAG = true;
#else
constexpr bool NL_DIAG = false;
#endif

// CPU clock boost: 480 MHz instead of libDaisy's 400 MHz default, applied at
// boot only on silicon revision V (main.cpp); false = always 400 MHz.
constexpr bool NL_CPU_BOOST = true;

// ---------------------------------------------------------------------------
// Pitch-tracker profile. Used through core/blocks/pitch_tracker.h, which
// `#include "constants.h"` and reads these global TRACK_* names. The consumer
// is armitage's mono/quant note tracking (SW2 MID/DOWN). Copied unchanged from
// ChronoTron3's profile (pedals/chronotron3/constants.h) — these names are
// read directly by the shared src/core/blocks/pitch_tracker.h, so they keep
// their established names rather than an NL_ prefix.
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
