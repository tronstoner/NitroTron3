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
static constexpr int    VESTIGE_MAX_VOICES   = 6;   // max LIVE voiced (K1 range 1..6)
static constexpr int    VESTIGE_VOICE_SPARES = 3;   // spare slabs for in-flight fade-outs / crossfades
static constexpr int    VESTIGE_VOICE_SLABS  = VESTIGE_MAX_VOICES + VESTIGE_VOICE_SPARES; // voiced slabs
static constexpr int    VESTIGE_FRIP_SLOT    = VESTIGE_VOICE_SLABS;      // frippertronics buffer
static constexpr int    VESTIGE_REC_SLOT     = VESTIGE_VOICE_SLABS + 1;  // dedicated record scratch
static constexpr int    VESTIGE_SLOTS        = VESTIGE_VOICE_SLABS + 2;  // total slabs
// K1 mapping: padded noon = 1 voice; CCW adds voices to 6; CW = frippertronics.
static constexpr float  VESTIGE_K1_NOON_LO  = 0.44f;  // below → voiced, more voices toward CCW
static constexpr float  VESTIGE_K1_NOON_HI  = 0.56f;  // above → frippertronics region
static constexpr int    VESTIGE_GRAINS      = 48;    // shared grain pool (multiband freeze = 3 bands/voice → more grains)

// ---------------------------------------------------------------------------
// Grain smoothness macro (K3): looper (CCW / 0) → freeze (CW / 1)
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
// Multiband granular freeze (K3 CW freeze region) — the EHX-style evolving freeze
// ported from mnemonic, made POLY (per voice). In the freeze region each voice's
// single loop buffer is granulated in 3 bands (low/mid/high) via PER-GRAIN band
// filters (no extra band buffers — host-verified identical to pre-filtering, so
// memory stays flat and loops keep full length). Each band scans at a COPRIME
// length so the bands never re-sync → dense, continuously evolving freeze. The K3
// freeze-point sweep still sets WHERE in the buffer (base position); the bands add
// the incommensurate movement + spectral split. Flag off = the old single-stream
// pinned-anchor freeze (fallback). Tune by ear.
static constexpr bool   VESTIGE_MB_FREEZE   = true;
static constexpr float  VESTIGE_MB_XLO = 250.f, VESTIGE_MB_XHI = 2000.f;   // Hz crossovers
static constexpr size_t VESTIGE_MB_GLEN_LO = 7200, VESTIGE_MB_GLEN_MID = 3840, VESTIGE_MB_GLEN_HI = 1920; // 150/80/40 ms
static constexpr float  VESTIGE_MB_OVERLAP = 2.0f;      // grains per band (Hann @2x = COLA-smooth, lowest CPU).
                                                        // NOTE: total freeze grains = voices x 3 bands x overlap.
                                                        // Multiband is 3x grain-dense vs the old 1-stream freeze.
static constexpr size_t VESTIGE_MB_SCAN_LO = 11987, VESTIGE_MB_SCAN_MID = 8419, VESTIGE_MB_SCAN_HI = 4099; // coprime scan lengths
static constexpr size_t VESTIGE_MB_SPRAY_LO = 480, VESTIGE_MB_SPRAY_MID = 240, VESTIGE_MB_SPRAY_HI = 120;   // samples
// Hard cap on concurrent freeze grains = the CPU ceiling. Above it, grains drop
// (freeze thins) instead of the callback overrunning. Measured: baseline ~24% +
// ~2.8%/grain, so ~20 grains keeps steady load ~80% (safe). SDRAM is cached, so
// the cost is compute (grain count), not memory.
static constexpr int    VESTIGE_MB_GRAIN_CAP = 16;
// Adaptive bands by voice count (poly CPU scaling): 3 bands (rich) up to
// 3BAND_MAX voices, 2 bands (low+high) up to 2BAND_MAX, then 1 full-band cloud
// (no filter — essentially the old single-stream freeze) above that. Keeps every
// voice audible within budget. Overlap stays 2 (min for click-free Hann OLA).
static constexpr int    VESTIGE_MB_3BAND_MAX_VOICES = 2;
static constexpr int    VESTIGE_MB_2BAND_MAX_VOICES = 4;

// ---------------------------------------------------------------------------
// Footswitch timing (FS1 stop)
// ---------------------------------------------------------------------------
static constexpr uint32_t VESTIGE_FS1_TAP_MAX_MS    = 350;  // <= this on release = tap (mute/pause)
static constexpr uint32_t VESTIGE_FS1_CLEAR_HOLD_MS = 700;  // >= this while held = clear all

// ---------------------------------------------------------------------------
// Continuous-auto capture (SW1 MIDDLE)
// ---------------------------------------------------------------------------
static constexpr float    VESTIGE_ENV_COEF        = 0.008f; // input |env| one-pole (~60 Hz; 0.002 was too lazy for onsets/short samples)
static constexpr float    VESTIGE_AUTO_THRESH_MIN = 0.005f; // K2 CCW: sensitive
static constexpr float    VESTIGE_AUTO_THRESH_MAX = 0.10f;  // K2 CW:  insensitive
static constexpr float    VESTIGE_AUTO_HYST       = 0.55f;  // close threshold = open * hyst
static constexpr uint32_t VESTIGE_AUTO_RELEASE_MS = 80;     // silence held this long ends a phrase

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
static constexpr float  VESTIGE_CRUSH_BITS_HI  = 16.f;   // bit depth near noon
static constexpr float  VESTIGE_CRUSH_BITS_LO  = 2.5f;   // bit depth at full CW

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
static constexpr float  VESTIGE_FADE_MIN_S         = 0.003f; // K5 CCW floor: declick, not a 1-sample step (voice-steal click)
// Output routing (vestige owns its mix): K6 = looper volume (0 → unity at noon
// → boost at CW), SW2 = dry (clean) routing. Both gains one-pole smoothed.
static constexpr float  VESTIGE_LOOP_BOOST_MAX = 2.0f;   // K6 full CW = +6 dB on the looper
static constexpr float  VESTIGE_ROUTING_SMOOTH = 0.003f; // ~7 ms smoothing for K6 / dry-gate
// Concurrent-voice cap: total granulating voiced voices (live + fading) is
// bounded to VESTIGE_MAX_VOICES — the pre-regression ceiling the CPU/grain pool
// handled fine. Beyond it, the oldest is "stolen": fast-released over
// VESTIGE_STEAL_RELEASE_S (declicked) so its slab frees quickly. Classic
// synth-style voice-stealing — a new note reclaims the oldest, cutting its tail.
static constexpr float  VESTIGE_STEAL_RELEASE_S = 0.006f; // fast release on voice-steal (~6 ms)
static constexpr float  VESTIGE_FRIP_OD_RAMP_S = 0.005f; // overdub input fade in/out (declick record in/out)
static constexpr float  VESTIGE_FRIP_DECAY_MIN = 0.20f;  // fast tape decay (frippertronics, just past noon, ~1 repeat)
static constexpr float  VESTIGE_FRIP_DECAY_MAX = 1.0f;   // infinite sustain (frippertronics, full CW)

// ---------------------------------------------------------------------------
// LED blink (Controls runs every ~10 ms)
// ---------------------------------------------------------------------------
static constexpr int    VESTIGE_BLINK_SLOW = 50;  // *10 ms → 500 ms half-period
static constexpr int    VESTIGE_BLINK_FAST = 12;  // *10 ms → 120 ms half-period
static constexpr int    VESTIGE_FLASH_TICKS = 30; // clear-confirm flash duration
