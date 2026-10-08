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
// Output kind (Ray Gun Youth's rotary, 2026-10-08):
//   OUT_VCO    — the divided VCO (square / triangle as above).
//   OUT_CLASH  — the PC1 XOR output itself: +1 where the input sign and the
//                divided VCO differ, -1 where they agree.
//   OUT_SPIKES — the 4046 phase pulses (pin 1): +1 while PC2 is tri-stated
//                (in phase), -1 while it pumps up or down.
// Optional output coupling (one-pole high-pass, the pedal's output capacitor)
// on whatever is played.
//
// Optional front end (Ray Gun Youth): the input is squared (sign), AC-coupled
// (one-pole high-pass) and low-passed (2-pole) before the comparator.
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
  enum OutKind { OUT_VCO = 0, OUT_CLASH = 1, OUT_SPIKES = 2 };
  void SetOutKind(int k) { kind_ = k; }
  // Front end: square -> one-pole HP (hp_hz) -> 2-pole LP (lp_hz, q). on = false: raw input.
  void SetFrontEnd(bool on, float hp_hz, float lp_hz, float q) {
    fe_ = on;
    fe_hp_a_ = 1.f / (1.f + 2.f * 3.14159265f * hp_hz / sr_);
    const float w = 2.f * 3.14159265f * lp_hz / sr_, cw = cosf(w), al = sinf(w) / (2.f * q), a0 = 1.f + al;
    fe_b0_ = (1.f - cw) * 0.5f / a0; fe_b1_ = (1.f - cw) / a0; fe_b2_ = fe_b0_;
    fe_a1_ = -2.f * cw / a0; fe_a2_ = (1.f - al) / a0;
  }
  // Output coupling: one-pole high-pass on the output (hz <= 0: off).
  void SetOutputHighpass(float hz) { out_hp_a_ = hz > 0.f ? 1.f / (1.f + 2.f * 3.14159265f * hz / sr_) : 1.f; }
  void SetTriGain(float g) { tri_gain_ = g; }    // 1 = triangle, >1 = trapezoid
  void SetMode(int fb_div, int out_div, bool pc2, bool tri = false) {
    tri_ = tri;
    fb_div_  = fb_div  < 1 ? 1 : fb_div;
    out_div_ = out_div < 1 ? 1 : out_div;
    if (pc2 && !pc2_) pfd_ = 0;        // entering PC2: start tri-stated
    pc2_ = pc2;
  }

  // One sample. Returns the chosen output (VCO square / triangle, CLASH, SPIKES), coupled.
  inline float Process(float in) {
    if (fe_) {                                   // front end: square, AC couple, low-pass
      const float sq = in >= 0.f ? 1.f : -1.f;
      fe_hp_y_ = fe_hp_a_ * (fe_hp_y_ + sq - fe_hp_x_); fe_hp_x_ = sq;
      const float x = fe_hp_y_;
      const float y = fe_b0_ * x + fe_b1_ * fe_x1_ + fe_b2_ * fe_x2_ - fe_a1_ * fe_y1_ - fe_a2_ * fe_y2_;
      fe_x2_ = fe_x1_; fe_x1_ = x; fe_y2_ = fe_y1_; fe_y1_ = y;
      in = y;
    }
    const float o = Raw(in);
    out_hp_y_ = out_hp_a_ * (out_hp_y_ + o - out_hp_x_); out_hp_x_ = o;   // output coupling
    return out_hp_y_;
  }

 private:
  inline float Raw(float in) {
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
    const float clash  = (in_hi != fb_q_) ? 1.f : -1.f;   // PC1 output (before the VCO moves)
    const float spikes = pfd_ == 0 ? 1.f : -1.f;          // phase pulses
    const float c = 2.f * v_ - 1.f;              // -1 .. +1 around the centre
    freq_ = fc_ * expf(c * (c >= 0.f ? up_oct_ : dn_oct_));
    if (kind_ == OUT_CLASH || kind_ == OUT_SPIKES) {
      phase_ += freq_ / sr_;
      if (phase_ >= 1.f) phase_ -= 1.f;
      return kind_ == OUT_CLASH ? clash : spikes;
    }
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


 public:
  float VcoHz() const { return freq_; }          // current VCO frequency (undivided)
  float Control() const { return v_; }           // loop-filter output 0..1

 private:
  int   kind_ = OUT_VCO;
  bool  fe_ = false;
  float fe_hp_a_ = 1.f, fe_hp_x_ = 0.f, fe_hp_y_ = 0.f;
  float fe_b0_ = 1.f, fe_b1_ = 0.f, fe_b2_ = 0.f, fe_a1_ = 0.f, fe_a2_ = 0.f;
  float fe_x1_ = 0.f, fe_x2_ = 0.f, fe_y1_ = 0.f, fe_y2_ = 0.f;
  float out_hp_a_ = 1.f, out_hp_x_ = 0.f, out_hp_y_ = 0.f;
  float sr_ = 48000.f, fmin_ = 30.f, fc_ = 200.f, fmax_ = 4000.f;
  float up_oct_ = 0.f, dn_oct_ = 0.f;
  float phase_ = 0.f, v_ = 0.5f, a_ = 0.f, freq_ = 200.f;
  int  fb_div_ = 1, out_div_ = 1;
  bool pc2_ = false, tri_ = false;
  float tri_gain_ = 1.f;                         // triangle -> trapezoid (clip gain)
  bool vco_prev_ = true, fb_q_ = true, out_q_ = true, in_prev_ = false;
  int  fb_cnt_ = 0, out_cnt_ = 0, pfd_ = 0;
};
