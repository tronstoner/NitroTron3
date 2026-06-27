# NitroTron3

A DIY digital bass effects pedal built on the [Electro-Smith Daisy Seed](https://electro-smith.com/products/daisy-seed) and [Cleveland Music Co. Hothouse DSP kit](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit). Licensed under [GPL v3](LICENSE).

## Overview

NitroTron3 is a project of [Nitro Mahalia](https://nitromahalia.net) — This pedal packages several of their signature bass-through-synth sounds into a single bass-specific unit and more: three distinct modes, each doing something not easily found in off-the-shelf pedals.

**BORDUN (Mode A).** A harmonic companion to the bass, modelled after a specific usage of the Moog MoogerFooger FreqBox (MF-107) — its envelope-gated oscillator mixed in alongside the dry signal, kept clean of sync and FM modulation. An internally generated oscillator, gated and shaped by the bass's own envelope, lays subtle or assertive accompanying harmonics over the input — pure intervals, fifths, octaves, drone-like wash. Placed **before** overdrive in the chain it stacks musically into a saturated sound; on its own it sits as a parallel voice along the played notes. Tracking modes lock the harmony to the played pitch; fixed mode anchors a drone against which the bass moves.

**SPRAWL (Mode B).** Granular-delay-based texture and soundscape engine. A rolling buffer feeds a grain scheduler whose voices can pitch-shift non-linearly, drift, scatter, and stutter — built to fill the void around the bass, intended for improvisation and experimental performance. Functionally a multi-mode effect on its own: all colouring and texturing stages (decimator/fold, event-driven glitch, ringmod, frequency shifter) are reachable in a non-delay path too (K2 fully CCW). High feedback with the tanh saturator pushes the loop into harmonic cloud blooms; the Bode SSB shifter inside the feedback loop cascades each pass and rapidly grows beyond pitched material.

**SCHISM (Mode C).** Dynamic bass filter and digital distortion unit, with a fat pitch-tracked synth voice as a third drive option. The filter can self-oscillate in a controlled manner — singing-into-screaming textures that play well into a downstream overdrive or fuzz. The bit-XOR drive enriches overtones cleanly and lights up especially well placed **before** overdrive/fuzz. The phaser sub-mode is provisional and likely to be replaced with a different effect once a fitting one is found.

**Presets.** A global preset system recalls mode + full parameter state in one footswitch press. One global edit buffer, 3 banks × 8 slots = 24 reachable presets. Each slot carries its own mode, so cycling presets can swap mode mid-set. FS1 cycles slots within the active bank; FS1+FS2 short tap cycles banks (Roman-numeral burst on both LEDs); FS2 long enters save mode (banks can be cycled inside save mode for cross-bank saves); FS1+FS2 held 2 s enters DFU. State persists across power cycles via debounced 2 s flash auto-save. The existing per-mode preset layout (older firmware) is migrated, not wiped.

## Get the firmware

Pre-built binaries (`.bin` for DFU, `.hex` for ST-Link / Daisy Web Programmer), the user manual PDF, and the licence bundle are attached to every tagged release: [github.com/tronstoner/NitroTron3/releases](https://github.com/tronstoner/NitroTron3/releases). Flashing steps live in [`INSTALL.md`](INSTALL.md) — short version: hold both footswitches on the pedal for 2 s to enter DFU, then `dfu-util -a 0 -s 0x08000000:leave -D NitroTron3-vX.Y.bin`. (Other Hothouse pedals use FS1 alone — not this one.)

Build from source instead if you want to modify the firmware — see *Repository setup* and *Getting started* below.

## Hardware

- [Daisy Seed 65 MB](https://electro-smith.com/products/daisy-seed) (STM32H750, 480 MHz Cortex-M7, 32-bit float, 48 kHz audio; 64 MB QSPI flash variant)
- [Hothouse DSP Pedal Kit](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit) (6 knobs, 3x 3-position toggles, 2 footswitches, LED, true-bypass relay, enclosure)

## Repository setup

This repo uses [HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) as a single git submodule under `lib/`. That submodule in turn contains libDaisy and DaisySP as nested submodules, so one checkout gets the full toolchain.

```
NitroTron3/
├── src/                        # source code
│   ├── NitroTron3.cpp          # main application
│   ├── constants.h             # compile-time DSP constants
│   ├── preset_system.h         # global preset bank, banks, flash, LED engines
│   │
│   ├── moog_osc.h              # parabolic waveshaper + PolyBLEP (Mode A)
│   ├── moog_ladder.h           # Huovilainen Moog ladder (Mode A)
│   ├── moog_ladder_v2.h        # tuned ladder for Mode C SW2=UP
│   ├── env_follower.h          # shared Moog-topology env follower
│   ├── pitch_tracker.h         # YIN pitch tracker (4x decimation)
│   │
│   ├── ring_buffer.h           # 8 s SDRAM ring (Mode B)
│   ├── grain_voice.h           # windowed grain voice (Mode B)
│   ├── freq_shifter.h          # Bode SSB frequency shifter (Mode B SW2=DOWN)
│   ├── glitch_zones.h          # event-driven digital glitch (Mode B SW1=MID)
│   ├── resampler.h             # 48 ↔ 32 kHz polyphase (reverb path)
│   ├── clouds/                 # vendored MI Clouds reverb (MIT)
│   │
│   ├── grendel.h               # 4-BPF formant filter (Mode C SW2=MID)
│   ├── phaser.h                # 6-stage allpass phaser (Mode C SW2=DOWN)
│   ├── synth_osc_c.h           # hypersaw / square+PWM osc (Mode C SW1=DOWN)
│   ├── bitcrush.h              # gated bit crusher (Mode C SW1=MID)
│   └── peak_limiter.h          # 2-band soft limiter (Mode C post-filter)
│
├── docs/                       # specs, plans, research
│   ├── PROJECT.md              # top-level plan, staging, architecture
│   ├── MODE_A_DRONE.md         # Mode A full spec
│   ├── MODE_B_GRANULAR.md      # Mode B full spec
│   ├── MODE_B_IMPL.md          # Mode B implementation notes
│   ├── MODE_B_REVERB_TASK.md   # Mode B reverb integration notes
│   ├── MODE_B_TEXTURE_IDEAS.md # Mode B texture-shaper exploration
│   ├── MODE_C_DISCOVERY.md     # Mode C discovery doc
│   ├── MODE_C_PHASER_PLAN.md   # Mode C phaser design
│   ├── PRESET_IMPL.md          # preset system as-built reference
│   ├── PRESET_GLOBAL_PLAN.md   # preset system planning history
│   ├── PITCH_TRACKING.md       # pitch tracking research + plan
│   ├── STUTTER_SPEC.md         # direct-texture stutter spec
│   ├── STUTTER_IMPLEMENTATION_NOTES.md
│   ├── USER_MANUAL.md          # end-user manual (with PDF render)
│   ├── ux-demo.html            # interactive preset / bank UX demo
│   ├── mode-b-engines.html     # interactive Mode B engine exploration
│   └── pedal-mode-{a,b,c}.svg  # per-mode pedal layout diagrams
│
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

## Modes in detail

### BORDUN (Mode A)

PolyBLEP oscillator (waveform via SW1) → Huovilainen Moog ladder → env-controlled VCA → equal-power mix with the dry. SW2 selects the pitch source: fixed (K1 semitones, K2 octave), octave-locked tracking (pitch class follows the bass within K2's octave, K1 adds an interval), or direct tracking (osc follows the bass exactly, K1/K2 are relative offsets). K5 detunes a second oscillator against the first for beating. K4 sweeps ladder cutoff in SAW/SQR; in TRI it morphs cutoff → wavefolder past noon.

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

| CONTROL           | DESCRIPTION         | NOTES                                                                                                                                                                                                                                                               |
| ----------------- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| KNOB 1            | Semitone / Interval | ±12 semitone offset, center dead zone. **Fixed**: center = A, also sets wrap point for tracking. **Track**: center = tracked note, wraps at the note set in fixed mode                                                                                              |
| KNOB 2            | Octave              | 7 positions (C-1–C5). **Octave-locked**: sets target octave. **Direct**: ±3 octave offset from tracked pitch                                                                                                                                                        |
| KNOB 3            | Fine tune           | ±50 cents continuous (osc1 only — creates beating with osc2)                                                                                                                                                                                                        |
| KNOB 4            | Tone / Wavefold     | SAW/SQ: full range ladder cutoff (80 Hz–8 kHz). TRI: CCW→noon = cutoff, noon→CW = wavefolding (filter stays fully open)                                                                                                                                             |
| KNOB 5            | Osc 2 detune        | Center = off (dead zone). Outside center = ±1–12 semitone steps. Not affected by fine tune                                                                                                                                                                          |
| KNOB 6            | Mix                 | 0 = full dry, 1 = full wet (oscillator)                                                                                                                                                                                                                             |
| SWITCH 1          | Waveform            | **UP** - Saw<br/>**MIDDLE** - Triangle<br/>**DOWN** - Square                                                                                                                                                                                                        |
| SWITCH 2          | Drone mode          | **UP** - Fixed pitch (K1 sets note, K2 sets octave)<br/>**MIDDLE** - Octave-locked tracking (pitch class follows bass in K2's octave, K1 adds interval)<br/>**DOWN** - Direct tracking (osc follows exact bass pitch, K1/K2 are relative offsets ±12 semi / ±3 oct) |
| SWITCH 3          | Mode select         | **UP** - BORDUN (Mode A)<br/>**MIDDLE** - SPRAWL (Mode B)<br/>**DOWN** - SCHISM (Mode C)                                                                                                                                                                            |
| FOOTSWITCH 1      | Preset              | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual                                                                                                                                                     |
| FOOTSWITCH 2      | Bypass / Save       | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel                                                                                                            |
| FS1+FS2 short tap | Bank cycle          | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank.                                                                                                      |
| FS1+FS2 held 2 s  | Bootloader          | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset)                                                                                                                                                                                      |

#### LEDs

| LED           | DESCRIPTION                                                                                                                                                       |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| LED 1 (left)  | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed                              |

### SPRAWL (Mode B)

8 s SDRAM ring buffer feeding 8 grain voices (Hann-windowed) → texture shaper (SW1: decimator/fold, event-driven glitch zones, or ringmod) → wet HPF 120 Hz → optional Bode SSB shifter (SW2=DOWN) → tanh-saturated feedback loop with build-up + on-play duckers → optional Clouds reverb on K5 CCW (mutually exclusive with feedback on K5 CW). SW2 picks the harmony source: fixed interval, resonance window pick, or frequency shifter. K2 fully CCW bypasses the grain engine and routes the dry directly into the texture shaper for a non-delay path with micro-stutter on K3.

#### Signal Chain

```
Input ──┬──────────────────────────────────────────────────► [Mix K6] ──► Output
        │                                                       ▲
        ├──► [EnvFollower] ──► grain amplitude                  │
        ├──► [PitchTracker] ──► harmony logic                   │
        │                                                       │
        └──► [Ring Buffer, 8s SDRAM] ──► [Grain Scheduler]
                  ▲                            │
                  │                      [Grain Voices × 8]
                  │                            │
                  │                      [Texture Shaper (SW1)]
                  │                            │
                  │                      [Freq Shifter (SW2=DOWN only)]
                  │                            │
                  │                      [Wet HPF 120 Hz]
                  │                            │
                  │                            ├───► [Clouds Reverb @ 32 kHz] (K5 CCW)
                  │                            │
                  │   [Build-up ducker] ◄──────┤
                  │   [On-play ducker] ◄── EnvFollower
                  │            │               │
                  └── [tanh saturator × K5 CW feedback] ◄────────┘
```

#### Controls

| CONTROL           | DESCRIPTION                 | NOTES                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ----------------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| KNOB 1            | Harmony / shift             | Meaning follows SW2. **SW2=UP**: fixed interval, K1 = ±12 semitones. **SW2=MID**: resonance pick, K1 spans the ±36-semitone scan. **SW2=DOWN**: Bode SSB frequency shifter on the wet bus, bipolar K1 with ±2% deadzone — CCW = down-shift (more bass), CW = up-shift, exponential taper, ±1 kHz at full deflection. In SW2 DOWN the grain buffer-read pitch is forced to unison so K1 isn't doing two jobs                              |
| KNOB 2            | Buffer range                | CCW = tight (100 ms). CW = deep (full 8 s). **Fully CCW**: enters direct-texture mode — grain engine bypassed, input routes straight to texture shaper                                                                                                                                                                                                                                                                                   |
| KNOB 3            | Character / Glitch          | **Grain mode**: CCW = soft/long/tight, CW = short/sharp/chaotic. **Direct-texture mode** (K2 fully CCW): micro-stutter — CCW = clean, CW = frequent choppy repeats                                                                                                                                                                                                                                                                       |
| KNOB 4            | Texture amount              | Depends on SW1 position. See Switch 1 notes                                                                                                                                                                                                                                                                                                                                                                                              |
| KNOB 5            | Reverb / Feedback (bipolar) | **CCW** = Clouds reverb amount (0→1). **Center (±5%)** = off. **CW** = ring-buffer feedback (0→0.95). Reverb tail does not feed the ring buffer                                                                                                                                                                                                                                                                                          |
| KNOB 6            | Mix                         | 0 = full dry, 1 = full wet. Equal-power curve                                                                                                                                                                                                                                                                                                                                                                                            |
| SWITCH 1          | Texture mode                | **UP** - Decimator/Wavefolder bipolar (K4 CCW = max crush, noon = clean, CW = wavefold)<br/>**MIDDLE** - Event-driven digital glitch (bipolar K4: noon = clean, CCW = random bit-flip events, CW = random timing events {freeze / stutter / reverse}; sparse near deadzone → continuous at extremes via event chaining)<br/>**DOWN** - Ringmod (K4 CCW–30% = tremolo 1–15 Hz, 30%–CW = bell partials, pitch-tracked with keytracked LPF) |
| SWITCH 2          | Harmony / shift             | **UP** - Fixed interval (K1 = ±12 semitones above tracked note)<br/>**MIDDLE** - Resonance (grains lock onto nearby harmonics; K1 spans ±36 semi scan)<br/>**DOWN** - Bode SSB frequency shifter on the wet bus (inside the feedback loop, so each pass cascades the shift). Grain buffer-read pitch forced to unison; K1 = ±1 kHz exponential, CCW = down, CW = up                                                                      |
| SWITCH 3          | Mode select                 | **UP** - BORDUN (Mode A)<br/>**MIDDLE** - SPRAWL (Mode B — this mode)<br/>**DOWN** - SCHISM (Mode C)                                                                                                                                                                                                                                                                                                                                     |
| FOOTSWITCH 1      | Preset                      | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual                                                                                                                                                                                                                                                                                                                          |
| FOOTSWITCH 2      | Bypass / Save               | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel                                                                                                                                                                                                                                                                                 |
| FS1+FS2 short tap | Bank cycle                  | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank.                                                                                                                                                                                                                                                                           |
| FS1+FS2 held 2 s  | Bootloader                  | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset)                                                                                                                                                                                                                                                                                                                                                           |

#### LEDs

| LED           | DESCRIPTION                                                                                                                                                       |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| LED 1 (left)  | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed                              |

### SCHISM (Mode C — C.6 in progress)

Two-stage chain: drive (SW1) → filter (SW2). SW1 picks the drive, with K4 bipolar around noon (noon = clean dry) for the UP and MIDDLE positions: **UP** = sine wavefolder (K4 CW) / Chebyshev waveshaper (K4 CCW — octave-up / metallic harmonic generator, pre-shaper low-pass for a clean octave); **MIDDLE** = gated bit-flipper (K4 CW — XOR a chosen bit, env-gated, per-bit loudness comp) / tanh overdrive (K4 CCW); **DOWN** = pitch-tracked synth oscillator (YIN-tracked semitone-quantized bass pitch drives a hypersaw → single saw → single rect → PWM morph along K4, env-VCA'd before the filter). SW2 picks the filter: Moog ladder v2 (tuned for self-osc, with K5-driven audio-rate cutoff self-FM), Grendel 4-BPF formant (vowel path oo → ee on K1), or phaser (provisional). K1/K2/K3 are filter cutoff/depth/env-mod; K5 is a bipolar pre-filter drive (CCW attenuate, noon unity, CW up to 8×). Asymmetric env smoothers per filter shape K3-direction (peak-follower CW, slow-swell CCW); K3 runs through a response curve so the sweet spots near noon get more travel. Post-filter: small pre-limit lift, 2-band peak limiter (LF preserved so fundamentals don't duck). Full status in `docs/MODE_C_DISCOVERY.md`.

#### Controls

| CONTROL           | DESCRIPTION            | NOTES                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ----------------- | ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| KNOB 1            | Filter "where"         | SW2=UP: Moog cutoff (20 Hz – 8 kHz, exponential). SW2=MID: Grendel vowel path (CCW = oo dark/closed, CW = ee bright/open). SW2=DOWN: phaser notch centre                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| KNOB 2            | Filter "how much"      | SW2=UP: Moog resonance (0 → self-osc, sqrt curve so the lower half is audible). SW2=MID: Grendel size (mouth scale, ×0.5 → ×1.6) — also breathed by env (±20% at full K3, coupled to K3 sign). SW2=DOWN: phaser feedback (clean sweep → resonant bloom → controlled self-oscillation at full CW)                                                                                                                                                                                                                                                                                                                       |
| KNOB 3            | Env / LFO modulation   | Bipolar with center deadzone (±5%); runs through a response curve (fine near noon, coarse toward the extremes). SW2=UP: opens/closes Moog cutoff (linear lift, passive-bass scaled). SW2=MID: pushes vowel path and size — CCW = opens brighter (toward ee) + mouth tightens; CW = closes darker (toward oo) + mouth opens. SW2=DOWN: bipolar phaser LFO rate (sign selects shape — CCW triangle, CW sample-and-hold; magnitude = rate; centre = LFO off, static notch at K1)                                                                                                                                            |
| KNOB 4            | Drive character        | Bipolar around noon (noon = clean dry) for SW1=UP and SW1=MID. SW1=UP: CW = sine wavefold (0 → max, internal loudness comp), CCW = Chebyshev waveshaper (octave-up / metallic harmonic generator, pre-shaper low-pass for a clean octave). SW1=MID: CW = gated bit-flipper (XOR bit position, env-gated, per-bit loudness comp), CCW = tanh overdrive (voicing provisional — still being ear-tuned). SW1=DOWN: synth-osc timbre morph (full-range, no noon split) — CCW half = saw (K4=0 = 7-voice hypersaw at 35-cent detune, K4 ∈ [0.46, 0.50] = pure single-saw sweet-spot plateau), CW half = rect (K4 just past 0.50 = pure single rect, K4=1.0 = max PWM with ±0.4 duty deviation at 2 Hz). Within each half, modulation depth ramps in across the first ~40% of travel from noon, then continued travel only widens the modulation (detune / LFO rate) |
| KNOB 5            | Filter drive (bipolar) | **CCW** = attenuate (~−12 dB at full CCW). **Noon** = unity. **CW** = boost up to 8× hot. Sets the Moog ladder's input drive; pre-tanh in front of Grendel and the phaser. Moog/Grendel/phaser have a fixed ×0.3 internal pad so K5 noon lands in their clean zone. On the Moog ladder, K5 from a touch before noon up to full CW also fades in audio-rate cutoff self-FM (modulated by the filter input) for a gritty/vocal resonance                                                                                                                                                                                   |
| KNOB 6            | Mix                    | 0 = full dry, 1 = full wet. Equal-power curve                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| SWITCH 1          | Drive                  | **UP** - Sine wavefolder (K4 CW) / Chebyshev waveshaper (K4 CCW), noon = clean<br/>**MIDDLE** - Gated bit-flipper (K4 CW, env-gated) / tanh overdrive (K4 CCW), noon = clean<br/>**DOWN** - Pitch-tracked synth oscillator (K4 = saw ↔ rect timbre morph, raw-env VCA pre-filter)                                                                                                                                                                                                                                                                                                                                       |
| SWITCH 2          | Filter                 | **UP** - Moog ladder (K1 cutoff, K2 resonance, K3 env)<br/>**MIDDLE** - Grendel formant (K1 vowel path, K2 size, K3 env on path)<br/>**DOWN** - Phaser (K1 notch centre, K2 feedback, K3 LFO rate/shape)                                                                                                                                                                                                                                                                                                                                                                                                               |
| SWITCH 3          | Mode select            | **UP** - BORDUN (Mode A)<br/>**MIDDLE** - SPRAWL (Mode B)<br/>**DOWN** - SCHISM (Mode C — this mode)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| FOOTSWITCH 1      | Preset                 | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| FOOTSWITCH 2      | Bypass / Save          | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| FS1+FS2 short tap | Bank cycle             | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank.                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| FS1+FS2 held 2 s  | Bootloader             | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |

#### LEDs

| LED           | DESCRIPTION                                                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| LED 1 (left)  | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8. In save mode, shows target slot.                        |
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
