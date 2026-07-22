---
title: NitroTron3 — User Manual
subtitle: DIY digital bass pedal — Daisy Seed + Hothouse
---

![NitroTron3](assets/NitroTron3.jpg){.cover}

A project of [Nitro Mahalia](https://nitromahalia.net). This pedal
packages several of their signature bass-through-synth sounds into a
single bass-specific unit and more: three distinct modes, each doing
something not easily found in off-the-shelf pedals. Built on the
Electro-Smith Daisy Seed and Cleveland Music Co. Hothouse DSP kit.

## Modes at a glance

**BORDUN (Mode A).** A second voice bound to the instrument: sometimes a
shadow, sometimes a fixed point. Inspired by a specific use of the Moog
Moogerfooger FreqBox MF-107, BORDUN places an envelope-gated oscillator beside
the dry signal, without forcing it into conventional synth behaviour. It can
follow the played pitch, lock notes into a chosen octave, or hold a drone while
the instrument moves around it. Use it for clean intervals, fifths and octaves,
detuned bodies, unstable chords, or FM rupture. Before overdrive or fuzz, both
voices fuse into a single larger sound.

**SPRAWL (Mode B).** A granular delay and texture system for sounds that refuse
to stay where they were played. An 8-second rolling buffer is cut into grains
that can drift, reverse, scatter, repeat, stretch, and lose their original
pitch. The texture stages also work directly, without the grain engine, for
crush, fold, ring modulation, and event-driven digital failure. Feedback turns
fragments into clouds; frequency shifting inside the loop makes each return
less related to the source until the material spreads beyond recognition.

**SCHISM (Mode C).** A signal divided, damaged, and rebuilt: drive first, then
filter. Wavefolding, harmonic shaping, octave-fuzz, polyphonic octave layers,
and a pitch-tracked synth voice feed a Moog-style ladder, formant filter, or
phaser. The filters range from movement and emphasis to resonance and controlled
self-oscillation. SCHISM is equally suited to precise harmonic construction and
the point where the sound starts to come apart. The phaser is provisional and
may be replaced in a future firmware release.

**Presets.** The preset system stores the mode and complete control state.
There are 3 banks with 8 slots each, for 24 presets. A preset may change modes
when loaded. See _Footswitches and Presets_.

## Hardware overview

- 6 knobs: K1–K6, left-to-right, top row then bottom row
- 3 three-position toggle switches: SW1–SW3
- 2 footswitches: FS1 = presets, FS2 = bypass/save
- 2 indicator LEDs

**SW3 selects the mode:**

- **UP** — BORDUN (Mode A)
- **MIDDLE** — SPRAWL (Mode B)
- **DOWN** — SCHISM (Mode C)

SW3 changes the functions of K1–K5, SW1, and SW2. K6 is always the dry/wet mix.

## Reading the knob icons

| Icon | Meaning |
| :---: | --- |
| ![CCW](assets/icon-ccw.svg){.icon-legend} | Turn counter-clockwise, left of centre |
| ![CW](assets/icon-cw.svg){.icon-legend} | Turn clockwise, right of centre |
| ![noon](assets/icon-noon.svg){.icon-legend} | Centre/noon position |
| ![bipolar](assets/icon-bipolar.svg){.icon-legend} | Bipolar control: different functions on either side of noon |
| ![sweep](assets/icon-uni.svg){.icon-legend} | Unipolar control: minimum to maximum |
| ![steps](assets/icon-steps.svg){.icon-legend} | Stepped control: selects discrete values |

---

# BORDUN (Mode A)

BORDUN adds an internally generated oscillator beside the dry instrument. The
input envelope opens and shapes the voice, so it follows the force and timing
of the performance rather than behaving like a continuously running synth.
SW2 determines whether the oscillator holds, locks, or follows pitch; SW1
selects its basic waveform. K4 shapes the spectrum, while K5 moves from added
mass and harmonic structure on the left to input-driven audio-rate FM on the
right.

![Mode A pedal layout](assets/pedal-mode-a.svg){.pedal-layout}

## Controls

| CONTROL | FUNCTION |
|---|---|
| **Pitch / interval**<br/><span class="ctl-id">Knob 1</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon} ![steps](assets/icon-steps.svg){.ctl-icon}<br/>**Function:** Sets the oscillator note or tracking interval.<br/>**Range:** −12 to +12 semitones, stepped; centre deadzone.<br/>**SW2 UP — Fixed:** Noon = A; turn left or right to select another note.<br/>**SW2 MIDDLE/DOWN — Tracking:** Transposes the tracked note by the selected interval. |
| **Octave**<br/><span class="ctl-id">Knob 2</span> | ![steps](assets/icon-steps.svg){.ctl-icon}<br/>**Function:** Sets the oscillator register.<br/>**Range:** 7 stepped octave positions.<br/>**SW2 UP — Fixed:** Selects the drone octave.<br/>**SW2 MIDDLE — Octave-locked:** Selects the octave into which the detected pitch class is folded.<br/>**SW2 DOWN — Direct:** Adds an octave offset to the played note.<br/>**Noon:** Approximately two octaves above the played note in the tracking modes. |
| **Fine tune**<br/><span class="ctl-id">Knob 3</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Fine-tunes the oscillator.<br/>**Range:** −50 to +50 cents.<br/>**Noon:** No detuning. |
| **Filter**<br/><span class="ctl-id">Knob 4</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Shapes the oscillator spectrum.<br/>**Noon:** Open/neutral.<br/>**SW1 UP/DOWN — Saw or Square:** ![CCW](assets/icon-ccw.svg){.ctl-icon} closes a low-pass filter from about 8 kHz to 250 Hz and adds saturation; ![CW](assets/icon-cw.svg){.ctl-icon} raises a high-pass filter from about 20 Hz to 2 kHz.<br/>**SW1 MIDDLE — Triangle:** ![CCW](assets/icon-ccw.svg){.ctl-icon} closes the low-pass filter; ![CW](assets/icon-cw.svg){.ctl-icon} increases wavefolding. |
| **Voice**<br/><span class="ctl-id">Knob 5</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Thickens or modulates the oscillator.<br/>**Noon:** One clean oscillator.<br/>**![CW](assets/icon-cw.svg){.ctl-icon}:** Adds through-zero audio-rate FM from the instrument. Depth increases with knob position and input level.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}, SW1 UP — Saw:** Adds detuned unison voices.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}, SW1 MIDDLE — Triangle:** Builds a just-intonation ensemble one octave above.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}, SW1 DOWN — Square:** Increases pulse-width modulation. |
| **Mix**<br/><span class="ctl-id">Knob 6</span> | ![sweep](assets/icon-uni.svg){.ctl-icon}<br/>**Function:** Balances the dry instrument and oscillator.<br/>**Range:** Full CCW = dry only; full CW = oscillator only. |
| **Waveform**<br/><span class="ctl-id">Switch 1</span> | **Function:** Selects the oscillator waveform.<br/>**UP:** Saw<br/>**MIDDLE:** Triangle<br/>**DOWN:** Square |
| **Pitch mode**<br/><span class="ctl-id">Switch 2</span> | **Function:** Selects the oscillator pitch source.<br/>**UP — Fixed:** K1 selects the note; K2 selects the octave.<br/>**MIDDLE — Octave-locked:** The played pitch class is placed in the octave selected by K2; K1 adds an interval.<br/>**DOWN — Direct tracking:** The oscillator follows the played pitch; K1 transposes by semitones and K2 by octaves. |

---

# SPRAWL (Mode B)

SPRAWL continuously records into an 8-second buffer and releases the material
through eight granular voices. Away from noon, K2 opens the buffer in either
forward or reverse; at noon, the grain engine drops out and the texture stage
acts directly on the input. SW1 selects the type of damage or transformation.
SW2 determines whether the grains keep an interval, scatter across harmonics,
or pass through a frequency shifter. K5 places reverb to the left of noon and
feedback to the right: one widens the space, the other lets the space begin to
consume the source.

![Mode B pedal layout](assets/pedal-mode-b.svg){.pedal-layout}

## Controls

| CONTROL | FUNCTION |
|---|---|
| **Pitch / shift**<br/><span class="ctl-id">Knob 1</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon} ![steps](assets/icon-steps.svg){.ctl-icon}<br/>**Function:** Sets pitch treatment selected by SW2.<br/>**SW2 UP — Fixed interval:** −12 to +12 semitones.<br/>**SW2 MIDDLE — Harmonic cloud:** Scans a ±36-semitone harmonic range.<br/>**SW2 DOWN — Frequency shift:** ![CCW](assets/icon-ccw.svg){.ctl-icon} shifts downward; ![CW](assets/icon-cw.svg){.ctl-icon} shifts upward; noon deadzone = no shift; maximum = approximately ±1 kHz. Grain pitch remains at unison. |
| **Buffer / direction**<br/><span class="ctl-id">Knob 2</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Selects direct processing or granular buffer playback.<br/>**Noon ±6%:** Grain engine bypassed; signal passes directly through the texture stage; K3 controls micro-stutter.<br/>**![CW](assets/icon-cw.svg){.ctl-icon}:** Forward granular playback.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}:** Reverse granular playback.<br/>**Distance from noon:** Increases buffer depth and timescale from about 100 ms to 8 s. |
| **Character / stutter**<br/><span class="ctl-id">Knob 3</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Sets grain shape, or stutter amount when K2 is at noon.<br/>**Granular mode, ![CCW](assets/icon-ccw.svg){.ctl-icon}:** Longer grains and slower, smoother smearing; approximately 0.3–2 s grain length.<br/>**Granular mode, ![CW](assets/icon-cw.svg){.ctl-icon}:** Shorter grains, sharper repeats, and increasingly chaotic glitches.<br/>**Direct-texture mode:** Full CCW = no stutter; turn CW for more frequent short repeats. |
| **Texture amount**<br/><span class="ctl-id">Knob 4</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Controls the texture selected by SW1.<br/>**SW1 UP — Crush/Fold:** ![CCW](assets/icon-ccw.svg){.ctl-icon} increases decimation/crushing; noon = clean; ![CW](assets/icon-cw.svg){.ctl-icon} increases wavefolding.<br/>**SW1 MIDDLE — Glitch:** ![CCW](assets/icon-ccw.svg){.ctl-icon} increases random bit-flip events; noon ±5% = clean; ![CW](assets/icon-cw.svg){.ctl-icon} increases timing events such as freeze, stutter, and reverse. Events become denser toward either extreme.<br/>**SW1 DOWN — Ring mod:** Lower range produces tremolo from about 1–15 Hz; upper range enters audio-rate, pitch-tracked ring modulation with bell-like partials. |
| **Reverb / feedback**<br/><span class="ctl-id">Knob 5</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Adds reverb or buffer feedback.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}:** Increases Clouds-style reverb.<br/>**Noon ±5%:** Both effects off.<br/>**![CW](assets/icon-cw.svg){.ctl-icon}:** Increases saturated feedback into the rolling buffer. High settings build sustained layers and drones. |
| **Mix**<br/><span class="ctl-id">Knob 6</span> | ![sweep](assets/icon-uni.svg){.ctl-icon}<br/>**Function:** Balances dry and processed signals.<br/>**Range:** Full CCW = dry only; full CW = wet only; equal-power crossfade. |
| **Texture type**<br/><span class="ctl-id">Switch 1</span> | **Function:** Selects the processor controlled by K4.<br/>**UP — Crush/Fold:** Decimator left of noon, wavefolder right of noon.<br/>**MIDDLE — Glitch:** Bit-flip events left of noon, timing events right of noon.<br/>**DOWN — Ring mod:** Tremolo at low K4 settings, audio-rate ring modulation above approximately 30%. |
| **Harmony type**<br/><span class="ctl-id">Switch 2</span> | **Function:** Selects the pitch process controlled by K1.<br/>**UP — Fixed:** Pitch-shifts grains by a fixed interval.<br/>**MIDDLE — Harmonic cloud:** Scatters grains across nearby harmonic intervals.<br/>**DOWN — Shift:** Applies Bode-style single-sideband frequency shifting inside the feedback loop; grain playback remains at unison. |

---

# SCHISM (Mode C)

SCHISM is a two-stage chain: source/drive into filter. SW1 selects what enters
the filter, from wavefolding and harmonic shaping to octave-fuzz, polyphonic
octave layers, or a pitch-tracked synth oscillator. SW2 selects the filter:
Moog-style ladder, Grendel-style formant, or six-stage phaser. K1-K3 control the
selected filter; K4 controls the selected source; K5 sets how hard the filter is
hit. The range extends from controlled reinforcement to self-oscillation,
clipping, sputter, and broken digital edges, with no attempt to hide the
transitions between them.

![Mode C pedal layout](assets/pedal-mode-c.svg){.pedal-layout}

## Controls

| CONTROL | FUNCTION |
|---|---|
| **Frequency**<br/><span class="ctl-id">Knob 1</span> | ![sweep](assets/icon-uni.svg){.ctl-icon}<br/>**Function:** Sets the main frequency parameter of the filter selected by SW2.<br/>**SW2 UP — Moog:** Cutoff, approximately 20 Hz–8 kHz, exponential sweep.<br/>**SW2 MIDDLE — Grendel:** Vowel position; CCW = dark/closed “oo”, CW = bright/open “ee”.<br/>**SW2 DOWN — Phaser:** Centre frequency of the phaser notches. |
| **Resonance / size / feedback**<br/><span class="ctl-id">Knob 2</span> | ![sweep](assets/icon-uni.svg){.ctl-icon}<br/>**Function:** Sets the secondary filter parameter.<br/>**SW2 UP — Moog:** Resonance from low emphasis to self-oscillation.<br/>**SW2 MIDDLE — Grendel:** Formant size from approximately ×0.5 to ×1.6.<br/>**SW2 DOWN — Phaser:** Feedback from a clean sweep through resonant bloom to controlled self-oscillation. |
| **Movement**<br/><span class="ctl-id">Knob 3</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Adds envelope or LFO movement. Response is fine near noon and stronger toward the extremes.<br/>**Noon ±5%:** No modulation.<br/>**SW2 UP — Moog:** ![CCW](assets/icon-ccw.svg){.ctl-icon} moves cutoff downward from the played envelope; ![CW](assets/icon-cw.svg){.ctl-icon} moves it upward.<br/>**SW2 MIDDLE — Grendel:** ![CCW](assets/icon-ccw.svg){.ctl-icon} and ![CW](assets/icon-cw.svg){.ctl-icon} move the vowel path and size in opposite directions from the played envelope.<br/>**SW2 DOWN — Phaser:** ![CCW](assets/icon-ccw.svg){.ctl-icon} selects triangle LFO; ![CW](assets/icon-cw.svg){.ctl-icon} selects sample-and-hold LFO; distance from noon sets rate. At noon, the phaser is static at K1. |
| **Drive / synth voice**<br/><span class="ctl-id">Knob 4</span> | **Function:** Controls the source selected by SW1.<br/>**SW1 UP — Fold/Cheby:** Bipolar. Noon = clean; ![CCW](assets/icon-ccw.svg){.ctl-icon} increases Chebyshev octave-up/metallic harmonics; ![CW](assets/icon-cw.svg){.ctl-icon} increases sine wavefolding.<br/>**SW1 MIDDLE — Fuzz/POG:** Bipolar. Noon = clean; ![CCW](assets/icon-ccw.svg){.ctl-icon} crossfades into sub, +1 octave, and +2 octave voices; full CCW = full octave stack with no dry signal in this stage. ![CW](assets/icon-cw.svg){.ctl-icon} increases gated XOR octave-fuzz; the fuzz reaches high gain early, then the remaining travel changes the XOR bit position and glitch character.<br/>**SW1 DOWN — Synth:** Full-range timbre control. Left half = saw family, from hypersaw at full CCW to single saw near noon. Right half = rectangle family, from single rectangle near noon to stronger PWM at full CW. |
| **Filter drive**<br/><span class="ctl-id">Knob 5</span> | ![bipolar](assets/icon-bipolar.svg){.ctl-icon}<br/>**Function:** Sets the level entering the selected filter.<br/>**![CCW](assets/icon-ccw.svg){.ctl-icon}:** Attenuates, down to approximately −12 dB.<br/>**Noon:** Unity/clean operating level.<br/>**![CW](assets/icon-cw.svg){.ctl-icon}:** Boosts up to approximately 8×, increasing saturation, resonance interaction, and output density. |
| **Mix**<br/><span class="ctl-id">Knob 6</span> | ![sweep](assets/icon-uni.svg){.ctl-icon}<br/>**Function:** Balances dry and processed signals.<br/>**Range:** Full CCW = dry only; full CW = wet only; equal-power crossfade. |
| **Drive type**<br/><span class="ctl-id">Switch 1</span> | **Function:** Selects the source controlled by K4.<br/>**UP — Fold/Cheby:** Chebyshev waveshaping left of noon; wavefolding right of noon.<br/>**MIDDLE — Fuzz/POG:** Polyphonic octave stack left of noon; gated XOR octave-fuzz right of noon.<br/>**DOWN — Synth:** Pitch-tracked oscillator; K4 morphs from saw/hypersaw to rectangle/PWM. |
| **Filter type**<br/><span class="ctl-id">Switch 2</span> | **Function:** Selects the filter controlled by K1–K3.<br/>**UP — Moog ladder:** K1 cutoff, K2 resonance, K3 envelope movement.<br/>**MIDDLE — Grendel formant:** K1 vowel, K2 size, K3 envelope movement.<br/>**DOWN — Phaser:** K1 notch centre, K2 feedback, K3 LFO shape and rate. |

---

# Footswitches and Presets

The footswitches and preset system work the same way in every mode.

## Footswitches

| CONTROL | FUNCTION |
|---|---|
| **Footswitch 1** | **Short press:** Cycle Manual → 1 → … → 8 → Manual. When the current preset is edited, short press reloads it and discards the edits.<br/>**Long press, 700 ms:** Return to Manual mode. |
| **Footswitch 2** | **Short press:** Toggle bypass.<br/>**Long press, 700 ms:** Enter save mode. In save mode, long press again to confirm the save.<br/>**Short press in save mode:** Cancel the save. |
| **FS1 + FS2, short tap** | Cycle bank 1 → 2 → 3 → 1. Both LEDs indicate the new bank. In save mode, this changes the destination bank. |
| **FS1 + FS2, hold 2 s** | Enter the DFU bootloader for firmware flashing. Both LEDs alternate for approximately 1.2 seconds before reset. |

## Indicator LEDs

| LED | FUNCTION |
|---|---|
| **LED 1 — left** | **Preset indicator.** Off = Manual mode. A Roman-numeral blink pattern identifies preset 1–8: I, II, III, IV, V, VI, VII, VIII. In save mode, it indicates the destination slot. |
| **LED 2 — right** | **State indicator.** Solid = effect active. Off = bypassed. Rapid flash = current preset edited. Fast blink = save mode. Burst = save confirmed. |
| **Both LEDs** | **Bank indicator.** After a bank change, both LEDs show I, II, or III for approximately 1.6 seconds. The pulses contain a fast flicker to distinguish them from preset indications. |

## Preset behavior

- **Manual mode:** The sound follows the physical panel positions.
- **Preset capacity:** 3 banks × 8 slots = 24 presets.
- **Stored state:** Each preset stores its mode, all knob values, SW1, and SW2.
- **Global edit buffer:** There is one current editable state shared across all modes.
- **Changing banks:** In Manual mode, only the active bank changes. When a preset
  is active, the same slot number is loaded from the next bank. In save mode,
  the destination moves to the same slot in the next bank.
- **Edited presets:** Moving any knob or switch, including SW3, marks the current
  preset as edited. LED 2 flashes rapidly. Press FS1 to reload the saved state
  and discard the changes.
- **Saving:** Hold FS2 to enter save mode. Use FS1 to select a slot. Tap FS1+FS2
  to select a bank. Hold FS2 to confirm, or tap FS2 to cancel.
- **Power-on restore:** The pedal restores the active bank, active preset, edit
  buffer, mode, and edited state.
- **Flash writes:** Changed state is written approximately 2 seconds after the
  final adjustment. No write occurs while the state remains unchanged.
- **Preset migration:** Firmware using the previous one-bank-per-mode system is
  migrated on first boot. Mode A slots move to Bank 1, Mode B slots to Bank 2,
  and Mode C slots to Bank 3. Each preset retains its original mode.

---

# Guitar build

NitroTron3 is voiced for bass. Its pitch tracking, envelope response, and
several filter ranges assume a bass-register input.

A separate guitar firmware shifts the relevant tracking and voicing ranges into
electric-guitar territory. Build it from source with:

`make INSTRUMENT=guitar`

The controls, modes, and preset system are otherwise identical. One firmware
variant supports one instrument range at a time; reflash the pedal to switch.
