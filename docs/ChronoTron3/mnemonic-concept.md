# mnemonic — Concept (working spec)

Platform: Hothouse (Daisy Seed), ChronoTron3 bundle, **SW3 MIDDLE**. Foot-operated.
Two footswitches, two single-colour LEDs (blink states only). Switch 3 reserved
for coarse pedal-mode selection (shell). K6 dry/wet mix is shell-owned unless the
module takes over its own output (see *Output routing* — mnemonic likely will,
because bypass here is a wet-trail behaviour, not a hard mute).

> **Discovery snapshot, provisional.** House style from `dynamic-looper-concept.md`
> and `impulse resonator - armitage/IMPULSE_SYNTH_SPEC.md`: everything is flagged
> **locked / draft / open**. This is a working spec, not a manual — assignments
> will move once we iterate on hardware. Companion doc: `mnemonic-impl-plan.md`.

## Character

A tap-tempo **analog-style delay** — tape (EHX Memory Man / Deluxe Memory Boy)
and BBD (bucket-brigade) behaviour, **not** a clean digital delay. The defining
trait: **changing the delay time bends the pitch while you change it.** Turn the
time knob, tap a new tempo, or use a tape gesture, and the read head glides to
the new position — you hear the varispeed pitch slur, exactly like nudging a tape
reel or a BBD clock. Repeats colour and degrade as they recirculate (saturation,
filtering, warble / decimation), so the effect ages the sound the longer it
rings. On top of the delay sits a **hold / loop** function (EHX Hazarai-style)
and two **tape-gesture** footswitch moves (spin-up / slow-down).

Roles this module leans on, in one line each:

- **Varispeed core** — a single slewed read tap. Time changes are *never*
  crossfade-retimed (that is the clean-digital trick we are avoiding); the tap
  glides, and the glide *is* the pitch bend.
- **Recirculating colour** — filter → param-EQ → tape drive → degrade, all
  **inside the feedback loop** (proposed), so repeats darken / thin / crumble as
  they age. Feedback runs up into self-oscillation, tamed by tape saturation.
- **Hold / loop** — a clean-signal loop recorder that plays *into* the delay line
  in parallel with the live input.
- **Tape gestures** — footswitch-held time+feedback ramps with slewed
  return, for dive-bombs and runaway swells.

Ethos (bundle-wide): advanced experimental players; sonic mayhem over safety.
Self-oscillation, warble, aliasing and runaway loops are features. Build the
extremes first; safeguards and polish are a later joint stage.

## Controls (working draft)

| Control | Assignment | Status |
|---|---|---|
| K1 | Delay time (SW2 UP) / tap division (SW2 MID) | function locked · ranges draft |
| K2 | Feedback — 0 → self-oscillation (into tape saturation) | locked · safeguards draft |
| K3 | Degrade character — bipolar: BBD/digital (CCW) ↔ tape (CW) | function locked · impl draft |
| K4 | Tone tilt — bipolar: LPF (CCW) ↔ neutral (noon) ↔ HPF (CW) | locked · curve draft |
| K5 | Resonance / EQ emphasis at K4's corner (+ BPF blend?) | function draft · topology open |
| K6 | Dry/wet mix | locked (shell, unless module owns output) |
| SW1 | FS1 **hold** gesture select — UP spin-up · MID loop · DOWN slow-down | locked |
| SW2 | Time mode — UP knob-time · MID tap-tempo · DOWN rhythmic taps (TBD) | up/mid locked · down open |
| SW3 | Mode select (shell) | reserved |
| FS1 | Tap tempo (tap) · SW1 gesture (hold) | locked |
| FS2 | Bypass (tap = gate send, trail rings) · kill/clear (hold) | locked · pause-vs-restart draft |
| LED 1 | Delay-clock blink (tempo × division) | locked |
| LED 2 | Bypass / loop-present / loop-in-bypass state | locked · exact patterns draft |

---

## K1 — Delay time / tap division

Meaning follows **SW2**.

### SW2 UP — knob time (free)

K1 sets the delay time directly, continuous. Turning K1 **slews** the read tap to
the new time, so **the pitch bends while you turn** (varispeed). Fast turn = big
slur; slow turn = gentle drift. This is the analog-delay identity — see
*Varispeed*.

- **Proposed range: 20 ms → 3 s**, exponential taper (more resolution at short
  times). *(Range is `draft` — "ms to several seconds" per brief; exact min/max
  and taper TBD by ear. Buffer is sized to the max — see impl plan.)*

### SW2 MID — tap division

K1 no longer sets absolute time; it sets a **musical division of the tapped
quarter-note**, quantised to a fixed set of ratios, **noon = 1/1** (delay = tap
period). CCW shortens the delay (faster subdivisions); CW **mirrors** those
ratios (their reciprocals) to give delays *longer* than the tap.

**Tap-division ratios** (computed per brief; noon-centred, 11 stops):

| K1 position | Ratio (× quarter) | Decimal | Musical value |
|---|---|---|---|
| full CCW | **1/4** | 0.250 | sixteenth note |
| | **1/3** | 0.333 | eighth-note triplet |
| | **1/2** | 0.500 | eighth note |
| | **2/3** | 0.667 | quarter-note triplet |
| just CCW of noon | **3/4** | 0.750 | dotted eighth note |
| **noon** | **1/1** | 1.000 | **quarter note = tap period** |
| just CW of noon | **4/3** | 1.333 | half-note triplet |
| | **3/2** | 1.500 | dotted quarter note |
| | **2/1** | 2.000 | half note |
| | **3/1** | 3.000 | dotted half note |
| full CW | **4/1** | 4.000 | whole note |

The two halves are exact reciprocal mirrors: 3/4↔4/3, 2/3↔3/2, 1/2↔2/1,
1/3↔3/1, 1/4↔4/1. CCW = shorter/faster, CW = longer/slower, symmetric about the
tapped quarter at noon.

- Changing division **also slews** (pitch-bends) to the new time — consistent
  with the varispeed identity. *(Open: whether division steps snap or glide;
  proposed **glide**, for musical smears. Flag `draft`.)*
- With the ×4 CW extreme, a slow tap can drive the delay to several seconds —
  the delay buffer is sized to the worst case (tap-max × 4). See impl plan.

---

## K2 — Feedback (0 → oscillation)

Feedback gain from silence (fully CCW) to **self-oscillation** (fully CW). The
loop runs into **tape saturation**, so oscillation self-limits into a warm,
compressing drone rather than a digital scream. Model directly on NitroTron3
Mode B's feedback bus (`FB_SAT_DRIVE`, `FEEDBACK_MAX`, tanh + duckers).

- **Loudness safeguards** (draft, borrow from Mode B):
  - **tanh saturator** in the loop bounds peak level and adds the tape
    compression as feedback climbs.
  - Optional **build-up ducker** (slow env on the recirculating signal pulls the
    loop gain down as it accumulates) so oscillation *simmers* instead of
    clipping the converters.
  - **HARD RULE:** only the wet/feedback path is ever limited. The dry and the
    summed output are sacrosanct — never touched.
- Feedback ceiling is a constant (analogous to `FEEDBACK_MAX = 2.0`) so oscillation
  is reachable but controlled.

---

## K3 — Degrade character (bipolar)

Clean(est) at noon; degradation flavour increases toward either extreme. Note the
**base tape drive is always in the chain** (see *Signal chain*) — K3 is not a
"turn on the character" gate, it is a **which-flavour-of-lo-fi** dial layered on
top of an already-coloured delay.

- **CW — tape:** more tape saturation / coloration, plus **tape warble** (wow &
  flutter — a slow+fast pitch-modulation of the read tap) and progressive HF
  loss / degradation the further CW you go. Musical, "melting" repeats.
- **CCW — BBD / digital:** bucket-brigade character — **sample-rate reduction**
  (decimation) and companding-style artefacts, moving toward digital glitch at
  the extreme. Deliberately **rounder / less harsh than a pure bitcrusher** —
  closer to BBD aliasing than to hard bit-depth crunch. (Bit-depth reduction is
  optional/secondary; if used, keep it gentle.)

Reuse candidates: `bitcrush.h` (decimation), the Mode B decimator/glitch zones,
a small wow/flutter LFO pair driving the read-tap offset.

---

## K4 — Tone tilt (bipolar filter)

A **tilt filter**, neutral at noon:

- **CCW — LPF:** rolls off highs; the delay sits *under* the mix, dark tape.
- **noon — neutral:** flat.
- **CW — HPF:** rolls off lows; the delay thins out, sits *above* the bass.

Guardrail intent (from brief): **K4 sets where the delay line lives** in the
spectrum — highs rolled off, or bass reduced.

- Proposed: single state-variable / one-pole pair crossfaded, or a shelving-tilt
  topology. *(Topology `draft`.)*

---

## K5 — Resonance / EQ emphasis

A **peak / resonance at K4's corner frequency**, post-tilt, to emphasise a band
and make the delay **sit more pronounced in the mix**. May do double duty as a
**blend toward a band-pass** at the same frequency (so K4+K5 together sweep from
tilt to focused BPF). Topology is explicitly **up for discussion**.

- The filter+EQ **feeds the tape drive** — so the emphasised band is what gets
  saturated hardest. This is intentional voicing, not just tone.
- **Base tape character is always on**, independent of K3 (per brief): K4/K5 find
  a good *base* tone into a base tape drive; K3 sets *how lo-fi* on top.

### EQ placement — the key experiment

Two options, both to be A/B'd on hardware (proposed default in **bold**):

- **In the feedback loop (proposed default).** Filter+EQ applied once per
  recirculation → repeats progressively darken/thin/focus as they age (classic
  tape/BBD). Matches "the filter goes into the tape drive" and the
  feedback→oscillation→saturation chain.
- **Post-feedback (static tone).** One fixed EQ on the wet output; every repeat
  identical. Cleaner, more "mixer EQ" than "aging tape".

*(Placement `open` — build a compile-time switch or a quick toggle so we can hear
both. Very likely we keep feedback-loop placement, but confirm by ear.)*

---

## K6 — Dry/wet mix

Shell-owned equal-power mix **unless mnemonic owns its output** (likely — see
*Output routing / bypass*). If the module owns output, K6 becomes the wet blend
inside the module and the bypass trail behaviour is handled there. **The dry path
is never processed or limited** (sacrosanct).

---

## Switches

### SW1 — FS1 hold-gesture select (locked)

Selects what an FS1 **hold** does (FS1 **tap** is always tap-tempo — see FS1):

- **UP — tape spin-up.** Hold FS1 → delay time **decreases** (pitch glides *up*)
  and feedback **increases**, ramping via a slewed envelope for as long as held.
  Release → slews back to the K1/K2 settings.
- **MID — loop (hold/loop function).** Press = start loop record; release = stop
  record and start loop playback (Hazarai-style). See *Hold / loop*.
- **DOWN — tape slow-down.** Same as UP but delay time **increases** (pitch
  glides *down*) with feedback increasing; slewed return on release.

Spin-up and slow-down almost certainly need **different feedback-ramp tunings** —
separate constants for UP vs DOWN (per brief).

### SW2 — Time mode (up/mid locked · down open)

- **UP — knob time:** K1 = absolute delay time.
- **MID — tap tempo:** K1 = division; FS1 taps set the tempo.
- **DOWN — rhythmic taps.** **Ruling: build "capture-the-rhythm" first.** The
  tap gesture captures the *rhythm between taps* (not just the average tempo)
  into a short **multi-tap / pattern** — tap a dotted or syncopated figure and the
  delay replays it. Two alternatives are kept **in evidence** (not built yet, may
  layer in later as SW2-DOWN variants or a sub-selection):
  - **Euclidean rhythms** — K1 (or a knob) selects a Euclidean pattern from a set
    of preset ratios / (pulses, steps) pairs spread across the knob, scaled to
    the tapped tempo.
  - **"The Edge" style multi-tap** — a fixed rhythmic multi-tap (e.g.
    dotted-eighth + eighth) scaled to tempo, as a simple fallback.

### SW3 — Mode select (shell, reserved)

---

## Footswitches

### FS1 — unified hold-then-commit

**The downpress is the universal event; press *length* disambiguates it — and
short taps always mean tempo/rhythm, in every SW1 position.** SW1 only selects
which *sustained* (long-hold) gesture you get. This is the key design: the loop
and tape gestures never block the taps.

| Release timing | What it is |
|---|---|
| released **before** `MNEM_TAP_RELEASE_MS` (≈300 ms) | **Tap** → tempo (SW2 MID) / rhythm (SW2 DOWN) |
| held **past** `MNEM_LONGPRESS_MS` (≈450 ms) | **Sustained gesture** (SW1-latched): MID = loop record · UP = spin-up · DOWN = slow-down |
| released **in the deadzone** between | no-op (ambiguous; ignored) |

- **Downpress is the timing reference** for taps even though the tap *commits* on
  release: the interval is measured downpress-to-downpress, so tempo accuracy is
  unaffected — only the moment the new value is *applied* waits for release.
  This "hold a provisional value, commit when the gesture is confirmed" pattern
  is the same one the loop uses (scratch buffer → commit).
- **SW1 is latched at downpress** — flipping SW1 mid-press does not change the
  gesture; the new mode takes effect on the next press.

**Tap tempo (SW2 = MID):** two taps set it immediately; further taps refine via
the **median** of recent intervals. A **listening window** groups taps; a gap
longer than the window starts fresh. **Slowest-tap clamp** ≈ 2 s (fastest is not
a concern); with K1 ×4 that reaches the multi-second ceiling.

**No conflict with the loop.** In SW1 = MID you get *both*: short taps set
tempo/rhythm, a long hold records a loop (into a scratch buffer, committed only
when the hold crosses the threshold — see below). A short tap therefore never
disturbs a loop already playing, and it still registers as a tempo/rhythm tap.

### FS2 — bypass / kill (locked; pause-vs-restart draft)

- **Tap — bypass toggle.** Bypass here does **not** kill the delay trail. It
  **gates the send into the delay** (no new input recirculates) and **gates the
  loop send** (loop stops). The existing tail **rings out and decays** per the
  feedback setting. Dry stays clean and present (sacrosanct). This gives natural
  spillover on bypass.
- **Hold — kill switch.** Instantly **kills the delay line** (clears the buffer /
  silences the tail) **and deletes any recorded loop**.

Loop-vs-bypass detail (brief flags as "do the simple thing first"): a recorded
loop is **paused** by bypass and **resumes** on un-bypass — a simple signal gate,
**not** stop/restart. The long-press kill is the only thing that *deletes* the
loop. *(Pause/resume chosen as simplest; `draft`.)*

---

## Hold / loop function (SW1 = MID)

EHX Hazarai-inspired, recorded from the **clean** signal. Built on a **two-buffer
scratch → commit** model so a short tap can never disturb the loop that's playing:

- **Press FS1** → recording into a **scratch buffer** starts immediately on the
  down-press (accurate, count-in-free start point).
- **Held past `MNEM_LONGPRESS_MS`** → the press is confirmed a loop gesture.
- **Release FS1** → **commit**: the scratch buffer is swapped in as the live loop
  (near-free pointer swap) and playback starts. **REPLACE, not overdub** — a new
  commit overwrites the previous loop.
- **Released before the threshold** → it was a tap; the scratch is discarded and
  the loop already playing is untouched.
- **Buffer full** (16 s ceiling) → treated as an automatic record-end (commit),
  vestige-style, via a `volatile` flag the control loop consumes.
- The committed loop **plays back *into* the delay line in parallel with the live
  clean input** — feeding the delay/feedback/colour chain just like playing does.
- Bypass (FS2 tap) pauses the loop; un-bypass resumes it. FS2 hold (kill) deletes
  it.

*(v1: single loop, REPLACE on each commit — no overdub / sound-on-sound. Open:
max loop length final value; whether to add overdub later. `draft`.)*

---

## Varispeed (the sonic identity)

Every delay-time change — K1 turn, new tap, or tape gesture — moves a **single
read tap** that is **slewed** toward its target. Because the tap glides rather
than jumping, the read rate momentarily differs from the write rate and the pitch
bends (Doppler / varispeed), exactly like tape or a BBD clock. **We deliberately
do not crossfade between two read taps** (the clean-digital "no pitch change"
trick) — the pitch slur *is* the effect.

- A per-time-change **glide rate** constant sets how tape-like the slur is
  (fast slew = snappy, near-instant repitch; slow slew = long syrupy dives).
  Likely one rate for knob/tap moves and separate, longer ramps for the SW1 tape
  gestures.
- Tape **wow/flutter** (K3-CW) is a small continuous modulation of the same tap
  offset — same mechanism, tiny amounts.

---

## Signal chain (proposed)

```
                        ┌────────────────────────── feedback loop ──────────────────────────┐
                        │                                                                    │
 in ──►[send gate]──►( + )──►[K4 tilt]──►[K5 peak/EQ]──►[tape drive]──►[K3 degrade]──►[delay write]
        (bypass)      ▲                                 (always on)   tape warble /            │
 loop ──►[loop play]──┘                                                BBD decimate            │
        (into delay, parallel with dry)                                                        ▼
                                                                          [delay read: slewed varispeed tap]
                        ┌──────────────────────────────────────────────────────────────────────┤
                     [×K2 feedback]◄──[tanh saturate]◄──[build-up ducker]◄─────────────────────┘  │
                                                                                                    │
 dry ───────────────────────────────────────────────────────────────────────────────►( mix K6 )──┴─► out
 wet (delay read) ─────────────────────────────────────────────────────────────────►( mix K6 )
```

Notes:
- Filter → EQ → tape drive → degrade sit **inside** the loop (proposed) → repeats
  age. The post-feedback alternative is the A/B experiment (K5 section).
- The loop layer sums into the **delay input**, not the output — it feeds the
  whole colour+feedback chain.
- Dry is never filtered, saturated or limited.

---

## LEDs

- **LED 1 — delay clock.** Blinks at the effective delay timing (tempo ×
  division). In knob-time mode it still blinks at the current delay time, at a
  quarter-note-equivalent rate. Visual metronome of the current repeat rate.
- **LED 2 — bypass / loop state.**
  - Active vs bypassed: solid / off (base indication).
  - **Loop recorded present:** rapid flash (a loop is loaded and armed).
  - **Loop running while bypassed:** dimmed flash (loop alive but send gated).
  - *(Exact patterns `draft` — reconcile against the vestige LED vocabulary so
    the two modules read consistently.)*

---

## Open items / decisions to collect

- **SW2 DOWN** — **ruled: capture-the-rhythm multi-tap first.** Euclidean-preset
  and "The Edge" fixed multi-tap kept in evidence as later variants.
- **K1 knob-time range + taper** (proposed 20 ms–3 s exp) and **tap-max interval**
  (proposed ~2 s) → together set the delay-buffer size.
- **Division steps snap or glide** (proposed glide).
- **K5 topology** — pure resonant peak vs peak+BPF-blend; and **EQ placement**
  in-loop (proposed) vs post-loop. Build both, decide by ear.
- **Feedback safeguards** — which of Mode B's tanh / build-up ducker / ceiling to
  port, and their tunings.
- **Tape-gesture ramp tunings** — separate feedback ramps + glide rates for SW1
  UP vs DOWN.
- **FS1 model** — **ruled & built: unified hold-then-commit.** Downpress is the
  universal event; short tap = tempo/rhythm (all SW1 positions), long hold =
  SW1-latched sustained gesture. Two thresholds with a deadzone
  (`MNEM_TAP_RELEASE_MS` / `MNEM_LONGPRESS_MS`) — values still to tune.
- **Loop** — **ruled & built: two-buffer scratch→commit, REPLACE not overdub;**
  buffer-full = auto record-end. Bypass pauses / un-bypass resumes; kill deletes.
  Open: final max length; possible later overdub mode.
- **Output ownership** — **resolved: no `OwnsOutput()`.** The shell K6 equal-power
  mix serves; the wet-trail bypass works because the wet buffer carries the
  decaying tail and the dry stays sacrosanct.
- **LED patterns** — final blink vocabulary, consistent with vestige.
