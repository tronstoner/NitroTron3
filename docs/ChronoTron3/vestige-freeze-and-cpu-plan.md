# vestige — plan: freeze controls + playback CPU

**Status: PLAN for review (2026-09-30). Nothing here is implemented.**
Two independent parts, to be done in this order: **A** gives the freeze side
(SW1 DOWN) the controls it ignores today; **B** fixes the playback CPU budget
(3 loops + K1 octave overruns the block even with every effect off). B's
cheap, bit-identical steps can go first if CPU headroom is needed for A.

Source: two read-only code audits of `pedals/chronotron3/modules/vestige.h`
(V) / `vestige_constants.h` (C) / `src/core/blocks/grain_voice.h` and the
libDaisy build, plus the on-pedal DIAG logs of 2026-09-29.

---

## A. Freeze: what reaches it today

The freeze pool is slots 8..13; every loop feature is guarded to the loop
voice slots (`s < VESTIGE_VOICE_SLABS`, `!e.frozen`), so almost nothing
reaches it.

| control | on the freeze today |
|-|-|
| K1 speed crossfade | **inert** — freeze grains are hard-coded to rate 1 (V `EmitBandGrain`) |
| K2 T + direction | **inert** (only the LED1 flash rate) — window fixed 400 ms, forward only |
| K3 CW glitch layers | **inert** — timing/condition/playback are loop-slot only |
| K3 CCW rhythm | **inert** |
| K4 degrade | **applies** (it runs on the summed wet) incl. the deep-BBD gain |
| K5 CCW repeats | **inert** — and in that half the replace crossfade is 3 ms (a near-hard swap) |
| K5 noon / CW fade | **applies** (endless; CW sets the re-freeze crossfade) |
| SW2 follow mode | **inert** (a freeze has no division of T) |
| FS1 tap | **inert** (LED1 only) |
| FS2 tap / hold | **applies** — hold is the freeze latch (no new captures, kept across SW1) |

How the freeze plays: 1 live voice, re-freezes on **every new phrase** (auto
gate, same as the loop side) with an equal-power K5 crossfade; 400 ms window,
unquantised; 3 bands (150 / 80 / 40 ms grains, overlap 2, coprime scans,
spray); 6 grains per freeze, 12 during a crossfade.

Found on the way (not controls, but worth deciding):
- **A capture ended by silence keeps its ~80 ms silent tail** in the window.
- **No pluck-transient avoidance** (the EHX trick in `freeze-research.md`):
  the window starts on the attack.
- `span = L - glen - scanlen` leaves the low band almost no position travel
  on a full window (13 samples) — relevant if a position/size control comes.
- Stale header comments in vestige.h / constants and `CONTROLS.md` (K3/K4
  meanings) — fix in the docs pass.

## A. Decisions (builder, 2026-09-30)

- **K3 stays consistent: the loop side's glitch engine applied to the freeze
  as is** (CW layers, CCW rhythm), not a separate freeze-blip scheduler. The
  freeze needs a timeline for that: a virtual pass (T, or its window) whose
  steps the layers / rhythm act on, with stutter / repeat / retrigger /
  reverse working on the freeze's read position. To be worked out.
- **Capture behaviour unchanged** — auto-capture is the core concept (as on
  the Onward; the reason manual capture was retired). **SW2 unchanged** (the
  follow mode stays; tape vs stretch still to be decided by testing). If SW2
  is reassigned later: likely degrade textures and/or glitch modes; a unified
  engine overlapping sprawl is a longer-term dream.
- **Agreed: K1** (an octave layer for the freeze pad) and **K5** (SUSTAIN on
  the CCW half). **K2: NOT on the freeze** (revised 2026-10-02): the freeze
  window is a constant; K2 / tap only set the tempo, which matters on the
  freeze for the K3 glitches (their virtual pass = T).
- Open: the freeze tail-trim / attack-skip fixes (they touch capture).

## A. Proposal (original, for reference): the omitted controls on the freeze

Onward's freeze side reacts to SIZE, SUSTAIN, TEXTURE and ERROR, and its
errors there are **brief blips** (not rhythmic events): timing = churning,
condition = bump & bite, playback = momentary pitch shifts (manual, see
`vestige-onward-rework-plan.md` §2.4). Proposal, one row per control — each
is a decision for you:

| control | proposal for the freeze | notes / cost |
|-|-|-|
| **K1** | the same octave crossfade (half / clean / double) on the freeze grains | grain rate hook in `EmitBandGrain`; doubles freeze grains inside the crossfade (6 → 12) |
| **K2** | magnitude = **SIZE**: the freeze window used, ~100 → 400 ms (grain lengths / scans scaled with it); CCW direction = **reverse** grains | > 400 ms needs a bigger freeze slab; T stays the tempo for K3 CCW / K5 |
| **K3 CW** | **freeze errors as brief blips**, free-running (not on a bar grid), density = K3: CONDITION = short rests / decimate (extend the mute/decimate stage to freeze slots), PLAYBACK = a momentary 2× / ½× pitch or reverse blip (grain rate), TIMING = churning (scan jump / grain-length wobble) | a separate scheduler weighting (Onward stage-7 note); new per-slot state for freeze slots |
| **K3 CCW** | the **rhythm on T** over the pad: Euclidean rests + off-beat decimate (no stutter — a pad has nothing to repeat), tempo from K2 / tap | the "freeze becomes rhythmic" middle ground, without feeding the loop |
| **K4** | as now | — |
| **K5 CCW** | **SUSTAIN**: the freeze fades out over N × T with the loop side's dB curve (E), smooth (a pad has no passes to step on) | K5 CW (fade) and noon (endless) as now |
| **SW2** | free on the freeze side — suggestion: **capture behaviour**: UP re-freeze on every phrase (now) · MIDDLE re-freeze only when the level drops (no mid-phrase swaps) · DOWN latch (first capture holds, like FS2 hold) | or leave unused |
| **FS1** | tempo for K3 CCW / K5 (as T) | — |
| **FS2** | as now | — |

Plus, recommended regardless: **trim the silent tail** off a silence-ended
capture and **skip the attack** (start the window a few ms after the onset),
both small.

Suggested order: SW2 capture behaviour + tail/attack fixes → K1 → K5 SUSTAIN
→ K3 CCW → K2 SIZE/reverse → K3 CW blips (largest). One change per build,
by ear.

---

## B. Playback CPU

**Measured (DIAG, `System::GetUs` around vestige `Process`, peak per 2 s):**
no loop ~210 µs · 1 loop + K1 octave 490-630 µs · 3 loops + K1 octave
950-1100 µs of a 1000 µs block → clicks, even at K3 CCW and K4 noon.

**Findings (audit, file refs in the audit notes):**
1. **The core runs at 400 MHz, not 480.** `hw.Init()` defaults to no boost
   (`hothouse.h:92` → `SysClkFreq::FREQ_400MHZ`). `hw.Init(true)` = +20 %
   on everything; SDRAM and audio clocks unchanged → bit-identical audio.
2. **The static cost model is 3-4× below the measurement** (~110-150 cycles
   per grain per sample estimated vs ~550 implied). The missing time is
   **stalls**, not arithmetic:
   - **SDRAM row misses**: grains are rendered sample by sample, interleaved,
     so nearly every cache-line fill is a row miss (~140-170 cycles each, one
     per 8 samples per grain; the warble ring write each sample adds more).
   - **I-cache conflicts**: the grain loop, `GrainVoice::Process` and
     `ServiceMBFreeze` map onto the same I-cache sets (16 KB, 2-way); code
     runs from AXI SRAM through the cache, ITCM (64 KB, zero-wait) is unused.
   - The DIAG peak also includes IRQ pre-emption (USB in DIAG builds).
3. **Per-sample overhead even when idle**: 2× `memset` of 15-slot sums, a
   scan of all 48 grains, a loop over all 15 slots — every sample.
4. **K1 between noon and its end doubles the grains** (clean + speed version
   both sound): 2 → 4 grains per loop, and the double-speed ones need twice
   the SDRAM fills. 3 loops + K1 = 12 grains (16 during a replace = the cap).
5. Small: a `vdiv` per grain per sample (window), `GrainVoice::Process` not
   inlined, `-fno-move-loop-invariants` in libDaisy's flags, `-O2`.

**Plan — measure once, then bit-identical steps first, each its own build:**

| step | what | sound risk | effort | expected |
|-|-|-|-|-|
| B0 | **One DIAG round with a real profile**: DWT cycle / stall counters per section (grain render, slot service, post), average + peak, IRQ time separated; silicon revision read | none | small | tells which of B2-B5 pays |
| B1 | **480 MHz** (`hw.Init(true)`), after checking the silicon rev | none (bit-identical) | 1 line | +20 % |
| B2 | cheap code: active-grain list instead of scanning 48; skip dormant slots; no per-sample memsets; warble ring out of SDRAM (AXI SRAM / DTCM) | none | small | ~5-10 % |
| B3 | build flags for our code: drop `-fno-move-loop-invariants`, add `-fno-math-errno` (no `-ffast-math`: breaks the finite guards) | none | small | a few % |
| B4 | **hot code into ITCM** (linker section + copy at boot): grain loop, `GrainVoice::Process`, `ServiceMBFreeze`, `AdvanceHead`, `TimingStep`, audio callback | none | medium | removes I-cache stalls — large if B0 shows them |
| B5 | **render grain by grain per block** (each grain reads its run of SDRAM in one go → row hits) instead of sample by sample across grains; sums added in the same order | none if done carefully (verify bit-identical on host) | large | the big SDRAM win (~3× cheaper fills) |
| B6 | window reciprocal at grain start instead of a divide per sample | ≤ 1 ulp of the window (~-150 dB), inaudible — flagged | small | ~5 % of grain cost |
| B7 | last resort: spread slabs over the 4 SDRAM banks; tighter SDRAM timings | none / hardware-stability risk | medium | small-medium |

**Target:** 3 loops + K1 octave at ≤ 70 % peak (headroom for the freeze
work in A and for K4 degrade engaged, ~+15-35 %).

**Suggested order:** B1 + B2 + B3 as one "free speed" build (all
bit-identical) → B0 profile on that build → B4 or B5 depending on what the
profile shows. If B1-B3 already reach the target, B4/B5 wait.

## Decisions for you

1. A: which rows of the freeze table (and the SW2 idea) — and the order.
2. A: tail trim + attack skip on freeze captures — yes/no.
3. B: OK to start with the bit-identical "free speed" build (B1-B3)?
4. B: one DIAG profile round (B0) after it — or skip and go by the clicks.

---

## Rhythm concept notes (builder, 2026-10-02) — for the K3 rhythm design

- A Euclidean pattern only means something against a reference: a straight
  pulse (the loop's own playing) or other Euclidean layers whose hits rarely
  or never coincide. Polymetric layers loop on their own cycles with no
  master / slave beat, yet form one gestalt; rhythm becomes timbral colour.
- Rests are not a voice: they make a second voice perceivable, two ways:
  **ducking** (rests where the second rhythm would hit — the loop ducks like
  a pad under a kick) and **negative space** (rests in the gaps where neither
  rhythm would hit, so the composite of both is heard through the silence).
  In vestige: stutters = voice A; rests on B's hits = ducking, rests on the
  gaps of A ∪ B = negative space.
- Method: rate by ear (DIAG `VS RHY` log), lock only liked combinations,
  then test these rules against the ratings.
