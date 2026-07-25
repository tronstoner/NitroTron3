# ChronoTron3 — Stage-1 Implementation Plan

Working plan for the first testable drafts of the ChronoTron3 bundle. Goal:
**testable starting points that explore range**, not an end-user-safe pedal. We
build the extremes and revisit safeguards/musicality later. This plan does not
override the module specs (`impulse resonator - ignis/IMPULSE_SYNTH_SPEC.md`,
`dynamic-looper-concept.md`) — where it guesses, that's flagged under _Decisions
to collect_.

## Bundle shell (built)

`pedals/chronotron3/` on the shared `src/core/` platform. `make PEDAL=chronotron3`.

- **`module.h`** — the Module interface (the proving ground for the architecture):
  `Init(sr)` · `Activate/Deactivate` · `Controls(cs, led1, led2)` (control-rate,
  main loop ~10 ms) · `Process(in, wet, size)` (audio-rate, mono wet).
- **`main.cpp` (shell)** owns only: SW3 mode select (**A=vestige · B=mnemonic ·
  C=ignis**), K6 equal-power dry/wet mix (smoothed), and the reserved both-FS
  bootloader gesture. Each active module owns everything else — including both
  footswitches.
- **`mnemonic`** = unspecced placeholder (passthrough) until it gets a spec.

## ignis — stage 1 (agent-implemented)

Per spec Staging: conditioning + both resonator cores at **fixed tuning**, played
into directly, **no analysis**. Chain: input → asymmetric-saturation conditioning
(K4 = asymmetry 0→0.5, drive constant ~1.0, per F3) → resonator bank (SW1: comb
/ modal, one param surface, T60-derived frequency-dependent damping per F1) →
env-coupled output filter (K5) → limiter → wet. Notes come from a fixed default
chord (stub for the analysis stage). K1 register (bipolar), K2 damping, K3
structure.

## vestige — stage 1 (agent-implemented)

Grain-based looper/freeze reusing `core/blocks` grain engine. FS2 = engage
(manual capture: record-while-held), FS1 = stop (tap mute / hold clear). K3 =
looper↔freeze smoothness macro (grain size + position scatter + density
co-vary; spray window = whole buffer). K1 = voices/topology. K4 = bipolar
texture (tape degrade ↔ digital glitch). ~8 s SDRAM buffer.

## Decisions taken (liberties — flag if wrong)

- **SW3 order** A/B/C = vestige/mnemonic/ignis (per your call, provisional).
- **Bypass:** ChronoTron3 has **no dedicated bypass footswitch** (both are
  module-owned). For now the effect is always active; only both-FS-held = DFU.
  Revisit if you want a true-bypass gesture.
- **K6 mix is shell-owned** and equal-power for every module (matches the
  NitroTron3 "K6 always mix" convention). Modules output pure wet.
- **Module interface = virtual base class** (dispatch once per block; negligible
  cost). This is the architecture proving ground; may be refined after stage 1.
- **No INSTRUMENT profile** for ChronoTron3 (ignis: "one control set for all
  instruments").
- Per-module constants files (`modules/<name>_constants.h`); bundle-global only
  in `pedals/chronotron3/constants.h`.

## Decisions to collect (revisit together)

**ignis** (spec Open items): register stepped-vs-continuous + default stacking;
whether structure (K3) and asymmetry (K4) are one perceptual axis; SW2
assignment (note-set policy) once a core is chosen; voice count (CPU); which
resonator core wins the A/B. Plus: the fixed default chord used as the stub, and
the manual-chord-entry mechanism for later.

**vestige** (spec Open items): K1 voice/topology geometry (range, where
frippertronics sits); voiced↔frippertronics selection; gain summation law
(1/N vs 1/√N vs soft-limit); K5 role split; K2 assignment; SW1-down and SW2;
LED blink-state mapping; continuous-auto capture depth.

### Concrete stage-1 choices in the drafts (tune/confirm by ear)

**ignis:** 6 voices allocated / 3 active = a **minor triad on A2** (MIDI 45,
one-line constant); register continuous ±1 oct, **no stacking**; K3 = allpass
dispersion (comb) / inharmonic partial stretch (modal); K5 = fast-atk·slow-rel
(CCW) ↔ slow-atk·fast-rel (CW); custom inline limiter; level-match makeup
`COMB=0.35 / MODAL=1.0` (guesses). K1/K2 coeff changes are **unsmoothed** →
fast sweeps may click (fine for discovery). SW1-MID → comb. Footswitches unused.

**vestige:** voices 1–6, frippertronics at **K1 > 0.86**; tap ≤350 ms / clear-hold
≥700 ms / min loop 10 ms; grains 400→30 ms, overlap 2→3, 16-grain shared pool;
K4 tape drive→45 / decimate hold→96 / crush→2.5 bits; **gain law 1/√N**, oldest
voice fades quietest; K2 = auto-capture threshold; K5 = age-fade slope / fripp
decay; continuous-auto capture implemented; SW1-DOWN → manual; SW2 unused.

### Flags needing a ruling / attention

- **vestige spec conflict:** the spec says "overlap = 0 at the looper end" *and*
  "overlap ≥ 1 for continuous output." The draft chose **≥1 everywhere**
  (click-free continuity). Needs your ruling.
- **vestige age-gain direction** (`(N−r)/N` "r-th-oldest") was ambiguous — draft
  makes the **oldest quietest**. Confirm.
- **vestige memory/CPU:** SDRAM ≈ **11.3 MB** (7 × 8 s slabs; tunable), CPU
  ~15–20% worst case. `EnterFrippertronics()` copies up to a full 8 s loop inside
  one 10 ms control tick → may stutter; chunk later if so. Record-write (ISR) vs
  commit (control) isn't lock-clean — glitch-tolerant, flag if you want it tight.
- **ignis:** whether K3 (structure) and K4 (asymmetry) are one perceptual axis;
  register stepped-vs-continuous + default stacking; the default chord identity.

## Ethos

Advanced experimental musicians; sonic mayhem over safety. Glitch, inharmonicity,
instability and happy accidents are features. Explore range to the extremes now;
safeguards and musicality are a later, joint stage.
