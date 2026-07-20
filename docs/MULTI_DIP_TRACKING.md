# Multi-dip YIN — pseudo-polyphonic tracking (discovery)

**Status: discovery / planning — nothing implemented.**
Branch: `feature/multi-dip-yin-tracking`. Date: 2026-07-20.

Both instruments are in scope — this is not guitar-only; bass double stops and
chords are a first-class target.

## Concept

The YIN tracker already computes the normalized difference function `d′(τ)`
across the whole lag range; today it keeps only the *first* dip below 0.15
(`pitch_tracker.h`, `RunYin()`) and early-exits. Every note in a chord leaves
its own dip in that curve. Extension: scan the full lag range, take the **N
deepest local minima** (after harmonic dedupe), and hand each one to its own
oscillator voice — the existing supersaw / PWM / FM voices, unchanged. Voice
count is dynamic: a clean single note yields one voice, a double stop two, a
chord three.

Philosophy fit: this is *not* clean polyphonic tracking (impossible at this
compute anyway). Dips interact — fifths spawn their implied root, clusters
wander — but the artifacts are **reproducible per fingering**, i.e. playable.

Interactive sketch of the dip behavior (synthetic dyads, real YIN math):
built during discovery, 2026-07-20 (Claude artifact "Multi-dip YIN").

## Why the ceiling is 3 voices (physics, not taste)

Each note's dip depth is set by the signal power it does *not* explain.
K equally-loud notes → dip depth `d′ ≈ (K−1)/K`:

| simultaneous notes | dip depth d′ | salience 1−d′ |
|-|-|-|
| 1 | ~0.05–0.15 | strong |
| 2 | ~0.5 | usable |
| 3 | ~0.67 | marginal |
| 4 | ~0.75 | drowned |

Beyond 3 equal notes YIN's SNR is spent. A 4th voice only ever has signal
when amplitudes are uneven (let-ring arpeggios). **Cap = 3.**

## Resolution limit (known, accepted)

Separating two close frequencies needs the analysis window to span ~one beat
cycle (`1/Δf`). Windows are ~27–33 ms (guitar/bass), so:

- minor 2nds resolve only above ~500 Hz; major 2nds above ~250 Hz;
  minor 3rds and wider resolve across the playing range.
- Below that, the pair is physically **one dip that wanders** between the two
  notes at the beat rate. No voice count fixes this. The per-voice pitch slew
  turns the wander into a slow detune drift — treated as character, not a bug.

Low-bass corollary: for low double stops (E1+B1 …) the common period falls
below the 30 Hz lag floor → **no implied-root quirk on the low neck**; it
appears only higher up.

## Design

### 1. Dip picker (in `RunYin()`, full scan)

- Remove the first-dip early-exit **only when the poly path is enabled**;
  scan all lags, collect local minima with `d′ < TRACK_POLY_DIP_MAX`,
  parabolic-refine each (reuse the existing gated refine).
- Sort deepest-first. Accept a dip unless (a) within `TRACK_POLY_DUP_TOL` of
  an accepted lag, or (b) at an integer multiple (≥2×) of an accepted lag
  within `TRACK_POLY_HARM_TOL` — sub-octave alias. Keep ≤ 3.
- Salience per dip = `1 − d′`.
- **The mono outputs (`GetMidiNote`, `GetMidiNoteContinuous`) keep the
  first-dip semantics exactly** — all current consumers (octave-locked drone,
  Mode B harmony, ringmod) are untouched. Multi-dip is an additional API:
  `GetVoice(i)` → {midi, salience, active}.

### 2. Voice matcher (new small class, runs per hop)

Warble control lives here, not in more voices:

- Match accepted dips to running voices within ±`TRACK_POLY_MATCH_ST`
  semitones (greedy, nearest first).
- Matched: glide pitch with per-hop slew limit; smooth salience (LP).
- Unmatched voice: release fade (`TRACK_POLY_RELEASE_MS`), then free.
- Unmatched dip with smoothed salience > `TRACK_POLY_SPAWN_SAL`: new voice,
  attack fade. Voice dies when salience < `TRACK_POLY_KILL_SAL` (hysteresis).
- Loudness: sustained voices take level from the **envelope follower** ×
  slow-smoothed salience — never raw per-hop salience (that's the beat-rate
  tremolo). Salience mainly decides spawn/kill.
- Slap/pop/muted transients produce no dips for a hop or two — the release
  time carries voices through. Release is a primary ear-tuning lever.

### 3. Voice → oscillator wiring

3 instances of the existing synth voices, per-voice gain = env × salience.
First targets, in order:

1. **Mode C SW1=DOWN synth** (hypersaw / saw / rect / PWM) — most audible,
   the sound the multi-dip idea came from.
2. **Mode A drone** (direct-track; saw unison / triangle-JI+FM / square-PWM).

Poly is **always-on** in these sub-modes (no user control) — gated by a
compile-time constant per path (see Decisions).

## Tuning constants (proposed starting values)

The instrument split already lives in the `TRACK_*` profile (decimation, lag
range, window, hop). Hop time is identical on both profiles (64/12 kHz =
128/24 kHz = 5.33 ms), so all per-hop logic is shared. Only one constant is a
per-instrument candidate at this point.

| constant | bass | guitar | meaning |
|-|-|-|-|
| `TRACK_POLY_VOICES` | 3 | 3 | voice cap (physics, see above) |
| `TRACK_POLY_DIP_MAX` | 0.75 | 0.75 | acceptance ceiling for candidate dips |
| `TRACK_POLY_DUP_TOL` | 0.03 | 0.03 | near-duplicate lag rejection |
| `TRACK_POLY_HARM_TOL` | 0.035 | 0.035 | integer-multiple (sub-octave) rejection |
| `TRACK_POLY_SPAWN_SAL` | 0.30 | 0.30 | smoothed salience to birth a voice |
| `TRACK_POLY_KILL_SAL` | 0.20 | 0.20 | smoothed salience to kill a voice |
| `TRACK_POLY_MATCH_ST` | 1.0 | 1.0 | semitone window for hop-to-hop matching |
| `TRACK_POLY_SLEW` | 0.25 | 0.25 | max pitch change per hop (semitones) |
| `TRACK_POLY_ATTACK_MS` | 15 | 15 | voice fade-in |
| `TRACK_POLY_RELEASE_MS` | 120 | 80 | voice fade-out — **per-instrument**: bass rings longer, slap gaps need carrying |
| `TRACK_POLY_SAL_LP_MS` | 50 | 50 | salience smoothing (kills beat-rate tremolo) |
| `TRACK_POLY_ENABLE` | true | true | master switch — false = byte-identical mono build |
| `TRACK_POLY_HARM_DEDUPE` | true | true | sub-octave dedupe defeat lever (experiment) |
| `TRACK_POLY_GAIN_EXP` | 1.0 | 1.0 | gain = salience^X (1 linear, 0.5 sqrt, 0 off) |
| `TRACK_POLY_OCTLOCK` | false | false | poly in octave-locked drone — to be tested |

All ear-tunable; values above are educated first guesses.

## Cost budget

- **CPU** — full lag scan runs in the main loop (`Update()`), not the audio
  callback. Worst case ≤ ~2× today's scan (a low note already scans most of
  the range before the early-exit). Oscillators: 7-voice hypersaw × 3 = 21
  oscs ≈ low single-digit % of the 480 MHz M7. Verify with the usual load
  check on hardware.
- **RAM** — 2 extra synth-voice instances + matcher state: trivial.
- **FLASH — the tight one.** 97.4/97.5% used (see PROJECT.md). Picker +
  matcher is small (~1–2 KB) but headroom is ~3 KB. If it doesn't fit:
  the documented one-line linker swap to `STM32H750IB_qspi.lds`.
- **Bass build byte-identity**: gate everything on the `NT3_TRACK_POLY`
  preprocessor flag so that with the poly path disabled the binary is
  byte-for-byte today's (verified against v0.4). A constexpr alone is not
  enough — the tracker's poly members shift the object in `.data` even when
  all poly *code* folds away; the OFF build must drop the members too.

## Staged plan

- **M.1** — tracker: full-scan dip extraction behind a constexpr flag;
  `GetVoice(i)` API; mono outputs byte-identical with flag off. Bench the
  scan cost.
- **M.2** — voice matcher class (hysteresis, matching, slew, fades).
- **M.3** — Mode C SW1=DOWN synth on 3 voices; audition on bass + guitar.
- **M.4** — Mode A drone voices; audition.
- **M.5** — ear-tune the constants table; decide per-instrument splits and
  how poly is exposed (always-on vs control).
- **M.6** — docs + README controls (on explicit request), fold findings back
  into PITCH_TRACKING.md.

## Decisions (interview, 2026-07-20)

1. **Exposure: always-on**, switchable per compile-time constant
   (`TRACK_POLY_ENABLE`). No user control.
2. **Sub-octave dedupe: tunable constant** (`TRACK_POLY_HARM_DEDUPE`) — an
   ear-tuning experiment lever (deliberate implied-root / sub-octave voices),
   not a user control. Default on.
3. **Salience→gain law: power-law constant** — gain = salience^`X`
   (`TRACK_POLY_GAIN_EXP`, start 1.0 = linear; 0.5 = sqrt, 0 = off). Bracket
   by ear in M.5.
4. **Octave-locked poly: switchable per constant** (`TRACK_POLY_OCTLOCK`,
   default off) — needs hardware testing to judge.
5. **Presets: moot** — everything above is a constant, no new control, no
   `ModePresetData` slot.
