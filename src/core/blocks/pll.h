#pragma once
//
// pll.h — minimal CD4046-style phase-locked loop (header-only).
//
//   input -> hard limiter (sign) ----------------------+
//                                                      phase comparator
//   VCO square -> feedback divider /N (flip-flop) -----+   (PC1 XOR or PC2 PFD)
//        |                                                 -> one-pole RC loop filter -> VCO control
//        +-> output divider /M (flip-flop) -> output (square, or triangle)
//
// The loop-filter output v (0..1) drives the VCO exponentially through its
// free-running centre: v = 0.5 -> centre, v = 0 -> fmin, v = 1 -> fmax (each
// half its own octave span). The VCO is a naive (non-band-limited) square.
//
// Dividers (4046 + CD4040 style): square/flip-flop counters on the VCO edges.
// Each divider counts BOTH VCO edges and toggles its own square every N edges,
// so /N is a 50 %-duty square at fVCO/N for any N (also /3). The comparator
// sees the input against the VCO divided by N (VCO locks at N x input); the
// audible output is the VCO divided by M.
//
// Phase comparators:
//   PC1 — XOR of input sign and divided VCO, straight into the RC filter.
//   PC2 — edge-triggered tri-state phase-frequency detector: an input rising
//         edge steps the state up, a divided-VCO rising edge steps it down
//         (state -1 / 0 / +1). +1 drives the RC filter towards 1 (VCO up),
//         -1 towards 0 (VCO down), 0 = high impedance: the filter HOLDS
//         (charge pump into the one-pole). Locks with zero phase error to the
//         input's rising zero crossings.
//
// Output shape: the divided square (default) or a bipolar TRIANGLE (-1..+1)
// of the OUTPUT phase at the same frequency. Output phase = (output-divider
// half-cycle count + VCO half-cycle phase) / (2 M) — i.e. (count + VCO
// phase) / M; for M = 1 it is the VCO phase itself. Naive (not band-limited).
//
// Nothing else: no hysteresis on the comparator, no lock detector, no
// frequency clamp beyond the VCO range itself.
//
#include <math.h>

class Pll4046 {
 public:
  void Init(float sr, float fmin, float fcentre, float fmax) {
    sr_ = sr;
    fmin_ = fmin; fc_ = fcentre; fmax_ = fmax;
    up_oct_ = logf(fmax / fcentre);    // natural-log span above the centre
    dn_oct_ = logf(fcentre / fmin);    // ... and below it
    Reset();
    SetLoopFilterHz(20.f);
  }
  void Reset() {
    phase_ = 0.f; v_ = 0.5f; freq_ = fc_;
    vco_prev_ = true;                  // phase 0 -> VCO square high
    fb_q_ = true;  fb_cnt_ = 0;
    out_q_ = true; out_cnt_ = 0;
    in_prev_ = false; pfd_ = 0;
  }
  // One-pole RC loop-filter cutoff (Hz).
  void SetLoopFilterHz(float hz) { a_ = 1.f - expf(-2.f * 3.14159265f * hz / sr_); }
  // Feedback divider N (>= 1), output divider M (>= 1), comparator PC2 (else PC1),
  // output triangle (else square). Divider counters and the PFD state are kept
  // across changes (no reset).
  void SetTriGain(float g) { tri_gain_ = g; }    // 1 = triangle, >1 = trapezoid
  void SetMode(int fb_div, int out_div, bool pc2, bool tri = false) {
    tri_ = tri;
    fb_div_  = fb_div  < 1 ? 1 : fb_div;
    out_div_ = out_div < 1 ? 1 : out_div;
    if (pc2 && !pc2_) pfd_ = 0;        // entering PC2: start tri-stated
    pc2_ = pc2;
  }

  // One sample. Returns the divided VCO square (+-1), or the triangle (-1..+1).
  inline float Process(float in) {
    const bool in_hi  = in >= 0.f;               // comparator: sign of the input
    const bool vco_hi = phase_ < 0.5f;           // VCO square
    if (vco_hi != vco_prev_) {                   // a VCO edge: clock both dividers
      vco_prev_ = vco_hi;
      const bool fb_was = fb_q_;
      if (++fb_cnt_ >= fb_div_)  { fb_cnt_ = 0;  fb_q_ = !fb_q_; }
      if (++out_cnt_ >= out_div_) { out_cnt_ = 0; out_q_ = !out_q_; }
      if (fb_q_ && !fb_was && pfd_ > -1) pfd_--;  // divided-VCO rising edge: down
    }
    if (in_hi && !in_prev_ && pfd_ < 1) pfd_++;   // input rising edge: up
    in_prev_ = in_hi;
    if (pc2_) {
      if (pfd_ > 0)      v_ += (1.f - v_) * a_;  // pump up
      else if (pfd_ < 0) v_ += (0.f - v_) * a_;  // pump down
      // pfd_ == 0: high impedance, the RC filter holds
    } else {
      const float xr = (in_hi != fb_q_) ? 1.f : 0.f;   // XOR phase comparator
      v_ += (xr - v_) * a_;                      // RC loop filter
    }
    const float c = 2.f * v_ - 1.f;              // -1 .. +1 around the centre
    freq_ = fc_ * expf(c * (c >= 0.f ? up_oct_ : dn_oct_));
    if (tri_) {
      // Output phase from the divider state + the VCO phase (read before the
      // advance, i.e. the same instant the square value below reflects).
      const float half = vco_hi ? phase_ * 2.f : phase_ * 2.f - 1.f;   // 0..1 within the VCO half
      const int   e    = (out_q_ ? 0 : out_div_) + out_cnt_;              // half-cycles into the output period
      float op = ((float)e + half) / (float)(2 * out_div_);
      if (op > 1.f) op = 1.f;                    // (counter left above a reduced M: until it wraps)
      phase_ += freq_ / sr_;
      if (phase_ >= 1.f) phase_ -= 1.f;
      // Triangle (+1 at the square's rising edge, -1 mid-period) x tri_gain_,
      // clipped: 1 = triangle, higher = a trapezoid toward the square.
      float t = (4.f * fabsf(op - 0.5f) - 1.f) * tri_gain_;
      return t > 1.f ? 1.f : (t < -1.f ? -1.f : t);
    }
    phase_ += freq_ / sr_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    return out_q_ ? 1.f : -1.f;
  }

  float VcoHz() const { return freq_; }          // current VCO frequency (undivided)
  float Control() const { return v_; }           // loop-filter output 0..1

 private:
  float sr_ = 48000.f, fmin_ = 30.f, fc_ = 200.f, fmax_ = 4000.f;
  float up_oct_ = 0.f, dn_oct_ = 0.f;
  float phase_ = 0.f, v_ = 0.5f, a_ = 0.f, freq_ = 200.f;
  int  fb_div_ = 1, out_div_ = 1;
  bool pc2_ = false, tri_ = false;
  float tri_gain_ = 1.f;                         // triangle -> trapezoid (clip gain)
  bool vco_prev_ = true, fb_q_ = true, out_q_ = true, in_prev_ = false;
  int  fb_cnt_ = 0, out_cnt_ = 0, pfd_ = 0;
};
