#!/usr/bin/env bash
# Build and run the ChronoTron3 host-side tests. Run from the repo root:
#   tools/host/run.sh [seconds]      (default 30 s of audio per scenario)
# See tools/host/README.md for what these cover -- and what they cannot see.
set -uo pipefail
SECS="${1:-30}"
fail=0

echo "== test_readfrac_edge (RingBuffer::ReadFrac one-past-the-end)"
if g++ -O2 -std=c++17 -I src/core/blocks -o /tmp/ct3_readfrac tools/host/test_readfrac_edge.cpp \
   && /tmp/ct3_readfrac; then
  echo "PASS  test_readfrac_edge"
else
  echo "FAIL  test_readfrac_edge"; fail=1
fi

echo
echo "== sprawl_harness (real Sprawl module, stubbed hardware, ${SECS}s per scenario)"
if g++ -O2 -std=c++17 -DCT3_DIAG_BUILD \
     -I tools/host/stub -I pedals/chronotron3 -I pedals/chronotron3/modules \
     -I src/core/blocks -I src/core/util \
     -o /tmp/ct3_sprawl tools/host/sprawl_harness.cpp \
   && /tmp/ct3_sprawl "$SECS"; then
  echo "PASS  sprawl_harness"
else
  echo "FAIL  sprawl_harness"; fail=1
fi

echo
[ "$fail" -eq 0 ] && echo "ALL PASS" || echo "FAILURES"
exit "$fail"
