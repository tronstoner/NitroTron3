#pragma once
//
// sprawl — grain engine: ring buffer + 8 grain voices + scheduler.
//
// 1:1 port of the scheduler inside NitroTron3's ProcessGranular(): burst/delay/
// scatter, direction flip, pitch re-roll cache, per-grain length variation with
// coupled loops, read-overrun safety, never-steal voice pick, burst spacing vs
// jittered interval. The RNG DRAW ORDER is part of the contract:
//   pos_offset → flip → [GrainPitchRatio pick, only when re-rolled] → len_var
//   → jitter.
//
#include "sprawl_constants.h"
#include "sprawl_harmony.h"
#include "sprawl_params.h"
#include "ring_buffer.h"
#include <cstring>        // memset (Kill)   // core/blocks
#include "grain_voice.h"   // core/blocks
#include <math.h>

class SprawlGrainEngine {
 public:
  // `slab` = externally allocated SDRAM storage (GRAIN_BUF_SAMPLES floats).
  void Init(float* slab, size_t n) { slab_ = slab; slab_n_ = n; ring_.Init(slab, n); }

  // Ring write (dry + feedback return), done before the scheduler tick.
  void Write(float s) { ring_.Write(s); }

  // Note-on burst: fire on the next scheduler tick.
  void ArmBurst() {
    grain_burst_left_ = TRANSIENT_BURST;
    grain_timer_ = 0;
  }

  // Panic (control thread, after the panic envelope has faded the output to
  // silence): wipe the ring, hard-stop every voice, drop any pending burst.
  void Kill() {
    memset(slab_, 0, slab_n_ * sizeof(float));   // wipe the SDRAM slab in place
    for (int v = 0; v < NUM_GRAIN_VOICES; v++) grain_voices_[v].Reset();
    grain_burst_left_ = 0;
  }

  // One sample of the grain engine: scheduler tick + sum of all active voices.
  float Tick(const SprawlParams& p, SprawlHarmony& h) {
    // Grain scheduler — runs for buffer mode AND live-grain mode (K2 noon +
    // K3 CCW). Continuous stream; K3-CW adds chaos, K3-CCW is the clean cloud.
    grain_timer_--;
    if (grain_timer_ <= 0) {
      // Delay: base offset + scatter within K5 range. Burst grains (fired by a
      // note-on) anchor to delay 0 instead — the overrun safety below floors
      // that to the physical minimum, so they read the FRESHEST content (the
      // note just played) regardless of the K5 read-back depth.
      bool burst = (grain_burst_left_ > 0);
      size_t delay;
      if (burst) {
        delay = 0;
      } else {
        size_t scatter_range = p.max_range - p.base_delay;
        size_t pos_offset = static_cast<size_t>(h.rng.Next() * p.glitch_amount * static_cast<float>(scatter_range));
        delay = p.base_delay + pos_offset;
        if (delay > p.max_range) delay = p.max_range;
      }

      // Direction: K2 sign sets the base (CW forward / CCW backward). K3
      // character adds occasional flips against that base — so a forward K2
      // stream reverses occasionally, a backward K2 stream plays forward
      // occasionally. At K3=0 the stream is purely K2's direction.
      bool flip = (p.glitch_amount > 0.1f) && (h.rng.Next() < p.glitch_amount * GRAIN_REVERSE_BIAS);
      bool reverse = p.buf_reverse ? !flip : flip;

      // Pitch re-roll. UP holds a fixed interval (only changes when K1
      // moves — deterministic, so no per-grain re-roll). MID/DOWN roll a new
      // random pick from the ±1 RESONANCES window every `reroll` grains:
      // change every grain (interval 1) across neutral + the CW glitch half,
      // then HELD progressively longer down the CCW cloud (up to
      // GRAIN_PITCH_HOLD_MAX) so the slow smear settles onto stable pitches.
      // K1-range for change detection uses the mode's effective span so UP
      // doesn't spuriously re-roll when K1 clamps above +12.
      int k1_semi_now = (p.harmony == 0) ? K1ToSemi(p.k1, 12) : K1ToSemi(p.k1, 36);
      bool k1_changed = (k1_semi_now != harmony_cached_k1_semi_);
      // Baseline change-every-grain; CCW ramps the hold interval 1 →
      // GRAIN_PITCH_HOLD_MAX with k3mag (longer holds the further out you go).
      int reroll_interval = p.cloud_mode
          ? 1 + static_cast<int>(p.k3mag * static_cast<float>(GRAIN_PITCH_HOLD_MAX - 1))
          : 1;
      if (k1_changed || (p.harmony != 0 && harmony_hold_counter_ <= 0)) {
        harmony_cached_ratio_ = h.GrainPitchRatio(p.harmony, p.k1);
        harmony_cached_k1_semi_ = k1_semi_now;
        harmony_hold_counter_ = (p.harmony == 0) ? 1 : reroll_interval;
      }
      if (p.harmony != 0 && harmony_hold_counter_ > 0) harmony_hold_counter_--;
      float pitch_ratio = harmony_cached_ratio_;
      if (p.freq_shift_active) pitch_ratio = 1.f;  // SW2 DOWN: buffer pitch held at unison
      float comp = 1.f / sqrtf(pitch_ratio);

      // Per-grain length variation → randomized stutter frequency. Scatter
      // THIS grain below the coarse block base by a skewed factor whose depth
      // grows with glitch_amount: most grains stay near base, but occasionally
      // one is dramatically short (a fast CD-hang). Repeats are COUPLED to the
      // result — repeats ≈ base_len / this_len — so a short grain gets many
      // reps (sustained fast hang) and a long grain gets one; the footprint
      // stays ≈ one block base length while the rate varies grain-to-grain.
      // Geometric shortening toward an ABSOLUTE audio-rate length (LEN_MIN),
      // not a fraction of base — that is what turns a slow CD-hang into a
      // pitched buzz (brrr→friii→kriii) as the repeat cycle climbs past ~20 Hz.
      // t is skewed toward 0 (most grains near base) and its reach grows with
      // glitch_amount, so the buzzes get higher/more frequent as K3 opens.
      float len_var = powf(h.rng.Next(), GRAIN_LEN_VAR_SKEW);   // 0..1, mostly small
      // Reach curve: glitch_amount^gamma keeps mid-CW percussive and bends into
      // audio-rate only near full CW (gamma>1). DEPTH scales the full extent.
      float reach = powf(p.glitch_amount, GRAIN_STUTTER_REACH_GAMMA);
      float t = reach * GRAIN_LEN_VAR_DEPTH * len_var;         // 0..1 shorten strength
      float base_f = static_cast<float>(p.grain_len);
      float min_f = GRAIN_STUTTER_LEN_MIN < base_f ? GRAIN_STUTTER_LEN_MIN : base_f;
      size_t this_len = static_cast<size_t>(base_f * powf(min_f / base_f, t));
      if (this_len < static_cast<size_t>(GRAIN_STUTTER_LEN_MIN))
          this_len = static_cast<size_t>(GRAIN_STUTTER_LEN_MIN);
      // Repeats fill ≈ one block-base footprint: short grain → many reps
      // (sustained buzz), long grain → one. Rate varies grain-to-grain.
      int loops = static_cast<int>(base_f / static_cast<float>(this_len) + 0.5f);
      if (loops < 1) loops = 1;
      if (loops > GRAIN_STUTTER_MAX_LOOPS) loops = GRAIN_STUTTER_MAX_LOOPS;

      // Read-overrun safety (ALL grains). A forward grain consumes
      // rate·grain_len source samples; a reverse grain starts a full
      // grain_len ahead of its delay point. Either can read PAST the moving
      // write head into stale 8 s-old ring content unless it starts far
      // enough back: forward pitch-up needs delay ≥ grain_len·(ratio−1),
      // reverse needs ≥ grain_len. Egregious in SW2 MID (RESONANCES reach
      // +36 semi = 8× rate). Applied after the max_range clamp, so a
      // pitched-up grain reaches into valid recent history rather than
      // wrapping. Forward+unison/down needs ~0, so live stays a passthrough.
      {
        size_t safety = reverse
            ? (this_len + 64)
            : (pitch_ratio > 1.f
                  ? static_cast<size_t>(this_len * (pitch_ratio - 1.f)) + 64
                  : 64);
        if (delay < safety) delay = safety;
      }

      // Find an inactive voice — never steal mid-playback
      int voice = -1;
      for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
        int idx = (grain_next_voice_ + v) % NUM_GRAIN_VOICES;
        if (!grain_voices_[idx].IsActive()) {
          voice = idx;
          grain_next_voice_ = (idx + 1) % NUM_GRAIN_VOICES;
          break;
        }
      }
      if (voice >= 0) {
        grain_voices_[voice].Trigger(
            ring_, delay, this_len, reverse, pitch_ratio, comp, loops, p.grain_alpha);
      }

      if (burst) {
        // Space the burst grains closely, then resume the metronomic stream.
        grain_burst_left_--;
        grain_timer_ = TRANSIENT_BURST_SPACING;
      } else {
        float jitter = (h.rng.Next() * 2.f - 1.f) * p.glitch_amount * 0.8f;
        grain_timer_ = static_cast<int>(
            static_cast<float>(p.base_interval) * (1.f + jitter));
        if (grain_timer_ < 32) grain_timer_ = 32;
      }
    }

    // Sum all active voices
    float wet = 0.f;
    for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
      wet += grain_voices_[v].Process(ring_);
    }
    return wet;
  }

 private:
  RingBuffer ring_;
  float* slab_ = nullptr;   // ring storage (for Kill)
  size_t slab_n_ = 0;
  GrainVoice grain_voices_[NUM_GRAIN_VOICES];
  int    grain_next_voice_ = 0;
  int    grain_timer_ = 0;             // samples until next event
  int    grain_burst_left_ = 0;        // grains remaining in current burst
  int    harmony_hold_counter_ = 0;    // grains remaining before re-rolling pitch
  float  harmony_cached_ratio_ = 1.f;  // cached pitch ratio for held harmony
  int    harmony_cached_k1_semi_ = 999; // cached K1 target; force re-roll on change
};
