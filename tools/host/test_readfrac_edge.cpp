// test_readfrac_edge.cpp — regression for the RingBuffer::ReadFrac one-past-the-
// end read (the 2026-09-17 sprawl incident: a fractional read tap landing on the
// last sample interpolated against the word AFTER the slab, leaking whatever the
// linker put there into the audio path).
//
// Builds the REAL src/core/blocks/ring_buffer.h against a slab with a hostile
// neighbour and runs 10 minutes of audio with a slowly wandering fractional tap.
// PASS = 0 leaks. Run via tools/host/run.sh.
//
#include <cstdio>
#include <cmath>
#include "ring_buffer.h"
// The real RingBuffer, a 4800-sample slab, and a hostile value living in the
// memory immediately after it (on target: the reverb's companded uint16 store).
struct Layout { float slab[4800]; float sentinel = 1.0e20f; };
static Layout L;
int main() {
  RingBuffer wr; wr.Init(L.slab, 4800);
  L.sentinel = 1.0e20f;                         // Init memsets only the slab
  const float base = 144.f;                     // 3 ms @ 48k, as in sprawl/vestige
  float warble_int = 0.f; int leaks = 0; long n = 0;
  const long N = 48000L * 600;                  // 10 minutes of audio
  for (; n < N; n++) {
    wr.Write(0.1f * sinf(0.01f * n));           // ordinary bounded audio
    // slow fractional wander of the warble integrator, +-142 like the log shows
    warble_int = 120.f * sinf(0.00001f * n) + 0.37f * sinf(0.0031f * n);
    float y = wr.ReadFrac((float)wr.GetWritePos() - base - warble_int);
    if (fabsf(y) > 10.f) { if (leaks < 5) printf("  leak at t=%.2fs y=%g (pos=%g)\n", n/48000.f, y, (float)wr.GetWritePos() - base - warble_int); leaks++; }
  }
  printf("ReadFrac leaked the out-of-bounds neighbour %d times in %ld s of audio\n", leaks, N/48000);
  return leaks ? 1 : 0;
}
