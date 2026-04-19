#pragma once

#include <cmath>

// YIN pitch tracker for bass guitar.
// Split into Feed() (audio callback, cheap) and Update() (main loop, heavy).
// Input is decimated 4x (48kHz → 12kHz) with proper anti-aliasing.
// YIN difference function with cumulative mean normalization.
// Output quantized to nearest MIDI semitone.
class PitchTracker {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    dec_sr_ = sr_ / DEC;
    hp_coeff_ = 1.f - expf(-6.2831853f * 25.f / sr_);
    hp_[0] = hp_[1] = 0.f;
    // LP at 400 Hz — isolates fundamental, rejects harmonics that cause
    // octave-up errors in YIN. Also serves as anti-alias for 4x decimation.
    aa_coeff_ = 1.f - expf(-6.2831853f * 400.f / sr_);
    for (int i = 0; i < 4; i++) aa_[i] = 0.f;
    for (int i = 0; i < BUF_SIZE; i++) buf_[i] = 0.f;
    write_pos_    = 0;
    dec_accum_    = 0.f;
    dec_count_    = 0;
    hop_count_    = 0;
    needs_update_ = false;
    midi_note_    = 36.f;
  }

  // Called every sample in the audio callback. Cheap — just filters and buffers.
  void Feed(float in, float env_level) {
    if (env_level < 0.001f) return;

    // HP filter (DC blocking at 25 Hz)
    hp_[0] += hp_coeff_ * (in - hp_[0]);
    hp_[1] += hp_coeff_ * (hp_[0] - hp_[1]);
    float hpf = in - hp_[1];

    // 4-pole anti-aliasing LP at 1.5 kHz before decimation
    aa_[0] += aa_coeff_ * (hpf - aa_[0]);
    aa_[1] += aa_coeff_ * (aa_[0] - aa_[1]);
    aa_[2] += aa_coeff_ * (aa_[1] - aa_[2]);
    aa_[3] += aa_coeff_ * (aa_[2] - aa_[3]);

    // Decimate 4x
    dec_accum_ += aa_[3];
    dec_count_++;
    if (dec_count_ < DEC) return;

    float decimated = dec_accum_ * (1.f / DEC);
    dec_accum_ = 0.f;
    dec_count_ = 0;

    // Write to ring buffer
    buf_[write_pos_] = decimated;
    write_pos_ = (write_pos_ + 1) & BUF_MASK;

    // Flag YIN to run in main loop
    hop_count_++;
    if (hop_count_ >= HOP) {
      hop_count_ = 0;
      needs_update_ = true;
    }
  }

  // Called in the main loop. Runs YIN when ready — heavy but not time-critical.
  void Update() {
    if (!needs_update_) return;
    needs_update_ = false;
    RunYin();
  }

  float GetMidiNote() const { return midi_note_; }

 private:
  static constexpr int DEC      = 4;
  static constexpr int BUF_SIZE = 1024;
  static constexpr int BUF_MASK = BUF_SIZE - 1;
  static constexpr int W        = 400;   // analysis window (~33ms at 12kHz)
  static constexpr int MIN_LAG  = 24;    // ~500 Hz at 12kHz
  static constexpr int MAX_LAG  = 400;   // ~30 Hz at 12kHz
  static constexpr int HOP      = 64;    // hop size (~5.3ms at 12kHz)
  static constexpr float THRESHOLD = 0.15f;

  float buf_[BUF_SIZE] = {};
  int   write_pos_     = 0;
  float dec_accum_     = 0.f;
  int   dec_count_     = 0;
  int   hop_count_     = 0;
  volatile bool needs_update_ = false;
  float sr_            = 48000.f;
  float dec_sr_        = 12000.f;
  float hp_coeff_      = 0.f;
  float hp_[2]         = {};
  float aa_coeff_      = 0.f;
  float aa_[4]         = {};
  float midi_note_     = 36.f;

  float Buf(int offset) const {
    return buf_[(write_pos_ - 1 - offset + BUF_SIZE) & BUF_MASK];
  }

  void RunYin() {
    float cum_sum   = 0.f;
    bool  was_below = false;
    int   best_tau  = -1;
    float best_dp   = 1.f;

    for (int tau = 1; tau <= MAX_LAG; tau++) {
      float d = 0.f;
      for (int j = 0; j < W; j++) {
        float diff = Buf(j) - Buf(j + tau);
        d += diff * diff;
      }

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
