#pragma once
//
// grid_quantize.h — snap a captured loop length to a division of a master
// period T.
//
// It answers exactly one question: *given a master period T and a raw captured
// length, what length should the loop actually be?* Nothing else. No state, no
// audio, no allocation, no libm — every value it returns is an integer sample
// count computed in integer arithmetic.
//
// WHY THERE IS NO CLOCK IN HERE. This is the whole design, not an omission.
// T is a *period*, not a grid: tap tempo sets how long a division lasts, it
// does not establish a free-running bar line that the player then has to fall
// in with. Every capture anchors its own grid at the moment its gate opens, so
// the only quantity that ever exists is a LENGTH measured from that anchor —
// never an absolute time, never a phase against a running counter. That is what
// keeps the effect feeling like a delay (timing relative to when you played)
// instead of a synced looper (timing relative to a clock you are a guest of),
// and it is what lets six voices share a pulse while each keeps its own beat
// one, so they phase against each other instead of locking. Give this block a
// clock and you get the absolute-grid behaviour the plan explicitly rejects.
// See docs/ChronoTron3/vestige-onward-rework-plan.md §4.3.
//
// WHY NEAREST, NOT UP. Rounding a short capture up to the next boundary is the
// permissive option: it always keeps everything played and it always lengthens.
// We round to the NEAREST boundary instead, which means a capture that runs a
// little past a boundary comes back AT that boundary and the overhang is
// truncated. That is intended and must not be "fixed": a late release gets cut
// rather than dragging the loop a whole division long. Quantising is supposed to
// be able to take material away — otherwise a sloppy phrase silently promotes
// itself to the next division and the groove the division set was chosen for
// never happens. (§4.3 rule 2. Note that §7 Stage 2 summarises this as "extend
// to the next boundary"; §4.3 is the normative rule and this is what it says.)
//
// Ties (a capture exactly halfway between two boundaries) round UP, i.e. toward
// the longer division. Exact ties are vanishingly rare with sample-accurate
// lengths; the choice matters only for the degenerate small-T cases and keeps
// the sub-minimum rule below consistent.
//
#include <cstddef>

class GridQuantize {
 public:
  // The division set, as exact integer fractions of T. FIXED, by design: the
  // point of the odd divisions is that loops of different lengths phase against
  // each other, which is a property of the instrument rather than a performance
  // parameter (§4.5). Edit the two tables together; they must stay sorted
  // LONGEST FIRST, which is what makes the nearest-search and the clamp below
  // trivial.
  //
  //     1  ·  3/4  ·  2/3  ·  1/2  ·  1/3  ·  1/4  ·  1/6  ·  1/8
  //
  // Binary alone (1, 1/2, 1/4, 1/8) is tidy and static — every loop is a
  // power-of-two multiple of every other, so loops that start together stay
  // together. The ternary and dotted entries are what make a 2/3 loop realign
  // with a 1/2 loop only every six periods.
  static constexpr int kDivisions = 8;
  static constexpr unsigned kDivNum[kDivisions] = {1, 3, 2, 1, 1, 1, 1, 1};
  static constexpr unsigned kDivDen[kDivisions] = {1, 4, 3, 2, 3, 4, 6, 8};

  // Sample count of division `i` of `period_samples`, rounded to nearest.
  // Integer arithmetic throughout: 1/3 and 1/6 of a period never drift, and the
  // same T always yields the same counts, so loops cut at different times stay
  // exactly commensurate. Index 0 is T itself, index kDivisions-1 the smallest.
  static size_t Boundary(int i, size_t period_samples) {
    if (i < 0 || i >= kDivisions) return 0;
    const unsigned long long p = static_cast<unsigned long long>(period_samples);
    const unsigned long long n = kDivNum[i];
    const unsigned long long d = kDivDen[i];
    return static_cast<size_t>((2ULL * p * n + d) / (2ULL * d));   // round half up
  }

  // The length the loop should actually be.
  //
  //   - at or beyond T                  -> T             (T is the maximum)
  //   - otherwise                       -> nearest boundary, up OR down
  //   - below half the smallest         -> the smallest division, never zero
  //
  // The floor falls out of the search rather than being a special case: zero is
  // not in the division set, so anything shorter than the smallest boundary is
  // nearest to it by definition. It is called out because it is a rule the
  // caller depends on — a stab must produce the shortest loop, not silence.
  static size_t Quantize(size_t raw_len, size_t period_samples) {
    if (period_samples == 0) return 0;
    const size_t full = Boundary(0, period_samples);          // == T
    if (raw_len >= full) return full;                         // clamp at T

    size_t best      = Boundary(kDivisions - 1, period_samples);
    size_t best_dist = (raw_len > best) ? (raw_len - best) : (best - raw_len);
    // Longest first, and ties keep the entry already held, so a tie resolves to
    // the LONGER division.
    for (int i = 0; i < kDivisions - 1; i++) {
      const size_t b    = Boundary(i, period_samples);
      const size_t dist = (raw_len > b) ? (raw_len - b) : (b - raw_len);
      if (dist < best_dist) { best_dist = dist; best = b; }
    }
    return best;
  }

  // Index of the division a quantised length sits on, or -1 if it sits on none
  // (which should never happen for a value that came out of Quantize — treat it
  // as a bug signal rather than rounding it away). Longest first, so when two
  // divisions collapse onto the same sample count at very small T the longer
  // fraction wins.
  static int IndexOf(size_t quantised_len, size_t period_samples) {
    if (period_samples == 0) return -1;
    for (int i = 0; i < kDivisions; i++) {
      if (Boundary(i, period_samples) == quantised_len) return i;
    }
    return -1;
  }

  // Which fraction of T the loop landed on, e.g. 0.5f for 1/2 — for LEDs and
  // diagnostics only. Returns 0.f if the length is not on the grid. Never feed
  // this back into a sample count: derive counts from Boundary(), which is
  // exact.
  static float DivisionOf(size_t quantised_len, size_t period_samples) {
    const int i = IndexOf(quantised_len, period_samples);
    if (i < 0) return 0.f;
    return static_cast<float>(kDivNum[i]) / static_cast<float>(kDivDen[i]);
  }
};

// The firmware builds as gnu++14, where an odr-used static constexpr member
// array still needs one out-of-class definition (C++17 makes it implicitly
// inline, which is why the C++17 host tests link without this). Pre-C++17 this
// is a non-inline definition, so include this header from ONE translation unit
// per binary — true for every pedal today (single-TU shells); a second TU fails
// loudly at link time, not silently.
#if __cplusplus < 201703L
constexpr unsigned GridQuantize::kDivNum[GridQuantize::kDivisions];
constexpr unsigned GridQuantize::kDivDen[GridQuantize::kDivisions];
#endif
