#pragma once

#include <cmath>

// Polyphase L/M rational resampler.
//
// Converts a stream of input samples at rate fs_in to fs_out where
// fs_out / fs_in = L / M. Conceptually: insert (L-1) zeros between input
// samples, lowpass-filter the upsampled stream, then keep every Mth sample.
// The polyphase decomposition avoids the zero-stuffing: each output sample
// only touches one of L sub-filters, so the multiply count per output is
// N taps (not L*N).
//
// Usage:
//   Resampler<2, 3, 16> down;          // 48 kHz -> 32 kHz, mono
//   down.Init(15000.0f, 96000.0f);     // prototype lowpass design rate
//   size_t n_out = down.Process(in_48, 48, out_32);  // 48 in -> 32 out
//
//   Resampler<3, 2, 16> up;            // 32 kHz -> 48 kHz, mono
//   up.Init(15000.0f, 96000.0f);
//   size_t n_out = up.Process(in_32, 32, out_48);    // 32 in -> 48 out
//
// Stereo upsampling: instantiate two upsamplers (one per channel) and run
// them with the same coefficients but independent history.
//
// The prototype filter is designed at fs_proto = L * fs_in = M * fs_out
// (= 96 kHz for both 48->32 and 32->48). Cutoff well below min(fs_in,fs_out)/2.

template <int L, int M, int N>
class Resampler {
 public:
  void Init(float cutoff_hz, float fs_proto_hz) {
    constexpr int N_total = L * N;
    constexpr float center = (N_total - 1) * 0.5f;
    const float two_fc_over_fs = 2.0f * cutoff_hz / fs_proto_hz;
    constexpr float kPi = 3.14159265358979323846f;

    // Build prototype filter (sinc * Hamming), then split into L polyphase
    // branches with branch index = n % L, tap index = n / L.
    float proto[N_total];
    float sum = 0.0f;
    for (int n = 0; n < N_total; ++n) {
      const float x = (static_cast<float>(n) - center) * two_fc_over_fs;
      const float sinc = (fabsf(x) < 1e-9f)
                             ? 1.0f
                             : sinf(kPi * x) / (kPi * x);
      const float w = 0.54f - 0.46f * cosf(2.0f * kPi * static_cast<float>(n) /
                                           static_cast<float>(N_total - 1));
      proto[n] = sinc * w;
      sum += proto[n];
    }

    // Normalize: DC gain = L. After polyphase decim by M, output-sequence
    // DC gain = L/L * (1/M-ratio handled by sample-rate change) = 1.
    const float scale = static_cast<float>(L) / sum;
    for (int n = 0; n < N_total; ++n) {
      const int branch = n % L;
      const int tap = n / L;
      h_[branch * N + tap] = proto[n] * scale;
    }

    for (int i = 0; i < N; ++i) hist_[i] = 0.0f;
    phase_ = 0;
  }

  // Push one input sample. Writes up to L output samples into `out` and
  // returns the count (0 to L). `out` must have capacity >= L.
  inline int Tick(float x, float* out) {
    // Shift history (history[0] = newest, history[N-1] = oldest).
    for (int i = N - 1; i > 0; --i) hist_[i] = hist_[i - 1];
    hist_[0] = x;

    int n_out = 0;
    while (phase_ < L) {
      const float* hb = &h_[phase_ * N];
      float acc = 0.0f;
      for (int n = 0; n < N; ++n) acc += hb[n] * hist_[n];
      out[n_out++] = acc;
      phase_ += M;
    }
    phase_ -= L;
    return n_out;
  }

  // Block-based convenience. `out` must have capacity >= in_n * L / M + L
  // (a safe upper bound is in_n * L). Returns the number of output samples
  // written.
  size_t Process(const float* in, size_t in_n, float* out) {
    size_t out_n = 0;
    float tmp[L];
    for (size_t i = 0; i < in_n; ++i) {
      const int n = Tick(in[i], tmp);
      for (int j = 0; j < n; ++j) out[out_n++] = tmp[j];
    }
    return out_n;
  }

  void Reset() {
    for (int i = 0; i < N; ++i) hist_[i] = 0.0f;
    phase_ = 0;
  }

 private:
  float h_[L * N];   // polyphase coefficients, branch-major
  float hist_[N];    // input history, hist_[0] = newest
  int phase_;        // 0..L-1 emission phase across input samples
};
