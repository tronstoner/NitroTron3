#pragma once
// Reusable knob / scalar mapping helpers — pedal-agnostic (core/util).
// Extracted verbatim from NitroTron3.cpp; no behavior change. Waveshapers and
// mode-specific maps that depend on pedal constants stay with the pedal.
#include <math.h>

// Linear interpolate: in (0..1) → (min..max).
static inline float Mapf(float in, float min, float max) {
  return min + in * (max - min);
}

// Exponential mapping for filter cutoff (80 Hz – 8 kHz).
static inline float MapCutoff(float knob) {
  constexpr float MIN_HZ = 80.f;
  constexpr float MAX_HZ = 8000.f;
  return MIN_HZ * powf(MAX_HZ / MIN_HZ, knob);
}

// Mix pre-warp — smoothstep on top of a sqrt equal-power crossfade.
static inline float MixCurve(float mix) {
  return mix * mix * (3.f - 2.f * mix);
}

// Quantize knob (0–1) into N equal steps, returning 0..N-1.
static inline int Quantize(float knob, int steps) {
  int val = static_cast<int>(knob * steps);
  if (val >= steps) val = steps - 1;
  return val;
}

// MIDI note to frequency: f = 440 * 2^((note - 69) / 12).
static inline float MidiToFreq(float note) {
  return 440.f * powf(2.f, (note - 69.f) / 12.f);
}

// Remap pot range — measured 0.000–0.968 at physical extremes.
constexpr float KNOB_MIN = 0.004f;
constexpr float KNOB_MAX = 0.964f;

static inline float RemapKnob(float raw) {
  float v = (raw - KNOB_MIN) / (KNOB_MAX - KNOB_MIN);
  if (v < 0.f) v = 0.f;
  if (v > 1.f) v = 1.f;
  return v;
}
