# Mode A — Drone OSC — Implementation Spec

Inspired by the Moog MoogerFooger FreqBox (MF-102). An internal oscillator, amplitude-controlled by an envelope follower tracking the bass input, filtered through a Moog-style ladder, mixed back with the dry signal.

**Not** included: oscillator sync, FM modulation. The FreqBox's sync/FM features are intentionally dropped — this mode focuses on the drone/subharmonic character alone.

---

## Signal Chain

```
Input ──┬──────────────────────────────────────────────► [Mix] ──► Output
        │                                                   ▲
        └──► [Full Wave Rectify] ──► [4-pole 33Hz LP] ──► [Threshold Gate]
                                                              │
                                                           [VCA gain]
                                                              │
                                                    [PolyBLEP Oscillator]
                                                              │
                                                    [Huovilainen Ladder LP]
                                                              │
                                                           [Mix] ──────────┘
```

---

## Stage-by-Stage Detail

### 1. Envelope Follower — Moog MoogerFooger Topology

Confirmed from MF-101 / MF-102 circuit analysis: full wave rectifier → 4-pole 33 Hz lowpass filter → threshold gate.

**Why this works:**
- The 4-pole (24 dB/oct) 33 Hz LP strips all audio-rate ripple while tracking the amplitude contour cleanly.
- Attack and release behavior is inherent to the filter's time constant — there are no separate attack/release controls in Moog's implementation.
- Threshold gate prevents oscillator bleed during silence.
- This is the same envelope follower block used across all MoogerFooger pedals.

**Implementation:**
- Rectifier: `fabsf(in)`
- 4-pole LP: cascade of two Daisy `OnePole` filters in biquad form, or a proper Butterworth 4-pole. Cutoff is tunable (default 33 Hz) — see `TUNING.md` page 2.
- Threshold: if output < threshold, force VCA gain to 0. Tunable.

### 2. VCA

Oscillator amplitude scaled by envelope follower output. Simple multiply in the audio loop.

```cpp
float vca_out = osc_sample * env_follower_value;
```

### 3. PolyBLEP Oscillator with Moog-style Fundamental Boost

The oscillator must sound **round and bass-heavy** before any filtering. We model the Moog VCO's natural fundamental weight directly in the oscillator — this is more efficient than trying to recover it with downstream filtering.

**Architectural model (what we're emulating, not copying):**

1. **Curved ramp from capacitor charging physics** — the Moog VCO timing capacitor charges through a near-ideal current source, producing a slightly parabolic ramp rather than linear. This distributes more energy into the fundamental and rolls off higher harmonics faster than a pure mathematical sawtooth.
2. **Triangle-core architecture** — Moog VCOs generate triangle internally via a precision integrator, then derive the saw. The integrator-smooth triangle means fundamental is inherently strong at the core.
3. **Stable DC operating point** — low output impedance and centered bias mean no phase smear or LF rolloff. The fundamental is coherent in time.

**What NOT to model (overrated for this goal):**
- Thermal drift / slow pitch modulation — contributes to "aliveness," not bass weight
- Power supply noise — distraction
- Oscillator bleed — mixing concern
- High-Q filter loading — irrelevant for this signal chain

#### Primary implementation: parabolic waveshaper on PolyBLEP saw

Apply a parabolic correction to a linear phase ramp. Approximates the capacitor-charging curve cheaply, no transcendentals.

```cpp
// x = linear phase ramp, 0..1
// k = curve amount (see TUNING.md page 1, default 0.35)
float shapeRamp(float x, float k) {
    return x + k * (x - x * x);
    // x*x pulls energy toward the fundamental
    // cost: 2 multiplies + 1 add per sample
}
```

#### Full oscillator class (parabolic + PolyBLEP)

```cpp
#include "daisysp.h"
using namespace daisysp;

class MoogOsc {
public:
    float phase = 0.f;
    float sampleRate;

    // Tunable via TUNING.md page 1
    float k        = 0.35f;   // Waveshape curve: 0.0=linear saw, 0.5=strong fundamental
    float dcTrim   = 0.f;     // Fine DC offset trim if needed
    float peakGain = 1.0f;    // Pre-filter trim

    void Init(float sr) { sampleRate = sr; }

    float Process(float freq) {
        float phaseInc = freq / sampleRate;
        phase += phaseInc;
        if (phase >= 1.f) phase -= 1.f;

        // Parabolic shaping
        float shaped = phase + k * (phase - phase * phase);

        // Rescale to -1..1
        shaped = shaped * 2.f - 1.f;

        // DC correction for parabolic term
        shaped -= k * (1.f / 6.f);

        // Apply DC trim
        shaped += dcTrim;

        // PolyBlep at discontinuity
        shaped -= polyBlep(phase, phaseInc);

        return shaped * peakGain;
    }

private:
    float polyBlep(float p, float inc) {
        if (p < inc) {
            float t = p / inc;
            return t + t - t * t - 1.f;
        } else if (p > 1.f - inc) {
            float t = (p - 1.f) / inc;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }
};
```

#### Optional: triangle-core variant

More expensive, closer to actual Moog architecture. Use if parabolic shaper alone is insufficient after Stage 3 tuning pass.

```cpp
// Generate triangle first, derive asymmetric saw
float tri = 2.f * std::abs(2.f * phase - 1.f) - 1.f; // -1..1
float foldAmt = 0.15f;  // tunable
float asymSaw = tri + foldAmt * tri * tri * (tri > 0.f ? 1.f : -1.f);
```

#### Waveforms

Three waveforms selectable via Toggle 1: Saw, Triangle, Square.

- **Saw:** parabolic-shaped saw as above. `k` affects it most strongly.
- **Triangle:** derived from phase, minimally affected by `k` (fundamental is already strong).
- **Square:** classic PolyBLEP square, unaffected by `k`.

#### Pitch

- Frequency calculation: `f = 440.0f * powf(2.0f, (note - 69) / 12.0f)`
- Semitone knob: C–C, 12 quantized steps (K1 in normal mode)
- Octave knob: 5 positions, C1–C5 (K2 in normal mode)
- Fine tune: ±50 cents continuous (K3 in normal mode)
- Run at 48 kHz (Daisy default). No oversampling needed — PolyBLEP handles aliasing adequately. CPU cost of shaper + PolyBLEP is <5% single core.

#### Verification

- Route output to spectrum analyzer.
- Compare fundamental-to-2nd-harmonic ratio against a pure saw (pure saw: 2nd harmonic at −6 dB relative to fundamental).
- Moog-like target: 2nd harmonic slightly weaker than −6 dB, fundamental slightly stronger.
- Tune `k` until bass weight matches reference (FreqBox or similar) by ear.
- Confirm no audible aliasing in 1–4 kHz range at bass fundamental frequencies.

### 4. Huovilainen Moog Ladder Filter

24 dB/oct Moog-style ladder lowpass on oscillator output.

- Per-stage `tanh` nonlinear saturation — this is the source of the characteristic warm, fat Moog tone.
- No resonance needed for this use case (drone oscillator, not a traditional synth voice).
- Cutoff controlled by Tone knob in normal mode (K4).
- DaisySP does **not** ship this model — implement directly in C++ (~50 lines), Huovilainen / Stilson-Smith model.
- Drive (input gain) and cutoff offset tunable via `TUNING.md` page 3.

This is the most important single DSP component for achieving the FreqBox character, after the oscillator shape itself.

### 5. Mix

Linear crossfade between dry input and processed oscillator signal:

```cpp
output = (mix * osc_filtered) + ((1.0f - mix) * dry_in);
```

Mix knob (K5 in normal mode), continuous 0.0–1.0.

---

## Controls — Normal Mode

| Control | Function | Notes |
|---|---|---|
| K1 | Semitone | C–C, 12 quantized steps. Physical knob range divided into 12 equal zones |
| K2 | Octave | 5 positions, C1–C5, quantized |
| K3 | Fine tune | ±50 cents, continuous |
| K4 | Tone | Ladder filter cutoff, continuous |
| K5 | Mix | Dry / oscillator blend, continuous |
| K6 | Envelope sensitivity | Input gain before follower, continuous |
| Toggle 1 | Waveform | Saw / Triangle / Square |
| Toggle 2 | Drone mode | Fixed pitch / Octave-locked tracking / Direct tracking |
| Toggle 3 | Mode | Drone (active) / Granular / Freq Shift |
| FS1 | Preset | Short press: cycle presets (Manual → 1–5), reload if edited. In save mode: cycle target slot |
| FS2 | Bypass / Save | Short press: bypass. Long press: save mode (long press = confirm, short press = cancel). See `PROJECT.md` Preset System |

**Knob behavior on preset load:** values jump immediately to stored values — no pickup mode. Moving a knob overrides that parameter in the edit buffer (the stored preset is not modified).

See `TUNING.md` for tuning-mode knob remapping.

---

## Preset Data Structure

```cpp
struct DronePreset {
    float semitone;        // 0–11, quantized
    float octave;          // 0–4 (C1–C5), quantized
    float fine_tune;       // -0.5 to +0.5 semitones
    float tone_cutoff;     // 0.0–1.0 normalized
    float mix;             // 0.0–1.0
    float env_sensitivity; // 0.0–1.0 (input gain before envelope follower)
    uint8_t waveform;      // 0=Saw, 1=Tri, 2=Square
};
```

9-byte payload (with padding). 5 preset slots + 1 edit buffer per mode, stored in `PersistentStorage`. See `PROJECT.md` Preset System for full UX spec.

---

## Compile-Time Constants (populated via TUNING.md workflow)

```cpp
// --- Oscillator (Page 1) ---
constexpr float OSC_K            = 0.350f;  // parabolic curve
constexpr float OSC_DC_TRIM      = 0.000f;
constexpr float OSC_FOLD_AMT     = 0.150f;  // triangle-core variant only
constexpr float OSC_PEAK_GAIN    = 1.000f;

// --- Envelope follower (Page 2) ---
constexpr float ENV_LP_CUTOFF_HZ = 33.0f;   // Moog canonical
constexpr float ENV_THRESHOLD    = 0.020f;
constexpr float ENV_PRE_GAIN     = 1.000f;  // multiplied by K6 sensitivity in normal mode
constexpr float ENV_ATTACK_BIAS  = 1.000f;
constexpr float ENV_RELEASE_BIAS = 1.000f;

// --- Stage / mix / ladder (Page 3) ---
constexpr float OSC_GAIN         = 0.700f;  // final osc level into mix
constexpr float LADDER_DRIVE     = 1.000f;
constexpr float LADDER_CUTOFF_OFFSET = 0.000f;
constexpr float DRY_TRIM         = 1.000f;
```

Defaults shown are starting points. Real values land after Stage 3 full tuning pass against bass.

---

## Implementation Order (within Mode A)

1. **Stage 1:** `MoogOsc` class (parabolic + PolyBLEP). Standalone testable — fixed pitch 110 Hz, output to both channels. Tuning mode page 1 wired.
2. **Stage 2:** Huovilainen ladder filter class. Wire between oscillator and output. Tuning mode page 3 wired (ladder params).
3. **Stage 3:** Envelope follower + VCA. Wire dry input → follower → VCA gain on oscillator. Tuning mode page 2 wired. **Full re-tune pass of all three pages against bass.**
4. **Stage 4:** Normal-mode control layer. Semitone/octave/fine tune/tone/mix/sensitivity applied from knobs. Waveform toggle wired.
5. **Stage 5:** Preset save/recall via `PersistentStorage`.
6. **Stage 6:** Multi-mode dispatch scaffold (see `PROJECT.md`).

---

## References

- Huovilainen, "Non-linear Digital Implementation of the Moog Ladder Filter," DAFx 2004
- Välimäki & Pakarinen, PolyBLEP anti-aliasing literature
- Moog MF-101 / MF-102 circuit analysis (envelope follower topology)
- Moog VCO architecture (capacitor-charging ramp, triangle-core derivation)
