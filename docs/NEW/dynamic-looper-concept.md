# Dynamic Looper — Concept (working spec)

Platform: Hothouse (Daisy Seed). Foot-operated. Two footswitches, two single-colour LEDs (blink states only). Switch 3 reserved for coarse pedal-mode selection. All loop playback is grain-based.

## Controls (working draft)

Hothouse surface: 6 knobs, 3 three-way toggles, 2 footswitches, 2 single-colour LEDs. Knob placements below are a temporary working layout (only K6 = mix was ever fixed); expect them to move. Status flags locked vs draft vs open.

| Control | Assignment | Status |
|---|---|---|
| Switch 1 | Capture mode — up: manual (hold-record) · mid: continuous-auto · down: TBD | up/mid locked · down open |
| Switch 2 | Unassigned — interaction axis dissolved (ducking dropped; layering lives on K1) | free / open |
| Switch 3 | Coarse pedal-mode select | reserved (out of scope here) |
| FS2 (right) | Main engage (behaviour below) | locked |
| FS1 (left) | Stop — tap: mute/pause · hold: clear | locked |
| K1 | Voice count / topology — 1 = parallel · 2–6 voiced · frippertronics region | function agreed · geometry + selection open |
| K2 | Unassigned — candidate: envelope sensitivity / threshold | open |
| K3 | Smoothness — looper↔freeze grain macro | function locked |
| K4 | Texture — bipolar: analogue degrade ↔ digital degrade | draft |
| K5 | Fade / decay — fade slope (voiced) / decay coeff (frippertronics) | proposed · not locked |
| K6 | Dry/wet mix | locked |
| LEDs ×2 | Single-colour, blink states only — state/blink mapping | open |

**FS2 (right) — main engage**
- Manual mode: records while held; release sets loop endpoint. Short press = very short loop.
- Continuous-auto mode: record-arm toggle.
- Manual + layering (K1 ≥ 2 voices): hold-to-record doubles as the layer-commit gesture. The FS2 overload is intentional.

**FS1 (left) — stop**
- Tap: mute/pause all loop playback (material retained; FS2 re-arm resumes the same loops).
- Hold: clear.

## Memory models

Two models, selected by voice count (K1):

- **Voiced (poly-looping):** N independent buffers, independent lengths. Finite N is a musical choice (defined N creates graspable phasing/rhythmic cycles). Voices fill and evict FIFO.
  - Voices = 1 behaves as parallel.
- **Frippertronics:** single shared buffer, decaying, length-synced (one buffer = one length).

**Fade-and-evict (voiced):** age-ramped gain over the FIFO stack, linear by rank. Gain of r-th-oldest voice = (N − r) / N. Higher N lengthens/refines the tail → reads as a fade-out.

**Layer summing:** the grain engine already handles summation across layers; the open question is only the normalisation law (see Open items), not the mechanism.

### Live control (K1)

The voice/topology knob is a performance control: turning it never resets or stops playback — it acts on whatever material is currently there.

- Reducing voice count evicts oldest-first (FIFO), live.
- Sweeping voiced → frippertronics keeps the last/active voice and rewrites it in frippertronics style; sweeping back keeps the loop and repopulates voices from new playing, not from restored old ones.
- In continuous-auto this is less controlled — flagged to explore, not locked.

The "never reset, work with current material" principle is locked; the voiced↔frippertronics transition mechanics above are provisional.

## Grain playback (K3 — smoothness)

Single macro sweeping the grain read from looper (CCW) to freeze (CW). Three co-varying parameters:

- **Grain size:** ~buffer length (CCW) → short, tens of ms (CW).
- **Playback-position randomisation:** ordered sequential read at 1.0× real-time (CCW) → position randomised across the **full buffer length** (CW).
- **Grain density / rate:** low (CCW) → high (CW). Rises as grain size falls, so overlap stays ≥ 1 (continuous output) and the short grains fuse into a smooth pad rather than a stuttery scatter. Not an independent control — it tracks K3.

The spray window is always the whole buffer; K3 never narrows it to a sub-region. Turning CW shrinks grains and widens position scatter across the entire captured buffer, whatever its length. This scales from millisecond loops to multi-second loops with no special-casing and no "which part of the buffer do we freeze" decision — full freeze draws grains from all of it.

EHX-style short-sample freeze is a **capture-length** behaviour, not a K3 position: to get the classic tiny-sample freeze, capture a short buffer (quick FS2 press in manual, or a short impulse/note in continuous-auto). K3 at full CW over a long manual capture yields an evolving cloud sampled across the whole phrase, not a frozen instant.

Click-free boundaries via grain gain-windowing (shared with the existing grain engine); the window is applied at all K3 positions, with overlap = 0 at the looper end.

**Provisional:** mid-travel character can't be judged without hardware. Build both ends properly; respec the transition region if it proves sonically dead.

## Texture (K4)

Bipolar degradation, clean at centre:

- Analogue side: tape-style saturation ramping into extreme tape degradation.
- Digital side: decimation ramping into glitch distortion / audio-buffer-overrun mayhem.

Still draft — character defined, implementation and range not locked.

## Capture and memory are orthogonal

Capture mode = how loop boundaries are set. Memory model = how captured material is stored/combined. They combine independently.

- Continuous-auto + voices = 1: each detected phrase replaces → single evolving loop.
- Continuous-auto + voices = N: each detected phrase pushes a new independent-length voice, FIFO.

**Silence must be authored into the loop.** Engaged playback is a continuous stream by default, so a rest only exists if it is captured as part of the loop. Manual capture can do this — you define the window, rests included. Continuous-auto structurally cannot: silence is its phrase delimiter, so it cannot leave a rest at the boundary.

## Dropped / out of scope

- **Ducking** — removed entirely; not needed. Dissolves the Switch 2 interaction axis (parallel and layering already live on K1), freeing Switch 2.
- **Retrigger, one-shot, auto-armed capture** — dropped. Removing the trigger behaviours is what collapsed the former trigger axis; there is no trigger switch.
- **Rolling-buffer / grain-delay engine** — out of scope for this matrix; possible standalone coarse mode later.

## Open items

- **Switch 1/down:** unassigned.
- **Switch 2:** now fully free — no interaction function assigned.
- **K1 voice/topology geometry:** count range (e.g. 1–6), 1 = parallel, and where frippertronics sits on the sweep — not finalised.
- **Voiced vs frippertronics selection:** whether purely a K1 region or chosen elsewhere — not decided.
- **K5 fade/decay:** role varies by model (fade slope voiced / decay coeff frippertronics). Proposed, not locked.
- **Frippertronics length in auto mode:** tentative — reset buffer length on each captured event so it tracks like one-voice parallel (avoids a clear-to-reset step). Marked open.
- **Gain summation law for flat/hard voiced stacking:** 1/N vs 1/√N vs soft-limit — undecided (engine handles the summing; only the law is open).
- **K2:** spare knob; candidate is envelope sensitivity / threshold.
- **LED blink-state mapping:** undefined.
- **Clear gesture** confirmed as hold-FS1; revisit once freeze is added (freeze deferred).
