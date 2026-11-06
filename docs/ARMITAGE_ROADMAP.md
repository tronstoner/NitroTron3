# Armitage — roadmap to milestone 1

**Status: current (2026-11-06).** Working plan, not a spec. Tuning values never
live here; they are in each module's `*_constants.h`.

## Where we are

Armitage is the NitroLab module we want to commit to: a synth voice for bass
without pitch tracking, built on the 84-band filterbank. The lab pedal has
three **benches**, selected by SW3 (see `NITROLAB.md`):

| Bench | SW3 | Module | Role |
|-|-|-|-|
| Bench 1 | UP | Armitage | the target; what ships |
| Bench 2 | MID | Explorer | where FM and wavetable are taking shape |
| Bench 3 | DOWN | FM Lab (Explorer) | 2-OP / Spectral / Additive experiments |

The journey so far: instead of committing ideas back to Bench 1, we branched
out into many models on Benches 2 and 3. That was fruitful, but the controls
drifted apart and Bench 1 is now far behind. Milestone 1 brings the winners
back into Bench 1 with one consistent control layout.

Tool: the dev interface (`DEV_INTERFACE.md`, `task dev`) lets the builder tune
live on the pedal. Presets and modulation are now tuned there, not in code.

## Milestone 1: the steps

1. **FM — done.** Bench 2, SW1 UP: ratios, touch env, K5 LFO placement are
   locked. It becomes Armitage's FM mode as it is.
2. **Wavetable (subtractive) — next.**
   - Controls simplified (2026-11-06):

     | Control | Job |
     |-|-|
     | K1 | preset |
     | K2 | timbre: moves through the preset's waves (smooth or stepped) |
     | K3 | filter |
     | K4 | touch env |
     | K5 | LFO, as in FM: CCW triangle, noon off, CW steps |

     The journey is gone. It had no mental model that maps to a control.
   - Per preset, tuned live: LFO depth on timbre and on the filter, speed
     range, and the source (LFO or touch env).
   - Open:
     - which tables and presets stay;
     - whether K2 should be discrete per preset;
     - whether LOFI stays a preset or becomes an option.
3. **Bench 3 — decide.** For each of FM Lab 2-OP, Spectral and Additive:
   - promote it to Armitage,
   - keep it on a bench,
   - or move it to the catalogue.

   Spectral has a strong touch env and density model.
4. **Port to Bench 1.**
   - The chosen modes go into Armitage with one control map: SW1 = mode,
     K1..K5 the same jobs across modes where possible.
   - Bench 2 and Bench 3 then become free benches for the next round.
5. **Release prep.** README controls tables, the manual SVGs (generated), a
   hardware check of CPU and clicks, then lock-in with tests.

## Catalogue (ideas kept, not in milestone 1)

Code stays in the repo; this list says where each idea lives and what it did,
so nothing gets lost when a bench is reused.

| Idea | Where | Status |
|-|-|-|
| Journey (K2 start, K5 direction and speed) | `explorer.h` JourneyUpdate | replaced by K5 LFO 2026-11-06 |
| Per-band lofi (foldover, roll-off weights) | git `57f7bfc` | rejected: summed hiss |
| Noise "blow" / breath | git stash "half-done quadrature BLOW" | dropped for lofi |
| Armitage PD / additive models | `armitage.h` | partly retired (see `project` notes) |
| Chord banks (OCT / MAJ / DOM7) | removed | CPU overload |

## Parallel projects

The lab may hold more than one target. A new pedal module gets its own name,
its own bench and its own roadmap file like this one.
