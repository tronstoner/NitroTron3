#pragma once

#include <cmath>

// YIN pitch tracker for bass guitar.
// Input is decimated 4x (48kHz → 12kHz) for efficiency.
// YIN difference function with cumulative mean normalization finds
// the fundamental period even when harmonics are stronger.
// Output quantized to nearest MIDI semitone.
// Holds last detected note on silence.
class PitchTracker {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    dec_sr_ = sr_ / DEC;
    hp_coeff_ = 1.f - expf(-6.2831853f * 25.f / sr_);
    hp_[0] = hp_[1] = 0.f;
    for (int i = 0; i < BUF_SIZE; i++) buf_[i] = 0.f;
    write_pos_ = 0;
    dec_accum_ = 0.f;
    dec_count_ = 0;
    hop_count_ = 0;
    midi_note_ = 36.f;
  }

  void Process(float in, float env_level) {
    // Skip tracking during silence
    if (env_level < 0.001f) return;

    // HP filter (DC blocking at 25 Hz)
    hp_[0] += hp_coeff_ * (in - hp_[0]);
    hp_[1] += hp_coeff_ * (hp_[0] - hp_[1]);
    float hpf = in - hp_[1];

    // Decimate 4x (simple averaging)
    dec_accum_ += hpf;
    dec_count_++;
    if (dec_count_ < DEC) return;

    float decimated = dec_accum_ * (1.f / DEC);
    dec_accum_ = 0.f;
    dec_count_ = 0;

    // Write to ring buffer
    buf_[write_pos_] = decimated;
    write_pos_ = (write_pos_ + 1) & BUF_MASK;

    // Run YIN every HOP decimated samples (~5.3 ms)
    hop_count_++;
    if (hop_count_ < HOP) return;
    hop_count_ = 0;
    RunYin();
  }

  float GetMidiNote() const { return midi_note_; }

 private:
  static constexpr int DEC      = 4;
  static constexpr int BUF_SIZE = 1024;  // at 12kHz = ~85ms
  static constexpr int BUF_MASK = BUF_SIZE - 1;
  static constexpr int W        = 300;   // analysis window (~25ms at 12kHz)
  static constexpr int MIN_LAG  = 24;    // ~500 Hz at 12kHz
  static constexpr int MAX_LAG  = 400;   // ~30 Hz at 12kHz
  static constexpr int HOP      = 64;    // hop size (~5.3ms at 12kHz)
  static constexpr float THRESHOLD = 0.15f;

  float buf_[BUF_SIZE] = {};
  int   write_pos_     = 0;
  float dec_accum_     = 0.f;
  int   dec_count_     = 0;
  int   hop_count_     = 0;
  float sr_            = 48000.f;
  float dec_sr_        = 12000.f;
  float hp_coeff_      = 0.f;
  float hp_[2]         = {};
  float midi_note_     = 36.f;

  // Read from ring buffer. offset=0 = newest sample.
  float Buf(int offset) const {
    return buf_[(write_pos_ - 1 - offset + BUF_SIZE) & BUF_MASK];
  }

  void RunYin() {
    float cum_sum  = 0.f;
    bool  was_below = false;
    int   best_tau  = -1;
    float best_dp   = 1.f;

    for (int tau = 1; tau <= MAX_LAG; tau++) {
      // Difference function: d(tau) = sum((x[j] - x[j+tau])^2)
      float d = 0.f;
      for (int j = 0; j < W; j++) {
        float diff = Buf(j) - Buf(j + tau);
        d += diff * diff;
      }

      // Cumulative mean normalization
      cum_sum += d;
      float d_prime = (cum_sum > 0.f)
                        ? (d * static_cast<float>(tau) / cum_sum)
                        : 1.f;

      if (tau >= MIN_LAG) {
        if (d_prime < THRESHOLD) {
          was_below = true;
          if (d_prime < best_dp) {
            best_dp = d_prime;
            best_tau = tau;
          }
        } else if (was_below) {
          // Passed the dip — found the fundamental, stop
          break;
        }
      }
    }

    if (best_tau > 0) {
      float freq = dec_sr_ / static_cast<float>(best_tau);
      float midi = 69.f + 12.f * log2f(freq / 440.f);
      midi_note_ = roundf(midi);
    }
  }
};
