# Changelog

Notable changes to NitroTron3, intended for users. Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The firmware is pre-1.0 — the feature set is still expected to move.

## Unreleased

### Changed

- **Env → VCA noise-floor handling.** The envelope VCAs (Mode A drone and the
  Mode C SW1=DOWN synth voice) now pass the envelope through a static downward
  expander: above a threshold the response is unchanged (full touch
  sensitivity), below it the envelope is scaled toward zero. This stops a rig
  with a higher noise floor from triggering the voice or smearing note-offs,
  and it's deterministic — the pedal's behaviour does not drift over time.
  Tunable via `ENV_VCA_EXP_THRESH` / `ENV_VCA_EXP_RATIO` (ratio = 1 → off).

## v0.2 — 2026-06-27 — Pre-release

Focused on **SCHISM (Mode C)** — its drives, filters, and control feel — plus a
global fix for knob zipper noise. Mode C is functionally complete but still
being voiced by ear (see Known limitations).

### Added

- **SCHISM — Tube Screamer → tube-amp overdrive** (SW1=MIDDLE, K4 CCW): pre-clip
  high-pass → pedal saturation → a touch of low-passed clean for body → amp
  saturation. Clamped-cubic soft clipping with bias-offset asymmetry; K4 is a
  staged master gain (pedal/TS drive builds first, amp drive enters over the top
  of the travel). Clean is handled by the K6 mix.
- **SCHISM — Chebyshev octave-up waveshaper** (SW1=UP, K4 CCW): a metallic /
  octave-up harmonic generator fed by a pre-shaper low-pass for a clean octave.
- **SCHISM — audio-rate cutoff self-FM** on the Moog ladder, faded in by K5 (CW)
  for gritty, vocal resonance instead of a sterile self-oscillation.
- **Preset system — WYSIWYG manual boot**: manual mode now adopts the physical
  knob *and* switch positions on boot, so the pedal matches its panel.

### Changed

- **SCHISM — K4 is now bipolar around noon** (noon = clean) for SW1=UP and
  SW1=MIDDLE, with one drive flavor on each side; drive slots reorganized.
- **SCHISM — SW1=MIDDLE CW is now a gated bit-flipper** (XOR of a chosen bit,
  env-gated, per-bit loudness compensation), replacing the earlier bit-crusher.
- **SCHISM — Moog ladder retuned** (MoogLadderV2): K3 env response curve and
  asymmetric attack/release, deeper env-to-cutoff range, lower (20 Hz) cutoff
  floor, per-filter input pads and makeup gains.
- **SCHISM — phaser rebuilt**: 6-stage allpass with per-stage detune and a
  soft-saturated feedback loop reaching bounded self-oscillation.
- **SCHISM — amp-envelope VCA is currently disabled** while the wet path is
  auditioned (the SW1=DOWN synth voice keeps its own envelope VCA).

### Fixed

- **Knob zipper noise (all modes).** Knob values update once per audio block, so
  block-rate values applied to audio-rate gains stair-stepped audibly when a
  control moved. Added per-sample smoothing on the K6 dry/wet mix (Modes A/B/C)
  and on Mode C's K5 drive and K4 drive-character gains. Steady-state tone is
  unchanged.

### Housekeeping

- README points to GitHub Releases for pre-built binaries; documented the
  `release` skill and refreshed the `tune` skill; dropped stale tuning-mode docs
  and recorded the Plague Bearer → phaser pivot.

### Known limitations

- The SCHISM phaser (SW2=DOWN) is still provisional and may be replaced.
- The SCHISM amp-envelope VCA is disabled in this build (see Changed).
- Mode C voicing (overdrive, bit-flipper comp, Chebyshev mix, filter-env times)
  is still being ear-tuned on hardware.
- Pitch tracking (YIN, 4× decimated) can still glitch on muted-string transients.

## v0.1 — 2026-06-24 — Pre-release

First public pre-release. Verified on hardware. The feature set is not yet settled and may change in later releases. Shared so interested parties can flash it and give feedback.

### Modes

- **BORDUN (Mode A)** — pitched harmonic drone. Internally generated oscillator (saw / triangle / square) gated by an envelope follower tracking the bass, blended alongside the dry signal. Fixed pitch, octave-locked tracking, or direct tracking via SW2. Second oscillator for detune. Huovilainen ladder filter; triangle mode crosses K4 into wavefolding past noon.
- **SPRAWL (Mode B)** — granular delay engine with three texture shapers (decimator/wavefolder, event-driven digital glitch zones, pitch-tracked ringmod), three harmony sources (fixed interval, resonance-window pick, Bode SSB frequency shifter), a tanh-saturated feedback path with build-up and on-play duckers, and a Clouds-style wet-path reverb. K2 fully CCW bypasses the grain engine for a direct-texture path with micro-stutter.
- **SCHISM (Mode C)** — drive → filter chain. Drive (SW1): sine wavefolder, gated bit crusher, or pitch-tracked synth oscillator (hypersaw / saw / rect / PWM morph). Filter (SW2): Moog ladder, Grendel formant, or phaser. Bipolar pre-filter drive, post-filter 2-band limiter (LF preserved), amp-env VCA so self-resonance doesn't ring on silence.

### Preset system

- One global edit buffer, 3 banks × 8 slots (24 reachable presets). Each slot stores its own mode — cycling presets can swap mode.
- FS1 short = cycle slot (or revert dirty). FS1 long = jump to Manual. FS2 short = toggle bypass. FS2 long = enter save mode / confirm save. FS1+FS2 short tap = cycle bank with a Roman-numeral burst on both LEDs. FS1+FS2 held 2 s = DFU bootloader (alternating LED burst before reset).
- Presets persist across power cycles. Dirty marking on knob, SW1, SW2 and SW3 movement.

### Hardware

- Electro-Smith Daisy Seed 65 MB on the Cleveland Music Co. Hothouse DSP Pedal Kit. Audio at 48 kHz.

### Known limitations

- Phaser sub-mode in SCHISM is provisional and may be replaced with a different effect in a future release.
- Pitch tracking (YIN, 4× decimated) works on passive bass at line level but can glitch on muted-string transients.
- Some Mode C parameters (bit-crush range, filter-env attack/release times) are still being ear-tuned.

### License

- GPL v3. See `LICENSE`. Third-party bundled code: Hothouse hardware proxy (GPL v3), libDaisy (MIT), DaisySP (MIT), Mutable Instruments Clouds reverb (MIT). Full texts in `THIRD_PARTY_LICENSES.md` on each GitHub release.
