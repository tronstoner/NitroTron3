# Host-side tests

Host binaries, built and run by `tools/host/run.sh [seconds]` from the repo root.

- **`test_readfrac_edge.cpp`** — regression for the `RingBuffer::ReadFrac`
  one-past-the-end read (2026-09-17 sprawl incident). Real `ring_buffer.h`, a
  slab with a hostile neighbour, 10 minutes of audio, wandering fractional tap.
  PASS = 0 leaks.
- **`test_grid_quantize.cpp`** — the loop-length grid quantiser
  (`src/core/blocks/grid_quantize.h`, vestige rework §4.3/§4.5). Pure integer
  maths, no module or stubs. Asserts nearest-boundary rounding in both
  directions (including truncation just past a boundary), the floor at the
  smallest division, the clamp at T, and that every result is the exact integer
  boundary over a sweep of awkward T. Verified to fail on a round-up-only, a
  round-down-only and a float-boundary implementation.
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

- **`test_vestige.cpp`** — the real vestige module (onward rework), stubbed
  hardware, pluck / steady-tone input, Controls() every 10 ms and Process()
  every 48 samples. Stage 0: FS2's four transport states, K2 length +
  direction, SW1 1-voice / 6-voice / freeze, K6 not owned. Buffers: the loop
  and freeze slabs and every slot row are disjoint memory, each RingBuffer is
  exactly its row's length, neither side ever changes a byte of the other's
  slab (hashed), a freeze capture run into the 400 ms ceiling stays inside its
  scratch row, an in-flight capture is dropped (not committed across) on an
  SW1 side switch, and a switch carries no content across — unheld the side
  left is cleared, held it is kept byte-identical, unscheduled while away, and
  fades back in on return, its head having kept running while away. Stage 1
  (T): K2 spans exactly T_MIN..T_MAX; tap intervals (incl. both range edges and
  out-of-range) give the right T, last interval of a chain wins, a long press
  is not a tap; a tapped T is the loop ceiling; K2 jitter below epsilon keeps
  the tap, a real move cancels it, direction stays on K2; LED1 flashes on
  capture-start + k*T, re-anchors on each capture start, never on the old beat,
  a tap mid-loop changes only the period, and LED1 is dark while off. Footswitch
  events mimic the real surface (held_ms = 0 on release). Stage 2 (quantised
  capture), measured on noise bursts: the capture start equals the sample an
  independently replicated gate envelope opens; the loop body is bit-identical
  to input[A..A+Q); Q is Quantize(raw, T latched at the start) and an exact
  division, rounding up, down (truncated: the guard behind Q is the head, not
  the cut material), floor and ceiling; playback enters at phase (t-A) mod Q,
  on its "one" at exactly A+Q when the end is known in time (incl. a reverse
  ceiling loop); the output is cross-correlated against the expected loop
  samples at entry and at the next "one" (lag = the wet path's measured fixed
  latency, 0 error); K2 moved mid-capture leaves its T; six MIDDLE voices keep
  six anchors and every head sits on (now - anchor) mod Q; the heard output
  has 0 samples latency vs dry (forward, reverse, freeze). Stage 6 (K1 speed
  crossfade): loop period Q at noon, 2Q full CCW, Q/2 full CW (autocorrelation),
  content on the grid anchored at the playback start (lag 0, independent of
  module state), forward and reverse at both speeds; the crossfade midpoints
  split into both versions by the equal-power law (least squares, residual
  ~0); half-speed grains read exactly on the half-speed head after restarts at
  arbitrary samples; the CCW->noon->CW sweep on a sine loop has no step above
  the steady double-speed maximum and swaps half<->double once, at exactly 0;
  a per-sample guard-read audit (2x / 0.5x, forward / reverse, short T ceiling,
  1/8 late join, 1 s ceiling) finds no read of an unwritten guard cell and none
  past L + min(L, guard); six voices never exceed the grain cap and do not
  double at the K1 end. White-box (`#define private public`).

Limits: memory-**layout**-dependent faults are invisible here (on target the
slabs share SDRAM; on the host the linker decides what sits after them). The
hardware is stubbed, the input is synthetic, and the harness only checks what it
was told to check. A clean run is not proof — the serial log is the source of
truth (`.agents/skills/serial-diag/SKILL.md`).
