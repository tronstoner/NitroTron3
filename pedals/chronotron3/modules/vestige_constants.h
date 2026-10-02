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
// Topology (SW1: UP = 1 voice · MIDDLE = 6 voices · DOWN = freeze)
// ---------------------------------------------------------------------------
static constexpr int    VESTIGE_MAX_VOICES   = 3;   // voiced pool ceiling (SW1 MIDDLE). Was 6, then 4.
// 2026-09-29: at 4 voices with K1 toward the upper octave, the pedal showed
// digital distortion and glitches (suspected CPU overload). 3 voices also fits the
// grain cap in every state, including a voice fading out during a replacement.
// Grain budget: one playback stream = 2 overlapping grains, and while K1 sits
// between noon and an end each voice plays TWO streams (clean + shifted) to
// crossfade them, i.e. 4 grains per voice. At 6 voices that is 24 against
// VESTIGE_MB_GRAIN_CAP = 16, so grains were refused and the crossfade thinned.
// At 4 voices it is exactly 16 in steady state; during a replacement (old voice
// fading out while the new one fades in) it is 20, so some grains are still
// refused for the length of the K5 fade. 3 voices would stay within 16 in every
// state. 4 is being tried on hardware to see whether that transient is audible.
static constexpr int    VESTIGE_VOICE_SPARES = 3;   // spare slabs for in-flight fade-outs / crossfades
static constexpr int    VESTIGE_VOICE_SLABS  = VESTIGE_MAX_VOICES + VESTIGE_VOICE_SPARES; // voiced slabs
static constexpr int    VESTIGE_FRIP_SLOT    = VESTIGE_VOICE_SLABS;      // frippertronics buffer (ARCHIVED — see below)
static constexpr int    VESTIGE_REC_SLOT     = VESTIGE_VOICE_SLABS + 1;  // dedicated record scratch
static constexpr int    VESTIGE_LOOP_SIDE_SLOTS = VESTIGE_VOICE_SLABS + 2; // slots backed by vestige_slab (loop side)
// FREEZE side (SW1 DOWN): its own pool of slots, backed by its own SDRAM slab
// (vestige_freeze_slab, sized VESTIGE_FREEZE_CAP per slot — see the FREEZE
// block below). The loop side and the freeze side never share memory: SW1
// UP/MIDDLE read and write only slots [0, VESTIGE_LOOP_SIDE_SLOTS), SW1 DOWN
// only [VESTIGE_FREEZE_SLOT0, VESTIGE_SLOTS). The freeze pool has as many
// playback slots as the voiced pool so its replace-crossfade / voice-steal
// headroom is exactly what freeze had when it borrowed the voiced slabs.
static constexpr int    VESTIGE_FREEZE_SLABS    = VESTIGE_VOICE_SLABS;             // freeze playback slots
static constexpr int    VESTIGE_FREEZE_SLOT0    = VESTIGE_LOOP_SIDE_SLOTS;         // first freeze playback slot
static constexpr int    VESTIGE_FREEZE_REC_SLOT = VESTIGE_FREEZE_SLOT0 + VESTIGE_FREEZE_SLABS; // freeze record scratch
static constexpr int    VESTIGE_SLOTS           = VESTIGE_FREEZE_REC_SLOT + 1;     // total slots (both sides)
// ARCHIVED (stage 0 of docs/ChronoTron3/vestige-onward-rework-plan.md): the K1
// voice-count / frippertronics mapping is retired. The two thresholds below are
// kept only so the unwired frippertronics code (vestige.h: fripp_mode_,
// EnterFrippertronics/LeaveFrippertronics, the frip branches in Process) can be
// revived by restoring the K1 block in Controls(). Pattern:
// docs/ChronoTron3/impulse resonator - armitage/ARMITAGE_ARCHIVED.md.
static constexpr float  VESTIGE_K1_NOON_LO  = 0.44f;  // [retired] below → voiced, more voices toward CCW
static constexpr float  VESTIGE_K1_NOON_HI  = 0.56f;  // [retired] above → frippertronics region
static constexpr int    VESTIGE_GRAINS      = 48;    // shared grain pool (multiband freeze = 3 bands/voice → more grains)

// ---------------------------------------------------------------------------
// Grain smoothness macro — formerly K3: looper (CCW / 0) → freeze (CW / 1).
// The K3 blend is RETIRED (rework stage 0). The two ends survive as fixed modes:
// the loop modes (SW1 UP/MIDDLE) run the CCW end, freeze (SW1 DOWN) runs the CW
// end. Nothing in between is reachable any more; the values are unchanged.
// ---------------------------------------------------------------------------
static constexpr size_t VESTIGE_CCW_GRAIN_LEN = 19200;  // 400 ms — long grain, looper end
static constexpr size_t VESTIGE_CW_GRAIN_LEN  = 4800;   // 100 ms — tonal freeze grain (30 ms was buzzy)
// Freeze character: the read head slows to a standstill and grains read a NARROW
// window around it so the overlap phases against itself (consistent), rather than
// scattering across the whole buffer (random). Tune by ear.
static constexpr size_t VESTIGE_FREEZE_SPRAY  = 1200;   // ±25 ms phasing spray at full freeze
static constexpr float  VESTIGE_FREEZE_JITTER = 0.15f;  // small scheduler jitter at freeze
// K3 travel: a small zone at the very CCW end is the normal forward loop; above
// it the head auto-scrubs BACKWARD, decelerating to a deterministic freeze
// anchored toward the END of the buffer (a grain scan-range in). Anchor and
// scrub speed scale with K3.
static constexpr float  VESTIGE_K3_LOOP_ZONE  = 0.05f;  // fully-CCW forward-loop zone
static constexpr size_t VESTIGE_GRAIN_MIN_LEN = 256;
// Short-buffer artifacts (see docs/ChronoTron3/SHORT_BUFFER_PLAN.md):
// seam crossfade (recorded as an overhang past the loop, so timing stays exact)
// + a spray clamp for very short loops.
static constexpr size_t VESTIGE_SEAM_XFADE_MAX = 240;   // ~5 ms seam crossfade (min to kill clicks)
static constexpr size_t VESTIGE_SHORT_LEN      = 4800;  // ~100 ms: clamp spray below this
static constexpr float  VESTIGE_CCW_OVERLAP   = 2.0f;   // Hann overlap-add sums flat → seamless loop
static constexpr float  VESTIGE_CW_OVERLAP    = 3.0f;   // denser cloud so short grains fuse
static constexpr size_t VESTIGE_MIN_INTERVAL  = 32;     // scheduler floor (samples)
static constexpr size_t VESTIGE_MIN_LOOP_SAMPLES = 240; // 5 ms shortest capture (short FS2 tap)

// ---------------------------------------------------------------------------
// K2 = T (the master period, below) + DIRECTION (bipolar). Magnitude away from
// noon sets T, which on the loop side is the ceiling on capture length:
// recording auto-stops and playback begins the moment T is reached. Log taper
// so the short end has usable resolution.
//   noon = VESTIGE_T_MIN_SAMPLES (100 ms — see the T block for why)
//   full CCW / full CW = VESTIGE_T_MAX_SAMPLES (8 s)
// An FS1 tap overrides the magnitude until K2 moves (see T block).
// Sign selects playback direction: past the CCW dead zone the loop plays in
// REVERSE (head walks backward, grains read backward); noon + CW = forward.
// Inert in freeze mode (SW1 DOWN) — see the freeze block below.
// (Was the VESTIGE_K4_MAXLEN test build; the mechanism is unchanged, only the
// knob and the added direction half.)
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_K2_DEADZONE = 0.06f;   // ±6% around noon: shortest length, forward

// ---------------------------------------------------------------------------
// T — THE MASTER PERIOD (rework stage 1, plan §4.3). K2's magnitude (above) or
// an FS1 tap sets it; on the loop side it is the maximum capture length. T is a
// PERIOD only: there is no running grid and no global downbeat — every capture
// anchors its own grid at its own start (stage 2), and LED1 flashes T from the
// most recent capture start.
//
// Range. Hard floor: stage 2 can quantise a capture down to T/8, and a loop
// shorter than VESTIGE_GRAIN_MIN_LEN (256) is muted by the grain engine, so
// T >= 8*256 = 2048 samples (42.7 ms). Chosen floor: 100 ms (4800 samples):
//   - T/8 = 600 samples keeps the FULL 240-sample seam crossfade on the shortest
//     loop (SeamXfadeLen shrinks it to L/2 below 480 samples, i.e. T < 80 ms);
//   - 100 ms is sprawl's tap floor (SPRAWL_TAP_MIN_MS) — the same FS1 gesture on
//     the same pedal accepts the same range;
//   - it is a period (600 BPM quarter / 150 BPM bar-of-16ths), not a click.
// Ceiling: the 8 s loop buffer. K2 noon = T_MIN (was one grain, 5.3 ms, as a
// length test); full CCW / CW = T_MAX, log taper between.
// ---------------------------------------------------------------------------
static constexpr uint32_t VESTIGE_T_MIN_MS      = 100;
static constexpr size_t   VESTIGE_T_MIN_SAMPLES = (size_t)(VESTIGE_T_MIN_MS * 48);   // 4800 @48k
static constexpr size_t   VESTIGE_T_MAX_SAMPLES = VESTIGE_LOOP_MAX_SAMPLES;          // 384000 = 8 s
static constexpr uint32_t VESTIGE_T_MAX_MS      = (uint32_t)(VESTIGE_LOOP_SECONDS * 1000.f); // 8000
static_assert(VESTIGE_T_MIN_SAMPLES / 8 >= VESTIGE_GRAIN_MIN_LEN,
              "T/8 must stay a playable loop (>= VESTIGE_GRAIN_MIN_LEN)");
static_assert(VESTIGE_T_MIN_SAMPLES / 8 >= 2 * VESTIGE_SEAM_XFADE_MAX,
              "T/8 must keep the full seam crossfade");
// FS1 tap tempo — sprawl's arbitration, exactly: the interval between two
// DOWN-presses IS T; a press released before TAP_RELEASE is a tap (longer = no-op,
// chain untouched); intervals outside [T_MIN, T_MAX] are ignored (and start a
// new chain). Moving K2 past K2_MOVE_EPS cancels the tapped T; K2 keeps its
// direction job either way.
static constexpr uint32_t VESTIGE_TAP_RELEASE_MS = 300;    // = SPRAWL_TAP_RELEASE_MS
// Tap tempo needs TWO AGREEING intervals (three taps): a new interval only sets T
// when it is within this fraction of the previous one; T is then their mean.
// Why: with loops following T, a single stray press after a pause paired with the
// last tap of the previous sequence (anything under 8 s is a valid interval) and
// briefly re-timed every playing loop to that accidental T. A stray interval never
// agrees with its neighbours, so it now changes nothing. (Sprawl keeps two taps.)
static constexpr float    VESTIGE_TAP_AGREE      = 0.15f;
static constexpr float    VESTIGE_K2_MOVE_EPS    = 0.02f;  // = SPRAWL_K2_MOVE_EPS (raw knob travel)
// LED1 clock: one flash per T, anchored to the most recent capture start.
static constexpr uint32_t VESTIGE_LED1_FLASH_MS  = 40;     // = SPRAWL_LED1_FLASH_MS
// BBD fold brightness in the looper: scales the regenerated fold mixed on top of
// the dark body. >1 = more mids/highs + grit (clarity). 1 = mnemonic default.
// ARCHIVED with the degradation colour: MnemDegrade is held clean (depth 0) —
// the rework gives K4 to capture sensitivity, so the colour engine has no knob.
// Revive by mapping a knob to degrade_.SetDepth() in Controls().
static constexpr float  VESTIGE_BBD_FOLD_SCALE = 2.0f;

// ---------------------------------------------------------------------------
// FREEZE (SW1 DOWN) — fixed, not blended. Values pinned from mnemonic's freeze
// (the one the builder signed off on): 400 ms capture window, 3 bands, 250 Hz /
// 2 kHz crossovers, per-band grain 150/80/40 ms, coprime scans 11987/8419/4099,
// spray 480/240/120. The crossovers come from VESTIGE_MB_XLO/XHI and the three
// per-band rows from the N=3 row of VESTIGE_MB_{GLEN,SCAN,SPRAY} below — they
// already hold exactly those numbers, so freeze only has to pin the band count
// and stop reading K3. See docs/ChronoTron3/vestige-onward-rework-plan.md §4.4.
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_FREEZE_WIN_MS   = 400.f;   // captured fragment length (= MNEM_FREEZE_WIN_MS)
static constexpr size_t VESTIGE_FREEZE_SAMPLES  = (size_t)(VESTIGE_FREEZE_WIN_MS * 0.001f * VESTIGE_SR); // 19200
static constexpr int    VESTIGE_FREEZE_BANDS    = 3;       // pinned band count (was the K3 chaos ramp)
static constexpr float  VESTIGE_FREEZE_POS_FRAC = 0.f;     // read anchor in the window (0 = start)
// Freeze capture window: skip the pick attack (EHX Freeze style — a freeze holds
// the tone, not the transient): the grains read from this far into the capture,
// less when that would leave under MIN_KEEP of tone. (A silence-ended capture
// ends where the sound stopped, like the loop side: no silent tail.)
static constexpr float  VESTIGE_FREEZE_ATTACK_SKIP_MS = 100.f;
static constexpr float  VESTIGE_FREEZE_MIN_KEEP_MS    = 120.f;
// Freeze slab sizing. A freeze grain is FROZEN: EmitBandGrain clamps its window
// inside [0, L-glen], so the deepest read (GrainVoice's interpolation partner)
// is index L — the freeze never reads across the loop seam and needs no
// head-continuation guard. What it does need past L is room for the recorded
// seam-crossfade overhang (EndRecording keeps writing SeamXfadeLen(L) <=
// VESTIGE_SEAM_XFADE_MAX samples past the loop end before the commit), which
// also covers the index-L read. So the guard is exactly the overhang.
static constexpr size_t VESTIGE_FREEZE_GUARD = VESTIGE_SEAM_XFADE_MAX;                   // 240
static constexpr size_t VESTIGE_FREEZE_CAP   = VESTIGE_FREEZE_SAMPLES + VESTIGE_FREEZE_GUARD; // 19440 per slot

// ---------------------------------------------------------------------------
// Multiband granular freeze (K3 CW freeze region) — the EHX-style evolving freeze
// ported from mnemonic, made POLY (per voice). In the freeze region each voice's
// single loop buffer is granulated in N bands via PER-GRAIN band filters (no extra
// band buffers — host-verified identical to pre-filtering, so memory stays flat and
// loops keep full length). Each band scans at a COPRIME length so the bands never
// re-sync → dense, continuously evolving freeze. The K3 freeze-point sweep still
// sets WHERE in the buffer (base position); the bands add the incommensurate
// movement + spectral split. Flag off = the old single-stream pinned-anchor freeze
// (fallback). Tune by ear.
//
// Band count is adaptive: up to VESTIGE_MAX_BANDS, scaled down by live-voice CPU
// budget and by the K3 chaos ramp (see below). The filterbank for each band count
// N is built at init from the XLO..XHI crossovers, log-spaced (N-1 crossovers):
// band 0 = LP, mids = BP, band N-1 = HP. This reproduces the old 1/2/3-band splits
// exactly and extends to 4/5. Per-band grain length / scan length / spray are the
// tunable tables below, indexed [N-1][band] (low band first).
static constexpr bool   VESTIGE_MB_FREEZE   = true;
static constexpr int    VESTIGE_MAX_BANDS   = 5;    // ceiling for the freeze filterbank
static constexpr float  VESTIGE_MB_XLO = 250.f, VESTIGE_MB_XHI = 2000.f;   // Hz crossover span (log-spaced within)
static constexpr float  VESTIGE_MB_OVERLAP = 2.0f;      // grains per band (Hann @2x = COLA-smooth, lowest CPU).
                                                        // NOTE: total freeze grains = voices x bands x overlap.

// Per-band grain length (samples @48k). Row = band count N-1, col = band (low→high).
// N=1..3 rows preserve the original LO/MID/HI values exactly; 4/5 interpolate.
static constexpr size_t VESTIGE_MB_GLEN[VESTIGE_MAX_BANDS][VESTIGE_MAX_BANDS] = {
    {7200,    0,    0,    0,    0},   // 1 band : 150 ms
    {7200, 1920,    0,    0,    0},   // 2 bands: 150 / 40 ms
    {7200, 3840, 1920,    0,    0},   // 3 bands: 150 / 80 / 40 ms
    {7200, 4320, 2880, 1920,    0},   // 4 bands: 150 / 90 / 60 / 40 ms
    {7200, 5280, 3840, 2640, 1920},   // 5 bands: 150 / 110 / 80 / 55 / 40 ms
};
// Coprime scan lengths — THE phasing ratios. Distinct primes so the bands can never
// re-sync (combined period = product of the lengths). Low band = longest scan.
// N=1..3 rows preserve the original values; 4/5 add primes between the extremes.
static constexpr size_t VESTIGE_MB_SCAN[VESTIGE_MAX_BANDS][VESTIGE_MAX_BANDS] = {
    {11987,     0,     0,     0,     0},
    {11987,  4099,     0,     0,     0},
    {11987,  8419,  4099,     0,     0},
    {11987,  9973,  6113,  4099,     0},
    {11987,  9973,  8419,  6113,  4099},
};
// Per-band phasing spray (samples). Low band = widest. N=1..3 rows preserve originals.
static constexpr size_t VESTIGE_MB_SPRAY[VESTIGE_MAX_BANDS][VESTIGE_MAX_BANDS] = {
    {480,   0,   0,   0,   0},
    {480, 120,   0,   0,   0},
    {480, 240, 120,   0,   0},
    {480, 320, 200, 120,   0},
    {480, 360, 240, 180, 120},
};
// Hard cap on concurrent freeze grains = the CPU ceiling. Above it, grains drop
// (freeze thins) instead of the callback overrunning. Measured: baseline ~24% +
// ~2.8%/grain, so ~20 grains keeps steady load ~80% (safe). SDRAM is cached, so
// the cost is compute (grain count), not memory. 5 bands x 1 voice x overlap 2 = 10.
static constexpr int    VESTIGE_MB_GRAIN_CAP = 16;
// Adaptive bands by live-voice count (poly CPU scaling) — CPU-safe schedule: only
// the 1-voice freeze gets the full 5 bands. <=5BAND_MAX voices → 5, <=3BAND_MAX → 3,
// <=2BAND_MAX → 2, else 1 full-band cloud (no filter). Keeps every voice audible
// within budget. Overlap stays 2 (min for click-free Hann OLA). The effective count
// is min(this budget, the K3 chaos ramp below).
static constexpr int    VESTIGE_MB_5BAND_MAX_VOICES = 1;
static constexpr int    VESTIGE_MB_3BAND_MAX_VOICES = 2;
static constexpr int    VESTIGE_MB_2BAND_MAX_VOICES = 4;

// ---------------------------------------------------------------------------
// K3 unified engine: order -> chaos -> focus -> sweep (one granular engine).
// RETIRED as a control (rework stage 0): K3 no longer drives this. The loop
// modes pin the s=0 end (chaos 0, focus 0, gscale = GSCALE_CCW, 1 band); freeze
// pins the s>=0.5 end (chaos 1, focus 1, gscale 1, VESTIGE_FREEZE_BANDS). The
// development curve and chaos thresholds below are therefore unused, kept only
// for reviving the blend.
// ---------------------------------------------------------------------------
// The multiband granular engine now spans the WHOLE K3 travel (not just the CW
// freeze). K3 morphs ONE cloud continuously — no engine switch at noon:
//   CCW  (chaos 0): 1 full-band cloud, long grains, read head follows the loop
//                   forward, no scatter  ->  the CLEAN loop.
//   ->noon (chaos rises to 1): grains shrink + multiply (density up), bands split
//                   in, positions scatter across the buffer  ->  dense multiband
//                   chaos, the loop breaking up.
//   noon (focus -> 1): scatter COLLAPSES onto the loop start + the coprime scan
//                   engages  ->  order from chaos = the evolving freeze at start.
//   noon->CW: freeze point sweeps start->end (unchanged; freeze half is identical
//                   to the old, loved multiband freeze).
// Derived params: chaos = min(s/0.5, 1); focus = smoothstep over [FOCUS_START,0.5]
// then 1; freeze_frac = (s-0.5)*2 above noon else 0. At s>=0.5 every lever below
// reduces to the prior freeze exactly, so the CW half is byte-for-byte preserved.
// Break-up development curve over CCW->noon. prog = s*2 (linear 0->1); the actual
// development (grain-shorten + band-split + focus/scan smear) = prog^CURVE. CURVE<1
// front-loads it so it gets interesting EARLY (near 9:00) instead of a dull first
// third; the very CCW corner still starts clean. 1 = linear, >1 = later/duller.
static constexpr float  VESTIGE_K3_DEVELOP_CURVE    = 0.5f;
static constexpr float  VESTIGE_K3_GSCALE_CCW      = 2.667f; // grain-len x at CCW (low band 7200 -> ~19200 = clean loop)
// Chaos-ramp band-split thresholds (all capped by the voice budget). The 2/3-band
// points are unchanged so the CCW->noon break-up feels identical at <=3 bands; the
// 4th/5th split in near noon so the freeze half (chaos pinned at 1.0) reaches the
// full band count. Only the 1-voice freeze actually uncaps to 5.
static constexpr float  VESTIGE_K3_CHAOS_2BAND     = 0.33f; // above this = 2 bands
static constexpr float  VESTIGE_K3_CHAOS_3BAND     = 0.66f; // above this = 3 bands
static constexpr float  VESTIGE_K3_CHAOS_4BAND     = 0.80f; // above this = 4 bands
static constexpr float  VESTIGE_K3_CHAOS_5BAND     = 0.92f; // above this = 5 bands

// ---------------------------------------------------------------------------
// Footswitch timing. FS2 is the only live footswitch: tap = capture + playback
// on/off, hold = buffer hold. Same values the retired FS1 stop/clear pair used.
// FS1 is free (reserved for tap tempo, stage 1).
// ---------------------------------------------------------------------------
static constexpr uint32_t VESTIGE_FS_TAP_MAX_MS = 350;  // nominal tap ceiling (see note in vestige.h)
static constexpr uint32_t VESTIGE_FS_HOLD_MS    = 700;  // >= this while held = toggle buffer hold

// ---------------------------------------------------------------------------
// Auto capture (always on for SW1 UP / MIDDLE / DOWN while engaged and not held)
// ---------------------------------------------------------------------------
static constexpr float    VESTIGE_ENV_COEF        = 0.008f; // input |env| one-pole (~60 Hz; 0.002 was too lazy for onsets/short samples)
// Gate meter: what the capture gate reads for its start, phrase-end (silence)
// and re-arm decisions.
//   0 = env_ itself (the behaviour before 2026-09-29)
//   1 = fix A: follows env_ up instantly, falls slowly (VESTIGE_GATE_RELEASE_MS)
//   2 = fix B: peak hold — not implemented yet
// Why: env_ is a ~61 Hz one-pole, so on LOW notes it still carries the rectified
// ripple at twice the note frequency — ~2.3x peak to valley at 41 Hz, ~2.8x at
// 31 Hz. That is more than the gate's 1/VESTIGE_AUTO_HYST = 1.8x hysteresis, so
// near threshold it crosses BOTH levels every ripple cycle: the 80 ms silence
// never builds up (captures run on to the T ceiling), and the ripple peaks
// re-open the gate on the decaying tail (the same note captured again).
// The onset detector and the phrase-end TIMESTAMP stay on the fast env_, so
// onset timing and recorded lengths are unchanged; only the gate's decisions
// use the gate meter. Mode 1's cost: phrase ends are decided later, so two stabs
// need a longer pause between them to become two captures.
static constexpr int      VESTIGE_GATE_ENV_MODE   = 1;
static constexpr float    VESTIGE_GATE_RELEASE_MS = 40.f;   // mode 1: fall time constant
static_assert(VESTIGE_GATE_ENV_MODE == 0 || VESTIGE_GATE_ENV_MODE == 1,
              "VESTIGE_GATE_ENV_MODE 2 (peak hold) is not implemented yet");
// Re-arm after EVERY capture end, not only after the T ceiling. While the
// re-arm block is on, only an ONSET (a sudden jump, see VESTIGE_ONSET_*) may
// start the next capture; plain level changes may not. The block lifts once
// the gate meter has fallen to VESTIGE_REARM_DEEP x the close level — i.e. the
// string has really gone quiet.
// Why: bass strings BEAT — two close partials make the level of one ringing
// note swell and dip slowly (~1 Hz). Near the threshold a swell rises by more
// than the gate's 1.8x hysteresis and re-opened the gate on the same note's
// tail. The gate meter's slow fall cannot help (the swell is far slower than
// 40 ms); the onset detector ignores slow changes by design and catches a new
// pluck. Cost: a very soft note with no attack, played while the previous one
// still rings faintly (above this level), does not start a capture.
static constexpr bool  VESTIGE_REARM_EVERY_END = true;
static constexpr float VESTIGE_REARM_DEEP      = 0.3f;   // x close level
// Capture open threshold: a constant — the builder's K4 setting (8:00-8:30,
// bass and guitar alike) from when K4 was the sensitivity knob. K4 is the
// degradation colour again.
static constexpr float    VESTIGE_AUTO_THRESH     = 0.011f;
// K4 = degradation colour: CCW half BBD, CW half tape, clean within this much
// of noon (remapped knob units; the colour depth starts from 0 past it).
static constexpr float    VESTIGE_K4_DEADZONE     = 0.06f;
// Deep BBD end (K4 CCW): input gain into the degrader, 0 dB at this share of
// the BBD depth up to BOOST_DB at full CCW (linear in dB) — the chain's
// low-passes make that end dark and quiet; the gain also drives its tanh.
static constexpr float    VESTIGE_K4_BBD_BOOST_FROM = 0.5f;    // ~9:10 on K4
static constexpr float    VESTIGE_K4_BBD_BOOST_DB   = 6.f;
// The retired K4 sensitivity map (host tests use it to set thresholds).
static constexpr float    VESTIGE_AUTO_THRESH_MIN = 0.005f;
static constexpr float    VESTIGE_AUTO_THRESH_MAX = 0.10f;
static constexpr float    VESTIGE_AUTO_HYST       = 0.55f;  // close threshold = open * hyst
static constexpr uint32_t VESTIGE_AUTO_RELEASE_MS = 80;     // silence held this long ends a phrase
// Onset detector — lifts the re-arm block after a ceiling stop when a NEW attack
// arrives while the old note still rings above both gate levels (level
// hysteresis cannot see that). Modelled on sprawl's note-on (TRANSIENT_*): the
// envelope jumping a RATIO above a slow baseline — level-independent, one
// firmware for bass and guitar — with a refractory. The floor is the K4 open
// threshold x FLOOR_REL, so it is exactly as sensitive as the gate and K4 keeps
// meaning sensitivity (no absolute trigger level). Used ONLY to lift the block;
// never to end or split a capture.
static constexpr float    VESTIGE_ONSET_SLOW_COEF     = 0.0006f; // baseline follower (= sprawl TRANSIENT_SLOW_COEF, ~35 ms)
static constexpr float    VESTIGE_ONSET_RISE          = 1.8f;    // env > baseline x this = an attack (= sprawl)
static constexpr float    VESTIGE_ONSET_FLOOR_REL     = 1.0f;    // x the K4 open threshold
// Refractory: sprawl's 50 ms is too short for a sustained LOW note here: on a
// 41 Hz string the envelope still carries its ripple when 50 ms end, and a
// ripple peak clears 1.8 x the not-yet-settled baseline (host: 2nd onset at
// 51 ms). 75 ms was the shortest that gave one onset per pluck down to 31 Hz
// (short + sustained); 100 for margin. Only the first onset after a ceiling
// stop matters, so this costs nothing musically.
static constexpr uint32_t VESTIGE_ONSET_REFRACTORY_MS = 100;     // one pluck = one onset
// The onset detector reads the GATE meter (fast rise, slow fall), and its
// baseline follows that meter too — not the fast env_.
// Why (hardware DIAG log, 2026-09-29, vestige-20260929-1228.log): on a real
// ringing low E the fast meter jumps around far more than any host model showed.
// The detector fired every 100 ms — exactly its refractory time — with ratios
// just over 1.8 (1.80..2.5), all on ONE ringing note. With a short T each capture
// hits the ceiling, and the next false onset lifted the re-arm block at once:
// 9 captures in 2.5 s from one note, every one "why=O". The gate meter has its
// valleys filled, so a ringing note looks flat to the detector, while a new
// pluck is still a sharp jump (the gate meter follows the fast meter UP at once).
static constexpr bool     VESTIGE_ONSET_ON_GATE       = true;
// Stage 2: background wrap-guard writer (vestige.h IsrFillGuards). Cells per
// sample; anything >= 1 keeps ahead of forward grains (they first read the
// guard one loop after playback starts). 8 = a full 21504-cell guard in ~56 ms,
// which decides how soon a fresh short loop may start in REVERSE. Cost: 8
// SDRAM copies per sample, only while a guard is being written.
// Background copy of the loop head behind the loop end, cells per sample.
// It must never be overtaken by that loop's reader, or a grain reads an
// unwritten cell right after a capture. The speed is ADAPTIVE:
//   - normally 8 per sample (VESTIGE_GUARD_FILL_PER_SAMPLE) — a loop read at
//     normal speed can never catch it;
//   - faster only while the loop being copied is itself read fast (tape or
//     stretch at a high rate, x2 for the K1 double-speed version), sized to that
//     reader, up to VESTIGE_GUARD_FILL_MAX.
// Why not simply fast: 1280dad copied at a flat 256 per sample. That is a burst
// of up to ~21000 copies exactly when a new loop is decided, and on the pedal it
// overran the audio callback — an occasional click when one loop replaced
// another. The host cannot see it (same arithmetic); the A/B on hardware
// (bcc18e0, back at 8) made it disappear.
static constexpr uint32_t VESTIGE_GUARD_FILL_PER_SAMPLE = 8;
static constexpr uint32_t VESTIGE_GUARD_FILL_MAX        = 256;  // 80x tape x 2 (K1) = 160 cells/sample + margin
// Stage 2: a loop whose end is decided AFTER its first grid boundary (every
// round-down, and any round-up within the 80 ms release of its boundary) can
// no longer start on its "one". true = it joins immediately, IN PHASE with its
// own grid (every loop sample still lands on start + k*len + j; the first pass
// just enters part-way in). false = it stays silent until its next "one" —
// entry on the downbeat, at the cost of up to a loop length of silence.
static constexpr bool     VESTIGE_LATE_JOIN_IN_PHASE = true;

// ---------------------------------------------------------------------------
// K1 = PLAYBACK SPEED CROSSFADE (rework stage 6, plan §4.2). A crossfade between
// three versions of the loop's playback, not an added voice: full CCW = only
// half speed, noon = only clean, full CW = only double speed; between noon and
// an end, the two adjacent versions crossfade equal-power. Tape-style: the head
// and the grain read rate move together (0.5 / 2), so a half-speed pass lasts
// exactly 2 loop lengths and a double-speed one exactly 1/2 — still on the grid.
// Freeze side too: each band stream gets a speed version (rate 0.5 / 2), same
// crossfade. Never touches capture or loop length.
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_K1_DEADZONE = 0.06f;   // ±6% around noon = only clean (same as K2's)
static constexpr float  VESTIGE_K1_SMOOTH   = 0.003f;  // one-pole per sample on the amount (~7 ms)
static constexpr float  VESTIGE_K1_GATE_EPS = 1e-3f;   // a version quieter than this emits no grains

// ---------------------------------------------------------------------------
// Playing loops follow T (SW2 temporary selector: UP = A tape, MIDDLE = B
// stretch, DOWN = C re-cut). Each loop keeps its division d; its target length
// is Boundary(d, T_now). A (tape): rate = material / target on the head and the
// grain read rate — pitch and time together, composed with K1.
// How playing loops follow a T change: STRETCH (speed follows, pitch stays),
// fixed (vestige.h Controls). Tape (speed + pitch) and re-cut (cut / pad the
// end) stay in the code. SW2 selects the K3 mode instead.

// ---- Errors on K3 (bipolar) -------------------------------------------------
// CW half: the level (0..1) of all three error layers together (timing mode 3:
// TIMING / CONDITION / PLAYBACK). Noon (+-VESTIGE_K3_DEADZONE): no errors. CCW
// half (K3 mode 1, SW2 UP): the RHYTHM only, depth u = 0 just past the dead
// zone .. 1 at full CCW, from one of three engines (VESTIGE_TIMING_RHY_ENGINE):
//   0 POLYMETRIC: Euclidean stutters + rests, one step each, on the
//     voice's running step count (it rolls across bars), at FIXED rotations
//     (RHY_S_ROT / RHY_R_ROT / RHY_D_ROT below, the same for every loop):
//     stutters: a RHY_S_CYCLE (12) step cycle, S_K_MIN .. S_K_MAX hits with the
//     depth (E(2,12) = dotted-quarter hemiola .. E(7,12) = the bell pattern);
//     rests: a RHY_R_CYCLE (8) step cycle, R_K_MIN .. R_K_MAX hits (E(1,8) ..
//     E(3,8) = tresillo); a stutter wins where both land. 12 : 8 = 3:2 and
//     12 : 16 = 4:3 (the cross-rhythms of the tradition; 7 was only coprime):
//     the whole rhythm repeats after lcm(12, 8, 16) = 48 steps (3 bars).
//   1 AFRO TABLE: a CURATED table of afro-based patterns,
//     VESTIGE_TIMING_RHY_TABLE_AFRO, 12 rows across the CCW travel (below).
//   2 INTERLOCK TABLE (default): VESTIGE_TIMING_RHY_ROWS rows of two
//     interlocking voices (A = stutters, B = rests), decimate accents where
//     they overlap, VESTIGE_TIMING_RHY_TABLE_INTERLOCK (below).
static constexpr float  VESTIGE_K3_DEADZONE          = 0.02f;
static constexpr int    VESTIGE_TIMING_RHY_ENGINE    = 2;   // 0 polymetric · 1 afro table · 2 interlock table
static constexpr int    VESTIGE_TIMING_RHY_S_CYCLE   = 12;
static constexpr float  VESTIGE_TIMING_RHY_S_K_MIN   = 2.f;
static constexpr float  VESTIGE_TIMING_RHY_S_K_MAX   = 7.f;
static constexpr int    VESTIGE_TIMING_RHY_R_CYCLE   = 8;
static constexpr float  VESTIGE_TIMING_RHY_R_K_MIN   = 1.f;
static constexpr float  VESTIGE_TIMING_RHY_R_K_MAX   = 3.f;
// Engine 0 ROTATIONS: rotation is part of the rhythm — fixed, never random; to
// be chosen by ear per rhythm. E(k, n) rotated by rot: hit at i if
// ((i + rot) % n * k) % n < k. S_ROT rotates the stutter cycle (0 .. S_CYCLE-1),
// R_ROT the rest cycle (0 .. R_CYCLE-1), D_ROT the decimate slots
// (0 .. D_SLOTS-1, also picks each slot's decimate factor). Every loop plays
// them. D_ROT is also engine 1's decimate rotation; engine 2 has no off-beat
// decimate (its decimates = its rows' overlap masks). The tables' stutter and
// rest patterns carry their own rotations.
static constexpr int    VESTIGE_TIMING_RHY_S_ROT     = 0;
static constexpr int    VESTIGE_TIMING_RHY_R_ROT     = 0;
static constexpr int    VESTIGE_TIMING_RHY_D_ROT     = 0;
// Engines 1 + 2 (tables): depth u (0 just past the dead zone .. 1 at full CCW) picks a row: the travel
// is cut into ROWS equal zones, row = min(ROWS-1, (int)(u * ROWS)), 4 rows per
// third. Each row = a GRID, a STUTTER pattern and a REST pattern, all indexed
// by the voice's RUNNING step count t (16th steps; it never resets per pass,
// so 12- and 8-step cycles roll against the 16-step bar). The curated
// (string) patterns are NOT rotated per loop: a clave's / bell's orientation
// relative to the loop's one is the point. A stutter wins where a stutter and
// a rest land on the same step.
//   grid HALF: one pattern entry = one 8th = 2 grid steps (index t/2). A
//     stutter hit is 2 steps long and replays the previous 8th; a rest covers
//     the 8th's 2 steps.
//   grid 1x : one entry = one 16th step (index t); every hit is 1 step. (The
//     builder's "FAST" rows are 1x too — dense bells + tremolo rests.)
// A pattern is one of:
//   RhyStr("x..x..")    a typed pattern, 'x' = hit, its own length = its cycle;
//   RhyEuc(k, n, rot)   Euclidean E(k, n): hit at i if ((i+rot)%n * k) % n < k;
//   RhyTrem(rule)       rests only: tremolo on odd 16ths (t odd) — kRhyTremBeat4
//                       only inside beat 4 of each 16-step bar (t % 16 >= 12),
//                       kRhyTremOdd everywhere (always 1x steps);
//   RhyNone()           nothing.
// EDITABLE: rows, their order and their count (ROWS follows the table).
enum { kRhyNone = 0, kRhyStr, kRhyEuc, kRhyTrem };
enum { kRhyTremBeat4 = 1, kRhyTremOdd = 2 };
enum { kRhyGrid1x = 1, kRhyGridHalf = 2 };                 // grid steps per pattern entry
struct VestigeRhyPat { int kind; const char* str; int len, k, n, rot; };
// dec (engine 2 only; engine 1 leaves it empty and ignores it) = the row's
// DECIMATE mask: where voice A (the stutters) and voice B overlap, a one-step
// decimate accent on that stutter (indexed by the running step % its length,
// like the rests). meta (engine 2) = what the row was built from, printed by
// the DIAG log: voice A (the stutters: name, hits k, base n, rotation), the
// rest principle (ducking / negative space) and voice B (the voice the rests
// and the overlaps are derived from; name nullptr = none, the log then reads
// the pattern).
enum { kRhyPrNone = 0, kRhyPrDuck = 1, kRhyPrNeg = 2 };
struct VestigeRhyVoice { const char* name; int k, n, rot; };
struct VestigeRhyMeta { VestigeRhyVoice a; int pr; VestigeRhyVoice b; };
struct VestigeRhyRow { int grid; VestigeRhyPat stut, rest; VestigeRhyPat dec = {}; VestigeRhyMeta meta = {}; };
constexpr int VestigeRhyStrLen(const char* s) { return *s ? 1 + VestigeRhyStrLen(s + 1) : 0; }
constexpr VestigeRhyPat RhyStr(const char* s)             { return {kRhyStr, s, VestigeRhyStrLen(s), 0, 0, 0}; }
constexpr VestigeRhyPat RhyEuc(int k, int n, int rot)     { return {kRhyEuc, nullptr, 0, k, n, rot}; }
constexpr VestigeRhyPat RhyTrem(int rule)                 { return {kRhyTrem, nullptr, 0, rule, 0, 0}; }
constexpr VestigeRhyPat RhyNone()                         { return {kRhyNone, nullptr, 0, 0, 0, 0}; }
static constexpr VestigeRhyRow VESTIGE_TIMING_RHY_TABLE_AFRO[] = {
  // noon ->
  // -- first third: HALF time (8ths), the claves --
  /*  0 tresillo (3-3-2, the Cuban / West-African cell)  */ {kRhyGridHalf, RhyStr("x..x..x."),         RhyNone()},
  /*  1 son clave 3-2 (2 bars)                           */ {kRhyGridHalf, RhyStr("x..x..x...x.x..."), RhyStr("...............x")},
  /*  2 shiko (Nigerian / Ghanaian bell, 2 bars)         */ {kRhyGridHalf, RhyStr("x...x.x...x.x..."), RhyStr(".......x.......x")},
  /*  3 gahu (Ewe, Ghana, 2 bars)                        */ {kRhyGridHalf, RhyStr("x..x..x...x...x."), RhyEuc(2, 8, 3)},
  // -- second third: 1x (16ths), Euclid + Cuban cells --
  /*  4 E(4,12): 4-over-3 (dotted-8th pulse)             */ {kRhyGrid1x,   RhyEuc(4, 12, 0),             RhyEuc(2, 8, 3)},
  /*  5 E(5,12): the 5-stroke bell's Euclid              */ {kRhyGrid1x,   RhyEuc(5, 12, 0),             RhyEuc(2, 8, 3)},
  /*  6 cinquillo (Cuban, 8 16ths)                       */ {kRhyGrid1x,   RhyStr("x.xx.xx."),           RhyStr(".x..x...")},   // rests in the gaps
  /*  7 rumba clave 3-2 (1 bar of 16ths)                 */ {kRhyGrid1x,   RhyStr("x..x...x..x.x..."),   RhyStr(".x...x.x")},
  // -- last third: the 12/8 bells ("FAST"), tremolo rests --
  /*  8 5-stroke bell (12 16ths)                         */ {kRhyGrid1x,   RhyStr("x.x.x..x.x.."),       RhyEuc(5, 12, 1)},
  /*  9 7-stroke standard bell (Ewe / Yoruba, 12 16ths)  */ {kRhyGrid1x,   RhyStr("x.x.xx.x.x.x"),       RhyStr("...x..x...x.")},   // rests in the gaps
  /* 10 standard bell + tremolo rests in beat 4          */ {kRhyGrid1x,   RhyStr("x.x.xx.x.x.x"),       RhyTrem(kRhyTremBeat4)},
  /* 11 standard bell + tremolo rests on every odd 16th  */ {kRhyGrid1x,   RhyStr("x.x.xx.x.x.x"),       RhyTrem(kRhyTremOdd)},
  // -> full CCW
};
// Engine 2: the INTERLOCK table. Rotation is part of the rhythm — fixed, never
// random; rows chosen by the builder's ear via the DIAG #n log. Every row is
// on the 1x 16th grid (kRhyGrid1x), indexed by the voice's RUNNING step count
// t (never reset per pass), with NO per-loop rotation for anything. A row =
// {kRhyGrid1x, stutters, rests, dec, meta}:
//   stutters = voice A: a Euclid or a named cell (clave, bell, shiko, gahu,
//     tresillo, cinquillo, bossa, ...) as its 8 / 12 / 16 step string, in its
//     traditional orientation (rotation 0);
//   rests = a mask of length lcm(A, B) built from a second voice B = E(k, n),
//     never on a stutter, by the row's principle:
//       ducking        = rests where voice B hits and A does not;
//       negative space = rests in the gaps where neither A nor B hits;
//   dec = the OVERLAPS of A and B (same length as the rests): where both hit,
//     the stutter gets a one-step DECIMATE accent (its factor fixed by the
//     overlap's position in the dec cycle); the ONLY decimates of engine 2;
//   each pattern = RhyStr("x..x....") (any length, 'x' = hit, entry t % L),
//     RhyEuc(k, n, rot) (n = 8 / 12 / 16) or RhyNone(); every hit is ONE step:
//     a stutter replays the previous step, a rest silences it;
//   meta = {{A name, k, n, rot}, kRhyPrDuck | kRhyPrNeg, {B name, k, n, rot}}
//     — the exact values the DIAG log prints.
// (Checked at compile time, below: grid 1x, Euclid n in {8, 12, 16},
// 0 <= k <= n, rot >= 0, strings non-empty; a rest never on a stutter; a
// decimate only on a stutter.)
// Rows #1 just past the dead zone near noon .. #ROWS full CCW;
// row = min(ROWS-1, (int)(u * ROWS)). The comment per row names a (voice A),
// the principle and b (voice B); the same values are in the row's meta.
// GENERATED by gen_interlock.py from a rows JSON (rows24.json) — regenerate,
// do not hand-edit.
static constexpr VestigeRhyRow VESTIGE_TIMING_RHY_TABLE_INTERLOCK[] = {
  // noon ->
  // BEGIN GENERATED INTERLOCK ROWS (gen_interlock.py — do not hand-edit)
  /* #1  a=E(2,12) ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x.....x....."), RhyStr("....x...x.......x...x..."), RhyStr("x...........x..........."), {{"E(2,12)", 2, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #2  a=shiko ducking b=E(2,8), decimates = the 3 overlaps */
  {kRhyGrid1x, RhyStr("x...x.x...x.x..."), RhyStr("........x......."), RhyStr("x...x.......x..."), {{"shiko", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #3  a=son_3-2 ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x...x.x..."), RhyStr("....x...x......."), RhyStr("x...........x..."), {{"son_3-2", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #4  a=son_2-3 ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("..x.x...x..x..x."), RhyStr("x...........x..."), RhyStr("....x...x......."), {{"son_2-3", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #5  a=rumba_3-2 ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x..x...x..x.x..."), RhyStr("....x...x......."), RhyStr("x...........x..."), {{"rumba_3-2", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #6  a=rumba_2-3 ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("..x.x...x..x...x"), RhyStr("x...........x..."), RhyStr("....x...x......."), {{"rumba_2-3", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #7  a=bossa ducking b=E(2,8), decimates = the 1 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x...x..x.."), RhyStr("....x...x...x..."), RhyStr("x..............."), {{"bossa", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #8  a=gahu ducking b=E(2,8), decimates = the 1 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x...x...x."), RhyStr("....x...x...x..."), RhyStr("x..............."), {{"gahu", 5, 16, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #9  a=E(4,12) ducking b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x..x.."), RhyStr("....x...x.......x...x..."), RhyStr("x...........x..........."), {{"E(4,12)", 4, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #10 a=tresillo ducking b=E(2,8), decimates = the 1 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x."), RhyStr("....x..."), RhyStr("x......."), {{"tresillo", 3, 8, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #11 a=E(5,12) ducking b=E(2,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x..x.x..x.x."), RhyStr("....x...........x......."), RhyStr("x.......x...x.......x..."), {{"E(5,12)", 5, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #12 a=bell_5 ducking b=E(2,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x.x.x..x.x.."), RhyStr("........x...........x..."), RhyStr("x...x.......x...x......."), {{"bell_5", 5, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #13 a=E(4,12) ducking b=E(3,8), decimates = the 3 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x..x.."), RhyStr("........x..x..x.x..x..x."), RhyStr("x..x..x................."), {{"E(4,12)", 4, 12, 0}, kRhyPrDuck, {"E(3,8)", 3, 8, 0}}},
  /* #14 a=tresillo ducking b=E(4,12), decimates = the 3 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x."), RhyStr(".........x..x..x..x..x.."), RhyStr("x..x..x................."), {{"tresillo", 3, 8, 0}, kRhyPrDuck, {"E(4,12)", 4, 12, 0}}},
  /* #15 a=E(5,12) ducking b=E(3,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x..x.x..x.x."), RhyStr("......x....x..x.x..x...."), RhyStr("x..x....x.............x."), {{"E(5,12)", 5, 12, 0}, kRhyPrDuck, {"E(3,8)", 3, 8, 0}}},
  /* #16 a=E(7,12) ducking b=E(2,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x.x.x.xx.x.x"), RhyStr("........x...........x..."), RhyStr("x...x.......x...x......."), {{"E(7,12)", 7, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #17 a=bell_7 ducking b=E(2,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x.x.xx.x.x.x"), RhyStr("........x...........x..."), RhyStr("x...x.......x...x......."), {{"bell_7", 7, 12, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #18 a=bell_7 ducking b=E(3,8), decimates = the 5 overlaps */
  {kRhyGrid1x, RhyStr("x.x.xx.x.x.x"), RhyStr("...x..x.x.............x."), RhyStr("x..........x..x.x..x...."), {{"bell_7", 7, 12, 0}, kRhyPrDuck, {"E(3,8)", 3, 8, 0}}},
  /* #19 a=cinquillo ducking b=E(2,8), decimates = the 1 overlaps */
  {kRhyGrid1x, RhyStr("x.xx.xx."), RhyStr("....x..."), RhyStr("x......."), {{"cinquillo", 5, 8, 0}, kRhyPrDuck, {"E(2,8)", 2, 8, 0}}},
  /* #20 a=cinquillo ducking b=E(4,12), decimates = the 5 overlaps */
  {kRhyGrid1x, RhyStr("x.xx.xx."), RhyStr(".........x..x..x........"), RhyStr("x..x..x...........x..x.."), {{"cinquillo", 5, 8, 0}, kRhyPrDuck, {"E(4,12)", 4, 12, 0}}},
  /* #21 a=bell_5 negative space b=E(3,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x.x.x..x.x.."), RhyStr(".x...x....x..x.x.xx.x..x"), RhyStr("x.............x.x..x...."), {{"bell_5", 5, 12, 0}, kRhyPrNeg, {"E(3,8)", 3, 8, 0}}},
  /* #22 a=E(5,12) negative space b=E(3,8), decimates = the 4 overlaps */
  {kRhyGrid1x, RhyStr("x..x.x..x.x."), RhyStr(".xx.x..x.x...x....x..x.x"), RhyStr("x..x....x.............x."), {{"E(5,12)", 5, 12, 0}, kRhyPrNeg, {"E(3,8)", 3, 8, 0}}},
  /* #23 a=E(4,12) negative space b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x..x.."), RhyStr(".xx..x.x..xx.xx..x.x..xx"), RhyStr("x...........x..........."), {{"E(4,12)", 4, 12, 0}, kRhyPrNeg, {"E(2,8)", 2, 8, 0}}},
  /* #24 a=son_3-2 negative space b=E(2,8), decimates = the 2 overlaps */
  {kRhyGrid1x, RhyStr("x..x..x...x.x..."), RhyStr(".xx..x.x.x.x.xxx"), RhyStr("x...........x..."), {{"son_3-2", 5, 16, 0}, kRhyPrNeg, {"E(2,8)", 2, 8, 0}}},
  // END GENERATED INTERLOCK ROWS
  // -> full CCW
};
// Engine 2's table, checked at compile time.
constexpr bool VestigeRhyIlPatOk(const VestigeRhyPat& p) {
  return p.kind == kRhyNone || (p.kind == kRhyStr && p.len > 0) ||
         (p.kind == kRhyEuc && (p.n == 8 || p.n == 12 || p.n == 16) && p.k >= 0 && p.k <= p.n && p.rot >= 0);
}
constexpr int VestigeRhyIlLen(const VestigeRhyPat& p) { return p.kind == kRhyStr ? p.len : p.kind == kRhyEuc ? p.n : 1; }
constexpr bool VestigeRhyIlHit(const VestigeRhyPat& p, int t) {   // (the module's RhyPatHit, t >= 0, no tremolo)
  return p.kind == kRhyStr ? p.str[t % p.len] == 'x'
       : p.kind == kRhyEuc ? p.k > 0 && ((((t % p.n) + p.rot) % p.n) * p.k) % p.n < p.k : false;
}
constexpr int VestigeRhyGcd(int a, int b) { return b ? VestigeRhyGcd(b, a % b) : a; }
constexpr bool VestigeRhyIlTableOk() {
  for (const VestigeRhyRow& R : VESTIGE_TIMING_RHY_TABLE_INTERLOCK) {
    if (R.grid != kRhyGrid1x || !VestigeRhyIlPatOk(R.stut) || !VestigeRhyIlPatOk(R.rest) || !VestigeRhyIlPatOk(R.dec)) return false;
    const int ls = VestigeRhyIlLen(R.stut), lr = VestigeRhyIlLen(R.rest), ld = VestigeRhyIlLen(R.dec);
    const int l2 = ls / VestigeRhyGcd(ls, lr) * lr, L = l2 / VestigeRhyGcd(l2, ld) * ld;   // their whole period
    for (int t = 0; t < L; t++) {
      const bool S = VestigeRhyIlHit(R.stut, t);
      if (S && VestigeRhyIlHit(R.rest, t)) return false;   // a rest never on a stutter
      if (!S && VestigeRhyIlHit(R.dec, t)) return false;   // a decimate only on a stutter
    }
  }
  return true;
}
// The selected engine's table (engines 1 + 2 share the table machinery).
static constexpr const VestigeRhyRow* VESTIGE_TIMING_RHY_TABLE =
    (VESTIGE_TIMING_RHY_ENGINE == 2) ? VESTIGE_TIMING_RHY_TABLE_INTERLOCK : VESTIGE_TIMING_RHY_TABLE_AFRO;
static constexpr int    VESTIGE_TIMING_RHY_ROWS = (VESTIGE_TIMING_RHY_ENGINE == 2)
    ? (int)(sizeof(VESTIGE_TIMING_RHY_TABLE_INTERLOCK) / sizeof(VESTIGE_TIMING_RHY_TABLE_INTERLOCK[0]))
    : (int)(sizeof(VESTIGE_TIMING_RHY_TABLE_AFRO) / sizeof(VESTIGE_TIMING_RHY_TABLE_AFRO[0]));
// All engines: one small variation with chance RHY_VAR_PROB per ~32 steps:
// one added 1-step stutter on a free step, for that pass only. Was 0.3; 0 =
// OFF for the rhythm rating session (every pass plays exactly the base).
static constexpr float  VESTIGE_TIMING_RHY_VAR_PROB  = 0.f;
// Decimate colour on the rhythm, engines 0 + 1: a Euclidean pattern on the
// 8th-note off-beats, E(D_K, D_SLOTS) over 2 bars' off-beats (D_SLOTS = 8),
// D_K = D_K_MIN just past noon .. D_K_MAX at full CCW; one step, never on a
// rest, stacked on a stutter; rotation: the fixed RHY_D_ROT (never random).
// Engine 1 only: in HALF-grid rows on the QUARTER off-beats instead
// (t % 8 == 4), the same slot logic (slot = t / 8). (Engine 2: none of this,
// its decimates are the rows' overlap masks.)
static constexpr int    VESTIGE_TIMING_RHY_D_SLOTS  = 8;
static constexpr float  VESTIGE_TIMING_RHY_D_K_MIN  = 1.f;
static constexpr float  VESTIGE_TIMING_RHY_D_K_MAX  = 6.f;
static_assert(VestigeRhyIlTableOk(),
              "VESTIGE_TIMING_RHY_TABLE_INTERLOCK: rows are {kRhyGrid1x, stutters, rests, dec, meta}; a Euclid needs "
              "n = 8, 12 or 16, 0 <= k <= n, rot >= 0; a string is non-empty; a rest never on a stutter; "
              "a decimate (dec) only on a stutter");

// ---- Stage 3: the TIMING error — a steady Euclidean groove inside a pass ---
// Level L = err_level_[kErrTiming]. L == 0: off. For any L > 0 a loop voice
// plays a EUCLIDEAN PATTERN E(k, n) all the time, one instance per
// VESTIGE_TIMING_PATTERN_PASSES passes (below): the span is cut into n equal
// steps and at every hit step the playback restarts from the loop's start
// (reverse: its end); step 0 is always a hit (the pass start itself). The pass
// keeps its length, the grid never moves.
// Patterns, ordered by density — EDITABLE: (hits, steps), generated with
// Bjorklund's spreading at init (vestige.h TimingBuildPatterns), not typed as
// masks:
//   E(2,8) x...x...  · E(4,12) x..x..x..x.. · E(3,8) tresillo · E(5,12)
//   E(3,6) x.x.x.    · E(7,12) bell         · E(5,8) cinquillo · E(4,6)
//   E(5,6)           · E(7,8)
// BASE pattern: index round(L * (N-1)) (vestige.h TimingBaseIndex), the same on
// every pass. Its ROTATION (one that starts on a hit) is drawn once per loop,
// when it starts playing, and kept: rotation = the loop's drawn index modulo
// the pattern's hit count, so K3 moving the pattern keeps it as close as the
// hit counts allow (and K3 back = the same rotation again).
// VARIATION: each pattern instance, with chance VESTIGE_TIMING_VAR_PROB, that
// one instance plays a variation — uniformly one of: a hit added (a rest step,
// not step 0), a hit dropped (not step 0), another hit-starting rotation. The
// instance after a variation is always the base (it does not roll).
static constexpr float  VESTIGE_TIMING_VAR_PROB   = 1.f / 6.f;
// HALF TIME: one pattern instance spans this many passes. Its n steps are
// spread over the whole span (step = span x pass / n); an instance starts on
// the loop's "one" at every span-th pass of the loop's own pass counter (the
// K1 half-speed parity), so the grid is unchanged. Between hits the read runs
// on — also THROUGH a pass wrap inside the span, with no restart there; a hit
// that lands where the read already is (on a pass start, read on the
// timeline) is not restarted. Base / rotation / variation / short-loop guard
// are per instance. 1 = one pattern per pass.
static constexpr int    VESTIGE_TIMING_PATTERN_PASSES = 2;
static_assert(VESTIGE_TIMING_PATTERN_PASSES >= 1, "a pattern spans at least one pass");
static constexpr int    VESTIGE_TIMING_PATTERNS   = 10;
static constexpr int    VESTIGE_TIMING_PAT_HITS[VESTIGE_TIMING_PATTERNS]  = {2,  4, 3,  5, 3,  7, 5, 4, 5, 7};
static constexpr int    VESTIGE_TIMING_PAT_STEPS[VESTIGE_TIMING_PATTERNS] = {8, 12, 8, 12, 6, 12, 8, 6, 6, 8};
static constexpr int    VESTIGE_TIMING_MAX_STEPS  = 16;          // upper bound on n (bit mask + hit list)
// Short-loop guard: when the base pattern's step (span x pass / n, output time)
// would be shorter than this, the pass plays the densest pattern BELOW it in the
// list that fits (nothing below fits: the sparsest one above that does); none
// fits at all = no pattern that pass. Variations keep n, so they
// never shorten a step. (Keeps each restart's 5 ms crossfade inside its step.)
static constexpr float  VESTIGE_TIMING_MIN_STEP_MS = 20.f;

// ---- Timing error MODE ------------------------------------------------------
// 0 = RETRIGGER (the Euclidean patterns above, as heard in ae31f88); 1 = SLICE
// REARRANGEMENT (below, as heard in e7afd92); 2 = PASS MEMORY (below that, as
// heard in 1379252); 3 = LAYERS (after it). Older modes stay, one constant away.
static constexpr int    VESTIGE_TIMING_MODE = 3;
// SLICES: the pass is cut into N equal slices (boundaries round(L*i/N) in
// material; in stretch at i/N of the pass in output time). Each loop has an
// ARRANGEMENT: which slice plays at each step; step 0 is always slice 0 (the
// downbeat is anchored), identity = the untouched loop. When a loop starts
// playing it draws, once, a priority order of the steps 1..N-1 and for each
// step a replacement slice (any slice but its own: repeats and drops allowed).
// The level L rearranges the first round(L*(N-1)) steps in that order, so
// raising K3 adds swaps and lowering removes them without reshuffling.
// RANDOMNESS: on every pass each step 1..N-1 independently plays a fresh
// random slice (any slice but the one it would play) for that pass only, with
// probability r = L^VESTIGE_TIMING_RAND_CURVE; step 0 never. L = 0 nothing,
// low L = steady with rare random slices, L = 1 = every step random every pass.
// (VESTIGE_TIMING_VAR_PROB is mode 0 only.)
// Playback: a step whose slice is the natural continuation of what the read is
// playing runs on; any other jumps the read to its slice's start (5 ms stream
// restart). Reverse: the same arrangement on the REVERSED loop (its pass
// starts at the loop's end; reversed slice j = the j-th N-th from the end).
// Short-loop guard: a slice shorter than VESTIGE_TIMING_MIN_STEP_MS (output
// time) halves N (8 -> 4 -> 2); 2 don't fit = no rearrangement.
static constexpr int    VESTIGE_TIMING_SLICES = 8;
static constexpr float  VESTIGE_TIMING_RAND_CURVE = 1.0f;       // per-step random chance = L^curve (was 2: too tame)
// Heard on ed8f8a1 (fixed nested arrangement + random slices on top): "too
// static — mixed up concepts". The builder's concept is simpler: on every step,
// a jump to a random slice, with K3 as the chance; slice 1 on the downbeat. So
// the fixed arrangement is OFF (kept, one constant away) and the chance is linear.
static constexpr bool   VESTIGE_TIMING_FIXED_ARRANGEMENT = false;
static constexpr int    VESTIGE_TIMING_SLICE_MAX = 16;          // table size bound
static constexpr int    VESTIGE_TIMING_SLICE_TIERS = 4;         // N, N/2, N/4, N/8 tables
static_assert(VESTIGE_TIMING_SLICES >= 2 && VESTIGE_TIMING_SLICES <= VESTIGE_TIMING_SLICE_MAX, "2..16 slices");

// ---- Timing mode 2: PASS MEMORY ---------------------------------------------
// The pass is cut into VESTIGE_TIMING_SLICES steps (same short-loop guard as
// mode 1). Each loop voice remembers VESTIGE_TIMING_MEM_PASSES passes that take
// turns (A B C A B C ...), each a small set of FIGURES placed on its steps, each
// evolving on its own: everything below happens to the memory whose turn it
// is (a figure's LIFE counts that memory's turns). Every pass start, in order:
//   1. every figure that has played VESTIGE_TIMING_MEM_LIFE passes is removed
//      (its steps go back to the edits below: clean or another figure);
//   2. EDITS figures are edited — always at least one, every pass: a figure
//      added when the pass holds fewer FIGURES than the TARGET (a figure counts
//      once, whatever its span; it only needs free steps), one removed when
//      more, one swapped (removed + a new one added) when it matches. Removals
//      only take figures placed on an earlier pass, so at EDITS = n every
//      step is re-rolled every pass. The target is drawn per edit: floor(D) +
//      (1 with chance frac(D)), at most n.
// The downbeat is NOT protected: step 1 takes any figure, like every step
//   D     = TARGET_A + TARGET_B * L                      figures held (L = K3 level > 0)
//   EDITS = max(1, round(EDITS_B * L * n))               per pass
// At L = 1: every step holds a figure (CLEAN among them) and every one of
// them is re-rolled every pass.
static constexpr float  VESTIGE_TIMING_MEM_TARGET_A = 0.4f;
static constexpr float  VESTIGE_TIMING_MEM_TARGET_B = 5.6f;     // D(1) = 6 figures: the pass is full
static constexpr float  VESTIGE_TIMING_MEM_EDITS_B  = 1.0f;     // 1 = one edit per step at L = 1
static constexpr int    VESTIGE_TIMING_MEM_LIFE     = 3;        // turns a figure plays, at most
static constexpr int    VESTIGE_TIMING_MEM_PASSES   = 3;        // memories taking turns (1 = one pass)
// FIGURES — each spans 1..VESTIGE_TIMING_SPAN_MAX steps (length drawn
// uniformly among those that fit), inside the pass; figures never overlap:
//   REST     the span is silent
//   STUTTER  the span plays the same number of steps before it again
//   REPEAT   the step before the span plays over the whole span (beat repeat)
//   DOUBLE   each step of the span squeezed in twice (double time)
//   RATCHET  each step of the span squeezed in 4 times
//   RETRIG   back to the one in Euclidean groups: E(2,n) = 4+4, E(3,8) = 3+3+2
//            (from its 2nd hit to the end of the pass; not 1..4)
//   REVERSE  the span plays backward (once the loop's guard copy is ready)
//   DECIMATE the span is sample-rate reduced (see below)
//   CLEAN    the span plays as recorded — the no-glitch choice in the set
// Steps before step 1 are the loop's last steps (a STUTTER / REPEAT at the
// start of the pass replays the end of the loop).
// A new figure only goes on free steps.
static constexpr int    VESTIGE_TIMING_SPAN_MAX = 4;
// Weights (relative chance of each figure being picked) — EDITABLE:
static constexpr int    VESTIGE_TIMING_FIGS = 9;
static constexpr float  VESTIGE_TIMING_FIG_WEIGHT[VESTIGE_TIMING_FIGS] = {
  1.f,   // REST
  1.f,   // STUTTER
  1.f,   // REPEAT
  1.f,   // DOUBLE
  1.f,   // RATCHET
  1.f,   // RETRIG
  1.f,   // REVERSE
  1.f,   // DECIMATE
  1.f,   // CLEAN
};
// RATCHET only from this K3 level on (below it: weight 0; ones already placed
// age out). 0.5 = noon.
static constexpr float  VESTIGE_TIMING_RATCHET_FROM = 0.5f;
// DECIMATE: the loop's output is held for N samples (48 kHz / N, an integer: no
// rounding), with no filter BEFORE the hold (the folded-down aliasing is the
// character), then a 2-pole low-pass AFTER it at LP_MULT x the reduced rate's
// Nyquist (48 kHz / N / 2), which takes off the fizzy steps. N drawn per
// figure from the table. Fades in/out over VESTIGE_TIMING_MUTE_MS.
static constexpr int    VESTIGE_TIMING_DECIM_N = 4;
static constexpr int    VESTIGE_TIMING_DECIM_FACTORS[VESTIGE_TIMING_DECIM_N] = {6, 8, 12, 16};   // 8, 6, 4, 3 kHz
static constexpr float  VESTIGE_TIMING_DECIM_LP_MULT = 0.75f;    // lower = darker, higher = harsher
// Where figures land: steps in the back half of the pass weigh this much vs 1
// for the front half (a fill leads into the one).
static constexpr float  VESTIGE_TIMING_MEM_BACK_WEIGHT = 2.f;
static constexpr int    VESTIGE_TIMING_MEM_FIGS   = 8;           // figures a pass can hold
static constexpr int    VESTIGE_TIMING_MAX_EVENTS = 320;         // jumps + mute changes per pass (64 steps x ratchet 4 + changes)
static constexpr float  VESTIGE_TIMING_MUTE_MS    = 5.f;         // rest fade (each way)

// ---- Timing mode 3: LAYERS ---------------------------------------------------
// Three error LAYERS play at once, on top of each other, each its own rhythm
// line — like drum parts. K3 sets all three levels together:
//   TIMING     STUTTER, REPEAT, DOUBLE, RATCHET, RETRIG
//   CONDITION  REST, DECIMATE
//   PLAYBACK   REVERSE
// A layer at level 0 is off. STEPS: the pass is cut into G equal steps, G =
// a power of two or 3 x one (1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64), the
// one whose step is closest to the wanted step: VESTIGE_TIMING_STEP_MS up to
// VESTIGE_TIMING_STEP_KNEE_MS, slower past it (below) — so the glitch keeps its
// character from short loops up (2 s loop: 16 steps of 125 ms), long ambient
// loops glitch slower, and a step always divides the loop exactly (tempo-
// synced, the grid restarts on every pass's one). LINE: each layer's line is G x ceil(32 / G)
// cells — about two bars of 16ths: a 2 s loop gets a 2-pass line, a 6 s loop
// a 1-pass (3-bar) line. Cells are HITS (1 .. VESTIGE_TIMING_SPAN_MAX cells,
// always at least one pause cell between two hits) and pauses. The line plays
// pass by pass and repeats.
//   SEED   k hits spread evenly (Bjorklund) over the pulse = every other cell
//          ("x-x-"), a random rotation; each grows by a cell with chance FILL
//          ("xx", "xxx").
//   EVERY PASS, per layer: 1 + round(OPS_B * L) changes, each one of: add a hit
//          (on a free pulse cell) when the line has fewer than the target,
//          remove one when more, else shift a hit by one cell (syncopation) or
//          grow / shrink it by one cell. From RAND_FROM on, a change is, with
//          rising chance (1 at L = 1), a random hit anywhere instead.
//   TYPE   each hit has a type from its layer (weights below); after
//          VESTIGE_TIMING_LINE_LIFE plays it takes another — the rhythm stays,
//          the content varies.
// Where layers overlap they stack: TIMING decides which step plays, PLAYBACK
// reverses what TIMING made, CONDITION silences / decimates the result.
//   hits per 16 steps (target) = HITS_A + HITS_B * L
//   FILL                       = FILL_A + FILL_B * L
static constexpr float  VESTIGE_TIMING_STEP_MS      = 125.f;    // 250 = half time, 62.5 = double time
// Longer loops glitch slower: past the knee the wanted step grows as
// (loop / knee)^EXP — 4 s: 167 ms (24 steps), 6 s: 188 ms (32), 8 s: 250 ms
// (32, half time). EXP 0 = the same step at every length, 1 = long loops keep
// the knee loop's step COUNT.
static constexpr float  VESTIGE_TIMING_STEP_KNEE_MS = 2000.f;
static constexpr float  VESTIGE_TIMING_STEP_EXP     = 0.5f;
static constexpr int    VESTIGE_TIMING_LINE_STEPS   = 32;       // a line is at least this many steps
static constexpr float  VESTIGE_TIMING_LINE_HITS_A  = 0.5f;
static constexpr float  VESTIGE_TIMING_LINE_HITS_B  = 7.5f;     // L = 1: 8 hits per 16 steps = the full pulse
static constexpr float  VESTIGE_TIMING_LINE_FILL_A  = 0.1f;
static constexpr float  VESTIGE_TIMING_LINE_FILL_B  = 0.5f;
static constexpr float  VESTIGE_TIMING_LINE_OPS_B   = 6.f;
static constexpr float  VESTIGE_TIMING_LINE_RAND_FROM = 0.67f;
static constexpr int    VESTIGE_TIMING_LINE_LIFE    = 3;
static constexpr int    VESTIGE_TIMING_LAYER_MAX_STEPS = 64;    // G bound (steps per pass)
static constexpr int    VESTIGE_TIMING_LINE_MAX_CELLS  = 64;    // line bound (64-bit cell masks)
static_assert(VESTIGE_TIMING_LINE_STEPS <= VESTIGE_TIMING_LINE_MAX_CELLS, "line too long");
// Type weights per layer — EDITABLE. 0 = that type is off; a layer whose
// weights are all 0 is off.
static constexpr float  VESTIGE_TIMING_W_STUTTER = 1.f;         // TIMING
static constexpr float  VESTIGE_TIMING_W_REPEAT  = 1.f;
static constexpr float  VESTIGE_TIMING_W_DOUBLE  = 0.f;
static constexpr float  VESTIGE_TIMING_W_RATCHET = 0.f;         // (only from VESTIGE_TIMING_RATCHET_FROM)
static constexpr float  VESTIGE_TIMING_W_RETRIG  = 1.f;         // the hit plays the loop from its start
static constexpr float  VESTIGE_TIMING_W_REST    = 1.f;         // CONDITION
static constexpr float  VESTIGE_TIMING_W_DECIM   = 1.f;
static constexpr float  VESTIGE_TIMING_W_REVERSE = 1.f;         // PLAYBACK

// ---------------------------------------------------------------------------
// K2 movement that counts as a T change (remapped knob units). Below it, ADC
// jitter would otherwise reach every following loop as pitch warble (at mid
// travel 0.001 of knob is ~1% of T, ~17 cents). A still knob = exactly the old T.
static constexpr float    VESTIGE_K2_FOLLOW_DB   = 0.004f;
static constexpr uint32_t VESTIGE_TAPE_SMOOTH_MS = 40;     // rate glide: a tap (jump) / K2 staircase -> smooth
// Tape rate: NOT folded, NOT capped in normal use. The rate is exactly
// material / target, so a loop always takes exactly Boundary(d, T_now), and the
// whole T range is reachable: T spans 100 ms..8 s, so up to 80x faster or
// slower (x2 more with K1), into sub- and super-audio range. The first version
// folded the rate by octaves into 1/4..4 — which made the pitch jump back an
// octave while K2 turned and never left +-2 octaves ("resets the pitch").
// These limits sit beyond anything the T range can ask for and only guard
// against a degenerate value.
static constexpr float    VESTIGE_TAPE_RATE_MAX  = 128.f;
static constexpr float    VESTIGE_TAPE_RATE_MIN  = 1.f / 128.f;
// B (stretch): the head moves at material / target, grains read at the K1 rate
// only, so pitch stays. While the rate is not 1 the grains are this long
// (instead of the ~400 ms clean-loop grains, which would smear time); 2 per
// stream as always. At rate 1 the clean-loop grains return.
static constexpr uint32_t VESTIGE_STRETCH_GRAIN_MS = 80;
// C (re-cut): speed and pitch stay; the loop's length becomes Boundary(d, T_now),
// cut at the end or silence-padded (read-time fade-out at the material's end).
// Non-destructive: read through a view; a new length is built into a spare
// view's guard at this many cells per sample and applied at the next wrap once
// ready (a full guard in ~56 ms).
static constexpr uint32_t VESTIGE_RECUT_FILL_PER_SAMPLE = 8;

// ---------------------------------------------------------------------------
// K4 = tape varispeed (pitch + speed COUPLED — the whole loop plays faster &
// higher / slower & lower, like a tape speed knob). Bipolar exp around noon.
//   CCW → down · noon = unity (dead-zone detent) · CW → up
// Grains read at this rate AND the loop head advances at it, so pitch and loop
// period move together. Texture (below) is PARKED while K4 is the pitch knob.
// ---------------------------------------------------------------------------
// K4 quantises to a fixed set of tape-speed ratios (a rotary switch). The set
// merges just-intonation intervals (fine musical control near unity) with clock
// divisions (¼…4× reach), overlapping at ½ / 1 / 2. Simple ratios also keep the
// resampler imaging on harmonics (cleaner than arbitrary fractions). Symmetric:
// 6 below unity · unity dead-centre (noon) · 6 above. The pitch glide below
// still carries the tape between stops (portamento, not a zipper step).
// Reciprocal-symmetric: every interval is paired with its 1/x on the far side of
// unity, and the just intervals (M3 5/4, P4 4/3, P5 3/2) are transposed by octave
// out to ±2 octaves so the outer octaves aren't just bare integers. Gap between
// 3 and 4 (and its mirror ¼–⅓) is intentional — it mirrors the fifth→octave gap.
static constexpr float VESTIGE_PITCH_STEPS[] = {
  1.f/4.f, 1.f/3.f, 3.f/8.f, 2.f/5.f, 1.f/2.f, 2.f/3.f, 3.f/4.f, 4.f/5.f, // down
  1.f,                                                                    // unity
  5.f/4.f, 4.f/3.f, 3.f/2.f, 2.f, 5.f/2.f, 8.f/3.f, 3.f, 4.f };           // up
static constexpr int   VESTIGE_PITCH_NSTEPS = 17;
static constexpr float VESTIGE_PITCH_SMOOTH = 0.0006f;   // ~35 ms tape-glide between stops
// Record head LAGS the playback tap by this many cells in frippertronics looper
// mode, like the head gap on a real tape machine. Playback therefore reads the
// PREVIOUS revolution's content — an overdub returns one loop later (real looper
// behaviour), not as a short slapback that combs against the clean signal — and
// reads away from the live write-frontier (also the varispeed record-hash fix).
static constexpr size_t VESTIGE_FRIP_HEAD_GAP  = 240;    // ~5 ms head gap

// ---------------------------------------------------------------------------
// Post-grain tape/BBD warble (K4 degrade). The degrade engine's wow / flutter /
// snag / BBD-clock-drift is a PLAYBACK-SPEED (pitch) modulation — the part a
// static grain sum can't show. On a continuous stream that IS a short modulated
// delay tap: write the looper sum in, read at a tap displaced by the engine's
// cents (leaky-integrated to a sample displacement, exactly like mnemonic's
// wobbled read tap). A small fixed base delay gives the tap room to swing both
// ways; the displacement is clamped so it never reads the future. Base is a few
// ms — inaudible, but note it is a fixed latency on the wet looper path.
// Since the degrade engine is retired (held idle) the tap is BYPASSED on the
// idle path, so the wet has no latency and stays on the stage-2 grid; the
// latency only returns if degrade is revived (vestige.h, ARCHIVED note).
// ---------------------------------------------------------------------------
static constexpr size_t VESTIGE_WARBLE_LEN     = 4800;      // 100 ms modulated-delay line (SDRAM)
static constexpr float  VESTIGE_WARBLE_BASE_MS = 3.f;       // fixed base delay = tap centre (~3 ms)
static constexpr float  VESTIGE_WARBLE_LEAK    = 0.99999f;  // leaky integrator (cents->displacement HP)
static constexpr float  VESTIGE_WARBLE_CENTS_TO_RATE = 0.00057762f; // ln2/1200 (cents -> fractional speed)

// ---------------------------------------------------------------------------
// Texture (K4 — PARKED): bipolar, clean at centre
//   analogue side (CCW) = tape saturation → extreme; digital side (CW) = decimate/crush → glitch
// ---------------------------------------------------------------------------
static constexpr float  VESTIGE_TEX_DEADZONE   = 0.06f;  // clean band around noon
static constexpr float  VESTIGE_TAPE_DRIVE_MAX = 8.f;    // tanh drive at full CCW (grit, gain-compensated)
static constexpr float  VESTIGE_DECIM_HOLD_MAX = 96.f;   // sample-hold length (samples) at full CW
static constexpr float  VESTIGE_DECIMATE_BITS_HI  = 16.f;   // bit depth near noon
static constexpr float  VESTIGE_DECIMATE_BITS_LO  = 2.5f;   // bit depth at full CW

// ---------------------------------------------------------------------------
// K5 loop fade in/out (per-slot envelope). Voiced age-fade is now fixed.
// ---------------------------------------------------------------------------
// Voiced age-fade depth: 0 = all active voices equal · 1 = oldest fades to
// silence. The ramp is power-normalized as a set, so total loudness stays
// constant at any voice count (single-voice is no longer the loudest).
static constexpr float  VESTIGE_AGE_FADE_DEPTH     = 0.4f;
// K5 fade in/out. BOTH are real bounded DURATIONS (linear in K5, 0 → max), so
// they share one scale and their ratio is explicit — no runaway one-pole tail.
static constexpr float  VESTIGE_FADE_ATTACK_MAX_S  = 6.0f;  // K5 CW: swell-in finishes in this
static constexpr float  VESTIGE_FADE_RELEASE_MAX_S = 6.0f;  // K5 CW: fade-out finishes in this (symmetric)
static constexpr float  VESTIGE_FADE_MIN_S         = 0.003f; // K5 noon / CCW: declick, not a 1-sample step (voice-steal click)
// K5 is bipolar: CCW half = number of repeats · noon = endless · CW half =
// the fade above (0 -> max over the half). Dead zone around noon (remapped
// knob units), like K4's.
static constexpr float  VESTIGE_K5_DEADZONE        = 0.06f;
// REPEATS (K5 CCW half): N = REPEAT_N_MAX just past the dead zone down to 1
// at full CCW (whole numbers, log taper). The first repeat plays at full
// level, repeat k (0-based) at FLOOR_DB x (k / (N-1))^CURVE dB — a curve in
// dB: gentle early, steeper later, the last repeat on the quiet floor so the
// stop after it is not heard as a cut (N = 8: 0 / -0.6 / -2.4 / -5.5 / -9.8 /
// -15.3 / -22 / -30 dB; N = 4: 0 / -3.3 / -13.3 / -30). A level never changes
// inside a repeat: each step is a RAMP that ends exactly on the pass end (the
// last one ramps to 0), so every repeat starts at its own level; then the loop
// is freed. N = 1: the loop plays once. Turning K5 CCW on a playing loop counts
// from then; FS2 hold pauses the count; back at noon the loop keeps its level.
static constexpr float  VESTIGE_REPEAT_N_MAX       = 16.f;
static constexpr float  VESTIGE_REPEAT_CURVE       = 2.f;    // 1 = even dB steps, higher = flatter early
static constexpr float  VESTIGE_REPEAT_FLOOR_DB    = -30.f;  // the last repeat's level
static constexpr float  VESTIGE_REPEAT_RAMP_MS     = 10.f;
// Output routing: RETIRED. K6 is now the shell's equal-power dry/wet mix (like
// mnemonic and sprawl) and vestige no longer owns its output — the looper
// volume knob and the SW2 dry gate are gone. VESTIGE_LOOP_BOOST_MAX is kept for
// the revival path only; VESTIGE_ROUTING_SMOOTH still smooths the warble ease.
static constexpr float  VESTIGE_LOOP_BOOST_MAX = 2.0f;   // [retired] was K6 full CW = +6 dB on the looper
static constexpr float  VESTIGE_ROUTING_SMOOTH = 0.003f; // ~7 ms one-pole smoothing
// Concurrent-voice cap: total granulating voiced voices (live + fading) is
// bounded to VESTIGE_MAX_VOICES — the pre-regression ceiling the CPU/grain pool
// handled fine. Beyond it, the oldest is "stolen": fast-released over
// VESTIGE_STEAL_RELEASE_S (declicked) so its slab frees quickly. Classic
// synth-style voice-stealing — a new note reclaims the oldest, cutting its tail.
static constexpr float  VESTIGE_STEAL_RELEASE_S = 0.006f; // fast release on voice-steal (~6 ms)
// Frippertronics — ARCHIVED (unwired, source kept; see the Topology note above).
static constexpr float  VESTIGE_FRIP_OD_RAMP_S = 0.005f; // overdub input fade in/out (declick record in/out)
static constexpr float  VESTIGE_FRIP_DECAY_MIN = 0.20f;  // fast tape decay (frippertronics, just past noon, ~1 repeat)
static constexpr float  VESTIGE_FRIP_DECAY_MAX = 1.0f;   // infinite sustain (frippertronics, full CW)

// ---------------------------------------------------------------------------
// LED blink (Controls runs every ~10 ms)
// ---------------------------------------------------------------------------
static constexpr int    VESTIGE_BLINK_SLOW = 50;  // *10 ms → 500 ms half-period
static constexpr int    VESTIGE_BLINK_FAST = 12;  // *10 ms → 120 ms half-period
static constexpr int    VESTIGE_BLINK_FLICKER = 3; // *10 ms → 30 ms half-period: LED2 while recording
static constexpr int    VESTIGE_FLASH_TICKS = 30; // clear-confirm flash duration
