# ChronoTron3 — Control Layout (discovery)

> **Discovery snapshot, provisional.** This mirrors what the current firmware
> actually does — not the manual or README (those come after we've iterated).
> Assignments will move. Source of truth: `pedals/chronotron3/`.

## Shell — applies in every mode

| Control | Function |
|---|---|
| **SW3** | **Mode select** — UP = *vestige* · MIDDLE = *mnemonic* · DOWN = *sprawl* (granular delay; replaced *armitage* 2026-09-14) |
| **K6** | **Dry/wet mix** — equal-power (mnemonic, sprawl). *vestige overrides it as looper volume — see below.* |
| **Both footswitches held ~2 s** | Enter Daisy bootloader (DFU). The only entry path (sealed pedal). |

The mode owns everything else — including both footswitches. There is no
dedicated bypass footswitch yet (K6 fully dry = effectively bypassed).

---

## vestige — SW3 UP · grain looper / freeze

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Voice count / topology | Padded noon = 1 (parallel) · CCW → up to 6 voiced (FIFO, auto age-fade fixed by count) · CW → frippertronics: just past noon = shortest decay (~1 repeat) · fully CW = infinite sustain. Live; never resets playback |
| KNOB 2 | Auto-capture threshold | Used in continuous-auto capture; spare in manual |
| KNOB 3 | Scan / freeze | **CCW** = normal forward loop · **CCW→noon** = backward auto-scrub decelerating to a **halt at noon** · **noon→CW** = frozen, with the freeze point sweeping **live** across the **whole buffer** — beginning (noon) to a grain-scan-range in from the **END** (full CW). Grain diffusion stays alive throughout |
| KNOB 4 | Texture | Bipolar, clean at noon: CCW tape saturation (gain-compensated) · CW decimation → digital glitch |
| KNOB 5 | Loop fade in/out | CCW = instant → CW = max, **both as real bounded durations on one scale** (no runaway tail). **Attack** = convex swell (slow start → full) over `ATTACK_MAX_S`; **release** = concave dies-away over `RELEASE_MAX_S`. Default 3 s : 3 s (1:1); ratio is set by those two constants. Applied on loop start / stop / mute |
| KNOB 6 | Looper volume | *vestige owns its output* — additive: `out = dry + K6·looper`. CCW = silent · noon = unity · CW = boost (+6 dB). The clean (dry) is routed by SW2, not by K6 |
| SWITCH 1 | Capture mode | UP = manual (hold-record) · MIDDLE = continuous-auto · DOWN = → manual (TBD) |
| SWITCH 2 | Dry (clean) routing | UP = clean always on (loop plays on top) · MIDDLE = clean on, but cut while recording or auto-armed · DOWN = clean off (loop only) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Stop | Tap = mute/pause (material kept; fades via K5) · Hold = clear all |
| FOOTSWITCH 2 | Engage | Manual: hold = record, release = set loop end · Auto: record-arm toggle · from muted: resume |
| LED 1 | Play state | solid = playing · slow-blink = muted · off |
| LED 2 | Record state | solid = recording · fast-blink = auto-armed · off |

---

## mnemonic — SW3 MIDDLE · tap-tempo tape/BBD delay

Spec: `mnemonic-concept.md` + `mnemonic-impl-plan.md`.

> **Knob layout changed 2026-09-14** to align with sprawl: K2 = time, K4 =
> character, K5 = feedback now mean the same thing on both modules (and K4 =
> texture on vestige). The physical knobs did not move, so a saved pedal
> position means something different than it used to — in particular K1 is now
> the tone tilt and K3 the band-limit, both of which sit INSIDE the feedback
> loop and will starve the repeats at their extremes.

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Tone tilt | Bipolar, flat at noon (cut-only): **CCW** LPF (rolls off highs, dark) · **CW** HPF (rolls off lows, thin). Sets where the delay sits. **In the feedback loop** — at the extremes it removes most of the loop's energy, so the repeats die off much faster |
| KNOB 2 | Delay time / division | **SW2 UP** = absolute delay time (exp 50 ms–1.5 s), turning it **glides** = varispeed pitch bend. **SW2 MID** = tap division, 11 stops, noon = 1/1 (quarter = tap): CCW 3/4·2/3·1/2·1/3·1/4 shorter · CW 4/3·3/2·2/1·3/1·4/1 longer. **SW2 DOWN** = Edge: same division as MID on the primary line, plus a per-stop companion ratio on the secondary line |
| KNOB 3 | Narrow (band-limit) | Shrinks the gap between the HP and LP cutoffs toward the geometric centre — band-limit by convergence (not a single resonant peak); a centre-gain makeup keeps a narrow setting from dropping out. **In the feedback loop**, so it ages the repeats — and, like K1, a hard setting starves them |
| KNOB 4 | Degrade | Bipolar, clean at noon: **CCW** BBD (sample-rate decimation + gentle crush + rounding) · **CW** tape (extra drive + wow/flutter warble + progressive HF loss) |
| KNOB 5 | Reverb / Feedback | **Bipolar**, ±5% deadzone at noon = neither. **CW** = feedback, 0 → bounded self-oscillation, into the always-on tape saturation + build-up ducker. **CCW** = reverb: blend and decay both open with travel, *and* the feedback ramp is mirrored from the CW side but **clamped** below oscillation, so the wash has decaying repeats underneath it. The two sides are NOT exclusive here (unlike sprawl, where the grain engine self-sustains and CCW is feedback-free) |
| KNOB 6 | Dry/wet mix | *(shell — equal-power. Dry is never processed/limited)* |
| SWITCH 1 | FS1 **hold** gesture | **UP** = tape spin-up (hold → time↓/pitch↑ + feedback↑, slewed; release slews back) · **MIDDLE** = hold/loop (press record, release play) · **DOWN** = freeze (hold captures the last ~400 ms of clean input; release commits + grain-loops it as a sustained parallel voice, summed to wet outside the feedback loop; latches until re-frozen or FS2 panic) |
| SWITCH 2 | Time mode | **UP** = knob time · **MIDDLE** = tap tempo (FS1 taps) · **DOWN** = Edge — two independent delay lines: MID primary + a clean lo-fi telephone secondary at a per-stop companion ratio |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Tap / gesture (hold-then-commit) | **Short tap** (release < ~300 ms) = tempo/rhythm tap — works in *every* SW1 position · **Long hold** (> ~450 ms) = the SW1-latched sustained gesture: MID loop record · UP spin-up · DOWN freeze. Downpress is the timing reference; deadzone between = no-op |
| FOOTSWITCH 2 | Bypass / panic | **Tap** = bypass toggle — gates the send + loop, but the delay **trail rings out** naturally (in bypass the tape/BBD hiss ducks away as the trail decays, so it doesn't leave a noise bed) · **Long-press** = panic — *always* drops into bypass and kills everything: loop deleted, feedback + tail spun down to true silence (click-free, even during self-oscillation), delay line wiped. The always-at-hand escape |
| LED 1 | Delay clock | Blinks at the effective delay timing (tempo × division) |
| LED 2 | Bypass / loop state | Active = solid · bypassed = off · recording = solid · loop armed = rapid flash · loop running while bypassed = dim slow flash |

**Loop (SW1 = MID).** Recorded from the clean signal; plays back *into* the delay
line in parallel with the live input. A short tap sets tempo/rhythm and never
disturbs a playing loop; a long hold records into a scratch buffer and **commits
on release** (pointer-swap, **replaces** the old loop — no overdub). Recording
starts on down-press for an accurate start; buffer-full auto-ends. Bypass pauses
the loop, panic (FS2 long-press) deletes it.

---

## sprawl — SW3 DOWN · granular delay / glitch texture

Ported 1:1 from NitroTron3's Mode B; port contract and deviations in
`sprawl-port-plan.md`. The control/DSP seam is `DeriveParams()` — re-assigning
a control is an edit there and nowhere else.

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Pitch | Meaning follows SW2. **UP** = fixed interval, ±12 semitones. **MIDDLE** = harmonic-cloud pick, K1 spans the ±36-semitone scan. **DOWN** = Bode SSB frequency shifter on the wet bus, bipolar with a ±2% deadzone, exponential to ±1 kHz; grain pitch is forced to unison so K1 isn't doing two jobs |
| KNOB 2 | Buffer — length + direction | **Noon (±6%)** = live: the grain engine runs on the write head, near-zero latency. Off noon the **magnitude** is buffer depth (100 ms → 8 s) and timescale, the **sign** is playback direction: CW forward, CCW backward. Length is overridden by a tap until K2 is moved again |
| KNOB 3 | Character | Bipolar, single coherent stream at noon (±6%). **CCW** = cloud: grains stretch ~0.3 → 2 s with the emission rate falling, a long slow smear on the deep buffer. **CW** = glitch: per-grain length variation (audio-rate stutter buzzes), scatter, jitter, random reverse, note-on bursts |
| KNOB 4 | Texture amount | Meaning follows SW1; bipolar (clean at noon) for UP (decimate/fold) and MIDDLE (BBD ← CCW · clean at noon · tape → CW), unipolar for DOWN |
| KNOB 5 | Reverb / Feedback | **Bipolar**, ±5% deadzone at noon. **CCW** = Clouds reverb blend, 0 → 1, decay fixed. **CW** = ring-buffer feedback, tanh-saturated with build-up and on-play duckers so it self-limits into a controlled drone. Mutually exclusive: CCW is feedback-free, which works here because the grain engine keeps generating on its own |
| KNOB 6 | Dry/wet mix | *(shell — equal-power)* |
| SWITCH 1 | Texture mode | **UP** = decimator (K4 CCW) / wavefolder (K4 CW) · **MIDDLE** = tape/BBD colour — the shared degrade engine on bipolar K4, vestige's post-stage-warble adaptation; replaced the event-driven glitch 2026-09-17. **CCW = BBD**: decimation + crush + rounding, and the decimator clock is deliberately UNSTABLE — a continuous random walk on the (fractional) clock plus Poisson "slip" events that jam it at another rate for a while and snap back. Slips start only past ~9 o'clock and their length scales with the current echo time, so the wobble keeps its character as you sweep K2 or tap a new tempo. Random timing throughout: it scales to the music's time-world, it never locks to the grid. **CW = tape**: drive, wow/flutter, HF loss, head bump, hiss and dropouts, on a travel extended past the shared engine's stock endpoint so the far end keeps going · **DOWN** = ringmod (K4 below 30% = tremolo 1–15 Hz, above = bell partials with a keytracked LPF) |
| SWITCH 2 | Harmony source | **UP** = fixed interval · **MIDDLE** = harmonic cloud (grains scatter across nearby harmonics) · **DOWN** = Bode SSB frequency shifter, inside the feedback loop so each pass cascades the shift |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 | Tap tempo / Freeze | **Tap** (release < ~300 ms) = tap tempo: one interval **is** the echo time, no subdivisions. Solved against the real read-back depth per block, so it stays accurate as K3 moves; exact 100 ms – 7 s at K3 noon/CW, upper end limited in deep cloud by the grain size itself. **Hold** (≥ 450 ms) = freeze toggle: the ring stops being written, so the grains keep playing the held material. Released in between = no-op |
| FOOTSWITCH 2 | Bypass / Panic | **Tap** = trail bypass: the input send is gated but the wet keeps running, feedback included, so a K5-CW drone rings on through bypass. The clean path is never touched. **Hold** (≥ 450 ms) = panic: lands in bypass and fades recirculation *and* wet output to true silence, then wipes the ring and stops the grain voices; also releases a freeze |
| LED 1 | Echo clock | One flash per echo (the read-back depth, not the buffer span). Re-syncs on each tap. **Inverted while frozen** — mostly lit with a brief gap on the beat |
| LED 2 | State | Solid = active · off = bypassed |

**SW1-MIDDLE voicing lives in `sprawl_constants.h`.** The shared degrade engine
is voiced per host through setters that default to 1, so mnemonic and vestige
are untouched by any of it: `SPRAWL_BBD_LEVEL` / `SPRAWL_TAPE_LEVEL` (output
level per side), `SPRAWL_BBD_FOLD_SCALE` + `SPRAWL_BBD_LPF_SCALE` (BBD
brightness — the LPF one raises the FLOOR only, see G10 and the Nyquist note in
the engine), `SPRAWL_TAPE_DRIVE_SCALE` (grit, level-compensated) and
`SPRAWL_TAPE_DEPTH_SCALE` (extends the whole CW travel past stock).
Instability: `SPRAWL_BBD_SLIP` (event rate), `SPRAWL_BBD_DRIFT` (continuous
clock walk) and `SPRAWL_BBD_SLIP_SYNC` (0 = fixed-ms event length, 1 = scaled
to the echo time).

**Freeze side-effect worth knowing.** With the write head parked, consecutive
grains replay the same slice. At K3-CW the per-grain scatter shuffles chunks out
of the held buffer, so it stays varied; at K3 noon/CCW scatter is zero, so
overlapping identical copies comb — a static, metallic hold. That is inherent to
a literal freeze of this engine (see `sprawl-port-plan.md`); a scanning read
offset would change it, and is deliberately not built.

---

## ~~Armitage~~ — ARCHIVED, no longer in the build

> Removed from the bundle 2026-09-14 (`6e0afbb`); **sprawl** (granular delay,
> ported from NitroTron3 Mode B) holds SW3 DOWN now. The table below is kept
> as the record of armitage's last control layout — see
> `impulse resonator - armitage/ARMITAGE_ARCHIVED.md`. **sprawl's own control
> table is deliberately not written here yet: its UI phase (knob/switch
> re-assignment) is the next work.** Until then the as-built surface is the
> header comment of `pedals/chronotron3/modules/sprawl.h`.

### armitage's last layout (historical)

| Control | Function | Notes |
|---|---|---|
| KNOB 1 | Register | Bipolar: CCW sub · noon unison · CW upper (±1 octave, continuous) |
| KNOB 2 | Damping | Decay time (T60), short/plucky → long drone. Primary timbre |
| KNOB 3 | Structure | Comb: allpass dispersion · Modal: inharmonic partial spread |
| KNOB 4 | Asymmetry | Excitation enrichment, 0 → 1.0 (full rectification; fills spectral gaps). Sole conditioning control |
| KNOB 5 | Filter envelope | Gated AR → 4-pole 24 dB/oct non-resonant LP; closed = muted. Fast onset detect (hysteresis crossing) retriggers the sweep per note; sustains while the note rings; releases on note-off. Bipolar: noon = snappy attack+release · CCW = longer attack · CW = longer release. Perceived decay = release, decoupled from K2 (CCW cuts the tail fast, CW lets it ring out) |
| KNOB 6 | Dry/wet mix | *(shell)* |
| SWITCH 1 | Unused | Free — modal core dropped, comb is the keeper; reassignment TBD |
| SWITCH 2 | **Note-set behaviour (A/B)** | UP = fixed dense bank (25-note semitone comb) · MIDDLE = mono-tracked voice (follows played pitch) · DOWN = key-quantised multivoice (arpeggiate to stack an in-key chord) |
| SWITCH 3 | Mode select | *(shell)* |
| FOOTSWITCH 1 / 2 | Unused | (bootloader gesture still reserved) |
| LED 1 | Input activity | brightness follows what you play (play indicator) |
| LED 2 | Filter envelope | openness of the K5 filter |

**How to smoketest each SW2 behaviour** (play into the pedal — the resonators are
*excited* by your signal):

- **UP fixed bank** — rings to anything you play, incl. chords. Judge the core
  itself: timbre, K2 damping range, K1 register, K3 structure, K4 asymmetry,
  drone character.
- **MIDDLE mono** — play single notes/lines; it tunes to the pitch and rings.
  Judge tracking across the range, register, the "voice" feel. (Chords → picks
  one pitch — that's expected; poly detection is deferred.)
- **DOWN key-quant** — play an arpeggio; distinct in-key notes stack into a
  chord (key A / minor pentatonic, a constant for now). Judge the pseudo-poly
  feel and whether the key/scale defaults work.

Deferred (needs the offline harness): true polyphonic *chord* detection.
