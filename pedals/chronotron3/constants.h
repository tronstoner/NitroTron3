#pragma once
//
// ChronoTron3 — bundle-global constants.
//
// Only things that are genuinely shell-wide live here. Per-module tuning lives
// in each module's own constants file (modules/<name>_constants.h). ChronoTron3
// is instrument-agnostic by design ("one control set for all instruments"),
// so there is no INSTRUMENT profile here.

#include <cstdint>

// Audio
static constexpr int   CT3_BLOCK_SIZE      = 48;      // frames per audio block
static constexpr float CT3_SAMPLE_RATE_HZ  = 48000.f; // nominal; real value from hw

// Shell: reserved both-footswitch gesture → Daisy bootloader (DFU).
static constexpr uint32_t CT3_BOOTLOADER_HOLD_MS = 2000;

// Mode slots on SW3 (UP / MIDDLE / DOWN). Order per project decision
// (2026-07-26): A=vestige, B=mnemonic, C=sprawl (armitage's former slot).
enum Ct3Mode {
  CT3_MODE_VESTIGE  = 0,  // SW3 UP
  CT3_MODE_MNEMONIC = 1,  // SW3 MIDDLE
  CT3_MODE_SPRAWL   = 2,  // SW3 DOWN
  CT3_MODE_COUNT    = 3
};

// K6 dry/wet mix smoothing (one-pole, per audio block) to avoid zipper noise.
static constexpr float CT3_MIX_SMOOTH = 0.002f;

// ---------------------------------------------------------------------------
// Diagnostics build. OFF in the shipping firmware; `make PEDAL=chronotron3
// DIAG=1` turns it on (the Makefile defines CT3_DIAG_BUILD). It enables the USB
// serial heartbeat / fault dump in main.cpp plus sprawl's non-finite guard, its
// peak meters and the LED2 fault strobe. Every site is gated with a plain
// `if (CT3_DIAG)` on this constexpr bool, so with DIAG=0 the compiler deletes
// the code outright — no runtime cost, no branch. See
// .agents/skills/serial-diag/SKILL.md for how to capture a log.
// ---------------------------------------------------------------------------
#ifdef CT3_DIAG_BUILD
constexpr bool CT3_DIAG = true;
#else
constexpr bool CT3_DIAG = false;
#endif

// CPU clock boost: 480 MHz instead of libDaisy's 400 MHz default, applied at
// boot only on silicon revision V (main.cpp); false = always 400 MHz.
constexpr bool CT3_CPU_BOOST = true;

// ---------------------------------------------------------------------------
// Pitch-tracker profile. Used through core/blocks/pitch_tracker.h, which
// `#include "constants.h"` and reads these global TRACK_* names. The consumer
// is Sprawl's ringmod keytracking (SW1 DOWN, K4 >= 30%: the bell-partial
// carrier and its keytracked LPF). ChronoTron3 is instrument-agnostic, so it
// defines ONE broad profile: ~30–600 Hz fundamentals (full bass + low/mid
// guitar). Fundamentals above ~600 Hz won't lock — acceptable for a tracked
// carrier; tunable. `NT3_GUITAR` here is NOT an instrument switch — it just enables the
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

// ---------------------------------------------------------------------------
// Phaser block tuning (core/blocks/phaser.h `#include "constants.h"` and reads
// these global PHASER_* names). Copied 1:1 from NitroTron3's Schism phaser
// (pedals/nitrotron3/constants.h, 2026-10-09). Consumer: mnemonic's K3 pre-
// effect (SW2 UP, CCW). The per-pedal knob settings live in mnemonic_constants.h.
// ---------------------------------------------------------------------------
constexpr float PHASER_F1_HZ_MIN       = 100.f;   // ω fully CCW → notches at ~41 / 241 Hz
constexpr float PHASER_F1_HZ_MAX       = 4000.f;  // ω fully CW  → notches at ~1660 / 9660 Hz
constexpr float PHASER_SWEEP_OCT       = 1.5f;    // LFO depth: ±1.5 octaves (3-octave total sweep, matches Small Stone)
// Triangle LFO range: ambient drift → near sub-audio (sideband-generating).
constexpr float PHASER_LFO_TRI_HZ_MIN  = 0.02f;   // 50-second cycle
constexpr float PHASER_LFO_TRI_HZ_MAX  = 80.f;    // near sub-audio
// S&H rate range: one event per 2 s → 40 events/sec. No audio-rate;
// S&H character lives well below the triangle's top end.
constexpr float PHASER_LFO_SH_HZ_MIN   = 0.01f;  // bottom of K3-CW travel ≈ hold: with attack sync, each note keeps its one random step (~100 s free-run period)
constexpr float PHASER_LFO_SH_HZ_MAX   = 40.f;
// K2 = character morph (v1 roadmap item 1). The final mixing node creates the
// response: 0.5·(in + g·chain). g=+1 (K2 CCW) = today's notch phaser; g=0
// (noon) = flat/no filter; g=−1 (full CW) = peaks at the notch frequencies —
// a bandpass-stack character from the same allpass chain. Feedback no longer
// sits on a knob: it's coupled to the morph position (peaks want resonance,
// clean notch sweep doesn't) via the two ear-tunable endpoints below.
constexpr float PHASER_FB_AT_NOTCH     = -0.4f;  // feedback at K2 full CCW — NEGATIVE widens/flattens the notches (vacuum-cleaner sweep); morph passes ~0 mid-travel on the way to the resonant peak end
constexpr float PHASER_FB_AT_PEAK      = 0.80f;  // feedback at K2 full CW (resonant bandpass character; tanh-in-loop bounds runaway)
constexpr float PHASER_STAGE_SPREAD    = 0.04f;   // per-stage allpass coeff detune; breaks perfect notch alignment (organic, less "digital"). 0 = all stages identical
// 70s-swirl pass (Uni-Vibe/Schulte direction): stages stop moving in lockstep.
constexpr float PHASER_STAGE_LFO_SPAN  = 0.5f;    // total per-stage LFO phase offset span (cycles); stages breathe against each other (LDR mismatch). 0 = lockstep (old behavior). Triangle LFO only
constexpr float PHASER_SPREAD_PEAK_MULT = 4.0f;   // stage-detune multiplier at K2 full CW (notch end stays 1×) — spreads the loop resonance across several softer peaks instead of one sharp Q
// K2 two-phase travel (per side, from noon): the character morph hits FULL
// notch/peak at K2_FULL_AT of the half-travel; the remaining travel widens
// the LFO sweep range instead (×1 at the knee → ×SWEEP_MAX_MULT at the end).
// Feedback keeps its original full-travel mapping (unchanged resonance feel).
constexpr float PHASER_K2_FULL_AT      = 0.5f;    // fraction of half-travel where morph saturates
constexpr float PHASER_SWEEP_MAX_MULT  = 2.0f;    // sweep-depth multiplier at full travel (±1.5 oct → ±3 oct)
