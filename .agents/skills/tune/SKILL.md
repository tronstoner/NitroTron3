---
name: tune
description: View or edit compile-time DSP constants in src/constants.h, then rebuild.
disable-model-invocation: true
argument-hint: [parameter value]
allowed-tools: Bash(make *) Read Edit
---

# Tune Constants

View or modify DSP constants in `src/constants.h` and rebuild.

## Usage

- `/tune` — show all current constant values
- `/tune OSC_K 0.5` — set a specific constant and rebuild
- `/tune ladder` — show constants related to the ladder filter

## Steps

1. Read `src/constants.h`
2. If arguments provided: update the specified constant, rebuild
3. If no arguments: display all constants grouped by section
4. After any change: run `make` to verify it compiles

## Sections

- **Oscillator**: OSC_K, OSC_DC_TRIM, OSC_FOLD_AMT, OSC_PEAK_GAIN, OSC_SAW_GAIN, OSC_TRI_GAIN, OSC_SQR_GAIN
- **Envelope follower**: ENV_LP_CUTOFF_HZ, ENV_PRE_GAIN, ENV_ATTACK_BIAS, ENV_RELEASE_BIAS
- **Envelope modulation**: ENV_FILTER_MOD, ENV_FOLD_MOD
- **Stage/mix/ladder**: OSC_GAIN, LADDER_DRIVE, LADDER_CUTOFF_OFFSET, DRY_TRIM
