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
│   ├── pitch_tracker.h         # YIN pitch tracker for bass (4x decimation)
│   └── preset_system.h         # preset navigation, flash storage, LED patterns
├── docs/                       # specs, plans, research
│   ├── PROJECT.md              # top-level plan, staging, architecture
│   ├── MODE_A_DRONE.md         # Mode A full spec
│   ├── MODE_B_GRANULAR.md      # Mode B full spec
│   ├── TUNING.md               # tuning-mode spec
│   ├── PITCH_TRACKING.md       # pitch tracking research + plan
│   ├── PRESET_IMPL.md          # preset system implementation plan
│   └── ux-demo.html            # interactive preset UX demo
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
├── agents-instructions.md      # AI agent behavioral rules
├── AGENTS.md                   # AI agent entry point + routing
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

## Current state — Stage 5 (Preset System) + Mode B (Granular Glitch)

Complete Mode A drone effect with full preset system. PolyBLEP oscillator → Huovilainen ladder filter → VCA controlled by envelope follower tracking bass input → mix with dry signal. Mode B granular glitch effect with grain scheduler, pitch-tracked harmony, texture shaping, and scatter control. Edit buffer + 8 stored presets per mode, dirty tracking, flash persistence across power cycles. FS1 navigates presets, FS2 toggles bypass or enters save mode. LED 1 shows preset number via Roman numeral blink encoding, LED 2 indicates active/bypass/dirty/save state.

### Mode A — Drone

#### Signal Chain

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

#### Controls

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
| SWITCH 3 | Mode select | **UP** - Mode A (Drone)<br/>**MIDDLE** - Mode B (Granular Glitch)<br/>**DOWN** - Mode C (Freq Shift, not yet implemented — dry passthrough) |
| FOOTSWITCH 1 | Preset | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | Bypass / Save | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel |
| FS1 held 2 s | Bootloader | Enter DFU bootloader for flashing |

#### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed |

### Mode B — Granular Glitch

#### Signal Chain

```
Input ──┬──────────────────────────────────────────► [Mix K6] ──► Output
        │                                               ▲
        ├──► [EnvFollower] ──► grain amplitude           │
        │                                                │
        ├──► [PitchTracker] ──► harmony logic            │
        │                                                │
        └──► [Ring Buffer, 8s SDRAM] ──► [Grain Scheduler]
                    ▲                         │
                    │                   [Grain Voices × 8]
                    │                         │
                    │                   [Texture Shaper (SW1)]
                    │                         │
                    │                    [Wet HPF 150 Hz]
                    │                         │
                    └── [Feedback K5] ◄───────┴──────────┘
```

#### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Interval | ±24 semitones, centered with dead zone. Pitch offset applied to each grain relative to tracked bass note |
| KNOB 2 | Buffer range | CCW = tight (100 ms, recent audio only). CW = deep (full 8 s, long trails) |
| KNOB 3 | Character / Glitch | CCW = soft, long, tight grains (200 ms, single pass, high overlap). CW = short, sharp, chaotic (20 ms, stutter loops, scatter, reverse probability) |
| KNOB 4 | Texture amount | Depends on SW1 position. See Switch 1 notes |
| KNOB 5 | Feedback | CCW = none. CW = max feedback (0.95 ceiling). Wet output re-injected into ring buffer |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet. Equal-power curve |
| SWITCH 1 | Texture mode | **UP** - Decimator/Wavefolder bipolar (K4 CCW = max crush, noon = clean, CW = wavefold)<br/>**MIDDLE** - Clean (no texture processing)<br/>**DOWN** - Ringmod (K4 CCW–30% = tremolo 1–15 Hz, 30%–CW = bell partials, pitch-tracked with keytracked LPF) |
| SWITCH 2 | Harmony | **UP** - Fixed interval (K1 semitones above tracked note)<br/>**MIDDLE** - Resonance (grains lock onto nearby harmonics)<br/>**DOWN** - Resonance (grains lock onto nearby harmonics) |
| SWITCH 3 | Mode select | **UP** - Mode A (Drone)<br/>**MIDDLE** - Mode B (Granular Glitch — this mode)<br/>**DOWN** - Mode C (Freq Shift, not yet implemented — dry passthrough) |
| FOOTSWITCH 1 | Preset | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | Bypass / Save | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel |
| FS1 held 2 s | Bootloader | Enter DFU bootloader for flashing |

#### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed |

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
