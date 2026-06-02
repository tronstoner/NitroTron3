#pragma once

#include <cstdint>
#include <cstddef>
#include "constants.h"

// Event-driven digital-glitch processor for Mode B SW1 MIDDLE.
//
// Buchla Source-of-Uncertainty model: random trigger timing, randomised
// per-event parameters. Trigger rate is gated by input envelope so a
// silent input produces silence (no events fire, dry passes through).
// During an event the wet/dry mix is scaled by the live envelope, so
// the glitch only speaks while the bass is playing.
//
// CCW (side=0) payload: bit-flip — XOR a random bit (0..max_bit, scaled
// by effect_pos) for the event duration.
//
// CW  (side=1) payload: timing — small ring buffer snapshotted at event
// start, then read with one of {freeze, short-loop stutter, reverse}.
//
// See docs/MODE_B_TEXTURE_IDEAS.md.

class GlitchEvents {
 public:
  void Init() {
    state_ = State::IDLE;
    write_pos_ = 0;
    event_remaining_ = 0;
    xor_mask_ = 0;
    cw_type_ = 0;
    snapshot_pos_ = 0;
    loop_start_ = 0;
    loop_len_ = 1;
    cw_offset_ = 0;
    ramp_ = 0.f;
    rng_ = 0x9E3779B9u;
    for (int i = 0; i < GLITCH_BUFFER_SAMPLES; i++) buffer_[i] = 0.f;
  }

  // Per-sample process. effect_pos = 0..1 (already deadzone-mapped),
  // side = 0 (CCW/XOR) or 1 (CW/timing).
  float Process(float in, int side, float effect_pos) {
    // Always write input to the ring buffer (CW payload needs history)
    const int w = write_pos_;
    buffer_[w] = in;
    write_pos_ = (w + 1) % GLITCH_BUFFER_SAMPLES;

    // Trigger probability: events/sec scaled by effect_pos, converted to per-sample.
    if (state_ == State::IDLE && effect_pos > 0.f) {
      const float rate_hz = effect_pos * GLITCH_EVENT_RATE_HZ_MAX;
      const float p = rate_hz * (1.f / 48000.f);
      if (NextRand() < p) {
        StartEvent(side, effect_pos, w);
      }
    }

    // Compute glitched payload (only meaningful when ACTIVE)
    float glitched = in;
    if (state_ == State::ACTIVE) {
      glitched = ApplyPayload(in);
      event_remaining_--;
      if (event_remaining_ <= 0) {
        // Chain into next event with probability effect_pos². At the
        // extreme this approaches 1, so glitches become continuous and
        // the dry signal is no longer audible between events.
        const float chain_p = effect_pos * effect_pos;
        if (NextRand() < chain_p) {
          StartEvent(active_side_, effect_pos, write_pos_);
        } else {
          state_ = State::IDLE;
        }
      }
    }

    // Wet-mix target: full wet during event, zero between events.
    const float target = (state_ == State::ACTIVE) ? 1.f : 0.f;

    // Click-free ramp toward target
    const float step = 1.f / static_cast<float>(GLITCH_RAMP_SAMPLES);
    if (ramp_ < target) {
      ramp_ += step;
      if (ramp_ > target) ramp_ = target;
    } else if (ramp_ > target) {
      ramp_ -= step;
      if (ramp_ < target) ramp_ = target;
    }

    return in * (1.f - ramp_) + glitched * ramp_;
  }

 private:
  enum class State { IDLE, ACTIVE };

  static int16_t FloatToQ15(float x) {
    if (x > 0.9999695f) x = 0.9999695f;
    if (x < -1.f)       x = -1.f;
    return static_cast<int16_t>(x * 32767.f);
  }
  static float Q15ToFloat(int16_t x) {
    return static_cast<float>(x) * (1.f / 32768.f);
  }

  // xorshift32 → [0,1)
  float NextRand() {
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(x) * (1.f / 4294967296.f);
  }

  void StartEvent(int side, float effect_pos, int w) {
    state_ = State::ACTIVE;

    // Min duration scales with effect_pos: events get progressively
    // longer at the extremes so each one fills more time.
    const int dur_floor = GLITCH_EVENT_DUR_MIN_SAMPLES +
        static_cast<int>(effect_pos * 0.5f *
            static_cast<float>(GLITCH_EVENT_DUR_MAX_SAMPLES - GLITCH_EVENT_DUR_MIN_SAMPLES));
    const int dur_range = GLITCH_EVENT_DUR_MAX_SAMPLES - dur_floor;
    event_remaining_ = dur_floor +
        static_cast<int>(NextRand() * static_cast<float>(dur_range));

    if (side == 0) {
      // CCW: max_bit scales with effect_pos. Bit pick is biased *toward*
      // max_bit (using NextRand² as low-biased random, subtracted from
      // max_bit). Picks live in roughly the top 4 bits of the range, with
      // strong weight on max_bit itself — uniform random would average
      // around bit max_bit/2 and produce inaudibly small XOR masks.
      int max_bit = static_cast<int>(effect_pos * static_cast<float>(GLITCH_XOR_MAX_BIT));
      if (max_bit < 1) max_bit = 1;
      if (max_bit > GLITCH_XOR_MAX_BIT) max_bit = GLITCH_XOR_MAX_BIT;
      const float r = NextRand() * NextRand();   // biased toward 0
      int drop = static_cast<int>(r * 4.f);      // 0..3, biased to 0
      int bit = max_bit - drop;
      if (bit < 0) bit = 0;
      xor_mask_ = static_cast<int16_t>(1 << bit);
      active_side_ = 0;
    } else {
      // CW: pick a random timing-payload type
      const float r = NextRand();
      cw_type_ = (r < 0.34f) ? 0 : (r < 0.67f ? 1 : 2);
      // Snapshot 1–20 ms behind the write head
      const int min_off = 48;
      const int max_off = 20 * 48;
      const int offset = min_off +
          static_cast<int>(NextRand() * static_cast<float>(max_off - min_off));
      int sp = w - offset;
      while (sp < 0) sp += GLITCH_BUFFER_SAMPLES;
      snapshot_pos_ = sp;
      cw_offset_ = 0;

      if (cw_type_ == 1) {
        // Stutter: 1–4 ms loop window
        loop_start_ = snapshot_pos_;
        loop_len_ = 48 + static_cast<int>(NextRand() * 144.f);
        if (loop_len_ < 1) loop_len_ = 1;
      }
      active_side_ = 1;
    }
  }

  float ApplyPayload(float in) {
    if (active_side_ == 0) {
      const int16_t q = FloatToQ15(in);
      return Q15ToFloat(static_cast<int16_t>(q ^ xor_mask_));
    }
    // CW timing payload
    int read_pos;
    if (cw_type_ == 0) {
      // Freeze: hold the snapshot sample
      read_pos = snapshot_pos_;
    } else if (cw_type_ == 1) {
      // Stutter loop
      int p = loop_start_ + cw_offset_;
      while (p >= GLITCH_BUFFER_SAMPLES) p -= GLITCH_BUFFER_SAMPLES;
      cw_offset_++;
      if (cw_offset_ >= loop_len_) cw_offset_ = 0;
      read_pos = p;
    } else {
      // Reverse: walk backward from snapshot
      int p = snapshot_pos_ - cw_offset_;
      while (p < 0) p += GLITCH_BUFFER_SAMPLES;
      cw_offset_++;
      read_pos = p;
    }
    return buffer_[read_pos];
  }

  State state_ = State::IDLE;
  int    active_side_ = 0;
  int    event_remaining_ = 0;
  float  ramp_ = 0.f;

  // CCW payload state
  int16_t xor_mask_ = 0;

  // CW payload state
  float buffer_[GLITCH_BUFFER_SAMPLES];
  int   write_pos_ = 0;
  int   cw_type_ = 0;
  int   snapshot_pos_ = 0;
  int   loop_start_ = 0;
  int   loop_len_ = 1;
  int   cw_offset_ = 0;

  uint32_t rng_ = 0x9E3779B9u;
};
