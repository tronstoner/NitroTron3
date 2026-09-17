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

Limits: memory-**layout**-dependent faults are invisible here (on target the
slabs share SDRAM; on the host the linker decides what sits after them). The
hardware is stubbed, the input is synthetic, and the harness only checks what it
was told to check. A clean run is not proof — the serial log is the source of
truth (`.agents/skills/serial-diag/SKILL.md`).
