#pragma once
//
// mnemonic_multiband_freeze.h — multiband incommensurate GRANULAR freeze.
//
// The EHX-Freeze character: a moving, evolving, DENSE sustain with transients
// removed — NOT a static tone. Model (user's ear):
//   • split the captured window into 3 bands (low / mid / high);
//   • per band, a GRAIN CLOUD scans that band's material — a read pointer loops a
//     band-specific length while overlapping grains (band-appropriate size: long
//     for lows, short for highs) are sprayed around it → density + shimmer;
//   • the per-band scan loop lengths are mutually INCOMMENSURATE (coprime) so the
//     bands never re-sync → the sum phases forever, never audibly repeats (that
//     perceived repetition was the "flutter" complaint).
//
// Density = grains per band (overlap). Movement = the scan + incommensurate bands
// + grain spray. Built on the proven GrainVoice / RingBuffer. Band buffers live
// in SDRAM (caller provides them). Pure DSP; no daisy dependency.
//
#include "grain_voice.h"   // core/blocks — pulls in ring_buffer.h
#include <cmath>
#include <cstdint>

class MultibandFreeze {
 public:
  static constexpr int NB   = 3;   // low, mid, high
  static constexpr int POOL = 5;   // grains per band (>= overlap + headroom)

  void Init(float sr, float* low, float* mid, float* high, int cap) {
    sr_ = sr; slab_[0] = low; slab_[1] = mid; slab_[2] = high; cap_ = cap;
    for (int b = 0; b < NB; b++) ring_[b].Init(slab_[b], cap);  // memsets the slabs
    active_ = false;
  }
  void SetXovers(float lo_hz, float hi_hz) { xlo_ = lo_hz; xhi_ = hi_hz; }
  // per-band scan-loop lengths (samples) — keep coprime (incommensurate).
  void SetLoopLens(int lo, int mid, int hi) { Le_[0]=lo; Le_[1]=mid; Le_[2]=hi; }
  // per-band grain length + overlap (density) + position spray (samples).
  void SetGrain(int band, int len, float overlap, int spray) {
    if (band<0||band>=NB) return;
    glen_[band]=len; overlap_[band]=overlap<1.f?1.f:overlap; spray_[band]=spray;
  }
  void  Stop()        { active_ = false; }
  bool  Active() const { return active_; }

  // data = chronological captured samples (oldest→newest). Splits into 3 bands
  // (written straight into the SDRAM band slabs) and starts the grain clouds.
  void Commit(const float* data, int n) {
    int len = n < cap_ ? n : cap_;
    if (len < 16) { active_ = false; return; }
    len_ = len;
    BQ lp1, hp1, lp2, hp2;
    lp1.LP(xlo_, sr_); hp1.HP(xlo_, sr_); lp2.LP(xhi_, sr_); hp2.HP(xhi_, sr_);
    for (int i = 0; i < len; i++) {
      float x = data[i];
      slab_[0][i] = lp1.P(x);                 // low  = LP@xlo
      slab_[1][i] = lp2.P(hp1.P(x));          // mid  = HP@xlo → LP@xhi
      slab_[2][i] = hp2.P(x);                 // high = HP@xhi
    }
    for (int b = 0; b < NB; b++) {
      for (int k = 0; k < POOL; k++) grains_[b][k].Reset();
      // clamp scan loop so a grain (len glen_) never reads past the capture seam
      int cap_scan = len_ - glen_[b] - (int)spray_[b] - 1;
      if (cap_scan < 1) cap_scan = 1;
      scanlen_[b] = Le_[b] < cap_scan ? Le_[b] : cap_scan;
      scan_[b] = (float)(b * scanlen_[b]) / (float)(NB + 1);  // staggered start
      timer_[b] = 0;                                          // emit immediately
    }
    active_ = true;
  }

  float NextSample() {
    if (!active_) return 0.f;
    float y = 0.f;
    for (int b = 0; b < NB; b++) {
      // scan pointer advances and loops the band's incommensurate length
      scan_[b] += 1.f;
      if (scan_[b] >= (float)scanlen_[b]) scan_[b] -= (float)scanlen_[b];
      // grain scheduler: emit at hop = glen/overlap
      if (--timer_[b] <= 0) {
        EmitGrain(b);
        int hop = (int)((float)glen_[b] / overlap_[b]);
        if (hop < 1) hop = 1;
        timer_[b] = hop;
      }
      for (int k = 0; k < POOL; k++)
        if (grains_[b][k].IsActive()) y += grains_[b][k].Process(ring_[b]);
    }
    return y;
  }

 private:
  void EmitGrain(int b) {
    int g = -1;
    for (int k = 0; k < POOL; k++) if (!grains_[b][k].IsActive()) { g = k; break; }
    if (g < 0) return;                          // pool full → drop (density cap)
    float off = (Rand() * 2.f - 1.f) * spray_[b];
    float pos = scan_[b] + off;
    if (pos < 0.f) pos = 0.f;
    float maxpos = (float)(len_ - glen_[b] - 1);
    if (maxpos < 0.f) maxpos = 0.f;
    if (pos > maxpos) pos = maxpos;
    size_t start = (size_t)pos;
    size_t delay = (0 + (size_t)cap_ - start) % (size_t)cap_;  // write_pos is 0
    float gain = 2.f / overlap_[b];             // Hann overlap-add level comp
    grains_[b][g].Trigger(ring_[b], delay, (size_t)glen_[b],
                          false, 1.f, gain, 1, 1.0f, 1.f);
  }

  float Rand() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                 return (float)rng_ / 4294967295.f; }

  struct BQ {  // RBJ 2-pole, Butterworth Q, TDF-II
    float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void LP(float fc, float sr) {
      float w=6.2831853f*fc/sr, c=cosf(w), s=sinf(w), al=s/1.41421356f, a0=1+al;
      b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=b0; a1=-2*c/a0; a2=(1-al)/a0; z1=z2=0;
    }
    void HP(float fc, float sr) {
      float w=6.2831853f*fc/sr, c=cosf(w), s=sinf(w), al=s/1.41421356f, a0=1+al;
      b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=b0; a1=-2*c/a0; a2=(1-al)/a0; z1=z2=0;
    }
    float P(float x){ float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
  };

  float sr_ = 48000.f;
  float* slab_[NB] = {nullptr,nullptr,nullptr};
  RingBuffer ring_[NB];
  GrainVoice grains_[NB][POOL];
  int    cap_ = 0, len_ = 0;
  int    Le_[NB]      = {11987, 8419, 4099};     // scan-loop lengths (coprime)
  int    scanlen_[NB] = {1,1,1};
  float  scan_[NB]    = {0.f,0.f,0.f};
  int    glen_[NB]    = {7200, 3840, 1920};      // grain len: low 150ms / mid 80ms / high 40ms
  float  overlap_[NB] = {3.f, 3.f, 3.f};         // grains per band (density)
  int    spray_[NB]   = {480, 240, 120};         // position spray (samples)
  int    timer_[NB]   = {0,0,0};
  float  xlo_ = 250.f, xhi_ = 2000.f;
  uint32_t rng_ = 0x777abc01u;
  bool   active_ = false;
};
