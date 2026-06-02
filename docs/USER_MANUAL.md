---
title: NitroTron3 — User Manual
subtitle: DIY digital bass pedal — Daisy Seed + Hothouse
---

# NitroTron3

A multi-mode digital bass effects pedal built on the Electro-Smith Daisy
Seed and Cleveland Music Co. Hothouse DSP kit.

Three modes — **Bordun** (sustained drone), **Sprawl** (granular texture),
and **Schism** (drive + filter) — each with its own controls and eight
storable presets.

## Hardware overview

- 6 knobs (K1–K6, left-to-right, top row then bottom)
- 3 three-position toggle switches (SW1–SW3)
- 2 footswitches (FS1 = preset / FS2 = bypass)
- 2 indicator LEDs

**SW3 selects the mode**:

- **UP** — Mode A — Bordun
- **MIDDLE** — Mode B — Sprawl
- **DOWN** — Mode C — Schism

The mode determines what every other knob and switch does. Each mode is
documented in its own section below.

---

# Mode A — Bordun

A pitched drone that follows your bass. A two-oscillator voice (saw,
triangle, or square) tracks the input, gated by an envelope follower so
the drone only sounds while you play. A Moog ladder filter shapes the
tone; in triangle mode the same knob crosses over into wavefolding.

![Mode A pedal layout](pedal-mode-a.svg){.pedal-layout}

## Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Semitone / Interval | ±12 semitone offset, centered with deadzone. **Fixed**: center = A, also sets the wrap point for tracking. **Track**: center = tracked note, wraps at the note set in fixed mode |
| KNOB 2 | Octave | 7 positions (C-1 – C5). **Octave-locked**: sets target octave. **Direct**: ±3 octave offset from tracked pitch |
| KNOB 3 | Fine tune | ±50 cents continuous (osc 1 only — creates beating with osc 2) |
| KNOB 4 | Tone / Wavefold | SAW/SQ: full ladder cutoff (80 Hz – 8 kHz). TRI: CCW → noon = cutoff, noon → CW = wavefolding (filter stays fully open) |
| KNOB 5 | Osc 2 detune | Center = off (deadzone). Outside center = ±1–12 semitone steps. Not affected by fine tune |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet (oscillator) |
| SWITCH 1 | Waveform | **UP** — Saw • **MIDDLE** — Triangle • **DOWN** — Square |
| SWITCH 2 | Drone mode | **UP** — Fixed pitch (K1 sets note, K2 sets octave) • **MIDDLE** — Octave-locked tracking (pitch class follows bass in K2's octave, K1 adds interval) • **DOWN** — Direct tracking (osc follows exact bass pitch, K1/K2 are relative offsets ±12 semi / ±3 oct) |

---

# Mode B — Sprawl

A live granular companion. An 8-second ring buffer continuously captures
your input; a grain scheduler scatters short pitched copies above the
dry signal, harmonically locked to the tracked bass note. A texture
shaper colors the wet path with a choice of three flavors. K5 is
bipolar — one direction adds reverb, the other adds ring-buffer feedback
for self-sustaining drones.

When K2 is fully CCW, the grain engine bypasses and the input routes
directly through the texture shaper — "direct-texture" mode — with K3
becoming a micro-stutter control.

![Mode B pedal layout](pedal-mode-b.svg){.pedal-layout}

## Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Interval | ±24 semitones, centered with deadzone. Pitch offset applied to each grain relative to tracked bass note |
| KNOB 2 | Buffer range | CCW = tight (100 ms). CW = deep (full 8 s). **Fully CCW** enters direct-texture mode (grain engine bypassed) |
| KNOB 3 | Character / Glitch | **Grain mode**: CCW = soft/long/tight, CW = short/sharp/chaotic. **Direct-texture mode**: micro-stutter — CCW = clean, CW = frequent choppy repeats |
| KNOB 4 | Texture amount | Depends on SW1 position — see below |
| KNOB 5 | Reverb / Feedback (bipolar) | **CCW** = Clouds reverb amount (0 → 1). **Center (±5%)** = off. **CW** = ring-buffer feedback (0 → 0.95). Reverb tail does not feed the ring buffer |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet. Equal-power curve |
| SWITCH 1 | Texture mode | **UP** — Decimator / Wavefolder bipolar (K4 CCW = max crush, noon = clean, CW = wavefold) • **MIDDLE** — Event-driven digital glitch (bipolar K4: noon = clean ±5%, CCW = random bit-flip events, CW = random timing events — freeze / stutter / reverse; sparse near noon → continuous at the extremes via event chaining) • **DOWN** — Ringmod (K4 0 – 30% = tremolo 1 – 15 Hz, 30 – 100% = bell partials, pitch-tracked with keytracked LPF) |
| SWITCH 2 | Harmony | **UP** — Fixed interval (K1 semitones above tracked note) • **MIDDLE / DOWN** — Resonance (grains lock onto nearby harmonics) |

---

# Mode C — Schism

A drive + filter mode. K1–K3 drive the filter selected by SW2; K4 drives
the wavefolder selected by SW1. K5 is a bipolar pre-filter drive
(attenuate / unity / boost, universal across all SW2 filter modes), K6
is the dry/wet mix.

Three filter flavors: a tuned **Moog ladder** (single input saturator,
cutoff-tracked resonance, asymmetric drive), a vowel-pathed **Grendel
formant** filter, and the dual-band **Plague** filter (lo + hi SVF
bands cross-fed with tanh saturation). K3 is a bipolar
envelope-to-filter modulator with a center deadzone.

The wet path runs through a 2-band post-filter peak limiter (low end
preserved so bass fundamentals don't duck under resonance peaks) and is
gated by an amplitude-env VCA — when you stop playing, the wet path goes
silent so self-resonance never rings alone.

*Note: Mode C is still in active development — SW1 = MIDDLE is reserved
for a future drive flavor, currently passes audio unchanged.*

![Mode C pedal layout](pedal-mode-c.svg){.pedal-layout}

## Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Filter "where" | SW2=UP: Moog cutoff (80 Hz – 8 kHz, exponential). SW2=MID: Grendel vowel path (CCW = ee, CW = oo). SW2=DOWN: Plague input balance (CCW = lo only, CW = hi only) |
| KNOB 2 | Filter "how much" | SW2=UP: Moog resonance (0 → self-osc, sqrt curve so the lower half is audible). SW2=MID: Grendel size (mouth scale, ×0.5 → ×1.6). SW2=DOWN: Plague intensity (tandem input + feedback drive) |
| KNOB 3 | Env → filter amount | Bipolar with ±5% center deadzone. SW2=UP: opens/closes Moog cutoff (passive-bass scaled). SW2=MID: shifts the vowel path. SW2=DOWN: shifts Plague balance. Center = static |
| KNOB 4 | Drive character | SW1=UP: sine wavefold amount (0 = clean, 1 = max fold; internal loudness compensation). SW1=MID/DOWN: unused |
| KNOB 5 | Filter drive (bipolar) | **CCW** attenuates (~−12 dB at full CCW). **Noon** is unity. **CW** boosts up to 8× hot. Sets the Moog ladder's input drive; pre-tanh in front of Grendel and Plague. Moog and Grendel have a fixed internal pad so noon sits in their clean sweet zone; Plague is unpadded (needs hot input to fold) |
| KNOB 6 | Mix | 0 = full dry, 1 = full wet. Equal-power curve |
| SWITCH 1 | Drive | **UP** — Sine wavefolder (K4 = fold amount) • **MIDDLE** — TBD (currently passthru) • **DOWN** — Passthrough |
| SWITCH 2 | Filter | **UP** — Moog ladder (K1 cutoff, K2 resonance, K3 env) • **MIDDLE** — Grendel formant (K1 vowel path, K2 size, K3 env on path) • **DOWN** — Plague (K1 input balance, K2 intensity, K3 env on balance) |

---

# Footswitches and Presets

The footswitches and the preset system work the same way in every mode.

## Footswitches

| CONTROL | DESCRIPTION |
|-|-|
| FOOTSWITCH 1 | **Short press**: cycle Manual → 1 → … → 8 → Manual (or reload the current preset if dirty). **Long press (700 ms)**: jump to Manual |
| FOOTSWITCH 2 | **Short press**: toggle bypass. **Long press (700 ms)**: enter save mode, or confirm save if already in save mode. **Short press in save mode**: cancel save |
| FS1 held 2 s | Enter DFU bootloader for flashing new firmware |

## Indicator LEDs

| LED | DESCRIPTION |
|-|-|
| LED 1 (left) | **Preset indicator.** Off = Manual mode. Otherwise a Roman-numeral blink pattern shows the preset number (I = short, V = long: I, II, III, IV, V, VI, VII, VIII). In save mode, shows the target slot |
| LED 2 (right) | **State indicator.** Solid = active, off = bypassed, rapid flash = dirty (preset edited but not saved), fast blink = save mode armed, burst = save confirmed |

## Preset behavior

- Each mode has its own 8 stored presets plus an edit buffer (Manual).
- Adjusting any knob or switch dirties the active preset (LED 2 rapid flash).
- Short-press FS1 while dirty to **reload** the preset and discard edits.
- Long-press FS2 to enter save mode; short-press FS1 to pick a target
  slot; long-press FS2 again to confirm. Short-press FS2 cancels.
- Presets persist across power cycles via internal flash storage.
- Switching modes via SW3 saves the current mode's edit buffer
  automatically and restores the destination mode's state.
