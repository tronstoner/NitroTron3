#pragma once

#include <cmath>
#include "constants.h"

// YIN pitch tracker. Voiced per instrument via the TRACK_* profile block in
// constants.h — BASS (default): 4x decimation → 12 kHz, range ≈30–500 Hz;
// GUITAR: 2x decimation → 24 kHz, range ≈67–1043 Hz, parabolic refine on.
// Split into Feed() (audio callback, cheap) and Update() (main loop, heavy).
// Input is decimated TRACK_DEC× with proper anti-aliasing.
// YIN difference function with cumulative mean normalization.
// Two outputs from the same YIN result: GetMidiNote() (quantized to the nearest
// semitone, for octave-locked mode) and GetMidiNoteContinuous() (unrounded,
// slew-limited — follows bends/microtonal, for direct tracking + the synth).
class PitchTracker {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    dec_sr_ = sr_ / TRACK_DEC;
    hp_coeff_ = 1.f - expf(-6.2831853f * 25.f / sr_);
    hp_[0] = hp_[1] = 0.f;
    // Fundamental-isolation LP (TRACK_AA_LP_HZ) — rejects harmonics that cause
    // octave-up errors in YIN. Also anti-aliases the TRACK_DEC decimation.
    aa_coeff_ = 1.f - expf(-6.2831853f * TRACK_AA_LP_HZ / sr_);
    for (int i = 0; i < 4; i++) aa_[i] = 0.f;
    for (int i = 0; i < BUF_SIZE; i++) buf_[i] = 0.f;
    write_pos_    = 0;
    dec_accum_    = 0.f;
    dec_count_    = 0;
    hop_count_    = 0;
    needs_update_ = false;
    midi_note_    = 36.f;
    cont_midi_    = 36.f;
  }

  // Called every sample in the audio callback. Cheap — just filters and buffers.
  void Feed(float in, float env_level) {
    if (env_level < 0.001f) return;

    // HP filter (DC blocking at 25 Hz)
    hp_[0] += hp_coeff_ * (in - hp_[0]);
    hp_[1] += hp_coeff_ * (hp_[0] - hp_[1]);
    float hpf = in - hp_[1];

    // 4-pole anti-alias / fundamental-isolation LP (TRACK_AA_LP_HZ)
    aa_[0] += aa_coeff_ * (hpf - aa_[0]);
    aa_[1] += aa_coeff_ * (aa_[0] - aa_[1]);
    aa_[2] += aa_coeff_ * (aa_[1] - aa_[2]);
    aa_[3] += aa_coeff_ * (aa_[2] - aa_[3]);

    // Decimate TRACK_DEC×
    dec_accum_ += aa_[3];
    dec_count_++;
    if (dec_count_ < TRACK_DEC) return;

    float decimated = dec_accum_ * (1.f / TRACK_DEC);
    dec_accum_ = 0.f;
    dec_count_ = 0;

    // Write to ring buffer
    buf_[write_pos_] = decimated;
    write_pos_ = (write_pos_ + 1) & BUF_MASK;

    // Flag YIN to run in main loop
    hop_count_++;
    if (hop_count_ >= TRACK_HOP) {
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

  // Quantized to nearest semitone — octave-locked mode.
  float GetMidiNote() const { return midi_note_; }
  // Unrounded, slew-limited — follows bends/slides/vibrato and any tuning.
  float GetMidiNoteContinuous() const { return cont_midi_; }

 private:
  // Decimation, window, lag range, hop, and threshold come from the TRACK_*
  // instrument profile in constants.h. The ring buffer must hold the deepest
  // lookback RunYin makes: Buf(W - 1 + MAX_LAG).
  static constexpr int BUF_SIZE = 1024;
  static constexpr int BUF_MASK = BUF_SIZE - 1;
  static_assert(TRACK_WINDOW + TRACK_MAX_LAG <= BUF_SIZE,
                "YIN lookback exceeds ring buffer");

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
  float midi_note_     = 36.f;   // quantized (octave-locked)
  float cont_midi_     = 36.f;   // continuous, slew-limited (direct track / synth)

  float Buf(int offset) const {
    return buf_[(write_pos_ - 1 - offset + BUF_SIZE) & BUF_MASK];
  }

  void RunYin() {
    float cum_sum   = 0.f;
    bool  was_below = false;
    int   best_tau  = -1;
    float best_dp   = 1.f;
    // Neighbour d' values around the minimum, for the parabolic refine.
    // Bookkeeping is gated on TRACK_PARABOLIC (constexpr) — folds away
    // entirely in the BASS build, whose output is byte-for-byte today's.
    float prev_dp      = 1.f;   // d'(tau - 1)
    float best_dp_prev = 1.f;   // d'(best_tau - 1)
    float best_dp_next = -1.f;  // d'(best_tau + 1); < 0 = not yet captured

    for (int tau = 1; tau <= TRACK_MAX_LAG; tau++) {
      float d = 0.f;
      for (int j = 0; j < TRACK_WINDOW; j++) {
        float diff = Buf(j) - Buf(j + tau);
        d += diff * diff;
      }

      cum_sum += d;
      float d_prime = (cum_sum > 0.f)
                        ? (d * static_cast<float>(tau) / cum_sum)
                        : 1.f;

      if (TRACK_PARABOLIC && best_tau == tau - 1) best_dp_next = d_prime;

      if (tau >= TRACK_MIN_LAG) {
        if (d_prime < TRACK_THRESHOLD) {
          was_below = true;
          if (d_prime < best_dp) {
            best_dp = d_prime;
            best_tau = tau;
            if (TRACK_PARABOLIC) {
              best_dp_prev = prev_dp;
              best_dp_next = -1.f;  // re-arm capture for the new minimum
            }
          }
        } else if (was_below) {
          break;
        }
      }
      prev_dp = d_prime;
    }

    if (best_tau > 0) {
      float tau_est = static_cast<float>(best_tau);
      if (TRACK_PARABOLIC && best_dp_next >= 0.f) {
        // Parabolic interpolation through d'(best_tau ± 1) → sub-lag period.
        // Refines only the already-chosen minimum — the tau search and the
        // first-dip rule (the lowest-fundamental lock) are untouched.
        float denom = best_dp_prev - 2.f * best_dp + best_dp_next;
        if (denom > 1e-9f) {
          float offset = 0.5f * (best_dp_prev - best_dp_next) / denom;
          if (offset >  0.5f) offset =  0.5f;
          if (offset < -0.5f) offset = -0.5f;
          tau_est += offset;
        }
      }
      float freq = dec_sr_ / tau_est;
      float midi = 69.f + 12.f * log2f(freq / 440.f);
      midi_note_ = roundf(midi);   // quantized path — unchanged
      cont_midi_ = midi;           // continuous path — raw, unrounded, no smoothing
    }
  }
};
