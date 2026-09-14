# armitage — ARCHIVED (kept for later pickup)

> **Status: archived 2026-09-14, not in any build.** The code is intact and
> unchanged on disk; only its two lines of wiring were removed. This file
> records *why*, *what state it was left in*, and *exactly how to bring it
> back* — read it before either reviving armitage or deleting it.

## What happened

armitage (impulse synth / chord-detect resonator, Chase Bliss Lost+Found as the
reference) occupied the ChronoTron3 **SW3-DOWN** slot. It was taken out of the
bundle in `6e0afbb` and the slot given to **sprawl** (the granular delay ported
from NitroTron3's Mode B).

**Why — the builder's verdict, 2026-09-14:** *"In the end the mode sounded
nearly like the Chase Bliss but I dislike the response and think the mode
largely unuseful for my plan."*

Read that precisely, because it decides what a revival would have to fix:

- **The voice succeeded.** The timbre landed on the reference. The driven
  near-unity comb + feedback-FM "gnarl" is a keeper (see
  `ARMITAGE_AS_BUILT.md` and memory `project_armitage_character`).
- **The response failed.** How it answers playing — detection latency, onset
  retriggering, note-set stability, the feel of the chord locking in — is what
  made it unusable in practice.

So this is **not** a "finish the DSP" job. Anyone reviving armitage should start
from the detection/onset/voice-leading pipeline (`ARMITAGE_AS_BUILT.md` §
detection → onset → settle → snapshot), not from the resonator.

## What is on disk

| Path | State |
|---|---|
| `pedals/chronotron3/modules/armitage.h` | intact, unmodified, **not compiled** |
| `pedals/chronotron3/modules/armitage_constants.h` | intact, tuning values = source of truth |
| `ARMITAGE_AS_BUILT.md` (this folder) | the as-built model — read first on revival |
| `IMPULSE_SYNTH_SPEC.md` | original design intent |
| `chase-bliss-lost-found-impulse-synth-research.md` | reference research |
| `saturation.py`, `validate.py` | offline helpers |

Nothing was deleted or moved. `armitage.h` is header-only and included by
nobody, so it costs zero flash, zero RAM and zero build time while archived.

**Last state in a shipping build:** commit `acbcc4e` (docs refresh) on top of
`5392071` (chord-detect voice: detection, onset, glide, declick). To read the
module exactly as it last ran, including its shell wiring:

```
git show 6e0afbb^:pedals/chronotron3/main.cpp        # shell with armitage wired in
git show 6e0afbb^:pedals/chronotron3/constants.h     # CT3_MODE_ARMITAGE enum slot
```

## How to bring it back

Three edits, all in the shell — the module itself needs no changes:

1. **`pedals/chronotron3/constants.h`** — add the mode slot back. It was
   `CT3_MODE_ARMITAGE = 2` (SW3 DOWN); sprawl now holds that index, so either
   displace sprawl or grow `CT3_MODE_COUNT` and give armitage a new slot. Note
   SW3 only has three positions, so a fourth module means a different selector.
2. **`pedals/chronotron3/main.cpp`** — `#include "modules/armitage.h"`,
   declare `Armitage armitage;`, and put `&armitage` in the `modules[]` array.
3. **Optional, the serial chord log** — the `armitage_k::DEBUG_LOG` block that
   printed each detected chord as note names + cents was removed from the main
   loop in `6e0afbb`. Recover it verbatim from `git show
   6e0afbb^:pedals/chronotron3/main.cpp`. `hw.seed.StartLog(false)` is still
   called in the shell, so the log works the moment the block is restored.

The pitch-tracker profile in `pedals/chronotron3/constants.h` (`TRACK_*`,
`NT3_GUITAR`) was **kept unchanged** — it was tuned for armitage and is now
also used by sprawl's ringmod keytracking. It needs no work on revival.

## Constraints that still apply

These are not negotiable on a revival, they cost real time to learn:

- **Instrument-agnostic.** One firmware for bass AND guitar. Never an
  instrument `#ifdef` or per-instrument constants (memory
  `project_chronotron3_instrument_agnostic`).
- **Driven resonator, not self-oscillation.** Self-oscillation was tried and
  rejected: it built slowly and sat static with no playing dynamics.
- **Detection range must reach the instrument's fundamentals** (chord detect
  starts at E1), and the resonators only ring at frequencies the input actually
  contains — you cannot cheaply "add" chord tones as extra resonators
  (`ARMITAGE_AS_BUILT.md` §4).
- The "directions parked" section of `ARMITAGE_AS_BUILT.md` lists what was
  already explored and abandoned (auto-voicing dead-end, pitch-shift/POG path
  for density). Check it before re-treading.

## When to delete instead

Delete only on the builder's explicit say-so. The material worth keeping even
then is the **research and the as-built model**, not the source: the chord
detector (filterbank → onset → settle → snapshot → sub-harmonic fundamental
filter) and the feedback-FM resonator are both reusable ideas for other
ChronoTron3 modules.
