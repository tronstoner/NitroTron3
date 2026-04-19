# NitroTron3

A DIY digital bass effects pedal built on the Electro-Smith Daisy Seed and Cleveland Music Co. Hothouse DSP kit.

## Hardware

- Daisy Seed (STM32H750, 480 MHz Cortex-M7, 32-bit float, 48 kHz audio)
- Hothouse DSP Pedal Kit (6 knobs, 3x 3-position toggles, 2 footswitches, LED, true-bypass relay)
- Hammond 125B enclosure

## Repository setup

This repo uses [HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) as a single git submodule under `lib/`. That submodule in turn contains libDaisy and DaisySP as nested submodules, so one checkout gets the full toolchain.

```
NitroTron3/
├── NitroTron3.cpp              # main source
├── constants.h                 # compile-time DSP constants (populated via tuning mode)
├── moog_osc.h                  # MoogOsc class — parabolic waveshaper + PolyBLEP
├── moog_ladder.h               # Huovilainen Moog ladder filter (24 dB/oct LP)
├── env_follower.h              # Moog envelope follower (rectifier → 4-pole LP)
├── pitch_tracker.h             # Zero-crossing pitch tracker for bass
├── Makefile
├── lib/
│   └── HothouseExamples/       # submodule
│       ├── libDaisy/            # nested submodule — hardware abstraction
│       ├── DaisySP/             # nested submodule — DSP library
│       └── src/
│           ├── hothouse.h       # Hothouse hardware proxy (compiled by our Makefile)
│           └── hothouse.cpp
└── .vscode/                    # build/debug tasks, IntelliSense config
```

## Getting started

Clone with submodules:

```sh
git clone --recurse-submodules <repo-url>
cd NitroTron3
```

Build the libraries (one-time):

```sh
make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP
```

Build and flash:

```sh
make            # compile
make program    # flash via OpenOCD / ST-Link
make program-dfu  # flash via DFU bootloader
```

## Current state — Stage 4 (Full Drone Effect)

Complete Mode A drone effect: PolyBLEP oscillator → Huovilainen ladder filter → VCA controlled by envelope follower tracking bass input → mix with dry signal. The oscillator only sounds when you play — amplitude follows the bass input's dynamics.

### Signal Chain

```
Input ──┬──────────────────────────────────────────► [Mix K6] ──► Output
        │                                               ▲
        └──► [EnvFollower] ──► [VCA gain]               │
                                 │                      │
                    [Osc1 K1-K3] + [Osc2 K5] ──► [Ladder K4] ──┘
```

### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Semitone / Interval | **Fixed**: 12 steps (C–B). **Track**: ±12 semitone offset, center dead zone = unison |
| KNOB 2 | Octave | **Fixed**: 7 positions (C-1–C5). **Track**: 7 positions (-3 to +3 octave offset) |
| KNOB 3 | Fine tune | ±50 cents continuous (osc1 only — creates beating with osc2) |
| KNOB 4 | Tone / Wavefold | SAW/SQ: full range ladder cutoff (80 Hz–8 kHz). TRI: CCW→noon = cutoff, noon→CW = wavefolding (filter stays fully open) |
| KNOB 5 | Osc 2 detune | Center = off (dead zone). Outside center = ±1–12 semitone steps. Not affected by fine tune |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet (oscillator) |
| SWITCH 1 | Waveform | **UP** - Saw<br/>**MIDDLE** - Triangle<br/>**DOWN** - Square |
| SWITCH 2 | Drone mode | **UP** - Fixed pitch (K1/K2 set absolute note)<br/>**MIDDLE** - Pitch tracking (osc follows bass, K1/K2 are relative offsets)<br/>**DOWN** - (TBD, same as Fixed) |
| SWITCH 3 | Unused | **UP** - <br/>**MIDDLE** - <br/>**DOWN** -  |
| FOOTSWITCH 1 | Bootloader | Hold 2 s → enter DFU bootloader for flashing |
| FOOTSWITCH 2 | Bypass | Toggles effect on/off. LED 2 on = effect active. The bypassed signal is buffered |

### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Waveform indicator: solid = Saw, slow blink = Triangle, fast blink = Square |
| LED 2 (right) | Bypass: on = effect active, off = bypassed |

## Updating dependencies

```sh
cd lib/HothouseExamples
git pull
git submodule update --recursive
cd ../..
make -C lib/HothouseExamples/libDaisy clean && make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP clean && make -C lib/HothouseExamples/DaisySP
```
