#pragma once

#include <cstdint>
#include "constants.h"

// Zoned digital-glitch processor for Mode B SW1 MIDDLE.
//
// Stateless. Caller computes side (0 = XOR, 1 = ROT) and effect_pos
// (0..1 within the active range, beyond the deadzone) once per block.
// See docs/MODE_B_TEXTURE_IDEAS.md.

inline int16_t GlitchFloatToQ15(float x) {
  if (x > 0.9999695f) x = 0.9999695f;
  if (x < -1.f)       x = -1.f;
  return static_cast<int16_t>(x * 32767.f);
}

inline float GlitchQ15ToFloat(int16_t x) {
  return static_cast<float>(x) * (1.f / 32768.f);
}

inline uint16_t GlitchRotR(uint16_t v, int n) {
  return static_cast<uint16_t>((v >> n) | (v << (16 - n)));
}

inline float ProcessGlitch(float in, int side, float effect_pos) {
  if (effect_pos <= 0.f) return in;

  const int16_t q = GlitchFloatToQ15(in);

  if (side == 0) {
    // XOR side (CCW): bit 0..GLITCH_XOR_MAX_BIT
    const float bit_f = effect_pos * static_cast<float>(GLITCH_XOR_MAX_BIT);
    int bit_lo        = static_cast<int>(bit_f);
    if (bit_lo < 0) bit_lo = 0;
    if (bit_lo >= GLITCH_XOR_MAX_BIT) bit_lo = GLITCH_XOR_MAX_BIT - 1;
    const float frac  = bit_f - static_cast<float>(bit_lo);
    const int16_t a   = static_cast<int16_t>(q ^ (1 << bit_lo));
    const int16_t b   = static_cast<int16_t>(q ^ (1 << (bit_lo + 1)));
    const float fa    = GlitchQ15ToFloat(a);
    const float fb    = GlitchQ15ToFloat(b);
    return fa * (1.f - frac) + fb * frac;
  }

  // ROT side (CW): right-rotate by 1..GLITCH_ROT_MAX
  const float rot_f = effect_pos * static_cast<float>(GLITCH_ROT_MAX - 1) + 1.f;
  int rot_lo        = static_cast<int>(rot_f);
  if (rot_lo < 1) rot_lo = 1;
  if (rot_lo >= GLITCH_ROT_MAX) rot_lo = GLITCH_ROT_MAX - 1;
  const float frac  = rot_f - static_cast<float>(rot_lo);
  const uint16_t u  = static_cast<uint16_t>(q);
  const int16_t a   = static_cast<int16_t>(GlitchRotR(u, rot_lo));
  const int16_t b   = static_cast<int16_t>(GlitchRotR(u, rot_lo + 1));
  const float fa    = GlitchQ15ToFloat(a);
  const float fb    = GlitchQ15ToFloat(b);
  return fa * (1.f - frac) + fb * frac;
}
