#pragma once

#include <cmath>

#include "constants.h"
#include "pitch_tracker.h"

#if NT3_TRACK_POLY

// Voice matcher for multi-dip poly tracking (docs/MULTI_DIP_TRACKING.md).
// Consumes the tracker's per-hop dip candidates (GetPoly*) and manages up to
// TRACK_POLY_VOICES stable voices with hop-to-hop continuity: matched dips
// glide (slew-limited), unmatched voices release (pitch held) and can be
// revived by a nearby dip — that's what carries voices through slap/pop/muted
// transients — and new dips spawn into free slots with an attack fade.
//
// Loudness policy: salience is LP-smoothed at hop rate and mapped through a
// power law into a per-voice gain target — never raw per-hop salience (that's
// the beat-rate tremolo of close dyads). Spawn/kill is a salience hysteresis
// (born above SPAWN_SAL, released below KILL_SAL). The consumer multiplies
// Gain(i) with its own envelope/VCA level.
//
// Threading: Update() runs in the main loop (right after
// PitchTracker::Update()); the audio callback calls TickGains() once per
// sample and reads Midi()/Gain(). Same benign-race float handoff as the
// tracker's mono outputs.
class PolyVoices {
 public:
  void Init(float sample_rate) {
    // Hop cadence in seconds: TRACK_HOP decimated samples per YIN run
    // (5.33 ms on both instrument profiles).
    float hop_s = static_cast<float>(TRACK_HOP * TRACK_DEC) / sample_rate;
    sal_lp_k_  = 1.f - expf(-hop_s * 1000.f / TRACK_POLY_SAL_LP_MS);
    sal_rel_k_ = 1.f - expf(-hop_s * 1000.f / TRACK_POLY_RELEASE_MS);
    atk_k_     = 1.f - expf(-1000.f / (TRACK_POLY_ATTACK_MS * sample_rate));
    rel_k_     = 1.f - expf(-1000.f / (TRACK_POLY_RELEASE_MS * sample_rate));
    for (int i = 0; i < TRACK_POLY_VOICES; i++) v_[i] = Voice{};
  }

  // Main loop, after tracker.Update(). Runs the match exactly once per hop.
  void Update(PitchTracker& t) {
    if (!t.ConsumeYinRan()) return;

    int   nc = t.GetPolyCount();
    float cand_midi[TRACK_POLY_VOICES];
    float cand_sal[TRACK_POLY_VOICES];
    for (int c = 0; c < nc; c++) {
      cand_midi[c] = t.GetPolyMidi(c);
      cand_sal[c]  = t.GetPolySalience(c);
    }

    bool cand_used[TRACK_POLY_VOICES] = {};
    bool matched[TRACK_POLY_VOICES]   = {};

    // Greedy globally-nearest matching within ±MATCH_ST semitones. Releasing
    // voices participate too — a dip near a fading voice's pitch revives it.
    for (;;) {
      float best_dist = TRACK_POLY_MATCH_ST;
      int   bv = -1, bc = -1;
      for (int i = 0; i < TRACK_POLY_VOICES; i++) {
        if (matched[i] || !Alive(i)) continue;
        for (int c = 0; c < nc; c++) {
          if (cand_used[c]) continue;
          float dist = fabsf(cand_midi[c] - v_[i].midi);
          if (dist <= best_dist) {
            best_dist = dist;
            bv = i;
            bc = c;
          }
        }
      }
      if (bv < 0) break;
      matched[bv]    = true;
      cand_used[bc]  = true;
      Voice& v = v_[bv];
      // Slew-limited glide: bends/slides come through, and an unresolved
      // close-dyad's wandering dip becomes a slow drift instead of a warble.
      float dmidi = cand_midi[bc] - v.midi;
      if (dmidi >  TRACK_POLY_SLEW_ST) dmidi =  TRACK_POLY_SLEW_ST;
      if (dmidi < -TRACK_POLY_SLEW_ST) dmidi = -TRACK_POLY_SLEW_ST;
      v.midi += dmidi;
      v.sal  += sal_lp_k_ * (cand_sal[bc] - v.sal);
      v.on    = v.sal >= TRACK_POLY_KILL_SAL;
    }

    // Unmatched voices: hold pitch, decay salience at release rate; below
    // KILL_SAL the voice stops sounding (gain target 0) but its slot is only
    // reused once the audio-rate gain has actually faded out.
    for (int i = 0; i < TRACK_POLY_VOICES; i++) {
      if (matched[i] || !Alive(i)) continue;
      Voice& v = v_[i];
      v.sal += sal_rel_k_ * (0.f - v.sal);
      if (v.sal < TRACK_POLY_KILL_SAL) v.on = false;
    }

    // Unused candidates above SPAWN_SAL take a free (faded-out) slot. The
    // attack fade doubles as the debounce for one-hop transient dips.
    for (int c = 0; c < nc; c++) {
      if (cand_used[c] || cand_sal[c] < TRACK_POLY_SPAWN_SAL) continue;
      int   slot  = -1;
      float quiet = 1e9f;
      for (int i = 0; i < TRACK_POLY_VOICES; i++) {
        if (Alive(i)) continue;
        if (v_[i].gain < quiet) {
          quiet = v_[i].gain;
          slot  = i;
        }
      }
      if (slot < 0) break;
      Voice& v = v_[slot];
      v.on   = true;
      v.midi = cand_midi[c];
      v.sal  = cand_sal[c];
    }

    // Gain targets (hop rate is plenty — TickGains smooths per sample).
    for (int i = 0; i < TRACK_POLY_VOICES; i++) {
      Voice& v = v_[i];
      float  s = v.on ? v.sal : 0.f;
      v.target = (TRACK_POLY_GAIN_EXP == 1.f) ? s : powf(s, TRACK_POLY_GAIN_EXP);
    }
  }

  // Audio callback: advance all voice gains one sample (asymmetric
  // attack/release one-pole toward the hop-rate target).
  void TickGains() {
    for (int i = 0; i < TRACK_POLY_VOICES; i++) {
      Voice& v = v_[i];
      float  k = (v.target > v.gain) ? atk_k_ : rel_k_;
      v.gain += k * (v.target - v.gain);
    }
  }

  float Midi(int i) const { return v_[i].midi; }   // per-voice pitch (unrounded)
  float Gain(int i) const { return v_[i].gain; }   // salience gain, 0..1
  // Sounding or still fading — consumers can skip silent voices.
  bool Audible(int i) const { return Alive(i); }

 private:
  struct Voice {
    bool  on     = false;  // spawned and not yet killed (logical state)
    float midi   = 36.f;   // held while releasing — revival keeps continuity
    float sal    = 0.f;    // hop-rate smoothed salience
    float target = 0.f;    // gain target (main loop writes)
    float gain   = 0.f;    // per-sample smoothed gain (audio thread writes)
  };

  static constexpr float kGainFloor = 1e-3f;
  bool Alive(int i) const { return v_[i].on || v_[i].gain > kGainFloor; }

  Voice v_[TRACK_POLY_VOICES];
  float sal_lp_k_  = 0.f;
  float sal_rel_k_ = 0.f;
  float atk_k_     = 0.f;
  float rel_k_     = 0.f;
};

#endif  // NT3_TRACK_POLY
