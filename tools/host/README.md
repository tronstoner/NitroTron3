# Host-side tests

Two host binaries, built and run by `tools/host/run.sh [seconds]` from the repo root.

- **`test_readfrac_edge.cpp`** — regression for the `RingBuffer::ReadFrac`
  one-past-the-end read (2026-09-17 sprawl incident). Real `ring_buffer.h`, a
  slab with a hostile neighbour, 10 minutes of audio, wandering fractional tap.
  PASS = 0 leaks.
- **`sprawl_harness.cpp`** — runs the *real* Sprawl module on the host against
  the stubs in `stub/`, driving `Controls()` every 10 ms and `Process()` every
  48 samples like the shell does, over a synthetic bass-pluck input with K4/K2
  sweeps. Checks every wet sample for non-finite **and** magnitude > 10.

- **`test_pitch_grains.cpp`** — transposed-grain scheduling across the K2 travel,
  on the real Sprawl module. SW2 UP, K1 full CW (+12), K3 at the noon pad, a
  steady sine. Asserts the tuned K2-noon shifter stays at 150 ms grains / 75 ms
  hop, that neither grows as K2 opens (grain growth = the pitched slapback), and
  that the wet is never gated (a capped grain emitted at the uncapped hop =
  tremolo). Verified to fail on both regressions.

Limits: memory-**layout**-dependent faults are invisible here (on target the
slabs share SDRAM; on the host the linker decides what sits after them). The
hardware is stubbed, the input is synthetic, and the harness only checks what it
was told to check. A clean run is not proof — the serial log is the source of
truth (`.agents/skills/serial-diag/SKILL.md`).
