# DIY Digital Bass Pedal — Project Plan

## Overview

A multi-mode digital effects pedal for bass guitar, built on the Electro-Smith Daisy Seed. Three independent effect modes are selectable via Switch 3. Presets are global across modes — each preset slot carries its own mode and full parameter state. Slots are organised into 3 banks of 8 (24 reachable presets, storage sized for up to 6 banks). Only the active mode runs at any time — inactive modes have zero CPU cost.

**Mode A — Bordun** (fully specced in `MODE_A_DRONE.md`)
Inspired by the Moog MoogerFooger FreqBox (MF-102). An internally generated oscillator, amplitude-controlled by an envelope follower tracking the bass input, mixed back with the dry signal. No oscillator sync, no FM modulation.

**Mode B — Sprawl** (fully specced in `MODE_B_GRANULAR.md`)
Inspired by the Chase Bliss Mood's "micro-looper as collaborator" philosophy. Rolling buffer → grain scheduler → pitch-shifted voices → gesture-reactive tonal shaping → feedback loop. The wet output sits above the dry bass in frequency and rhythmically around it. Reuses EnvFollower, PitchTracker, MoogLadder, and wavefolder from Mode A.

**Mode C — Schism** (discovery doc in `MODE_C_DISCOVERY.md`)
Two-stage distortion → filter chain. Switchable distortion (bipolar K4: sine wavefolder / Chebyshev waveshaper, gated bit-flipper / Tube-Screamer→tube-amp overdrive, or pitch-tracked synth oscillator) and filter (Moog ladder, Grendel formant, phaser). Envelope follower modulates the filter. Reuses EnvFollower, MoogLadder, and wavefolder from Mode A.

---

## Hardware Platform

### Core Module
- **[Electro-Smith Daisy Seed 65 MB](https://electro-smith.com/products/daisy-seed)** — STM32-based DSP platform, 480 MHz, 32-bit float, 96 kHz audio
- **Memory:** 128 KB internal flash, 64 MB QSPI flash (the 65 MB variant), 512 KB SRAM
- Programmable in C++/Arduino, PureData/PlugData, or Max/MSP Gen~
- Source: **Thonk (UK)** — ships to Austria

**QSPI migration plan:** The firmware targets the 128 KB internal flash. When usage gets uncomfortable, switch the linker script from `STM32H750IB_flash.lds` to `STM32H750IB_qspi.lds` (one-line Makefile change). The 64 MB QSPI flash is effectively unlimited for this project; the Cortex-M7 instruction cache (16 KB) keeps audio-hot code at full speed and the tight DSP loops that run every sample stay cached permanently. Requires the Daisy bootloader (already in use for DFU flashing).

### Pedal PCB / Kit
- **[Hothouse DSP Pedal Kit](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit)** by Cleveland Music Co.
- Provides: audio I/O buffering, power filtering, voltage regulation, noise filtering, all hardware controls pre-specced
- Includes: 6 potentiometers, 3 × 3-position toggle switches, 2 footswitches, jacks, LED
- Daisy Seed purchased separately and seated on the Hothouse PCB
- Source: **Reverb.com** (Cleveland Music Co. store) — ships to Europe

### Additional Sourcing (Austria/EU)
- Components if needed: **Musikding.de** (Germany, fast shipping to AT)

---

## Signal Chain (Mode A)

```
Input ──┬──────────────────────────────────────────────► [Mix] ──► Output
        │                                                   ▲
        └──► [Full Wave Rectify] ──► [4-pole 33Hz LP] ──► [Threshold Gate]
                                                              │
                                                           [VCA gain]
                                                              │
                                                    [PolyBLEP Oscillator]
                                                              │
                                                    [Huovilainen Ladder LP]
                                                              │
                                                           [Mix] ──────────┘
```

Full detail in `MODE_A_DRONE.md`.

---

## Multi-Mode Architecture

```cpp
switch (current_mode) {
    case MODE_DRONE:    ProcessDrone(in, out); break;
    case MODE_GRANULAR: ProcessGranular(in, out); break;
    case MODE_FREQSHIFT: ProcessFreqShift(in, out); break;
}
```

Only the active branch executes per audio block. No CPU cost for inactive modes.

---

## Global Controls

These are consistent across all modes:

| Control | Function |
|---|---|
| Switch 3 | Mode select — Bordun / Sprawl / Schism |
| Footswitch 1 | Preset navigation (see Preset System below) |
| Footswitch 2 | Bypass (true bypass via Hothouse relay). Long press: save mode |
| FS1 + FS2 short tap | Cycle bank (1 → 2 → 3 → 1) |
| FS1 + FS2 held 2 s | Enter DFU bootloader for flashing |

Switches 1–2 and knobs 1–6 are mode-specific — see each mode's spec file.

---

## Preset System

Inspired by the EHX Ring Thing UX, adapted for a two-LED, two-footswitch interface. Implementation reference: `docs/PRESET_IMPL.md`.

### Slots, banks, edit buffer

- **One global edit buffer.** Holds knobs + sw1 + sw2 + mode. Persisted to flash. Manual mode = the edit buffer with no preset loaded; manual is fully WYSIWYG, no hidden state outside presets.
- **8 stored presets per bank, 3 reachable banks** (storage sized for 6 banks). Each preset slot stores its own mode — recalling a preset can swap mode + parameters in one footswitch press.
- **Active bank** persists across power cycles. Only the active bank's slots are addressable via FS1 at any moment.

Stored in Daisy Seed onboard flash via DaisySP `PersistentStorage`.

### Navigation (Normal Mode)

**FS1 short press** cycles through: Manual → Preset 1 → … → Preset 8 → Manual → … within the active bank.

**FS1 long press** jumps directly to Manual mode from any preset, reading current hardware (knobs + SW1 + SW2 + SW3) into the edit buffer.

**FS1 + FS2 short tap** (released before the long-press threshold) cycles the active bank: 1 → 2 → 3 → 1. Both LEDs play a brief Roman-numeral burst to confirm the new bank.

Loading a saved preset copies its stored values into the edit buffer including its mode. Knob values jump immediately — no pickup mode.

**Empty (unsaved) slots** act like manual mode: hardware knob positions flow into the edit buffer. No dirty tracking — knobs write through freely. LED 1 still shows the slot number. Saving to a slot marks it as saved; subsequent loads will use the stored values.

### Bank cycling behaviour (per context)

- **Manual** — bank advances, edit buffer preserved, active preset stays 0. Only the addressable slot space changes.
- **On a saved preset** — bank advances, then the same slot number loads from the new bank (saved → loads + clears dirty; empty → manual-like, LED 1 still shows the slot number).
- **In save mode** — bank advances, save target slot number unchanged. The next FS2 long-press writes into the new bank's slot. This is the cross-bank save path.

### Dirty State (Preset Edited)

When a preset is loaded and any knob, SW1, SW2, or SW3 is moved past its threshold, the edit buffer diverges from the stored preset. This is the **dirty** state:

- **LED 2** switches from solid on to a **rapid flash** pattern.
- The stored preset slot is not modified — only the edit buffer changes.

While dirty:
- **FS1 short press** → **reloads the current preset**, reverting the edit buffer to the stored values. LED 2 returns to solid.
- **FS1 short press** does **not** cycle while dirty. Reload first, then press again to cycle.

SW3 dirty marking detects **physical movement** of the switch, not a mismatch between the loaded preset's stored mode and the SW3 position. Loading a Mode B preset while SW3 sits at Mode A is fine — only an actual switch flip marks dirty.

### LED Preset Indication (LED 1)

LED 1 indicates the active preset using a Roman-numeral-inspired encoding: **I** (short blink), **V** (long blink).

| State | Symbol | LED 1 pattern |
|---|---|---|
| Manual (no preset) | — | Off |
| Preset 1 | I | short |
| Preset 2 | II | short, short |
| Preset 3 | III | short, short, short |
| Preset 4 | IV | short, long |
| Preset 5 | V | long |
| Preset 6 | VI | long, short |
| Preset 7 | VII | long, short, short |
| Preset 8 | VIII | long, short, short, short |

Default timing (source of truth: `docs/ux-demo.html`, mirrored in `src/constants.h`):

| Constant | Value | Description |
|---|---|---|
| I (short) on | 150 ms | Short blink duration |
| V (long) on | 950 ms | Long blink duration |
| Element gap | 200 ms | Gap between symbols |
| Repeat gap | 700 ms | Gap before pattern repeats |
| Dirty flash | 50/50 ms | On/off time for dirty indicator (LED 2) |
| Save mode blink | 150/150 ms | On/off time for save mode (LED 2) |
| Save confirm burst | 75/75 ms for 500 ms | Rapid flash after save confirmed (LED 2) |
| Long press threshold | 700 ms | FS1/FS2 long press detection |
| Both-FS bootloader hold | 2000 ms | Both FS held to enter DFU |
| Bank burst total | 1200 ms | Total time for the bank-switch indicator |
| Bank burst gap | 150 ms | Pause between bank-burst pulses |
| Bank burst flicker | 20 ms | On/off chunk inside each bank-burst pulse |
| Bank burst trailing hold | 400 ms | Pause after burst, before LEDs return to normal |

### Bank-switch burst (both LEDs)

When the user cycles banks, both LEDs play a one-shot Roman-numeral burst for the new bank number, then yield back to normal LED state (LED 1 preset blink, LED 2 bypass/dirty/save).

- **Both LEDs in sync** — visually distinct from LED 1's solo preset blink and LED 2's solo state indicators.
- **Each pulse is internal fast flicker** — deterministic 20 ms on/off chunks throughout the pulse, reading visually distinct from a clean pulse.
- **Fixed total burst time, 1.2 s.** Bank 1 = one 1200 ms pulse; bank 2 = two 525 ms pulses + 150 ms gap; bank 3 = three 300 ms pulses + 2 × 150 ms gaps. Pulse count alone differentiates banks 1–3 (I/V duration distinction is dropped — going beyond 3 banks would need it back).

### Save Mode

**FS2 long press** → enters save mode.

- **Target slot is pre-selected** to the currently loaded preset (or 1 from manual).
- **LED 2** blinks fast (distinct from the dirty-state flash) to indicate save mode.
- **LED 1** shows the target slot's blink pattern.

While in save mode:
- **FS1 short press** → cycles target slot (1 → … → 8 → 1).
- **FS1 + FS2 short tap** → cycles bank. Target slot unchanged; the save will land in the new bank's slot.
- **FS2 long press** → **confirms save**. Edit buffer is written to the selected bank/slot. LED 2 flashes rapidly for ~500 ms. Returns to normal mode (preset now clean).
- **FS2 short press** → **cancels save**.

The edit buffer captures its current mode at save time, so the stored slot carries the active mode.

### Mode Switching (SW3)

SW3 is a soft control, treated uniformly with SW1 and SW2:
- **Manual:** SW3 movement updates the edit buffer's stored mode and the active audio mode immediately.
- **On a saved preset:** SW3 movement marks the preset dirty and the audio mode follows SW3. FS1 short reverts to the preset (including its stored mode).

There is no per-mode state preservation any more — there is only one global edit buffer.

### Power Cycle Behavior

On boot, the pedal restores: active bank, active preset, dirty flag, full edit buffer (including mode). Returns exactly to where the user left off.

**Persistence policy:** discrete navigation events (preset cycle, bank cycle, jump to manual, SW3 move, knob/switch movement) arm a debounced auto-save. After 2 s of inactivity the state is written to flash. No changes → no flash writes. Save-confirm (FS2 long) writes immediately.

**Migration:** if flash holds the old per-mode layout (version 2), it's migrated on boot — each mode's 8 presets become bank 1/2/3 slots, with each slot's stored mode set to its source mode. Existing presets are preserved.

**First boot (factory state):** edit buffer reads current hardware (knobs + SW1 + SW2 + SW3). All preset slots in all banks remain empty.

### Knob Behavior

- **On preset load:** values jump immediately to stored values. Knobs are then "frozen" (not live) — the edit buffer holds the loaded value and ignores hardware until the user moves a knob past `KNOB_DIRTY_THRESHOLD` (2 % of travel). Once moved, the knob takes over and the edit buffer tracks hardware at full ADC resolution.
- **Manual mode on startup:** when no preset is loaded (`active_preset == 0`), the deferred boot snapshot adopts the full physical control set — all knob positions (marked live immediately, no wiggle-past-threshold needed) plus SW1, SW2, and SW3/mode — so anything moved while the pedal was off takes effect at once (fully WYSIWYG). A loaded preset still freezes/restores its stored values.

### Bootloader Entry

**Hold both FS1 and FS2 simultaneously for 2 seconds.** The audio + ADC stop, both LEDs alternate at 75 ms per phase for 1.2 s as visible confirmation, then DFU is entered.

The FS1-alone bootloader path is dropped — it was too easy to trigger by accident while preset-cycling. Only the both-FS hold counts now.

---

## Gain Staging

- **Input:** Hothouse input buffer handles instrument-level signals (passive and active bass) — biases and scales to Daisy Seed ADC range (0–3.3 V). No hardware trim required.
- **Output:** Buffered back to instrument level by Hothouse output stage.
- **Internal balance:** Per-mode, calibrated by ear and committed as compile-time constants in `src/constants.h`.

---

## Current Status

- **Stage 0** — DONE. Hardware confirmed, clean passthrough works.
- **Stage 1** — DONE. MoogOsc class (parabolic + PolyBLEP, saw/tri/square).
- **Stage 2** — DONE. Huovilainen ladder filter with per-stage tanh saturation.
- **Stage 3** — DONE. Pitch controls: K1=semitone (12 steps), K2=octave (C-1–C5), K3=fine tune (±50 cents).
- **Stage 4** — DONE. Envelope follower (Moog topology, no gate) + VCA + equal-power dry/wet mix. Second oscillator on K5 (±12 semitone detune with center dead zone). All 6 knobs wired. Mode A is fully playable.
- **Pitch tracking** — DONE. YIN algorithm with 4x decimation, anti-alias LP at 400 Hz. Three drone sub-modes on Switch 2: fixed pitch, octave-locked tracking (pitch class in target octave), direct tracking. Wrap point set by K1 in fixed mode (default A). See `PITCH_TRACKING.md` for research and improvement plan.
- **Wavefolding** — DONE. Triangle mode: K4 noon→CW applies wavefolding. Envelope subtly modulates fold amount and filter cutoff for dynamic response.
- **Per-waveform gains** — DONE. Independent level trim for saw/tri/square in constants.h.
- **Serial logging** — DONE. `StartLog(false)` + raw knob values printed every ~2 s from main loop using `FLT_FMT3` macros. Safe: no printing from audio callback.
- **Tuning mode** — DROPPED. Originally planned as an in-pedal page-based tuning UI; in practice ear-tuning against `src/constants.h` (with optional serial readouts when wanted) has been enough.
- **Mode B spec** — DONE. Sprawl fully specced in `MODE_B_GRANULAR.md`.
- **Knob remap** — DONE. Measured physical range 0.000–0.968, calibrated `RemapKnob()` with named constants.
- **Preset system** — DONE. Verified on hardware (P.6 listening pass passed). Global model: one edit buffer (knobs + sw1 + sw2 + mode), 3 banks × 8 slots (storage sized for 6), each slot carries its own mode. FS1 cycles within the active bank; FS1+FS2 short tap cycles banks (Roman-numeral burst on both LEDs confirms the new bank); FS2 long enters save mode (banks can also be cycled inside save mode for cross-bank saves). SW3 is a soft control uniform with SW1/SW2 — moves on a saved preset mark dirty by physical movement. FS1+FS2 held 2 s enters DFU (alternating LED burst); FS1-alone bootloader path dropped. Debounced auto-save (2 s after last edit; no-change → no flash write). One-shot v2 → v3 migration preserves existing per-mode presets into banks 1/2/3. Implementation reference in `docs/PRESET_IMPL.md`; planning history in `docs/PRESET_GLOBAL_PLAN.md`.
- **Stage 6** — DONE. Multi-mode scaffold: Switch 3 dispatches `ProcessDrone()` / `ProcessGranular()` / `ProcessFreqShift()`. Modes B and C are dry passthrough stubs. Bypass handled once in `AudioCallback`, mode functions only run when active. Pitch tracker conditional on Mode A.
- **Mode B (Sprawl)** — IN PROGRESS. Granular engine: ring buffer (8 s SDRAM), grain scheduler, 8 voices, Hann windowing, texture shaper (decimator/wavefolder bipolar on SW1 UP, zoned digital glitch on MID — bipolar K4, XOR CCW / bit-rotate CW, ringmod on DOWN), wet HPF 120 Hz, equal-power mix. Knob layout: K1=harmony/shift (meaning follows SW2), K2=buffer range, K3=character/glitch, K4=texture amount, **K5=reverb/feedback bipolar** (CCW=Clouds reverb, center=off, CW=feedback 0.95 ceiling), K6=mix. **SW2 harmony / shift**: UP = fixed interval (K1 ±12 semi), MID = resonance window pick (K1 spans ±36 semi scan), DOWN = **Bode SSB frequency shifter** on the wet bus, inside the feedback loop so each pass cascades the shift — 8-stage Hilbert IIR pair + quadrature osc, K1 bipolar with ±2% deadzone, exponential taper, ±1 kHz at full deflection (grain buffer-read pitch forced to unison in this sub-mode). **Feedback path**: tanh saturator (`FB_SAT_DRIVE = 8`) brings distortion in earlier and self-limits peak level; build-up ducker (slow-attack/slow-release env on prev_wet pulls feedback gain down 1:∞ above a threshold so the loop simmers down between gestures); on-play ducker (dry-env, instant attack / 400 ms release, max 50 % GR) makes space for transients while playing. Direct-texture mode (K2 fully CCW): grain engine bypassed, input→texture shaper, K3=micro-stutter. **Wet-path reverb**: Clouds FDN (vendored from MI, MIT), 32 kHz internal via polyphase 48↔32 resampler, mono in / stereo out (collapsed to mono at mix for future stereo upgrade), reverb tail does not feed ring buffer.
- **Mode C (Schism)** — IN PROGRESS. C.1–C.6 DONE: K5 is a bipolar pre-filter drive (CCW attenuate ~−12 dB, noon unity, CW up to 8× — universal across all SW2 modes). **K4 is bipolar around noon (noon = clean dry) for SW1=UP and SW1=MID.** SW1=UP: CW half = sine wavefolder (`SINEFOLD_DRIVE_MAX`/`COMP_AT_MAX`), CCW half = Chebyshev waveshaper (octave-up / metallic harmonic generator, `MODE_C_CHEBY_H2..H5`, fed by a 2-pole pre-shaper LP `MODE_C_CHEBY_LP_HZ` so T2 makes a clean octave — the Octavia trick; DC at x=0 subtracted out). SW1=MID: CW half = **gated bit-flipper** (XOR of a chosen Q15 bit; K4 sweeps the bit position 0→`MODE_C_BITCRUSH_MAX_BIT`(15); env-gated with 1 ms click-free ramp; 16-entry per-bit loudness-comp table `MODE_C_BITCRUSH_COMP_TABLE`), CCW half = **Tube-Screamer→tube-amp overdrive**: pre-clip HPF (`MODE_C_OD_HP_HZ`) → pedal saturation stage → a touch of low-passed clean for body (`MODE_C_OD_CLEAN_MIX`/`_CLEAN_LP_HZ`) → amp saturation stage. Both stages share one symmetric primitive `Saturate()` (clamped cubic `x − x³/3`, ±2/3; rational `x/(1+|x|)` a one-line alt), asymmetry only via per-stage bias offset (`MODE_C_OD_BIAS`/`_AMP_BIAS`). K4-CCW is a staged master gain: pedal ramps first (curved `MODE_C_OD_K4_CURVE`) to `MODE_C_OD_DRIVE_MAX`, amp holds at unity until `MODE_C_OD_AMP_KNEE` then ramps to `MODE_C_OD_AMP_DRIVE` over the top. No in-stage dry/wet (clean = K6); output makeup curves from a boost at noon (`MODE_C_OD_COMP_AT_NOON`, matches clean level) to a cut at full CCW (`MODE_C_OD_COMP_AT_MAX`). SW1=DOWN: pitch-tracked synth oscillator (full-range K4 saw↔rect morph, raw-env VCA pre-filter). Moog ladder under SW2=UP using `MoogLadderV2` (single input saturator post-feedback, no per-stage tanh darkening; k-cutoff cross-comp for consistent self-osc; (1+res) input level comp; +0.05 asymmetric DC bias for transistor-flavored even harmonics) — K1 cutoff (`MapCutoffModeC`, 20 Hz–8 kHz exp), K2 sqrt-curved resonance up to self-osc, K3 bipolar env-to-cutoff with passive-bass ×10 normalization, deepened sweep (`MODE_C_ENV_MOD_RANGE = 420`) and a response curve (`MODE_C_K3_CURVE = 2.0`, fine near noon / coarse toward the extremes, endpoints fixed), 10 kHz top clamp and a 20 Hz floor (`MODE_C_CUTOFF_MIN_HZ`) so the K1-CCW / K3-CW corner shuts fully. **Audio-rate cutoff self-FM** on the Moog: K5 from `MODE_C_MOOG_FM_START`(0.4, a touch before noon) up to full CW fades in modulation of the ladder cutoff by the filter input (`mod_cutoff *= 1 + depth·amt·in`, `MODE_C_MOOG_FM_DEPTH = 1.5`), splattering sidebands around the resonant peak; clamped to [MIN,MAX] Hz, Moog only. Phaser under SW2=DOWN (`phaser.h`) — 6-stage allpass, per-stage detune (`PHASER_STAGE_SPREAD`), tanh-saturated feedback loop up to bounded self-osc (`PHASER_FB_MAX = 0.98`); K1 notch centre, K2 feedback, K3 bipolar LFO rate + shape (CCW triangle / CW sample-and-hold, centre = static). Provisional, still hunting for a final third filter (Plague Bearer modelled but not kept — see `docs/MODE_C_DISCOVERY.md`). Grendel formant filter under SW2=MID — 4 parallel RBJ BPF biquads, vowel path oo→oh→ah→eh→ee (CCW dark/closed → CW bright/open), K1 path, K2 size (mouth scale, ×0.5 → ×1.6), K3 bipolar env coupled on path *and* size (block-rate coef updates) — CCW K3 opens path toward ee + tightens size, CW K3 closes toward oo + opens size (±20% size swing at full K3 + hard pluck). Uses its own 400 ms slow-swell env smoother regardless of K3 sign (snap shape reserved for the ladder). Mode A's `MoogLadder` is intentionally untouched (signed off). Per-filter input pads (×0.3 for Moog, Grendel, and phaser) put the filters in their clean sweet zone at K5 noon, with small per-filter post-filter makeup gains (`MODE_C_{MOOG,GRENDEL,PHASER}_MAKEUP = 1.4`) lifting the wet floor without fully compensating the pad (K5 keeps its level + filter-drive double duty). Post-filter signal chain: pre-limit lift `MODE_C_POST_FILTER_GAIN = 1.3` so quieter notes engage the limiter cleanly, then `PeakLimiter` with 2-band split at 160 Hz (LF preserved so bass fundamentals don't duck under HF resonance peaks; HF soft-knee limited with "warmth-when-working" tanh blend that only blooms while the limiter is reducing gain). The amp-env VCA (`wet *= env_val * MODE_C_VCA_GAIN`) is **currently commented out** in `ProcessFreqShift` (auditioning the wet path without env-driven amplitude shaping); the SW1=DOWN synth path keeps its own raw-env VCA. The ladder filter env reads an asymmetric A/R smoother on the shared env (`MODE_C_FILTER_ENV_RELEASE_MS = 150` at noon, shortening toward `RELEASE_MIN_MS = 40` further CW; `ATTACK_MS = 600` constant swell — K3-sign-dependent shape: peak follower CW, slow rise CCW) so cutoff sweeps don't twitch on transients. Grendel uses its own dedicated slow-swell smoother (`MODE_C_GRENDEL_ENV_ATTACK_MS = 400`, instant snap-back) regardless of K3 sign — formant motion always breathes, never snaps.
- **FLASH at ~95%** — climbed during the global preset / banks rewrite plus Mode C work. Migration still ready via one-line linker swap to `STM32H750IB_qspi.lds` once it pushes past 100% or audio code needs the headroom.
- **Next** — Mode C: tune the bit-flipper per-bit loudness-comp table, the Chebyshev harmonic mix, the Moog self-FM depth, and the filter-env A/R times on hardware; decide whether to re-enable the amp-env VCA; settle the SW2=DOWN slot (the phaser is provisional). All gated on pedal access. Pitch tracking improvements still deferred.

## Staged Development Timeline

Small iterations. Each stage produces a working, testable artifact.

### Stage 0 — Hardware bring-up ✓
Flash Hothouse blink example. Confirm clean audio passthrough with bass → Daisy → amp. No DSP. Proves the hardware before any original code.

### Stage 1 — Oscillator ✓
MoogOsc class (parabolic waveshaper + PolyBLEP). Fixed 110 Hz pitch, output to both channels. Knobs control oscillator character for ear-tuning. Waveform toggle wired.

### Stage 2 — Huovilainen ladder filter ✓
Ladder filter on oscillator output. K5 controls cutoff (80 Hz – 8 kHz, exponential). Drive and cutoff offset from compile-time constants. Per-stage tanh saturation for Moog warmth.

### Stage 3 — Pitch controls ✓
K1=semitone (12 quantized steps, C–B), K2=octave (7 positions, C-1–C5), K3=fine tune (±50 cents continuous). Oscillator plays selectable pitches through the ladder filter.

### Stage 4 — Envelope follower + VCA + mix + tracking + wavefold ✓
Envelope follower (Moog topology, no gate) + VCA + equal-power mix. Three drone sub-modes (Switch 2): fixed pitch, octave-locked tracking, direct tracking. YIN pitch tracker with 4x decimation. Wavefolding on triangle (K4 noon→CW). Envelope modulates filter cutoff and fold amount. Second oscillator (K5 detune). Per-waveform gain constants. Octave-locked wrap point fixed to A (compile-time constant `TRACKING_WRAP_NOTE`). All 6 knobs + Switch 1/2 wired. Mode A is fully playable.

### Stage 5 — Preset system ✓
Original per-mode model first, then migrated to a global model: one edit buffer (knobs + sw1 + sw2 + mode), 3 banks × 8 slots (storage sized for 6), each slot carries its own mode. Audio callback reads from the edit buffer, not hardware. FS1 cycles slots within the active bank, FS2 toggles bypass / enters save mode, FS1+FS2 short tap cycles banks (Roman-numeral burst on both LEDs), FS1+FS2 held 2 s enters DFU. SW3 is a soft control — moves it on a saved preset mark dirty. Debounced auto-save (2 s after last edit) replaces the original 30 s interval. One-shot v2 → v3 migration preserves existing per-mode presets.

### Stage 6 — Multi-mode scaffold ✓
Switch 3 dispatches to `ProcessDrone()`, `ProcessGranular()`, `ProcessFreqShift()`. Modes B and C are dry passthrough stubs. Bypass handled once in `AudioCallback`. Pitch tracker runs only in Mode A. Architecture ready for mode implementation.

### Stage 7 — Enclosure
Drill the kit enclosure and label.

### Deferred
- **Mode B direct-texture mode** — DONE. K2 fully CCW bypasses grain engine, K3 = micro-stutter.
- **Mode C** — Schism: implement per `MODE_C_DISCOVERY.md`

---

## Development & Testing Strategy

### Phase 1 — Mac Prototype (JUCE), optional
DSP core as a JUCE `AudioProcessor` plugin. Run as AU/VST in Reaper. Tune constants in a GUI with exact value readouts before touching hardware. Useful for early iteration on oscillator character and filter tone.

### Phase 2 — Hardware
Port to Daisy. Final tuning happens by ear against real hardware and bass, with values committed back into `src/constants.h`.

### Phase 3 — Enclosure
Drill, paint, label.

### DSP Implementation Order (within Mode A)
1. PolyBLEP oscillator (Stage 1)
2. Huovilainen ladder filter (Stage 2)
3. Moog envelope follower (Stage 3)
4. Mix and gain staging (Stage 3–4)
5. Control layer + preset layer (Stage 4–5)
6. Multi-mode scaffold (Stage 6)

---

## Key Technical References

- **Huovilainen ladder filter model** — Antti Huovilainen, "Non-linear Digital Implementation of the Moog Ladder Filter" (DAFx 2004)
- **PolyBLEP anti-aliasing** — Välimäki & Pakarinen, standard implementation widely documented
- **Moog envelope follower topology** — full wave rectifier + 4-pole 33 Hz LP, consistent across MF-101, MF-102 per circuit traces
- **Moog oscillator fundamental weight** — parabolic ramp from capacitor charging physics; see `MODE_A_DRONE.md` for modeling approach
- **DaisySP library** — https://github.com/electro-smith/DaisySP
- **Hothouse kit** — https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/
- **Daisy Seed** — https://www.thonk.co.uk/shop/electrosmith-daisy-seed/

---

## Document Map

- `PROJECT.md` — this file. Top-level plan, hardware, staging, multi-mode architecture.
- `MODE_A_DRONE.md` — Mode A (Bordun) full spec: oscillator model, envelope follower, ladder filter, controls, presets.
- `MODE_B_GRANULAR.md` — Mode B (Sprawl) full spec: granular processor, signal chain, controls.
- `MODE_C_DISCOVERY.md` — Mode C (Schism) discovery notes: distortion + filter chain, implementation stages.
- `AGENTS.md` — AI agent entry point. Routes agents to the right doc for the task.
