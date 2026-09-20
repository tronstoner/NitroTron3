#pragma once
//
// diffuser.h — 4-stage Schroeder allpass diffuser.
//
// Port of Mutable Instruments Clouds / Parasites `Diffuser` (dsp/fx/diffuser.h),
// mono. In Parasites' LOOPING DELAY mode this is what the DENSITY knob drives:
// `diffuser_.set_amount(parameters_.density)`, applied to the playback output.
//
// An allpass leaves the magnitude spectrum alone and only scrambles phase, so a
// transient is spread into ~30 ms of dense decaying hash while sustained
// material passes through unchanged. That is what washes attacks out without
// repeating them, without flattening dynamics and without touching timing —
// the thing a grain cloud cannot do, because a cloud can only copy a transient
// around, never dissolve it.
//
// Delays are Clouds' 126 / 180 / 269 / 444 taps scaled from its 32 kHz to
// 48 kHz (x1.5). Coefficient and structure are unchanged: fixed lengths, fixed
// g, no modulation, no randomness.
//
#include <cstddef>

class Diffuser {
 public:
  // Clouds runs FOUR stages per stereo channel with a DIFFERENT delay set on
  // each side. A mono listener hears the SUM of those two chains, and that sum
  // is where the character comes from: two decorrelated allpass networks added
  // together cancel and reinforce across frequency (the "phasy" part) and
  // average out the fixed comb resonances a single chain has on its own.
  // So both sets run in PARALLEL here and are summed -- a mono downmix of
  // Clouds' stereo diffuser. Cascading them in series instead stacks the
  // colouration rather than cancelling it, and sounds stiff and resonant.
  static constexpr int   kStages = 8;      // 2 parallel chains of 4
  static constexpr int   kPerChain = 4;
  // Left  126 / 180 / 269 / 444 and right 151 / 205 / 245 / 405 @ 32 kHz,
  // x1.5 for 48 kHz.
  static constexpr int   kD0 = 189, kD1 = 270, kD2 = 404, kD3 = 666;
  static constexpr int   kD4 = 227, kD5 = 308, kD6 = 368, kD7 = 608;
  static constexpr int   kTotal = kD0 + kD1 + kD2 + kD3 + kD4 + kD5 + kD6 + kD7;
  static constexpr float kAp    = 0.625f;                  // Clouds' fixed coefficient

  void Init() {
    for (int i = 0; i < kTotal; i++) buf_[i] = 0.f;
    for (int s = 0; s < kStages; s++) idx_[s] = 0;
  }

  // One sample. `amount` is the dry/wet of the whole chain (0 = bypass), mixed
  // exactly as Clouds does it: out += amount * (wet - out).
  float Process(float x, float amount) {
    if (amount <= 0.f) return x;
    const int delay[kStages] = {kD0, kD1, kD2, kD3, kD4, kD5, kD6, kD7};
    float* p = buf_;
    float sum = 0.f;
    for (int c = 0; c < 2; c++) {          // the two chains, same input
      float acc = x;
      for (int k = 0; k < kPerChain; k++) {
        const int   s = c * kPerChain + k;
        const int   n = delay[s];
        const int   i = idx_[s];
        const float d = p[i];              // delayed stage output
        const float v = acc + kAp * d;     // v[n] = x[n] + g*v[n-D]
        p[i] = v;
        acc = d - kAp * v;                 // y[n] = v[n-D] - g*v[n]
        idx_[s] = (i + 1 == n) ? 0 : (i + 1);
        p += n;
      }
      sum += acc;
    }
    // The two chains are decorrelated, so their sum is ~sqrt(2) louder than one:
    // scale to hold the level across the dry/wet mix.
    const float wet = sum * 0.70710678f;
    return x + amount * (wet - x);
  }

 private:
  float buf_[kTotal] = {};
  int   idx_[kStages] = {};
};
