# mnemonic — Implementation Plan

Companion to `mnemonic-concept.md`. Goal, per the ChronoTron3 ethos: **testable
starting points that explore range**, not an end-user-safe pedal. Build the
extremes (varispeed slur, self-oscillation, warble/decimation, loop) first;
safeguards and musicality are a later joint stage.

Target: `pedals/chronotron3/modules/mnemonic.h` (currently a passthrough stub) +
a new `pedals/chronotron3/modules/mnemonic_constants.h`. Bundle: `make
PEDAL=chronotron3`. Module interface is `pedals/chronotron3/module.h`
(`Init` / `Activate` / `Deactivate` / `Controls` @ ~10 ms / `Process` @ audio
block / optional `OwnsOutput`).

## Firmware translation (prototype → firmware)

Per `agents-instructions.md`, the concept is written like a pedal in the hand;
these are the execution-model gaps the firmware must close. Flagging them now is
where the bugs would otherwise hide.

- **Control rate vs audio rate.** `Controls()` runs ~every 10 ms; `Process()`
  runs per audio block (48 kHz). Everything the ear tracks continuously — the
  varispeed read tap, feedback gain, wow/flutter, gesture ramps, filter coefs —
  must be **smoothed/interpolated at audio rate** from control-rate targets, or
  it will zipper. Delay-time target is set in `Controls()`; the tap **glides** in
  `Process()`.
- **"Changed vs differs."** Knob reads jitter (ADC noise) and are polled, not
  event-driven. K1 needs a dead-zone before it re-targets the delay time, or the
  tap will micro-slur on noise. Division mode must quantise K1 to the 11 ratio
  stops with hysteresis at the stop boundaries (no flicker between adjacent
  divisions).
- **Footswitch timing is multi-tick.** Tap-vs-hold, tap-tempo intervals, the loop
  min-gate, and the both-FS bootloader gesture are all measured across polls. Use
  a monotonic sample/tick counter (the bundle already runs one) — **not**
  `Date.now()`-style wall clock. Tap intervals are measured in the audio-callback
  sample count for accuracy, latched for `Controls()` to read.
- **Atomic-looking state changes span ticks.** Starting/stopping the loop,
  entering/leaving bypass, and committing a new delay time each touch multiple
  variables. Guard against half-applied states (e.g. loop playback reading a
  buffer whose length was not yet committed). Prefer a single "commit" flag the
  audio thread reads.
- **ISR/control race.** Record-write and loop-commit, like vestige, are not
  lock-clean across the audio ISR and the control loop. vestige chose
  glitch-tolerant; do the same unless it bites.
- **Varispeed is not a demo crossfade.** Any high-level "delay" mental model that
  silently repitches by swapping taps is wrong here — the single-tap glide is
  mandatory and is the whole effect.

## Reuse map (what exists in `src/core/blocks`)

- **`ring_buffer.h`** — mono circular buffer in externally-allocated memory
  (SDRAM). Has `ReadFrac(pos)` (linear-interpolated fractional read) — this is
  the varispeed read tap. Allocate the delay + loop buffers as `DSY_SDRAM_BSS`
  slabs (see vestige's `vestige_slab`).
- **`bitcrush.h`** + Mode B decimator / `glitch_zones.h` — K3-CCW BBD/digital
  decimation and glitch.
- **NitroTron3 Mode B feedback bus** (`pedals/nitrotron3/main.cpp` ~700–820:
  `FB_SAT_DRIVE`, `FEEDBACK_MAX`, tanh return, build-up ducker) — copy the
  topology for K2 feedback + tape-saturation self-limiting. **HARD RULE:** wet
  only; dry/summed output untouched.
- **Filters** — `mode_a_hpf.h` (2-pole HPF), `moog_ladder*.h`, or a small SVF for
  the K4 tilt + K5 peak/BPF. Pick the lightest that gives the tilt+resonance; a
  hand-rolled SVF (LP/BP/HP from one core) is likely cleanest for the K4/K5 pair.
- **`peak_limiter.h`** — only if a wet-path limiter is wanted on top of tanh;
  never on the dry/sum.
- Interpolation: linear (`ReadFrac`) is the BBD-honest choice. Allpass/cubic is
  an option if linear-interp HF loss during fast slews is objectionable — but the
  HF loss is arguably *desirable* (BBD character). Start linear.

## Memory sizing

At 48 kHz, mono float (4 B/sample):

- **Delay buffer** — sized to the worst-case delay. With knob-time max 3 s, or
  tap-max ~2 s × division 4/1, the ceiling is ~**8 s** → 8 × 48000 × 4 ≈ **1.5
  MB** (round up with wrap-guard headroom). Trivial in 64 MB SDRAM.
- **Loop buffer** — proposed max ~**16 s** → ~3 MB. (Confirm max loop length.)
- Total well under vestige's ~11 MB. No pressure.

Sizes are compile-time constants in `mnemonic_constants.h` (assume 48000 literal,
like `vestige_constants.h`).

## Staging

Each stage compiles and is playable. One concern per stage; tune by ear before
moving on. Do **not** run `make` after pure tuning edits — the user flashes.

### M0 — Skeleton + straight delay
Replace the passthrough. Single `RingBuffer` delay, fixed time, K6 wet blend
(shell mix first; decide `OwnsOutput` at M6). No feedback, no filter. Proves the
module wiring and buffer allocation. **Milestone: a clean repeat.**

### M1 — Varispeed read tap (the identity)
Delay time set by K1 (SW2 UP, knob-time) into a control-rate **target**; audio
thread slews a `read_delay_` toward it and reads via `ReadFrac`. Verify the
**pitch bends** on a fast K1 turn (the whole point). Add the `glide-rate`
constant. **Milestone: turning K1 slurs the pitch like tape.**

### M2 — Feedback + tape saturation
K2 feedback 0 → self-oscillation, tanh in the loop, `FEEDBACK_MAX` ceiling. Port
Mode B's build-up ducker if oscillation clips the converters. Confirm the
sacrosanct rule holds (scope the dry). **Milestone: controllable runaway
oscillation that saturates, not screams.**

### M3 — Filter + EQ (K4 / K5), in-loop
K4 tilt (LPF↔neutral↔HPF), K5 peak/BPF at the corner, placed **inside** the loop
so repeats age. Add a compile-time switch for the post-loop placement A/B.
**Milestone: repeats darken/thin/focus as they recirculate.**

### M4 — Degrade character (K3) + wow/flutter
K3 bipolar: CW tape (saturation coloration + wow/flutter LFO on the read tap +
progressive HF loss); CCW BBD/digital (decimation via `bitcrush`/decimator,
gentle/rounded → glitch at the extreme). Base tape drive stays on at all K3
positions. **Milestone: both lo-fi extremes audible, clean-ish at noon.**

### M5 — Tap tempo (SW2 MID) + K1 divisions
Tap detection in the audio callback (sample-accurate intervals), median of the
window, slowest-tap clamp, listening-window grouping. K1 quantises to the 11-stop
ratio table with boundary hysteresis. New tempo/division **glides** the tap
(consistent with M1). LED 1 blinks the delay clock. **Milestone: tap a tempo,
dial divisions, LED tracks.**

### M6 — Bypass / kill + output ownership
FS2 tap = gate send + gate loop send, **trail rings out and decays** (this is why
mnemonic likely takes `OwnsOutput()` — it must keep emitting wet while the send
is muted, and keep dry sacrosanct). FS2 hold = kill (clear buffer + delete loop).
Decide `OwnsOutput` here. **Milestone: bypass leaves a decaying trail; kill is
instant and total.**

### M7 — SW1 tape gestures (FS1 hold)
FS1 tap = tap tempo (M5); FS1 hold, per SW1: UP spin-up (time↓/pitch↑ + fb↑),
DOWN slow-down (time↑/pitch↓ + fb↑), both slewed, separate UP/DOWN ramp
constants; release slews back to K1/K2. **Milestone: held dive-bomb and swell,
smooth return.**

### M8 — Hold / loop (SW1 MID)
Clean-signal loop recorder. Press = record from down (accurate start), release =
commit + play. Loop plays **into the delay input** in parallel with live dry.
Bypass pauses/resumes; kill deletes. **Milestone: Hazarai-style loop feeding the
delay.** *(Superseded by the FS1 rework below — now a two-buffer scratch→commit,
REPLACE-not-overdub, buffer-full = auto record-end.)*

### M9 — SW2 DOWN (rhythmic taps): capture-the-rhythm
**Ruled:** build **capture-the-rhythm** multi-tap first — the tap gesture records
the inter-tap intervals into a short pattern the delay replays (scaled if the
tempo is re-tapped). Euclidean-preset and "The Edge" fixed multi-tap are kept in
evidence as later SW2-DOWN variants, not built now.

## Constants (initial `mnemonic_constants.h` sketch — all tune-by-ear)

Names indicative; values are starting brackets to bracket the range, not final.

- `MNEM_SR = 48000.f`
- Buffers: `MNEM_DELAY_MAX_S` (~8), `MNEM_LOOP_MAX_S` (~16), derived sample caps
  + wrap-guard.
- Time: `MNEM_TIME_MIN_MS` (20), `MNEM_TIME_MAX_MS` (3000), knob taper exp.
- Glide: `MNEM_GLIDE_RATE` (knob/tap slew), `MNEM_GESTURE_UP_RAMP_S`,
  `MNEM_GESTURE_DOWN_RAMP_S`, `MNEM_GESTURE_RETURN_S`.
- Feedback: `MNEM_FB_MAX` (~1.1–2.0), `MNEM_FB_SAT_DRIVE`, ducker thresh/att/rel.
- Gesture feedback targets: `MNEM_GESTURE_UP_FB`, `MNEM_GESTURE_DOWN_FB`
  (different per brief).
- Degrade: `MNEM_TAPE_DRIVE_BASE`, `MNEM_WOW_HZ` / `_DEPTH`, `MNEM_FLUTTER_HZ` /
  `_DEPTH`, `MNEM_HF_LOSS_MAX`; `MNEM_BBD_DECIM_MAX`, `MNEM_BBD_BITS_MIN`.
- Filter: `MNEM_TILT_*`, `MNEM_PEAK_Q_*`, `MNEM_BPF_BLEND`, `MNEM_EQ_IN_LOOP`
  (bool A/B).
- Tap: `MNEM_TAP_WINDOW_S` (~3), `MNEM_TAP_MAX_INTERVAL_S` (~2),
  `MNEM_TAP_MIN_INTERVAL_S`, hysteresis for division stops.
- Loop: `MNEM_LOOP_MIN_S` (~0.3–0.5).
- FS: `MNEM_LONGPRESS_MS` (reuse bundle value if one exists).

## As-built rework — FS1 unified hold-then-commit (post-M9)

The stage-1 M5–M8 FS1 handling was reworked after review into one model (see
`mnemonic-concept.md` § FS1). Landed:

- **Downpress is the universal event; press length disambiguates.** Released
  before `MNEM_TAP_RELEASE_MS` (300) = tap (tempo/rhythm, *all* SW1 positions);
  held past `MNEM_LONGPRESS_MS` (450) = SW1-latched sustained gesture (MID loop /
  UP spin-up / DOWN slow-down); deadzone between = no-op. Taps and the loop/tape
  gestures now coexist — the old "loop dedicates FS1" constraint is gone.
- **Taps commit on release but are timed from the downpress** (`RegisterTap` takes
  the down timestamp), so tempo accuracy is release-independent.
- **SW1 latched at downpress** (`f1_mode_`); mid-press flips take effect next press.
- **Loop = two SDRAM slabs, pointer-swap commit, REPLACE not overdub.** Scratch
  records from the downpress; commits only when the press becomes a sustained
  gesture, so a short tap never disturbs a playing loop. Buffer-full raises a
  `volatile` flag the control loop treats as an auto record-end (vestige pattern).

## Decisions to collect (block the stages that need them)

- **M9 / SW2 DOWN** — rhythmic-tap design (hard gate on M9). *(Ruled:
  capture-the-rhythm; built.)*
- **M1/M5** — knob-time range + taper, tap-max interval, division snap-vs-glide.
- **M3** — K5 topology (peak vs peak+BPF) and EQ placement (in-loop vs post) — but
  build both so the ruling is an ear test, not a rewrite.
- **M2** — which feedback safeguards to port and their tunings.
- **FS1 thresholds** — tune `MNEM_TAP_RELEASE_MS` / `MNEM_LONGPRESS_MS` + the
  deadzone width by feel.
- **M8** — loop max length; whether to add an overdub mode later.
- **LEDs** — final blink vocabulary consistent with vestige.

## Not doing (v1)
- No INSTRUMENT profile (bundle-wide decision — one control set for all).
- No dedicated bypass footswitch beyond FS2's module-owned behaviour; both-FS
  hold = DFU stays shell-reserved.
- No true polyphony / pitch tracking — mnemonic is a delay, not a synth.
