# Mode C — Schism — Discovery Notes

Status: historical discovery doc, kept for reference. The two- / three-stage architecture (SW1 drive → SW2 filter → mix) shipped largely as planned, but the SW2=DOWN filter is no longer Plague Bearer.

**SW2=DOWN history.** Plague Bearer (the dual-band resonant nonlinear filter described below) was modelled, tuned, and audited but never produced a sound worth keeping in the pedal. The slot now holds a 6-stage allpass phaser instead (per-stage detuned, soft-saturated feedback loop; see `docs/MODE_C_PHASER_PLAN.md`). The phaser itself is provisional — we're still hunting for a third filter / effect that earns the slot. The Plague Bearer material below is preserved as a record of the path and an option to revisit; the Plague handover notes for the Daisy port live in `docs/plague-bearer-daisy-handover.md`.

Other values, mappings, and some structural choices in this doc may have shifted by ear during implementation — `docs/MODE_C_DISCOVERY.md` is the original plan, not the as-built reference.

---

## Concept

Two-stage chain: **drive → filter**, then dry/wet mix. SW1 selects a drive flavor (character/harmonic content only — no gain change). SW2 selects a filter flavor — Moog ladder, Grendel formant, or Plague Bearer (a resonant nonlinear filter that's architecturally a filter, not a drive). The bass envelope follower (reused from Mode A) modulates the active filter's primary parameter via K3.

### Principles

- **Env follower tracks playing dynamics directly.** No attack/decay/sustain/release knobs. K3 sweeps the filter's primary parameter (cutoff / vowel index / hi-lo balance) following the bass envelope contour, smoothed only by the canonical 33 Hz 4-pole LP.
- **K4 is a character knob, not a gain knob.** Following Mode B's convention: dialing K4 up adds harmonic content (wavefolds harder, etc.) but doesn't raise output loudness. Internal compensation keeps the wet path roughly level across K4's range. There is no separate input-gain control.
- **Mix is a first-class control.** K6 is dry/wet, not output trim. Bass benefits from retaining low-end fundamental from the dry signal underneath any drive/filter coloration.
- **Filter row controls the filter, drive row controls the drive.** K1/K2/K3 reinterpret per SW2 position; K4/K5 reinterpret per SW1 position. K6 is global. No knob changes meaning across two switches.

---

## Signal Chain

```
Input ──┬─────────────────────────────────────────────────────────────────┐
        │                                                                 │
        └─► [SW1: drive] ──► [SW2: filter] ──► × K5 (wet level) ──────────┤
                 ▲                  ▲                                     │
                 │                  │                                     │
            [K4: character]  [K3: env amount]                             │
                                    │                                     │
                              [Env follower]                              │
                                                                          ▼
                                                                     [K6: mix] ──► Output
```

Env follower: full-wave rectifier → 4-pole 33 Hz LP. Single instance, shared with Mode A.

K5 (wet level) is a post-filter, pre-mix trim on the wet path. Always active, regardless of SW1/SW2 positions. Provisional in the sense that, if internal compensation across all SW1 × SW2 combinations proves tight enough, K5 may turn out redundant against K6 and get dropped during C.4/C.5 listening.

---

## Switches

| Switch | Function | Positions |
|---|---|---|
| **SW1** | Drive | UP: Sine wavefolder · MID: Gated bit crusher (C.6 explorative) · DOWN: Pitch-tracked synth oscillator (K4 = saw↔rect timbre morph) |
| **SW2** | Filter | UP: Moog ladder · MID: Grendel formant · DOWN: Plague |
| **SW3** | Mode select | UP: Mode A · MID: Mode B · DOWN: Mode C |

To audition the drive alone: SW2=UP with K1 (cutoff) fully open and K2 (resonance) at zero — functionally a filter bypass. (Note: the former SW1=DOWN passthrough is now a pitch-tracked synth oscillator, so "filter-only on dry bass" is no longer available; SW1=UP with K4 at zero is the closest equivalent.)

---

## Controls

Top row (K1–K3) = filter section, reinterpreting per SW2. K4 = drive character (reinterpreting per SW1). K5 = global wet level. K6 = global mix.

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Filter "where" | Ladder: cutoff Hz (exp). Grendel: vowel position along curated path (linear). Plague: hi/lo input balance (CCW: input → lo band only, noon: both bands equal, CW: input → hi band only) |
| KNOB 2 | Filter "how much" | Ladder: resonance. Grendel: Size (mouth scale). Plague: intensity (input gain + feedback drive in tandem — see Plague section) |
| KNOB 3 | Env → filter amount | Modulates K1 per the active SW2 mode. Polarity TBD per SW2 — see [Env follower behavior](#env-follower-behavior) |
| KNOB 4 | Drive character | Per SW1 stage. SW1=UP (sine fold): fold amount, internal loudness compensation. SW1=MID (bit crush): bit depth 16 → 4. SW1=DOWN (synth osc): timbre morph — CCW half = saw (max hypersaw → single saw, small single-saw sweet-spot plateau just below noon), CW half = rect (single rect → PWM with depth-then-rate ramp) |
| KNOB 5 | Wet level | Post-filter, pre-mix wet-path trim. Always active. May turn out redundant against K6 after listening — evaluated post-C.5 |
| KNOB 6 | Mix | Dry/wet. Fully CCW = dry, fully CW = wet only. Always active |
| SWITCH 1 | Drive | **UP** - Sine wavefolder<br/>**MIDDLE** - Gated bit crusher<br/>**DOWN** - Pitch-tracked synth oscillator (K4 morphs saw ↔ rect) |
| SWITCH 2 | Filter | **UP** - Moog ladder<br/>**MIDDLE** - Grendel formant<br/>**DOWN** - Plague |
| SWITCH 3 | Mode select | **UP** - Mode A<br/>**MIDDLE** - Mode B<br/>**DOWN** - Mode C |
| FOOTSWITCH 1 | Preset | Per `PROJECT.md` Preset System |
| FOOTSWITCH 2 | Bypass / Save | Per `PROJECT.md` Preset System |

K1, K2, K3 reinterpret per SW2 position; K4, K5 reinterpret per SW1 position. The user re-finds the sweet spot when toggling either switch.

---

## Drive stage (SW1)

### UP — Sine wavefolder

Symmetric harmonic saturation. Reuses the wavefolder used in Mode A (triangle mode) and Mode B (shaper bus). K4 controls fold amount: 0 = nearly transparent, 1 = maximum folding. The wavefolder's geometry keeps peaks bounded; perceived loudness stays roughly flat across K4 thanks to internal compensation (`SINEFOLD_COMP_AT_MAX`).

No gesture modulation — the env follower drives the filter, not the drive stage.

### MID — Gated bit crusher (explorative)

Linear-PCM quantizer with an input-envelope noise gate. K4 sweeps bit depth from `MODE_C_BITCRUSH_BITS_MAX` (16 = effectively transparent) down to `MODE_C_BITCRUSH_BITS_MIN` (4 = classic gnarl). The gate (`MODE_C_BITCRUSH_ENV_GATE`) keys the wet/dry mix off the shared envelope follower with a 1 ms click-free ramp so silent input → silent output and no zipper noise sits on the wet path when the bass is quiet.

First-pass distortion variant being evaluated alongside the sine wavefolder. May be replaced or refined during C.6 listening (candidates: SR reduction added on top, asymmetric/chebyshev wavefold, tape-style saturation, transistor fuzz).

### DOWN — Pitch-tracked synth oscillator

A pitch-tracked synth voice replaces the dry-bass passthrough. The bass note is tracked by the existing YIN tracker (semitone-quantized, recycled from Mode A direct mode), feeding an oscillator engine whose timbre is morphed by K4. The engine output is amplitude-controlled by the **raw** shared envelope follower (Mode A-style direct multiply, no smoother) and routed into the SW2 filter — so the VCA sits **before** the filter, and the filter's modulation curves stay separate.

K4 splits at noon — saw on the left, rect on the right, with a small single-saw sweet-spot plateau just below noon so the user can land on a clean unison-free saw:

- `K4 = 0.00`: max hypersaw — 7 unison voices, ±50-cent detune
- `K4 ∈ [0.00, ~0.28]`: all voices at full gain, detune widens to 50 cents
- `K4 ∈ [~0.28, 0.46]`: side voices fade in fast, detune ~10 cents (already incoherent so the RMS normalization tracks loudness across the ramp)
- `K4 ∈ [0.46, 0.50]`: pure single saw plateau (`MODE_C_SYNTH_SAW_PLATEAU` = 0.04)
- `K4 = 0.50`: discrete swap to rect
- `K4 ∈ [0.50, ~0.70]`: PWM depth ramps in fast (LFO held at 0.2 Hz)
- `K4 ∈ [~0.70, 1.00]`: depth at max (±0.3 duty deviation — sweet-spot detuned-rect feel; deeper drives the fundamental too far down at duty extremes via `sin(π·duty)`), LFO speeds up to 2 Hz

Voices are initialized with irrational-stagger phases (√2 − 1 fractional, per voice index) so the hypersaw is fully decorrelated from the first sample — no slow boot-time flange. Detune within the hypersaw zone is split from gain: gain ramps in across the first `HYPER_GAIN_FRAC` (0.4) of travel past the plateau so the supersaw is "fully on" early; remaining CCW travel only widens detune. PWM mirrors this — depth across the first `PWM_DEPTH_FRAC` (0.4) of CW travel, then rate.

The rect is phase-locked to the center saw (driven from `MoogOsc::GetPhase()`) and its polarity is inverted — a naive saw and naive rect are anti-phase at the fundamental, so without inversion they would partially cancel. The new layout doesn't mix saw and rect (discrete swap at noon) but the phase-lock + inversion stays in place at zero cost in case future revisions reintroduce a mix.

K5 (wet level) and K6 (mix) behave as before; K4 in SW1=DOWN is fully consumed by the timbre morph. The canonical "filter only on dry bass" trick (former SW1=DOWN passthrough) is no longer available — closest equivalent is SW1=UP with K4 at zero (transparent wavefolder).

#### Synth-osc constants

```cpp
constexpr int   MODE_C_SYNTH_UNISON_VOICES    = 7;
constexpr float MODE_C_SYNTH_DETUNE_CENTS_MIN = 10.f;
constexpr float MODE_C_SYNTH_DETUNE_CENTS_MAX = 50.f;
constexpr float MODE_C_SYNTH_HYPER_GAIN_FRAC  = 0.4f;
constexpr float MODE_C_SYNTH_SAW_PLATEAU      = 0.04f;
constexpr float MODE_C_SYNTH_PWM_LFO_HZ_MIN   = 0.2f;
constexpr float MODE_C_SYNTH_PWM_LFO_HZ_MAX   = 2.f;
constexpr float MODE_C_SYNTH_PWM_DEPTH_MAX    = 0.3f;
constexpr float MODE_C_SYNTH_PWM_DEPTH_FRAC   = 0.4f;
constexpr float MODE_C_SYNTH_VCA_GAIN         = 12.f;
```

Open: synth output octave (currently follows the tracked bass note 1:1, ear-tune in C.6 — may add a fixed octave offset or a sub-osc layer once auditioned).

---

## Filter stage (SW2)

### UP — Moog ladder

Reuses `MoogLadder` from Mode A. 24 dB/oct LP with per-stage `tanh` saturation. K1 = cutoff (exponential, 80 Hz – 8 kHz). K2 = resonance. K3 modulates cutoff via env follower.

### MID — Grendel formant

4-band parallel BPF formant filter, modeled after the Rare Waves Grendel Formant Filter.

A vowel is a 2D point on the IPA vowel chart, defined by four formant `{center frequency, amplitude}` pairs. K1 sweeps along a curated 1D path through the 2D vowel space — both X and Y vary together as K1 moves between vowel anchors.

K2 = Size (mouth scale, ×0.5 → ×1.6, moves all 4 BPF centers in parallel). K3 modulates K1's vowel index *and* K2's size in tandem via env follower.

Compile-time vowel formant table in `constants.h`. Vowel set: `oo → oh → ah → eh → ee` (CCW dark/closed → CW bright/open). K3 sign drives both axes coherently: **CCW K3** pushes path toward ee and tightens size (≈ +20% at full K3, "open vowel + tense mouth"); **CW K3** pushes toward oo and opens size (≈ −20%, "closed vowel + relaxed mouth"). Both modulations share a single 400 ms slow-swell env smoother (slow attack, instant snap-back) regardless of K3 sign — formant motion always breathes, never snaps. Snap-style env shapes are reserved for the Moog ladder.

### DOWN — Plague

A resonant nonlinear filter modeled on the Flight of Harmony Plague Bearer. Architecturally a filter (two resonant peaks with internal saturation) rather than a drive — placed on SW2 alongside the other filters. Behaves best on clean signals; on heavily-folded input from SW1=UP the harmonic-generation behavior is muted because the saturation has no headroom to produce new content. Canonical setup is SW1=DOWN passthrough into Plague.

#### Implementation core

`tanh` saturation lives **inside** the SVF integrator state updates, not in front of the filter. Per-integrator saturation produces wavefolder-like harmonic generation because the saturation interacts with the resonance feedback loop. Same principle as Mode A's Huovilainen ladder.

Implementation form: two parallel Cytomic / Andrew Simper saturating SVFs at fixed center frequencies — one band lo, one band hi. The "level" controls in the FoH circuit aren't post-band amplitude attenuators; turning them up audibly changes folding *character*, which only makes sense if those controls sit *inside* each band's saturating feedback loop. We model them as per-band feedback drive accordingly.

#### Plague topology

```
in ─► × K2_input ─┬─► × balance_lo(K1, K3·env) ─► [SatSVF @ PLAGUE_LOW_HZ,  fb gain = K2_fb] ─┐
                  │                                                                          ├─► [tanh] ─► out
                  └─► × balance_hi(K1, K3·env) ─► [SatSVF @ PLAGUE_HIGH_HZ, fb gain = K2_fb] ─┘

  K2_input    = K2 · PLAGUE_INPUT_RATIO                              # tandem pre-saturation drive
  K2_fb       = PLAGUE_FB_BASE + K2 · PLAGUE_FB_RANGE                # tandem feedback drive (stays below self-osc)
  balance_lo  = clamp(0.5 − (K1 + K3 · env_bipolar))                 # constant-sum input split
  balance_hi  = clamp(0.5 + (K1 + K3 · env_bipolar))                 # K1=noon → 0.5/0.5
```

#### Plague controls

Three independent axes from the FoH circuit (input gain, lo feedback, hi feedback) projected onto two filter-row knobs + env, with input gain and feedback drive tied together to keep the "intensity" relationship monotonic and obvious:

- **K1 = hi/lo input balance** (bipolar, constant-sum). Splits the post-K2 input between the two bands.
  - CCW: input → lo band only, hi band silent (no input, no resonance)
  - Noon: both bands receive equal input → both fold together
  - CW: input → hi band only, lo band silent
- **K2 = intensity** (tandem). Single knob that simultaneously cranks pre-saturation input gain (`K2_input`) AND per-band feedback drive (`K2_fb`). Cranking K2 makes everything fold harder regardless of K1. Output level is compensated post-stage so K2 reads as a character knob, not a loudness knob.
  - K2 low + K1=noon → both bands lightly fed, low feedback → mellow, both audible
  - K2 high + K1=noon → both bands hot input + max feedback → maximum Plague gnarl
  - K2 high + K1=CCW → all input to lo, both feedback paths hot but only lo has signal → focused low-band fold
- **K3 = env amount** (bipolar, with center deadzone). Modulates K1 (input balance). CCW of noon: env shifts balance toward lo on attack (quiet → hi voiced, loud → lo voiced). CW of noon: inverse. Playing dynamics become a *voicing morph* between the two bands' folding characters, not just an amplitude shift.

The tandem K2 mapping loses one degree of freedom vs the original (you can't independently set "high input gain + low feedback" or vice versa), but those nuance combinations don't justify a fourth knob on this pedal. The asymmetric per-band feedback combinations (e.g., lo feedback hot, hi feedback cold while both receive input) are also collapsed — K1 controls input split, not feedback split.

#### Plague safeguards

1. Per-integrator `tanh` saturation limits resonance buildup inside each band.
2. Output `tanh()` bounds peaks to ±1 after the band sum.
3. `K2_fb` clamped below self-oscillation threshold via `PLAGUE_FB_BASE` and `PLAGUE_FB_RANGE`.
4. `K2_input` clamped to a sane peak — driving the input gain too hard before any saturation just wastes headroom.

#### Internal level compensation

K2 sweeps shift perceived loudness substantially because both gain stages compound. K1 should be roughly loudness-neutral by design (constant-sum input balance), but verify by listening.

Approach: open-loop precomputed compensation. Plague applies a post-stage gain that is a function of K2 — a 1D curve, ear-tuned during C.4. If K1 shows residual loudness drift across its range, add a 2D term over (K1, K2) or a small balance-axis correction.

Compensation curves are ear-tuned during stage tuning. Open-loop chosen over RMS-based AGC for determinism, zero latency, and absence of pumping artifacts.

#### Plague constants (initial values, ear-tune in C.4)

```cpp
// Corner frequencies — fixed (FoH-style, set by circuit topology)
constexpr float PLAGUE_LOW_HZ         = 220.0f;   // ear-tune
constexpr float PLAGUE_HIGH_HZ        = 1800.0f;  // ear-tune

// Tandem K2 mapping — input gain ramps linearly, feedback ramps from a safe base
constexpr float PLAGUE_INPUT_RATIO    = 1.0f;     // input-gain scaling at K2=1
constexpr float PLAGUE_FB_BASE        = 0.5f;     // feedback gain at K2=0
constexpr float PLAGUE_FB_RANGE       = 0.4f;     // feedback gain added at K2=1; total must stay below self-osc

// Sine wavefolder compensation at full fold
constexpr float SINEFOLD_COMP_AT_MAX  = 0.7f;

// Plague compensation curve coefficients — form decided during C.4
// constexpr float PLAGUE_LOUDNESS_COMP_*  = ...;

// Env K3 deadzone (bipolar mode)
constexpr float K3_DEADZONE           = 0.05f;    // noon ±5%, ear-tune
```

---

## Env follower behavior

Single instance, shared with Mode A. K3 sets the modulation amount applied to K1, where K1 is whichever parameter the active SW2 filter exposes (Moog cutoff / Grendel vowel index / Plague input balance). The drive stage receives no env routing.

**Polarity per SW2 mode:**

- **SW2=DOWN (Plague): bipolar with center deadzone** — natural fit. The two voicings (lo-band vs hi-band) live on either side of noon, and bipolar K3 morphs which band the input is routed to as you play, sweeping folding character with dynamics.
- **SW2=UP (Moog): bipolar with center deadzone, K3 sign switches env *shape* not direction** — both CW and CCW open cutoff upward from K1 as env grows (always brightens on attack). CW uses a peak follower (instant attack, slow release → snappy auto-wah); CCW uses a slow-rise smoother (slow attack, instant snap-back → swell). Shape only, direction shared.
- **SW2=MID (Grendel): bipolar with center deadzone, K3 sign switches env *direction*** — K3 sign drives both vowel path and size in tandem. CCW K3 opens path toward ee and tightens size (mouth closes/tense, classic "auto-wah opens up brighter"). CW K3 closes path toward oo and opens size (mouth widens/yawn, "anti-wah / ducking filter"). Both directions use the same slow-swell env smoother (400 ms attack, instant snap-back); the snap shape stays Moog-only.

---

## File scaffold

```
src/
├── plague.h          # Twin-T-style nonlinear BP wavefolder
├── grendel.h         # 4-BPF parallel formant filter + vowel-path interpolator
├── svf_nonlinear.h   # Saturating SVF (per-integrator tanh) — used by plague.h
├── bitcrush.h        # SW1=MID gated bit crusher
├── synth_osc_c.h     # SW1=DOWN pitch-tracked synth osc (hypersaw + PWM rect)
└── constants.h       # PLAGUE_*, GRENDEL_VOWEL_TABLE, MODE_C_*  (additions)
```

Mode C dispatch reuses the existing `ProcessFreqShift()` function in `NitroTron3.cpp`. Reused components: `EnvFollower`, `MoogLadder`, sine wavefolder, `PitchTracker` (YIN, shared with Mode A direct mode), `MoogOsc` (saw voices in the synth engine).

### Preset data

Mode C uses the existing `ModePresetData` shape (6 knobs + 2 switches per mode, per `PRESET_IMPL.md`). Mode-specific interpretation:

```
knobs[0] = filter "where"  (cutoff / vowel / input balance)
knobs[1] = filter "how much" (resonance / size / intensity)
knobs[2] = env amount
knobs[3] = drive character
knobs[4] = wet level
knobs[5] = mix (dry/wet)
sw1 = drive flavor   (0=sinefold, 1=bitcrush, 2=synth-osc)
sw2 = filter flavor  (0=ladder,   1=grendel, 2=plague)
```

---

## Implementation order

Each stage produces a flashable, testable build.

### C.1 — Scaffold + mix
Replace `ProcessFreqShift()` body with the bare wet/dry split, K5 wet-level trim, and K6 mix. SW1=DOWN, SW2=UP with K1 fully open and K2 at zero — wet path is just a unity copy of dry. Verify mix law (equal-power or linear, TBD) sounds correct at noon. K4 unused here.

### C.2 — Sine wavefolder port
Wire wavefolder under SW1=UP. K4 controls fold amount. Filter chain still effectively pass-through (Moog open). Ear-tune `SINEFOLD_COMP_AT_MAX` so wet loudness stays roughly stable across K4's range, judged against the dry signal via K6. K5 just trims wet level globally.

### C.3 — Moog ladder + env
SW2=UP fully active. K1 cutoff, K2 resonance, K3 env-to-cutoff. Reuse `MoogLadder` from Mode A. First build uses **bipolar K3 with center deadzone**; record a listening note on whether swells are actually used or whether unipolar CW-only would be preferable. Re-check `SINEFOLD_COMP_AT_MAX` if filter coloring shifts the perceived loudness curve.

### C.4 — Plague as third filter
Implement `SvfNonlinear` and `Plague` per the corrected topology (per-band saturating SVFs with feedback drive inside each band's loop, constant-sum input balance ahead of the bands). SW2=DOWN active. K1 = input balance, K2 = intensity (tandem input gain + feedback drive), K3 = bipolar env on K1.

Ear-tune (in roughly this order):
- `PLAGUE_LOW_HZ`, `PLAGUE_HIGH_HZ` — fixed corner positions. Pick by listening across bass-friendly registers (rough starting guess: 220 Hz / 1.8 kHz).
- `PLAGUE_FB_BASE`, `PLAGUE_FB_RANGE` — feedback gain at K2=0 and additional range at K2=1. Total must stay below self-oscillation at K2=max for both bands; verify by holding K2=1 with no input and confirming no runaway.
- `PLAGUE_INPUT_RATIO` — input-gain scaling. Adjust the tandem balance: more pre-saturation drive vs more feedback drive, by ear, until cranking K2 produces the expected "intensifies" character.
- Plague loudness compensation curve — sweep K2 across its range with K1 at noon, K3 at noon; adjust the post-stage compensation gain until perceived wet loudness is roughly flat. Then check at K1=CCW and K1=CW; if either shows residual loudness drift, add a 2D term over (K1, K2) or a balance-axis correction.
- `K3_DEADZONE` width.

Decision point at end of C.4: K5 evaluation deferred to C.6 (need Grendel in place first to judge K5 across all SW2 positions).

### C.5 — Grendel
Implement `Grendel`. Curated vowel formant table in `constants.h`. SW2=MIDDLE active. K1 = vowel path, K2 = Size. Audition all SW1 × SW2 combinations; adjust vowel set/order if needed.

### C.6 — K3 polarity, K5 disposition, SW1=MID and SW1=DOWN slots
K3 polarity DONE: bipolar for all three SW2 modes. Moog uses K3 sign to swap env *shape* (snap vs swell, direction always opens upward). Grendel uses K3 sign to swap env *direction* (CCW = open path + tighten size; CW = close path + open size), always slow-swell. Plague is bipolar by design (balance morph). SW1=MID DONE: gated bit crusher (explorative, kept). SW1=DOWN DONE: pitch-tracked synth oscillator (YIN-tracked, K4 = saw↔rect timbre morph, raw-env VCA pre-filter — see § Drive stage / DOWN). Open: K5 disposition (always-on wet-level trim vs redundant against K6 mix) — decide post-listening; synth-osc octave (currently 1:1 with tracked bass, ear-tune for fixed offset or sub-osc layer).

### C.7 — Documentation
Replace this discovery doc with `docs/MODE_C.md` following the `MODE_A_DRONE.md` structure. Update README controls table.

---

## Open questions

1. ~~**K3 polarity for SW2=UP and SW2=MID**~~ — RESOLVED. Both bipolar with center deadzone. Moog: sign swaps env shape (snap CW / swell CCW), direction always opens upward. Grendel: sign swaps env direction (CCW opens path + tightens size, CW closes path + opens size), always slow-swell.
2. **K5 disposition** — does the always-on wet-level trim earn its place across all SW1 × SW2 combinations, or is it redundant against K6 mix? Decided in C.6 once Grendel is in place.
3. **K6 mix law** — equal-power (constant perceived loudness at noon) vs linear vs sqrt. Settled in C.1.
4. **SW1=MID drive flavor** — second wavefolder variant, tape saturation, fuzz, or other. Decided in C.6.
5. **Plague corner frequencies** (`PLAGUE_LOW_HZ`, `PLAGUE_HIGH_HZ`) — fixed positions, ear-tuned in C.4.
6. **Plague tandem K2 mapping** — `PLAGUE_INPUT_RATIO`, `PLAGUE_FB_BASE`, `PLAGUE_FB_RANGE`. Sets the ratio of pre-saturation drive vs feedback drive at K2's extremes. Total feedback must stay below self-oscillation. Ear-tuned in C.4.
7. **Grendel vowel set, order, and path-shape** (linear vs curved through 2D space).
8. **Plague loudness compensation curve representation** (LUT, polynomial, simple sqrt) — 1D over K2 unless K1 shows residual drift, then 2D over (K1, K2).
9. **Whether K1 input balance needs its own loudness compensation** in addition to K2 — decided by listening in C.4.
10. **`K3_DEADZONE` width** for bipolar mode.

---

## Reuse from existing modes

- `EnvFollower`, `MoogLadder` — Mode A.
- Sine wavefolder — Mode A (triangle mode), Mode B (shaper bus).
- `PitchTracker` (YIN, semitone-quantized) — Mode A direct mode, Mode B harmony tracking. Powers the SW1=DOWN synth osc.
- `MoogOsc` — Mode A oscillator class, reused as the unison saw voices in `synth_osc_c.h` (extended with `SetPhase()` / `GetPhase()` for boot decorrelation and saw↔rect phase-lock).
- `ModePresetData` storage pattern — `PRESET_IMPL.md`.
