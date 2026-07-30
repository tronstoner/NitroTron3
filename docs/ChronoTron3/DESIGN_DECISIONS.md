# ChronoTron3 — Design Decision Log

Focus: the **mnemonic** module (tap-tempo tape/BBD analog-style delay, SW3 MIDDLE).
Also captures a few bundle-wide rules that mnemonic depends on.

## What this file is / how to use it

This is the **why**, not the **what**. The concept/impl-plan/CONTROLS docs describe
what the module does today; this file records *why* each non-obvious choice was
made, what problem it solved, what was tried and rejected, and the hard rules that
must not be re-litigated or regressed.

- **Before changing a settled area**, read the relevant row below. If a decision is
  "in force", don't undo it without new evidence — several of these are fixes for a
  specific, reproduced regression (noted where known).
- **When you make a new non-obvious decision** (or reverse one here), add/annotate a
  row: What · Context · Why-over-alternatives · Alternatives-rejected · Status.
- **Rejection is contextual, not eternal.** Several techniques were rejected *in a
  specific role here*, not banned as ideas — see "Wrong paths / lessons".

Sources distilled: ChronoTron3/mnemonic commit bodies (`b08bb42` … `96c360c`),
`mnemonic-concept.md`, `mnemonic-impl-plan.md`, `mnemonic-degradation-colour-spec.md`,
`mnemonic_constants.h`. Where a "why" is not in any of those, it is marked
**rationale not recorded** rather than guessed.

---

## Hard guardrails (non-negotiable)

| # | Rule | Motivating incident / source |
|---|---|---|
| G1 | **The clean/dry signal is sacrosanct — never processed, filtered, saturated, or limited, and never attenuated in bypass.** Taming, limiting, saturation and EQ act **only on the WET path, before it is summed** with dry. | Bundle-wide rule (concept §K2, §K6, signal-chain notes; user hard rule). Regression `e3544a3`: the shell was scaling dry by K6 unconditionally, so in soft-bypass with K6 toward wet the *clean* path got attenuated → fixed with a `Module::Bypassed()` hook that ramps dry to unity. |
| G2 | **There is no hardware/true bypass on this pedal.** The buffered dry path + soft-bypass is the *only* clean-signal guarantee. Bypass is a **wet-trail** behaviour (gate the send, let the tail ring), never a hard mute of the output. | `mnemonic-concept.md` §FS2, §Output routing; `e3544a3` ("no hardware bypass on this pedal"). CONTROLS shell note: "K6 fully dry = effectively bypassed." |
| G3 | **The K4/K5 tone filter and its narrow-band makeup gain are the USER'S EQ. Do not alter them to solve an unrelated problem** (e.g. a hot self-oscillation). Fix such problems on a *separate* wet-only stage. | `96c360c` + the wet safety roll-off constant block: the piercing high-pitched self-osc (K5-narrow + makeup boost) is tamed by a dedicated de-esser on the wet output "WITHOUT touching the EQ, the makeup, or the feedback loop." |
| G4 | **Taming/limiting is dynamic and wet-only, and self-scaling where possible** — react to how loud the offending band actually is, not to an absolute ceiling, so the user's levels pass untouched when they're fine. | Wet HF safety roll-off (`MNEM_WET_LIMIT_*`): splits off the high band and rolls back *only* that band *only when hot*; low/mid feedback passes completely untouched. |
| G5 | **Varispeed identity: a single slewed read tap. NEVER crossfade between two read taps to change delay time.** The pitch slur *is* the effect; a clean-digital retime is the exact thing being avoided. | `mnemonic-concept.md` §Varispeed; impl-plan "Varispeed is not a demo crossfade"; confirmed `605368d` ("single fractional-delay tap, ReadFrac … K1 is literally the tape read distance"). |
| G6 | **In-loop vs out-of-loop placement is deliberate per stage** — don't move a stage across the feedback boundary casually. The K4/K5 tone filter, tape drive, and K3 degrade all live **IN** the loop, so repeats age through them (`Filter(x)` runs before `delay_.Write` in both paths). Freeze sums **outside** the loop (doesn't recirculate). The feedback tap and wet output both read the buffer (`ds`/`dd`), which is already filtered+coloured from prior laps. | Verified in current code (`mnemonic.h`, "K4/K5 tone — IN the loop"). History: filters were briefly moved OUT during the feedback-drown investigation, then returned IN once the drown was traced to the Mode-B ducker (`605368d`). |
| G7 | **The FS2 long-press panic must reach TRUE silence even at feedback ≥ 1 / self-oscillation** — the envelope throttles the *recirculation itself*, not just the output. It is the always-available escape and always also deletes the loop. | `211ed96`, concept §FS2, `MNEM_PANIC_*`. |
| G8 | **A short FS1 tap must never disturb a playing loop / running gesture.** Downpress is the universal event; press *length* disambiguates (tap vs sustained). Loop records to a scratch buffer, commits only once the press is confirmed a gesture. | `117e465` (unified hold-then-commit rework). |
| G9 | **Don't run `make` after pure tuning edits; one DSP change per build by default.** User flashes himself and watches the instrument profile. | Bundle workflow (impl-plan staging note; user memory). |

---

## Design decisions

### Footswitches & gestures

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **FS2 tap = bypass that rings out** (gate the send + loop send; tail decays per feedback; dry stays present) | A delay's tail dying instantly on bypass is wrong; want natural spillover | Gating only the *send* keeps the wet tail ringing while the clean path is untouched (G1/G2) | Hard mute of output (kills the musical spillover) | In force (`b08bb42`, `211ed96`) |
| **FS2 long-press = panic kill to silence** | Need an always-at-hand escape from runaway self-oscillation/loops | Envelope throttles the *recirculation*, so it collapses even at fb ≥ 1; then wipes the line + deletes loop; any tap re-engages over a short declick ramp | (none recorded) | In force (`211ed96`; G7) |
| **Bypass noise-duck: fade hiss WITH the trail** | In bypass, the ringing tail is fine but the medium hiss would leave a steady bed after the trail is gone | A trail-envelope follower ducks the degrade engine's *noise injection* to zero as the trail decays toward the engine noise floor (threshold = floor × margin, so it tracks K3). Tail itself is never gated (rings 100% clean). Disabled during play so inter-note tape hiss stays as character | Gating the tail (violates wet-trail identity); a static gate (wouldn't track K3's noise floor) | In force (`211ed96`, `MNEM_NGATE_*`) |
| **Unified FS1 hold-then-commit** — downpress = universal event, press length disambiguates (tap < 300 ms; sustained gesture > 450 ms; deadzone = no-op). SW1 latched at downpress. Taps timed from downpress, committed on release | Original design dedicated FS1 to the loop, so taps and loop/tape gestures couldn't coexist | Timing from downpress keeps tempo accuracy release-independent; commit-on-confirm means a short tap never disturbs a playing loop (G8) | Loop-dedicated FS1 (blocked taps); event on release only (loses tempo accuracy) | In force (`117e465`) |
| **Two-buffer loop, pointer-swap commit, REPLACE not overdub** | Loop must coexist with taps; commit must be atomic across the audio ISR | Scratch slab records from downpress; commits only when press becomes a gesture; buffer-full → `volatile` flag → auto record-end (vestige pattern). Second slab cost +3 MB SDRAM | Single-buffer overdub (would disturb a playing loop; not atomic) | In force v1 (`117e465`); overdub left as possible later mode |
| **SW1-UP tape spin-up** (hold → time↓/pitch↑ + feedback↑, slewed; release slews back) | Wanted a held pitch-dive gesture | Fits the varispeed topology — shortening the read tap glides pitch up cleanly | (see next row for the DOWN counterpart) | In force (`b08bb42`, `MNEM_GEST_*`) |
| **SW1-DOWN = freeze (replaced the original tape slow-down)** | The original SW1-DOWN was a tape *slow-down*; it **fought the varispeed topology** — lengthening the read tap while feedback climbs muddies rather than dives | Freeze delivers the intended "hold this" sustain without fighting the read-tap; grain-looped (2 half-overlapped full-Hann grains, wrap seam crossfaded not avoided), two-slab pointer-swap so re-freeze is clean, summed **outside** the feedback loop (parallel, doesn't age), latches | Tape slow-down (muddied instead of diving) | In force (`489bf02`; concept §SW1-DOWN) |

### Time / varispeed

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **Knob-time curve = pure exponential** over 50 ms – 1.5 s (`MNEM_TIME_CURVE = 1.0`) | The original `^2`-warped 20 ms–3 s taper had a dead CCW half (double-log) | Pure exp = equal knob degrees → equal *time ratio*, the natural delay-knob feel; noon ~275 ms, no dead zone | `^2` warp (dead CCW half); wider 20 ms–3 s range | In force (`7ebe8f0`) |
| **Delay-time de-jitter: pre-smooth `base_delay_` (12 ms) before the varispeed glide** | A single-pole glide fed a 10 ms-stepped / ADC-noisy target had velocity jumps each tick → ~100 Hz pitch lurch during playback | Cascading two poles makes read-tap *velocity* (= pitch) continuous | Single glide only (audible pitch lurch) | In force (`7ebe8f0`, `MNEM_TIME_SMOOTH_MS`) |
| **Tap-division set = full 11 noon-centred stops** (1/4 … 4/1, exact reciprocal mirror, noon = 1/1), matching Deluxe Memory Boy subdivisions | Needs a musical division set; noon must be the tap period | Reciprocal symmetry (CCW shorter / CW longer) + DMB subdivision set is the familiar analog-delay vocabulary; noon = 1/1 keeps WYSIWYG | Fewer/asymmetric stops (`489bf02` restored the full 11 after a reduction) | In force (`MNEM_DIV_RATIOS`, `b08bb42`, restored `489bf02`) |
| **Division changes glide (don't snap)** | Consistency with the varispeed identity | Gliding gives musical smears; snapping would be the clean-digital retime being avoided (G5) | Snap steps | In force (concept §K1; `b08bb42`) |
| **K1 seeds tempo before a tap exists** (`MNEM_TAP_INIT_RATIO = 1.0`) | Division modes are meaningless with no tapped quarter yet | Until a real tap is tracked, seed the quarter from K1's knob-time position × ratio (1.0 = knob-time value *is* the quarter), so division modes are usable immediately | Silence / no delay until first tap (unusable) | In force (`7d8cf14`, `MNEM_TAP_INIT_RATIO`) |
| **Division stops use knob hysteresis** (`MNEM_DIV_HYST`) | ADC noise would flicker between adjacent division stops | Boundary hysteresis; no flicker | Raw quantisation (flickers) | In force (impl-plan; `MNEM_DIV_HYST`) |

### Feedback / self-oscillation

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **Remove the Mode-B build-up ducker; in-loop tanh (TapeDrive) is the sole level safety** | The ported ducker ducked feedback on any normal-level repeat (thresh 0.55 / 40 ms atk); sweeping K1 moved the read tap into louder regions → slammed feedback down (the "feedback-drown" regression) | tanh alone bounds level and lets regeneration stay accurate; K1 sweeps no longer get ducked | Mode-B build-up ducker (caused the drown regression) | In force (`605368d`; note in `mnemonic_constants.h`) |
| **Analog-bloom feedback compression** — dedicated saturator (`FbSat`, `MNEM_FB_DRIVE`) on the recirculation only, after the feedback gain | A clean exponential decay is un-tape-like; want loud early repeats to pull toward the tail level and "bloom" | Fresh input (first repeat) stays present (sees only mild K3 drive); each recirculated lap compresses/warms → decay evens out and blooms; high K2 blooms into a compressed drone instead of clipping | Compressing the whole wet (would squash the fresh input too) | In force (`734989d`) |
| **Feedback controlled-decay: downward expander (`FbCtl`) on the feedback path** | Bloom alone rings forever; wanted pronounced *initial* repeats + a shorter tail (rhythmic echoes, good on bass) | Full feedback while loud; loop gain drops once the tail falls below the knee → tail decays faster. `AMT` = 0 is pure bloom (previous behaviour), higher = more controlled | Global shorter feedback (kills the strong initial repeats too) | In force (`96c360c`, `MNEM_FB_CTL_*`) |
| **Self-osc level tuning: `MNEM_FB_MAX` 1.15 → 1.30; `bbd_makeup` 0.7→1.25, `tape_makeup` 1.0→1.3** | Self-oscillation was too hard to reach; loop gain didn't match between BBD/Tape degrade | Higher ceiling makes oscillation easier; makeup values level-match the loop across degrade modes; BBD noise pulled back to compensate | (tuning, by ear) | In force (`96c360c`) |
| **Wet HF safety limiter (de-esser on wet output)** | K5-narrow + its makeup boost make a piercing high-pitched self-oscillation | Split off the high band (`MNEM_WET_LIMIT_SPLIT_HZ`) and roll back only that band when it's hot, dynamically — never touching the EQ/makeup/loop (G3, G4); low/mid passes untouched | A global limiter or lowering the makeup (would alter the user's EQ) | In force (`MNEM_WET_LIMIT_*`) |
| **Zipper fix: audio-rate (~5 ms) smoothing of K2 feedback, K3 drive, K4/K5 cutoffs + makeup** | Control tick is ~10 ms; stepped params zipper | SVF coeffs recompute per-sample from smoothed cutoffs (2 tanf via `CopyCoefFrom`); K1 already smoothed by the glide | (unsmoothed) | In force (`2774895`, `MNEM_SMOOTH_MS`). **Flagged not-smoothed:** BBD decimation params (K3-CCW, inherently steppy) |

### Tone filter (K4 / K5)

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **K4/K5 filters live IN the feedback loop** (repeats age through the tone filter; feedback + wet both tap the already-filtered buffer read) | Aging-tape character: each repeat should re-colour through the EQ | Matches the classic aging-tape default in the concept doc; the drown that once seemed filter-caused was actually the Mode-B ducker | Filters OUT of the loop — tried briefly during the drown investigation (`605368d`) then **reverted** once the drown was traced to the ducker | In force (verified in code: `Filter(x)` before `delay_.Write`) |
| **New tone topology: two 24 dB/oct filters — HP at `lo` + LP at `hi`.** K4 = tilt/center, K5 = narrow (shrinks hi↔lo toward the geometric center = band-limit by convergence) | Replaces the single sharp BP; wanted tilt + a controllable narrowing | Convergence gives band-limiting without a single sharp resonant peak; matches the K4-tilt / K5-emphasis brief | Single sharp band-pass | In force (`605368d`; demo in `mnemonic-filter-demo.html`) |
| **Per-stage Q default flat (`RES_Q = 0.707`)** | Resonance was a listening-review choice | 0.707 = flat/no resonance; documented that raising it (~1.6 → +9 dB) gives a nasal formant. `2774895` set flat as the shipping default | Resonant bump at both cutoffs (kept as a tuning lever, not the default) | In force (`2774895`) — note `605368d` briefly used per-stage Q 1.6; `2774895` flattened it |
| **Narrow-band center makeup gain** (`MNEM_FILT_MAKEUP_XS = 2.0`, capped +24 dB) | K5 fully CW (narrow) silenced the loop | Makeup restores level as the band narrows; XS = 2.0 over-compensates so narrow K5 sits *louder* while wide stays flat; capped to avoid runaway | No makeup (narrow K5 → silence) | In force (`2774895`). This makeup is part of the user's EQ (G3) |

### Degradation engine (K3) — see `mnemonic-degradation-colour-spec.md` for the full spec

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **Bipolar K3: BBD (CCW) ↔ clean center ↔ Tape (CW), one chain active at a time, NO crossfade through center** | The two signatures (compander breathing + aliasing vs. pitch wander + head bump) are contradictory; blending cancels both identities | ±0.03 dead-zone gives a reliable clean spot on a real pot; 10 ms fade + chain reset on switch; DC blocker; 32-sample control rate | Crossfading the two models (cancels both identities) | In force (`eb7643d`; spec §1, §8) |
| **BBD integer-divisor clock** (f_clk = fs / integer N) | The earlier exp-mapped f_clk produced a "decimator sizzle" | Integer divisor keeps the ZOH imaging on clean harmonic ratios, killing the sizzle | Continuous/exp f_clk mapping (sizzle) | In force (`969d371`) |
| **Split BBD anti-alias into IN_AA + REC** (input AA > 0.5 f_clk → fold-down *mid grit*; reconstruction filter darker → tames *high sizzle*); both clamped below Nyquist | A single AA filter couldn't both preserve the musical fold-down grit and tame harsh high sizzle; a >Nyquist biquad also blew up | Two filters with different corners separate "grit we want" from "sizzle we don't"; clamp fixes the biquad blowup | Single AA filter (can't do both) | In force (`96c360c`, `MNEMD_BBD_IN_AA` / `_REC`) |
| **Shallow AA filtering in BBD** | Fold-back aliasing *is* the effect | Steeper filtering removes the fold-back products that define BBD; validation sweeps a 3 kHz sine to confirm fold-back descends | Steep AA (removes the effect) | In force (spec §A.2, §8) |
| **REMOVE the compander from the BBD chain** | The compander restored the signal each lap → stopped the repeats degrading in feedback; also cost 2 `powf`/sample | Repeats now genuinely crumble: nonlinearity + ZOH + raw noise + loss accumulate in the loop (the desired cumulative decay). Frees CPU | Keep compander (fought cumulative feedback degradation; the spec §A.3/§8 had marked it "never remove" for the *standalone* BBD identity — that assumed a non-recirculating chain) | In force (`96c360c`) — **explicitly reverses the spec's "never remove compander"**; valid BBD theory, wrong for *this recirculating* use |
| **Approximate BBD "breathing" with a cheap 1-mult amplitude flicker** (`MNEMD_BBD_BREATH`, reusing the wow/OU mod block) | Breathing was the compander's job; compander now gone | A single amplitude flicker recreates the perceptual "breath" at ~1 mult vs 2 powf | Full compander for breathing (cost, and it fought decay) | In force (`96c360c`) |
| **Extended lo-fi extreme + more drive; aged noise + clock drift** | Wanted the CCW extreme to reach further into damage | (tuning, by ear) | — | In force (`969d371`) |
| **Tape: 2× oversampling, not ADAA1, on the saturator** | Spec §B.3 called for ADAA1 | `tanh(kx + ax²)` (asymmetric) has no closed-form antiderivative → ADAA1 not applicable | ADAA1 (no closed form for the asymmetric curve) | In force (`eb7643d`, integration note) |
| **Wow/flutter drives the MAIN varispeed tap** via a leaky speed→displacement integrator (cents-accurate), not a separate 40 ms read buffer | Spec §B.1 assumed a separate read buffer | Reusing the main read tap is cheaper and stays cents-accurate; leak = HP on the integrator so a DC speed offset can't drift the delay time | Separate 40 ms read buffer (extra buffer, redundant) | In force (`eb7643d`, `MNEM_FLUTTER_LEAK`, `MNEM_CENTS_TO_RATE`) |
| **Tape colour tuning: unity-gain sat, sooner/stronger sat + noise, capped dropouts, less noise / more saturation** | By-ear voicing of the tape chain | (tuning) | — | In force (`d71bdf3`, `01d2e5c`) |
| **Base tape drive always on** (`MNEM_TAPE_DRIVE`), independent of K3 | Brief: K3 is "which flavour of lo-fi", not an on/off gate | An always-on warmth means K4/K5 voice into a base tape drive; K3 layers extra colour | K3 as the only source of colour (would make noon fully clean/sterile) | In force (concept §K3; `b08bb42`) |

### Edge mode (SW2 DOWN)

| Decision | Context / problem | Why this over alternatives | Alternatives rejected (why) | Status |
|---|---|---|---|---|
| **SW2-DOWN evolved: capture-the-rhythm → Edge dual-tap → two fully independent delay lines** | Original ruling was "capture-the-rhythm multi-tap first"; then a single-buffer dual-tap; then rebuilt as two independent lines | Two independent lines (each own feedback) give a real primary + companion, not two taps sharing one buffer | Capture-the-rhythm (dropped, `605368d`); single-buffer two-read dual-tap (superseded by `489bf02`) | In force (`489bf02`) — supersedes the concept doc's M9 capture-the-rhythm ruling |
| **Edge primary = bit-identical to SW2-MID** (delay = quarter × K1 division, full K4/K5 + drive + K3) | Want Edge to *add* one thing, not be a separate voicing | Primary stays the known-good colored line; Edge "literally adds ONE thing" — a secondary line | A distinct primary voicing (would fork behaviour) | In force (`489bf02`) |
| **Edge secondary ratio = per-K1-stop companion** (`MNEM_EDGE_SECONDARY_RATIOS`, index-aligned) | Want a 3-layer interlock: your quarter playing + primary division + secondary | A companion ratio chosen per stop so the three layers interlock rhythmically | A fixed single ratio | In force (`489bf02`) |
| **Edge secondary voice = CLEAN telephone band-pass** (350 Hz–2.5 kHz, ~25% K4/K5 follow), no drive/shaper | See the secondary-line journey below — this is the endpoint | The mid band alone gives the separation from the primary; no shaper needed; light K4/K5 follow keeps tonal cohesion | Octave-up, Chebyshev drive, RMS-normalize (all rejected — see next section) | In force (`7d8cf14`) — `MNEM_SEC_DRIVE_ENABLE` kept as a false flag to re-enable the grit for A/B |
| **Edge loop-record ducks the old loop** | (recording behaviour) | Recording a new loop ducks the previous one | — | In force (`7d8cf14`) |
| **LED clock re-aligns on FS1 downpress** | Metronome should reset phase on the tap | Downpress is the timing reference (consistent with G8) | — | In force (`489bf02`) |

---

## Wrong paths / lessons

**Rejection here is not a permanent ban.** Each of these was rejected *for a specific
role in this module*; the underlying technique remains valid elsewhere.

| Thing built then removed / rejected | Why rejected **here** | Still valid because… |
|---|---|---|
| **Octave-up secondary voice (Edge)** — granular pitch shifter on the secondary input | Metallic **comb** artefact in this role | The pitch-shift technique isn't banned; it was metallic *in this specific voice*. `MNEM_EDGE_SECONDARY_RATIOS` still document an octave-up intent per stop |
| **Chebyshev telephone drive (T2–T5) on the secondary** | Too much / sat "on top" — piled grit onto an already-busy mix | Kept behind `MNEM_SEC_DRIVE_ENABLE = false` for A/B; flip true to re-enable. The clean band-pass won, but the grit path is intact |
| **RMS normalize on the secondary** | Part of the drive/Chebyshev path that was dropped for the clean band | (dropped with the drive path) |
| **Compander in the BBD chain** | Restored the signal each lap → repeats *stopped degrading* in feedback; also 2 powf/sample | Valid, textbook BBD theory (spec §A.3 marks it the primary BBD identifier and "never remove") — but that assumes a **non-recirculating** chain. In *this feedback loop* it fought the cumulative decay. Context-specific removal |
| **SW1-DOWN tape slow-down** | Fought the varispeed topology — lengthening the read tap while feedback climbs muddies rather than dives | The spin-up (SW1-UP) direction works fine; only the slow-down direction was the problem. Replaced by freeze |
| **Mode-B build-up ducker on feedback** | The "feedback-drown" regression: ducked on any normal-level repeat; K1 sweeps into louder regions slammed feedback down | Valid in Mode B's context; wrong here where K1 continuously moves the read tap through the buffer |
| **Filters OUT of the feedback loop** (brief experiment) | Tried while chasing the "feedback drown"; seemed like in-loop filtering was bleeding energy | The real cause was the Mode-B ducker, not filter placement. Once the ducker was removed, filters went **back IN** the loop (aging repeats, the current + intended state). So: out-of-loop was the reverted path, in-loop is in force |
| **`^2`-warped 20 ms–3 s knob-time taper** | Double-log → dead CCW half | Pure exponential is the correct delay-knob law |
| **Single-glide delay time (no pre-smooth)** | ~100 Hz pitch lurch on each 10 ms control tick | Cascading a 12 ms pre-smooth before the glide makes read-tap velocity (pitch) continuous |
| **Capture-the-rhythm / single-buffer Edge dual-tap** | Superseded by the two-independent-lines Edge | The idea was ruled then rebuilt; not a failure so much as an iteration |

---

## Known gaps (decisions referenced but rationale not fully recorded)

- **`MNEM_GLIDE_COEF` = 0.0007 (~30 ms)** — the specific value is by-ear; no commit
  records why 30 ms over another slur time. **Rationale not recorded** (it's the
  documented tuning lever for "syrupy vs snappy").
- **Freeze window = 400 ms, 2 grains** — the count/window are stated as design
  (COLA-smooth) but the *400 ms* figure itself is a bracket, not justified. **Rationale
  not recorded.**
- **Tap timing thresholds (300/450 ms, 3 s window)** — kept as separate constants "a
  deadzone may be wanted"; the exact values are flagged "still to tune" in both docs.
- **Self-osc makeup values** (`bbd_makeup` 1.25 / `tape_makeup` 1.3) — stated as
  "loop-gain match" but the target level itself is by-ear.
- **Doc map / `PROJECT.md`** — per AGENTS.md a doc-map entry should be added when a new
  doc is created; this file was **not** added there (per the user's "update docs only
  when told" rule). Flag for the user if a PROJECT.md entry is wanted.
</content>
</invoke>
