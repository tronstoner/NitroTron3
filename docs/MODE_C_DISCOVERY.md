# Mode C — Schism — Discovery Notes

Status: discovery, not spec. Values, mappings, and some structural choices are open and will be settled by ear during implementation. Once playable, this is replaced by `MODE_C.md` following the structure of `MODE_A_DRONE.md`.

---

## Concept

Two-stage chain: **distortion → filter**. Sub-mode switches select the flavor of each stage. The bass envelope follower (reused from Mode A) modulates the filter via K4.

### Principles

- **Env follower tracks playing dynamics directly.** No attack/decay/sustain/release knobs. The K4 filter sweep responds to the bass envelope contour, smoothed only by the canonical 33 Hz 4-pole LP.
- **Output level is internally compensated.** K1 changes distortion *character*; loudness stays roughly stable across K1's range. K6 is fine-tune output trim, not loudness compensation.

---

## Signal Chain

```
Input ──► [K1: input gain] ──► [SW1: distortion] ──► [SW2: filter] ──► [K6: output level] ──► Output
                                       ▲                    ▲
                                       │                    │
                                       │            [K4: env amount]
                                       │                    │
                                  [Env follower] ───────────┘
```

Env follower: full-wave rectifier → 4-pole 33 Hz LP. Single instance, shared with Mode A.

No dry/wet mix. K6 is pure wet output level.

---

## Switches

| Switch | Function | Positions |
|---|---|---|
| **SW1** | Distortion | UP: Sine wavefolder · MID: Plague · DOWN: passthrough (reserved) |
| **SW2** | Filter | UP: Moog ladder · MID: Grendel formant · DOWN: bypass |
| **SW3** | Mode select | UP: Mode A · MID: Mode B · DOWN: Mode C |

SW1=DOWN and SW2=DOWN allow each stage to be auditioned in isolation.

---

## Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Input gain | Drives the distortion stage. Output level internally compensated across K1's range |
| KNOB 2 | Distortion character | Sine fold: fold amount. Plague: corner blend. SW1=DOWN: unused |
| KNOB 3 | Filter cutoff / vowel | Static center point that env modulates around. Ladder: cutoff Hz (exp). Grendel: vowel position (linear). SW2=DOWN: unused |
| KNOB 4 | Env → filter amount | Centered with deadzone. CCW: env subtracts from K3. Noon: static. CW: env adds to K3 |
| KNOB 5 | Resonance / Size | Ladder: resonance. Grendel: Size. SW2=DOWN: unused |
| KNOB 6 | Output level | Fine-tune wet output trim |
| SWITCH 1 | Distortion | **UP** - Sine wavefolder<br/>**MIDDLE** - Plague<br/>**DOWN** - Passthrough |
| SWITCH 2 | Filter | **UP** - Moog ladder<br/>**MIDDLE** - Grendel formant<br/>**DOWN** - Filter bypass |
| SWITCH 3 | Mode select | **UP** - Mode A<br/>**MIDDLE** - Mode B<br/>**DOWN** - Mode C |
| FOOTSWITCH 1 | Preset | Per `PROJECT.md` Preset System |
| FOOTSWITCH 2 | Bypass / Save | Per `PROJECT.md` Preset System |

K3 and K5 reinterpret per SW2 position. The user re-finds the sweet spot when toggling SW2.

---

## Distortion stage (SW1)

### UP — Sine wavefolder

Symmetric harmonic saturation. Reuses the wavefolder used in Mode A (triangle mode) and Mode B (shaper bus). K2 controls fold amount. No gesture modulation — the env follower drives the filter, not the distortion.

### MID — Plague

A resonant nonlinear bandpass that produces wavefolder-like harmonic generation. Modeled on the Flight of Harmony Plague Bearer. Behaves best on clean signals; on already-distorted input the harmonic-generation behavior is muted because the saturation has no headroom to produce new content.

#### Implementation core

`tanh` saturation lives **inside** the SVF integrator state updates, not in front of the filter. Pre-filter saturation produces a fuzz-into-filter character. Per-integrator saturation produces wavefolder-like behavior because the saturation interacts with the resonance feedback loop. Same principle as Mode A's Huovilainen ladder.

Implementation form: Cytomic / Andrew Simper saturating SVF.

#### Plague controls

- **K1** drives audio into the saturation stage. Higher K1 → harder folding / clangier character. Output level compensated post-stage so K1 reads as a character knob.
- **K2** sweeps the corner blend:
  - K2 = 0: low corner emphasized → dark, dippy
  - K2 = 0.5: balanced — both corners audible
  - K2 = 1: high corner emphasized → bright, ringing

#### Corner sweep

Initial implementation: parallel sweep — both corners traverse their ranges in parallel as K2 moves. Straightforward to implement, but the corners do not overlap at K2=0.5 (they sit at the midpoints of their respective ranges).

If a formant-overlap zone at K2=0.5 is musically required after C.4 listening, upgrade to a piecewise scheme where both corners converge on a shared overlap frequency at K2=0.5 and diverge at the extremes — adds a "comb / vocal" zone at the midpoint.

#### Plague safeguards

1. Per-integrator `tanh` saturation limits resonance buildup.
2. Output `tanh()` bounds peaks to ±1.
3. Internal Q fixed below self-oscillation threshold. K1 cannot push past it.

#### Internal level compensation

The peak safeguards do not stabilize *average* level. K1 sweeps and K2 sweeps both shift perceived loudness substantially, and the formant-overlap zone (when implemented) is louder than the parallel-sweep extremes.

Approach: open-loop precomputed compensation. Each distortion stage applies a post-saturation gain that is a function of its character knob(s):

- Sine wavefolder: 1D curve over K2.
- Plague: 2D curve over (K1, K2). Form (LUT, polynomial, or factored) decided during C.4.

Compensation curves are ear-tuned during stage tuning. Open-loop chosen over RMS-based AGC for determinism, zero latency, and absence of pumping artifacts.

SW1=DOWN passthrough has no compensation — K1 acts as plain gain.

#### Plague constants (initial values, ear-tune in C.4)

```cpp
// Corner sweep ranges (parallel sweep — initial implementation)
constexpr float PLAGUE_LOW_HZ_MIN     = 60.0f;
constexpr float PLAGUE_LOW_HZ_MAX     = 800.0f;
constexpr float PLAGUE_HIGH_HZ_MIN    = 800.0f;
constexpr float PLAGUE_HIGH_HZ_MAX    = 4000.0f;

// Internal Q — below self-osc
constexpr float PLAGUE_Q              = 12.0f;

// Sine wavefolder compensation at full fold
constexpr float SINEFOLD_COMP_AT_MAX  = 0.7f;

// Plague compensation curve coefficients — form decided during C.4
// constexpr float PLAGUE_COMP_*  = ...;
```

### DOWN — Passthrough

Audio passes unchanged. K2 unused.

---

## Filter stage (SW2)

### UP — Moog ladder

Reuses `MoogLadder` from Mode A. 24 dB/oct LP with per-stage `tanh` saturation. K3 = cutoff (exponential, 80 Hz – 8 kHz). K5 = resonance. K4 modulates cutoff.

### MID — Grendel formant

4-band parallel BPF formant filter, modeled after the Rare Waves Grendel Formant Filter.

A vowel is a 2D point on the IPA vowel chart, defined by four formant `{center frequency, amplitude}` pairs. K3 sweeps along a curated 1D path through the 2D vowel space — both X and Y vary together as K3 moves between vowel anchors.

K5 = Size (mouth scale, moves all 4 BPF centers in parallel). K4 modulates K3's vowel index.

Compile-time vowel formant table in `constants.h`. Initial vowel set: `ee → eh → ah → oh → oo`. Set, order, and path shape (linear interpolation between anchors vs curved through vowel space) settled by ear in C.5.

### DOWN — Filter bypass

Stage skipped. K3, K4, K5 inactive.

---

## Env follower behavior

Single instance, shared with Mode A. Drives K4 only. The distortion stage receives no env routing.

K4 centered with deadzone:
- CCW: env subtracts from K3 (ladder closes / vowel index moves down).
- Noon ±deadzone: filter static, env scaling bypassed.
- CW: env adds to K3 (auto-wah / vowel-up sweep).

---

## File scaffold

```
src/
├── plague.h          # Twin-T-style nonlinear BP wavefolder
├── grendel.h         # 4-BPF parallel formant filter + vowel-path interpolator
├── svf_nonlinear.h   # Saturating SVF (per-integrator tanh) — used by plague.h
└── constants.h       # PLAGUE_*, GRENDEL_VOWEL_TABLE, MODE_C_*  (additions)
```

Mode C dispatch reuses the existing `ProcessFreqShift()` function in `NitroTron3.cpp`. Reused components: `EnvFollower`, `MoogLadder`, sine wavefolder.

### Preset data

Mode C uses the existing `ModePresetData` shape (6 knobs + 2 switches per mode, per `PRESET_IMPL.md`). Mode-specific interpretation:

```
knobs[0] = input gain         knobs[3] = env amount (centered)
knobs[1] = distortion char.   knobs[4] = resonance / size
knobs[2] = cutoff / vowel     knobs[5] = output level
sw1 = distortion flavor (0=sinefold, 1=plague, 2=passthru)
sw2 = filter flavor     (0=ladder,   1=grendel, 2=bypass)
```

---

## Implementation order

Each stage produces a flashable, testable build.

### C.1 — Scaffold + gain staging
Replace `ProcessFreqShift()` body with K1/K6 wiring. SW1=DOWN, SW2=DOWN only — no processing in between.

### C.2 — Sine wavefolder port
Wire wavefolder under SW1=UP. K2 controls fold amount. Filter still bypassed. Ear-tune `SINEFOLD_COMP_AT_MAX` so output loudness stays roughly stable across K2's range.

### C.3 — Moog ladder + env
SW2=UP active. K3 cutoff, K5 resonance, K4 env-to-cutoff with center deadzone. Reuse `MoogLadder` from Mode A. Re-check `SINEFOLD_COMP_AT_MAX` if filter coloring shifts the perceived loudness curve.

### C.4 — Plague stage
Implement `SvfNonlinear` and `Plague`. SW1=MIDDLE active. K2 = corner blend (parallel sweep). Ear-tune:
- `PLAGUE_Q` — must stay below self-oscillation.
- `PLAGUE_LOW_HZ_*`, `PLAGUE_HIGH_HZ_*` — corner sweep ranges.
- Plague compensation curve — sweep K1 at fixed K2 values (0.0, 0.25, 0.5, 0.75, 1.0); adjust until perceived loudness is roughly flat. Repeat for K2 sweeps at fixed K1.

If the formant midpoint is musically missed, upgrade the corner sweep to converging-corners.

### C.5 — Grendel
Implement `Grendel`. Curated vowel formant table in `constants.h`. SW2=MIDDLE active. K3 = vowel path, K5 = Size. Audition all four (distortion × filter) combinations; adjust vowel set/order if needed.

### C.6 — SW1=DOWN slot
Decide and implement.

### C.7 — Documentation
Replace this discovery doc with `docs/MODE_C.md` following the `MODE_A_DRONE.md` structure. Update README controls table.

---

## Open questions

1. `PLAGUE_Q` value.
2. Plague corner sweep ranges.
3. Whether converging-corners overlap is needed (decided during C.4 listening).
4. Grendel vowel set, order, and path-shape (linear vs curved through 2D space).
5. K2 taper for Plague corner blend (linear vs log vs midpoint-weighted).
6. Plague compensation curve representation (LUT, polynomial, or factored `f(k1) * g(k2)`).
7. Whether to leave a small loudness rise in Plague's character zones or compensate fully flat.
8. SW1=DOWN final purpose.

---

## Reuse from existing modes

- `EnvFollower`, `MoogLadder` — Mode A.
- Sine wavefolder — Mode A (triangle mode), Mode B (shaper bus).
- `ModePresetData` storage pattern — `PRESET_IMPL.md`.
