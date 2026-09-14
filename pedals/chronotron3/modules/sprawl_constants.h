#pragma once
//
// sprawl — compile-time constants (source of truth).
//
// 1:1 port of NitroTron3's Mode B ("Sprawl", granular delay). Every value and
// every explanatory comment is copied verbatim from
//   pedals/nitrotron3/constants.h   (Mode B block)
//   pedals/nitrotron3/main.cpp      (Mode B file-local constants)
// so the port stays diff-able against the original. Do not retune here without
// deciding, explicitly, to diverge from NitroTron3.
//
// *** INCLUDE-ORDER REQUIREMENT ***
// src/core/blocks/glitch_zones.h does `#include "constants.h"` and reads the
// GLITCH_* names below as PLAIN GLOBAL constexprs. For ChronoTron3 that include
// resolves to pedals/chronotron3/constants.h, which does NOT define them — so
// this header must be included BEFORE glitch_zones.h (see sprawl.h). That is
// also why the GLITCH_* names stay unprefixed and at global scope.
//
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Grain buffer / voices (main.cpp file-locals)
// ---------------------------------------------------------------------------
static constexpr size_t GRAIN_BUF_SAMPLES = 48000 * 8;  // 8 s at 48 kHz
static constexpr int    NUM_GRAIN_VOICES  = 8;
static constexpr size_t GRAIN_MIN_RANGE   = 4800;   // min read range: 100 ms

// Envelope follower low-pass. NitroTron3 profiles this (50 Hz bass / 80 Hz
// guitar); ChronoTron3 is one instrument-agnostic firmware, so the shipped
// bass value is taken for both.
static constexpr float SPRAWL_ENV_LP_CUTOFF_HZ = 50.0f;

// ---------------------------------------------------------------------------
// Mode B SW1 MIDDLE — Event-Driven Digital Glitch
// Bipolar K4: noon = clean. CCW = bit-flip events, CW = timing events
// (freeze / stutter / reverse). Buchla SoU style: stochastic trigger
// timing, randomised per-event parameters, env-gated mix (silence-in →
// silence-out). See docs/MODE_B_TEXTURE_IDEAS.md.
// ---------------------------------------------------------------------------
constexpr float GLITCH_DEADZONE             = 0.05f; // ±5% around noon → clean
constexpr int   GLITCH_XOR_MAX_BIT          = 13;    // highest bit flipped at full CCW (±0.25 of full scale)
constexpr float GLITCH_EVENT_RATE_HZ_MAX    = 25.0f; // CCW: events/sec at full effect_pos × full env
constexpr float GLITCH_EVENT_RATE_HZ_MAX_CW = 50.0f; // CW: 2× CCW rate at full deflection — timing events feel faster
constexpr int   GLITCH_EVENT_DUR_MIN_SAMPLES = 240;  // 5 ms at 48 kHz
constexpr int   GLITCH_EVENT_DUR_MAX_SAMPLES = 2400; // 50 ms at 48 kHz
constexpr int   GLITCH_BUFFER_SAMPLES       = 2400;  // 50 ms ring buffer for CW timing payload
constexpr int   GLITCH_RAMP_SAMPLES         = 48;    // 1 ms click-free wet/dry ramp
constexpr float GLITCH_ENV_GATE             = 0.01f; // raw env_val noise gate — below this, no new events arm (passive bass ≈0.02–0.1 while played)
// Auto (stochastic) event onset along K4 travel. Below this fraction of
// effect_pos, NO auto events fire — that early travel is reserved for note-on
// triggered events only. Above it, the auto amount ramps in (squared curve, so
// it starts slow) up to full rate at the extreme. Reactive triggering is
// unaffected: it fires from the moment K4 leaves the deadzone.
constexpr float GLITCH_AUTO_ONSET           = 0.15f; // fraction of travel reserved for triggered-only events

// ---------------------------------------------------------------------------
// Mode B SW2 DOWN — Bode frequency shifter (K1 bipolar, unison at noon)
// K1 ranges ±1 kHz with an exponential taper around unison: fine sub-Hz
// resolution near center, full kHz at the extremes. Center deadzone holds
// pure unison. Shifter sits inside the feedback loop (post-grain, post-SW3
// FX, pre-wet-HPF) so each loop pass cascades the shift — pile-up by design.
// Grain buffer-read pitch is forced to 1.0 in this sub-mode.
// ---------------------------------------------------------------------------
constexpr float FREQ_SHIFT_MAX_HZ    = 1000.f;
constexpr float FREQ_SHIFT_DEADZONE  = 0.02f;  // |k1_norm| < this → 0 Hz
constexpr float FREQ_SHIFT_CURVE     = 6.0f;   // taper exponent (higher = more weighted near unison)

// ---------------------------------------------------------------------------
// Mode B reverb + bipolar K5
// Wet high-pass (2-pole) — keeps the wet bus above the dry instrument's low
// range so the wet sits on top instead of fighting the fundamentals. Guitar's
// dry range starts an octave up, so the shelf rises with it.
// ---------------------------------------------------------------------------
constexpr float WET_HPF_FREQ = 120.f;  // feedback-return HPF, both profiles — guitar's 200 made the loop go harsh/bright too soon
constexpr float K5_CENTER_DEADZONE = 0.05f;  // ±5% deadzone around center
constexpr float REVERB_INPUT_GAIN  = 0.40f;  // gain into the Clouds reverb
constexpr float REVERB_TIME        = 0.70f;  // reverb decay (krt in Clouds)
constexpr float FEEDBACK_MAX = 2.0f;  // K5-CW ring-buffer feedback ceiling (tanh limiter + duckers keep it a controlled drone)
constexpr float FB_UNISON_SCALE     = 0.60f; // feedback scale at unison (K1=0). Unison piles up coherently, so it's cut vs intervals; ramps to 1.0 by +3 semi. Raise toward 1.0 for stronger unison feedback.
constexpr float REVERB_AMT_SMOOTH_COEF = 0.002f; // one-pole on K5 reverb amount, ~10 ms tc
constexpr float PARAM_SMOOTH_COEF      = 0.002f; // generic control smoother (Smoother), ~10 ms tc — de-zipper audio-rate knob gains

// Reverb sample-rate conversion (main.cpp file-locals). Clouds reverb runs
// internally at 32 kHz. 48->32 downsampler is mono (one shared input). 32->48
// upsampler is per-channel so the reverb's L/R decorrelation survives.
static constexpr float RESAMPLER_CUTOFF_HZ   = 15000.f;
static constexpr float RESAMPLER_PROTO_FS_HZ = 96000.f;

// ---------------------------------------------------------------------------
// Feedback bus (main.cpp file-locals)
// ---------------------------------------------------------------------------
// Feedback saturation drive: pre-multiplies the tanh input so distortion
// kicks in earlier and the loop self-limits at lower volume.
static constexpr float FB_SAT_DRIVE = 8.f;
// Feedback build-up ducker: one-pole envelope on prev_wet drives a soft
// 1:∞ attenuation of feedback_amt when the loop level rises above
// THRESHOLD. Slow attack passes transients; slow release lets the loop
// simmer down between gestures instead of re-igniting instantly.
static constexpr float FB_DUCK_THRESHOLD  = 0.20f;  // average wet level above which ducking starts
static constexpr float FB_DUCK_ATTACK_MS  = 500.f;
static constexpr float FB_DUCK_RELEASE_MS = 800.f;
// On-play ducker: dry-input envelope (reuse grain_env, ×10 normalized per
// passive-bass scale) reduces feedback_amt while playing. Instant attack,
// slow release so the duck doesn't pulse between notes. AMOUNT caps the
// reduction so feedback isn't completely killed when playing hard.
static constexpr float ON_PLAY_RELEASE_MS = 400.f;
static constexpr float ON_PLAY_ENV_GATE   = 0.02f;
static constexpr float ON_PLAY_ENV_SCALE  = 10.f;
static constexpr float ON_PLAY_AMOUNT     = 0.5f;  // max GR while playing

// ---------------------------------------------------------------------------
// Mode B bipolar K2 (buffer / direction)
// K2 is bipolar around noon. |K2-0.5| (past the deadzone) = buffer length +
// timescale, as the old unipolar K2 did. Sign = global grain playback
// direction: CW = forward, CCW = backward. Noon deadzone = direct-texture
// passthrough (grain engine bypassed). See docs/MODE_B_DISCOVERY.md.
// ---------------------------------------------------------------------------
constexpr float GRAIN_K2_DEADZONE = 0.06f;  // ±6% around noon → passthrough
// Bias of the CW-character random reverse (SW2-independent). On the forward
// (CW) K2 side a grain reverses with P = k3·this; on the backward (CCW) side
// the bias flips (grains mostly reverse, occasionally play forward).
constexpr float GRAIN_REVERSE_BIAS = 0.6f;

// ---------------------------------------------------------------------------
// Mode B bipolar K3 (character / Clouds density)
// K3 is bipolar around noon. Noon deadzone = neutral single coherent stream;
// CW = character/glitch, CCW = MI-Clouds-style deterministic density. Both
// sides are one continuous grain axis through the noon origin (see below).
// See docs/MODE_B_DISCOVERY.md.
// ---------------------------------------------------------------------------
constexpr float GRAIN_K3_DEADZONE  = 0.06f; // ±6% around noon → neutral stream
// K3 is a single grain axis through the noon origin (GRAIN_NEUTRAL_LEN,
// GRAIN_NEUTRAL_OVERLAP). Both sides depart from that same anchor so crossing
// noon is seamless:
//  CCW  — lengthen/slow: k3mag lerps length neutral→CLOUD_LEN_MAX, overlap held
//         at neutral so the emission RATE falls as grains grow — a long, slow
//         smear that leans on the deep buffer (cloud).
//  CW   — shorten with chaos: gc_sq lerps length neutral→GRAIN_MIN_LEN, overlap
//         thins neutral→1×, scatter/jitter/loops/reverse rise (glitch).
// grain_len = k2_scale · length, so K2's timescale is an overall size zoom.
constexpr float GRAIN_NEUTRAL_LEN     = 14400.f; // noon base grain length (300 ms, ×k2_scale)
constexpr float GRAIN_NEUTRAL_OVERLAP = 2.0f;    // default overlap anchor — smooth minimum for Hann grains; sparse/glitch-friendly
constexpr float GRAIN_OVERLAP_MID_ECHO = 6.0f;   // richer anchor for echo (K2 engaged) + SW2 MID — lets that mode bloom
constexpr float CLOUD_LEN_MAX         = 96000.f; // full-CCW base length (2 s, ×k2_scale) — long slow smear

// SW2 MID pitch re-roll: how many grains share a random pitch before a new one
// is rolled from the ±1 resonance window. Baseline is change-EVERY-grain
// (interval 1), which holds across all of neutral + the CW glitch half — the
// shimmer re-rolls every grain there. Going CCW (cloud) the pitch is HELD longer
// and longer, interval ramping 1 → GRAIN_PITCH_HOLD_MAX at full CCW, so the slow
// smear settles onto stable, tonal pitches. (=1 disables the hold: change
// every grain across the whole range, CCW cloud included.)
constexpr int   GRAIN_PITCH_HOLD_MAX   = 1;

// Per-grain length variation (K3-CW character). Each grain's length is scattered
// BELOW the coarse block base by a skewed random factor, so the loop/stutter
// FREQUENCY (≈ 1/length) feels random instead of locking to the knob value. The
// shortening is GEOMETRIC toward an absolute audio-rate floor (LEN_MIN), so as a
// grain gets short its repeat cycle crosses ~20 Hz into the audio range and the
// CD-hang becomes a pitched buzz (brrr ≈ 100 Hz → kriii ≈ 1 kHz). Skew keeps
// these rare at low K3; DEPTH·glitch_amount sets how far toward LEN_MIN the
// knob reaches — so the knob raises the CEILING (higher, more frequent buzzes)
// while the typical grain stays coarse — contrast, not a uniform speed-up.
//  Repeats are COUPLED to the length: repeats ≈ base_len / this_len, so a short
//  grain gets many reps (sustains the buzz), a long grain gets one.
constexpr float GRAIN_CW_LEN_FLOOR      = 4800.f; // coarse block-base short-end (~100 ms) — the typical grain
constexpr float GRAIN_STUTTER_LEN_MIN   = 32.f;   // shortest per-grain length (~1.5 kHz repeat) — audio-rate buzz
constexpr float GRAIN_LEN_VAR_DEPTH     = 1.0f;   // how far toward LEN_MIN full CW reaches (1 = all the way)
constexpr float GRAIN_STUTTER_REACH_GAMMA = 3.0f; // knob→reach curve: >1 keeps mid-CW percussive, bends to audio-rate only near full CW
constexpr float GRAIN_LEN_VAR_SKEW      = 1.5f;   // >1 → short/buzzing grains stay rare (raises ceiling not floor)
constexpr int   GRAIN_STUTTER_MAX_LOOPS = 200;    // cap on length-coupled repeats (lets a short grain sustain a tone)

// Transient-responsive triggering (Mode B grain engine, K3-CW glitch side only
// — off on the CCW cloud and the K3-neutral passthrough, which stay a clean
// free-running wash). A note-on — a rising edge on the input envelope — fires an
// extra grain burst anchored to the freshly-played note, so the glitch engine
// answers our playing (env-mode feel). Burst grains read the newest ring
// content (delay 0 → the per-grain
// safety floor sets the physical minimum), so they are grains of the note just
// played, pitched by SW2/K1 and shaped by K3 like any grain. Detector thresholds
// are relative to the passive-bass env scale (env_val ≈ 0.02–0.1) — ear-tune.
constexpr float TRANSIENT_SLOW_COEF     = 0.0006f; // baseline follower speed (lower = slower reference level)
constexpr float TRANSIENT_RISE          = 1.8f;    // env must exceed baseline × this to count as an attack
constexpr float TRANSIENT_GATE          = 0.02f;   // min env to trigger (noise-floor guard)
constexpr int   TRANSIENT_REFRACTORY    = 2400;    // re-trigger lockout in samples (~50 ms @ 48k)
constexpr int   TRANSIENT_BURST         = 2;       // grains fired per attack
constexpr int   TRANSIENT_BURST_SPACING = 1600;    // samples between burst grains (~33 ms)

// Hard floor on grain length (safety clamp). The character sweep's own short
// end is 480 samples (≈10 ms), set in the grain_len formula.
constexpr size_t GRAIN_MIN_LEN = 64;

// ---------------------------------------------------------------------------
// ChronoTron3-only: FS2 bypass / panic (not in NitroTron3, which had a hard
// relay bypass). Same values as mnemonic. Tap (released before LONGPRESS) =
// trail bypass: the send is gated, the wet — feedback loop included — rings
// on. Hold >= LONGPRESS = PANIC: bypass + spin the recirculation and the wet
// output down to true silence, then wipe the ring / grains. The escape hatch
// for a K5-CW drone that would otherwise sustain forever through bypass.
// ---------------------------------------------------------------------------
constexpr uint32_t SPRAWL_LONGPRESS_MS  = 450;   // FS2 held beyond this = panic
constexpr float    SPRAWL_PANIC_RISE_MS = 15.f;  // re-engage ramp (click-free, near-instant)
constexpr float    SPRAWL_PANIC_FADE_MS = 120.f; // panic spin-down to silence (fast but click-free)

// ---------------------------------------------------------------------------
// ChronoTron3-only: FS1 tap tempo (not in NitroTron3, which had the preset
// system on FS1). The tap sets the BUFFER LENGTH directly — one tap interval =
// the whole buffer, no subdivisions (mnemonic's divisions are a delay concept;
// here there is only one span). EHX-style arbitration with K2: the last gesture
// wins — a tap overrides the knob, moving K2 past SPRAWL_K2_MOVE_EPS overrides
// the tap. K2 always keeps its direction / noon-deadzone job.
// Length changes are a HARD CUT (the read range simply changes) — deliberately
// no varispeed glide, unlike mnemonic's tape read tap.
// ---------------------------------------------------------------------------
constexpr uint32_t SPRAWL_TAP_MIN_MS  = 100;    // faster than this = ignored (= GRAIN_MIN_RANGE)
constexpr uint32_t SPRAWL_TAP_MAX_MS  = 7000;   // slowest usable tap (8 s ring minus grain headroom)
constexpr uint32_t SPRAWL_TAP_RELEASE_MS = 300; // FS1 released before this = TAP
                                                // (300..SPRAWL_LONGPRESS_MS = no-op deadzone;
                                                //  >= SPRAWL_LONGPRESS_MS = freeze toggle)
constexpr float    SPRAWL_K2_MOVE_EPS = 0.02f;  // K2 travel that cancels a tapped length
constexpr uint32_t SPRAWL_LED1_FLASH_MS = 40;   // LED1 buffer-clock flash width
