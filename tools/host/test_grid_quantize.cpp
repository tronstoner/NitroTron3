// test_grid_quantize.cpp — the loop-length grid quantiser (GridQuantize).
//
// Stage 2 of the vestige rework snaps a captured loop to a division of the
// master period T. This pins down the behaviour the plan (§4.3 / §4.5) actually
// asks for, in particular the parts that are easy to "fix" into something else:
//
//   1. REACHABILITY   every division in the set can come out of Quantize.
//   2. BOTH WAYS      rounding goes down as well as up, and this asserts which
//                     for constructed cases on each side of a midpoint.
//   3. TRUNCATION     a capture that runs just past a boundary comes back AT
//                     that boundary. The overhang is cut. That is the point of
//                     quantising and not a bug.
//   4. FLOOR          a capture shorter than half the smallest division rounds
//                     up to the smallest division, never to zero.
//   5. CLAMP          T is the maximum; at or beyond T the answer is T.
//   6. EXACTNESS      over a wide sweep of T — including T not divisible by 3,
//                     6 or 8 — every result is exactly Boundary(i, T) for some
//                     i. No float drift, and Quantize is monotone in raw_len.
//   7. ROUND-TRIP     DivisionOf() names the fraction that was landed on.
//
// Build/run via tools/host/run.sh.
//
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include "grid_quantize.h"

static int g_fail = 0;

static const char* kDivNameTab[GridQuantize::kDivisions] = {
    "1", "3/4", "2/3", "1/2", "1/3", "1/4", "1/6", "1/8"};
// Guarded so a broken IndexOf reports instead of crashing the table printer.
static const char* DivName(int i) {
  return (i >= 0 && i < GridQuantize::kDivisions) ? kDivNameTab[i] : "OFF-GRID";
}

static void Check(bool ok, const char* fmt, ...) {
  if (ok) return;
  g_fail = 1;
  va_list ap;
  va_start(ap, fmt);
  printf("     FAIL  ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}

// ---------------------------------------------------------------- 1. table
static void ShowTable(size_t T) {
  printf("  T = %zu samples (%.1f ms @48k)\n", T, (double)T * 1000.0 / 48000.0);
  printf("    %-5s  %9s  %9s\n", "div", "samples", "DivisionOf");
  for (int i = 0; i < GridQuantize::kDivisions; i++) {
    const size_t b = GridQuantize::Boundary(i, T);
    printf("    %-5s  %9zu  %9.4f\n", DivName(i), b,
           (double)GridQuantize::DivisionOf(b, T));
  }
}

// ------------------------------------------ 6a. independent boundary reference
// Recomputed here in exact integer arithmetic, deliberately NOT by calling the
// header. Comparing Quantize's output against Boundary() alone only proves the
// block is self-consistent — a float implementation would agree with itself.
// This is what catches drift in 1/3 and 1/6, and at T above 2^24 it is what
// catches a float boundary that can no longer represent the sample count.
static size_t RefBoundary(int i, size_t T) {
  const unsigned long long p = T;
  const unsigned long long n = GridQuantize::kDivNum[i];
  const unsigned long long d = GridQuantize::kDivDen[i];
  return (size_t)((2ULL * p * n + d) / (2ULL * d));
}

static void TestBoundaryExact(size_t T) {
  for (int i = 0; i < GridQuantize::kDivisions; i++) {
    const size_t got = GridQuantize::Boundary(i, T);
    const size_t ref = RefBoundary(i, T);
    Check(got == ref, "T=%zu: Boundary(%s) = %zu, exact integer value is %zu", T,
          DivName(i), got, ref);
  }
}

// ------------------------------------------------- 1. every division reachable
static void TestReachable(size_t T) {
  for (int i = 0; i < GridQuantize::kDivisions; i++) {
    const size_t b = GridQuantize::Boundary(i, T);
    const size_t q = GridQuantize::Quantize(b, T);
    Check(q == b, "T=%zu: raw exactly on %s (%zu samples) quantised to %zu",
          T, DivName(i), b, q);
    const int idx = GridQuantize::IndexOf(q, T);
    // At tiny T two divisions can collapse onto the same sample count; the
    // longer fraction wins, so only require the count to match.
    Check(idx >= 0 && GridQuantize::Boundary(idx, T) == b,
          "T=%zu: %s (%zu samples) does not map back onto the grid", T,
          DivName(i), b);
  }
}

// ------------------------------------------------ 2./3. direction of rounding
struct DirCase {
  const char* name;
  size_t      raw;
  int         expect_div;   // index into the division table
  const char* why;
};

static void TestDirections() {
  // T = 48000 (1 s @48k). Boundaries: 48000 36000 32000 24000 16000 12000 8000 6000
  const size_t T = 48000;
  const DirCase cases[] = {
      // rounds DOWN — 13000 is 1000 past 1/4 and 3000 short of 1/3
      {"13000 -> 1/4 (down)",   13000, 5, "1000 past 1/4, 3000 short of 1/3"},
      // rounds UP — 15000 is 3000 past 1/4 and 1000 short of 1/3
      {"15000 -> 1/3 (up)",     15000, 4, "3000 past 1/4, 1000 short of 1/3"},
      // truncation: just past a boundary stays AT it, does not promote
      {"24100 -> 1/2 (trunc)",  24100, 3, "100 samples of overhang are cut"},
      {"12001 -> 1/4 (trunc)",  12001, 5, "1 sample of overhang is cut"},
      {"36050 -> 3/4 (trunc)",  36050, 1, "overhang past the dotted boundary"},
      // rounds up across the top of the set, but never past T
      {"47000 -> 1   (up)",     47000, 0, "nearer T than 3/4"},
      // exact midpoint 1/4..1/3 = 14000 -> ties go UP
      {"14000 -> 1/3 (tie up)", 14000, 4, "exact midpoint, ties round up"},
  };
  printf("  direction of rounding, T = %zu\n", T);
  printf("    %-24s  %9s  %6s  %s\n", "case", "result", "div", "why");
  for (const DirCase& c : cases) {
    const size_t q   = GridQuantize::Quantize(c.raw, T);
    const size_t exp = GridQuantize::Boundary(c.expect_div, T);
    printf("    %-24s  %9zu  %6s  %s\n", c.name, q,
           DivName(GridQuantize::IndexOf(q, T)), c.why);
    Check(q == exp, "%s: got %zu, expected %zu (%s)", c.name, q, exp,
          DivName(c.expect_div));
    // and say explicitly which way it went
    if (c.expect_div != 0) {
      const bool went_down = q < c.raw;
      const bool want_down = exp < c.raw;
      Check(went_down == want_down, "%s: rounded the wrong way", c.name);
    }
  }
}

// ------------------------------------------------------------- 4./5. edges
static void TestEdges(size_t T) {
  const size_t smallest = GridQuantize::Boundary(GridQuantize::kDivisions - 1, T);
  const size_t full     = GridQuantize::Boundary(0, T);

  // floor: anything under half the smallest division still gives the smallest
  const size_t under[] = {0, 1, smallest / 4, (smallest / 2) - 1, smallest / 2};
  for (size_t r : under) {
    const size_t q = GridQuantize::Quantize(r, T);
    Check(q == smallest, "T=%zu: raw %zu gave %zu, expected the floor %zu", T, r,
          q, smallest);
    Check(q != 0, "T=%zu: raw %zu quantised to zero", T, r);
  }

  // clamp: at T and beyond, the answer is T
  const size_t over[] = {full, full + 1, full * 2, full * 37 + 11};
  for (size_t r : over) {
    const size_t q = GridQuantize::Quantize(r, T);
    Check(q == full, "T=%zu: raw %zu gave %zu, expected the clamp %zu", T, r, q,
          full);
  }
  // and nothing ever exceeds T
  Check(GridQuantize::Quantize(full - 1, T) <= full, "T=%zu: result exceeded T", T);
}

// ------------------------------------------- 6./7. exactness over a T sweep
struct SweepStat {
  size_t   T;
  unsigned probes;
  unsigned hits[GridQuantize::kDivisions];
};

static SweepStat TestSweepOne(size_t T) {
  SweepStat s{};
  s.T = T;
  for (int i = 0; i < GridQuantize::kDivisions; i++) s.hits[i] = 0;

  const size_t full = GridQuantize::Boundary(0, T);
  size_t       prev = 0;
  // Step fine enough to land inside every gap even at small T.
  const size_t step = (full / 512) ? (full / 512) : 1;
  for (size_t raw = 0; raw <= full + step * 4; raw += step) {
    const size_t q = GridQuantize::Quantize(raw, T);
    s.probes++;

    // exactly one of the listed divisions, computed the same way every time
    const int idx = GridQuantize::IndexOf(q, T);
    Check(idx >= 0, "T=%zu raw=%zu: result %zu is not a boundary of T", T, raw, q);
    if (idx < 0) break;
    s.hits[idx]++;

    // DivisionOf round-trips: the named fraction reproduces the sample count
    const float  frac = GridQuantize::DivisionOf(q, T);
    const size_t back = (size_t)llround((double)frac * (double)T);
    Check(frac > 0.f, "T=%zu: DivisionOf(%zu) returned 0", T, q);
    // DivisionOf returns a float ratio, so reconstructing a sample count from it
    // carries float epsilon (~6e-8 relative) plus the rounding. Tolerance scales
    // with T accordingly. This is exactly why callers must use Boundary(), not
    // this ratio, to derive sample counts.
    const size_t tol = 1 + (size_t)((double)T * 1e-7);
    Check(back == q || (back > q ? back - q : q - back) <= tol,
          "T=%zu: DivisionOf(%zu)=%.6f reconstructs %zu", T, q, (double)frac, back);

    // within range, and monotone non-decreasing in raw_len
    Check(q >= GridQuantize::Boundary(GridQuantize::kDivisions - 1, T) && q <= full,
          "T=%zu raw=%zu: result %zu out of range", T, raw, q);
    Check(q >= prev, "T=%zu raw=%zu: result %zu went backwards from %zu", T, raw,
          q, prev);
    prev = q;
  }
  return s;
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  printf("== grid quantiser (division set: ");
  for (int i = 0; i < GridQuantize::kDivisions; i++)
    printf("%s%s", DivName(i), i + 1 < GridQuantize::kDivisions ? " " : ")\n");

  ShowTable(48000);
  printf("\n");
  TestDirections();
  printf("\n");

  // A wide sweep of T, deliberately including values that are not divisible by
  // 3, 6 or 8 — that is where an implementation using floats or truncating
  // division starts to drift, and where two divisions can collide.
  const size_t Ts[] = {
      240,    997,    1000,   1024,   4801,   12000,  12345,  16000,
      23999,  24000,  30001,  32768,  47999,  48000,  48001,  65537,
      96000,  144000, 199999, 240000, 480000, 1000003, 16777219, 20000006,
  };
  printf("  sweep: result is always an exact boundary, monotone, round-trips\n");
  printf("    %-9s  %7s  %s\n", "T", "probes", "divisions hit (1 .. 1/8)");
  for (size_t T : Ts) {
    const SweepStat s = TestSweepOne(T);
    TestBoundaryExact(T);
    TestReachable(T);
    TestEdges(T);
    printf("    %-9zu  %7u  ", s.T, s.probes);
    for (int i = 0; i < GridQuantize::kDivisions; i++) printf("%6u", s.hits[i]);
    printf("\n");
    for (int i = 0; i < GridQuantize::kDivisions; i++) {
      Check(s.hits[i] > 0, "T=%zu: division %s was never produced by the sweep",
            s.T, DivName(i));
    }
  }

  // 1/3 and 1/6 must not drift: at T divisible by 6 they are exact, and 1/6 is
  // always half of 1/3 to within a sample at any T.
  printf("\n  ternary exactness\n");
  printf("    %-9s  %9s  %9s  %s\n", "T", "1/3", "1/6", "note");
  const size_t Tt[] = {48000, 47999, 30001, 997, 1000003};
  for (size_t T : Tt) {
    const size_t third = GridQuantize::Boundary(4, T);
    const size_t sixth = GridQuantize::Boundary(6, T);
    const bool   exact = (T % 6 == 0);
    printf("    %-9zu  %9zu  %9zu  %s\n", T, third, sixth,
           exact ? "T divisible by 6" : "awkward T");
    if (exact) {
      Check(third * 3 == T, "T=%zu: 1/3 = %zu, 3x = %zu", T, third, third * 3);
      Check(sixth * 6 == T, "T=%zu: 1/6 = %zu, 6x = %zu", T, sixth, sixth * 6);
    }
    const size_t twice = sixth * 2;
    Check(twice > third ? twice - third <= 1 : third - twice <= 1,
          "T=%zu: 2 x 1/6 (%zu) != 1/3 (%zu)", T, twice, third);
  }

  printf("%s\n", g_fail ? "  RESULT: FAIL" : "  RESULT: ok");
  return g_fail;
}
