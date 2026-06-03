# Plague Bearer DSP Emulation — Handover

## Objective

Real-time model of the Flight of Harmony Plague Bearer filter/mangler on the Daisy Seed. The defining character is **gain-dependent nonlinearity**. A clean linear filter is not an acceptable result.

## Platform

- **Board:** Daisy Seed (STM32H750, ARM Cortex-M7 @ 480 MHz, single-precision hardware FPU).
- **Frameworks:** libDaisy + DaisySP.
- **Audio:** block-based `AudioCallback`, 48 kHz, small block size (e.g. 48).
- **Language:** C++17 (Daisy toolchain).

## Reference behavior

- Resonant filter that rings, driven into saturation; "FSU"/mangler character.
- Strong input-level dependence: low input → subtle coloration; high input → harsh distortion that overwhelms the filtering.
- Independent HP and LP corners; overlapping them gives comb / notch coloration.
- `High` control = ringing amount, `Low` control = notch amplitude/position.
- Soft output limiter on the final stage.

## Core implementation

1. **Zero-delay-feedback (TPT) state-variable filter.** Gives HP and LP outputs from one structure, so the combined HP+LP / comb behavior is essentially free. Reference: Zavalishin, *The Art of VA Filter Design*.
2. **Saturator inside the resonance feedback path.** This is the heart of it. As signal level rises, the saturator lowers effective loop gain, so Q and slope shift with amplitude on their own — the level-dependence is emergent, not scripted.
3. **Saturator on the input drive stage.** The primary character control.
4. **Soft-clip waveshaper** as the output limiter.

## Decisions

- **Behavioral nonlinear SVF, not WDF/SPICE.** No official schematic exists; the behavioral model gets the character at a fraction of the effort and CPU.
- **`float`, not `double`.** M7 has a single-precision hardware FPU; double is emulated and slow.
- **No explicit signal-level → cutoff/Q modulation.** Dropped: the feedback-path saturator already produces amplitude-dependent Q for free. Adding an envelope follower to steer cutoff/Q is extra code, extra CPU, and extra tuning for behavior we get implicitly.
- **Cheap polynomial saturators, not lookup tables.** A cubic/`tanh`-approx soft clip is enough on the M7; `tanhf` is too costly per sample and the LUT machinery isn't worth it. Reserve exact `tanhf` for offline reference only.

## Constraints

- **2× oversampling baseline.** Saturation aliases; 2× with a cheap halfband removes most of it. Go higher only if CPU allows and aliasing is still audible. This is the first knob to drop if CPU-bound.
- **Get input drive staging right before tuning anything else.** A single clip point yields a generic distorted filter, not the Plague Bearer.
- **No allocation, locks, or blocking in the audio callback.** Pre-allocate all state; bounded per-sample work.

## Acceptance criteria

1. Input level alone audibly shifts timbre (subtle → harsh).
2. Resonance can ring toward self-oscillation.
3. Overlapping corners produces comb / notch coloration.
4. No glaring aliasing at high resonance or on transients.
5. Sustained CPU leaves headroom at the chosen block size.
