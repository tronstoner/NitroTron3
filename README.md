# NitroTron3

A DIY digital bass effects pedal built on the [Electro-Smith Daisy Seed](https://electro-smith.com/products/daisy-seed) and [Cleveland Music Co. Hothouse DSP kit](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit). Licensed under [GPL v3](LICENSE).

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
│   ├── phaser.h                # 4-stage allpass phaser (Mode C SW2=DOWN)
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
│   ├── TUNING.md               # tuning-mode spec
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

## Current state — Mode A complete, Mode B fleshed out, Mode C in late stages

**Preset system (global model).** One global edit buffer (knobs + sw1 + sw2 + mode), 3 banks × 8 slots (storage sized for 6), each slot carries its own mode. Dirty tracking, debounced 2 s flash persistence (no-change → no write), v2 → v3 migration preserves prior per-mode presets. FS1 cycles slots within the active bank; FS1+FS2 short tap cycles banks (Roman-numeral burst on both LEDs); FS2 toggles bypass / enters save mode; FS1+FS2 held 2 s enters DFU. SW3 is a soft control treated uniformly with SW1/SW2.

**Mode A — Bordun.** PolyBLEP oscillator → Huovilainen ladder → env-VCA → mix.

**Mode B — Sprawl.** Granular: 8 s SDRAM ring buffer → 8 grain voices → texture shaper (SW1 chooses decimator/fold, glitch zones, or ringmod) → wet HPF 120 Hz → tanh-saturated feedback loop with build-up + on-play duckers → optional Clouds reverb (K5 bipolar against feedback). Pitch-tracked harmony (SW2): fixed interval, resonance window pick, or Bode SSB frequency shifter (SW2 DOWN, K1 = ±1 kHz exponential, inside the feedback loop). K2 fully CCW enters direct-texture mode with micro-stutter.

**Mode C — Schism.** Two-stage drive → filter chain. SW1 selects drive (sine wavefolder / gated bit crusher / pitch-tracked synth osc); SW2 selects filter (Moog ladder v2, Grendel formant, phaser). K5 is a bipolar pre-filter drive. Asymmetric env smoothers for ladder/formant. Post-filter 2-band peak limiter + amp-env VCA so self-resonance doesn't ring alone.

LED 1 shows preset via Roman-numeral blink; LED 2 shows active/bypass/dirty/save; both LEDs play a Roman-numeral burst on bank change.

### Mode A — Bordun

A harmonic companion to the bass, modelled after a specific usage of the Moog MoogerFooger FreqBox (MF-102) — its envelope-gated oscillator mixed in alongside the dry signal, kept clean of sync and FM modulation. An internally generated oscillator, gated and shaped by the bass's own envelope, lays subtle or assertive accompanying harmonics over the input — pure intervals, fifths, octaves, drone-like wash. Placed **before** overdrive in the chain it stacks musically into a saturated sound; on its own it sits as a parallel voice along the played notes. Tracking modes lock the harmony to the played pitch; fixed mode anchors a drone against which the bass moves.

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
| SWITCH 3 | Mode select | **UP** - Mode A (Bordun)<br/>**MIDDLE** - Mode B (Sprawl)<br/>**DOWN** - Mode C (Schism) |
| FOOTSWITCH 1 | Preset | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | Bypass / Save | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel |
| FS1+FS2 short tap | Bank cycle | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank. |
| FS1+FS2 held 2 s | Bootloader | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset) |

#### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed |

### Mode B — Sprawl

Granular-delay-based texture and soundscape engine. A rolling buffer feeds a grain scheduler whose voices can pitch-shift non-linearly, drift, scatter, and stutter — built to fill the void around the bass, intended for improvisation and experimental performance. Functionally a multi-mode effect on its own: all colouring and texturing stages (decimator/fold, event-driven glitch, ringmod, frequency shifter) are reachable in a non-delay path too (K2 fully CCW). High feedback with the tanh saturator pushes the loop into harmonic cloud blooms; the Bode SSB shifter inside the feedback loop cascades each pass and rapidly grows beyond pitched material.

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

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Harmony / shift | Meaning follows SW2. **SW2=UP**: fixed interval, K1 = ±12 semitones. **SW2=MID**: resonance pick, K1 spans the ±36-semitone scan. **SW2=DOWN**: Bode SSB frequency shifter on the wet bus, bipolar K1 with ±2% deadzone — CCW = down-shift (more bass), CW = up-shift, exponential taper, ±1 kHz at full deflection. In SW2 DOWN the grain buffer-read pitch is forced to unison so K1 isn't doing two jobs |
| KNOB 2 | Buffer range | CCW = tight (100 ms). CW = deep (full 8 s). **Fully CCW**: enters direct-texture mode — grain engine bypassed, input routes straight to texture shaper |
| KNOB 3 | Character / Glitch | **Grain mode**: CCW = soft/long/tight, CW = short/sharp/chaotic. **Direct-texture mode** (K2 fully CCW): micro-stutter — CCW = clean, CW = frequent choppy repeats |
| KNOB 4 | Texture amount | Depends on SW1 position. See Switch 1 notes |
| KNOB 5 | Reverb / Feedback (bipolar) | **CCW** = Clouds reverb amount (0→1). **Center (±5%)** = off. **CW** = ring-buffer feedback (0→0.95). Reverb tail does not feed the ring buffer |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet. Equal-power curve |
| SWITCH 1 | Texture mode | **UP** - Decimator/Wavefolder bipolar (K4 CCW = max crush, noon = clean, CW = wavefold)<br/>**MIDDLE** - Event-driven digital glitch (bipolar K4: noon = clean, CCW = random bit-flip events, CW = random timing events {freeze / stutter / reverse}; sparse near deadzone → continuous at extremes via event chaining)<br/>**DOWN** - Ringmod (K4 CCW–30% = tremolo 1–15 Hz, 30%–CW = bell partials, pitch-tracked with keytracked LPF) |
| SWITCH 2 | Harmony / shift | **UP** - Fixed interval (K1 = ±12 semitones above tracked note)<br/>**MIDDLE** - Resonance (grains lock onto nearby harmonics; K1 spans ±36 semi scan)<br/>**DOWN** - Bode SSB frequency shifter on the wet bus (inside the feedback loop, so each pass cascades the shift). Grain buffer-read pitch forced to unison; K1 = ±1 kHz exponential, CCW = down, CW = up |
| SWITCH 3 | Mode select | **UP** - Mode A (Bordun)<br/>**MIDDLE** - Mode B (Sprawl — this mode)<br/>**DOWN** - Mode C (Schism) |
| FOOTSWITCH 1 | Preset | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | Bypass / Save | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel |
| FS1+FS2 short tap | Bank cycle | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank. |
| FS1+FS2 held 2 s | Bootloader | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset) |

#### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8 (I=short, V=long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows target slot. |
| LED 2 (right) | State indicator: solid = active, off = bypassed, rapid flash = dirty (preset edited), fast blink = save mode, burst = save confirmed |

### Mode C — Schism (C.6 in progress)

Dynamic bass filter and digital distortion unit, with a fat pitch-tracked synth voice as a third drive option. The filter can self-oscillate in a controlled manner — singing-into-screaming textures that play well into a downstream overdrive or fuzz. The bit-XOR drive enriches overtones cleanly and lights up especially well placed **before** overdrive/fuzz. The signal-chain philosophy of this pedal is: **after** whammy and octavers, **before** overdrives and distortion. The phaser sub-mode is provisional and likely to be replaced with a different effect once a fitting one is found.

Discovery in progress (`docs/MODE_C_DISCOVERY.md`). C.1–C.5 implement the wet/dry mix scaffold, the SW1=UP sine wavefolder, the SW2=UP Moog ladder (tuned `MoogLadderV2`: single input saturator, k-cutoff cross-comp, level comp, asymmetric bias) with env-to-cutoff, the SW2=DOWN Plague filter, and the SW2=MID Grendel formant filter (4 parallel BPFs along a curated oo → oh → ah → eh → ee vowel path, CCW = dark/closed → CW = bright/open). **SW1=MID is a gated bit crusher** (K4 = bit depth from 16 → 4 bits, input-envelope gate so silent input → silent output) — explorative distortion variant. **SW1=DOWN is a pitch-tracked synth oscillator**: YIN-tracked semitone-quantized bass pitch drives an oscillator engine whose timbre K4 morphs — saw on the CCW half (7-voice hypersaw at full CCW with 50-cent detune, collapsing to a single-saw sweet-spot plateau just below noon), rect on the CW half (single rect just past noon, ramping into PWM with depth-then-rate at full CW). The synth output is amplitude-controlled by the raw env follower (Mode A-style direct VCA) **before** the SW2 filter, so the filter colors the gated synth instead of the dry bass. Post-filter chain: slight pre-limit lift (×1.3), 2-band peak limiter (LF below ~160 Hz preserved so fundamentals don't duck) with "warmth-when-working" bloom, then an amp-env VCA that gates the wet path so self-resonance doesn't ring alone. Per-filter input pads put Moog and Grendel in their clean sweet zone at K5 noon; Plague is unpadded (needs hot input to fold). Ladder + Plague env modulation reads a 30 ms / 150 ms asymmetric A/R smoother (less twitchy than the VCA gate); Grendel uses its own slower 400 ms swell smoother (slow attack, instant snap-back) so vowel/size sweeps breathe rather than snap.

#### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Filter "where" | SW2=UP: Moog cutoff (80 Hz – 8 kHz, exponential). SW2=MID: Grendel vowel path (CCW = oo dark/closed, CW = ee bright/open). SW2=DOWN: Plague input balance (CCW = lo only, CW = hi only) |
| KNOB 2 | Filter "how much" | SW2=UP: Moog resonance (0 → self-osc, sqrt curve so the lower half is audible). SW2=MID: Grendel size (mouth scale, ×0.5 → ×1.6) — also breathed by env (±20% at full K3, coupled to K3 sign). SW2=DOWN: Plague intensity (tandem input drive + feedback drive) |
| KNOB 3 | Env → filter amount | Bipolar with center deadzone (±5%). SW2=UP: opens/closes Moog cutoff (linear lift, passive-bass scaled). SW2=MID: pushes vowel path and size — CCW = opens brighter (toward ee) + mouth tightens; CW = closes darker (toward oo) + mouth opens. SW2=DOWN: shifts Plague input balance. Center = static |
| KNOB 4 | Drive character | SW1=UP: sine wavefold amount (0 = clean, 1 = max fold, internal loudness compensation). SW1=MID: bit-crush amount (CCW = 16-bit clean, CW = 4-bit gnarly, env-gated). SW1=DOWN: synth-osc timbre morph — CCW half is the saw side (K4=0 = 7-voice hypersaw at 50-cent detune, K4 ∈ [0.46, 0.50] = pure single-saw sweet-spot plateau), CW half is the rect side (K4 just past 0.50 = pure single rect, K4=1.0 = max PWM with ±0.3 duty deviation at 2 Hz). Within each half, modulation depth ramps in across the first ~40% of travel from noon, then continued travel only widens the modulation (detune / LFO rate) |
| KNOB 5 | Filter drive (bipolar) | **CCW** = attenuate (~−12 dB at full CCW). **Noon** = unity. **CW** = boost up to 8× hot. Sets the Moog ladder's input drive; pre-tanh in front of Grendel and Plague. Moog/Grendel have a fixed ×0.3 internal pad so K5 noon lands in their clean zone; Plague is unpadded |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet. Equal-power curve |
| SWITCH 1 | Drive | **UP** - Sine wavefolder (K4 = fold amount)<br/>**MIDDLE** - Gated bit crusher (K4 = bit depth 16 → 4, env-gated)<br/>**DOWN** - Pitch-tracked synth oscillator (K4 = saw ↔ rect timbre morph, raw-env VCA pre-filter) |
| SWITCH 2 | Filter | **UP** - Moog ladder (K1 cutoff, K2 resonance, K3 env)<br/>**MIDDLE** - Grendel formant (K1 vowel path, K2 size, K3 env on path)<br/>**DOWN** - Plague (K1 input balance, K2 intensity, K3 env on balance) |
| SWITCH 3 | Mode select | **UP** - Mode A (Bordun)<br/>**MIDDLE** - Mode B (Sprawl)<br/>**DOWN** - Mode C (Schism — this mode) |
| FOOTSWITCH 1 | Preset | **Short press**: cycle Manual→1→…→8→Manual (or reload preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | Bypass / Save | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode (or confirm save if already in save mode). **Short press in save mode**: cancel |
| FS1+FS2 short tap | Bank cycle | Cycle active bank (1 → 2 → 3 → 1). Both LEDs play a Roman-numeral burst confirming the new bank. Also works in save mode to retarget a save into another bank. |
| FS1+FS2 held 2 s | Bootloader | Enter DFU bootloader for flashing (both LEDs alternate for 1.2 s before reset) |

#### LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | Preset indicator: off = Manual, Roman numeral blink pattern for presets 1–8. In save mode, shows target slot. |
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
