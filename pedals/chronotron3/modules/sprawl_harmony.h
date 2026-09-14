#pragma once
//
// sprawl — harmony logic + RNG. Ported 1:1 from NitroTron3's Mode B helpers
// (pedals/nitrotron3/main.cpp, "Mode B harmony logic"). Only structural change:
// the file-static xorshift32 RNG becomes an owned instance (SprawlRng) so the
// module carries its own state; algorithm and seed are unchanged.
//
#include "sprawl_constants.h"
#include <cstdint>
#include <math.h>

// Simple xorshift32 RNG for grain scatter
struct SprawlRng {
  uint32_t state = 12345;
  inline float Next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) / 4294967295.f;
  }
};

// Natural harmonic intervals (semitones). Asymmetric: floor at -1 octave,
// ceiling at +3 octaves, with upper-harmonic partials filled in.
static const int RESONANCES[] = {
    -12, -7, -5, 0, 5, 7, 12, 19, 24, 28, 31, 36
};
static constexpr int NUM_RES = 12;

// K1 → semitones. Lower half always covers -12..0; upper half spans
// 0..max_up. UP mode uses max_up=12 (symmetric ±12); MID/DOWN uses
// max_up=36 so the resonance table can reach upper partials.
static inline int K1ToSemi(float k1, int max_up) {
  if (k1 < 0.5f) return static_cast<int>(roundf((k1 - 0.5f) * 24.f));
  return static_cast<int>(roundf((k1 - 0.5f) * 2.f * static_cast<float>(max_up)));
}

// Per-semitone feedback scale (SW2 UP / fixed-interval only).
// Unison piles up because each loop pass replays at the same pitch; pitch-down
// loses energy to the wet HPF each pass and needs compensation.
// Curve: unison cut to FB_UNISON_SCALE, up-side ramps back to 1.0 by +3 semi,
// down-side boosts +0.12 per semitone for a saturated growl, capped at 1.9.
static inline float FixedIntervalFeedbackScale(int k1_semi) {
  if (k1_semi == 0) return FB_UNISON_SCALE;
  if (k1_semi > 0) {
    float t = static_cast<float>(k1_semi) / 3.f;
    if (t > 1.f) t = 1.f;
    return FB_UNISON_SCALE + (1.f - FB_UNISON_SCALE) * t;
  }
  float scale = 1.f + 0.12f * static_cast<float>(-k1_semi);
  if (scale > 1.9f) scale = 1.9f;
  return scale;
}

// Harmony helper bundle — owns the RNG so GrainPitchRatio's re-roll draws from
// the same stream as the scheduler's scatter/flip/length/jitter draws (call
// ORDER is part of the port: pos_offset, flip, [pitch pick], len_var, jitter).
struct SprawlHarmony {
  SprawlRng rng;

  // Compute pitch ratio for one grain.
  // harmony == 0 (SW2 UP): fixed interval, K1 = exact semitones in [-12, +12].
  // harmony != 0 (SW2 MID/DOWN): grain picks uniformly from a ±1 entry window
  // around K1's closest RESONANCES entry. K1 spans -12..+36 for the table scan.
  float GrainPitchRatio(int harmony, float k1) {
    float semi;

    if (harmony == 0) {
      semi = static_cast<float>(K1ToSemi(k1, 12));
    } else {
      int k1_semi = K1ToSemi(k1, 36);
      int closest = 0;
      int min_dist = 100;
      for (int i = 0; i < NUM_RES; i++) {
        int d = RESONANCES[i] - k1_semi;
        if (d < 0) d = -d;
        if (d < min_dist) { min_dist = d; closest = i; }
      }
      int lo = (closest > 0) ? closest - 1 : 0;
      int hi = (closest < NUM_RES - 1) ? closest + 1 : NUM_RES - 1;
      int idx = lo + static_cast<int>(rng.Next() * static_cast<float>(hi - lo + 1));
      if (idx > hi) idx = hi;
      semi = static_cast<float>(RESONANCES[idx]);
    }

    return powf(2.f, semi / 12.f);
  }
};
