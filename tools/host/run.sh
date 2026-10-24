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
echo "== test_grid_quantize (loop-length grid quantiser)"
if g++ -O2 -std=c++17 -I src/core/blocks -o /tmp/ct3_gridq tools/host/test_grid_quantize.cpp \
   && /tmp/ct3_gridq; then
  echo "PASS  test_grid_quantize"
else
  echo "FAIL  test_grid_quantize"; fail=1
fi

echo
echo "== test_dynquapoteg (time grid block: K2 taper, free-run, loop floor, tap ranges)"
if g++ -O2 -std=c++17 -I src/core/blocks -I pedals/chronotron3/modules -o /tmp/ct3_dynq tools/host/test_dynquapoteg.cpp \
   && /tmp/ct3_dynq; then
  echo "PASS  test_dynquapoteg"
else
  echo "FAIL  test_dynquapoteg"; fail=1
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
echo "== test_pitch_grains (transposed grain length / hop across the K2 travel)"
if g++ -O2 -std=c++17 -DCT3_DIAG_BUILD \
     -I tools/host/stub -I pedals/chronotron3 -I pedals/chronotron3/modules \
     -I src/core/blocks -I src/core/util \
     -o /tmp/ct3_pitch tools/host/test_pitch_grains.cpp \
   && /tmp/ct3_pitch 6; then
  echo "PASS  test_pitch_grains"
else
  echo "FAIL  test_pitch_grains"; fail=1
fi

echo
echo "== test_vestige (real vestige module: transport, K2, SW1, loop/freeze buffer separation)"
if g++ -O2 -std=c++17 -DCT3_DIAG_BUILD \
     -I tools/host/stub -I pedals/chronotron3 -I pedals/chronotron3/modules \
     -I src/core/blocks -I src/core/util \
     -o /tmp/ct3_vestige tools/host/test_vestige.cpp \
   && /tmp/ct3_vestige; then
  echo "PASS  test_vestige"
else
  echo "FAIL  test_vestige"; fail=1
fi

echo
echo "== test_mnemonic (real mnemonic module: K3 pre-effect, K4 degrade, read-side K1/K4, PLL switch)"
if g++ -O2 -std=c++17 -DCT3_DIAG_BUILD \
     -I tools/host/stub -I pedals/chronotron3 -I pedals/chronotron3/modules \
     -I src/core/blocks -I src/core/util \
     -o /tmp/ct3_mnemonic tools/host/test_mnemonic.cpp \
   && /tmp/ct3_mnemonic; then
  echo "PASS  test_mnemonic"
else
  echo "FAIL  test_mnemonic"; fail=1
fi

echo
echo "== nt3m_ab (NitroTron3m carve vs shipping NitroTron3: bit-exact A/B, bass + guitar; armitage slot smoke)"
NT3M_S="-O2 -std=c++17 -I tools/host/stub_nt3m -I src/core/blocks -I src/core/util"
if g++ $NT3M_S -o /tmp/nt3m_ab_cmp tools/host/nt3m_ab.cpp; then
  for inst in bass guitar; do
    NT3M_D=""; [ "$inst" = guitar ] && NT3M_D="-DNT3_INSTRUMENT_GUITAR"
    if g++ $NT3M_S $NT3M_D -DAB_SHIP -I pedals/nitrotron3  -o /tmp/nt3m_ab_ship tools/host/nt3m_ab.cpp \
       && g++ $NT3M_S $NT3M_D -DAB_M  -I pedals/nitrotron3m -o /tmp/nt3m_ab_m    tools/host/nt3m_ab.cpp \
       && /tmp/nt3m_ab_ship /tmp/nt3m_ab_ship.f32 && /tmp/nt3m_ab_m /tmp/nt3m_ab_m.f32 \
       && /tmp/nt3m_ab_cmp /tmp/nt3m_ab_ship.f32 /tmp/nt3m_ab_m.f32; then
      echo "PASS  nt3m_ab ($inst)"
    else
      echo "FAIL  nt3m_ab ($inst)"; fail=1
    fi
  done
  if g++ $NT3M_S -DAB_M -DNT3M_SLOT_B_ARMITAGE -I pedals/nitrotron3m -o /tmp/nt3m_ab_arm tools/host/nt3m_ab.cpp \
     && /tmp/nt3m_ab_arm /tmp/nt3m_ab_arm.f32; then
    echo "PASS  nt3m_ab (armitage slot smoke: finite, |x| <= 10, L == R)"
  else
    echo "FAIL  nt3m_ab (armitage slot smoke)"; fail=1
  fi
else
  echo "FAIL  nt3m_ab (compare tool build)"; fail=1
fi

echo
[ "$fail" -eq 0 ] && echo "ALL PASS" || echo "FAILURES"
exit "$fail"
