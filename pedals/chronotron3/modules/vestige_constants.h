#pragma once
//
// vestige_constants.h — named tuning constants for the vestige module
// (dynamic looper / freeze, grain-based). Included from vestige.h.
//
// All sample-count constants assume 48 kHz (CT3 runs at SAI_48KHZ). They size
// compile-time arrays, so they use the 48000 literal rather than the runtime sr.
//
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Buffer / memory
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_SR               = 48000.f;
static constexpr float  VESTIGE_LOOP_SECONDS     = 8.f;                 // max capture length
static constexpr size_t VESTIGE_GUARD_SAMPLES    = 21504;              // ~448 ms wrap-guard (> longest grain)
static constexpr size_t VESTIGE_LOOP_MAX_SAMPLES = (size_t)(VESTIGE_LOOP_SECONDS * VESTIGE_SR); // 384000
static constexpr size_t VESTIGE_VOICE_CAP        = VESTIGE_LOOP_MAX_SAMPLES + VESTIGE_GUARD_SAMPLES;

// ---------------------------------------------------------------------------
// Topology (K1)
// ---------------------------------------------------------------------------
static constexpr int    VESTIGE_MAX_VOICES  = 6;      // voiced poly max (slots 0..5)
static constexpr int    VESTIGE_FRIP_SLOT   = VESTIGE_MAX_VOICES; // slot 6 = frippertronics buffer
static constexpr int    VESTIGE_SLOTS       = VESTIGE_MAX_VOICES + 1;
static constexpr float  VESTIGE_FRIP_THRESHOLD = 0.86f;  // K1 above this → frippertronics region
static constexpr int    VESTIGE_GRAINS      = 16;    // shared grain pool (bounds CPU)

// ---------------------------------------------------------------------------
// Grain smoothness macro (K3): looper (CCW / 0) → freeze (CW / 1)
// ---------------------------------------------------------------------------
static constexpr size_t VESTIGE_CCW_GRAIN_LEN = 19200;  // 400 ms — long grain, looper end
static constexpr size_t VESTIGE_CW_GRAIN_LEN  = 1440;   // 30 ms  — short grain, freeze end
static constexpr size_t VESTIGE_GRAIN_MIN_LEN = 256;
static constexpr float  VESTIGE_CCW_OVERLAP   = 2.0f;   // Hann overlap-add sums flat → seamless loop
static constexpr float  VESTIGE_CW_OVERLAP    = 3.0f;   // denser cloud so short grains fuse
static constexpr size_t VESTIGE_MIN_INTERVAL  = 32;     // scheduler floor (samples)
static constexpr size_t VESTIGE_MIN_LOOP_SAMPLES = 480; // 10 ms shortest capture (short FS2 tap)

// ---------------------------------------------------------------------------
// Footswitch timing (FS1 stop)
// ---------------------------------------------------------------------------
static constexpr uint32_t VESTIGE_FS1_TAP_MAX_MS    = 350;  // <= this on release = tap (mute/pause)
static constexpr uint32_t VESTIGE_FS1_CLEAR_HOLD_MS = 700;  // >= this while held = clear all

// ---------------------------------------------------------------------------
// Continuous-auto capture (SW1 MIDDLE)
// ---------------------------------------------------------------------------
static constexpr float    VESTIGE_ENV_COEF        = 0.002f; // input |env| one-pole
static constexpr float    VESTIGE_AUTO_THRESH_MIN = 0.005f; // K2 CCW: sensitive
static constexpr float    VESTIGE_AUTO_THRESH_MAX = 0.10f;  // K2 CW:  insensitive
static constexpr float    VESTIGE_AUTO_HYST       = 0.55f;  // close threshold = open * hyst
static constexpr uint32_t VESTIGE_AUTO_RELEASE_MS = 300;    // silence held this long ends a phrase

// ---------------------------------------------------------------------------
// Texture (K4): bipolar, clean at centre
//   analogue side (CCW) = tape saturation → extreme; digital side (CW) = decimate/crush → glitch
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_TEX_DEADZONE   = 0.06f;  // clean band around noon
static constexpr float  VESTIGE_TAPE_DRIVE_MAX = 45.f;   // tanh drive at full CCW
static constexpr float  VESTIGE_DECIM_HOLD_MAX = 96.f;   // sample-hold length (samples) at full CW
static constexpr float  VESTIGE_CRUSH_BITS_HI  = 16.f;   // bit depth near noon
static constexpr float  VESTIGE_CRUSH_BITS_LO  = 2.5f;   // bit depth at full CW

// ---------------------------------------------------------------------------
// K5 fade / decay
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_AGE_SLOPE_MIN  = 0.35f;  // gentle age-fade curve (voiced)
static constexpr float  VESTIGE_AGE_SLOPE_MAX  = 3.0f;   // steep age-fade curve (voiced)
static constexpr float  VESTIGE_FRIP_DECAY_MIN = 0.90f;  // fast tape decay (frippertronics)
static constexpr float  VESTIGE_FRIP_DECAY_MAX = 1.0f;   // infinite sustain (frippertronics)

// ---------------------------------------------------------------------------
// LED blink (Controls runs every ~10 ms)
// ---------------------------------------------------------------------------
static constexpr int    VESTIGE_BLINK_SLOW = 50;  // *10 ms → 500 ms half-period
static constexpr int    VESTIGE_BLINK_FAST = 12;  // *10 ms → 120 ms half-period
static constexpr int    VESTIGE_FLASH_TICKS = 30; // clear-confirm flash duration
