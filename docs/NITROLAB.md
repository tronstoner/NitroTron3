# NitroLab — the test-bench pedal

NitroLab (`pedals/nitrolab/`) is the lab pedal: a firmware bundle for developing
one module at a time on real hardware, with room next to it for variations and
breakout experiments. It is not a product; nothing here ships until it moves
into a real bundle (NitroTron3, ChronoTron3, ...).

- Build: `make PEDAL=nitrolab` (DIAG serial log: `make PEDAL=nitrolab DIAG=1`)
- Flash: `task flash-nitrolab` (DIAG: `task flash-nitrolab-diag`)

## The working method

**One main module, plus slots for variations and breakout discovery.** SW3
selects the slot:

- **SW3 UP — the main module** under development. Breakout work never touches it.
- **SW3 MIDDLE / DOWN — variation or breakout slots.** A breakout module copies
  the main module's engine (copy, not a shared refactor) and explores ideas the
  main module isn't ready for: other models, other algorithms, other control laws.

Rules for a breakout slot:

1. **Sweepable controls, judged on the pedal.** One static setting rendered on a
   sample is hit or miss; a knob sweep shows the actual effect. Typical layout:
   a model selector (switch = family, knob = model in zones), one or two sweep
   knobs per model, an amount knob (unprocessed ↔ model only).
2. **Ranges go to their natural extremes**, no precautionary taming, no
   per-model makeup gain (see `DESIGN_PHILOSOPHY.md`).
3. **An overload switch-off.** Any optional heavy work (e.g. extra voices) gets a
   knob position that skips its computation entirely, so overload glitches can be
   identified by ear.
4. **Promote by ear.** What survives the sweep is proposed for the main module
   (or a new one); the breakout slot is then reused for the next round.

Tuning values live in each module's `*_constants.h` (source of truth), never here.

## Current slots

| SW3 | Module | Source |
|-|-|-|
| UP | **Armitage** — polyphonic synth voice without pitch tracking (84-band complex filterbank, per-band shaping) | `modules/armitage.h`, `armitage_constants.h`, `armitage_bands.h` |
| MIDDLE | **Explorer** — breakout: Armitage's band engine with 14 FM / phase-distortion / digital models on sweepable knobs | `modules/explorer.h`, `explorer_constants.h` |
| DOWN | empty (pass-through) | `modules/empty.h` |

The control maps are in the comment block at the top of each module header.
