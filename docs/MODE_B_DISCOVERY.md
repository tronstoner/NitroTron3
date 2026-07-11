# Mode B — Sprawl — Bipolar K2/K3 Redesign (Discovery / Implementation Plan)

Status: **planned, not yet implemented.** Source code is the source of truth for
current behavior; this doc plans a change on top of it. Inspired by fresh
listening to Chase Bliss **Mood 2** demos.

This redesign turns Mode B's two granular-shape knobs (K2 buffer, K3
character) into **bipolar** controls with a padded neutral zone at noon, and
adds two capabilities on top: **very short grains** and **pitch-controlled
micro-stutter**. The overriding constraint: **do not regress the ear-tuning
already done on this mode.** The CW half of each knob reproduces today's tuned
behavior; the new material lives on the CCW halves and at the extremes.

---

## Goals

1. **K2 bipolar** — noon = passthrough (padded); magnitude = buffer length +
   timescale (as today); **sign = global playback direction** (CW forward,
   CCW backward), replacing randomized per-grain reverse as the *direction*
   source.
2. **K3 bipolar** — noon = neutral (padded); CW = today's character/glitch;
   CCW = MI-Clouds-style **deterministic density** (sparse→dense, fixed grain
   length, no event randomization).
3. **Very short grains** — allow grain lengths down to ~1 ms without clicks.
4. **Pitch-controlled micro-stutter** — when SW2 = UP/MID, K1's pitch setting
   also sets the micro-stutter slice playback speed.

---

## Does the current architecture support the Clouds CCW approach? — Yes

The grain scheduler in `ProcessGranular` (NitroTron3.cpp:560) already emits
grains on a timer, each with an independent length, pitch ratio, read offset,
reverse flag and loop count. The Clouds-style CCW mode is therefore just a
**parameter regime inside the existing scheduler** — no new DSP blocks:

- emission interval swept by a density param (sparse→dense), decoupled from
  grain length;
- grain length held fixed;
- `scatter = 0` (coherent read point), `jitter = 0` (metronomic timer),
  `loops = 1`, direction from the global K2 sign.

**The one real constraint is the 8-voice ceiling** (`NUM_GRAIN_VOICES`). Max
simultaneous grains = 8, so max density ≈ 8 / grain_len; a dense cloud of
*long* fixed grains will hit the ceiling and silently drop grains (the
scheduler never steals a live voice — NitroTron3.cpp:913). With short grains
this is a non-issue. If dense long-grain clouds are wanted later, raise
`NUM_GRAIN_VOICES` — this costs one `ReadFrac` + one `cosf` per voice per
sample (CPU, not FLASH/RAM), measurable but likely fine at 12–16.

---

## Resolved decisions

| # | Decision | Choice |
|---|----------|--------|
| 1 | Clouds-mode grain length source | **Fixed base × K2's existing timescale** (`k2_scale`, 0.5×–2×). Deeper K2 → longer cloud grains. This is "the grain length of knob 2." |
| 2 | K2-noon zone (direct-texture) K3 | **Keep micro-stutter, make it bipolar too**: noon = no stutter (padded), noon→CW = today's probabilistic stutter, CCW = deterministic density mirror. |
| 3 | Reverse handling | **Keep randomization in K3-CW character mode, but flip its bias with K2 sign.** K2 forward → mostly-forward w/ occasional reverse (as today); K2 backward → mostly-backward w/ occasional forward. In K3-CCW Clouds mode, direction is deterministic (= K2 sign). |
| 4 | Direct-texture K3-CCW behavior | **Idea A — metronomic granulation**: regular-rate short-slice replay of the live capture buffer, sparse→dense, no randomization, direction = K2 sign. Mirror of the buffer-engaged Clouds mode; reuses the `StutterVoice` pair. |
| 5 | Pitch-controlled micro-stutter | **Yes** — SW2 UP/MID: micro-stutter slice playback rate = `GrainPitchRatio(harmony, k1)`, sampled once at slice trigger. SW2 DOWN: unison (Bode shifter handles that path). |

---

## Refined control map

| | CCW half | Noon (padded) | CW half |
|---|---|---|---|
| **K2** | buffer length grows + **backward** read | direct-texture passthrough | buffer length grows + **forward** read (as now) |
| **K3** (buffer engaged) | Clouds density: fixed len, sparse→dense, no rand, dir = K2 | single coherent stream, neutral | today's character/glitch (random reverse, bias flips with K2) |
| **K3** (K2-noon / direct-texture) | metronomic granulation of live buffer, sparse→dense, dir = K2 | clean (no stutter) | today's probabilistic micro-stutter |

Everything else in Mode B is untouched: SW2 harmony/pitch, K4 texture shaper,
K5 reverb/feedback + duckers, wet HPF, K6 mix, LEDs, presets.

---

## New constants (`src/constants.h`, Mode B block ~L350–378)

| Constant | Value | Purpose |
|---|---|---|
| `GRAIN_K2_DEADZONE` | `0.06f` | K2 noon passthrough pad |
| `GRAIN_K3_DEADZONE` | `0.06f` | K3 noon neutral pad |
| `GRAIN_MIN_LEN` | `48` | very-short grain floor (~1 ms; replaces effective 240 / hard 64) |
| `CLOUD_OVERLAP_MAX` | `8.f` | dense-end overlap for the Clouds side |

(Deadzone widths and floor are ear-tune targets, not final.)

---

## `ProcessGranular` front-end reinterpretation (NitroTron3.cpp:560+)

```
K2:  k2c = RemapKnob(k2) - 0.5
     direct_texture = |k2c| < GRAIN_K2_DEADZONE
     buf_reverse    = (k2c < 0)                       // CCW = backward, global
     k2mag = clamp((|k2c| - dz)/(0.5 - dz), 0, 1)      // drives max_range + k2_scale, as today

K3:  k3c = RemapKnob(k3) - 0.5
     cloud_mode = (k3c < -GRAIN_K3_DEADZONE)
     k3mag = clamp((|k3c| - dz)/(0.5 - dz), 0, 1)
```

- **k3 CW (character):** unchanged math with `k3mag` substituted for the old
  `k3` (grain shortening, overlap density, scatter, jitter, stutter loops).
  Reverse becomes probabilistic with bias flipped by `buf_reverse`: forward
  side `P(rev) = k3mag·0.6` (as today), backward side `P(fwd) = k3mag·0.6`.
- **k3 noon (neutral):** single coherent stream, fixed `grain_len =
  9600·k2_scale`, direction = `buf_reverse`, no scatter/jitter/loops.
- **k3 CCW (Clouds):** same fixed `grain_len = 9600·k2_scale`;
  `overlap = 0.5 + k3mag·(CLOUD_OVERLAP_MAX-0.5)`; `interval = grain_len/overlap`
  (sparse→dense); scatter = 0, jitter = 0, loops = 1, direction = `buf_reverse`.
- **Scheduler `Trigger` reverse arg** (NitroTron3.cpp:924) fed from the resolved
  direction/bias flag instead of the old random line at NitroTron3.cpp:888.

---

## Very short grains

- **NitroTron3.cpp:** curve floor 480 → `GRAIN_MIN_LEN`; hard floor 64 →
  `GRAIN_MIN_LEN` (NitroTron3.cpp:589–591).
- **grain_voice.h:42 window fix (load-bearing).** The current Tukey `alpha_`
  gives **long grains full Hann, short grains a near-flat 20% Tukey** — exactly
  backwards for short grains, which then click on their ~0.1 ms taper. Invert
  it so **short grains → full Hann** (smooth bell) and long grains taper toward
  flat-Tukey: `alpha ≈ 1.0` at ≤960 samples → `≈0.3` at 9600 samples. This is
  the change that makes clean sub-5-ms grains possible.

---

## Direct-texture bipolar micro-stutter (Idea A) + pitch control

The K2-noon zone keeps today's `StutterVoice` engine (NitroTron3.cpp:79) but
reworks the K3 mapping and adds pitch:

- **noon:** clean, no stutter (padded via `GRAIN_K3_DEADZONE`).
- **CW:** today's probabilistic micro-stutter, driven by `k3mag`.
- **CCW:** metronomic granulation — a regular timer (`interval = f(k3mag)`,
  sparse→dense) re-triggers fixed short slices through the ping-pong voice pair,
  direction = `buf_reverse`, no random chunk length / cut-out. Mirror of the
  buffer-engaged Clouds mode.

**Pitch-controlled playback (both stutter halves):**

- Upgrade `StutterVoice` from integer `offset` reads to **fractional /
  interpolated reads with a signed `rate`** (mirrors `GrainVoice::ReadFrac`).
  Reverse folds into the sign of `rate`, retiring the separate `reverse` bool.
- At slice **trigger**, set `rate`:
  - SW2 = UP/MID → `rate = GrainPitchRatio(harmony, k1)`, sampled **once** per
    slice so a MID resonance pick stays stable for the slice's duration;
  - SW2 = DOWN → `rate = 1.0` (the Bode SSB shifter already colors this path).
- Optional per-slice level comp `1/sqrt(rate)` (as grains do), tuned by ear.

This makes K1 pitch the micro-stutter identically to how it pitches grains, so
the whole mode reads as one pitch model.

---

## Firmware-translation caveats

- **Bipolar deadzones vs ADC jitter.** Both K2 and K3 noon zones must be wide
  enough (`GRAIN_K*_DEADZONE`) that jitter never flickers between
  passthrough/engaged or CW/CCW. Direction (`buf_reverse`) and mode
  (`cloud_mode`) are derived from the *sign* of a jittery value near center —
  the deadzone is what stops that from chattering.
- **Pitched stutter buffer wrap.** A slice pitched up an octave reads ~2× its
  length from the 200 ms capture buffer (`STUTTER_BUF_SIZE = 9600`) and wraps
  it. Very long CW chunks + high pitch-up therefore loop the buffer ~twice
  within one slice — acceptable as texture, noted so it isn't mistaken for a
  bug. Metronomic-CCW slices are short and unaffected.
- **Voice ceiling.** See the Clouds-support note above — dense long-grain
  clouds are capped by `NUM_GRAIN_VOICES = 8`.
- **No preset/storage impact.** Knobs are stored raw; all bipolar
  reinterpretation is front-end in `ProcessGranular`. No migration needed.

---

## Proposed staging (each build stays playable)

1. **Bipolar K2** — magnitude = length/timescale, sign = direction, noon =
   passthrough relocation; reverse-bias flip. K3 stays unipolar.
2. **Bipolar K3** (buffer engaged) — character CW / Clouds CCW / neutral noon.
3. **Very short grains** — curve floor + window redesign.
4. **Direct-texture bipolar** — metronomic CCW + pitch-controlled micro-stutter
   (StutterVoice fractional-rate upgrade).

---

## Open questions / ear-tune targets

- Deadzone widths (`GRAIN_K2_DEADZONE`, `GRAIN_K3_DEADZONE`).
- Very-short floor (`GRAIN_MIN_LEN`) and the exact short/long `alpha_` curve.
- Clouds density curve (`overlap = 0.5 + k3mag·…`) — linear vs shaped.
- Whether pitched stutter needs the `1/sqrt(rate)` level comp.
- Whether to raise `NUM_GRAIN_VOICES` for dense long-grain clouds.
</content>
</invoke>
