#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Mode C POG simulation — ERB-PS2 quadrature-filterbank octave engine.
//
// Adapted from schult/terrarium-poly-octave (MIT License, Copyright (c) 2024
// Steven Schulteis), the closest public analysis-based recreation of the EHX
// POG. Changes for NitroTron3: no cycfi::q / gcem dependencies, hand-rolled
// complex arithmetic (std::complex<float> products can lower to __mulsc3
// libcalls without -fcx-limited-range), the down-2-octaves voice replaced by
// an up-2-octaves voice (our stack is -1/+1/+2), fixed arrays instead of
// std::vector, and per-voice block outputs so the caller applies its own
// staged gains.
//
// Method (docs/MODE_C_POG_DISCOVERY.md): decimate 48 kHz -> 8 kHz (the wet
// path is band-limited by design — part of the POG sound), run 80 complex
// (analytic) bandpass biquads with quasi-log-spaced centers ~60 Hz–1.7 kHz,
// and phase-scale each band's analytic signal: out = in * (in/|in|)^(g-1)
// (g=2 doubles the phase = octave up; g=1/2 halves it = octave down, with
// sign bookkeeping across phase wraps; +2 oct = the g=2 step applied twice).
// Shifted bands are summed (imperfect reconstruction is part of the
// character) and each voice is interpolated back to 48 kHz.
//
// References:
//  - E. Thuillier, "Real-Time Polyphonic Octave Doubling for the Guitar"
//    (ERB-PS2, developed after analysing the POG) —
//    https://core.ac.uk/download/pdf/80719011.pdf
//  - A. J. Noga, "Complex Band-Pass Filters for Analytic Signal Generation
//    and Their Application" — https://apps.dtic.mil/sti/tr/pdf/ADA395963.pdf
//  - R. Bristow-Johnson, "Audio EQ Cookbook" (LPF prototype)

namespace polyoct {

// https://en.wikipedia.org/wiki/Fast_inverse_square_root (Schulteis' variant)
static inline float FastInvSqrt(float x) {
  uint32_t xi;
  memcpy(&xi, &x, sizeof(xi));
  xi = 0x5F1FFFF9u - (xi >> 1);
  float y;
  memcpy(&y, &xi, sizeof(y));
  return y * (0.703952253f * (2.38924456f - (x * y * y)));
}
static inline float FastSqrt(float x) { return FastInvSqrt(x) * x; }

// Ring buffer with cycfi::q semantics: push() overwrites the oldest slot;
// operator[](i) indexes oldest -> newest as i grows (i = N-1 is the newest).
// The FIR tap tables below were written against exactly this ordering.
template <size_t N>
class RingBuf {
 public:
  static_assert((N & (N - 1)) == 0, "power of two");
  inline void push(float v) {
    data_[pos_ & (N - 1)] = v;
    pos_++;
  }
  inline float operator[](size_t i) const {
    return data_[(pos_ + i) & (N - 1)];
  }

 private:
  float    data_[N] = {};
  uint32_t pos_     = 0;
};

// 48 kHz -> 8 kHz two-stage FIR decimator (coefficients verbatim from
// terrarium-poly-octave Multirate.h).
class Decimator {
 public:
  // Consumes exactly 6 input samples, returns 1 output sample.
  inline float operator()(const float* s) {
    buffer1.push(s[0]);
    buffer1.push(s[1]);
    buffer1.push(s[2]);
    buffer2.push(filter1());

    buffer1.push(s[3]);
    buffer1.push(s[4]);
    buffer1.push(s[5]);
    buffer2.push(filter1());

    return filter2();
  }

 private:
  float filter1() const {
    // 48000 Hz sample rate, 0-1800 Hz pass band (3 dB ripple),
    // 8000-24000 Hz stop band (-80 dB)
    return
        0.000066177472224418f * (buffer1[offset1+0] + buffer1[offset1+20]) +
        0.0009613901552378511f * (buffer1[offset1+1] + buffer1[offset1+19]) +
        0.003835090815380887f * (buffer1[offset1+2] + buffer1[offset1+18]) +
        0.010496532623165526f * (buffer1[offset1+3] + buffer1[offset1+17]) +
        0.02272703591356282f * (buffer1[offset1+4] + buffer1[offset1+16]) +
        0.041464390530886956f * (buffer1[offset1+5] + buffer1[offset1+15]) +
        0.06591039391505207f * (buffer1[offset1+6] + buffer1[offset1+14]) +
        0.09309984953947406f * (buffer1[offset1+7] + buffer1[offset1+13]) +
        0.11829177835273737f * (buffer1[offset1+8] + buffer1[offset1+12]) +
        0.13620590247679107f * (buffer1[offset1+9] + buffer1[offset1+11]) +
        0.14270010010002276f * buffer1[offset1+10];
  }

  float filter2() const {
    // Half-band filter, 16000 Hz sample rate, 0-1800 Hz pass band
    return
        -0.00299995f * (buffer2[offset2+0] + buffer2[offset2+14]) +
        0.01858487f * (buffer2[offset2+2] + buffer2[offset2+12]) +
        -0.06984829f * (buffer2[offset2+4] + buffer2[offset2+10]) +
        0.30421664f * (buffer2[offset2+6] + buffer2[offset2+8]) +
        0.5f * buffer2[offset2+7];
  }

  static constexpr size_t bsize1 = 32, fsize1 = 21, offset1 = bsize1 - fsize1;
  static constexpr size_t bsize2 = 16, fsize2 = 15, offset2 = bsize2 - fsize2;

  RingBuf<bsize1> buffer1;
  RingBuf<bsize2> buffer2;
};

// 8 kHz -> 48 kHz two-stage FIR interpolator (coefficients verbatim from
// terrarium-poly-octave Multirate.h; passband gains 2 and 3 make up the
// zero-stuffing energy loss).
class Interpolator {
 public:
  // Consumes 1 input sample, produces exactly 6 output samples into out[].
  inline void operator()(float s, float* out) {
    buffer1.push(s);

    buffer2.push(filter1a());
    out[0] = filter2a();
    out[1] = filter2b();
    out[2] = filter2c();

    buffer2.push(filter1b());
    out[3] = filter2a();
    out[4] = filter2b();
    out[5] = filter2c();
  }

 private:
  // Filter 1: 16000 Hz sample rate, 0-3600 Hz pass band (3 dB ripple),
  // 4400-8000 Hz stop band (-80 dB), gain=2 in passband
  float filter1a() const {
    return
        -0.0028536199247471473f * (buffer1[offset1+0] + buffer1[offset1+24]) +
        -0.040326725115203695f * (buffer1[offset1+1] + buffer1[offset1+23]) +
        -0.036134596458820015f * (buffer1[offset1+2] + buffer1[offset1+22]) +
        0.033522051189265496f * (buffer1[offset1+3] + buffer1[offset1+21]) +
        -0.031442224275585025f * (buffer1[offset1+4] + buffer1[offset1+20]) +
        0.03258337681750486f * (buffer1[offset1+5] + buffer1[offset1+19]) +
        -0.03538414864961937f * (buffer1[offset1+6] + buffer1[offset1+18]) +
        0.038811868988079715f * (buffer1[offset1+7] + buffer1[offset1+17]) +
        -0.042204493894155204f * (buffer1[offset1+8] + buffer1[offset1+16]) +
        0.045128824129776035f * (buffer1[offset1+9] + buffer1[offset1+15]) +
        -0.04736995557907843f * (buffer1[offset1+10] + buffer1[offset1+14]) +
        0.048831901671617876f * (buffer1[offset1+11] + buffer1[offset1+13]) +
        0.9507771467941135f * buffer1[offset1+12];
  }

  float filter1b() const {
    return
        -0.015961858776449508f * (buffer1[offset1+0] + buffer1[offset1+23]) +
        -0.056128740058266235f * (buffer1[offset1+1] + buffer1[offset1+22]) +
        0.011026026040094625f * (buffer1[offset1+2] + buffer1[offset1+21]) +
        0.003198795994721635f * (buffer1[offset1+3] + buffer1[offset1+20]) +
        -0.01108582057161854f * (buffer1[offset1+4] + buffer1[offset1+19]) +
        0.01951384497860086f * (buffer1[offset1+5] + buffer1[offset1+18]) +
        -0.030860282826182514f * (buffer1[offset1+6] + buffer1[offset1+17]) +
        0.04707993944078406f * (buffer1[offset1+7] + buffer1[offset1+16]) +
        -0.07155908583004919f * (buffer1[offset1+8] + buffer1[offset1+15]) +
        0.1129220770668398f * (buffer1[offset1+9] + buffer1[offset1+14]) +
        -0.2033122562119347f * (buffer1[offset1+10] + buffer1[offset1+13]) +
        0.6336728217960803f * (buffer1[offset1+11] + buffer1[offset1+12]);
  }

  // Filter 2: 48000 Hz sample rate, 0-3600 Hz pass band (3 dB ripple),
  // 8000-24000 Hz stop band (-80 dB), gain=3 in passband
  float filter2a() const {
    return
        0.00036440608905813593f * buffer2[offset2+0] +
        0.0005821260464558225f * buffer2[offset2+1] +
        -0.043244023722481956f * buffer2[offset2+2] +
        -0.10310036386076359f * buffer2[offset2+3] +
        0.13604229993913602f * buffer2[offset2+4] +
        0.5503466630244301f * buffer2[offset2+5] +
        0.4407091552750118f * buffer2[offset2+6] +
        0.009420000864297772f * buffer2[offset2+7] +
        -0.09801301258361905f * buffer2[offset2+8] +
        -0.019627176246818184f * buffer2[offset2+9] +
        0.001762424830497545f * buffer2[offset2+10];
  }

  float filter2b() const {
    return
        0.001112114188613258f * (buffer2[offset2+0] + buffer2[offset2+10]) +
        -0.005449383064836152f * (buffer2[offset2+1] + buffer2[offset2+9]) +
        -0.07276547446584428f * (buffer2[offset2+2] + buffer2[offset2+8]) +
        -0.0709695783332148f * (buffer2[offset2+3] + buffer2[offset2+7]) +
        0.2904591843823435f * (buffer2[offset2+4] + buffer2[offset2+6]) +
        0.590541634315722f * buffer2[offset2+5];
  }

  float filter2c() const {
    return
        0.001762424830497545f * buffer2[offset2+0] +
        -0.019627176246818184f * buffer2[offset2+1] +
        -0.09801301258361905f * buffer2[offset2+2] +
        0.009420000864297772f * buffer2[offset2+3] +
        0.4407091552750118f * buffer2[offset2+4] +
        0.5503466630244301f * buffer2[offset2+5] +
        0.13604229993913602f * buffer2[offset2+6] +
        -0.10310036386076359f * buffer2[offset2+7] +
        -0.043244023722481956f * buffer2[offset2+8] +
        0.0005821260464558225f * buffer2[offset2+9] +
        0.00036440608905813593f * buffer2[offset2+10];
  }

  static constexpr size_t bsize1 = 32, fsize1 = 25, offset1 = bsize1 - fsize1;
  static constexpr size_t bsize2 = 16, fsize2 = 11, offset2 = bsize2 - fsize2;

  RingBuf<bsize1> buffer1;
  RingBuf<bsize2> buffer2;
};

// One analytic (complex) bandpass filter + per-band phase-scaled octave
// voices. Prototype LPF from the Audio EQ Cookbook, rotated into a complex
// bandpass as in Noga sec. 3.1; phase scaling per Thuillier.
class BandShifter {
 public:
  void Init(float center, float sample_rate, float bw) {
    const double pi     = 3.14159265358979323846;
    const double w0     = pi * (double)bw / (double)sample_rate;
    const double cos_w0 = cos(w0);
    const double sin_w0 = sin(w0);
    const double sqrt2  = 1.41421356237309515;
    const double a0     = 1.0 + sqrt2 * sin_w0 / 2.0;
    const double g      = (1.0 - cos_w0) / (2.0 * a0);

    const double w1     = 2.0 * pi * (double)center / (double)sample_rate;
    // e1 = e^(j*w1), e2 = e^(j*2*w1)
    const double e1r = cos(w1), e1i = sin(w1);
    const double e2r = cos(2.0 * w1), e2i = sin(2.0 * w1);

    d0_  = (float)g;
    d1r_ = (float)(e1r * 2.0 * g);
    d1i_ = (float)(e1i * 2.0 * g);
    d2r_ = (float)(e2r * g);
    d2i_ = (float)(e2i * g);
    const double c1 = -2.0 * cos_w0 / a0;
    c1r_ = (float)(e1r * c1);
    c1i_ = (float)(e1i * c1);
    const double c2 = (1.0 - sqrt2 * sin_w0 / 2.0) / a0;
    c2r_ = (float)(e2r * c2);
    c2i_ = (float)(e2i * c2);
  }

  inline void Update(float x) {
    // Complex one-input biquad (transposed direct form II):
    //   y  = s2 + d0*x
    //   s2 = s1 + d1*x - c1*y
    //   s1 =      d2*x - c2*y
    const float pyi = yi_;
    yr_ = s2r_ + d0_ * x;
    yi_ = s2i_;
    s2r_ = s1r_ + d1r_ * x - (c1r_ * yr_ - c1i_ * yi_);
    s2i_ = s1i_ + d1i_ * x - (c1r_ * yi_ + c1i_ * yr_);
    s1r_ = d2r_ * x - (c2r_ * yr_ - c2i_ * yi_);
    s1i_ = d2i_ * x - (c2r_ * yi_ + c2i_ * yr_);

    // Octave-down sign bookkeeping: the half-phase signal flips sign each
    // time the analytic signal's phase crosses pi (Re < 0, Im sign change).
    if ((yr_ < 0.f) && (std::signbit(yi_) != std::signbit(pyi)))
      down1_sign_ = -down1_sign_;

    const float a   = yr_;
    const float b   = yi_;
    const float n   = a * a + b * b;
    const float inv = FastInvSqrt(n);

    // Up 1 octave: y^2 / |y| — phase doubled, amplitude preserved.
    const float p   = a * a - b * b;
    const float q   = 2.f * a * b;
    const float u1r = p * inv;
    const float u1i = q * inv;
    up1_ = u1r;

    // Up 2 octaves: the same phase-scaling applied to the up-1 signal.
    // Computed from u1 with its own normalization — NEVER via inv^3 of the
    // band signal: FastInvSqrt(0) is a huge finite number whose cube
    // overflows to inf, and 0 * inf = NaN on silent input (which then sticks
    // in every downstream filter state and mutes the whole mode).
    const float p2   = u1r * u1r - u1i * u1i;
    const float inv2 = FastInvSqrt(u1r * u1r + u1i * u1i);
    up2_ = p2 * inv2;

    // Down 1 octave: complex square root (half phase), half-angle identities;
    // sign continuation via down1_sign_.
    const float b_sign = (b < 0.f) ? -1.f : 1.f;
    const float xh     = 0.5f * a * inv;
    const float c      = FastSqrt(0.5f + xh);
    const float d      = b_sign * FastSqrt(0.5f - xh);
    dn1r_ = down1_sign_ * (a * c + b * d);
    dn1i_ = down1_sign_ * (b * c - a * d);
  }

  inline float Up1() const { return up1_; }
  inline float Up2() const { return up2_; }
  inline float Down1() const { return dn1r_; }

 private:
  float d0_ = 0.f;
  float d1r_ = 0.f, d1i_ = 0.f;
  float d2r_ = 0.f, d2i_ = 0.f;
  float c1r_ = 0.f, c1i_ = 0.f;
  float c2r_ = 0.f, c2i_ = 0.f;

  float s1r_ = 0.f, s1i_ = 0.f;
  float s2r_ = 0.f, s2i_ = 0.f;
  float yr_ = 0.f, yi_ = 0.f;

  float up1_ = 0.f, up2_ = 0.f;
  float dn1r_ = 0.f, dn1i_ = 0.f;
  float down1_sign_ = 1.f;
};

// The full engine: decimate -> 80-band shift -> per-voice interpolation.
class PolyOctave {
 public:
  static constexpr int kBands  = 80;
  static constexpr int kFactor = 6;  // 48 kHz / 6 = 8 kHz analysis rate

  void Init(float sample_rate) {
    const float sr8 = sample_rate / (float)kFactor;
    for (int i = 0; i < kBands; i++) {
      const float f0 = CenterFreq(i - 1);
      const float f1 = CenterFreq(i);
      const float f2 = CenterFreq(i + 1);
      const float a  = f2 - f1;
      const float b  = f1 - f0;
      const float bw = 2.f * (a * b) / (a + b);
      bands_[i].Init(f1, sr8, bw);
    }
  }

  // Process one audio block (n must be a multiple of kFactor; block size 48
  // is). Fills the three per-voice wet buffers at the full sample rate; the
  // caller mixes them with its own gains.
  void ProcessBlock(const float* in, float* sub, float* up1, float* up2,
                    size_t n) {
    for (size_t i = 0; i + kFactor <= n; i += kFactor) {
      const float d = dec_(in + i);
      float s_dn1 = 0.f, s_up1 = 0.f, s_up2 = 0.f;
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        s_dn1 += bands_[k].Down1();
        s_up1 += bands_[k].Up1();
        s_up2 += bands_[k].Up2();
      }
      int_sub_(s_dn1, sub + i);
      int_up1_(s_up1, up1 + i);
      int_up2_(s_up2, up2 + i);
    }
  }

 private:
  // Quasi-log band spacing, ~60 Hz (n=0) to ~1.7 kHz (n=79) — the analysis
  // range is deliberately fundamentals-only (restricted bandwidth is part of
  // the POG character). NOTE for the bass profile: low B (31 Hz) sits below
  // band 0 — the curve becomes an instrument-profile lever when we retune.
  static float CenterFreq(int n) {
    return 480.f * powf(2.f, 0.027f * (float)n) - 420.f;
  }

  BandShifter  bands_[kBands];
  Decimator    dec_;
  Interpolator int_sub_, int_up1_, int_up2_;
};

}  // namespace polyoct
