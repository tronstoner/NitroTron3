# DSP Inventory — algorithms, code size, CPU and memory

Analysis snapshot of the **implemented** DSP building blocks and their resource
cost. Written 2026-07-26 against branch `feature/ChronoTron3` at `a11dddb`.

This is an *analysis* document, not a spec. Nothing here changes behaviour; it
exists so the next "can we afford X?" question has numbers behind it.

> **Scope.** Covers the `nitrotron3` pedal target (bass profile) — the only
> target with real DSP today. `pedals/chronotron3/` is currently a shell +
> `Module` interface with three stub modules (`vestige`/`mnemonic`/`ignis`,
> 20–30 lines each, `Process()` compiles to 20 bytes); it is excluded from the
> numbers below and was under active development while this was written.

---

## 1. How the numbers were produced

Three independent measurements, none of them guesses:

| Quantity | Method |
|---|---|
| **Code size** | Cross-compiled `pedals/nitrotron3/main.cpp` for Cortex-M7 (`-O2`, project flags), then `objdump -h` per `.text.*` section and a real `--gc-sections` link against `libdaisy`/`libdaisysp` using `STM32H750IB_sram.lds`. |
| **Memory** | `arm-none-eabi-size` + `nm --print-size` on that linked ELF, plus the linker's `--print-memory-usage` region report. Post-`--gc-sections`, so dead code/data is already excluded. |
| **CPU** | (a) Host benchmark: every block compiled natively `-O2` and run over 20 s of synthetic bass signal, reported as ns per 48 kHz output sample. (b) Static Cortex-M7 instruction counts per block from the ARM disassembly. (c) A census of libm calls per sample (`powf`/`tanf`/`expf`/`sinf`/`tanhf`), because that is exactly where host and M7 diverge. |

The build in `build/` at the time of writing was a **ChronoTron3** build (another
agent's work in progress), so all of the above was compiled and linked into a
scratch directory. `build/` was not touched.

**The one caveat that matters:** the host has fast, vectorised libm. The
Cortex-M7 runs newlib software `powf`/`tanf`/`expf`/`tanhf` at roughly
100–400 cycles per call. Any block that calls those per sample is *cheap on the
host and expensive on the pedal*. Both views are given below, and where they
disagree, that disagreement is the finding.

---

## 2. Inventory

`sr` = per 48 kHz sample. "State" = bytes of instance data in the final image.
Code size is only quotable where the compiler kept the block out of line; blocks
marked *(inlined)* are folded into the mode function that uses them.

### 2.1 Oscillators & synthesis

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `MoogOsc` | `blocks/moog_osc.h` | 86 | Parabolic waveshaper + PolyBLEP; saw/tri/square, bidirectional phase (through-zero FM safe) | A, C | 8 B | 576 B | 5.4 |
| `DroneOsc` | `blocks/synth_osc_a.h` | 220 | 7 × `MoogOsc`. Saw = detuned unison cloud; tri = Haible just-intonation ensemble; square = PolyBLEP PWM. CW half = linear through-zero FM (LP → partial-norm → tanh → DC block) | A (K5) | 232 B | 1 426 B | 29–37 |
| `ModeCSynth` | `blocks/synth_osc_c.h` | 170 | 7 × `MoogOsc` hypersaw with staged pair fade-in ↔ PolyBLEP rect + PWM, K4 morph | C SW1=DOWN | 176 B | 988 B | 34–35 |

### 2.2 Filters

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `MoogLadder` | `blocks/moog_ladder.h` | 73 | Huovilainen 4-pole, per-stage Padé-tanh saturation (8 saturators/sample) | A | 36 B | *(inlined)* | 39.7 |
| `MoogLadderV2` | `blocks/moog_ladder_v2.h` | 85 | Stilson/Smith 4-pole, single input saturator, k↔cutoff cross-comp, asymmetric bias | C SW2=UP | 36 B | *(inlined)* | 15.9 |
| `Grendel` | `blocks/grendel.h` | 88 | 4 parallel RBJ bandpass biquads, vowel-path interpolation; coefficients per **block** | C SW2=MID | 148 B | *(inlined)* | 4.1 |
| `Phaser` | `blocks/phaser.h` | 234 | 6 × 1st-order allpass, per-stage coefficient detune, tanh-saturated feedback, tri / S&H LFO | C SW2=DOWN | 152 B | 736 B | 18–43 |
| `ModeAHpf` | `blocks/mode_a_hpf.h` | 38 | 2 cascaded one-pole HP, block-smoothed cutoff | A (K4-CW) | 20 B | *(inlined)* | 3.3 |
| `PeakLimiter` | `blocks/peak_limiter.h` | 71 | 2-band split (one-pole @160 Hz), HF-only soft-knee peak limiter, GR-proportional tanh warmth | C post-filter | 24 B | *(inlined)* | 3.4 |
| `EnvFollower` | `blocks/env_follower.h` | 51 | Moog topology: full-wave rectifier → 4 cascaded one-poles | A, B, C | 28 B | *(inlined)* | 3.4 |

### 2.3 Distortion / texture

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `BitCrush` | `blocks/bitcrush.h` | 69 | Deterministic Q15 single-bit XOR, K4 sweeps bit index, env-gated 1 ms ramp, per-bit loudness LUT | C SW1=MID CW | 4 B | *(inlined)* | 1.4 |
| `GlitchEvents` | `blocks/glitch_zones.h` | 246 | Stochastic (xorshift32) event scheduler; CCW = bit-flip payload, CW = timing payload (freeze / stutter loop / reverse) over a 50 ms ring | B SW1=MID | **9 648 B** | 692 B | 4.1 |
| `Chebyshev`, `Saturate`, `Wavefold`, `WrapFold` | `main.cpp` helpers | ~50 | T2–T5 harmonic generator; clamped cubic `x−x³/3`; reflective folder | A, C | — | *(inlined)* | <1 |

### 2.4 Pitch / spectral

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `PitchTracker` | `blocks/pitch_tracker.h` | 206 | YIN with cumulative-mean normalisation. `Feed()` = HP + 4-pole AA LP + 4× decimate + ring write (audio rate). `Update()` = the O(lag × window) search, **main loop only** | A, B, C | **4 164 B** | *(inlined)* | 4.0 feed / **182 total** |
| `polyoct::PolyOctave` | `blocks/poly_octave.h` | 382 | ERB-PS2 POG engine: two-stage FIR decimate 48→8 kHz, **80 complex analytic bandpass biquads**, per-band phase scaling (×2, ×4, ×½), 3 × two-stage FIR interpolate back to 48 kHz | C SW1=MID CCW | **7 200 B** + 3 072 B voice buffers | 3 426 B | **102.9** |
| `FreqShifter` | `blocks/freq_shifter.h` | 61 | Bode SSB: 2 × 4-stage allpass Hilbert pair + quadrature oscillator | B SW2=DOWN | 76 B | *(inlined)* | 7.1 |

### 2.5 Granular / time-domain

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `RingBuffer` | `blocks/ring_buffer.h` | 51 | Mono circular buffer, linear-interpolated fractional read | B | 12 B + **1 536 000 B SDRAM** | *(inlined)* | — |
| `GrainVoice` | `blocks/grain_voice.h` | 109 | Adaptive Tukey/Hann windowed grain, fractional rate (pitch), reverse, loop repeat (stutter) | B (×8) | 352 B (8 voices) | *(inlined)* | 30.2 (8 voices + ring) |
| Grain scheduler | `main.cpp` | ~200 | Interval/overlap model, per-grain length skew, transient burst, harmony re-roll, read-overrun safety | B | — | *(in `ProcessGranular`)* | included above |
| `StutterVoice` | `main.cpp` | 60 | Two-voice Tukey crossfade micro-stutter | **dead** (`if (false)`) | 0 B | 0 B | — |

`StutterVoice` and its 38 400 B `stutter_buf` are confirmed **absent from the
linked ELF** — `--gc-sections` strips them. Deleting them is source hygiene only,
as `PROJECT.md` states.

### 2.6 Reverb & rate conversion

| Block | File | LOC | Algorithm | Used by | State | Code | Host ns/sr |
|---|---|---|---|---|---|---|---|
| `clouds::Reverb` | `blocks/clouds/reverb.h` | 190 | Griesinger/Dattorro FDN: 4 input allpass diffusers + 2 × (2 AP + delay) loop, LFO-modulated. Vendored from Mutable Instruments Clouds (MIT), runs at 32 kHz | B (K5 CCW) | 68 B + **32 768 B SDRAM** | 1 088 B | 11.5 |
| `clouds::FxEngine` | `blocks/clouds/fx_engine.h` | 303 | Template delay-line/allpass DSL, 12-bit companded storage | — | *(template)* | *(inlined)* | — |
| `Resampler<L,M,N>` | `blocks/resampler.h` | 107 | Polyphase rational resampler, 16-tap sinc·Hamming prototype. 3 instances: 48→32 mono down, 32→48 ×2 up | B | 716 B | 272 B init | **22.6** |

The three resamplers cost **about twice as much as the reverb they feed**.

### 2.7 Support layers (not DSP, but they take flash)

| Unit | File | LOC | Role | State | Code |
|---|---|---|---|---|---|
| `PresetSystem` | `pedals/nitrotron3/preset_system.h` | 1 038 | 3 banks × 8 slots, edit buffer, dirty tracking, Roman-numeral LED patterns, save mode, bank burst, debounced autosave, v2→v3 migration | 2 576 B + 2 868 B flash mirror | **5 240 B** (`ProcessFootswitches` 2 902 + `TickLeds` 1 076 + `Init` 1 262) |
| `constants.h` | `pedals/nitrotron3/constants.h` | 653 | All compile-time DSP tuning + instrument profile | 596 B rodata | — |
| `ControlSurface` | `src/core/io/control_surface.h` | 122 | Policy-free Hothouse read layer (knobs, toggles, FS edges/holds) | small | *(ChronoTron3 only)* |
| `knob_map.h` | `src/core/util/knob_map.h` | 45 | `Mapf` / `MapCutoff` / `MixCurve` / `Quantize` / `MidiToFreq` / `RemapKnob` | — | *(inlined)* |

---

## 3. Rankings

### 3.1 Most code-heavy

Measured `.text` in the linked ELF. The three mode functions dominate because
almost every block is inlined into them.

| # | Symbol | Bytes | What's in it |
|---|---|---|---|
| 1 | `ProcessGranular` | **6 564** | Grain scheduler + 8 voices + texture shaper (3 modes) + feedback duckers + reverb pipeline + mix |
| 2 | `ProcessFreqShift` (Mode C) | **6 304** | 3 drive branches × 3 filter branches + 16 smoothers + limiter + env shapers |
| 3 | `PresetSystem` (3 fns) | **5 240** | Footswitch policy, LED patterns, banks, flash |
| 4 | `main()` | 3 474 | Init of every block + bootloader gesture |
| 5 | `polyoct::PolyOctave::ProcessBlock` | 3 426 | Unrolled FIR decimator + 3 interpolators |
| 6 | `ProcessDrone` | 3 132 | Pitch/sub-mode logic + bipolar K4 filter + wavefold |
| 7 | `DroneOsc::Process` | 1 426 | 3 waveform branches × 3 K5 engines |
| 8 | `clouds::Reverb::Process` | 1 088 | FDN loop |
| 9 | `ModeCSynth::Process` | 988 | Hypersaw + PWM |
| 10 | `Phaser::Process` | 736 | 6-stage allpass + 2 LFO paths |

**Project code total: 38 524 B `.text` + 12 437 B `.data` + 596 B rodata.**
The rest of the 132 KB image is libDaisy + STM32 HAL + newlib.

Source-line view (a different ranking — `main.cpp` carries all the *policy*,
`poly_octave.h` all the *unrolled coefficients*):

```
1717  pedals/nitrotron3/main.cpp
1038  pedals/nitrotron3/preset_system.h
 653  pedals/nitrotron3/constants.h
 382  src/core/blocks/poly_octave.h
 303  src/core/blocks/clouds/fx_engine.h
 246  src/core/blocks/glitch_zones.h
 234  src/core/blocks/phaser.h
 220  src/core/blocks/synth_osc_a.h
 206  src/core/blocks/pitch_tracker.h
 190  src/core/blocks/clouds/reverb.h
```

### 3.2 Most CPU-heavy

Two rankings, because they disagree — and the disagreement *is* the result.

**(a) Host benchmark** — pure float throughput, fast libm:

| # | Block (worst-case setting) | ns / 48 kHz sample | × baseline |
|---|---|---|---|
| 1 | `PolyOctave::ProcessBlock` (80 bands) | **102.9** | 72× |
| 2 | `Phaser` — triangle LFO, per-stage coefficients | 42.9 | 30× |
| 3 | `MoogLadder` v1 (8 Padé saturators) | 39.7 | 28× |
| 4 | `DroneOsc` saw, 7 voices | 37.2 | 26× |
| 5 | `ModeCSynth` (7-voice hypersaw / rect) | 34–35 | 24× |
| 6 | Granular: ring write + 8 grain voices | 30.2 | 21× |
| 7 | Reverb pipeline (resamplers + Clouds FDN) | 30.4 | 21× |
| 8 | `Phaser` — static / S&H | 18.1–18.7 | 13× |
| 9 | `MoogLadderV2` | 15.9 | 11× |
| 10 | `FreqShifter` (Bode SSB) | 7.1 | 5× |
| — | `EnvFollower`, `ModeAHpf`, `PeakLimiter`, `Grendel`, `GlitchEvents`, `BitCrush` | 1.4–4.1 | 1–3× |
| — | `PitchTracker::Feed` (audio-rate half) | 4.0 | 3× |
| *(main loop)* | `PitchTracker` incl. YIN `Update()` | **182.5** | 128× |

**(b) libm census** — calls per sample, which is what the M7 actually pays for:

| Block | libm calls / sample | Which |
|---|---|---|
| `ModeCSynth` | **8** | 7 × `powf` (inside the 7-voice loop) + `sqrtf` |
| `Phaser` (triangle, per-stage) | **13** | 6 × `tanf` + 6 × `expf` + `tanhf` |
| `DroneOsc` saw | **8** | 6 × `exp2f` + `sqrtf` + `tanhf` |
| `DroneOsc` tri | 3 | `exp2f` + `sqrtf` + `tanhf` |
| `Phaser` (S&H) | 3 | `tanf` + `expf` + `tanhf` |
| `MoogLadderV2` in Mode C | 1 | `tanf` — `SetCutoff()` is called **per sample** for the audio-rate self-FM |
| `FreqShifter` | 2 | `sinf` + `cosf` |
| `GrainVoice` × 8 | up to 16 | `fmodf` per voice + `cosf` during window tapers |
| `PolyOctave`, `clouds::Reverb`, `Resampler`, `EnvFollower`, `MoogLadder` v1 | **0** | hand-rolled `FastInvSqrt` / Padé tanh — deliberately libm-free |

**Combined estimate for the pedal.** Budget is 480 MHz / 48 kHz =
**10 000 cycles per sample**. Using measured M7 instruction counts (× ~1.3 for
FP stalls) plus 100–400 cycles per libm call:

| Rank | Block / config | est. cycles / sample | est. % of budget |
|---|---|---|---|
| 1 | Mode C `ModeCSynth` (SW1=DOWN) | ~4 100 | ~41 % |
| 2 | Mode A `DroneOsc` saw, 7 voices | ~2 900 | ~29 % |
| 3 | `PolyOctave` POG (SW1=MID CCW) | ~2 600 | ~26 % |
| 4 | `Phaser`, triangle LFO | ~2 400 | ~24 % |
| 5 | Mode A `DroneOsc` triangle | ~2 300 | ~23 % |
| 6 | Granular: 8 voices + ring | ~1 300 | ~13 % |
| 7 | `Phaser`, S&H LFO | ~790 | ~8 % |
| 8 | Reverb pipeline (resamplers + FDN) | ~500 | ~5 % |
| 9 | `FreqShifter` | ~330 | ~3 % |
| 10 | `MoogLadderV2` + per-sample `SetCutoff` | ~280 | ~3 % |
| — | `GlitchEvents` / `PeakLimiter` / `MoogLadder` v1 / `Grendel` / `EnvFollower` | 26–270 | <3 % each |

Worst-case mode composites:

| Configuration | est. % of the 48 kHz budget |
|---|---|
| **Mode C, SW1=DOWN synth + SW2=DOWN phaser (triangle LFO)** | **~69 %** ← peak |
| Mode C, SW1=DOWN synth + SW2=UP Moog | ~47 % |
| Mode C, SW1=MID POG + SW2=UP Moog | ~32 % |
| Mode A, saw, K5 CCW unison | ~31 % |
| Mode B, SW2=DOWN shifter + SW1=MID glitch + reverb on | ~26 % |
| Mode A, triangle, K5 CCW | ~26 % |

Plus `PitchTracker::Update()` (YIN) in the **main loop**, not the audio callback —
it cannot cause dropouts, but it does throttle the 10 ms control tick.

**Hardware corroboration:** the comment in `main.cpp:1386-1392` records that
running the POG filterbank unconditionally *starved the main-loop YIN on the
guitar profile* (late hops → octave-hopping tracking), which is why it is gated
on `drive_mode == 1 && k4_ccw > 0.001f`. That is real, observed evidence that
`PolyOctave` is the single heaviest block in the codebase.

### 3.3 Most memory-heavy

| # | Allocation | Bytes | Region | Notes |
|---|---|---|---|---|
| 1 | `grain_sdram_buf` | **1 536 000** | SDRAM | 8 s × 48 kHz × float — Mode B ring buffer |
| 2 | `reverb_buf` | **32 768** | SDRAM | Clouds FDN, `uint16_t` 12-bit companded |
| 3 | `glitch_events` | **9 648** | DTCM/SRAM | 2 400-sample (50 ms) CW timing ring |
| 4 | `polyoct_c` | **7 200** | `.data` | 80 × `BandShifter` (~80 B of coefficients + state each) |
| 5 | `tracker` | **4 164** | `.data` | 1 024-float YIN ring + filter state |
| 6 | `polyoct_sub/up1/up2` | 3 072 | `.bss` | 3 × 256-float per-voice block buffers (block size is 48; sized for 256) |
| 7 | `flash_storage` | 2 868 | `.bss` | `PersistentStorage<StorageData>` — 6 banks × 8 slots |
| 8 | `preset` | 2 576 | `.bss` | Preset system runtime state |
| 9 | `rev_upsampler_l/r`, `rev_downsampler` | 716 | `.bss` | Polyphase coefficients + history |
| 10 | `grain_voices` | 352 | `.data` | 8 × `GrainVoice` |

Everything else is under 250 B.

**Region totals for the current bass build:**

| Region | Used | Size | % |
|---|---|---|---|
| SRAM (app image — `BOOT_SRAM`) | 146 556 B | 480 KB | **29.8 %** |
| DTCMRAM (`.bss`) | 47 584 B | 128 KB | 36.3 % |
| RAM_D2_DMA | 16 704 B | 32 KB | 51.0 % |
| SDRAM | 1 532 KB | 64 MB | 2.3 % |
| Internal FLASH | 0 | 128 KB | 0 % (bootloader only) |

`text 132 412 + data 14 136 + bss 1 619 636`.

---

## 4. Observations

Things the numbers surface. None of these are acted on here — they are
candidates, not decisions.

1. **`PROJECT.md`'s "FLASH at ~97%" line is stale.** It predates commit
   `f170770` ("Switch to Daisy bootloader (BOOT_SRAM)"). The app no longer
   targets the 128 KB internal flash — it lives in QSPI and runs from SRAM, at
   **29.8 % of 480 KB**. The QSPI-migration plan in `PROJECT.md` is already done.
   There is a lot more code headroom than the doc claims.

2. **Per-voice `powf`/`exp2f` inside the audio loop.** `ModeCSynth::Process`
   calls `powf(2, spread·detune/1200)` for all 7 voices *every sample*
   (`synth_osc_c.h:102`), and `DroneOsc` does the same with `exp2f`
   (`synth_osc_a.h:167`). The argument is block-constant — `detune` derives only
   from the knob. This is the largest single M7 cost in the inventory and it is
   recomputing a constant ~336 000 times per second.

3. **The unison voices run even when they are silent.** Both `DroneOsc` (saw)
   and `ModeCSynth` loop over all 7 `MoogOsc` voices unconditionally; the pair
   gains only scale the *output*. The comment in `synth_osc_c.h:91` says this is
   deliberate ("always advance all voices to preserve decorrelation") — worth
   knowing that K5-at-noon costs exactly as much as K5-full-CCW. The host
   benchmark confirms it: 35.5 ns at noon vs 37.2 ns at full CCW.

4. **The phaser's triangle LFO costs 6× what its S&H LFO costs.** The
   per-stage-coefficient path (`phaser.h:138-145`) calls `CoeffFor()` — a `tanf`
   — plus `FastExp2()` — an `expf` — once per stage per sample: 12 libm calls
   where the shared-coefficient path makes 2. This is the Uni-Vibe swirl feature;
   it is just expensive.

5. **The resamplers cost more than the reverb.** 22.6 ns vs 11.5 ns on the host.
   `Resampler::Tick` shifts a 16-element history array element-by-element
   (`resampler.h:70`) before each 16-tap MAC, and there are three instances.

6. **`PolyOctave` is exemplary about libm** — zero calls, `FastInvSqrt`
   throughout, with an explicit comment about why `inv³` would NaN on silence
   (`poly_octave.h:290-294`). It's expensive purely because it is 80 complex
   biquads, not because of anything avoidable.

7. **`polyoct_sub/up1/up2` are sized 256 floats but the block size is 48** —
   3 072 B allocated where 576 B is used. Harmless (SRAM is 70 % free), noted
   for completeness.

8. **`MoogLadder` v1 measures 2.5× `MoogLadderV2`** (39.7 vs 15.9 ns) — 8 Padé
   saturators per sample vs 1. Mode A is signed off and untouched by design;
   this is just the price of the Huovilainen topology.

9. **Mode C is where the budget goes.** Its two heaviest branches
   (`SW1=DOWN` synth, `SW2=DOWN` phaser) are the two most expensive blocks in the
   pedal and they *combine*. Every other mode sits at roughly a third of the
   audio budget; that one configuration is estimated at ~69 %.

---

## 5. Reproducing this

```bash
S=/tmp/nt3-analysis && mkdir -p $S
LD=lib/HothouseExamples/libDaisy

# 1. Compile the pedal target without touching build/
arm-none-eabi-g++ -c -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  -DUSE_HAL_DRIVER -DSTM32H750xx -DHSE_VALUE=16000000 -DCORE_CM7 -DSTM32H750IB \
  -DARM_MATH_CM7 -DUSE_FULL_LL_DRIVER -DBOOT_APP \
  -Ipedals/nitrotron3 -Isrc/core/blocks -Isrc/core/util -Isrc/core/io \
  -Ilib/HothouseExamples/src -I$LD -I$LD/src/ -I$LD/src/sys -I$LD/src/usbd \
  -I$LD/src/usbh -I$LD/Drivers/CMSIS_5/CMSIS/Core/Include/ \
  -I$LD/Drivers/CMSIS-DSP/Include -I$LD/Drivers/CMSIS-Device/ST/STM32H7xx/Include \
  -I$LD/Drivers/STM32H7xx_HAL_Driver/Inc/ -I$LD/core/ \
  -I lib/HothouseExamples/DaisySP/Source \
  -O2 -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti -std=gnu++14 \
  pedals/nitrotron3/main.cpp -o $S/main.o

# 2. Per-function code size
arm-none-eabi-objdump -h $S/main.o | grep '\.text\.'

# 3. Real link + region report (needs build/hothouse.o + startup .o present)
arm-none-eabi-g++ $S/main.o build/hothouse.o build/startup_stm32h750xx.o \
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  --specs=nano.specs --specs=nosys.specs -T$LD/core/STM32H750IB_sram.lds \
  -L$LD/build -L lib/HothouseExamples/DaisySP/build \
  -ldaisy -lc -lm -lnosys -ldaisysp \
  -Wl,--gc-sections -Wl,--print-memory-usage -o $S/nt3.elf
arm-none-eabi-size $S/nt3.elf
arm-none-eabi-nm --print-size --size-sort --radix=d $S/nt3.elf | tail -30
```

The host block benchmark used for the CPU column is a standalone harness that
includes each `src/core/blocks/*.h` directly and times 20 s of signal through it;
the blocks are header-only and depend on nothing but `constants.h`, so it
compiles natively with `clang++ -O2 -Ipedals/nitrotron3 -Isrc/core/blocks`.
