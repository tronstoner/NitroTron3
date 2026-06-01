# Task — Add Clouds Wet-Path Reverb to Mode B

## Goal

Add the Mutable Instruments Clouds wet-path reverb (the small one used inside the granular processor, not the alt-mode reverb) to Mode B's wet bus. K5 becomes bipolar: CCW = reverb, center = off, CW = feedback (existing behavior preserved).

## Reverb requirements

- **Source:** Mutable `eurorack/clouds/dsp/fx/reverb.h` + `fx_engine.h`. Use Émilie Gillet's exact default parameters. **MIT license** (per the file headers — compatible with our GPLv3); preserve the MIT copyright notice in our ported files.
- **stmlib shims required:** `DISALLOW_COPY_AND_ASSIGN`, `STATIC_ASSERT`, `MAKE_INTEGRAL_FRACTIONAL` macros (from `stmlib/stmlib.h`), `Clip16()` (from `stmlib/dsp/dsp.h`), and `CosineOscillator` (`stmlib/dsp/cosine_oscillator.h`, ~60 lines). Vendor into `src/stmlib_shims.h` / small companion file.
- **Mono input, stereo-ready wet bus:** Clouds uses stereo `FloatFrame`. Feed input as `in*2.0f` into `c.Read(...)` (mono input is fine — pedal jack is mono regardless of any future stereo upgrade). Keep **both** wet outputs (`wet_l`, `wet_r`) live end-to-end; collapse to mono only at the final mix point (see "Stereo readiness" below).
- **Internal sample rate: 32 kHz.** Decimate 48→32 in, interpolate 32→48 out via polyphase. Non-negotiable — part of the sound.
- **Coefficients (delay lengths, all-pass gains, damping, mod rates) copied verbatim.** Do not retune.
- **Position:** after wet HPF, before mix. Reverb does not feed the ring buffer.
- **Check DaisySP-LGPL** for an existing port preserving 32 kHz before reimplementing.

## K5 split

- CCW (with deadzone): reverb dry/wet, 0 → 1.0
- Center ±`K5_CENTER_DEADZONE`: off
- CW (with deadzone): feedback amount, 0 → 0.95 (unchanged)

## Stereo readiness

The pedal is mono today, but Clouds natively produces decorrelated L/R (different LFO phases, asymmetric loop halves). Carry both channels through the wet bus so a future stereo output is a one-line change.

**Signatures (stereo-shaped from day one):**
- `Reverb::Process(float in, float& out_l, float& out_r)` — mono in, stereo out.
- `Downsampler::Process(float in_48k, float* out_32k, size_t)` — mono, one filter state.
- `Upsampler::Process(float in_l_32k, float in_r_32k, float* out_l_48k, float* out_r_48k, size_t)` — two parallel filter states, same coefficients.

**Single collapse point** (the only mono-specific line in the chain):
```cpp
float wet_mono = (wet_l + wet_r) * 0.5f;  // remove for stereo
```
Lives at the Mode B mix point. Going stereo = delete that line, route `wet_l`/`wet_r` to `out[0]`/`out[1]` alongside the dry. No template gymnastics, no abstract channel base class — just two channels of float wherever the wet bus exists.

**Cost of stereo-readiness:**
- Upsampler runs 2 channels instead of 1: ~0.5% extra CPU.
- ~8 KB extra state for the second upsampler filter buffer (SDRAM).
- Inside `Reverb::Process`, both wet outputs are computed already — no extra CPU there.

## Files

**New:**
- `src/reverb.h` / `.cpp` — Clouds FDN, verbatim coefficients, mono in / stereo out
- `src/resampler.h` — 48↔32 kHz polyphase (mono downsampler + stereo upsampler)

**Modify:**
- `src/constants.h` — add `REVERB_INTERNAL_SR_HZ`, `K5_CENTER_DEADZONE`
- Mode B process function — instantiate reverb + resampler, wire K5 bipolar
- `docs/MODE_B_GRANULAR.md` — update signal chain diagram, K5 controls row, add reverb block description, add Clouds reference
- `docs/PROJECT.md` — fold reverb work into Stage B.5
- `README.md` — update Mode B K5 row

## Constraints

- No commits without explicit user confirmation (`AGENTS.md`).
- Verify resampler with white noise (clean roll-off at 16 kHz, no aliasing) before connecting reverb.
- Only ear-tuned parameter is the K5-CCW → reverb-amount curve. Algorithm stays untouched.
- Budget check: **~32 KB delay buffer** (`uint16_t[16384]`, 12-bit compressed format) — goes in **SDRAM** (`DSY_SDRAM_BSS`), not on-chip SRAM. ~1–2% CPU reverb + ~1% resampling (mono 48→32 in, stereo 32→48 out). Well within Mode B headroom.
