# ChronoTron3 — Sprawl port plan (Mode B → module, SW3 DOWN)

> **Status: implemented + independently audited (2026-09-11), first flash test OK; FS2 changed to tap-trail + hold-panic.** Replaces *armitage*
> in the SW3-DOWN slot with a **lossless, 1:1 port** of NitroTron3's Mode B
> (Sprawl, granular delay). UI (knob/switch re-assignment, FS1 tap tempo) is a
> *later* design phase — this pass only moves the mode onto the Module interface
> and cuts it into swappable pieces so that phase can shuffle components freely.

## Goal / non-goals

- **Goal:** `pedals/chronotron3/modules/sprawl*.h` implements Mode B exactly as
  `pedals/nitrotron3/main.cpp::ProcessGranular` + its globals/helpers do today —
  same signal path, same per-sample order of operations, same constant values,
  same control mapping (K1–K5, SW1, SW2). Sound must be indistinguishable.
- **Goal:** internal decomposition into components with a single
  control-mapping seam, so the next phase can re-wire controls without touching
  DSP.
- **Non-goal:** presets (FS1 stays unused; tap tempo may land there later).
- **Non-goal:** any tuning, any "improvement", any new stage. One exception is
  forced by the bundle (see *Deliberate deviations*).
- **Non-goal:** touching `src/core/` or `pedals/nitrotron3/`. NitroTron3's
  binary must stay byte-identical (bass md5 `3d8f6e8fef93bd9bf8e302be3e72602c`
  at plan time).

## Source inventory (what gets ported)

Everything Mode B in `pedals/nitrotron3/main.cpp`:

| Original (main.cpp) | Role |
|---|---|
| `ProcessGranular()` (≈ lines 556–1190) | per-block control mapping + per-sample loop + reverb block + mix |
| Mode B globals (≈ lines 55–245): `grain_sdram_buf`, `grain_ring`, `b_shifter`, `grain_voices[8]`, scheduler state, `trans_*`, `harmony_*`, `prev_wet`, `wet_hp_*`, `fb_duck_*`, `on_play_*`, resamplers, `reverb_buf`, `reverb_instance`, `reverb_amt_smooth`, `Smoother`, `glitch_events`, `RandFloat` (xorshift32, seed 12345) | DSP state |
| Helpers (≈ lines 365–425): `RESONANCES[12]`, `K1ToSemi`, `FixedIntervalFeedbackScale`, `GrainPitchRatio` | harmony logic |
| `main()` init (≈ lines 1615–1650): `env.Init/SetCutoff(ENV_LP_CUTOFF_HZ)`, `tracker.Init`, `grain_ring.Init`, `b_shifter.Init`, `wet_hp_coeff`, duck/on-play coefs, resampler + reverb init (`amount 1.0`, `REVERB_INPUT_GAIN`, `REVERB_TIME`), `glitch_events.Init()` | init |
| main loop: `tracker.Update()` every 10 ms | YIN in the control loop |
| Shared blocks used: `EnvFollower env`, `PitchTracker tracker` | shared with A/C in NitroTron3; module-private here |

Constants (all values copied verbatim):

- `pedals/nitrotron3/constants.h` Mode B block (≈ lines 529–650): `GLITCH_*`,
  `FREQ_SHIFT_*`, `WET_HPF_FREQ`, `K5_CENTER_DEADZONE`, `REVERB_INPUT_GAIN`,
  `REVERB_TIME`, `FEEDBACK_MAX`, `FB_UNISON_SCALE`, `REVERB_AMT_SMOOTH_COEF`,
  `PARAM_SMOOTH_COEF`, `GRAIN_*`, `CLOUD_LEN_MAX`, `TRANSIENT_*`.
- `ENV_LP_CUTOFF_HZ` — instrument-profiled in NitroTron3 (`50` bass / `80`
  guitar). ChronoTron3 is one firmware for both, so the **bass value 50 Hz** is
  taken (it is the shipped default). Named `SPRAWL_ENV_LP_CUTOFF_HZ`.
- File-local constants from main.cpp: `GRAIN_BUF_SAMPLES` (8 s),
  `NUM_GRAIN_VOICES` (8), `GRAIN_MIN_RANGE` (4800), `FB_SAT_DRIVE` (8),
  `FB_DUCK_THRESHOLD/ATTACK_MS/RELEASE_MS` (0.20/500/800), `ON_PLAY_*`
  (400 ms/0.02/10/0.5), `RESAMPLER_CUTOFF_HZ/PROTO_FS_HZ` (15 k/96 k).

Core blocks reused unchanged: `ring_buffer.h`, `grain_voice.h`,
`freq_shifter.h`, `glitch_zones.h`, `env_follower.h`, `pitch_tracker.h`,
`resampler.h`, `clouds/reverb.h`, `knob_map.h`.

## Target layout

```
pedals/chronotron3/modules/
  sprawl.h                 class Sprawl : Module — shell hooks, control snapshot,
                           FS2 bypass, DeriveParams() seam, per-sample loop order
  sprawl_constants.h       every tuning constant (source of truth), original names
  sprawl_harmony.h         RESONANCES, K1ToSemi, FixedIntervalFeedbackScale,
                           GrainPitchRatio, xorshift RNG
  sprawl_grain_engine.h    ring + 8 GrainVoices + scheduler + burst + pitch cache
  sprawl_texture.h         SW1 texture shaper: decim/fold · GlitchEvents · ringmod
  sprawl_feedback.h        feedback return: 2-pole HPF · build-up ducker ·
                           on-play ducker · tanh saturator
  sprawl_reverb.h          48→32 kHz resamplers · Clouds reverb · K5-CCW blend
```

**The seam that the UI phase edits:** `SprawlControls` (raw K1–K5 0..1 + SW1 +
SW2, captured in `Controls()`) → `SprawlParams DeriveParams(const SprawlControls&)`
(all the per-block knob math that today sits at the top of `ProcessGranular`).
Everything downstream consumes `SprawlParams` only. Re-assigning a knob or a
toggle = editing `DeriveParams`, nothing else.

### Component contracts (per-sample order is the invariant)

For each sample `i`, in **this order** (identical to `ProcessGranular`):

1. `dry = in[i] * send` (`send` = bypass ramp, see deviations; 1.0 when active)
2. `env = env_follower.Process(dry)`
3. note-on detector (`trans_slow`, refractory, `TRANSIENT_*`) → `note_on`;
   if `note_on && glitch_amount > 0.01` → arm burst (`grain_burst_left =
   TRANSIENT_BURST`, `grain_timer = 0`)
4. `tracker.Feed(dry, env)`
5. **feedback bus** from `prev_wet`: build-up ducker → on-play ducker (from
   `env`) → 2-pole HPF → `tanh(fb_hp · fb_amt_eff · FB_SAT_DRIVE)/FB_SAT_DRIVE`;
   `ring.Write(dry + that)`
6. **grain engine**: scheduler tick (`grain_timer--`, trigger with delay /
   scatter / direction flip / pitch re-roll cache / per-grain length variation +
   coupled loops / read-overrun safety / never-steal voice pick / burst spacing
   vs jittered interval), then `wet = Σ voices.Process(ring)`
7. **texture shaper** by SW1: `0` decimator+sinefold (bipolar K4), `1`
   `GlitchEvents::Process(wet, side, effect_pos, env, note_on)`, `2` ringmod
   (tremolo <30 % / bell partials with keytracked one-pole LP)
8. if SW2 DOWN: `wet = freq_shifter.Process(wet)`
9. `prev_wet = wet`; `wet_block[i] = wet`

After the loop: reverb pipeline on `wet_block` (downsample → Clouds → upsample
L/R), then per sample `reverb_amt_smooth += REVERB_AMT_SMOOTH_COEF·(ra − s)`,
`out_wet[i] = wet_block[i]·(1−s) + 0.5·(rev_l[i]+rev_r[i])·s`.

The **module returns wet only**; the shell applies K6 (`MixCurve(RemapKnob(K6))`,
equal-power, one-pole smoothed at `CT3_MIX_SMOOTH = 0.002`) — the same curve and
the same coefficient as NitroTron3's `sdg_b/swg_b` (`PARAM_SMOOTH_COEF = 0.002`).
`OwnsOutput() = false`.

### Dropped: dormant stutter engine

`ProcessGranular` still carries the retired probabilistic micro-stutter inside
an `if (false) { … }` block (`StutterVoice`, `stutter_buf[9600]` = 38 KB SRAM,
`STUTTER_*`). It is unreachable code — the live-grain path replaced it. It is
**not ported**. Zero sound impact; frees 38 KB of the 480 KB SRAM budget. The
history stays in git (`pedals/nitrotron3/main.cpp`).

## Firmware translation (NitroTron3 → ChronoTron3 shell)

| Divergence | NitroTron3 today | ChronoTron3 module | Handling |
|---|---|---|---|
| Knob source | `preset.GetEditBuffer().knobs[]`, refreshed from hardware every 10 ms `Tick`, read once per 1 ms audio block | `cs.Knob(i)` read in `Controls()` every 10 ms | Same 10 ms cadence. `Controls()` stores a `SprawlControls` snapshot; `Process()` runs `DeriveParams` once per block exactly as before. No extra smoothing added. |
| Switch source | `eb.sw1 / eb.sw2` | `cs.Switch(0) / cs.Switch(1)` (0=UP,1=MID,2=DOWN — same encoding) | 1:1 |
| K6 mix | inside `ProcessGranular` (smoothed sqrt gains) | shell | Same curve/coefficient; module emits wet only |
| Pitch tracker | `tracker.Update()` in `main()` loop, bass profile `TRACK_*` | `tracker_.Update()` in `Controls()`, ChronoTron3's **instrument-agnostic** `TRACK_*` profile (30–600 Hz, robust path on) | Only consumer in Sprawl is the ringmod carrier (SW1 DOWN, K4 ≥ 30 %). Profile difference is a bundle-level decision, not a Sprawl decision — see deviations |
| Bypass | preset system: `out = in`, mode not processed | module FS2 tap → `Bypassed()`; shell lifts dry to unity | See deviations (trail bypass per bundle rule G2) |
| LEDs | preset system owns both | module owns both | LED1 off (no preset system). LED2 = solid active / off bypassed (bundle convention) |
| `GLITCH_*` visibility | `glitch_zones.h` does `#include "constants.h"` and reads globals from the pedal's constants | `-Ipedals/chronotron3` resolves that include to the bundle constants, which do not define `GLITCH_*` | `sprawl.h` includes `sprawl_constants.h` **before** `glitch_zones.h`; the names are plain global `constexpr`, so they resolve. Comment this ordering in both headers. Cleanup candidate: give `GlitchEvents` a config struct (would touch core → separate, checksum-gated task) |
| Block size | 48 | `CT3_BLOCK_SIZE = 48` | Size scratch arrays by `CT3_BLOCK_SIZE` (`wet_block`, `rev` mid buffers ⌈48·2/3⌉ = 32) |
| Denormals | not flushed | shell sets FPSCR.FZ | Free win, no code |
| Boot window | edit buffer populated before audio start | audio starts ~10 ms before the first `Controls()` tick | `SprawlControls` knobs default to 0.5 (noon): live passthrough, neutral stream, K5 off. Inaudible either way (buffers empty), but avoids deriving `reverb_amt = 1` / full reverse buffer from all-zero knobs |

## Deliberate deviations (flag list — user rules where noted)

1. **FS2 = trail bypass (tap) + panic (hold), mnemonic-style — user decision
   2026-09-14.** ChronoTron3 has no relay; bundle guardrail G2: bypass gates
   the *send* and lets the wet ring; the clean path is never processed.
   - **Tap** (released before `SPRAWL_LONGPRESS_MS` = 450 ms): `send` ramps
     1→0 over ~3 ms, module keeps running, `Bypassed()` = true. The feedback
     loop is NOT gated, so a K5-CW drone rings on through bypass (audit finding:
     the feedback bus is self-sustaining off `prev_wet`). That is the intended
     trail behaviour; the escape is the hold.
   - **Hold** ≥ 450 ms: lands in bypass and engages `panic_env_` (fade
     `SPRAWL_PANIC_FADE_MS` = 120 ms), which multiplies the feedback amount
     (throttles the recirculation itself, so even feedback ≥ 1 collapses) and
     the wet output (reverb included). Once faded the control thread wipes the
     ring and hard-stops the grain voices. Re-engage (any later tap) rises over
     15 ms. Reverb + feedback-return filter state are left to decay (muted).
   - Active state: `send = 1`, `panic_env_ = 1` → identical to NitroTron3.
2. **FS1 = tap tempo, LED1 = echo clock — user decision 2026-09-14.**
   NitroTron3 had the preset system here. One tap interval = the whole echo
   time; there are no subdivisions (mnemonic's divisions are a delay concept, a
   granular buffer has only one span).
   **What the tap actually sets.** The audible echo is NOT `max_range` (the
   buffer span) but the grain read-back depth `base_delay`. Two wrong passes
   before this one, both worth recording:
   1. Tap + LED1 drove `max_range` — audibly 8x long at the top.
   2. Tap inverted `base_delay = max(max_range/8, grain_len)`, i.e. it respected
      the `>= grain_len` floor. That floor pins the echo TO the grain length, so
      on K3-CCW (grains up to 4 s) no short tap was reachable at all.
   **The floor is a trails aesthetic for the knob, not a physical limit.** The
   real read-overrun guard is the per-grain safety at trigger time, which only
   binds for reverse and pitch-up grains; a forward unison grain stays exactly
   `base_delay` behind the write head indefinitely. So **when a tap is active the
   echo is pinned to the tapped time verbatim and the floor is skipped**; with no
   tap the knob path is untouched, floor included.
   `K2ForTap()` then picks the K2 magnitude: the knob's own `max_range/8`
   inversion, raised if needed so the span physically holds the tapped echo plus
   a grain of headroom (linear in k2, always solvable). That headroom term is
   what makes short taps reachable in deep cloud.
   **Result:** taps are exact from 100 ms to 7 s at K3 noon and CW. In CCW cloud
   the long end is limited by the grain size itself (~6.4 s at k3mag 0.3, 4 s at
   full CCW); short taps are exact everywhere.
   **K2 arbitration (EHX-style, last gesture wins):** a tap overrides the
   knob's length; moving K2 more than `SPRAWL_K2_MOVE_EPS` (2 % travel) drops
   the tapped value and hands length back to the knob. K2 keeps its direction
   and noon-deadzone job in both cases — only the magnitude is overridden.
   Length changes are a **hard cut** — deliberately no varispeed glide, unlike
   mnemonic's tape read tap.
   LED1 flashes once per `base_delay` (grain rate in live mode), re-synced on
   each tap — so it matches the knob too, not just the tap.

3. **FS1 long-press = buffer FREEZE (hold), toggle — user decision 2026-09-14.**
   **Mechanism (literal, minimal):** freeze simply stops the ring write. Nothing
   else changes — the grain engine, texture, shifter, reverb and both duckers
   keep running on the held material. `RingBuffer::Write` is what advances
   `write_pos_`, and every grain anchors its read to `write_pos_` at trigger
   time, so a frozen write head means every later grain reads the same held
   region. No loop scanner, no capture buffer, no crossfade.
   **What is held:** the dry AND the feedback return, since both go into the
   same ring write. So K5-CW stops accumulating while frozen — the buffer is
   genuinely held, not slowly overwritten by its own tail.
   `Inject()` is still called and its result discarded, so the build-up and
   on-play duck envelopes stay live and unfreezing has no stale-state jump.
   **Gestures.** FS1 gets mnemonic's hold-then-commit disambiguation, since it
   already carries tap tempo:
   - released < `SPRAWL_TAP_RELEASE_MS` (300 ms) = **tap** (timed from the
     DOWN-press, so tempo accuracy is release-independent)
   - held >= `SPRAWL_LONGPRESS_MS` (450 ms) = **freeze toggle**, fired once on
     crossing the threshold (not on release), so the state flips under the foot
   - released in the 300–450 ms deadzone = no-op
   - FS2 long-press (panic) also clears freeze — it is the global escape.
   **LED1** inverts while frozen: mostly on with a brief gap at each clock
   period, so the buffer clock stays readable and the held state is obvious.
   **Emergent side-effect, flagged:** with the write head parked, consecutive
   grains replay the SAME slice. At K3-CW the per-grain scatter shuffles chunks
   out of the held buffer (varied). At K3 noon/CCW scatter is zero, so overlapping
   identical copies offset by the emission interval will comb — a static,
   metallic hold rather than a moving one. That is inherent to a literal freeze
   of this engine; if it is not the wanted sound the fix is a scanning read
   offset, which is a new mechanism and deliberately NOT built here.

4. **Pitch-tracker profile** = the bundle's agnostic one, not NitroTron3's bass
   profile. Affects only the ringmod carrier's tracked pitch.
5. **Envelope LP 50 Hz** (bass value) fixed for both instruments.
6. **Dormant stutter code not ported** (unreachable).
7. **Armitage is unlinked, not deleted.** `armitage.h`/`armitage_constants.h`
   stay on disk out of the build until the user says delete. The debug-log
   block for it leaves `main.cpp`.

## Shell changes

- `constants.h`: `CT3_MODE_ARMITAGE` → `CT3_MODE_SPRAWL` (SW3 DOWN). Update the
  TRACK-profile comment: consumer is now Sprawl's ringmod keytracking.
- `main.cpp`: include/instantiate `Sprawl` instead of `Armitage`; remove the
  armitage snapshot logging block. `StartLog(false)` stays (generic serial
  debug hook, see `reference_daisy_serial`).
- Makefile: no change.

## Verification

1. `make PEDAL=chronotron3` builds; report `text/data/bss` and the linker's
   SRAM (`DTCMRAM`/`SRAM`) usage vs the 480 KB budget. Baseline before port:
   text 131024, data 12800.
2. `make PEDAL=nitrotron3` → `md5 -q build/NitroTron3.bin` must equal
   `3d8f6e8fef93bd9bf8e302be3e72602c` (core untouched).
3. Port audit: for every constant name in the inventory, the value in
   `sprawl_constants.h` equals the NitroTron3 value (grep-diff). For every
   numbered step above, a side-by-side read of old vs new confirms identical
   arithmetic and order.
4. On hardware (user): SW3 DOWN; K2 noon = live grain passthrough at unison
   (dry-like); K2 CW/CCW = echo forward/backward; K3 CCW smear / CW glitch;
   SW1 UP K4 sweep; SW1 MID glitch events on note-on; SW1 DOWN tremolo→bell;
   SW2 DOWN K1 shift; K5 CCW reverb / CW feedback self-limits; FS2 tap bypass
   with trail.

## Later (not this pass)

- UI phase: re-map controls in `DeriveParams`, FS1 tap tempo, 3-way switch
  re-assignment.
- `GlitchEvents` config struct (core change, checksum-gated).
- Backport of the module architecture to NitroTron3 (ARCHITECTURE.md step 6).
- Delete armitage sources once confirmed.
