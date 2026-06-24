# Changelog

Notable changes to NitroTron3, intended for users. Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The firmware is pre-1.0 — the feature set is still expected to move.

## Unreleased

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
