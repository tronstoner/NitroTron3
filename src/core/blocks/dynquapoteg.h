#pragma once
//
// dynquapoteg.h — DYNamic QUAntised POlymetric TEmpo Grid.
//
// The time handling of a looper / delay that has a master period T but no
// clock: where T comes from (a knob, a tap, a free-run capture), how a captured
// length is quantised against it, how a playing loop follows it, and how a
// pass is cut into rhythm steps. Lifted out of vestige (the formulas, the
// rules and the order of every operation are exactly vestige's); every tuning
// value comes in through DynquapotegConfig, so another module can run the same
// grid with its own range.
//
// What it is NOT: a clock. T is a PERIOD, never a running bar line — each
// capture anchors its own grid at its own start (see grid_quantize.h, which
// this block uses for the division set and the nearest-boundary rule). Loops of
// different divisions of the same T phase against each other: that is the
// "polymetric" in the name, and why nothing in here keeps a phase.
//
// No audio, no allocation, no hardware: knob values, footswitch edges and the
// millisecond time come in as arguments.
//
// THREADS. Two callers, as in vestige:
//   control thread — UpdatePeriod() (knob / tap / free arbitration, T).
//   audio thread   — PostFree() / TakePinRelease() (the free-run handshake),
//                    every length rule (DecideLen, FollowLen, PassLen, ...),
//                    reading T() and InFreeZone().
// T and the free-run flags are each one aligned word; UpdatePeriod stores T
// before the flags the audio thread acts on (compiler barrier, single core).
//
// The bounds, on an absolute scale (samples / ms / knob travel):
//   LOWER  t_min  — the shortest T (knob noon, the fastest valid tap) — and
//          loop_floor, the shortest loop any rule may decide or follow, and
//          min_step_ms, the shortest rhythm step (ratchet / slice fallback).
//   MID    step_ms — the rhythm step a pass is cut into (up to step_knee_ms,
//          growing as (pass / knee)^step_exp past it).
//   UPPER  t_max  — the longest T (knob ends, the slowest valid tap, the free
//          capture ceiling) — and max_steps, the most steps per pass.
//
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "grid_quantize.h"   // the division set + nearest-boundary quantiser

struct DynquapotegConfig {
  // ---- T range ----------------------------------------------------------------
  size_t   t_min;            // LOWER bound of T (samples)
  size_t   t_max;            // UPPER bound of T (samples)
  uint32_t t_min_ms;         // the tap's valid interval range (ms) — the same
  uint32_t t_max_ms;         //   bounds, in the tap's own unit
  size_t   loop_floor;       // no loop length below this (samples; division kept)
  // ---- the knob (T magnitude; remapped travel 0..1) ----------------------------
  float    knob_deadzone;    // +- around noon: T = t_min
  float    free_zone;        // outer zone at each end: free-run (0 = none)
  float    knob_move_eps;    // raw travel that cancels a tapped T
  float    knob_follow_db;   // travel below which T does not move (ADC jitter)
  // ---- the tap ------------------------------------------------------------------
  uint32_t tap_release_ms;   // a press released before this is a tap
  float    tap_agree;        // a second interval within this fraction sets T
  // ---- rhythm steps ---------------------------------------------------------------
  float    step_ms;          // MID: the wanted step (output time)
  float    step_knee_ms;     // pass length past which the step grows ...
  float    step_exp;         // ... as (pass / knee)^exp
  int      max_steps;        // UPPER: steps per pass
  float    min_step_ms;      // LOWER: shortest step a split may make
};

class Dynquapoteg {
 public:
  void Init(const DynquapotegConfig& cfg, float sample_rate) {
    cfg_ = cfg;
    sr_  = sample_rate;
    period_ = cfg_.t_min;
  }

  // ---- T: the master period ---------------------------------------------------
  size_t T() const          { return period_; }
  size_t TMin() const       { return cfg_.t_min; }
  size_t TMax() const       { return cfg_.t_max; }
  size_t TapPeriod() const  { return tap_period_; }
  // The knob is in the FREE-RUN zone now (control writes; a capture latches it
  // at its start, LEDs read it).
  bool   InFreeZone() const { return k2_free_; }
  // HOST-TEST HOOK, never set by the firmware: accept a single interval (the
  // old two-tap rule), for tests that re-tap at a sample-exact moment to
  // exercise how loops follow T — not the tap rule itself.
  void   SetTapAcceptOne(bool on) { tap_accept_one_ = on; }

  // Knob magnitude -> T: log taper, noon dead zone = t_min, either end = t_max.
  size_t KnobPeriod(float k2) const {
    const float c = k2 - 0.5f;                        // [-0.5, +0.5]
    float mag = (fabsf(c) - cfg_.knob_deadzone) / (0.5f - cfg_.knob_deadzone);
    if (mag < 0.f) mag = 0.f;
    if (mag > 1.f) mag = 1.f;
    const float lo = (float)cfg_.t_min;
    const float hi = (float)cfg_.t_max;
    size_t t = (size_t)(lo * powf(hi / lo, mag));
    if (t < cfg_.t_min) t = cfg_.t_min;
    if (t > cfg_.t_max) t = cfg_.t_max;
    return t;
  }

  // Control thread, once per tick. Knob / tap / free arbitration (sprawl's,
  // exactly): the last gesture wins. k2_raw = the raw knob (tap cancel),
  // k2 = remapped (taper, follow deadband, free zone). tap_down / tap_up = the
  // tap switch's edges this tick, now_ms = the control clock. The DOWN-press is
  // the timing reference (so tempo accuracy does not depend on the release),
  // timed from our own rising-edge timestamp. Returns true when a tap set T;
  // *tap_press_ms is then how long ago (ms) the closing down-press was, for a
  // caller that anchors a beat on it.
  bool UpdatePeriod(float k2_raw, float k2, bool tap_down, bool tap_up,
                    uint32_t now_ms, uint32_t* tap_press_ms) {
    bool tapped = false;
    if (!k2_seeded_) { k2_last_ = k2_raw; k2_seeded_ = true; }
    if (fabsf(k2_raw - k2_last_) > cfg_.knob_move_eps) {
      k2_last_ = k2_raw;
      tap_period_ = 0;                                // knob wins: drop the tapped T
    }
    const uint32_t now = now_ms;
    if (tap_down) f1_down_ms_ = now;
    if (tap_up) {
      const uint32_t press = now - f1_down_ms_;
      if (press < cfg_.tap_release_ms) {
        if (tap_prev_ms_ != 0) {
          const uint32_t iv = f1_down_ms_ - tap_prev_ms_;
          const bool valid = (iv >= cfg_.t_min_ms && iv <= cfg_.t_max_ms);
          // tap_agree: only a SECOND interval agreeing with the previous one
          // sets T (their mean), so one stray press cannot re-time the loops.
          const uint32_t pv = tap_prev_iv_ms_;
          const bool agree = valid && (tap_accept_one_ || (pv != 0 &&
              fabsf((float)iv - (float)pv) <= cfg_.tap_agree * (float)pv));
          tap_prev_iv_ms_ = valid ? iv : 0;          // an invalid interval breaks the chain
          if (agree) {
            const float mean_ms = tap_accept_one_ ? (float)iv : 0.5f * ((float)iv + (float)pv);
            size_t t = (size_t)(mean_ms * 0.001f * sr_ + 0.5f);
            if (t < cfg_.t_min) t = cfg_.t_min;
            if (t > cfg_.t_max) t = cfg_.t_max;
            tap_period_ = t;
            tapped = true;
            if (tap_press_ms) *tap_press_ms = press;
          }
        }
        tap_prev_ms_ = f1_down_ms_;
      }
    }
    // Knob T through a small movement deadband: loops FOLLOW T, so ADC jitter
    // on the knob must not reach it (1% of T is ~17 cents of tape warble). T is
    // recomputed only when the knob has moved more than knob_follow_db; a knob
    // that does not move gives exactly the same T as before.
    if (!k2f_seeded_) { k2f_ = k2; k2f_seeded_ = true; }
    if (fabsf(k2 - k2f_) > cfg_.knob_follow_db) k2f_ = k2;
    // FREE-RUN zone. Knob positions inside the zone count as ONE position (its
    // edge), so wiggling inside it is no move. A free capture's T (posted by
    // the audio thread, PostFree) is adopted here — it replaces a tapped T
    // (last gesture wins) — and holds, also after the knob leaves the zone,
    // until the knob moves on beyond knob_follow_db from where it was at the
    // adoption, or a tap.
    k2_free_ = (k2 >= 1.f - cfg_.free_zone || k2 <= cfg_.free_zone);
    const float lo = cfg_.free_zone, hi = 1.f - cfg_.free_zone;
    const float k2c = k2 < lo ? lo : (k2 > hi ? hi : k2);
    if (tap_period_ > 0) free_period_ = 0;            // a tap (or a tap still standing) wins over an older free T
    const uint32_t post = free_post_seq_;
    if (post != free_seen_seq_) {
      free_seen_seq_ = post;
      free_period_ = free_post_Q_;
      free_k2_ref_ = k2c;
      tap_period_  = 0;
    } else if (free_period_ > 0 && fabsf(k2c - free_k2_ref_) > cfg_.knob_follow_db) {
      free_period_ = 0;                               // the knob moved on: it sets T again
    }
    period_ = (tap_period_ > 0) ? tap_period_ : (free_period_ > 0) ? free_period_ : KnobPeriod(k2f_);
    __asm__ __volatile__("" ::: "memory");            // period_ is stored before the flags the audio thread reads
    free_in_force_ = (tap_period_ == 0 && free_period_ > 0);
    free_ack_seq_  = free_seen_seq_;                  // after period_: the audio thread may now release on !free_in_force_
    return tapped;
  }

  // ---- FREE-RUN handshake (audio thread) ----------------------------------------
  // A free capture decided at length Q: it is to become T. The caller pins
  // whatever must keep its length (FollowLen / PassLen at T_now) BEFORE posting,
  // and pins the new loop at Q until T = Q is adopted.
  void PostFree(size_t Q) {
    pins_held_ = true;
    free_post_Q_ = Q;
    free_post_seq_ = free_post_seq_ + 1;
  }
  bool PinsHeld() const { return pins_held_; }
  // Block start: once T is no longer a free T (the control thread adopted the
  // last post and then the knob moved on / a tap), every pin releases. True =
  // release them now (the caller clears its pins); the grid then holds none.
  bool TakePinRelease() {
    if (!pins_held_ || free_ack_seq_ != free_post_seq_ || free_in_force_) return false;
    pins_held_ = false;
    return true;
  }

  // ---- Lengths ------------------------------------------------------------------
  // The loop floor: every loop length decided or followed.
  size_t LoopFloor(size_t L) const { return (L < cfg_.loop_floor) ? cfg_.loop_floor : L; }
  // Boundary(d, T), floored (0 stays 0: no division).
  size_t LoopBoundary(int d, size_t T) const {
    const size_t b = GridQuantize::Boundary(d, T);
    return (b == 0) ? 0 : LoopFloor(b);
  }
  // Division d as a fraction of T (exact integer ratio, in double).
  static double DivFrac(int d) {
    return (double)GridQuantize::kDivNum[d] / (double)GridQuantize::kDivDen[d];
  }
  // A free capture's length: raw, kept inside [t_min, t_max] — it becomes T
  // (t_min >= loop_floor: the loop floor).
  size_t FreeLen(size_t raw) const {
    if (raw < cfg_.t_min) return cfg_.t_min;
    if (raw > cfg_.t_max) return cfg_.t_max;
    return raw;
  }
  // A capture ended at raw samples, its grid T latched at its start: the
  // length it plays and (*div) its division. Free: raw kept in range, 1/1 of
  // itself (the caller posts it, PostFree). Else the NEAREST division of T, up
  // or down (grid_quantize.h), division taken before the loop floor, then
  // floored — extended, like a round-up, division kept.
  size_t DecideLen(size_t raw, size_t T, bool free, int* div) const {
    size_t Q;
    if (free) {
      Q = FreeLen(raw);
      *div = 0;
    } else {
      Q = GridQuantize::Quantize(raw, T);             // nearest division, up or down
      *div = GridQuantize::IndexOf(Q, T);             // its division, before the floor
      Q = LoopFloor(Q);                               // the loop floor: extended, like a round-up
    }
    return Q;
  }
  // The length a loop of division d (stored length M) is heading for at T_now:
  // Boundary(d, T_now); no division -> M.
  size_t FollowLen(size_t M, int d) const {
    const size_t Lt = (d >= 0) ? LoopBoundary(d, period_) : 0;
    return (Lt == 0) ? M : Lt;
  }
  // A following loop's target at T_now: its pin if pinned (free-run), else
  // Boundary(d, T_now) (0 = no division).
  size_t FollowTarget(int d, size_t pin) const {
    return pin ? pin : LoopBoundary(d, period_);
  }
  // A virtual pass (a rhythm cycle with no loop of its own): its pin if
  // pinned, else Boundary(d, T_now), or T itself (floored) before any division
  // is known (d < 0). Never 0.
  size_t PassLen(int d, size_t pin) const {
    if (pin > 0) return pin;
    const size_t T = period_ > 0 ? period_ : 1u;
    const size_t L = (d >= 0) ? LoopBoundary(d, T) : LoopFloor(T);
    return L > 0 ? L : 1u;
  }

  // ---- Rhythm steps -------------------------------------------------------------
  // G: 2^k or 3 x 2^k steps per pass, the step closest (in ratio) to the
  // wanted step in output time: step_ms up to a pass of step_knee_ms, then
  // growing as (pass / knee)^step_exp (long, ambient loops glitch slower).
  // step_scale = the caller's tempo hook on the wanted step (2 = half time).
  int StepCount(double pass_out, double step_scale) const {
    const double knee = (double)cfg_.step_knee_ms * 0.001 * (double)sr_;
    const double want = (double)cfg_.step_ms * 0.001 * (double)sr_
                      * (pass_out > knee ? pow(pass_out / knee, (double)cfg_.step_exp) : 1.0)
                      * step_scale;
    int G = 1; double best = 1e30;
    for (int base = 1; base <= 3; base += 2)
      for (int g = base; g <= cfg_.max_steps; g *= 2) {
        const double r = pass_out / (double)g / want;
        const double d = r > 1.0 ? r : 1.0 / r;
        const bool tie = fabs(d - best) <= best * 1e-6;       // (a tie keeps the finer grid)
        if ((d < best && !tie) || (tie && g > G)) { best = d; G = g; }
      }
    return G;
  }
  // The shortest step a split (ratchet, slice) may make, in samples.
  double MinStep() const { return (double)cfg_.min_step_ms * 0.001 * (double)sr_; }

  // ---- Phrase measurement ---------------------------------------------------------
  // A capture's PHRASE, measured per sample from its onset: it ends at the
  // ceiling (T latched at the onset, or t_max when free) or after `release`
  // samples of gate silence; raw = where the sound stopped. Two meters: the
  // FAST one says where the sound last reached the close level, the GATE one
  // says when the silence is long enough. to_last_loud (a slow gate meter):
  // the end is the fast meter's last loud sample + 1, also at the ceiling when
  // the sound had already stopped there; else the silence's first sample.
  class Phrase {
   public:
    void Start(size_t ceiling) {
      idx_ = 0; ceil_ = ceiling;
      sil_run_ = 0; sil_onset_ = 0; last_loud_ = 0;
    }
    size_t Len() const     { return idx_; }      // samples measured so far
    size_t Ceiling() const { return ceil_; }     // the grid T it quantises against
    // One sample. True = the phrase ended here, *raw = its length.
    bool Step(float fast, float gate, float close, uint32_t release, bool to_last_loud, size_t* raw) {
      const size_t r = idx_;
      idx_ = r + 1;
      if (fast >= close) last_loud_ = r;
      if (idx_ >= ceil_) {
        const bool ended = to_last_loud && fast < close && last_loud_ + 1 < idx_;
        *raw = ended ? last_loud_ + 1 : idx_;
        return true;
      } else if (gate < close) {
        if (sil_run_ == 0) sil_onset_ = r;
        if (++sil_run_ >= release) {
          *raw = to_last_loud ? last_loud_ + 1 : sil_onset_;
          return true;
        }
      } else {
        sil_run_ = 0;
      }
      return false;
    }
   private:
    size_t   idx_ = 0, ceil_ = 0, sil_onset_ = 0, last_loud_ = 0;
    uint32_t sil_run_ = 0;
  };

 private:
  DynquapotegConfig cfg_ = {};
  float    sr_ = 48000.f;

  // T and its knob / tap arbitration. Control thread only (period_ is read by
  // the audio thread). tap_period_ = 0 means "no tap: the knob sets T".
  size_t   period_       = 0;
  size_t   tap_period_   = 0;
  uint32_t tap_prev_ms_  = 0;     // last committed tap down-press (0 = no chain)
  uint32_t tap_prev_iv_ms_ = 0;   // previous valid tap interval (0 = none): the agreement check
  bool     tap_accept_one_ = false;
  uint32_t f1_down_ms_   = 0;     // current tap press start (own timestamp)
  float    k2_last_      = 0.f;   // last seen raw knob (move detector)
  bool     k2_seeded_    = false;
  float    k2f_          = 0.f;   // the knob through the follow deadband
  bool     k2f_seeded_   = false;

  // ---- FREE-RUN ------------------------------------------------------------------
  // Control -> audio: the knob is in the zone now.
  volatile bool k2_free_ = false;
  // Audio -> control: a free capture decided; T is to become free_post_Q_.
  // The control thread adopts it (free_period_) and acks with the same seq.
  volatile size_t   free_post_Q_   = 0;
  volatile uint32_t free_post_seq_ = 0;  // audio writes
  volatile uint32_t free_ack_seq_  = 0;  // control writes, after period_ / free_in_force_
  uint32_t free_seen_seq_ = 0;           // control: last seq adopted
  size_t   free_period_   = 0;           // control: the free T (0 = none); tap > free > knob
  float    free_k2_ref_   = 0.f;         // control: zone-collapsed knob at the adoption
  volatile bool free_in_force_ = false;  // control: period_ IS the free T right now
  bool     pins_held_ = false;           // audio: pins are set (released by TakePinRelease)
};
