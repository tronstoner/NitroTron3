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

// K3/delay decoupling (set false to restore the old coupled behaviour).
// On the CCW cloud side K3 used to stretch the grain length to 2 s, and the echo
// time was floored at the grain length -- so past roughly k3mag 0.3 the echo time
// WAS the grain length and K2 no longer set it. That is two timing controls
// fighting over one parameter, and it reads as "the character knob changes the
// delay time". With this true:
//   * cloud grain length no longer follows K3; it stays on the k2_scale coupling
//     that already existed (GRAIN_NEUTRAL_LEN x 0.5..2.0 = 150..600 ms),
//   * the echo time loses the grain-length floor, so it is max_range/8 (or the
//     tapped value) and nothing else,
//   * cloud grain length is capped to a fraction of the buffer span, which is the
//     job the floor used to do at the short-buffer end.
// CW is untouched: its grains shorten, and glitch moves timing on purpose.
constexpr bool  GRAIN_K3_DECOUPLE_TIME   = true;
constexpr float GRAIN_CLOUD_LEN_MAX_FRAC = 0.5f;  // cloud grain <= this x buffer span

// ---------------------------------------------------------------------------
// K3 CCW = multiband smear (set GRAIN_MB_SMEAR false to restore the plain cloud).
//
// The CCW half stops being "longer grains" and becomes a blend from clean
// playback to smeared playback, the same engine throughout:
//   k3mag 0        1 band, no filter, no spray, grain placement tracks the write
//                  head  ->  bit-for-bit the old neutral stream.
//   0 -> RAMP      spray fades in: the single stream thickens.
//   SPLIT_2/_3     the band count grows to 2 then 3. Each band is its own grain
//                  cloud, band-limited by a per-grain biquad (no band buffers),
//                  with its own grain length and its own scan length.
//   -> 1           the scan fades in fully: each band's grain placement stops
//                  tracking the head and loops a window instead. The scan lengths
//                  are distinct primes, so the bands never re-sync and the texture
//                  keeps evolving without repeating.
// This is the same mechanism as vestige's freeze, reduced to 3 bands to fit the
// 8-voice pool (3 bands x overlap 2 = 6 grains).
//
// NOTE: "smear" (this, a playback character on K3) and "buffer hold" (FS1, which
// stops the ring being written) are separate things and combine freely.
// ---------------------------------------------------------------------------
constexpr bool  GRAIN_MB_SMEAR      = false;  // OFF: K3 CCW is the allpass diffuser instead
constexpr int   GRAIN_MB_MAX_BANDS  = 3;
constexpr float GRAIN_MB_XLO        = 250.f;   // Hz, low/mid crossover
constexpr float GRAIN_MB_XHI        = 2000.f;  // Hz, mid/high crossover
constexpr float GRAIN_MB_OVERLAP    = 2.0f;    // grains per band (Hann @2x = flat sum)
constexpr float GRAIN_MB_SPRAY_RAMP = 0.35f;   // k3mag at which spray is fully in
constexpr float GRAIN_MB_SPLIT_2    = 0.35f;   // k3mag: 1 -> 2 bands
constexpr float GRAIN_MB_SPLIT_3    = 0.65f;   // k3mag: 2 -> 3 bands
// Per-band grain length, as a fraction of the cloud grain length (which K2 sets).
// Rows = band count - 1, cols = band low->high. Ratios follow vestige's
// 150/80/40 ms split, so the whole texture scales with K2 as one unit.
constexpr float GRAIN_MB_LEN_RATIO[GRAIN_MB_MAX_BANDS][GRAIN_MB_MAX_BANDS] = {
    {1.00f, 0.00f, 0.00f},
    {1.00f, 0.27f, 0.00f},
    {1.00f, 0.53f, 0.27f},
};
// Scan lengths (samples). Distinct primes = the bands never re-sync; the low band
// scans the longest window. These ARE the phasing character.
constexpr size_t GRAIN_MB_SCAN[GRAIN_MB_MAX_BANDS][GRAIN_MB_MAX_BANDS] = {
    {11987,     0,     0},
    {11987,  4099,     0},
    {11987,  8419,  4099},
};
// Per-band position spray (samples), widest on the low band.
constexpr size_t GRAIN_MB_SPRAY[GRAIN_MB_MAX_BANDS][GRAIN_MB_MAX_BANDS] = {
    {480,   0,   0},
    {480, 120,   0},
    {480, 240, 120},
};

// ---------------------------------------------------------------------------
// K3 CCW = allpass DIFFUSION (Clouds / Parasites looping-delay model).
//
// A grain cloud can only copy a transient around; an allpass chain dissolves it.
// Parasites drives its 4-stage diffuser straight from the DENSITY knob in
// looping-delay mode, 0..1, as a plain dry/wet on the playback output. K3 CCW
// takes that role here: 0 at noon (bit-identical to today) -> 1 at full CCW.
// The chain sits just before the feedback tap, where Clouds has it, so repeats
// are diffused again on every pass. Wet path only.
// ---------------------------------------------------------------------------
constexpr bool  GRAIN_DIFFUSE      = true;
constexpr float GRAIN_DIFFUSE_MAX  = 1.0f;   // diffuser amount at full K3 CCW

// ---------------------------------------------------------------------------
// Transposed grains keep a SHORT window, whatever the delay time is.
//
// A grain at ratio r reads its source r times as fast as it plays, but the read
// point is anchored to the write head, which advances in real time. So every
// source sample ends up played glen*r/hop = 2*r times (4 at one octave up,
// with overlap 2) -- always, at every setting. Grain length does not change how
// MANY copies there are, only how far apart they land, which is one hop:
//   K2 noon   glen 150 ms -> hop  75 ms (13 Hz) -- fuses, the old-school shifter
//                                                  (this is the tuned reference)
//   K2 mid    glen 293 ms -> hop 146 ms         -- audible repeats
//   K2 CW     glen 600 ms -> hop 300 ms         -- four distinct slaps
// Same artifact throughout; the K2 grain-length coupling is what drags it out of
// timbre and into slapback. The window that makes a good pitch shifter belongs
// to the SHIFTER, not to the delay time, so transposed grains get capped here
// and base_delay is left alone.
//
// At unison this cannot matter: forward rate-1 grains with full Hann windows at
// overlap 2 reconstruct the delayed input exactly, so grain length is inaudible.
// Only transposition, reverse, scatter or stutter break that reconstruction.
//
// Applied on the cloud/neutral side only (glitch_amount < 0.01). The CW glitch
// half moves timing on purpose and is left alone.
// ---------------------------------------------------------------------------
constexpr bool   GRAIN_PITCH_SHORT_GRAINS = true;
// The cap IS the tuned K2-noon length, so that position is untouched (the cap
// cannot bind there) and every deeper K2 setting is pulled down to match it:
//   GRAIN_NEUTRAL_LEN 14400 x k2_scale 0.5 at the K2 deadzone = 7200 = 150 ms,
//   hop 75 ms, so the 4 copies land 75 ms apart exactly as they do live.
// Shorter than this is NOT better: at a 50 ms grain the hop is 25 ms and the
// copies comb at 40 Hz, which reads as metallic.
constexpr size_t GRAIN_PITCH_MAX_LEN      = 7200;  // 150 ms = the tuned live length
// SW2 UP only. There every grain carries the SAME fixed interval, so the 2r
// copies line up and spreading them out is what makes the slapback. In the
// resonance-table modes each grain draws its own pitch, so the copies never
// line up into a slap -- and there the grain rate IS the cloud's rate, which is
// meant to fall as the delay lengthens. Capping it pinned the harmonic cloud at
// a single high rate at every K2 setting. Leaving those modes uncapped restores
// their original coupling exactly (GRAIN_NEUTRAL_LEN x k2_scale, 150-600 ms).
constexpr bool   GRAIN_PITCH_CAP_HARMONY  = false;

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
constexpr float    SPRAWL_PANIC_FADE_MS = 60.f;  // panic spin-down to silence (fast but click-free)

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

// ---------------------------------------------------------------------------
// SW1 MIDDLE — tape/BBD colour (2026-09-17), replacing the event-driven glitch.
// This is VESTIGE's adaptation of the shared degrade engine, not mnemonic's:
// mnemonic modulates its single read tap (a delay-topology trick sprawl has no
// equivalent of — grains have no one tap), whereas vestige carries the engine's
// tape pitch on a short POST-stage warble delay line. That version is
// host-agnostic, which is exactly why it ports here.
// Starting values are vestige's, as the adaptation that already sounds right.
//
// Placement note: sprawl's texture shaper feeds prev_wet, so like the decimator
// and ringmod beside it the colour sits INSIDE the feedback loop — repeats age
// through it, as mnemonic's does.
// ---------------------------------------------------------------------------
static constexpr float  SPRAWL_BBD_FOLD_SCALE = 0.8f;   // BBD fold amount (2.0 = the vestige value this started from)
// Per-instance voicing of the shared engine for THIS host (mnemonic and vestige
// keep the defaults — these are setters, not edits to the MNEMD_* constants).
static constexpr float  SPRAWL_BBD_LPF_SCALE   = 2.0f;  // CCW: open the BBD low-pass well up — the
                                                        // default voicing is far too dark on the grain bus
static constexpr float  SPRAWL_TAPE_DRIVE_SCALE = 2.5f; // CW: harder into the tape saturator (grit —
                                                        // Shape() normalises by drive, so level is flat)
static constexpr float  SPRAWL_TAPE_LEVEL      = 1.30f; // CW output level
// Both sides are normalised so their LOUDEST point equals the noon/clean level
// (= SW1 UP at noon = unity), measured with events and noise off:
//   BBD  at level 1.0 runs 0.80..1.15 (peaks near the deadzone edge) -> 0.87
//   tape at level 1.0 runs 0.51..0.77 (peaks just past noon)         -> 1.30
// The two chains are NOT symmetric: BBD is already near unity on its own,
// tape loses level to the drive normalisation and HF loss, so they need very
// different numbers to end up equally loud. Peak-matched rather than
// average-matched because the peak is what tips the feedback loop early.
static constexpr float  SPRAWL_BBD_LEVEL       = 0.87f; // CCW output level
// Depth compensation: the gain each chain reaches at FULL depth, blended in
// from 1.0 at noon. Both chains lose level as they deepen, so the extremes —
// where this mode is actually used — sat ~3.5 dB below the clean noon level.
// Fixing that with OUTPUT level instead would lift the correct centre too and
// tip the feedback loop early, which is exactly what happened.
static constexpr float  SPRAWL_BBD_DEPTH_COMP  = 1.45f;
static constexpr float  SPRAWL_TAPE_DEPTH_COMP = 1.52f;
// CW: extend the tape travel past its stock endpoint. One knob on the shared
// engine's depth, so every tape parameter reaches further in the same
// proportion — the character at a given position is unchanged, the far end
// just goes on. 1.0 = stock. See MnemDegrade::SetTapeDepthScale.
static constexpr float  SPRAWL_TAPE_DEPTH_SCALE = 1.5f;
// CCW: BBD clock-slip instability — Poisson events that jam the decimator
// clock at a longer integer hold for a few tens of ms, so the static
// decimator lurches like the tape side's dropouts. Starts only past ~9
// o'clock (MNEMD_BBD_SLIP_KNEE) and scales in from there. 0 = off.
static constexpr float  SPRAWL_BBD_SLIP        = 1.0f;
// How much the replay REPLACES the live signal during an event. 1 = you hear
// the stutter alone for the event's duration, 0 = the replay is inaudible.
static constexpr float  SPRAWL_BBD_REPLAY_MIX  = 0.00f;  // TEMP: stutter off — was 0.50f
// Bit-crush on the held BBD sample (scales MNEMD_CRUSH_STEP; 0 = off).
// Coarse amplitude grit towards full CCW, as opposed to the smooth tanh fuzz.
static constexpr float  SPRAWL_BBD_CRUSH       = 0.30f;  // scales MNEMD_CRUSH_STEP (0.008) -> effective step 0.0024
// CCW: continuous clock drift on top of the slip events — a random walk on the
// decimator clock so it never repeats. This is the 'between integers' region
// the engine used to quantise away; wanted here, off for the other hosts.
static constexpr float  SPRAWL_BBD_DRIFT       = 0.00f;  // constant tape warble OFF — the sag below is the instability now
// CCW: scale the slip-event LENGTH to the current echo time instead of fixed
// milliseconds. 0 = fixed (MNEMD_BBD_SLIP_*_MS), 1 = fully scaled. The timing
// stays random either way — this is scale, NOT sync: the wobble keeps the same
// character whether the echo is 200 ms or 3 s, without landing on the grid.
// Reference = the grain read-back depth, or the grain length at K2 noon where
// there is no read-back.
static constexpr float  SPRAWL_BBD_SLIP_SYNC   = 1.0f;
static constexpr size_t SPRAWL_WARBLE_LEN     = 4800;   // 100 ms modulated-delay line (SDRAM)
static constexpr float  SPRAWL_WARBLE_BASE_MS = 3.f;    // fixed base delay = tap centre (~3 ms)
static constexpr float  SPRAWL_WARBLE_LEAK    = 0.99999f;    // leaky integrator (cents->displacement HP)
static constexpr float  SPRAWL_WARBLE_CENTS_TO_RATE = 0.00057762f;  // ln2/1200
static constexpr float  SPRAWL_WARBLE_EASE    = 0.003f; // ~7 ms ease of the wobble back to 0 when idle

// Non-finite guard: how long LED2 strobes after a caught fault (diagnostic).
constexpr int SPRAWL_FAULT_LED_MS = 1000;  // DIAG builds only (CT3_DIAG)
