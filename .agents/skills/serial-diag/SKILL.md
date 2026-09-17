---
name: serial-diag
description: Capture and read a timestamped USB-serial DIAG log of the ChronoTron3 sprawl module, to chase intermittent audio faults.
disable-model-invocation: false
allowed-tools: Bash(make *), Bash(tools/diag/*), Bash(python3 tools/diag/*), Bash(tools/host/run.sh*), Read
---

# Serial diagnostics (ChronoTron3 / sprawl)

Capture a timestamped USB-serial log of the sprawl **heartbeat** (a full state
snapshot every 2 s) and of **interrupt-time fault snapshots** (the picture taken
in the audio ISR at the first non-finite sample). This is for intermittent,
hard-to-reproduce audio faults — runaway levels, wet silence, hangs — where the
only honest evidence is what the pedal itself reports.

**Observation only.** The DIAG build never changes behaviour after a fault: no
auto-recovery, no reset, no muting. Auto-recovery was tried and made things
worse (it hid the cause and produced new artefacts). The DIAG build just looks.

## Steps

1. **Build** the DIAG firmware:
   ```bash
   make PEDAL=chronotron3 DIAG=1
   ```
   `DIAG=1` defines `CT3_DIAG_BUILD`, which flips the constexpr `CT3_DIAG` in
   `pedals/chronotron3/constants.h` to `true`; in a normal build it is
   `constexpr false` and the whole emitter is dead-code eliminated. The build
   stamp includes DIAG, so switching DIAG on or off rebuilds automatically.
   **The user flashes** — do not attempt it.

2. **Capture**:
   ```bash
   tools/diag/capture.sh            # or: tools/diag/capture.sh /dev/cu.usbmodem1234
   ```
   It uses `/dev/cu.usbmodem*`, never `/dev/tty.usbmodem*`: on macOS the `tty.*`
   node is the dial-in device and **blocks on carrier-detect (DCD)**, which the
   Daisy's CDC-ACM port never asserts — `cat` on it hangs forever and reads
   nothing. `cu.*` (call-out) opens immediately.

   The log lands in the current directory as `sprawl-YYYYmmdd-HHMM.log`, each
   line prefixed with a wall-clock `HH:MM:SS`. `Ctrl-C` stops it. If the pedal
   is power-cycled or re-flashed the device node disappears and the script
   exits — just re-run it.

3. **Confirm it is live.** The first block prints:
   ```
   SP DIAG online (DIAG=1 build)
   SP HB t=<ms> n=<faultcount>
   ```
   then `SP HB ...` every 2 s. The heartbeat only runs while **SW3 = sprawl
   (DOWN)** — no heartbeat means the wrong mode, not a dead log.

4. **Analyse**:
   ```bash
   tools/diag/sprawl_log_table.py sprawl-20260917-1605.log
   ```
   One row per snapshot plus a summary. Then go back and read the **raw block**
   around anything the table flags — the table is a finding aid, not the
   evidence.

5. **What to look for** (all from the 2026-09-17 ReadFrac incident):
   - `met in= grn= tex= wet=` are peak followers along the chain. Comparing
     `met` peaks against `grn` (the grain sum) tells you **which stage** a burst
     entered at — if `grn` is sane and `wet` is huge, it happened after the
     grain sum.
   - `deg nz=` is the colour engine's raw **INPUT** peak follower — it sees what
     arrives, not what the colour engine produced.
   - `fbk duck=` above `0.200` kills feedback. Its release is 800 ms, so a big
     duck value takes on the order of **40 s** to decay back — a fault that is
     long over still shows as "no feedback" for most a minute.
   - Values printed as `nan`, `inf` or `...e6` are the tell. The emitter spells
     them out deliberately (newlib-nano has no `%f`, so floats are fixed-point
     formatted by hand); `411085e6` in a `duck=` field is the whole bug report.
   - `SP FAULT @<stage>` is the interrupt-time picture at the **first**
     non-finite sample (`@grain-sum` / `@wet-bus` / `@post-reverb`).
   - `SP HB *** WET SILENT ***` means wet has been silent for ≥ 4 s while input
     is present and feedback is engaged — a real event class, and *not* a
     non-finite fault.

## Field key

Emitted by the `// Sprawl diagnostics` block in `pedals/chronotron3/main.cpp`
(19 lines per snapshot, one line per 10 ms control tick — the logger's line
buffer is 128 bytes and USB TX is non-blocking, so a burst would be dropped).
Read that block if a field is missing here; it is the source of truth.

| line | fields |
|-|-|
| `SP` | header: `HB` \| `HB *** WET SILENT ***` \| `FAULT @grain-sum` \| `FAULT @wet-bus` \| `FAULT @post-reverb`; `t=` ms since boot, `n=` cumulative fault count |
| `ctl` | `k=` K1 K2 K3 K4 K5 (five values, raw knob positions) · `sw=` SW1SW2 (digits) · `tap=` tap-tempo interval |
| `st` | `frz=` frozen · `byp=` bypassed · `pan=` panic ramp · `snd=` send level |
| `prm` | `fb=` feedback amount · `rv=` reverb amount · `k2s=` K2 scale · `k3m=` K3 magnitude · `gl=` glitch · `cl=` cloud idx · `lv=` live idx · `tex=` texture idx · `hm=` harmony idx |
| `rng` | `maxr=` max delay range · `base=` base delay · `glen=` grain length · `bint=` base grain interval · `wpos=` ring write position (all in samples) |
| `grn` | `act=` active voices · `tmr=` grain timer · `nxt=` next voice slot · `hold=` hold counter · `ratio=` cached pitch ratio · `bad=` bad-voice bitmask (hex, bit per voice) |
| `v0`..`v7` | two voices per line: `v<n> a<active> d<delay> l<len> r<reverse> n<loops> p<rate> o=<last output>` |
| `fbk` | `duck=` feedback duck envelope (> 0.200 kills feedback; 800 ms release) · `onp=` on-play envelope · `hp=` feedback high-pass state `z0,z1` |
| `env` | `pw=` previous wet sample · `env=` grain envelope · `slow=` slow transient follower |
| `deg` (1st) | `ch=` active/target colour chain · `mix=` colour mix · `d=` depth/target depth · `warb=` warble integrator (the fractional read offset) · `ng=` noise gate · `sg=` noise soft-gate |
| `deg` (2nd) | `nz=` raw **input** peak follower · `env=` colour envelope · `fclk=` BBD clock frequency · `fold=` fold blend/fold drive |
| `bbd` (1st) | `hl=` S&H hold length · `hold=` held sample · `inz=` input LP state `z1,z2` · `recz=` reconstruction LP state `z1,z2` |
| `bbd` (2nd) | `lossz=` loss filter state · `dc=` DC blocker `x1,y1` |
| `tap` (1st) | `sx1=` saturator state · `lpz=` tape LP state · `hp=` tape HP `x1,y1` · `hb=` head-bump `z1,z2` |
| `tap` (2nd) | `drop=` dropout gain · `snag=` snag (cents) |
| `met` | `in=` input peak · `grn=` grain-sum peak · `tex=` texture peak · `wet=` wet-bus peak · `dec=` decimator hold · `rml=` ringmod LP |

## Rules

- **The log is the source of truth.** Read it before proposing a cause. Do not
  theorise from the code about what "must" be happening when a capture exists.
- The host harness (`tools/host/run.sh`, see `tools/host/README.md`) is a
  complement, not a substitute: it **cannot see memory-layout-dependent faults**
  (on target the slabs share SDRAM; on the host the linker decides what sits
  after them), and it only checks what you told it to check.
- Check **MAGNITUDE, not just finiteness.** The module's own guard catches
  `nan`/`inf`; the ReadFrac bug produced a huge *finite* value that walked
  straight through it. Any new check must test the magnitude too.
