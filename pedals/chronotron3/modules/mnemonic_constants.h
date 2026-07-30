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
static constexpr float MNEM_TIME_MIN_MS = 50.f;
static constexpr float MNEM_TIME_MAX_MS = 1500.f;
// Knob-time curve (SW2 UP): 1.0 = pure exponential (equal knob degrees = equal
// TIME RATIO, the natural/correct delay-knob feel; noon ~275 ms, ~2.3x per
// quarter-turn). >1 pre-warps toward short delays but risks a dead CCW half —
// leave at 1.0 unless you deliberately want skew.
static constexpr float MNEM_TIME_CURVE = 1.0f;

// Varispeed glide: one-pole coef per sample pulling the read tap toward its
// target. Small = slow, syrupy tape slur; large = snappy. ~1/(coef*sr) sec.
static constexpr float MNEM_GLIDE_COEF = 0.0007f;   // ~30 ms time-constant

// ---------------------------------------------------------------------------
// Tap-division ratios (K1, SW2 MID). Noon = index 5 = 1/1 (quarter = tap).
// CCW shorter, CW longer, exact reciprocal mirror. See concept doc table.
// ---------------------------------------------------------------------------
// Full 11-stop set, noon-centred (index 5 = 1/1 at 12 o'clock with linear
// mapping). CCW = subdivisions (incl. the whole DMB tap-divide set: 16th,
// 8th-triplet, 8th, quarter-triplet, dotted-8th); CW = the reciprocal mirror for
// delays longer than the tap. Used by SW2 MID and, IDENTICALLY, the Edge primary.
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
// Edge secondary ("4th") line: a companion ratio PER K1 stop (octave-up support
// voice), chosen so the primary division, this secondary, and your quarter-note
// playing interlock into a 3-layer rhythm. Index-aligned with MNEM_DIV_RATIOS.
static constexpr float MNEM_EDGE_SECONDARY_RATIOS[MNEM_DIV_COUNT] = {
    0.5f,   // 1/4 prim  -> 1/2  (16th over straight 8th)
    0.5f,   // 1/3 prim  -> 1/2  (3-against-2)
    0.75f,  // 1/2 prim  -> 3/4  (2-against-3 shimmer)
    1.f,    // 2/3 prim  -> 1/1  (triplet across the beat)
    0.5f,   // 3/4 prim  -> 1/2  (the Edge cross-rhythm)
    0.75f,  // 1/1 prim  -> 3/4  (in-time echo + syncopated counter) — NOON
    1.f,    // 4/3 prim  -> 1/1  (broad triplet vs quarter)
    1.f,    // 3/2 prim  -> 1/1  (3:2 over two beats)
    0.75f,  // 2/1 prim  -> 3/4  (long echo + busy 8ve-up fill)
    1.f,    // 3/1 prim  -> 1/1  (slow primary, quarter keeps time up top)
    1.5f,   // 4/1 prim  -> 3/2  (ambient primary + dotted-quarter counter)
};
static constexpr float MNEM_DIV_HYST = 0.015f;  // knob margin to change division stop

// ---------------------------------------------------------------------------
// Feedback (K2, 0 -> self-oscillation) + always-on tape drive
// ---------------------------------------------------------------------------
static constexpr float MNEM_FB_MAX      = 1.30f;  // >1: oscillates; tanh bounds level
static constexpr float MNEM_TAPE_DRIVE  = 1.4f;   // always-on base warmth (K3 degrade engine adds colour on top)
// BBD output makeup (K3 CCW): the BBD LPF rolloff drops perceived loudness as K3
// goes deeper. This lifts the WET OUTPUT only (post feedback loop — balance
// untouched), unity up to KNEE (~9:00 sweetspot) then rising to MAX at full CCW.
// KNEE is in BBD-depth units (0 = centre .. 1 = full CCW); nudge to align with
// the 9:00 spot. MAX = gain at full CCW (drastic per taste). Volume only, no EQ.
static constexpr float MNEM_BBD_OUT_KNEE = 0.33f; // BBD depth where the boost starts (~9:00)
static constexpr float MNEM_BBD_OUT_MAX  = 1.6f;  // output gain at full CCW (1 = off)
// Dedicated feedback-path saturator (analog bloom): compresses the RECIRCULATION
// only, so repeats warm + even out while the fresh input (first repeat) stays
// present. Higher = compresses earlier / more. Unity small-signal, bounded.
static constexpr float MNEM_FB_DRIVE = 3.0f;
// Level control is the SLOW-attack build-up ducker below (MNEM_FB_DUCK_*). The
// earlier FAST-attack ducker caused the "feedback-drown" (it reacted to normal
// repeats + K1 sweeps); the slow attack is what makes it work — see that block.
//
// Controlled-decay shaping: a downward expander on the feedback path. Full
// feedback while the signal is loud (initial repeats stay strong), but the loop
// gain drops once the tail falls below the knee -> the tail decays FASTER instead
// of ringing forever (rhythmically pronounced echoes, no ultra-long trail; good
// on bass). AMT is the nudge: 0 = pure bloom (as before), higher = more controlled.
static constexpr float MNEM_FB_CTL_AMT  = 0.30f;  // 0 = off (bloom) · 1 = strongly controlled
static constexpr float MNEM_FB_CTL_KNEE = 0.08f;  // level below which the tail accelerates
static constexpr float MNEM_FB_CTL_MS   = 60.f;   // feedback envelope time (decay-rate detector)

// Feedback build-up ducker (Sprawl-proven) — the loop level control. A SLOW-attack
// envelope on the loop read drives a 1:inf attenuation of the feedback gain once
// the SUSTAINED loop level exceeds THR, so runaway/self-oscillation is capped to a
// controlled simmer (musical drone, never ear-piercing) at ANY pitch. The slow
// attack is the whole trick: transients, normal repeats and K1 varispeed sweeps
// are all faster than it, so they pass untouched (a FAST attack here was the old
// "drown" regression). Acts on the feedback GAIN, in the loop — not the EQ, not
// the dry, not a static output ceiling; self-scales with the loop level.
static constexpr float MNEM_FB_DUCK_THR    = 0.50f;  // sustained loop level the ducker holds to (raise = looser/louder, toward Sprawl's 0.20)
static constexpr float MNEM_FB_DUCK_ATK_MS = 500.f;  // attack (matches Sprawl; slower = passes more transient before catching)
static constexpr float MNEM_FB_DUCK_REL_MS = 800.f;  // slow release (loop simmers down between gestures)

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
// Delay-time target smoothing: pre-smooths base_delay_ before the varispeed glide
// so the read-tap velocity (= pitch) stays continuous when K1 moves. Longer =
// less jitter but more lag before the glide. Pitch-sensitive, so a touch slower.
static constexpr float MNEM_TIME_SMOOTH_MS = 12.f;

// ---------------------------------------------------------------------------
// Degrade character (K3): full bipolar BBD/Tape engine in mnemonic_degrade.h.
// The tape speed-irregularity it produces (in cents) is integrated here into a
// read-tap position offset (speed deviation -> tape displacement). The leak is a
// high-pass on the integrator so a DC speed offset can't drift the delay time;
// it passes the whole mod band (wow 0.7 Hz .. flutter 11.7 Hz, OU, snags).
// ---------------------------------------------------------------------------
static constexpr float MNEM_FLUTTER_LEAK  = 0.99999f;
static constexpr float MNEM_CENTS_TO_RATE = 0.00057762f;  // ~ ln2/1200 (cents -> fractional speed)

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
// Until a real tap tempo exists, the division modes seed the quarter from K1's
// knob-time position x this ratio (1.0 = the knob-time value IS the quarter).
static constexpr float    MNEM_TAP_INIT_RATIO = 1.0f;
static constexpr uint32_t MNEM_TAP_MAX_MS    = 2000; // slowest usable tap interval
static constexpr uint32_t MNEM_TAP_MIN_MS    = 40;   // ignore faster than this
static constexpr int      MNEM_TAP_MEDIAN_N  = 4;    // recent intervals for the median

// ---------------------------------------------------------------------------
// Tape gesture (FS1 hold; SW1 UP = spin-up). DOWN is now FREEZE (see below).
// ---------------------------------------------------------------------------
static constexpr float MNEM_GEST_UP_TIMEFAC   = 0.10f; // spin-up shortens delay -> pitch up
static constexpr float MNEM_GEST_UP_FB        = 1.1f; // feedback while spinning up
static constexpr float MNEM_GEST_ATK_MS       = 1100.f;// ramp-in while held (the pitch dive)
static constexpr float MNEM_GEST_REL_MS       = 1400.f;// slew back on release

// ---------------------------------------------------------------------------
// Freeze (FS1 hold, SW1 = DOWN). EHX-Freeze-style: while held, the clean input
// is written continuously into a short circular ring that keeps only the last
// FREEZE_WIN_MS; on release (past long-press) that fragment is committed and
// grain-looped as a sustained parallel voice, summed into the wet output OUTSIDE
// the feedback loop. Two-slab pointer-swap (like the loop) so a re-freeze
// captures cleanly while the current freeze keeps playing. Playback uses 2
// half-overlapped full-Hann grains so the ring's wrap seam is crossfaded (no
// click), NOT avoided. Latches until re-frozen or cleared by FS2 panic.
// ---------------------------------------------------------------------------
static constexpr float  MNEM_FREEZE_WIN_MS  = 400.f;   // captured fragment length
static constexpr size_t MNEM_FREEZE_SAMPLES = (size_t)(MNEM_FREEZE_WIN_MS * 0.001f * MNEM_SR); // 19200
static constexpr int    MNEM_FREEZE_GRAINS  = 2;       // 2x overlap -> COLA-smooth sustain
static constexpr float  MNEM_FREEZE_GAIN    = 1.0f;    // freeze voice level into the wet sum
static constexpr float  MNEM_FREEZE_AMP_MS  = 30.f;    // start/stop de-click ramp on the freeze voice

// ---------------------------------------------------------------------------
// Hold / loop (SW1 MID)
// ---------------------------------------------------------------------------
// Loop commit is governed by MNEM_LONGPRESS_MS (a press held that long is a loop,
// not a tap); no separate min-length gate.
static constexpr float    MNEM_LOOP_XFADE_MS = 6.f;  // seam crossfade at the wrap
static constexpr float    MNEM_LOOP_FADE_MS  = 8.f;  // play start/stop de-click ramp

// ---------------------------------------------------------------------------
// Panic gesture (FS2 long-press). NORMAL bypass (short tap) lets the delay
// trail ring out naturally — that's the whole point of a delay. The PANIC
// long-press is the always-available escape: it forces bypass AND kills the
// loop + feedback + tail. A panic envelope multiplies BOTH the wet output and
// the feedback recirculation, spinning down to TRUE silence click-free (works
// even at fb>=1, because the recirculation itself is throttled). Re-engage
// (any tap) rises over a fixed short de-click ramp and starts from a clean slate.
// ---------------------------------------------------------------------------
static constexpr float MNEM_PANIC_RISE_MS = 15.f;  // re-engage ramp (click-free, near-instant)
static constexpr float MNEM_PANIC_FADE_MS = 120.f; // panic spin-down to silence (fast but click-free)

// Bypass noise-duck: in NORMAL bypass the trail rings out naturally, but the
// medium hiss would otherwise sustain a bed forever. A trail-envelope follower
// watches the wet read; once it decays toward the engine's own noise floor
// (threshold = floor x MARGIN, so it tracks K3), the degrade engine's noise
// injection is ducked to zero — the hiss dies WITH the trail, not after it.
// Only active in bypass (tape character between notes is kept during play).
static constexpr float MNEM_NGATE_MARGIN     = 4.f;   // open threshold = noise floor x this
static constexpr float MNEM_NGATE_ENV_ATK_MS = 2.f;   // trail follower attack
static constexpr float MNEM_NGATE_ENV_REL_MS = 60.f;  // trail follower release
static constexpr float MNEM_NGATE_OPEN_MS    = 8.f;   // duck re-opens (noise back) fast when trail returns
static constexpr float MNEM_NGATE_CLOSE_MS   = 250.f; // duck closes slowly so it fades with the tail

// ---------------------------------------------------------------------------
// Edge mode (SW2 DOWN) = SW2 MID + one secondary line. The PRIMARY (colored) line
// is bit-identical to SW2 MID (delay = quarter x K1 division, full K4/K5 + drive
// + K3). Edge literally adds ONE thing: a SECONDARY line whose ratio is the
// per-stop companion (MNEM_EDGE_SECONDARY_RATIOS), made deliberately distinct —
// octave-up (granular pitch shifter on its INPUT, outside its feedback so repeats
// don't keep climbing), NO filter, NO K3, own feedback loop, quieter. Three
// layers interlock: your quarter playing + primary division + 8ve-up secondary.
// ---------------------------------------------------------------------------
static constexpr float MNEM_EDGE_DIV_GAIN = 0.85f;  // primary (division) line level
static constexpr float MNEM_EDGE_SEC_GAIN = 0.50f;  // secondary line level — quieter

// Secondary line voice: a clean lo-fi "telephone" delay for rhythmic counterpoint.
// Just a mid band-pass on the fresh input (outside its feedback loop) — the band
// alone gives the separation; no drive/shaper needed. Band center follows K4/K5
// slightly (~25%) for tonal cohesion without leaving the mids.
static constexpr float MNEM_SEC_HP_HZ      = 350.f;  // telephone band floor (base, pre-follow)
static constexpr float MNEM_SEC_LP_HZ      = 2500.f; // telephone band ceiling (base, pre-follow)
static constexpr float MNEM_SEC_FOLLOW_OCT = 0.5f;   // K4 tilt shifts band +-0.5 oct (~25% of the main travel)
static constexpr float MNEM_SEC_FOLLOW_NAR = 0.25f;  // K5 narrows the band a touch (follow)

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------
static constexpr float MNEM_LED_CLOCK_ON_MS = 40.f;  // LED1 delay-clock blink width
