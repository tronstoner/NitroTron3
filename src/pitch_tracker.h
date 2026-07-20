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

#if NT3_TRACK_POLY
  // Multi-dip poly candidates: raw deduped dips from the last YIN run,
  // deepest-first. These are per-hop candidates, NOT stable voices — the
  // voice matcher turns them into voices (attack/release, hop-to-hop
  // continuity). Count is 0 when nothing tracks. i < GetPolyCount().
  int   GetPolyCount() const { return poly_count_; }
  float GetPolyMidi(int i) const { return poly_midi_[i]; }      // unrounded MIDI
  float GetPolySalience(int i) const { return poly_sal_[i]; }   // 1 - d' (0..1)
#endif

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
#if NT3_TRACK_POLY
  int   poly_count_    = 0;      // accepted multi-dip candidates this hop
  float poly_midi_[TRACK_POLY_VOICES] = {};
  float poly_sal_[TRACK_POLY_VOICES]  = {};
#endif

  float Buf(int offset) const {
    return buf_[(write_pos_ - 1 - offset + BUF_SIZE) & BUF_MASK];
  }

  void RunYin() {
    // Preprocessor dispatch — the OFF build never sees the poly variant (or
    // the poly members above), keeping it byte-identical to the mono-only one.
#if NT3_TRACK_POLY
    RunYinPoly();
#else
    RunYinMono();
#endif
  }

  // The shipping first-dip mono tracker — body untouched.
  void RunYinMono() {
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

#if NT3_TRACK_POLY
  // Full-scan variant (docs/MULTI_DIP_TRACKING.md). Mono
  // result is identical to RunYinMono — the first-dip selection is latched at
  // the lag where the mono loop would have exited — but the scan continues to
  // the last lag, collecting every local minimum of d' below TRACK_POLY_DIP_MAX
  // as a poly candidate (insert-sorted deepest-first, then deduped).
  void RunYinPoly() {
    float cum_sum   = 0.f;
    bool  was_below = false;
    bool  mono_done = false;    // first-dip region ended, mono result latched
    int   best_tau  = -1;
    float best_dp   = 1.f;
    float prev_dp      = 1.f;   // d'(tau - 1)
    float pp_dp        = 1.f;   // d'(tau - 2), for local-min detection
    float best_dp_prev = 1.f;   // d'(best_tau - 1)
    float best_dp_next = -1.f;  // d'(best_tau + 1); < 0 = not yet captured

    static constexpr int kMaxCand = 16;  // dips below DIP_MAX are few
    float cand_tau[kMaxCand];
    float cand_dp[kMaxCand];
    int   n_cand = 0;

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

      // Was tau-1 a local minimum worth keeping as a poly candidate? The
      // parabolic refine is always on here — candidates are new output, there
      // is no byte-identity to preserve (unlike the mono TRACK_PARABOLIC).
      if (tau - 1 >= TRACK_MIN_LAG && prev_dp < TRACK_POLY_DIP_MAX
          && prev_dp < pp_dp && prev_dp <= d_prime) {
        float tau_c = static_cast<float>(tau - 1);
        float denom = pp_dp - 2.f * prev_dp + d_prime;
        if (denom > 1e-9f) {
          float offset = 0.5f * (pp_dp - d_prime) / denom;
          if (offset >  0.5f) offset =  0.5f;
          if (offset < -0.5f) offset = -0.5f;
          tau_c += offset;
        }
        if (n_cand < kMaxCand || prev_dp < cand_dp[kMaxCand - 1]) {
          int pos = (n_cand < kMaxCand) ? n_cand++ : kMaxCand - 1;
          while (pos > 0 && cand_dp[pos - 1] > prev_dp) {
            cand_dp[pos]  = cand_dp[pos - 1];
            cand_tau[pos] = cand_tau[pos - 1];
            pos--;
          }
          cand_dp[pos]  = prev_dp;
          cand_tau[pos] = tau_c;
        }
      }

      if (tau >= TRACK_MIN_LAG && !mono_done) {
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
          mono_done = true;  // where RunYinMono exits — latch, keep scanning
        }
      }
      pp_dp   = prev_dp;
      prev_dp = d_prime;
    }

    // Dedupe deepest-first candidates into <= TRACK_POLY_VOICES accepted dips.
    // Rejections: near-duplicate lags (DUP_TOL) and — gated on
    // TRACK_POLY_HARM_DEDUPE — integer multiples of an accepted lag
    // (sub-octave aliases of a stronger dip).
    poly_count_ = 0;
    float acc_tau[TRACK_POLY_VOICES];
    for (int c = 0; c < n_cand && poly_count_ < TRACK_POLY_VOICES; c++) {
      bool reject = false;
      for (int a = 0; a < poly_count_; a++) {
        float r  = cand_tau[c] / acc_tau[a];
        float rs = (r < 1.f) ? 1.f / r : r;
        if (rs - 1.f < TRACK_POLY_DUP_TOL) { reject = true; break; }
        if (TRACK_POLY_HARM_DEDUPE) {
          float k = roundf(r);
          if (k >= 2.f && fabsf(r - k) < TRACK_POLY_HARM_TOL * k) {
            reject = true;
            break;
          }
        }
      }
      if (reject) continue;
      acc_tau[poly_count_] = cand_tau[c];
      float pfreq = dec_sr_ / cand_tau[c];
      poly_midi_[poly_count_] = 69.f + 12.f * log2f(pfreq / 440.f);
      poly_sal_[poly_count_]  = 1.f - cand_dp[c];
      poly_count_++;
    }

    // Mono outputs — same conversion as RunYinMono.
    if (best_tau > 0) {
      float tau_est = static_cast<float>(best_tau);
      if (TRACK_PARABOLIC && best_dp_next >= 0.f) {
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
      midi_note_ = roundf(midi);
      cont_midi_ = midi;
    }
  }
#endif  // NT3_TRACK_POLY
};
