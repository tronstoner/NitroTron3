# Freeze — research findings (EHX Freeze & how to recreate it)

Status: **reference.** Distilled from a verified multi-source deep-research pass
(2026-07-31). Feeds the freeze rebuild in `vestige-rework-plan.md` §2. Cited,
adversarially fact-checked (21/25 claims confirmed 2–3 votes).

## What the EHX Freeze actually is

- **Not publicly documented.** No patent/teardown reveals the algorithm. A
  hardware teardown shows a **Freescale DSP56xxx** (24-bit fixed-point) running a
  program from EEPROM — so it's a generic DSP, any algorithm is possible.
- **Designer David Cockerell (Premier Guitar):** it is **NOT a naive looper**
  (capture slice → amplitude envelope → replay, which clicks). It uses an
  algorithm with a **special provision to avoid freezing on the pluck transient**,
  which is what kills the click. That's the only confirmed internal detail.
- So every "how it works" below is a **proven recreation technique**, not EHX's
  actual internals.

## Why our 2-grain freeze flutters (root cause, confirmed)

Per Julius O. Smith / CCRMA (COLA theory): a Hann window at exactly 50% overlap
**is** constant-amplitude — so the window *sum* is not the problem. The flutter is
**inter-grain beating**: two overlapping grains read the *same fixed buffer at
positions ~N/2 apart* (decorrelated content), and cross-fading two different
signals beats at the grain-hop rate. **This confirms the earlier session mistake:**
adding drift/detune (more decorrelation) between just 2 grains made the beating
worse, not better. Decorrelation only averages out with *many* grains.

## Two proven recreation paths

### A. Time-domain granular freeze (cheap, no FFT) — like MI Clouds
Freeze the capture buffer, read grains from it. Fixes:
- **Satisfy COLA** (Hann @ 50%, or Hamming @ 75%, Blackman @ 2/3).
- **More, decorrelated grains** (3–4+, spread read positions) so the beating
  averages out — NOT 2.
- **Small slow position jitter** (±~2–20 ms) around the read point → adds
  movement, avoids comb-filtering/sterility (Sound on Sound).
- **Per-grain amplitude window** (full Hann) → no edge clicks.
- Slow evolution: the position jitter itself, or two slowly-drifting loop points
  crossfaded.
- Reference: Mutable Instruments Clouds granular engine, **MIT-licensed**
  (`clouds/` in github.com/pichenettes/eurorack), runs on an STM32F4 — squarely
  in our budget.
- ⚠ Refuted: "just raise grain density" alone does NOT smooth it (0–3).

### B. Phase-vocoder spectral freeze (FFT) — true "glassy" smoothness — like MrFreeze
Hold the captured **magnitude** spectrum fixed; advance each bin's **phase** by the
per-frame increment measured at freeze (`mag · e^{j·accumulated_phase}`), resynth
with **sqrt-Hann** analysis+synthesis windows @ 50% overlap. This is exactly
`github.com/romi1502/MrFreeze` (FFT 2048, 50%).
- Reusing static phase → phasey/discontinuous; the **running-phase accumulation**
  is what keeps it smooth (Cycling '74).
- Slow evolving/drone smear: inject a **small random per-bin phase deviation** each
  hop (Cycling '74, Frostbite); full phase randomization = Paulstretch drone
  (destroys transients).
- Cost: needs an FFT (1024–2048) — feasible on the STM32H7 but the heavier path;
  can still sound phasey on rich bass without peak-locking / PGHI (arXiv 2202.07382).

## Recommendation

Try **path A** first (COLA hop + 3–4 decorrelated grains + small slow position
jitter + full-Hann per grain); it's cheap and Clouds proves it works on the same
class of MCU. Reserve **path B** (small-FFT phase-vocoder) only if A can't reach
EHX-grade smoothness on real bass/guitar.

## Sources
- Premier Guitar — Cockerell on the Freeze: https://www.premierguitar.com/pro-advice/the-good-stuff/the-beauty-of-the-ehx-freeze
- JOS/CCRMA COLA: https://ccrma.stanford.edu/~jos/sasp/COLA_Examples.html
- MI Clouds (open source, MIT): https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/open_source/
- MrFreeze (PV freeze, C++): https://github.com/romi1502/MrFreeze
- Cycling '74 phase-vocoder I/II: https://cycling74.com/tutorials/the-phase-vocoder-%E2%80%93-part-i
- Paulstretch: https://mashav.com/sha/praat/scripts/Paulstretch.html
- Sound on Sound granular primer: https://www.soundonsound.com/techniques/granular-synthesis-practical-introduction
- "Phase Vocoder Done Right" (PGHI): https://arxiv.org/pdf/2202.07382
