# Agent Entry Point

This file provides guidance to AI agents when working with code in this repository.

This is a DIY digital bass effects pedal project built on the Electro-Smith Daisy Seed and Hothouse DSP kit.

## Read first, always

Before starting any work, agents must:

1. Read `docs/PROJECT.md` — top-level plan, hardware, staging timeline, multi-mode architecture.
2. Read any other docs relevant to the topic at hand (see "Read for specific tasks" below).
3. Read `agents-instructions.md` — hard rules for agent behavior, git, documentation, decision-making. **Non-negotiable.**

Do not jump into code or propose changes without first understanding the project context and the relevant spec.

## Read for specific tasks

**Working on DSP / Mode A (Bordun) code:**
- `docs/MODE_A_DRONE.md` — oscillator, envelope follower, ladder filter, controls, presets, compile-time constants.

**Working on pitch tracking:**
- `docs/PITCH_TRACKING.md` — research, algorithm comparison (zero-crossing vs YIN vs MPM), signal conditioning, implementation plan.

**Working on Mode B (Sprawl):**
- `docs/MODE_B_GRANULAR.md` — granular processor, signal chain, grain scheduler, gesture-reactive shaping, feedback bus, controls.

**Working on Mode C (Schism):**
- Discovery doc: `docs/MODE_C_DISCOVERY.md`. When implementation begins, this will be replaced by `docs/MODE_C.md` following MODE_A_DRONE.md structure.

**Working on the preset system:**
- `docs/PROJECT.md` § Preset System — behavioural spec.
- `docs/PRESET_IMPL.md` — as-built reference. Timing constants come from `docs/ux-demo.html`.

**Working on performance, CPU budget, or memory footprint:**
- `docs/DSP_INVENTORY.md` — per-block code size, CPU cost, memory footprint, per-mode budget estimates, and how to re-measure them.

**Working on repo layout, `core/` vs `pedals/`, or a new pedal:**
- `docs/ARCHITECTURE.md` — the platform/pedal/module seams and the block/module/mode/shell vocabulary.
- `docs/ChronoTron3/` — specs and plans for the ChronoTron3 bundle (in progress).

**Working on a ChronoTron3 module** (`pedals/chronotron3/modules/`) — spec = design intent, **as-built = what the code does (read first):**
- ***armitage*** (SW3 DOWN — impulse-synth / chord-detect resonator): **`docs/ChronoTron3/impulse resonator - armitage/ARMITAGE_AS_BUILT.md`** for detection / onset / excitation / portamento / voice — read before touching any of those. Spec + research + `saturation.py`/`validate.py` in the same folder.
- ***vestige*** (SW3 UP — granular looper/freeze): `docs/ChronoTron3/vestige-rework-plan.md` **§2.0** is the as-built multiband granular freeze; `dynamic-looper-concept.md` + `freeze-research.md`.
- ***mnemonic*** (SW3 MIDDLE — tape/BBD delay): `docs/ChronoTron3/mnemonic-concept.md` + `mnemonic-impl-plan.md`.
- Tuning values are never in the docs — they live in each module's `*_constants.h` (source of truth).

**Any other topic:** `docs/PROJECT.md` § Document Map indexes every doc in the
repo and marks each one current or historical. Check it before assuming a doc
doesn't exist — and add an entry there whenever you add a doc.

## Skills

Reusable task recipes live in `.agents/skills/`. Each subdirectory contains a `SKILL.md` describing the task, steps, and allowed tools.

- **build** — compile the firmware and report success/failure
- **commit-prep** — prepare a commit message and stage files
- **release** — cut a tagged GitHub release (build, stage artifacts, assemble `THIRD_PARTY_LICENSES.md`, draft release notes, tag, push, `gh release create`); gates irreversible steps on explicit user confirmation
- **tune** — view / edit compile-time DSP constants in `pedals/nitrotron3/constants.h` and rebuild
- **update-controls** — regenerate README control/LED tables from source

## Repository structure

```
NitroTron3/
├── src/core/                   # shared, pedal-agnostic library
│   ├── blocks/                 #   DSP building blocks (header-only) + clouds/
│   ├── util/                   #   knob mapping + math helpers
│   └── io/                     #   control_surface (Hothouse read layer)
├── pedals/<pedal>/             # one firmware target per pedal
│   ├── main.cpp                #   shell: init, mode routing, audio dispatch
│   ├── constants.h             #   compile-time config + INSTRUMENT profile
│   └── preset_system.h         #   (nitrotron3) pedal-level footswitch policy
├── docs/                       # specs, plans, research (see ARCHITECTURE.md)
├── .agents/skills/             # reusable agent task recipes
├── lib/HothouseExamples/       # submodule (libDaisy + DaisySP)
├── build/                      # compiled output (gitignored)
├── Makefile                    # build system (root); `make PEDAL=<name>`
├── README.md                   # user-facing docs + control tables
├── agents-instructions.md      # hard rules for agent behavior
├── AGENTS.md                   # this file
├── CLAUDE.md                   # Claude Code config (references this file)
└── LICENSE                     # GPL v3
```

**Convention:** Shared code lives in `src/core/`; per-pedal source lives in `pedals/<pedal>/`. Documentation and specs live in `docs/` — see `docs/ARCHITECTURE.md` for the multi-pedal layout. The Makefile stays at the project root.

## Build setup

- All dependencies live under `lib/HothouseExamples/` — a single git submodule that contains libDaisy and DaisySP as nested submodules.
- The Hothouse hardware proxy (`hothouse.h` / `hothouse.cpp`) is compiled from `lib/HothouseExamples/src/` — it is not copied into this repo.
- The Makefile references all libraries via `lib/HothouseExamples/` relative paths. No sibling-directory dependencies.
- After cloning, build libraries once: `make -C lib/HothouseExamples/libDaisy && make -C lib/HothouseExamples/DaisySP`.
- `make INSTRUMENT=guitar` builds a guitar-voiced variant (see the "Instrument profile" block in `pedals/nitrotron3/constants.h`). Default = bass. Profile switches rebuild automatically via a stamp file (`build/.buildprofile`).

## Hardware reference

- [Daisy Seed 65 MB](https://electro-smith.com/products/daisy-seed) — Electro-Smith, STM32H750, 480 MHz, 32-bit float, 96 kHz (we run at 48 kHz), 64 MB QSPI flash variant
- [Hothouse DSP Pedal Kit](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit) — Cleveland Music Co., 6 knobs, 3x 3-position toggles, 2 footswitches, LED, true-bypass relay, enclosure

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

Include separate tables for each mode if applicable. See `agents-instructions.md` for update rules.

## Staging

See `docs/PROJECT.md` for the full staged development timeline and current status.
