# Claude Code Entry Point

This is a DIY digital bass effects pedal project built on the Electro-Smith Daisy Seed and Hothouse DSP kit.

## Read first, always

- `PROJECT.md` — top-level plan, hardware, staging timeline, multi-mode architecture.

## Read for specific tasks

**Working on DSP / Mode A code:**
- `MODE_A_DRONE.md` — oscillator, envelope follower, ladder filter, controls, presets, compile-time constants.

**Adjusting tuning mode, or helping the user dial in constants:**
- `TUNING.md` — tuning-mode entry/exit, page layout, serial print format, dev flash slot, workflow.

**Working on Mode B (Granular) or Mode C (Freq Shift):**
- Specs not yet written. If the user asks to start on these, first help spec them — create `MODE_B_GRANULAR.md` or `MODE_C_FREQSHIFT.md` following the structure of `MODE_A_DRONE.md`.

## Repository structure

- All dependencies live under `lib/HothouseExamples/` — a single git submodule that contains libDaisy and DaisySP as nested submodules.
- The Hothouse hardware proxy (`hothouse.h` / `hothouse.cpp`) is compiled from `lib/HothouseExamples/src/` — it is not copied into this repo.
- The Makefile references all libraries via `lib/HothouseExamples/` relative paths. No sibling-directory dependencies.
- After cloning, the libraries must be built once: `make -C lib/HothouseExamples/libDaisy && make -C lib/HothouseExamples/DaisySP`.

## Project conventions

- Compile-time constants for DSP parameters live in `constants.h` and are populated via the tuning-mode workflow described in `TUNING.md`. Do not hand-edit values outside that workflow unless explicitly asked.
- Stages are incremental. When working on Stage N, assume stages 0 through N-1 are complete and tested. If the current state is unclear, ask the user which stage they are on.
- Tuning mode is part of the main binary, not a separate build. No `#ifdef DEV_MODE` guards around tuning code.
- DaisySP is the preferred DSP library. The Huovilainen ladder and parabolic oscillator shaper are implemented directly because DaisySP does not ship them.

## Hardware reference

- Daisy Seed (Electro-Smith, STM32, 480 MHz, 32-bit float, 96 kHz — we run at 48 kHz)
- Hothouse DSP Pedal Kit (6 knobs, 3× 3-position toggles, 2 footswitches, LED, true-bypass relay)
- Hammond 125B enclosure

## Staging (summary — detail in `PROJECT.md`)

0. Hardware bring-up + passthrough
1. Oscillator + tuning mode scaffold
2. Ladder filter
3. Envelope follower + VCA + **full re-tune pass**
4. Normal-mode UX wiring
5. Preset system
6. Multi-mode scaffold
7. Enclosure
