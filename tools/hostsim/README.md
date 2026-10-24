# hostsim — offline renderer + live host for NitroLab armitage

Two programs, one engine path:

- `armsim` — offline: WAV in, WAV out, controls and constants as arguments.
- `armlive` — real time: the same path through the Mac's default audio output,
  driven from a browser page (`ui.html`) over a localhost WebSocket. See
  [armlive](#armlive--live-version) below.

`armsim` runs the unmodified `pedals/nitrolab/modules/armitage.h` the way the
NitroLab shell does (`pedals/nitrolab/main.cpp`): 48 kHz, 48-frame blocks,
`PreProcess` → `Process` → K6 equal-power dry/wet mix, `Controls()` every
10 ms (every 10th block). Mono in, mono out. SW3 is fixed to UP (armitage).

## Build

    make -C tools/hostsim          # -> tools/hostsim/armsim + armlive (clang++ -O2)

## Render

    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --out out.wav
    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --k2 0.2 \
        --set ARM_PWM_HZ_MAX=20 --out pwm20.wav

| option | meaning |
|-|-|
| `--in FILE` | WAV, 48 kHz, PCM 16/24/32-bit or 32-bit float; channel 0 of multi-channel files |
| `--out FILE` | mono 24-bit WAV (the shell's output after the K6 mix) |
| `--out-f32 FILE` | also raw float32 (no quantisation, for diffs / analysis) |
| `--loop N` | play the input N times back to back |
| `--level P` | scale the input so its peak is P (0.18 = the DI excerpts at playing level) |
| `--k1 .. --k6 V` | knob positions 0..1, as the module sees them after `RemapKnob` (defaults K1 .5, K2 .5, K3 0, K4 .5, K5 .5, K6 1 = full wet) |
| `--sw1`, `--sw2` | `up` / `mid` / `down` (default mid) |
| `--fs1 1` | one FS1 tap at the start (armitage: wet mute) |
| `--set NAME=V` | override a constant from `armitage_constants.h` (repeatable; bools take true/false/0/1) |
| `--dump-constants` | print every constant: name, type, value, default expression, `fixed` / `set` |

Controls are static for the whole render and are applied before the first
block; the K6 smoother starts at its target (the knobs have been in place
since power-up). On hardware the first `Controls()` call comes ~10 ms after
audio starts and K6 ramps up from dry; that start-up transient isn't modelled.

## Constant overrides

`armitage_constants.h` declares each constant as `ARM_TUNABLE(type, NAME, value)`.
In the firmware this expands to the same `constexpr type NAME = value;` as
before, so the binary is bit-identical. `host_tunables.h` defines the macro
first, so on the host each constant is a runtime variable registered in
`hostsim::Registry()` (name, type, pointer, default expression). Defaults are
re-evaluated after `--set`, so derived ones follow: `--set ARM_ALIGN=0` also
switches `ARM_GAIN_UP`, `ARM_OUT_GAIN`, ... to their unaligned values unless
they are set explicitly.

`armitage.h` is compiled unmodified. `armitage_host.h` includes it with
`constexpr` read as `const` and `static_assert` dropped, because a few
expressions in it are constexpr over tunables. The dropped check
(`ARM_BETA_CLAMP + ARM_XMOD_MAX < 90`) runs at startup instead
(`ValidateTunables`).

Fixed on the host (`ARM_FIXED`, not settable): `ARM_SIN_SIZE` (table size /
mask) and `ARM_ADD_MMAX` (array sizes). `ARM_ADD_MID` must stay ≤ `ARM_ADD_MMAX`.

Constants are read wherever the module reads them, so those used in `Init()`
(filter laws, smoothing coefficients, crossfade lengths, band tables) only take
effect at the start of a render. Every `--set` is applied before `Init()`.

## armlive — live version

    make -C tools/hostsim
    tools/hostsim/armlive --in demos/armitage/wav/di_attacks_dry.wav --level 0.18
    tools/hostsim/armlive --input                       # bass DI through the interface
    tools/hostsim/armlive --in di.wav --level 0.18 --sw1 mid --k1 0.75 --set ARM_PWM_HZ_MAX=20

then open **http://localhost:8766/** (Ctrl-C in the terminal quits).

Same engine as `armsim` (the module, the K6 mix, FS1, `Controls()` every 10th
48-frame block, `RemapKnob` on the knob values), run from the miniaudio device
callback instead of a file loop. Output: the default output device, mono on
both channels. Input, switchable at runtime: the WAV looped (peak scaled to
`--level`, exactly as `armsim` scales it), or the default capture device
(first channel, `--gain` linear). The engine always runs at 48 kHz; if the
device runs at another rate miniaudio resamples at the device boundary.
The first run with an input device asks for microphone permission (macOS);
denied = silence on the input source.

| option | meaning |
|-|-|
| `--in FILE` | WAV, looped (48 kHz; channel 0) |
| `--level P` | WAV peak scaled to P (as `armsim --level`); changeable on the page |
| `--input` | start on the capture device instead of the WAV |
| `--gain G` | linear gain on the capture input (default 1); changeable on the page |
| `--k1..--k6`, `--sw1`, `--sw2`, `--fs1`, `--set` | start state, as in `armsim` |
| `--ws-port N` / `--http-port N` | default 8765 / 8766 (both bind 127.0.0.1 only) |
| `--ui FILE` | page to serve (default `ui.html` next to the executable) |
| `--null-audio` | miniaudio null backend: real-time callback, no sound (protocol tests) |
| `--selftest` | no device, no server: render `--seconds` (default 2) through the callback path with irregular callback sizes (512, 37, 1000, 48, 333, 1, 4096, 95), FIFO latency removed, to `--out-f32 FILE` |

Self-test against `armsim` (must be byte-identical):

    tools/hostsim/armsim  --in x.wav --loop 3 --level 0.18 --k1 0.25 --out /tmp/a.wav --out-f32 /tmp/a.f32
    tools/hostsim/armlive --in x.wav --level 0.18 --k1 0.25 --selftest --out-f32 /tmp/l.f32
    cmp /tmp/l.f32 <(head -c $((96000*4)) /tmp/a.f32) && echo identical

### The page

Knobs K1..K6 (vertical, 0..1 = the module's remapped value), SW1 / SW2, FS1
tap, source / level / input gain, output peak / rms and input peak meters, the
DIAG readout line, and under each knob what it does in the current model.
Below: the constants panel, every tunable of `armitage_constants.h` as a row:
log slider default/10 .. default×10 for positive floats, ±10×|default| for
negative ones, ±1 for a zero default, a checkbox for bools, a number box for
everything (ints: number box only); `reset` per row; "show changed only";
**Export** writes the changed tunables as `NAME = value;` lines (plus the
equivalent `--set` arguments) into a text box to copy.

- A constant change is applied on slider release / Enter, on the audio thread
  at the next control tick, followed by `Init()` + `Activate()` (as at power-up),
  because many constants are only read in `Init()`. That restarts the engine
  state: a short audible reset per change. Derived defaults follow (as `--set`).
  A change that breaks a runtime check (`ValidateTunables`) is rolled back and
  reported on the page.
- The mode name and the DIAG line come from the module's own DIAG snapshot
  (`armlive` is compiled with the DIAG gate on; the gate does not touch the
  audio path — the self-test proves it). The module writes a snapshot once the
  controls have been still for `ARM_DIAG_SETTLE_MS` (600 ms), or on an FS1 tap,
  so the mode / knob meanings follow a change after ~0.6 s. (`ARM_DIAG_SETTLE_MS`
  is itself a tunable.)
- The knob meaning texts are copied by hand from the CONTROL MAP in
  `armitage.h` into `ui.html` (`MEANING`); update them when the layout changes.
- Number display goes through one helper (`fmt`, locale in `CFG.locale`).

### Protocol (WebSocket, JSON, one object per text frame / line)

Client -> server (keys can be combined in one object):

| message | effect |
|-|-|
| `{"k":[k1,..,k6]}` | knob positions 0..1 (shorter arrays / `null` entries leave the rest) |
| `{"sw1":"up\|mid\|down"}`, `{"sw2":...}` | switches |
| `{"fs1":1}` | one FS1 tap (rising edge on the next control tick) |
| `{"src":"wav"\|"input"}` | input source |
| `{"level":P}` / `{"gain":G}` | WAV peak level / capture input gain |
| `{"set":{"NAME":value,...}}` | set tunables (number, or true/false for bools) |
| `{"reset":"NAME"}` or `{"reset":["A","B"]}` | back to the default expression |

Server -> client:

- on connect: `{"state":{k, sw1, sw2, src, level, gain, wav, input_ok}}` and
  `{"tunables":[{name, type, value, default, expr, fixed, set}, ...]}`; the
  tunables list again after every set / reset has been applied.
- every 100 ms: `{"t", "cpu", "peak", "rms", "in_peak", "reinits", "src", ...}`
  plus, once the module wrote a DIAG snapshot, its fields: `"n"` (tap number,
  0 = auto), `"mode"` (SAW / PULSE / ADD / FB / UNSHAPED), `"h"`, `"k"` (K1..K5),
  `"sw1"`, `"sw2"`, `"beta"`, `"filt_t"`, `"fm"`, `"env_t2"`, `"env_e"` (K5 filter
  envelope), `"envf_t2"`, `"envf_e"` (K2 FM envelope), `"oct"`, `"mute"`,
  `"diag_age_ms"`. `cpu` = host callback time / real time (0..1), `peak` /
  `rms` / `in_peak` = linear, over the last 100 ms.
- `{"error":"..."}` for a rejected message or a rolled-back constant.

### Threads

The audio callback runs the engine: no allocation, no locks. Knobs, switches,
source and gains are atomics; FS1 taps and constant set / reset go through a
single-producer ring to the audio thread, which applies them at the control
tick (the tunables are written only there) and acknowledges; meters, DIAG
snapshots and acks come back through rings. The server reads the constants
only when every op it sent has been acknowledged.

### Caveats

- Not the Daisy: no ADC jitter or knob noise (knob values are exact), no
  hysteresis, no Daisy CPU figure — `cpu` is this Mac's load, meaningless for
  the 480 MHz M7 budget (see `docs/DSP_INVENTORY.md` for that).
- Block size 48 like the firmware, but the device callback size differs; the
  48-frame FIFO adds one block (1 ms) of latency on top of the device's.
- `Controls()` runs on the audio thread every 10th block (deterministic, same
  as `armsim`), not on a main loop with timing jitter.
- Constant changes restart the engine state (see above).

### Dependencies

- `third_party/miniaudio.h` — miniaudio 0.11.25 by David Reid, single header,
  public domain (Unlicense) or MIT-0, your choice: `third_party/LICENSE.miniaudio`.
  Only the low-level device API is compiled (`miniaudio_impl.c`).
- WebSocket / HTTP server (`ws_server.h`, incl. SHA-1 + base64) and the JSON
  reader (`json_mini.h`): own code, POSIX sockets only.
