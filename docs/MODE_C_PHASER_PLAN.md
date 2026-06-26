# Mode C SW2=DOWN — Phaser Plan

Status: **as-built diverged from this plan.** The slot shipped, replacing Plague Bearer, but the implementation is NOT the 3-band parallel resonant BPF described below. It's a **6-stage first-order allpass phaser** (Small Stone topology, denser than a stock 4-stage), implemented in `src/phaser.h`. The BPF plan below is kept for historical context only — the `PHASER_BPF_RATIO` / `PHASER_Q_*` constants it lists were never created.

As-built voicing (ear-tuned):
- **6 allpass stages** → 3 notches (stock Small Stone is 4 stages / 2 notches; we run denser/thicker).
- **Per-stage coefficient detune** (`PHASER_STAGE_SPREAD`): stages sit at slightly different corners so notches/resonance spread organically instead of stacking into one razor peak — removes the "digital" perfect-alignment edge.
- **Soft-saturated feedback** (`tanh` inside the loop, analog-OTA style): at low feedback it's ~linear; as the loop rings up it blooms and self-limits instead of ringing as a sterile sine. Also bounds runaway.
- **Feedback ceiling** `PHASER_FB_MAX = 0.98`: wide open — K2 reaches into bounded self-oscillation at full CW; the `tanh` is the safety, so no conservative pre-limit is needed.
- K1 = notch centre (exp), K2 = feedback, K3 = bipolar LFO rate + shape (CCW triangle / CW sample-and-hold, centre = static). Internal dry+wet sum locked at 0.5/0.5. No env-follower routing.

Live `PHASER_*` constants are in `src/constants.h`; this doc is not the source of truth for values.

---

Original planning notes (historical — describes the abandoned BPF approach):

---

## Decision

Drop the Plague Bearer. The sine wavefolder (SW1=UP) already covers the saturated/folded character on bass; another saturated-filter slot doesn't earn its place.

Replace SW2=DOWN with a **3-band parallel resonant BPF**, swept by an internal LFO. Single structure delivers both target behaviors:

- **LFO off** → static resonant formant filter (range-dependent: reads as bandpass at lower K1, highpass-ish at upper K1)
- **LFO on** → phaser (peaks and the notches between them slide together; ratios fixed so the formant character stays coherent during sweeps)

Baseline target: approximate an **EHX Small Stone** character at sensible default knob positions. Not bit-exact emulation — the parallel-BPF topology has its own voice; "Small Stone-ish" is the range we're aiming for, not a faithful clone.

No env-follower modulation on this mode (drops the Mode C K3 = env convention here — phaser drives itself from its internal LFO).

---

## Control layout

| Knob | Function | Notes |
|---|---|---|
| **K1** | Base center frequency `f1` (exponential, ~80 Hz – 4 kHz). LFO sweeps around this center; sweep range fixed in constants. `f2 = f1 × r`, `f3 = f1 × r²` | Whole stack scales together |
| **K2** | Resonance / Q (shared across all three BPFs) | Low Q = mellow sweep; high Q = vocal, whistley — covers Small Stone "Color off" → "Color on" range |
| **K3** | Bipolar LFO speed + shape. Noon (with `K3_DEADZONE`) = LFO stopped → static filter. CCW side = triangle, speed ramps 0 → fast with distance from noon. CW side = sample-and-hold, speed ramps 0 → fast with distance from noon | Single knob covers static → slow → fast for both shapes. CCW/CW side assignment: see Open fork 1 |
| **K4** | Drive character (per SW1, unchanged) | |
| **K5** | Mode-specific reassignment — see Open fork 2 | Current global "filter drive" meaning is dropped for SW2=DOWN |
| **K6** | Mix (global, unchanged) | Dry/wet handled here; no per-mode mix needed |

K3 = noon naturally delivers the static bandpass/highpass case. No separate mode switch.

---

## Pre-tuned constants

Ear-tuned during implementation. Initial guesses:

```cpp
// Phaser (SW2=DOWN). Sweep range and LFO rates fixed; tuned for
// Small Stone-ish baseline at sensible default knob positions.
constexpr float PHASER_F1_HZ_MIN     = 80.f;    // K1 fully CCW
constexpr float PHASER_F1_HZ_MAX     = 4000.f;  // K1 fully CW
constexpr float PHASER_BPF_RATIO     = 2.0f;    // octave spacing between bands — ear-tune (1.5 = fifth, 1.26 = m3)
constexpr float PHASER_SWEEP_RATIO   = 0.5f;    // LFO depth: ±0.5 octaves around f1 — ear-tune
constexpr float PHASER_LFO_HZ_MIN    = 0.1f;    // rate just past K3 deadzone (slow but moving)
constexpr float PHASER_LFO_HZ_MAX    = 5.f;     // rate at K3 fully off-noon — Small Stone tops out ~5 Hz
constexpr float PHASER_Q_MIN         = 1.f;     // K2=0 — gentle smear
constexpr float PHASER_Q_MAX         = 12.f;    // K2=1 — peaky, vocal — ear-tune to avoid runaway
```

`K3_DEADZONE` reused from existing Mode C constants (±5%).

`PHASER_BPF_RATIO` decision drives the formant character — octave reads classic-formant, fifth reads more vocal, minor third tighter/whistlier. Pick by ear during implementation.

---

## Implementation notes

- **Topology**: 3 parallel state-variable filters (DaisySP `Svf` or our own simple SVF in BP mode), centers locked at ratio. Outputs summed. No nonlinearity in the loop — this is a clean filter, character comes from the resonant peaks and the LFO motion, not from drive.
- **LFO**: single shared LFO instance feeding `f1`; `f2`/`f3` scale from `f1` each block. Phase reset on shape change (CCW ↔ CW boundary crossing) is optional — probably not worth it, let it drift.
- **Sample-and-hold**: simple — pick a new random value in `[-1, +1]` at rate steps, hold between. Smooth slightly (one-pole, ~5 ms) to avoid clicks on the step transitions.
- **Triangle LFO**: standard, ±1 range, fed through `expf(depth × tri)` to get exponential frequency sweep (musically symmetric in octaves).
- **No env follower in this mode.** Don't touch `env_c_filter`.
- **K3 sign read**: per existing Mode C pattern. Sign selects shape, magnitude (after deadzone) selects rate.

---

## Cleanup (Plague removal)

Done as part of the phaser's first build, not a separate commit:

- Delete `src/plague.h`, `src/svf_nonlinear.h`.
- Remove `Plague plague_c;` instance and `plague_c.Init(sr)` from `NitroTron3.cpp`.
- Remove the `plague_on` branch in `ProcessFreqShift()`; replace with the phaser branch.
- Strip `PLAGUE_*` constants from `constants.h`.
- Update `docs/MODE_C_DISCOVERY.md` § Filter stage / DOWN to describe the phaser. Update Reuse list. Drop the "filter-only on dry bass — closest equivalent" note about Plague.
- Update README controls table (Plague row → phaser row).
- Delete `docs/plague-bearer-daisy-handover.md` (now stale).
- Phaser source file: `src/phaser.h`.

Preset-data shape is unchanged (`ModePresetData`: 6 knobs + 2 switches). Existing presets that saved Plague values will be reinterpreted as phaser values — acceptable; presets need a re-listen pass anyway.

---

## Open forks (resolve before implementation)

### Fork 1 — K3 side assignment

Which LFO shape lives on which side of noon?

- **Option A**: Triangle CCW, S&H CW. Reads "familiar/safe on the left, experimental on the right" — matches most pedal-layout conventions.
- **Option B**: Reverse. No strong reason; defensible if user prefers S&H on the natural-feeling CCW side.

### Fork 2 — K5 behavior

What does K5 do in SW2=DOWN?

- **Option a**: Plain wet trim (0 → unity → slight boost). Simplest.
- **Option b** *(recommended for cross-mode muscle memory)*: Bipolar drive (CCW attenuate, noon unity, CW boost), matching the current Mode C convention for K5 in the other SW2 positions.
- **Option c**: Wet trim with built-in loudness compensation for K2 (high Q shifts perceived loudness). Most engineering; least transparent to the user.

LFO depth does cause perceived amplitude swing as peaks sweep across input energy. Plain wet trim (a or b) handles this fine; we don't need to couple K5 to depth explicitly.

---

## Reuse from existing code

- DaisySP `Svf` (BP mode) or a simple SVF — TBD during implementation.
- `K3_DEADZONE` from `constants.h`.
- Mix law (sqrt equal-power) and K6 handling in `ProcessFreqShift()`, unchanged.
- `MoogLadder`, `Grendel`, sine wavefolder, bitcrush, synth osc — all untouched (other SW1/SW2 slots).
