# hostsim — offline renderer for NitroLab armitage

`armsim` runs the unmodified `pedals/nitrolab/modules/armitage.h` the way the
NitroLab shell does (`pedals/nitrolab/main.cpp`): 48 kHz, 48-frame blocks,
`PreProcess` → `Process` → K6 equal-power dry/wet mix, `Controls()` every
10 ms (every 10th block). Mono in, mono out. SW3 is fixed to UP (armitage).

## Build

    make -C tools/hostsim          # -> tools/hostsim/armsim (clang++ -O2)

## Render

    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --out out.wav
    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --k2 0.2 \
        --set ARM_PWM_HZ_MAX=20 --out pwm20.wav

| option | meaning |
|-|-|
| `--in FILE` | WAV, 48 kHz, PCM 16/24/32-bit or 32-bit float; channel 0 of multi-channel files |
| `--out FILE` | mono 24-bit WAV (the shell's output after the K6 mix) |
| `--out-f32 FILE` | also raw float32 (no quantisation, for diffs / analysis) |
| `--loop N` | play the input N times back to back |
| `--level P` | scale the input so its peak is P (0.18 = the DI excerpts at playing level) |
| `--k1 .. --k6 V` | knob positions 0..1, as the module sees them after `RemapKnob` (defaults K1 .5, K2 .5, K3 0, K4 .5, K5 .5, K6 1 = full wet) |
| `--sw1`, `--sw2` | `up` / `mid` / `down` (default mid) |
| `--fs1 1` | one FS1 tap at the start (armitage: wet mute) |
| `--set NAME=V` | override a constant from `armitage_constants.h` (repeatable; bools take true/false/0/1) |
| `--dump-constants` | print every constant: name, type, value, default expression, `fixed` / `set` |

Controls are static for the whole render and are applied before the first
block; the K6 smoother starts at its target (the knobs have been in place
since power-up). On hardware the first `Controls()` call comes ~10 ms after
audio starts and K6 ramps up from dry; that start-up transient isn't modelled.

## Constant overrides

`armitage_constants.h` declares each constant as `ARM_TUNABLE(type, NAME, value)`.
In the firmware this expands to the same `constexpr type NAME = value;` as
before, so the binary is bit-identical. `host_tunables.h` defines the macro
first, so on the host each constant is a runtime variable registered in
`hostsim::Registry()` (name, type, pointer, default expression). Defaults are
re-evaluated after `--set`, so derived ones follow: `--set ARM_ALIGN=0` also
switches `ARM_GAIN_UP`, `ARM_OUT_GAIN`, ... to their unaligned values unless
they are set explicitly.

`armitage.h` is compiled unmodified. `armitage_host.h` includes it with
`constexpr` read as `const` and `static_assert` dropped, because a few
expressions in it are constexpr over tunables. The dropped check
(`ARM_BETA_CLAMP + ARM_XMOD_MAX < 90`) runs at startup instead
(`ValidateTunables`).

Fixed on the host (`ARM_FIXED`, not settable): `ARM_SIN_SIZE` (table size /
mask) and `ARM_ADD_MMAX` (array sizes). `ARM_ADD_MID` must stay ≤ `ARM_ADD_MMAX`.

Constants are read wherever the module reads them, so those used in `Init()`
(filter laws, smoothing coefficients, crossfade lengths, band tables) only take
effect at the start of a render. Every `--set` is applied before `Init()`.
