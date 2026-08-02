# Degradation Colour — Bipolar BBD / Tape (implementation spec)

Target platform: Daisy Seed (Cortex-M7, hard FPU, float32), 48 kHz, block-based.

Purpose: audible, musically sophisticated colouring. Not a mastering-grade emulation. Any term whose perceptual contribution does not justify its cycle cost is explicitly excluded (see *Excluded* per chain).

---

## 1. Control mapping

Single bipolar control, position `p ∈ [-1, +1]`.

| Range | Chain | Depth |
|---|---|---|
| `p < -0.03` | BBD | `d = (|p| - 0.03) / 0.97` |
| `-0.03 ≤ p ≤ +0.03` | bypass (clean) | — |
| `p > +0.03` | Tape | `d = (p - 0.03) / 0.97` |

Only one chain is instantiated as active at a time. There is **no crossfade through centre** — the two signatures are contradictory (compander breathing + aliasing vs. pitch wander + head bump), and blending them cancels the identity of both. Centre dead zone gives a reliable clean position on a real pot.

Switching chains: 10 ms linear crossfade against bypass, both chains reset on entry.

`d` is a macro over all per-stage parameters in the tables below; a single sweep must travel from "slightly coloured" to "obviously damaged" monotonically.

Output level must match bypass within ±1 dB at `d = 0` and stay within ±3 dB across the full sweep. Apply a static makeup gain curve per chain.

---

## 2. Shared infrastructure

Instantiated once, used by whichever chain is active:

- **Envelope follower** — one-pole, 5 ms attack / 80 ms release, on the chain input.
- **Noise source** — xorshift32 → float, shaped per chain.
- **Modulation block** — 3 sine accumulators, 1 OU random walk, 1 Poisson event slot (§5).
- **Output DC blocker** — one-pole HP, 20 Hz corner.

### Noise injection rule (both chains)

Noise is injected **upstream of the loss filtering and upstream of the delay / read stage**. Noise summed at the chain output reads as a separate hiss generator sitting on top of clean audio; injected upstream it is filtered and pitch-modulated with the material and reads as part of the medium.

### Signal-keyed noise gate (both chains)

The injected hiss is scaled by a shared, signal-keyed gate so the medium noise follows what you play instead of sitting on continuously. One presence detector (peak-follow of the block input: instant attack, slow release) drives a soft-knee target — fully open above `floor × THR`, gliding to closed across a knee band below it, so a decaying note easing through the threshold never chatters the gate. The gate gain has a **fast one-pole attack** (noise arrives *with* the note, no swell-in lag) and a **slow linear release** (an even, unhurried fade-out over several seconds — a linear ramp rather than an exponential one-pole, which would lurch downward early then crawl). The bypass noise-duck multiplies on top of this, so the trail-tail behaviour is unchanged. Constants: `MNEMD_NGATE_{ATK_MS, REL_MS, DET_MS, THR, KNEE}`.

---

## 3. Chain A — BBD (CCW)

```
in → input LPF → compressor → [decimate ZOH → +noise → N-stage line → stage loss]
   → interpolate ZOH → reconstruction LPF → expander → out
```

The compander sandwich must enclose the noise and loss stages. That ordering is what produces noise breathing, which is the primary BBD identity — not the darkness.

### A.1 Clock / bandwidth coupling — Tier 1

`f_clk` is the single driver of delay length, bandwidth and aliasing. Fixed stage count.

| `d` | `f_clk` | Line delay (`N / 2f_clk`, N = 256) |
|---|---|---|
| 0.0 | 48 kHz | 2.7 ms |
| 0.5 | 22 kHz | 5.8 ms |
| 1.0 | 9 kHz | 14.2 ms |

Map `f_clk` exponentially in `d`.

Implementation: fractional phase accumulator, increment `f_clk / fs`. On overflow, write input into the line and advance the write index. Read index = write index − N. Output holds the last read sample until the next tick (zero-order hold, **no output interpolation** — the imaging is the sound).

> The delay this chain introduces is not summed with dry inside the block. If a dry path is summed externally, comb filtering results; reduce `N` to 64–128 in that case.

### A.2 Deliberate aliasing — Tier 1

Input LPF: single biquad (Butterworth 2nd order) at `0.40 × f_clk`. Reconstruction LPF: single biquad at `0.40 × f_clk`. Deliberately shallow — steeper filtering removes the fold-back products that define the effect.

Coefficients recomputed at control rate only (§5).

### A.3 Compander — Tier 1

2:1 / 1:2, RMS-ish detector per side.

- Compressor: `g_c = env^-0.5`, attack 2 ms, release 50 ms.
- Expander: `g_e = env^+0.5`, attack 2 ms, release 50 ms, detector on the post-line signal.
- Scale both exponents by `0.3 + 0.7·d` so the breathing intensifies with depth.

`powf` at control rate only; interpolate gain per sample.

### A.4 Stage loss — Tier 1

One one-pole LPF after the line, corner `0.30 × f_clk`. Do **not** cascade N one-poles; the lumped single pole is perceptually sufficient.

### A.5 Noise floor — Tier 1

White noise, one one-pole LPF at 6 kHz, injected at the decimated rate before the line.

| `d` | Level |
|---|---|
| 0.0 | −74 dBFS |
| 1.0 | −52 dBFS |

### A.6 Nonlinearity — Tier 2

`tanh` soft clip before the compressor, drive `1.0 + 3.0·d`. No oversampling; the input LPF already band-limits. Drop first if over budget.

### Excluded

Per-stage charge transfer model; clock feedthrough tone; per-stage DC offset accumulation; measured NE571 transfer curve; variable-samplerate delay-line formulation (only required for a swept clock, which this control does not do at audio rate).

---

## 4. Chain B — Tape (CW)

```
in → saturation → +noise → modulated fractional read → loss filters → dropout → out
```

### B.1 Speed irregularity — Tier 1

The single most identifiable tape term. Three components summed into one pitch-offset signal, in cents:

**Sines** (incommensurate, prevents an audible repeating cycle):

| Component | Rate | Share of total depth |
|---|---|---|
| Wow | 0.7 Hz | 0.50 |
| Flutter | 4.3 Hz | 0.30 |
| Flutter 2 | 11.7 Hz | 0.20 |

Each sine's amplitude is re-randomised ±20 % every 250 ms, smoothed by a one-pole (τ = 150 ms). This jitter is what separates "tape" from "chorus".

**OU random walk**: `x += (-x/τ + σ·white)·dt`, τ = 0.5 s, contributes 25 % of total deviation. Adds non-periodic drift.

Total deviation: `0 → ±35 cents` over `d`.

**Read stage**: circular buffer, 40 ms, nominal read offset 20 ms (allows bipolar modulation). Pointer increment = `2^(cents/1200)`. 3-point Lagrange interpolation. Allpass interpolation is cheaper but detunes with coefficient — not acceptable here.

### B.2 Loss filters — Tier 1

Three filters total:

| Filter | `d = 0` | `d = 1` |
|---|---|---|
| One-pole LP (HF loss) | 18 kHz | 3.5 kHz |
| Peaking biquad (head bump), 70 Hz, Q 1.2 | +0 dB | +4 dB |
| One-pole HP | 30 Hz | 45 Hz |

The head bump is a large share of the perceived "tape" quality and costs one biquad — keep it above the saturation stage in priority.

### B.3 Saturation — Tier 1

Static asymmetric waveshaper:

```
y = tanh(k·x + a·x²)      k = 1 + 3·d,  a = 0.15·d
```

2× oversampling with ADAA1 on the shaper. **No hysteresis model** — Jiles-Atherton with an iterative or RK4 solver plus 8× oversampling is out of budget and its contribution is not distinguishable from the above once wow/flutter and loss are present.

**Bias deadzone — Tier 2**: flatten the curve origin (`y = tanh(k·x)·(x²/(x²+t²))`, `t = 0.02·d`) for the under-biased cassette signature. Cheap and audibly valuable; keep if the ADAA1 branch allows.

### B.4 Noise — Tier 1

Pink-ish: white through two cascaded one-poles (2 kHz, 200 Hz), summed 0.7/0.3.

Level: `base + k·env`, where the envelope-dependent term models asperity/modulation noise — hiss that rises with signal level rather than sitting statically underneath.

| `d` | base | k (env term, dB at full scale input) |
|---|---|---|
| 0.0 | −76 dBFS | +0 dB |
| 1.0 | −54 dBFS | +8 dB |

Injected before B.1's read stage so it warbles, and before B.2's filters so it ages.

### B.5 Dropouts — Tier 2

Single event slot, Poisson-triggered (§5), rate `0 → 1.2 /s`.

Each event: duration 5–40 ms, gain dip −3 to −18 dB (depth scaled by `d`), envelope = 3 ms fall / duration hold / 12 ms recovery. Simultaneously drop the B.2 one-pole corner by a factor 0.3–0.6 for the event duration.

**Snags — Tier 2**: reuse the same event slot at 0.4× the rate; instead of a gain dip, apply a pitch offset of 30–150 cents into B.1 with 15 ms fall / 60 ms recovery (asymmetric — fast dip, slow recovery).

### Excluded

Jiles-Atherton hysteresis and all iterative solvers; physical gap/spacing/thickness loss equations (sinc nulls, exponential spacing term); azimuth misalignment; print-through; record/playback EQ curve pairs; scrape flutter above 100 Hz *(see removal order — retained only if budget allows)*.

---

## 5. Modulation and event generator

- **Sines**: 3 phase accumulators, sine from a 512-entry table with linear interpolation, or a 5th-order polynomial approximation.
- **Jitter**: per-sine amplitude target re-drawn every 250 ms, one-pole smoothed.
- **OU**: one state variable, updated at control rate.
- **Poisson**: per control block, trigger if `rand() < rate · blockSize / fs`. One active slot; ignore retriggers while busy.
- **Control rate**: 32 samples (≈1.5 ms). All filter coefficient recomputation, `powf`, `exp`, envelope-to-gain mapping and event logic run here. Per-sample work is limited to: filter state updates, waveshaper, interpolated read, and linearly-interpolated gain application.

---

## 6. Budget and removal order

If over budget, remove in this order:

1. Scrape flutter (Tape) — if implemented at all
2. BBD nonlinearity (A.6)
3. Saturation asymmetry term (B.3, keep symmetric `tanh`)
4. Bias deadzone (B.3)
5. Dropout HF notch (keep the gain dip)
6. Snags (B.5)

Never remove: `f_clk` coupling, ZOH decimation, compander (BBD); speed irregularity, HF loss, head bump, envelope-modulated noise (Tape).

---

## 7. Validation

**BBD**
- Sweep `d` with a 3 kHz sine: fold-back products must appear and descend in frequency. If the spectrum stays clean, the input/reconstruction filters are too steep.
- Staccato input at `d > 0.5`: noise floor must audibly pump between notes. If not, the noise is outside the compander sandwich.
- Sustained input: no zipper noise on knob movement (verify gain interpolation).

**Tape**
- Sustained note, `d = 0.7`, 30 s: pitch wander must not present an audible repeating period.
- Same material passed through 8 times: cumulative HF loss must be monotonic and noise must accumulate.
- Low E fundamental: verify the head bump does not push the fundamental into the saturation stage; check HP corner is not eating fundamentals.

**Both**
- Level match against bypass at `d = 0`, and across the sweep.
- Snap `p` from full CCW to full CW: no click, no discontinuity from stale state.

---

## 8. Design decisions

| Decision | Rationale |
|---|---|
| No crossfade between models | Signatures are contradictory; blending cancels both identities |
| Noise upstream of loss and modulation in both chains | Downstream injection reads as a separate hiss generator, not as medium noise |
| Shallow anti-alias filtering in the BBD chain | Aliasing fold-back is the effect, not an artefact to suppress |
| Compander retained despite cost | Noise breathing, not bandwidth loss, is the primary BBD identifier |
| Hysteresis dropped entirely | Wow/flutter + loss + level-dependent noise carry the tape identity at a small fraction of the cost |
| Sines + jitter + random walk rather than filtered noise | Pure sines read as chorus; pure noise reads as malfunction; the mix reads as a transport |
| Head bump prioritised over saturation refinement | One biquad, large perceptual return |
| Signal-keyed noise gate with linear (not exponential) release | Faithful always-on hiss can be tamed without a hard gate; a linear multi-second fade reads as the medium dying away, where an exponential one-pole lurches down early then crawls |
| BBD reconstruction/loss LPF cutoffs driven by the smooth target clock, not the quantised `f_clk` | Integer ZOH hold length kills fractional-ratio decimator sizzle, but stepping the filter cutoffs at each integer boundary clicked; the filters only roll off imaging and need not lock to the exact quantised clock |

---

## 9. References

- C. Raffel, J. O. Smith, *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10 — component-wise BBD model (filters, compander, nonlinearity); fixed sample rate.
- M. Holters, J. D. Parker, *A Combined Model for a Bucket Brigade Device and its Input and Output Filters*, DAFx-18 — variable-samplerate formulation; required only for clock sweeps.
- J. Chowdhury, *Real-time Physical Modelling for Analog Tape Machines*, DAFx-19, and `github.com/jatinchowdhury18/AnalogTapeModel` — open C++ reference for hysteresis, loss terms and measured flutter characteristics.
