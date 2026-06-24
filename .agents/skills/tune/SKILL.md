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

Groupings mirror `src/constants.h`. Use prefixes to grep when the user names a feature instead of a constant.

### Mode A — Bordun
- **Oscillator**: `OSC_K`, `OSC_DC_TRIM`, `OSC_FOLD_AMT`, `OSC_PEAK_GAIN`, `OSC_SAW_GAIN`, `OSC_TRI_GAIN`, `OSC_SQR_GAIN`
- **Envelope follower**: `ENV_LP_CUTOFF_HZ`, `ENV_PRE_GAIN`, `ENV_ATTACK_BIAS`, `ENV_RELEASE_BIAS`
- **Envelope modulation**: `ENV_FILTER_MOD`, `ENV_FOLD_MOD`
- **Stage / mix / ladder**: `OSC_GAIN`, `LADDER_DRIVE`, `LADDER_CUTOFF_OFFSET`, `DRY_TRIM`

### Mode B — Sprawl
- **Glitch zones (SW1 MID)**: `GLITCH_DEADZONE`, `GLITCH_XOR_MAX_BIT`, `GLITCH_EVENT_RATE_HZ_MAX*`, `GLITCH_EVENT_DUR_*_SAMPLES`, `GLITCH_BUFFER_SAMPLES`, `GLITCH_RAMP_SAMPLES`, `GLITCH_ENV_GATE`
- **Reverb / K5 bipolar**: `K5_CENTER_DEADZONE`, `REVERB_INPUT_GAIN`, `REVERB_TIME`, `REVERB_MAX_FEEDBACK`, `REVERB_AMT_SMOOTH_COEF`
- **Feedback path** (in `NitroTron3.cpp`, not constants.h): `FB_SAT_DRIVE`, `FB_DUCK_THRESHOLD`, `FB_DUCK_ATTACK_MS`, `FB_DUCK_RELEASE_MS`, `ON_PLAY_*`, `WET_HPF_FREQ`
- **Bode SSB shifter (SW2 DOWN)**: `FREQ_SHIFT_MAX_HZ`, `FREQ_SHIFT_DEADZONE`, `FREQ_SHIFT_CURVE`

### Mode C — Schism
- **Sine wavefolder (SW1 UP)**: `SINEFOLD_DRIVE_MAX`, `SINEFOLD_COMP_AT_MAX`
- **Moog ladder (SW2 UP)**: `MODE_C_LADDER_RES_MAX`, `MODE_C_ENV_MOD_RANGE`, `MODE_C_ENV_SCALE`, `MODE_C_CUTOFF_MAX_HZ`, `MODE_C_CUTOFF_MIN_HZ`, `MODE_C_CUTOFF_K1_MAX_HZ`, `K3_DEADZONE`
- **Pre-filter drive (K5)**: `MODE_C_DRIVE_MIN`, `MODE_C_DRIVE_MAX`, `MODE_C_MOOG_INPUT_PAD`, `MODE_C_GRENDEL_INPUT_PAD`, `MODE_C_PHASER_INPUT_PAD`
- **Amp-env VCA**: `MODE_C_VCA_GAIN`
- **Synth osc (SW1 DOWN)**: `MODE_C_SYNTH_UNISON_VOICES`, `MODE_C_SYNTH_DETUNE_CENTS_*`, `MODE_C_SYNTH_HYPER_V{3,5,7}_END`, `MODE_C_SYNTH_VOICE_SPREAD`, `MODE_C_SYNTH_SAW_PLATEAU`, `MODE_C_SYNTH_PWM_*`, `MODE_C_SYNTH_VCA_GAIN`
- **Filter env smoothers**: `MODE_C_FILTER_ENV_ATTACK_MS`, `MODE_C_FILTER_ENV_RELEASE_MS`, `MODE_C_GRENDEL_ENV_ATTACK_MS`, `MODE_C_GRENDEL_TARGET_GAIN`, `MODE_C_GRENDEL_SIZE_MOD_AMT`
- **Bit crusher (SW1 MID)**: `MODE_C_BITCRUSH_MAX_BIT`, `MODE_C_BITCRUSH_ENV_GATE`, `MODE_C_BITCRUSH_RAMP_SAMPLES`
- **Post-filter**: `MODE_C_POST_FILTER_GAIN`, `MODE_C_LIMIT_*`
- **Grendel formant data**: `GRENDEL_NUM_FORMANTS`, `GRENDEL_NUM_VOWELS`, `GRENDEL_FORMANT_Q`, `GRENDEL_OUT_GAIN`, `GRENDEL_VOWELS`, `GRENDEL_FORMANT_GAIN`, `GRENDEL_SIZE_*`, `GRENDEL_ENV_PATH_RANGE`
- **Phaser (SW2 DOWN)**: `PHASER_F1_HZ_*`, `PHASER_SWEEP_OCT`, `PHASER_LFO_*_HZ_*`, `PHASER_FB_MAX`

### Preset system & UI
- **LED 1 (preset blink)**: `LED_SHORT_ON_MS`, `LED_LONG_ON_MS`, `LED_ELEM_GAP_MS`, `LED_REPEAT_GAP_MS`
- **LED 2 (dirty / save / save-confirm)**: `LED_DIRTY_*_MS`, `LED_SAVE_MODE_*_MS`, `LED_SAVE_CONFIRM_*_MS`
- **Bank burst (both LEDs)**: `LED_BANK_FLICKER_MS`, `LED_BANK_TOTAL_MS`, `LED_BANK_GAP_MS`, `LED_BANK_HOLD_MS`
- **Footswitch timing**: `FS_LONG_PRESS_MS`, `FS_BOOT_HOLD_MS`
- **Knob dirty detection**: `KNOB_DIRTY_THRESHOLD`
- **Pitch tracking**: `TRACKING_WRAP_NOTE`

The interactive UX demo (`docs/ux-demo.html`) mirrors the LED / footswitch constants and is a faster place to iterate visual timings before touching `constants.h`.
