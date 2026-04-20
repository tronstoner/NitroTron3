# Claude Code Entry Point

This is a DIY digital bass effects pedal project built on the Electro-Smith Daisy Seed and Hothouse DSP kit.

## Read first, always

- `docs/PROJECT.md` — top-level plan, hardware, staging timeline, multi-mode architecture.

## Read for specific tasks

**Working on DSP / Mode A code:**
- `docs/MODE_A_DRONE.md` — oscillator, envelope follower, ladder filter, controls, presets, compile-time constants.

**Adjusting tuning mode, or helping the user dial in constants:**
- `docs/TUNING.md` — tuning-mode entry/exit, page layout, serial print format, dev flash slot, workflow.

**Working on pitch tracking:**
- `docs/PITCH_TRACKING.md` — research, algorithm comparison (zero-crossing vs YIN vs MPM), signal conditioning, implementation plan.

**Working on Mode B (Granular) or Mode C (Freq Shift):**
- Specs not yet written. If the user asks to start on these, first help spec them — create `docs/MODE_B_GRANULAR.md` or `docs/MODE_C_FREQSHIFT.md` following the structure of `docs/MODE_A_DRONE.md`.

## Repository structure

```
NitroTron3/
├── src/                        # all source code (.cpp, .h)
├── docs/                       # specs, plans, research
├── lib/HothouseExamples/       # submodule (libDaisy + DaisySP)
├── build/                      # compiled output (gitignored)
├── Makefile                    # build system (root)
├── README.md                   # user-facing docs + control tables
└── CLAUDE.md                   # this file
```

**Convention:** Source code lives in `src/`. Documentation and specs live in `docs/`. The Makefile stays at the project root. README.md and CLAUDE.md stay at root for discoverability.

## Git rules

- **Never `git commit` without explicit user confirmation.** Prepare the message, show what will be committed, and wait for the user to say "commit" or equivalent.
- **Never `git push`.** Only the user pushes.

## Project conventions

- All dependencies live under `lib/HothouseExamples/` — a single git submodule that contains libDaisy and DaisySP as nested submodules.
- The Hothouse hardware proxy (`hothouse.h` / `hothouse.cpp`) is compiled from `lib/HothouseExamples/src/` — it is not copied into this repo.
- The Makefile references all libraries via `lib/HothouseExamples/` relative paths. No sibling-directory dependencies.
- After cloning, the libraries must be built once: `make -C lib/HothouseExamples/libDaisy && make -C lib/HothouseExamples/DaisySP`.
- Compile-time constants for DSP parameters live in `src/constants.h` and are populated via the tuning-mode workflow described in `docs/TUNING.md`. Do not hand-edit values outside that workflow unless explicitly asked.
- Stages are incremental. When working on Stage N, assume stages 0 through N-1 are complete and tested. If the current state is unclear, ask the user which stage they are on.
- Tuning mode is part of the main binary, not a separate build. No `#ifdef DEV_MODE` guards around tuning code.
- DaisySP is the preferred DSP library. The Huovilainen ladder and parabolic oscillator shaper are implemented directly because DaisySP does not ship them.

## Hardware reference

- Daisy Seed (Electro-Smith, STM32, 480 MHz, 32-bit float, 96 kHz — we run at 48 kHz)
- Hothouse DSP Pedal Kit (6 knobs, 3× 3-position toggles, 2 footswitches, LED, true-bypass relay)
- Hammond 125B enclosure

## Controls documentation format

When documenting controls in the README, always use the full template listing every physical control, even if unused. This ensures the current state of the pedal is always clear at a glance. Template:

```
### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Unused |  |
| KNOB 2 | Unused |  |
| KNOB 3 | Unused |  |
| KNOB 4 | Unused |  |
| KNOB 5 | Unused |  |
| KNOB 6 | Unused |  |
| SWITCH 1 | Unused | **UP** - <br/>**MIDDLE** - <br/>**DOWN** -  |
| SWITCH 2 | Unused | **UP** - <br/>**MIDDLE** - <br/>**DOWN** -  |
| SWITCH 3 | Unused | **UP** - <br/>**MIDDLE** - <br/>**DOWN** -  |
| FOOTSWITCH 1 | Unused |  |
| FOOTSWITCH 2 | Bypass | The bypassed signal is buffered |
```

**MANDATORY: After every code change that touches controls, knobs, switches, footswitches, or LEDs, update the README controls and LEDs tables before considering the task complete.** This is not optional — the README is the only user-facing reference for what the pedal does. Include separate tables for each mode if applicable.

## Staging (summary — detail in `docs/PROJECT.md`)

0. Hardware bring-up + passthrough
1. Oscillator + tuning mode scaffold
2. Ladder filter
3. Envelope follower + VCA + **full re-tune pass**
4. Normal-mode UX wiring
5. Preset system
6. Multi-mode scaffold
7. Enclosure
