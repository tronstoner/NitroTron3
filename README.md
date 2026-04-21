# NitroTron3

A DIY digital bass effects pedal built on the Electro-Smith Daisy Seed and Cleveland Music Co. Hothouse DSP kit. Licensed under [GPL v3](LICENSE).

## Hardware

- Daisy Seed (STM32H750, 480 MHz Cortex-M7, 32-bit float, 48 kHz audio)
- Hothouse DSP Pedal Kit (6 knobs, 3x 3-position toggles, 2 footswitches, LED, true-bypass relay)
- Hammond 125B enclosure

## Repository setup

This repo uses [HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) as a single git submodule under `lib/`. That submodule in turn contains libDaisy and DaisySP as nested submodules, so one checkout gets the full toolchain.

```
NitroTron3/
├── src/                        # source code
│   ├── NitroTron3.cpp          # main application
│   ├── constants.h             # compile-time DSP constants
│   ├── moog_osc.h              # MoogOsc class — parabolic waveshaper + PolyBLEP
│   ├── moog_ladder.h           # Huovilainen Moog ladder filter (24 dB/oct LP)
│   ├── env_follower.h          # Moog envelope follower (rectifier → 4-pole LP)
│   └── pitch_tracker.h         # YIN pitch tracker for bass (4x decimation)
├── docs/                       # specs, plans, research
│   ├── PROJECT.md              # top-level plan, staging, architecture
│   ├── MODE_A_DRONE.md         # Mode A full spec
│   ├── TUNING.md               # tuning-mode spec
│   └── PITCH_TRACKING.md       # pitch tracking research + plan
├── lib/
│   └── HothouseExamples/       # submodule
│       ├── libDaisy/            # nested submodule — hardware abstraction
│       ├── DaisySP/             # nested submodule — DSP library
│       └── src/
│           ├── hothouse.h       # Hothouse hardware proxy
│           └── hothouse.cpp
├── .agents/skills/             # reusable agent task recipes
├── Makefile
├── README.md
├── AGENTS.md                   # AI agent instructions
├── CLAUDE.md                   # Claude Code config
└── LICENSE                     # GPL v3
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

Complete Mode A drone effect: PolyBLEP oscillator → Huovilainen ladder filter → VCA controlled by envelope follower tracking bass input → mix with dry signal. The oscillator only sounds when you play — amplitude follows the bass input's dynamics. Envelope subtly modulates filter cutoff and wavefold amount for dynamic response.

### Signal Chain

```
Input ──┬──────────────────────────────────────────► [Mix K6] ──► Output
        │                                               ▲
        └──► [EnvFollower] ──┬──► [VCA gain]            │
                             │       │                  │
                             │  [Osc1 K1-K3] + [Osc2 K5]
                             │       │                  │
                             ├──► [Wavefold] (TRI mode) │
                             │       │                  │
                             └──► [Ladder K4] ──────────┘
```

### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Semitone / Interval | ±12 semitone offset, center dead zone. **Fixed**: center = A, also sets wrap point for tracking. **Track**: center = tracked note, wraps at the note set in fixed mode |
| KNOB 2 | Octave | 7 positions (C-1–C5). **Octave-locked**: sets target octave. **Direct**: ±3 octave offset from tracked pitch |
| KNOB 3 | Fine tune | ±50 cents continuous (osc1 only — creates beating with osc2) |
| KNOB 4 | Tone / Wavefold | SAW/SQ: full range ladder cutoff (80 Hz–8 kHz). TRI: CCW→noon = cutoff, noon→CW = wavefolding (filter stays fully open) |
| KNOB 5 | Osc 2 detune | Center = off (dead zone). Outside center = ±1–12 semitone steps. Not affected by fine tune |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet (oscillator) |
| SWITCH 1 | Waveform | **UP** - Saw<br/>**MIDDLE** - Triangle<br/>**DOWN** - Square |
| SWITCH 2 | Drone mode | **UP** - Fixed pitch (K1 sets note, K2 sets octave)<br/>**MIDDLE** - Octave-locked tracking (pitch class follows bass in K2's octave, K1 adds interval)<br/>**DOWN** - Direct tracking (osc follows exact bass pitch, K1/K2 are relative offsets ±12 semi / ±3 oct) |
| SWITCH 3 | Mode select | **UP** - Mode A (Drone)<br/>**MIDDLE** - Mode B (Granular Glitch, future)<br/>**DOWN** - Mode C (Freq Shift, future) |
| FOOTSWITCH 1 | Preset | Short press: cycle Manual → Preset 1–5 (reload current if edited). In save mode: cycle target slot |
| FOOTSWITCH 2 | Bypass / Save | Short press: bypass on/off. Long press: enter save mode (long press again = save, short press = cancel). Buffered bypass |
| FS1 + FS2 held 2 s | Bootloader | Enter DFU bootloader for flashing |

### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: 1–5 blinks in 1.5 s window = preset number. Manual mode: waveform indicator (solid = Saw, slow blink = Triangle, fast blink = Square) |
| LED 2 (right) | Bypass on = effect active. Rapid flash = preset edited (dirty). Fast blink = save mode. Burst flash ~1 s = save confirmed |

## Trademarks

All product names, trademarks, and registered trademarks are property of their respective owners. References are for descriptive and educational purposes only — this project is not affiliated with or endorsed by any mentioned company.

## Updating dependencies

```sh
cd lib/HothouseExamples
git pull
git submodule update --recursive
cd ../..
make -C lib/HothouseExamples/libDaisy clean && make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP clean && make -C lib/HothouseExamples/DaisySP
```
