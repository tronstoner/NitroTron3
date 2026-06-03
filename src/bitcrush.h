#pragma once

#include <cmath>
#include <cstdint>
#include "constants.h"

// Bit-flipper for Mode C SW1=MIDDLE drive flavor.
//
// Deterministic XOR of a single Q15 bit on every sample — same mechanism as
// Mode B SW1 MIDDLE CCW (event-driven glitch), but applied continuously with
// no random event timing. K4 picks which bit gets flipped: 0 = LSB
// (inaudible), MODE_C_BITCRUSH_MAX_BIT = ±0.25 of full scale on every
// parity flip — the "fuzz" character.
//
// Gate: input envelope keys a 1 ms click-free wet/dry ramp so silent input →
// silent output, no constant XOR pedestal on quiet passages.

class BitCrush {
 public:
  void Init() { ramp_ = 0.f; }

  // crush_amt in [0,1]: 0 = no flip (clean), 1 = flip MAX_BIT.
  // env is the current envelope-follower value (used for the gate).
  float Process(float in, float crush_amt, float env) {
    // Bit position scales linearly with K4.
    int bit = static_cast<int>(
        roundf(crush_amt * static_cast<float>(MODE_C_BITCRUSH_MAX_BIT)));
    if (bit < 0) bit = 0;
    if (bit > MODE_C_BITCRUSH_MAX_BIT) bit = MODE_C_BITCRUSH_MAX_BIT;

    // bit==0 → XOR'ing the LSB is effectively inaudible; skip the conversion.
    float flipped = in;
    if (bit > 0) {
      const int16_t q   = FloatToQ15(in);
      const int16_t xor_mask = static_cast<int16_t>(1 << bit);
      flipped = Q15ToFloat(static_cast<int16_t>(q ^ xor_mask));
    }

    // Gate: ramp wet/dry toward 1.f when env opens, 0.f when env closes.
    const float target = (env > MODE_C_BITCRUSH_ENV_GATE) ? 1.f : 0.f;
    const float ramp_step =
        1.f / static_cast<float>(MODE_C_BITCRUSH_RAMP_SAMPLES);
    if (ramp_ < target) {
      ramp_ += ramp_step;
      if (ramp_ > target) ramp_ = target;
    } else if (ramp_ > target) {
      ramp_ -= ramp_step;
      if (ramp_ < target) ramp_ = target;
    }
    return in * (1.f - ramp_) + flipped * ramp_;
  }

 private:
  static int16_t FloatToQ15(float x) {
    if (x >  0.9999695f) x =  0.9999695f;
    if (x < -1.f)        x = -1.f;
    return static_cast<int16_t>(x * 32767.f);
  }
  static float Q15ToFloat(int16_t x) {
    return static_cast<float>(x) * (1.f / 32768.f);
  }

  float ramp_ = 0.f;
};
