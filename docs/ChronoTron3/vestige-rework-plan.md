# Vestige — rework plan & sonic-exploration notes (working)

Status: **working / discovery.** Vestige (SW3 UP, the grain-based dynamic looper /
freeze) was a successful experiment, but the *pedal concept and UX are still
considered unfinished*. Before committing to a UX rework we want to widen the
**sonic bandwidth** — find the feature set worth keeping. This doc collects the
threads from that exploration so they don't get lost. It is not a locked spec.

Companion docs: `dynamic-looper-concept.md` (the original working spec),
`DESIGN_DECISIONS.md` (bundle-wide hard rules — G1 clean/dry sacrosanct etc.).

---

## 1. K4 = BBD / Tape degradation (folded in from mnemonic) — IN PROGRESS

**What / why.** K4 previously drove a *tape varispeed pitch-shifter* (a quantised
rotary of tape-speed ratios, pitch + loop-period coupled). That is retired. K4
now hosts mnemonic's degradation engine (`mnemonic_degrade.h`, the `MnemDegrade`
class) so we can audition the BBD/tape lo-fi character on looped/frozen material.

- **Bipolar around noon**, dead-zone clean centre (engine's own ±0.03).
  - **CCW → BBD**: integer-divisor ZOH clock, fold-down aliasing grit, dark
    reconstruction, aged noise + slow clock drift, amplitude "breath".
  - **CW → Tape**: asymmetric saturation, HF loss + head bump, wow/flutter,
    level-dependent hiss, capped dropouts + pitch snags.
  - Orientation matches mnemonic's K3 (BBD = CCW, Tape = CW) for cross-module
    muscle memory. (The old `dynamic-looper-concept.md` draft had the analogue
    side on CCW; that draft was never locked — we follow the shared engine.)
- **Placement.** Colours **only the looper output** `y` (the summed wet grain
  signal), applied where the parked texture block used to sit — *before* the
  routed dry `x` is summed. The clean/dry path is never touched (hard rule G1).
- **Idle hiss guard.** The engine injects noise; with no loop captured that would
  add a hiss bed to the output. `SetNoiseGate()` ducks the injected noise to 0
  when there is no active loop content, so at rest the output stays clean.
- **Modulation state.** `TapePitchCents()` is still called every sample (it
  advances the BBD "breath" flicker and the wow/flutter LFOs) even though its
  returned pitch offset is currently discarded.

### Pitch wander (wow/flutter/snag/drift) — DONE, as a post-grain warble

The wow/flutter/snag/clock-drift from `TapePitchCents()` is the biggest part of the
tape/BBD realism, and the first fold-in dropped it. It's now reproduced as a
**post-grain modulated delay line** on the continuous looper output (`warble_ring_`
in vestige): cents → leaky-integrated sample displacement → wobbled read tap, i.e.
mnemonic's wobbled read tap re-used as a post insert. Chosen over modulating the
grain read-rate because a tape wobble needs a *continuous* stream to read as tape.
Constants `VESTIGE_WARBLE_*`. Trade-off: a small fixed base delay (~3 ms,
inaudible) on the wet looper path so the tap can swing without reading the future;
displacement is clamped to stay in the past. Order: warble (speed) → `ColourProcess`
(head/electronics colour), matching tape physics.

---

## 2. Freeze character — kill the tremolo, get a slow morph

> **AS-BUILT (current) — read this first.** The exploration history below (§2 "Plan"
> onward) is kept for context but is **superseded**: vestige did *not* stay on the
> old pinned-anchor grain freeze. It adopted mnemonic's **multiband incommensurate
> granular** winner and generalised it into ONE engine spanning the whole K3 sweep.
> The as-built architecture is in the box immediately below; trust it over the
> narrative that follows.

### 2.0 As-built: the unified multiband granular engine

Vestige's K3 knob is a single granular cloud morphing continuously from clean loop
(CCW) → break-up → evolving freeze (CW) — **no engine switch at noon**. The freeze
half (s≥0.5) is the multiband freeze ported from mnemonic; the CCW half is the same
engine with the levers relaxed (long grains, 1 band, head follows the loop, no
scatter). Legacy single-stream looper/scrub/freeze survives only behind
`VESTIGE_MB_FREEZE=false` (fallback, not used).

**The phasing mechanism (the whole point).** The captured buffer is split into N
bands by **per-grain** biquads (no separate band buffers — memory stays flat). Each
band is its own grain cloud that scans the buffer at a **coprime scan length**, so
the bands drift against each other and never re-sync → a dense, continuously
evolving, phasing freeze. Coprimality (distinct primes) is what makes it "never
repeat"; the *ratios* between the scan lengths set the character of the phasing.

**Adaptive band count** = `min(voice budget, K3 chaos ramp)`:
- *Voice budget* (CPU-safe schedule, `VESTIGE_MB_*BAND_MAX_VOICES`): 1 voice → 5
  bands, 2 → 3, 3–4 → 2, 5–6 → 1. More voices = fewer bands so every voice gets
  grains under the `VESTIGE_MB_GRAIN_CAP` (16) ceiling. Only the 1-voice freeze
  uncaps to the full 5.
- *K3 chaos ramp* (`k3_bands_chaos_`, thresholds `VESTIGE_K3_CHAOS_{2,3,4,5}BAND`):
  1 band at the clean-loop end, splitting in to 5 as chaos → 1.0. Chaos pins at 1.0
  through the whole freeze half, so the freeze runs the full (voice-capped) count.
  The 2/3-band split points are unchanged, so the ≤3-band break-up feel is preserved.

**The filterbank** is generated at init from `VESTIGE_MB_XLO..XHI` (250 Hz–2 kHz),
**log-spaced**, N−1 crossovers: band 0 = LP, mids = BP (centre = geomean of its two
crossovers, Q = centre/bandwidth), band N−1 = HP. This reproduces the original
1/2/3-band splits **byte-for-byte** and extends cleanly to 4/5. Low & high bands are
pinned at 250/2000 for every N≥3 — adding bands only subdivides the mids.

**Per-band grain length / scan length / spray** are the tunable tables in
`vestige_constants.h`, indexed `[N-1][band]` (low→high). Grain-window ms per band:
low band pinned at 150 ms (holds bass wavelengths), high band at 40 ms (denser,
livelier; the floor before the grain-rate flutter `2/T` = 50 Hz climbs into audible
AM roughness), mids interpolate. 5-band scan primes: `{11987, 9973, 8419, 6113,
4099}`. Bump `VESTIGE_MAX_BANDS` past 5 by adding matching table rows.

**Code map** (all in `pedals/chronotron3/modules/`):
- `vestige.h` `ServiceMBFreeze(slot)` — per-band scan + scheduler, the K3 lever math
  (base head → swept freeze point via `k3_focus_`, coprime scan faded in with focus).
- `vestige.h` `EmitBandGrain(...)` — allocates a grain from the shared pool, applies
  the band biquad (`SetBandFilter`), enforces `VESTIGE_MB_GRAIN_CAP`.
- `vestige.h` `MBBuildBank(n)` / `MBInit()` — build the log-spaced filterbank for
  every band count into `mb_bank_coef_[N-1][band][5]`.
- `vestige.h` `Controls()` — computes `mb_nbands_ = min(bands_voice, k3_bands_chaos_)`.
- `vestige_constants.h` — the `VESTIGE_MB_{GLEN,SCAN,SPRAY}[MAX_BANDS][MAX_BANDS]`
  tables, crossovers, voice/chaos thresholds, `VESTIGE_MAX_BANDS`, grain cap.

CPU is bounded by the grain cap regardless of band count (5 bands × 1 voice ×
overlap 2 = 10 grains, under 16). Overlap is a uniform 2 (`VESTIGE_MB_OVERLAP`,
min for click-free Hann OLA).

---

**Plan:** the two freezes get **different** mechanisms so they can be A/B'd —
vestige stays time-domain grain, mnemonic gets a spectral phase-vocoder freeze.

- **vestige = path A (time-domain grain) — ATTEMPTED TWICE, BOTH REVERTED.** The
  vestige grain freeze is back at its **original stable tuning** (spray ±25 ms,
  overlap 3, jitter 0.15) — do not touch it again without a clearly grounded,
  hardware-verified reason. Two grain-tuning attempts regressed it:
  1. Shared `FreezeMod` (slow drift + micro-detune + hop) — made it *worse* (more
     stutter/flutter, "more random").
  2. Small-spray + overlap-4 + slow drift — introduced **clicks + bad grain
     behaviour** and (at overlap 4) an audio-callback **overrun/hang** on
     multi-voice freeze. Reverted in full.
  Lesson: the grain model is tuned and fragile; ungrounded grain-tuning
  "improvements" keep breaking it. Leave it alone. The tape warble + degrade
  (§1) are the kept, stable additions.
- **mnemonic freeze = MULTIBAND INCOMMENSURATE GRANULAR — THE WINNER.** User:
  "this is 100% it… an absolute winner." Block `mnemonic_multiband_freeze.h`
  (`MultibandFreeze`, built on GrainVoice/RingBuffer), mnemonic SW1-DOWN,
  `MNEM_FREEZE_MODE=1`:
  - split the captured window into 3 bands (low/mid/high, `MNEM_MB_XLO/XHI`);
  - each band = a **grain cloud** scanning that band's material — band-appropriate
    grain length (`MNEM_MB_GLEN_*`, long lows → short highs), overlap = **density**
    (`MNEM_MB_OVERLAP`, the "more grains" knob), position spray (`MNEM_MB_SPRAY_*`);
  - per-band scan loops are **coprime / incommensurate** (`MNEM_MB_LOOP_*`) so the
    bands never re-sync → continuously evolving, phasing, dense; never audibly
    repeats. `MNEM_FREEZE_GAIN` makeup.
  - Transient handling: freeze the **sustain** (normal gesture); structure alone
    doesn't remove transients. Add taming later if attacks tick.
  - Grain path kept as fallback (`MNEM_FREEZE_MODE=0`).
  - **The FFT/spectral path (and ShyFFT) were removed** — it was smooth/glassy, the
    opposite of the wanted moving/dense character. See `freeze-research.md` for the
    (now-historical) spectral investigation.
  - **Key lesson:** the builder had described this multiband-granular model for
    years; chasing smooth/coherent (grains-for-stability, phase-vocoder) was
    backwards. Build the builder's ear-described model first.

**Corrected root-cause analysis.** The flutter is *intrinsic to grain overlap-add
looping of a fixed buffer*: two half-overlapped grains constantly cross-fade
between **decorrelated** regions of the buffer, and cross-fading two different
signals beats (amplitude modulation at the grain-hop rate). That beat is the
tremolo. `FreezeMod`'s drift/detune add *evolution by adding decorrelation* — but
decorrelation is exactly what causes the tremolo, so it amplified the problem.
**You cannot fix overlap-add warble with more decorrelation.**

The fix must be a smoother *playback mechanism*, not more modulation. Candidate
directions (to be grounded by the EHX-Freeze research now running, not guessed):
- **Straight-read loop + short seam-only crossfade** (like the frippertronics
  loop) — no repeated overlap-add, so no grain-rate beat. Slow evolution would
  then come from *sliding the loop window* (a straight-read move), not grain
  decorrelation.
- **Dense many-grain cloud** (6–8 spread grains) so the AM averages out.
- **Spectral / phase-vocoder freeze** — if that's what true smoothness needs (cost
  concern on the Daisy; note whether an FFT is unavoidable).

Deep-research task launched: how the EHX Freeze/Superego actually sustain a grab,
why naive overlap-add flutters, how smooth freezes get a slow morph, and known
open recreations to build on. Rebuild the shared freeze block from that evidence.

### Original (superseded) sketch

**The nitpick.** The fixed-snapshot freeze in **mnemonic** behaves as expected and
is the reference. Vestige's freeze (K3 → CW) is close but its modulation still
reads **tremolo-like / loop-seam-like**. The original EHX Freeze smears much more:
a very slowly evolving, shifting, morphing sustain with no perceptible periodic
pulse.

**Why it pulses today.** At full freeze the read head is *pinned* to one anchor and
every grain draws from the same narrow window (`VESTIGE_FREEZE_SPRAY` ±25 ms) with
only small scheduler jitter (`VESTIGE_FREEZE_JITTER` 0.15). Two periodicities
result: (a) grains re-trigger at a deterministic hop → a periodic amplitude/spectral
seam at the grain rate (the "tremolo"); (b) all grains phase against the *same*
static window, so the beat pattern repeats instead of evolving.

**The model — "wandering, de-correlated multi-grain freeze".** Attack both
periodicities and add slow evolution. Three high-impact, cheap levers first:

1. **Slow anchor drift (de-correlated).** Instead of a pinned anchor, let the
   freeze read point *slowly wander* over a bounded sub-window via a very slow
   random walk / summed low-rate LFOs (think ~0.05–0.3 Hz over ±100–300 ms). Each
   grain samples the anchor at emit time, so successive grains come from slightly
   different, slowly-migrating positions → the timbre morphs continuously instead
   of repeating. This is the core of the EHX "evolving" feel.
2. **De-correlated / incommensurate grain hop.** Break the fixed grain period so
   overlap-add never lines up into a periodic seam. Either widen the per-grain hop
   jitter well past 0.15, or run the overlapping grain streams at mutually
   incommensurate hop periods so their sum never repeats → removes the tremolo
   pulse. (Guard density so it doesn't thin out.)
3. **Micro-detune spread.** Give each grain a tiny random pitch offset (±a few
   cents) so overlapping grains beat slowly against one another → a chorus/shimmer
   smear, the classic freeze "sheen". Reuses the grain rate parameter.

Secondary levers if still not smeary enough: longer grains + higher overlap at
full freeze (deeper Hann overlap lowers per-grain AM depth and COLA ripple), or a
two-anchor slow crossfade.

**Proposed tunable constants (names provisional):**
`VESTIGE_FREEZE_DRIFT_RANGE` (samples the anchor may wander),
`VESTIGE_FREEZE_DRIFT_RATE` (Hz, slow),
`VESTIGE_FREEZE_DETUNE_CENTS` (± per-grain detune),
plus a raised `VESTIGE_FREEZE_JITTER` / incommensurate-hop scheme, and possibly
larger `VESTIGE_CW_OVERLAP` / grain length.

**Recommended first build:** drift + micro-detune + de-correlated hop (levers 1–3),
each behind a constant, tuned by ear on hardware. Endpoints (K3 CCW looper, the
scrub zone) untouched — this only reshapes the frozen (noon→CW) region.

---

## 3. Two freeze models — keep the morph-from-loop, note the separation insight

We now have two freeze implementations:

- **Vestige:** sweepable, *morphs from looper → freeze* on one macro (K3).
- **Mnemonic:** fixed **snapshot** freeze (SW1-DOWN), behaves as expected.

Listening to the snapshot model, we start to understand why **Chase Bliss Onward**
keeps looper and freeze as *separate* concepts rather than morphing between them.
**Decision (soft):** we are **not** giving up on the loop→freeze morph in vestige —
it is the distinctive idea. But the snapshot model's predictability is a genuine
strength worth remembering when the UX rework happens. Park this as an open UX
tension, not an action item.

---

## 4. Future plan — auto-sliced freeze cells (loop → focused grain cells)

**Idea.** After a loop is captured, **auto-slice** it and use the slices as the
*freeze ranges*. Play in a melodic phrase, detect the note seams in the loop, and
treat the individual notes as freeze cells to build grains on — freeze cell
playback that is either **random** or **sweepable**.

**Constraints / shape (from the brain-dump):**

- **Bounded cell count `N`.** Limit to N cells; find a sweet spot for the cell
  distribution. Not Ableton — it does **not** need to be accurate or clean;
  glitchy is fine and on-brand.
- **No minimum cell length** — freeze already handles very short slices well.
- **Adjustable maximum cell length** as a compile-time constant. Default it to the
  same freeze-window ms mnemonic uses (a good freeze reference), so a long slice is
  capped into a meaningful freeze range.
- **Slicing method — start simple.** A default **slice grid** may be the best
  starting point: divide the loop by a fixed fraction (relative to loop length) so
  every slice lands in a time range usable for freeze generation. Onset/seam
  detection is the richer version; the fixed-grid divide is the cheap first step
  and probably where to begin.
- **Playback:** random or sweepable selection across the ≤ N cells (a knob scans
  the cells; another mode picks at random), each cell driving the existing freeze
  grain engine.

**Status:** future / not scheduled. Recorded here so the concept isn't lost. When
picked up, decide grid-divide vs onset-detect first, then the cell-scan UX.

---

## Open questions carried forward

- Degrade orientation confirmed BBD=CCW / Tape=CW (matches mnemonic) — revisit only
  if the ear disagrees.
- Whether the tape/BBD **pitch wander** should drive the grains (§1 opt-in).
- Freeze model levers and their ranges — all by-ear on hardware (§2).
- The looper-vs-freeze UX tension (§3) feeds the eventual UX rework.
- Auto-slice: grid-divide vs onset-detect, cell-scan control surface (§4).
