# Architecture — multi-pedal platform (proposal)

> **Status: proposal / draft.** This describes a target structure for growing
> the repo from one pedal (NitroTron3) into a *family* of pedals that share
> groundwork. Nothing here is built yet. It is written to be reacted to — see
> _Open questions_ at the end.

## Why restructure

The repo has produced a lot of reusable groundwork (DSP building blocks,
hardware bring-up, control handling, a preset system). We want to build new
pedals on top of it without (a) starting from scratch, (b) forking the whole
firmware per idea, or (c) being forced into NitroTron3's preset/footswitch
scheme when a new pedal wants its footswitches for something else.

Today two files carry everything and fuse concerns that want to be separate:

- **`src/NitroTron3.cpp`** — the pedal wiring (audio dispatch, mode routing)
  **plus** reusable helpers trapped inside it (`Mapf`, `MapCutoff`,
  `RemapKnob`, `Quantize`, `MixCurve`, `Wavefold`, knob smoothing).
- **`src/preset_system.h`** — reads the Hothouse hardware (footswitches,
  toggles, knobs, debounce/edge/hold) **and** decides what it all *does*
  (banks, flash recall, LED policy, SW3 = mode, bootloader gesture).
- **`src/*.h` DSP blocks** — already clean, swappable building blocks
  (`env_follower`, `moog_ladder`, `phaser`, `grain_voice`, `poly_octave`,
  `clouds/…`). This layer is in good shape and mostly just relocates.

The fix is to introduce two **seams**: one between the reusable *platform* and a
specific *pedal*, and one between a *pedal* and the self-contained *modes* it
hosts.

## Terminology

Precise words, because we now have several kinds of "unit":

- **block** — a fine-grained DSP building block, header-only (`env_follower`,
  `moog_ladder`, `phaser`, `grain_voice`, `poly_octave`, `clouds`). The
  ingredients. (These are what earlier drafts loosely called "modules".)
- **module** — a self-contained, **swappable** unit that owns its control
  mapping, its footswitch behavior, *and* its DSP graph (composed from blocks).
  This is the real unit of reuse and the thing a build can include or leave out.
- **mode** — the user-facing term for a selectable module slot ("SW3 selects
  the mode"). A mode is *implemented by* a module. Manuals keep saying "mode".
- **shell** — the per-pedal frame: hardware bring-up, the control surface, mode
  selection (**SW3, uniform across every pedal**), bypass, the reserved
  bootloader gesture, and the list of modules the build composes.

## The layers

```
┌─ core/  (platform — every pedal reuses it) ──────────┐
│   blocks/   DSP building blocks (header-only)        │
│   io/       control_surface: hw read + debounce/     │
│             edge/hold, LED helpers   (no policy)     │
│   util/     knob mapping + math helpers              │
└──────────────────────────────────────────────────────┘
             ▲                        ▲
             │ composes               │ composes
┌─ pedals/<pedal>/  (one firmware target) ─────────────┐
│   shell        mode select · bypass · reserved       │
│   (main.cpp)   both-FS bootloader gesture            │
│   modules/     one per mode — each owns its          │
│                controls + footswitches + DSP         │
│   constants.h  build-time config / feature flags     │
└──────────────────────────────────────────────────────┘
```

- **core/** is the platform. It is pedal-agnostic, has no opinion about what a
  footswitch means, and never touches flash.
- **pedals/`<pedal>`/** is one pedal = one firmware target. It composes a set of
  modules and provides the shell around them.
- **A module** is the swappable unit. Because it carries its own footswitch
  behavior, two modules in the same pedal can use the footswitches completely
  differently, and a module can (eventually) move between pedals unchanged.

## Target directory layout

```
src/
  core/
    blocks/     env_follower.h moog_ladder*.h phaser.h grain_voice.h
                poly_octave.h synth_osc_*.h freq_shifter.h bitcrush.h
                grendel.h resampler.h ring_buffer.h peak_limiter.h clouds/ …
    io/         control_surface.h   # Hothouse read, debounce, edge/hold, LEDs
    util/       knob_map.h math.h    # Mapf, MapCutoff, RemapKnob, Quantize, …
  pedals/
    nitrotron3/
      main.cpp            # shell: init, mode routing, audio dispatch
      preset_policy.h     # pedal-level footswitch policy (banks/flash/LEDs)
      modules/
        bordun.h  sprawl.h  schism.h
      constants.h         # config + INSTRUMENT profile
    <new_pedal>/
      main.cpp
      modules/
        <mode_1>.h        # owns its own footswitches
        <mode_2>.h        # owns its own footswitches (differently)
        <mode_3>.h        # in discovery — placeholder slot
      constants.h
```

Modules start life under their pedal. When one is genuinely reused by a second
pedal, promote it to a shared `src/modules/` — the Module interface below is
what makes that promotion a *move*, not a rewrite. `src/clouds/` relocates to
`core/blocks/clouds/` unchanged (vendored, keep its LICENSE alongside).

## Seam 1 — control surface (core/io)

Reusable, **no policy**. Wraps the Hothouse and exposes debounced/smoothed
inputs plus footswitch *events*:

```cpp
struct FootswitchEvent {   // per footswitch, refreshed each Tick
  bool     down;           // currently pressed
  bool     rising, falling;// edges this tick
  uint32_t held_ms;        // 0 when up; dwell while held
};

class ControlSurface {
 public:
  void  Init(Hothouse& hw);
  void  Tick(uint32_t tick_ms);         // poll + debounce; from the main loop
  float Knob(int i) const;              // 0..1, smoothed
  int   Switch(int sw) const;           // 0/1/2 toggle position
  const FootswitchEvent& Foot(int fs) const;
  bool  BothHeld(uint32_t ms) const;    // for the reserved bootloader gesture
};
```

## Seam 2 — the Module interface (the swappable unit)

A module is self-contained: it reads controls (including footswitch events),
runs its DSP, and drives whatever LED behavior it wants while active.

```cpp
class Module {                          // one per mode
 public:
  void Init(float sample_rate);
  void Activate();                      // entered this mode
  void Deactivate();                    // left this mode
  void Controls(const ControlSurface& cs, LedView& leds);  // owns FS policy
  void Process(const float* in, float* out, size_t n);
};
```

The shell holds the pedal's modules and routes to the active one. Footswitch
events flow like this:

```
footswitch activity
      │
      ▼
[shell]  both-FS held ≥ 2 s ? ──yes──▶ enter bootloader   (always reserved)
      │ no
      ▼
pedal-level policy installed?  ──yes (NitroTron3: presets)──▶ consumed here
      │ no
      ▼
active module .Controls(...)   ──▶ per-mode footswitch behavior
```

- **NitroTron3** installs a **pedal-level policy** (`preset_policy.h`) that
  claims the footswitches across *all* its modes: FS1 cycles presets, banks,
  flash recall, Roman-numeral LEDs, SW3 = mode. Its three modules only supply
  controls + DSP; they don't touch the footswitches. Behavior stays identical.
- **The new pedal** installs **no** pedal-level policy, so each module handles
  the footswitches itself — different from mode to mode, exactly as intended.

The only globally reserved gesture is the both-FS ≥ 2 s bootloader entry; the
shell watches for it in parallel and preempts.

> **Interaction constraint.** Because that gesture is always reserved, a module
> that uses *both* footswitches (or long holds) must coexist with it — hold
> actions under the 2 s threshold are safe; a both-FS hold that runs to 2 s will
> be captured by the shell as bootloader entry. Keep module footswitch actions
> clear of that.

## Composition & build

"Swappable modules / different builds with different modules" = **compile-time
composition**, the pattern already in use (`INSTRUMENT=guitar`,
`MODE_C_POG_ENABLE`). No runtime plugin system needed.

- **`PEDAL=` selector** in the Makefile picks which `pedals/<pedal>/main.cpp`
  builds — exactly mirroring today's `INSTRUMENT=`. One target per pedal.
  `INSTRUMENT` stays orthogonal (a pedal may or may not honor it).

  ```
  make PEDAL=nitrotron3                    # default
  make PEDAL=nitrotron3 INSTRUMENT=guitar
  make PEDAL=<new_pedal>
  ```

- **Which modules a build includes** is a pedal's `main.cpp` composition (+
  `constants.h` feature flags in the `MODE_C_POG_ENABLE` style). Dropping or
  swapping a mode is a local change to one pedal.
- **Module swap** is safe because a module is a leaf that depends only on
  `core/` — never on another module.

## Migration path (staged, behavior-preserving)

Each stage leaves `make` green and NitroTron3 firmware **byte-identical** until
we deliberately add the new pedal. Nothing is a big-bang rewrite.

1. **Relocate DSP blocks** into `core/blocks/` (+ `clouds/`), fix includes.
   Pure move; diff is paths only.
2. **Extract `core/util/`** — lift `Mapf`, `MapCutoff`, `RemapKnob`,
   `Quantize`, `MixCurve`, `Wavefold`, knob smoothing out of `NitroTron3.cpp`.
   Mechanical; same code.
3. **Extract `core/io/control_surface.h`** — pull the raw Hothouse read +
   debounce/edge/hold out of `preset_system.h`; it now consumes a
   `ControlSurface`. Checksum the `.bin` to prove no drift.
4. **Move NitroTron3 into `pedals/nitrotron3/`** and add `PEDAL=` (default
   `nitrotron3`). Two sub-steps:
   - **4a** move as-is behind the shell; preset system → `preset_policy.h`.
     Confirm byte-identical release.
   - **4b** carve BORDUN / SPRAWL / SCHISM into `modules/` behind the Module
     interface. Still byte-identical — this is where the seam earns its keep.
5. **Scaffold `pedals/<new_pedal>/`** — its two defined modes as modules (each
   owning its footswitches), plus a placeholder for the third. First new work.

Steps 1–4 are refactors with a checksum gate. Step 5 is the first new feature.

## Decided

- **SW3 selects the mode on every pedal.** Mode selection lives in the shell and
  is uniform across the family; only what the *footswitches* do varies (per
  module, or per pedal-level policy). This keeps the shell/module boundary
  fixed: the shell owns SW3, the active module owns the footswitches.

## Open questions

- **Third mode still in discovery** — its module slot is a placeholder; the
  interface should not be locked purely around the two defined modes.
- **When to promote a module to shared `src/modules/`** — proposal: only on the
  first genuine second use, to avoid premature generality.
- **Cross-pedal platform constants** — sample rate, block size, peak limiter,
  DFU address: platform defaults in `core/`, per-pedal `constants.h` may
  override.
- **Docs & skills** — `AGENTS.md`, `PROJECT.md`, and the `build`/`release`
  skills assume `src/NitroTron3.cpp` and a single target; they update alongside
  step 4, not before.
