#pragma once
//
// mnemonic_constants.h — named tuning constants for the mnemonic module
// (tap-tempo tape/BBD delay). Included from mnemonic.h.
//
// Spec: docs/ChronoTron3/mnemonic-concept.md + mnemonic-impl-plan.md.
// All sample-count constants assume 48 kHz (CT3 runs at SAI_48KHZ) so they can
// size compile-time SDRAM arrays; runtime uses the real sr where it matters.
//
// Every value here is a STARTING BRACKET to explore range, not a final voicing.
// Tune by ear on hardware (the bundle ethos: extremes first, polish later).
//
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Buffers / memory (SDRAM)
// ---------------------------------------------------------------------------
static constexpr float  MNEM_SR                 = 48000.f;
static constexpr float  MNEM_DELAY_MAX_S        = 8.f;   // worst case (tap 2 s x div 4/1)
static constexpr size_t MNEM_DELAY_SAMPLES      = (size_t)(MNEM_DELAY_MAX_S * MNEM_SR); // 384000
static constexpr float  MNEM_LOOP_MAX_S         = 16.f;  // hold/loop capture ceiling
static constexpr size_t MNEM_LOOP_SAMPLES       = (size_t)(MNEM_LOOP_MAX_S * MNEM_SR);  // 768000

// ---------------------------------------------------------------------------
// Delay time (K1, SW2 UP = knob time)
// ---------------------------------------------------------------------------
static constexpr float MNEM_TIME_MIN_MS = 20.f;
static constexpr float MNEM_TIME_MAX_MS = 3000.f;

// Varispeed glide: one-pole coef per sample pulling the read tap toward its
// target. Small = slow, syrupy tape slur; large = snappy. ~1/(coef*sr) sec.
static constexpr float MNEM_GLIDE_COEF = 0.0007f;   // ~30 ms time-constant

// ---------------------------------------------------------------------------
// Tap-division ratios (K1, SW2 MID). Noon = index 5 = 1/1 (quarter = tap).
// CCW shorter, CW longer, exact reciprocal mirror. See concept doc table.
// ---------------------------------------------------------------------------
static constexpr int   MNEM_DIV_COUNT = 11;
static constexpr int   MNEM_DIV_NOON  = 5;
static constexpr float MNEM_DIV_RATIOS[MNEM_DIV_COUNT] = {
    0.25f,        // 1/4  sixteenth
    1.f / 3.f,    // 1/3  eighth-triplet
    0.5f,         // 1/2  eighth
    2.f / 3.f,    // 2/3  quarter-triplet
    0.75f,        // 3/4  dotted-eighth
    1.f,          // 1/1  quarter (tap period) — NOON
    4.f / 3.f,    // 4/3  half-triplet
    1.5f,         // 3/2  dotted-quarter
    2.f,          // 2/1  half
    3.f,          // 3/1  dotted-half
    4.f,          // 4/1  whole
};
static constexpr float MNEM_DIV_HYST = 0.015f;  // knob margin to change division stop

// ---------------------------------------------------------------------------
// Feedback (K2, 0 -> self-oscillation) + always-on tape drive
// ---------------------------------------------------------------------------
static constexpr float MNEM_FB_MAX      = 1.15f;  // >1: oscillates; tanh bounds level
static constexpr float MNEM_TAPE_DRIVE  = 1.4f;   // base in-loop tanh drive (always on)
static constexpr float MNEM_TAPE_DRIVE_K3 = 2.6f; // extra drive added at full K3-CW tape
// No build-up ducker: the in-loop tanh (TapeDrive) is the sole level safety, so
// feedback regenerates accurately and K1 sweeps don't get ducked. (The Mode-B
// ducker was the feedback-drown regression — removed.)

// ---------------------------------------------------------------------------
// Tone filter (K4 tilt/center + K5 narrow) — POST-delay, OUT of the feedback
// loop. Two 24 dB/oct filters (2x cascaded SVF each): a high-pass at `lo` and a
// low-pass at `hi`. K4 sets tilt/center; K5 shrinks the gap hi<->lo toward the
// center (band-limit by convergence, not one sharp BP peak). Each SVF stage runs
// at MNEM_FILTER_RES_Q -> a moderate resonant bump at BOTH cutoffs.
// Model mirrored in docs/ChronoTron3/mnemonic-filter-demo.html.
// ---------------------------------------------------------------------------
static constexpr float MNEM_FILT_FMIN    = 20.f;    // band floor
static constexpr float MNEM_FILT_FMAX    = 20000.f; // band ceiling
static constexpr float MNEM_FILT_HP_MAX  = 4000.f;  // HP cutoff at full K4-CW (thin)
static constexpr float MNEM_FILT_LP_MIN  = 180.f;   // LP cutoff at full K4-CCW (dark)
static constexpr float MNEM_FILTER_RES_Q = 0.707f;  // per-stage Q: 0.707 = flat/no resonance; raise for "nasal" formant (~1.6 = +9 dB)
static constexpr float MNEM_FILT_MAKEUP_XS  = 2.0f; // narrow-band over-compensation: 1 = level-restore only, >1 = narrow K5 sits louder
static constexpr float MNEM_FILT_MAKEUP_MAX = 16.f; // cap on the center-gain makeup (~+24 dB)

// Audio-rate parameter smoothing (kills the ~10 ms control-tick zipper on K2-K5).
static constexpr float MNEM_SMOOTH_MS = 5.f;

// ---------------------------------------------------------------------------
// Degrade character (K3): CW tape (warble+drive+HF loss) / CCW BBD (decimate)
// ---------------------------------------------------------------------------
static constexpr float MNEM_WOW_HZ           = 0.7f;   // slow tape wow
static constexpr float MNEM_FLUTTER_HZ       = 6.3f;   // fast flutter
static constexpr float MNEM_WOW_DEPTH_MS     = 7.f;    // read-tap wobble at full tape
static constexpr float MNEM_FLUTTER_DEPTH_MS = 1.6f;
static constexpr float MNEM_TAPE_HFLOSS_HZ   = 2200.f; // in-loop LP cutoff at full tape
static constexpr int   MNEM_BBD_DECIM_MAX    = 16;     // sample-hold factor at full CCW (48k/16=3k)
static constexpr float MNEM_BBD_LP_HZ        = 4200.f; // rounding LP on the BBD side
static constexpr float MNEM_BBD_BITS_MIN     = 8.f;    // gentle bit reduction floor (>=8, "round")

// ---------------------------------------------------------------------------
// Footswitch / tap timing (control-rate, wall-clock ms via System::GetNow)
// ---------------------------------------------------------------------------
// FS1 disambiguation (unified hold-then-commit): released before TAP_RELEASE = a
// tap; held past LONGPRESS = a sustained gesture (loop / tape); released in the
// deadzone between = no-op. Kept as two separate constants (a deadzone may be
// wanted) rather than one shared threshold.
static constexpr uint32_t MNEM_TAP_RELEASE_MS = 300; // released before this = TAP
static constexpr uint32_t MNEM_LONGPRESS_MS   = 450; // held beyond this = sustained gesture
static constexpr uint32_t MNEM_TAP_WINDOW_MS = 3000; // group taps into one gesture
static constexpr uint32_t MNEM_TAP_MAX_MS    = 2000; // slowest usable tap interval
static constexpr uint32_t MNEM_TAP_MIN_MS    = 40;   // ignore faster than this
static constexpr int      MNEM_TAP_MEDIAN_N  = 4;    // recent intervals for the median

// ---------------------------------------------------------------------------
// Tape gestures (FS1 hold; SW1 UP spin-up / DOWN slow-down)
// ---------------------------------------------------------------------------
static constexpr float MNEM_GEST_UP_TIMEFAC   = 0.30f; // spin-up shortens delay -> pitch up
static constexpr float MNEM_GEST_DOWN_TIMEFAC = 3.0f;  // slow-down lengthens -> pitch down
static constexpr float MNEM_GEST_UP_FB        = 1.05f; // feedback while spinning up
static constexpr float MNEM_GEST_DOWN_FB      = 1.08f; // feedback while slowing down (own tuning)
static constexpr float MNEM_GEST_ATK_MS       = 1100.f;// ramp-in while held (the pitch dive)
static constexpr float MNEM_GEST_REL_MS       = 1400.f;// slew back on release

// ---------------------------------------------------------------------------
// Hold / loop (SW1 MID)
// ---------------------------------------------------------------------------
// Loop commit is governed by MNEM_LONGPRESS_MS (a press held that long is a loop,
// not a tap); no separate min-length gate.
static constexpr float    MNEM_LOOP_XFADE_MS = 6.f;  // seam crossfade at the wrap
static constexpr float    MNEM_LOOP_FADE_MS  = 8.f;  // play start/stop de-click ramp

// ---------------------------------------------------------------------------
// Edge dual-tap (SW2 DOWN): tap A = 4/4 (quarter) · tap B = K1 division. A fixed
// detune keeps them off unison at noon -> chorus; feedback recirculates the sum.
// ---------------------------------------------------------------------------
static constexpr float MNEM_EDGE_DETUNE_MS = 11.f;  // fixed offset on tap B (chorus near unison)
static constexpr float MNEM_EDGE_A_GAIN    = 0.75f; // 4/4 tap level
static constexpr float MNEM_EDGE_B_GAIN    = 0.75f; // division tap level

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------
static constexpr float MNEM_LED_CLOCK_ON_MS = 40.f;  // LED1 delay-clock blink width
