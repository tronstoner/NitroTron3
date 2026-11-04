# hostsim — offline renderer + live host for the NitroLab modules

The three NitroLab modules, selected like SW3 on the pedal:

| SW3 | module | `--sw3` / `--module` |
|-|-|-|
| UP | armitage (`modules/armitage.h`) | `up` / `armitage` (default) |
| MIDDLE | explorer (`modules/explorer.h`, `Explorer(false)`) | `mid` / `explorer` |
| DOWN | FM LAB (`modules/explorer.h`, `Explorer(true)`) | `down` / `lab` |

Two programs, one engine path:

- `armsim` — offline: WAV in, WAV out, controls and constants as arguments.
- `armlive` — real time: the same path through the Mac's default audio output,
  driven from a browser page (`ui.html`) over a localhost WebSocket. See
  [armlive](#armlive--live-version) below.

Both run the unmodified modules the way the NitroLab shell does
(`pedals/nitrolab/main.cpp`), through one shared copy of the shell
(`nl_shell.h`): 48 kHz, 48-frame blocks, `PreProcess` → `Process` → K6
equal-power dry/wet mix, `Controls()` every 10 ms (every 10th block), FS1 tap.
Mono in, mono out. As on the pedal, all three modules are `Init()`ed at start
(the explorer tables are built once and shared by both instances), only the
SW3 one is `Activate()`d and runs; an SW3 change at a control tick =
`Deactivate()` old, `Activate()` new, then the new module's `Controls()`.
Inactive modules keep their state (they don't run).

Only armitage's constants are runtime tunables (`--set`, the constants panel).
Explorer's (`explorer_constants.h`) stay `constexpr`: no overrides.
`explorer.h` compiles as is on the host; its `EXP_FAST_BSS` (D2 SRAM),
`DSY_SDRAM_BSS` and `DTCM_MEM_SECTION` expand to nothing there (plain `.bss`).

## Build

    make -C tools/hostsim          # -> tools/hostsim/armsim + armlive (clang++ -O2)

## Render

    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --out out.wav
    tools/hostsim/armsim --in di2.wav --level 0.18 --sw1 mid --k1 0.75 --k2 0.2 \
        --set ARM_PWM_HZ_MAX=20 --out pwm20.wav
    tools/hostsim/armsim --in di2.wav --level 0.18 --module explorer --sw1 mid --k1 0.25 --k3 0.6 --out wt.wav
    tools/hostsim/armsim --in di2.wav --level 0.18 --module lab --sw1 down --k1 0.7 --k2 0.6 --k5 0.2 --out uni.wav

| option | meaning |
|-|-|
| `--in FILE` | WAV, 48 kHz, PCM 16/24/32-bit or 32-bit float; channel 0 of multi-channel files |
| `--out FILE` | mono 24-bit WAV (the shell's output after the K6 mix) |
| `--out-f32 FILE` | also raw float32 (no quantisation, for diffs / analysis) |
| `--loop N` | play the input N times back to back |
| `--level P` | scale the input so its peak is P (0.18 = the DI excerpts at playing level) |
| `--k1 .. --k6 V` | knob positions 0..1, as the module sees them after `RemapKnob` (defaults K1 .5, K2 .5, K3 0, K4 .5, K5 .5, K6 1 = full wet) |
| `--sw1`, `--sw2` | `up` / `mid` / `down` (default mid) |
| `--sw3`, `--module` | `up` / `mid` / `down` = `armitage` / `explorer` / `lab` (default armitage) |
| `--fs1 1` | one FS1 tap at the start (all three modules: wet mute) |
| `--set NAME=V` | override a constant from `armitage_constants.h` (repeatable; bools take true/false/0/1; armitage only) |
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
    tools/hostsim/armlive --in demos/armitage/wav/di_fast_dry.wav --level 0.18 --module explorer   # SW3 MID
    tools/hostsim/armlive --in demos/armitage/wav/di_fast_dry.wav --level 0.18 --module lab        # SW3 DOWN, FM LAB
    tools/hostsim/armlive --input --module lab --sw1 mid                                          # bass DI into FM LAB SPECTRAL

then open **http://localhost:8766/** (Ctrl-C in the terminal quits). The start
module only sets SW3's start position: the page switches modules live (SW3).
With `--ws-port N` open the page as `http://localhost:<http-port>/?ws=N`.

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
| `--k1..--k6`, `--sw1`, `--sw2`, `--sw3` / `--module`, `--fs1`, `--set` | start state, as in `armsim` |
| `--ws-port N` / `--http-port N` | default 8765 / 8766 (both bind 127.0.0.1 only) |
| `--ui FILE` | page to serve (default `ui.html` next to the executable) |
| `--null-audio` | miniaudio null backend: real-time callback, no sound (protocol tests) |
| `--selftest` | no device, no server: render `--seconds` (default 2) through the callback path with irregular callback sizes (512, 37, 1000, 48, 333, 1, 4096, 95), FIFO latency removed, to `--out-f32 FILE` |

Self-test against `armsim` (must be byte-identical):

    tools/hostsim/armsim  --in x.wav --loop 3 --level 0.18 --k1 0.25 --out /tmp/a.wav --out-f32 /tmp/a.f32
    tools/hostsim/armlive --in x.wav --level 0.18 --k1 0.25 --selftest --out-f32 /tmp/l.f32
    cmp /tmp/l.f32 <(head -c $((96000*4)) /tmp/a.f32) && echo identical

### The page

Knobs K1..K6 (vertical, 0..1 = the module's remapped value), SW3 (module),
SW1 / SW2, FS1 tap (wet mute on all three modules), source / level / input
gain, output peak / rms and input peak meters, the DIAG readout, and under
each knob what it does in the current module / SW1 family / model. The title,
the SW1 labels and the knob roles follow SW3.

- **Armitage**: DIAG line (mode, h, beta, filter t, FM, envelopes, mute), the
  knob meaning of the current mode.
- **Explorer / FM LAB**: the zone / model name, the firmware's DIAG second-half
  line (same branches and names as `main.cpp`'s `XP` print, `%.2f` numbers),
  and under each knob (green) the value it sets now: K1 zone, K2 parameter
  (at the knob → with the K5 LFO), K3 (fc / tilt / index or harmonics), K4
  pluck ↔ swell state (g, decay / rise, T, applied fc), K5 LFO shape / depth /
  rate, SPECTRAL e / shift s / partials N or UNISON beat. These are the values of the
  module's last DIAG snapshot, not a live meter: the envelope g is the value at
  the snapshot moment.

Below: the Armitage constants panel (dimmed while another module is active;
a change there re-inits armitage only), every tunable of `armitage_constants.h` as a row:
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
- The mode / zone name and the DIAG line come from the module's own DIAG snapshot
  (`armlive` is compiled with the DIAG gate on; the gate does not touch the
  audio path — the self-test proves it). The module writes a snapshot once the
  controls have been still for `ARM_DIAG_SETTLE_MS` (600 ms), or on an FS1 tap,
  so the mode / knob meanings follow a change after ~0.6 s. (`ARM_DIAG_SETTLE_MS`
  is itself a tunable.)
- The knob meaning texts are copied by hand from the CONTROL MAPs in
  `armitage.h` (`MEANING`) and `explorer.h` (`XMEAN`, `MODS`) into `ui.html`;
  update them when the layout changes. The explorer DIAG line format is copied
  from `main.cpp` into `armlive.cpp` (`ExplorerDiagLine`).
- Number display goes through one helper (`fmt`, locale in `CFG.locale`).

### Protocol (WebSocket, JSON, one object per text frame / line)

Client -> server (keys can be combined in one object):

| message | effect |
|-|-|
| `{"k":[k1,..,k6]}` | knob positions 0..1 (shorter arrays / `null` entries leave the rest) |
| `{"sw1":"up\|mid\|down"}`, `{"sw2":...}`, `{"sw3":...}` | switches (SW3 = module) |
| `{"module":"armitage\|explorer\|lab"}` | same as `sw3` |
| `{"fs1":1}` | one FS1 tap (rising edge on the next control tick) |
| `{"src":"wav"\|"input"}` | input source |
| `{"level":P}` / `{"gain":G}` | WAV peak level / capture input gain |
| `{"set":{"NAME":value,...}}` | set tunables (number, or true/false for bools) |
| `{"reset":"NAME"}` or `{"reset":["A","B"]}` | back to the default expression |

Server -> client:

- on connect: `{"state":{k, sw1, sw2, sw3, module, src, level, gain, wav, input_ok}}` and
  `{"tunables":[{name, type, value, default, expr, fixed, set}, ...]}`; the
  tunables list again after every set / reset has been applied.
- every 100 ms: `{"t", "cpu", "peak", "rms", "in_peak", "reinits", "src", "active", ...}`
  (`active` = the module the engine runs: armitage / explorer / lab) plus, once
  the active module wrote a DIAG snapshot, `"diag_mod"` (its module),
  `"diag_age_ms"` and its fields. Armitage: `"n"` (tap number,
  0 = auto), `"mode"` (SAW / PULSE / ADD / FB / UNSHAPED), `"h"`, `"k"` (K1..K5),
  `"sw1"`, `"sw2"`, `"beta"`, `"filt_t"`, `"fm"`, `"env_t2"`, `"env_e"` (K5 filter
  envelope), `"envf_t2"`, `"envf_e"` (K2 FM envelope), `"oct"`, `"mute"`.
  Explorer / FM LAB: `"n"`, `"k"`, `"sw1"`, `"sw2"`, `"model"`,
  `"zone"` (zone / model name), `"line"` (the firmware's DIAG second half),
  `"labz"` / `"specz"` / `"addz"` (FM LAB family zone, -1 = not that family),
  `"k2n"` / `"k3n"` (ADDITIVE parameter names), and the raw `DiagSnap` values
  `"a"`, `"b"`, `"lb"`, `"lue"`, `"llfd"`, `"lside"` (FM LAB 2-OP), `"c3"`, `"bm"`, `"fc"`, `"lfe"`, `"dfc"`, `"sge"`, `"sv"`, `"sm"`,
  `"env"`, `"dir"`, `"T"`, `"fce"`, `"lfo"`, `"lfod"`, `"lfohz"`, `"loop"`,
  `"mute"` (meanings: `Explorer::DiagSnap` in `explorer.h`). A snapshot of a
  module that is no longer active is dropped.
  `cpu` = host callback time / real time (0..1), `peak` / `rms` / `in_peak` = linear, over the last 100 ms.
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
- Constant changes restart armitage's state (see above).
- Explorer / FM LAB: no constant overrides (constexpr); the knob value
  readouts update only with a DIAG snapshot (settled controls or FS1).
- SW3 switching is instant (main.cpp has no crossfade either): the old
  module's last block and the new module's first are spliced as on the pedal.

### Dependencies

- `third_party/miniaudio.h` — miniaudio 0.11.25 by David Reid, single header,
  public domain (Unlicense) or MIT-0, your choice: `third_party/LICENSE.miniaudio`.
  Only the low-level device API is compiled (`miniaudio_impl.c`).
- WebSocket / HTTP server (`ws_server.h`, incl. SHA-1 + base64) and the JSON
  reader (`json_mini.h`): own code, POSIX sockets only.
