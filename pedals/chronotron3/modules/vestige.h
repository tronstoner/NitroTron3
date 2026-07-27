#pragma once
//
// vestige — dynamic looper / freeze (grain-based).  SW3 UP.
// Spec: docs/ChronoTron3/dynamic-looper-concept.md
//
// STAGE-1 discovery implementation. Grain-based poly-looper + freeze macro:
//   FS2  = main engage (manual: record while held; auto: record-arm toggle).
//   FS1  = stop (tap = mute/pause · hold = clear all).
//   SW1  = capture mode (UP manual · MID continuous-auto · DOWN → manual, TBD).
//   K1   = voice count / topology (CCW 6 voices … noon 1 voice … CW frippertronics).
//   K2   = auto-capture threshold (spare in manual).
//   K3   = smoothness macro (looper CCW → freeze CW).
//   K4   = texture (bipolar: tape saturation CCW ↔ decimation/crush CW).
//   K5   = loop fade in/out time (CCW instant → CW 3 s).
//   K6   = mix (shell-owned).
//
// Reuses the core grain engine (grain_voice.h / ring_buffer.h). Playback is a
// shared pool of GrainVoice objects; each grain is tagged with the RingBuffer it
// reads from, so any number of loop-voices share the pool with bounded CPU.
//
// Storage: one SDRAM slab per slot (6 voiced + 1 frippertronics + 1 record
// scratch). Recording writes directly into the record scratch slab (voiced) so
// it never evicts a playing loop; commit copies the scratch into a target slot.
// Each slot's RingBuffer views the same memory
// for grain reads. A wrap-guard copy of the loop head sits after the loop end so
// grains that read across the loop boundary stay seamless.
//
// Requires daisy.h + hothouse.h + control_surface.h + knob_map.h included first.
//
#include "module.h"
#include "vestige_constants.h"
#include "grain_voice.h"   // core/blocks — pulls in ring_buffer.h
#include <cmath>
#include <cstring>         // memcpy (commit copies record scratch → target slot)

// ---------------------------------------------------------------------------
// SDRAM storage — one slab per slot. File scope (single TU) is safe here.
// Layout per slab: [0 .. loop_len)  = captured loop
//                  [loop_len .. +GUARD) = copy of the loop head (wrap-guard)
// ---------------------------------------------------------------------------
static float DSY_SDRAM_BSS vestige_slab[VESTIGE_SLOTS][VESTIGE_VOICE_CAP];

// xorshift32 RNG for grain scatter (audio-thread only).
static uint32_t vestige_rng = 0x1234567u;
static inline float VestigeRand() {
  vestige_rng ^= vestige_rng << 13;
  vestige_rng ^= vestige_rng >> 17;
  vestige_rng ^= vestige_rng << 5;
  return static_cast<float>(vestige_rng) / 4294967295.f;
}

static inline float VestigeClamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

class Vestige : public Module {
 public:
  // -------------------------------------------------------------------------
  void Init(float sr) override {
    sr_ = sr;
    frip_od_coef_ = 1.f - expf(-1.f / (VESTIGE_FRIP_OD_RAMP_S * sr_));
    steal_inc_ = 1.f / (VESTIGE_STEAL_RELEASE_S * sr_);   // fast-release step for stolen voices
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      ring_[s].Init(vestige_slab[s], VESTIGE_VOICE_CAP);  // memsets the slab
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      active_[s]   = false;
      age_[s]      = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;
      fade_target_[s] = 0.f;
      fade_phase_[s] = 0.f; fade_from_[s] = 0.f;
      if (s < VESTIGE_VOICE_SLABS) { dying_[s] = false; stolen_[s] = false; }
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++) { grain_src_[g] = &ring_[0]; grain_slot_[g] = 0; }
    // Sensible defaults so Process is silent before the first Controls pass.
    grain_len_    = VESTIGE_CCW_GRAIN_LEN;
    overlap_      = VESTIGE_CCW_OVERLAP;
    spray_        = 0.f;
    jitter_       = 0.f;
    k3_mode_         = kLooper;
    scrub_back_frac_ = 0.f;
    freeze_pos_frac_ = 0.f;
    k3_amt_          = 0.f;
    target_voices_ = 1;
    frip_head_ = 0.f;
  }

  void Activate() override {
    // Material persists across mode switches; nothing to reset.
  }
  void Deactivate() override {
    // Recording cannot straddle a mode switch — drop any in-flight capture.
    recording_ = false;
    rec_full_  = false;
    commit_pending_ = false; pending_len_ = 0; overhang_left_ = 0;
    frip_od_gain_ = 0.f; frip_od_target_ = 0.f; frip_stop_pending_ = false;
    frip_head_ = 0.f;
  }

  // -------------------------------------------------------------------------
  // Control-rate (~10 ms). Footswitch policy, knob mapping, topology, LEDs.
  // -------------------------------------------------------------------------
  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));
    const float k5 = RemapKnob(cs.Knob(4));
    const int   sw1 = cs.Switch(0);        // 0=UP manual, 1=MID auto, 2=DOWN TBD
    const int   sw2 = cs.Switch(1);        // dry routing: 0=UP clean · 1=MID auto-cut · 2=DOWN off
    const float k6  = RemapKnob(cs.Knob(5)); // looper output volume (vestige owns the mix)
    const FootswitchEvent f1 = cs.Foot(0); // FS1 = stop
    const FootswitchEvent f2 = cs.Foot(1); // FS2 = engage

    blink_++;

    // ---- Topology (K1): voice count / frippertronics -----------------------
    //   CCW..NOON_LO : voiced, 6 voices (full CCW) → 1 voice (noon)
    //   NOON_LO..HI  : voiced, 1 voice (padded noon)
    //   NOON_HI..CW  : frippertronics, decay 0.40 (just past noon, ~1 repeat) → 1.0 infinite (full CW)
    bool want_frip = (k1 > VESTIGE_K1_NOON_HI);
    if (want_frip && !fripp_mode_) EnterFrippertronics();
    if (!want_frip && fripp_mode_) LeaveFrippertronics();
    fripp_mode_ = want_frip;

    if (!fripp_mode_) {
      if (k1 < VESTIGE_K1_NOON_LO) {
        float pos = k1 / VESTIGE_K1_NOON_LO;   // 0 (full CCW) → 1 (at noon band)
        int tv = 1 + (int)lroundf((1.f - pos) * (float)(VESTIGE_MAX_VOICES - 1));
        if (tv < 1) tv = 1;
        if (tv > VESTIGE_MAX_VOICES) tv = VESTIGE_MAX_VOICES;
        target_voices_ = tv;
      } else {
        target_voices_ = 1;                    // padded noon = 1 voice
      }
      EvictToTarget();       // reducing K1 evicts oldest-first, live
      UpdateVoicedGains();    // fixed age-ramp fade (K5 no longer affects it)
    } else {
      gain_[VESTIGE_FRIP_SLOT] = 1.f;
      float cw = (k1 - VESTIGE_K1_NOON_HI) / (1.f - VESTIGE_K1_NOON_HI);
      // Reversed travel: just past noon = shortest decay (barely repeats),
      // full CW = infinite sustain.
      frip_decay_ = Mapf(cw, VESTIGE_FRIP_DECAY_MIN, VESTIGE_FRIP_DECAY_MAX);
    }

    // ---- K5 fade in/out: two bounded durations on one scale ----------------
    // Both are phase ramps that FINISH in their time (no one-pole tail): attack
    // = convex swell over atk_s, release = concave "dies-away" over rel_s.
    float atk_s = Mapf(k5, 0.f, VESTIGE_FADE_ATTACK_MAX_S);
    float rel_s = Mapf(k5, 0.f, VESTIGE_FADE_RELEASE_MAX_S);
    atk_inc_ = (atk_s < 1e-4f) ? 1.f : 1.f / (atk_s * sr_);
    rel_inc_ = (rel_s < 1e-4f) ? 1.f : 1.f / (rel_s * sr_);

    // ---- K3 smoothness macro ----------------------------------------------
    const float s = k3;                    // 0 = looper (CCW), 1 = freeze (CW)
    float glen_f = Mapf(s, (float)VESTIGE_CCW_GRAIN_LEN, (float)VESTIGE_CW_GRAIN_LEN);
    grain_len_ = (size_t)glen_f;
    if (grain_len_ < VESTIGE_GRAIN_MIN_LEN) grain_len_ = VESTIGE_GRAIN_MIN_LEN;
    overlap_ = Mapf(s, VESTIGE_CCW_OVERLAP, VESTIGE_CW_OVERLAP);
    spray_  = s * (float)VESTIGE_FREEZE_SPRAY;
    jitter_ = s * VESTIGE_FREEZE_JITTER;
    // Position/direction: a small CCW zone plays the loop forward; above it the
    // head auto-scrubs BACKWARD, decelerating to a deterministic freeze anchored
    // toward the END. Anchor + deceleration scale with K3 travel.
    k3_amt_ = s;                  // first-grain attack softening toward freeze
    // Position / direction across the K3 travel:
    //   CCW zone  → normal forward loop.
    //   zone→noon → backward auto-scrub, decelerating to a HALT at noon.
    //   noon→CW   → frozen; the freeze point sweeps the WHOLE buffer, 0 → END (live).
    if (s < VESTIGE_K3_LOOP_ZONE) {
      k3_mode_ = kLooper;
    } else if (s < 0.5f) {
      k3_mode_ = kScrub;
      float t2 = (s - VESTIGE_K3_LOOP_ZONE) / (0.5f - VESTIGE_K3_LOOP_ZONE);
      scrub_back_frac_ = 1.f - t2;          // ~1× just off CCW → 0 at noon
    } else {
      k3_mode_ = kFreeze;
      scrub_back_frac_ = 0.f;
      freeze_pos_frac_ = (s - 0.5f) * 2.f;  // 0 = centre (noon) → 1 = end (CW)
    }

    // ---- K4 texture (bipolar) ---------------------------------------------
    float k4c = k4 - 0.5f;
    if (fabsf(k4c) < VESTIGE_TEX_DEADZONE) {
      tape_amt_ = 0.f; digi_amt_ = 0.f;
    } else if (k4c < 0.f) {                // analogue / tape side
      float a = (-k4c - VESTIGE_TEX_DEADZONE) / (0.5f - VESTIGE_TEX_DEADZONE);
      tape_amt_ = VestigeClamp(a, 0.f, 1.f);
      digi_amt_ = 0.f;
    } else {                               // digital / decimate side
      float d = (k4c - VESTIGE_TEX_DEADZONE) / (0.5f - VESTIGE_TEX_DEADZONE);
      digi_amt_ = VestigeClamp(d, 0.f, 1.f);
      tape_amt_ = 0.f;
    }
    tape_drive_  = 1.f + tape_amt_ * VESTIGE_TAPE_DRIVE_MAX;
    tape_makeup_ = 1.f / (0.5f + 0.5f * tape_drive_);  // color, not boost
    decim_hold_ = 1.f + digi_amt_ * (VESTIGE_DECIM_HOLD_MAX - 1.f);
    crush_bits_ = Mapf(digi_amt_, VESTIGE_CRUSH_BITS_HI, VESTIGE_CRUSH_BITS_LO);

    // ---- K2: auto-capture threshold ---------------------------------------
    auto_thresh_ = Mapf(k2, VESTIGE_AUTO_THRESH_MIN, VESTIGE_AUTO_THRESH_MAX);

    // ---- FS1 = stop (tap = mute/pause · hold = clear) ----------------------
    if (f1.down && !clear_latched_ && f1.held_ms >= VESTIGE_FS1_CLEAR_HOLD_MS) {
      ClearAll();
      clear_latched_ = true;
      flash_ = VESTIGE_FLASH_TICKS;
    }
    if (f1.falling) {
      if (!clear_latched_ && f1.held_ms <= VESTIGE_FS1_TAP_MAX_MS) {
        muted_ = !muted_;   // tap: toggle mute (fades out/in over K5 time)
        for (int s = 0; s < VESTIGE_SLOTS; s++)
          // Don't touch dying voices — resurrecting their fade would strand them
          // active forever (they'd never hit the free condition).
          if (active_[s] && !(s < VESTIGE_VOICE_SLABS && dying_[s]))
            SetFade(s, muted_ ? 0.f : 1.f);
      }
      clear_latched_ = false;
    }

    // ---- FS2 = main engage -------------------------------------------------
    // FS2 always means "record"; starting a record resumes a paused loop
    // (StartRecording → ResumeFromMute). To resume WITHOUT recording, tap FS1.
    const bool auto_mode = (sw1 == 1);
    if (f2.rising) {
      if (auto_mode) {
        auto_armed_ = !auto_armed_;   // continuous-auto: record-arm toggle
      } else {
        StartRecording();    // manual (SW1 UP) and DOWN (TBD → manual)
      }
    }
    if (f2.falling) {
      if (!auto_mode && recording_) {
        EndRecording();   // set loop end, record the seam overhang, then commit
      }
    }

    // ---- Continuous-auto capture state machine -----------------------------
    if (auto_mode && auto_armed_) {
      RunAutoCapture();
    } else if (recording_ && auto_mode) {
      // Disarmed mid-phrase → close it out.
      EndRecording();
    }

    // ---- Recording auto-stop (buffer full) --------------------------------
    if (rec_full_) {
      rec_full_ = false;
      EndRecording();
    }

    // ---- Deferred commit (fires after the seam overhang is recorded) -------
    if (commit_pending_) {
      commit_pending_ = false;
      CommitRecording();
    }

    // ---- K6 looper volume + SW2 dry routing (vestige owns its output) ------
    // K6: 0 (CCW) → unity (noon) → boost (CW). Sets the looper's level only.
    k6_vol_ = (k6 < 0.5f)
                ? (k6 * 2.f)
                : (1.f + (k6 - 0.5f) * 2.f * (VESTIGE_LOOP_BOOST_MAX - 1.f));
    // SW2 dry (clean) routing:  UP = always on ·  MID = off while recording or
    // auto-armed ·  DOWN = off (loop only). Gain is smoothed in Process.
    dry_gain_ = 1.f;
    if (sw2 == 2) dry_gain_ = 0.f;
    else if (sw2 == 1 && (recording_ || auto_armed_)) dry_gain_ = 0.f;

    // ---- LEDs --------------------------------------------------------------
    UpdateLeds(led1, led2, auto_mode);
  }

  bool OwnsOutput() const override { return true; }

  // -------------------------------------------------------------------------
  // Audio-rate. Recording writes, grain scheduler, grain sum, texture.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // ---- Frippertronics loop head (single per-sample head) --------------
      // Advance it before record/playback so both reference the same position.
      if (fripp_mode_ && frip_len_ > 0) AdvanceFripHead();

      // ---- Input envelope (drives continuous-auto gate) -------------------
      float a = fabsf(x);
      env_ += VESTIGE_ENV_COEF * (a - env_);

      // ---- Recording ------------------------------------------------------
      if (recording_) {
        float* m = vestige_slab[rec_slot_];
        if (fripp_mode_ && frip_len_ > 0) {
          // Overdub (sound-on-sound) writes at the loop head, so the phrase
          // lands where it's played. Decay existing, add ramped input. The
          // input ramp (frip_od_gain_) declicks record in/out; the decay is
          // ramped WITH it (eff_decay: 1.0 when faded out → matches the
          // untouched loop, real decay at full overdub) so auto-record's
          // partial ducking has no amplitude step at its seams.
          size_t idx = (size_t)frip_head_;
          frip_od_gain_ += frip_od_coef_ * (frip_od_target_ - frip_od_gain_);
          float eff_decay = 1.f + (frip_decay_ - 1.f) * frip_od_gain_;
          m[idx] = m[idx] * eff_decay + x * frip_od_gain_;
          if (frip_stop_pending_ && frip_od_gain_ < 1e-3f) {
            frip_stop_pending_ = false;
            commit_pending_    = true;   // faded out → Controls commits (WriteGuard)
          }
        } else {
          // Linear capture (voiced, or frippertronics first pass). After the
          // record end (pending_len_ set) we keep writing a short overhang for
          // the seam crossfade, then flag commit.
          m[rec_idx_] = x;
          rec_idx_++;
          if (overhang_left_ > 0) {
            if (--overhang_left_ == 0) commit_pending_ = true;
          } else if (pending_len_ == 0 && rec_idx_ >= VESTIGE_LOOP_MAX_SAMPLES) {
            rec_full_ = true;
          }
        }
      }

      // ---- Grain scheduler + per-slot sum + fade envelope -----------------
      // Mute/unmute rides the per-slot fade (K5), so the scheduler runs even
      // while muted so the fade-out tail can play; fully-faded slots sum to 0.
      if (fripp_mode_) {
        ServiceSlot(VESTIGE_FRIP_SLOT);
      } else {
        for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) ServiceSlot(v);
      }
      float slot_sum[VESTIGE_SLOTS] = {0.f};
      for (int g = 0; g < VESTIGE_GRAINS; g++) {
        if (grains_[g].IsActive())
          slot_sum[grain_slot_[g]] += grains_[g].Process(*grain_src_[g]);
      }
      float y = 0.f;
      for (int s = 0; s < VESTIGE_SLOTS; s++) {
        // One duration-based fade for both directions. fade_phase_ ramps 0→1
        // over the (attack|release) time; fade_from_ is the gain the fade
        // started at, so an interrupted fade resumes smoothly with no jump.
        const bool rising = (fade_target_[s] > 0.5f);
        // Stolen (fast-released) voices fade out at steal_inc_, not the K5 rate.
        const bool fast = (s < VESTIGE_VOICE_SLABS && stolen_[s]);
        if (fade_phase_[s] < 1.f) {
          fade_phase_[s] += rising ? atk_inc_ : (fast ? steal_inc_ : rel_inc_);
          if (fade_phase_[s] > 1.f) fade_phase_[s] = 1.f;
        }
        const float p = fade_phase_[s];
        if (rising) {
          // Convex swell: slow start → steep approach to full.
          float sh = 1.f - cosf(0.5f * kVestigePi * p);
          fade_gain_[s] = fade_from_[s] + (1.f - fade_from_[s]) * sh;
        } else {
          // Fade-out = the swell played BACKWARDS (time-reverse of the attack
          // curve), so the fade matches the swell gesture.
          float sh = 1.f - sinf(0.5f * kVestigePi * p);
          fade_gain_[s] = fade_from_[s] * sh;
        }
        y += slot_sum[s] * fade_gain_[s];
        // Free a retired (dying) voiced slot once its fade-out has completed.
        if (s < VESTIGE_VOICE_SLABS && dying_[s] &&
            fade_target_[s] < 0.5f && fade_phase_[s] >= 1.f) {
          active_[s] = false; dying_[s] = false; stolen_[s] = false;
          loop_len_[s] = 0; gain_[s] = 0.f;
        }
      }

      // ---- Texture (K4) ---------------------------------------------------
      if (tape_amt_ > 0.001f) {
        // Tape saturation: tanh grit with makeup gain — colors, doesn't boost.
        float driven = tanhf(y * tape_drive_) * tape_makeup_;
        y = y * (1.f - tape_amt_) + driven * tape_amt_;
      }
      if (digi_amt_ > 0.001f) {
        // Sample-rate reduction + bit-crush → glitch / overrun mayhem.
        decim_phase_ += 1.f;
        if (decim_phase_ >= decim_hold_) {
          decim_phase_ -= decim_hold_;
          float step = powf(2.f, crush_bits_);
          decim_hold_val_ = floorf(y * step + 0.5f) / step;
        }
        y = y * (1.f - digi_amt_) + decim_hold_val_ * digi_amt_;
      }

      // Vestige owns its output: looper (y) at K6 volume + routed dry (x).
      // Both gains one-pole smoothed so K6 moves and the dry gate don't zip.
      k6_vol_s_   += (k6_vol_   - k6_vol_s_)   * VESTIGE_ROUTING_SMOOTH;
      dry_gain_s_ += (dry_gain_ - dry_gain_s_) * VESTIGE_ROUTING_SMOOTH;
      wet[i] = dry_gain_s_ * x + k6_vol_s_ * y;
    }
  }

 private:
  // -------------------------------------------------------------------------
  // Grain scheduling for one slot.
  // -------------------------------------------------------------------------
  // Effective grain length for a slot: the K3 macro length, capped so it never
  // exceeds the loop or the wrap-guard. (Short loops → short grains → the loop
  // simply repeats continuously, which is the EHX-style short-sample freeze.)
  size_t SlotGrainLen(size_t L) const {
    size_t glen = grain_len_;
    if (glen > L) glen = L;
    if (glen > VESTIGE_GUARD_SAMPLES) glen = VESTIGE_GUARD_SAMPLES;
    if (glen < VESTIGE_GRAIN_MIN_LEN) glen = (L < VESTIGE_GRAIN_MIN_LEN) ? L : VESTIGE_GRAIN_MIN_LEN;
    return glen;
  }

  // Advance the single frippertronics loop head one sample. K3 sets its motion:
  // forward (looper), backward (scrub), or pinned to the live freeze point.
  // Grains read from it and recording writes to it → one head, phrases land
  // where played. Phase-locked forward grains COLA-sum to a clean tape stream,
  // so CCW is a seamless loop with no straight-head special case.
  void AdvanceFripHead() {
    const int    s = VESTIGE_FRIP_SLOT;
    const size_t L = frip_len_;
    if (k3_mode_ == kFreeze) {
      size_t glen = SlotGrainLen(L);
      float end_anchor = (float)L - ((float)glen + spray_);
      if (end_anchor < 0.f) end_anchor = 0.f;
      frip_head_ = end_anchor * freeze_pos_frac_;          // pinned freeze point
    } else {
      float rate = (k3_mode_ == kLooper) ? 1.f : -scrub_back_frac_;
      frip_head_ += rate;
      while (frip_head_ >= (float)L) frip_head_ -= (float)L;
      while (frip_head_ < 0.f)      frip_head_ += (float)L;
    }
    play_pos_[s] = (size_t)frip_head_;
  }

  void ServiceSlot(int s) {
    const size_t L = loop_len_[s];
    if (!active_[s] || L < VESTIGE_GRAIN_MIN_LEN) return;
    if (--timer_[s] > 0) return;

    size_t glen = SlotGrainLen(L);
    const bool is_frip = (s == VESTIGE_FRIP_SLOT);

    // Freeze (noon→CW): pin the head to the LIVE freeze point before emitting.
    // Frip's head is pinned per-sample in AdvanceFripHead(); voiced slots pin
    // here.
    if (!is_frip && k3_mode_ == kFreeze) {
      float end_anchor = (float)L - ((float)glen + spray_);   // deepest safe point
      if (end_anchor < 0.f) end_anchor = 0.f;
      play_pos_[s] = (size_t)(end_anchor * freeze_pos_frac_);
    }

    EmitGrain(s, glen);

    // Hop = grain/overlap, so overlap stays >= 1 (continuous output).
    size_t hop = (size_t)((float)glen / overlap_);
    if (hop < VESTIGE_MIN_INTERVAL) hop = VESTIGE_MIN_INTERVAL;

    // Head advance: voiced slots step play_pos per emit here; frip's head
    // advances per-sample in AdvanceFripHead(), so skip it.
    if (!is_frip) {
      if (k3_mode_ == kScrub) {
        // Backward auto-scrub (CCW→noon); speed per K3, 0 at noon. Live.
        float ppos = (float)play_pos_[s] - scrub_back_frac_ * (float)hop;
        ppos = fmodf(ppos, (float)L); if (ppos < 0.f) ppos += (float)L;
        play_pos_[s] = (size_t)ppos;
      } else if (k3_mode_ == kLooper) {
        play_pos_[s] += hop;                                  // forward 1× loop
        while (play_pos_[s] >= L) play_pos_[s] -= L;
      }
      // kFreeze: head pinned above; no advance.
    }

    // Reset the timer, with a little jitter (small so the freeze stays steady).
    float j = (VestigeRand() * 2.f - 1.f) * jitter_ * 0.6f;
    int itv = (int)((float)hop * (1.f + j));
    if (itv < (int)VESTIGE_MIN_INTERVAL) itv = (int)VESTIGE_MIN_INTERVAL;
    timer_[s] = itv;
  }

  void EmitGrain(int s, size_t glen) {
    int g = -1;
    for (int k = 0; k < VESTIGE_GRAINS; k++) {
      int idx = (next_grain_ + k) % VESTIGE_GRAINS;
      if (!grains_[idx].IsActive()) { g = idx; next_grain_ = (idx + 1) % VESTIGE_GRAINS; break; }
    }
    if (g < 0) return;   // pool exhausted → drop (glitch, acceptable)

    const size_t L = loop_len_[s];
    // Position = the read head + a NARROW phasing spray (not a full-buffer
    // scatter): overlapping grains around the (frozen at freeze) head phase
    // against each other for a consistent, evolving freeze.
    float slot_spray = spray_;
    if (L < VESTIGE_SHORT_LEN) {                 // short loop: keep spray from wrapping
      float cap = (float)L * 0.125f;
      if (slot_spray > cap) slot_spray = cap;
    }
    float off = (VestigeRand() * 2.f - 1.f) * slot_spray;
    float pos = fmodf((float)play_pos_[s] + off, (float)L);
    if (pos < 0.f) pos += (float)L;
    size_t posi = (size_t)pos;

    // Delay = distance from the (frozen) write head back to this absolute index.
    const size_t wp  = ring_[s].GetWritePos();
    const size_t cap = VESTIGE_VOICE_CAP;
    size_t delay = (wp + cap - posi) % cap;

    grain_src_[g]  = &ring_[s];
    grain_slot_[g] = s;
    // Overlap gain-compensation: Hann overlap-add is COLA (flat) only at 2×;
    // at 3× the sum ripples ~1.5×, so scale by 2/overlap to hold level constant.
    float ov_comp = 2.f / overlap_;
    // rate 1.0, forward, full Hann (alpha=1) so overlap-add stays click-free.
    // First grain of a fresh loop: (near-)instant attack so playback starts
    // immediately, softened proportionally toward freeze (k3_amt_) to avoid a
    // sharp attack-repeat there. attack_scale 0 = instant, 1 = normal fade-in.
    float atk_scale = first_grain_[s] ? k3_amt_ : 1.f;
    grains_[g].Trigger(ring_[s], delay, glen, false, 1.f, gain_[s] * ov_comp, 1, 1.0f,
                       atk_scale);
    first_grain_[s] = false;
  }

  // -------------------------------------------------------------------------
  // Recording lifecycle
  // -------------------------------------------------------------------------
  // Resume paused loops (fade back in from the current gain). No-op if playing.
  void ResumeFromMute() {
    if (!muted_) return;
    muted_ = false;
    for (int s = 0; s < VESTIGE_SLOTS; s++)
      if (active_[s] && !(s < VESTIGE_VOICE_SLABS && dying_[s]))
        SetFade(s, 1.f);   // don't resurrect dying voices
  }

  void StartRecording() {
    if (recording_) return;
    if (fripp_mode_) {
      rec_slot_ = VESTIGE_FRIP_SLOT;
      rec_idx_  = (frip_len_ > 0) ? play_pos_[VESTIGE_FRIP_SLOT] : 0; // overdub syncs to playback
      if (frip_len_ > 0) {          // overdub: fade the summed input in (declick)
        frip_od_gain_ = 0.f; frip_od_target_ = 1.f; frip_stop_pending_ = false;
      }
    } else {
      // Record into a dedicated scratch slot so recording never evicts/mutes a
      // playing voice. Target slot is chosen at commit time.
      rec_slot_ = VESTIGE_REC_SLOT;
      rec_idx_  = 0;
      active_[rec_slot_]   = false;  // scratch slot is never itself audible
      loop_len_[rec_slot_] = 0;
    }
    recording_ = true;
  }

  static constexpr float kVestigePi = 3.14159265358979323846f;

  // Begin a fade toward `target` (0 or 1) for slot s. Captures the current gain
  // as the fade's start point and resets its phase, so an interrupted fade
  // resumes smoothly (no jump) regardless of direction.
  void SetFade(int s, float target) {
    fade_target_[s] = target;
    fade_from_[s]   = fade_gain_[s];
    fade_phase_[s]  = 0.f;
  }

  // Minimal seam crossfade length (samples), scaled down for tiny loops.
  static size_t SeamXfadeLen(size_t L) {
    size_t xf = VESTIGE_SEAM_XFADE_MAX;
    if (xf > L / 2) xf = L / 2;
    return xf;
  }

  // Record end (FS2 release / phrase end / buffer full): set the loop length
  // NOW (timing-exact), then keep recording a short overhang for the seam
  // crossfade. Commit fires once the overhang is captured (commit_pending_).
  void EndRecording() {
    if (!recording_) return;
    if (fripp_mode_) {
      // Overdub: fade the input out, then commit once silent (declick record-out).
      if (frip_len_ > 0) { frip_od_target_ = 0.f; frip_stop_pending_ = true; return; }
      CommitRecording();                                // first pass: nothing to fade
      return;
    }
    size_t L = rec_idx_;
    if (L < VESTIGE_MIN_LOOP_SAMPLES) L = VESTIGE_MIN_LOOP_SAMPLES;
    if (L > VESTIGE_LOOP_MAX_SAMPLES) L = VESTIGE_LOOP_MAX_SAMPLES;
    pending_len_ = L;
    size_t target = L + SeamXfadeLen(L);              // record up to here
    if (target > VESTIGE_VOICE_CAP) target = VESTIGE_VOICE_CAP;
    if (rec_idx_ >= target) commit_pending_ = true;   // already have enough
    else overhang_left_ = (int)(target - rec_idx_);
  }

  void CommitRecording() {
    if (!recording_) return;
    recording_ = false;
    ResumeFromMute();   // record END unpauses the retained loops

    // ---- Frippertronics path (unchanged: records in-place into FRIP_SLOT) --
    if (fripp_mode_) {
      const int s = rec_slot_;   // == VESTIGE_FRIP_SLOT
      if (frip_len_ > 0) {
        // Overdub pass ended — refresh the wrap-guard, keep playing.
        WriteGuard(s, frip_len_);
        return;
      }
      size_t L = rec_idx_;
      if (L < VESTIGE_MIN_LOOP_SAMPLES) L = VESTIGE_MIN_LOOP_SAMPLES;
      if (L > VESTIGE_LOOP_MAX_SAMPLES) L = VESTIGE_LOOP_MAX_SAMPLES;
      WriteGuard(s, L);
      loop_len_[s] = L;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      active_[s]   = true;
      age_[s]      = ++age_counter_;
      frip_len_    = L;
      frip_head_   = 0.f;         // play the fresh loop from the top
      StartFadeIn(s);
      return;
    }

    // ---- Voiced path: choose target NOW, copy scratch → target -------------
    size_t L = (pending_len_ > 0) ? pending_len_ : rec_idx_;
    if (L < VESTIGE_MIN_LOOP_SAMPLES) L = VESTIGE_MIN_LOOP_SAMPLES;
    if (L > VESTIGE_LOOP_MAX_SAMPLES) L = VESTIGE_LOOP_MAX_SAMPLES;
    // Copy the loop PLUS the recorded overhang so WriteGuard can crossfade it.
    size_t copy = L + SeamXfadeLen(L);
    if (copy > VESTIGE_VOICE_CAP) copy = VESTIGE_VOICE_CAP;

    // Prefer a FREE voiced slot so the outgoing voice can fade out in its OWN
    // buffer (no slab reuse under in-flight grains). Single-voice mode always
    // has spares, so this is where the crossfade lives. Only a full pool (6
    // active) forces reuse of the oldest slab (declick handled in Commit B).
    int target = FindFreeSlot();
    if (target < 0) { target = EvictOldest(); KillSlotGrains(target); }  // last resort

    memcpy(vestige_slab[target], vestige_slab[VESTIGE_REC_SLOT], copy * sizeof(float));
    WriteGuard(target, L);      // crossfades the overhang into the loop head
    loop_len_[target] = L;
    play_pos_[target] = 0;
    timer_[target]    = 0;      // fire the first grain immediately
    active_[target]   = true;
    dying_[target]    = false;
    age_[target]      = ++age_counter_;
    StartFadeIn(target);        // new loop swells in over K5

    // Retire the oldest live voice(s) beyond the target — graceful fade-out over
    // K5 (frees when silent). For single-voice this IS the crossfade: the old
    // loop fades out while the new swells in. With K5 CCW both are instant.
    while (CountLive() > target_voices_) {
      int o = OldestLive();
      if (o < 0 || o == target) break;
      StartDying(o);
    }
    // Bound total granulating voices to the CPU/grain ceiling: steal (fast-
    // release) the oldest tails beyond it. This is what keeps a long fade + fast
    // captures from piling up ~9 voices and overrunning the audio block.
    EnforceVoiceCap();
    pending_len_ = 0; overhang_left_ = 0;
    UpdateVoicedGains();
  }

  // Begin a fade-in for a slot: silent now, swelling to unity over K5 time.
  void StartFadeIn(int s) {
    fade_gain_[s] = 0.f;
    SetFade(s, 1.f);          // swell from silence
    first_grain_[s] = true;   // first grain of this fresh loop starts (near-)instantly
    // Position isn't anchored here: looper/scrub start from CommitRecording's
    // play_pos = 0; freeze pins its own live centre→end point in ServiceSlot.
  }

  // Build the wrap-guard so grains reading across the loop boundary continue
  // seamlessly. First xf samples = equal-power crossfade of the recorded
  // overhang m[L+k] (natural continuation of the loop tail) fading OUT into the
  // loop head m[k] fading IN — kills the seam click with no timing change. The
  // remainder is the loop repeated. (Fripp has no overhang → the crossfade
  // degenerates to the head copy, i.e. the old behaviour.)
  void WriteGuard(int s, size_t L) {
    float* m = vestige_slab[s];
    size_t xf = SeamXfadeLen(L);
    for (size_t k = 0; k < xf; k++) {
      float ov = m[L + k];
      float hd = m[k];
      float t  = (float)(k + 1) / (float)(xf + 1);
      m[L + k] = ov * cosf(t * 1.5707963f) + hd * sinf(t * 1.5707963f);
    }
    for (size_t k = xf; k < VESTIGE_GUARD_SAMPLES; k++) m[L + k] = m[k % L];
  }

  // Pick a voiced slot for a new capture: a free slot if under target, else
  // evict the oldest (FIFO) and reuse it.
  int EvictOldest() {
    int oldest = -1;
    uint32_t best = 0xFFFFFFFFu;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) {
      if (active_[v] && age_[v] < best) { best = age_[v]; oldest = v; }
    }
    if (oldest < 0) oldest = 0;
    active_[oldest]   = false;
    loop_len_[oldest] = 0;
    return oldest;
  }

  // Reduce active voiced count to target, oldest-first (live K1 control).
  // Live = active and not fading out. Dying voices still sound (fading) but no
  // longer count toward the target or the gain normalization.
  int CountLive() const {
    int n = 0;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) if (active_[v] && !dying_[v]) n++;
    return n;
  }
  int OldestLive() const {
    int oldest = -1; uint32_t best = 0xFFFFFFFFu;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++)
      if (active_[v] && !dying_[v] && age_[v] < best) { best = age_[v]; oldest = v; }
    return oldest;
  }
  int FindFreeSlot() const {
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) if (!active_[v] && !dying_[v]) return v;
    return -1;
  }
  // Retire a voice gracefully: keep it sounding but fade it out over K5, then
  // free it once silent (Process). Used for K1-reduce and the crossfade tail.
  void StartDying(int s) {
    if (s < 0 || dying_[s]) return;
    dying_[s] = true;
    SetFade(s, 0.f);   // release over K5
  }

  // Concurrency cap: count/find the oldest voice that is NOT already being
  // fast-released. Total granulating voices are bounded to VESTIGE_MAX_VOICES
  // (the pre-regression CPU/grain ceiling); the excess oldest gets stolen.
  int CountUnstolen() const {
    int n = 0;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) if (active_[v] && !stolen_[v]) n++;
    return n;
  }
  int OldestUnstolen() const {
    int oldest = -1; uint32_t best = 0xFFFFFFFFu;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++)
      if (active_[v] && !stolen_[v] && age_[v] < best) { best = age_[v]; oldest = v; }
    return oldest;
  }
  // Steal a voice: fast-release it (declicked) so its slab frees quickly.
  void StealVoice(int s) {
    if (s < 0) return;
    stolen_[s] = true;
    if (!dying_[s]) { dying_[s] = true; SetFade(s, 0.f); }
  }
  // Keep total granulating voices within the CPU/grain budget by stealing the
  // oldest not-already-stolen voice until the count is back under the cap.
  void EnforceVoiceCap() {
    while (CountUnstolen() > VESTIGE_MAX_VOICES) {
      int o = OldestUnstolen();
      if (o < 0) break;
      StealVoice(o);
    }
  }
  void KillSlotGrains(int s) {
    for (int g = 0; g < VESTIGE_GRAINS; g++)
      if (grain_slot_[g] == s) grains_[g] = GrainVoice{};   // deactivate
  }

  void EvictToTarget() {
    while (CountLive() > target_voices_) {
      int o = OldestLive();
      if (o < 0) break;
      StartDying(o);   // fade out over K5, free when silent
    }
  }

  // Age-ramped fade over the FIFO stack. rank r (0 = newest); linear gain
  // (N-r)/N, normalised 1/sqrt(N) for the stacking law. Fixed slope (K5 now
  // drives the loop fade envelope, not this age-fade).
  void UpdateVoicedGains() {
    // Level tracks the ACTUAL active-voice count. Age-ramp weights (newest = 1,
    // oldest = 1-d) are power-normalized as a SET so the total power equals a
    // single voice → the loop stays equally loud at any voice count (single is
    // no longer the loudest), while newer voices still sit above older ones.
    int n = 0;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) if (active_[v] && !dying_[v]) n++;
    const float d = VESTIGE_AGE_FADE_DEPTH;
    float sumsq = 0.f;
    for (int v = 0; v < VESTIGE_VOICE_SLABS; v++) {
      if (!active_[v]) { gain_[v] = 0.f; continue; }
      if (dying_[v]) continue;       // fading out: keep its frozen gain_
      int r = 0;   // rank among live voices: 0 = newest
      for (int w = 0; w < VESTIGE_VOICE_SLABS; w++)
        if (active_[w] && !dying_[w] && age_[w] > age_[v]) r++;
      float wr = (n > 1) ? (1.f - d * ((float)r / (float)(n - 1))) : 1.f;
      if (wr < 0.f) wr = 0.f;
      gain_[v] = wr;                 // stash weight; power-normalize below
      sumsq += wr * wr;
    }
    if (sumsq > 1e-9f) {
      const float norm = 1.f / sqrtf(sumsq);
      for (int v = 0; v < VESTIGE_VOICE_SLABS; v++)
        if (active_[v] && !dying_[v]) gain_[v] *= norm;
    }
  }

  // -------------------------------------------------------------------------
  // Frippertronics transitions (provisional — see Open items)
  // -------------------------------------------------------------------------
  void EnterFrippertronics() {
    // Seed the shared buffer from the newest voiced loop so playback continues.
    if (frip_len_ == 0) {
      int src = -1; uint32_t best = 0;
      for (int v = 0; v < VESTIGE_VOICE_SLABS; v++)
        if (active_[v] && age_[v] >= best) { best = age_[v]; src = v; }
      if (src >= 0) {
        size_t L = loop_len_[src];
        // Copy the loop PLUS its already-built wrap-guard, so the fripp seam is
        // the voiced loop's seamless seam. (Copying only [0,L) and re-running
        // WriteGuard here crossfaded the head against stale fripp-slab data →
        // a step at the wrap = the percussive "dang".)
        size_t copyN = L + VESTIGE_GUARD_SAMPLES;
        if (copyN > VESTIGE_VOICE_CAP) copyN = VESTIGE_VOICE_CAP;
        float* d = vestige_slab[VESTIGE_FRIP_SLOT];
        float* srcm = vestige_slab[src];
        for (size_t k = 0; k < copyN; k++) d[k] = srcm[k];
        frip_len_ = L;
      }
    }
    active_[VESTIGE_FRIP_SLOT]   = (frip_len_ > 0);
    loop_len_[VESTIGE_FRIP_SLOT] = frip_len_;
    play_pos_[VESTIGE_FRIP_SLOT] = 0;
    timer_[VESTIGE_FRIP_SLOT]    = 0;
    if (active_[VESTIGE_FRIP_SLOT]) StartFadeIn(VESTIGE_FRIP_SLOT);
  }

  void LeaveFrippertronics() {
    // Sweeping back keeps the loop but repopulates voices from new playing.
    active_[VESTIGE_FRIP_SLOT] = false;
  }

  // -------------------------------------------------------------------------
  // Continuous-auto capture: silence is the phrase delimiter.
  // -------------------------------------------------------------------------
  void RunAutoCapture() {
    const float open  = auto_thresh_;
    const float close = auto_thresh_ * VESTIGE_AUTO_HYST;
    const uint32_t now = daisy::System::GetNow();
    if (!recording_) {
      if (env_ > open) StartRecording();   // silence → sound: begin a phrase
    } else {
      if (env_ < close) {
        if (silence_since_ == 0) silence_since_ = now;
        else if (now - silence_since_ >= VESTIGE_AUTO_RELEASE_MS) {
          EndRecording();                  // sound → sustained silence: end + overhang
          silence_since_ = 0;
        }
      } else {
        silence_since_ = 0;
      }
    }
  }

  // -------------------------------------------------------------------------
  void ClearAll() {
    recording_ = false;
    commit_pending_ = false; pending_len_ = 0; overhang_left_ = 0;
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      active_[s]   = false;
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;   // silence immediately (no fade)
      fade_target_[s] = 0.f;
      fade_phase_[s] = 0.f; fade_from_[s] = 0.f;
      if (s < VESTIGE_VOICE_SLABS) { dying_[s] = false; stolen_[s] = false; }
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++) grains_[g] = GrainVoice{};
    frip_len_ = 0;
    frip_head_ = 0.f;
    muted_    = false;
    // NB: auto_armed_ is intentionally preserved — clearing the loop should not
    // disarm continuous-auto if it was armed.
  }

  // -------------------------------------------------------------------------
  // LED mapping (single-colour, blink states). Liberties taken — see report.
  //   led1 (PLAY/STOP): solid while playing · slow-blink while muted/paused · off
  //   led2 (RECORD):    solid while recording · fast-blink while auto-armed · off
  //   clear: both fast-blink together for a moment.
  // -------------------------------------------------------------------------
  void UpdateLeds(daisy::Led& led1, daisy::Led& led2, bool auto_mode) {
    const bool slow = ((blink_ / VESTIGE_BLINK_SLOW) & 1) != 0;
    const bool fast = ((blink_ / VESTIGE_BLINK_FAST) & 1) != 0;

    if (flash_ > 0) {   // clear-confirm flash overrides
      flash_--;
      float f = fast ? 1.f : 0.f;
      led1.Set(f); led2.Set(f);
      return;
    }

    bool has_content = false;
    for (int s = 0; s < VESTIGE_SLOTS; s++) if (active_[s]) has_content = true;

    // led1 — playback/stop status (correlates with FS1 = stop)
    if (muted_ && has_content) led1.Set(slow ? 1.f : 0.f);
    else if (has_content)      led1.Set(1.f);
    else                       led1.Set(0.f);

    // led2 — record status (correlates with FS2 = engage/record)
    if (recording_)            led2.Set(1.f);
    else if (auto_mode && auto_armed_) led2.Set(fast ? 1.f : 0.f);
    else                       led2.Set(0.f);
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  float sr_ = CT3_SAMPLE_RATE_HZ;

  // Grain pool (shared across all slots)
  GrainVoice       grains_[VESTIGE_GRAINS];
  const RingBuffer* grain_src_[VESTIGE_GRAINS];
  int              grain_slot_[VESTIGE_GRAINS] = {0};  // which slot emitted grain g
  int              next_grain_ = 0;

  // Per-slot loop state (slots 0..5 voiced, slot 6 frippertronics)
  RingBuffer ring_[VESTIGE_SLOTS];
  size_t     loop_len_[VESTIGE_SLOTS] = {0};
  size_t     play_pos_[VESTIGE_SLOTS] = {0};
  int        timer_[VESTIGE_SLOTS]    = {0};
  bool       active_[VESTIGE_SLOTS]   = {false};
  bool       dying_[VESTIGE_VOICE_SLABS]  = {false}; // voiced slot fading out → free when silent
  bool       stolen_[VESTIGE_VOICE_SLABS] = {false}; // dying voice being fast-released (voice-steal)
  float      steal_inc_ = 1.f;                        // fast-release phase step for stolen voices
  uint32_t   age_[VESTIGE_SLOTS]      = {0};
  float      gain_[VESTIGE_SLOTS]     = {0.f};
  bool       first_grain_[VESTIGE_SLOTS] = {false};  // next grain skips its fade-in
  uint32_t   age_counter_ = 0;

  // Per-slot loop fade envelope (K5): multiplier on each slot's summed output.
  float      fade_gain_[VESTIGE_SLOTS]   = {0.f};  // current smoothed gain
  float      fade_target_[VESTIGE_SLOTS] = {0.f};  // 0 (fade out) or 1 (fade in)
  float      fade_phase_[VESTIGE_SLOTS] = {0.f};   // progress of current fade (0..1)
  float      fade_from_[VESTIGE_SLOTS]  = {0.f};   // gain the current fade started at
  float      atk_inc_ = 1.f;                       // attack phase step (1/(atk_s*sr))
  float      rel_inc_ = 1.f;                       // release phase step (1/(rel_s*sr))
  // Output routing (K6 looper volume + SW2 dry gate) — vestige owns its mix.
  float      k6_vol_    = 1.f, k6_vol_s_   = 1.f;  // looper volume target / smoothed
  float      dry_gain_  = 1.f, dry_gain_s_ = 1.f;  // dry (clean) gain target / smoothed

  // Cached K3 grain-macro params (Controls → Process)
  size_t grain_len_    = VESTIGE_CCW_GRAIN_LEN;
  float  overlap_      = VESTIGE_CCW_OVERLAP;
  float  spray_        = 0.f;   // ± phasing spray (samples) around the head
  float  jitter_       = 0.f;   // scheduler timing jitter
  // K3 backward-scrub / deterministic end-freeze (Controls → ServiceSlot)
  enum K3Mode { kLooper = 0, kScrub = 1, kFreeze = 2 };
  K3Mode k3_mode_         = kLooper;
  float  scrub_back_frac_ = 0.f;  // backward scrub speed frac of hop (CCW→noon, →0 at noon)
  float  freeze_pos_frac_ = 0.f;  // freeze point: 0 = start (noon) → 1 = end (CW)
  float  k3_amt_          = 0.f;  // raw K3 (first-grain attack softening toward freeze)

  // Topology
  int  target_voices_ = 1;
  bool fripp_mode_    = false;
  size_t frip_len_    = 0;
  float  frip_decay_  = 1.f;
  // Single frippertronics loop head (per-sample): record writes to it and grains
  // read from it, so overdubs land exactly where they were played. K3 sets its
  // motion (forward / backward scrub / frozen). Voiced slots keep their own
  // per-emit play_pos_ — this is frip-only.
  float  frip_head_   = 0.f;
  // Frippertronics overdub declick: ramp the summed input in/out over a few ms
  // at record engage/disengage so the sound-on-sound add has no hard step.
  float  frip_od_gain_    = 0.f;   // current overdub input gain (0..1)
  float  frip_od_target_  = 0.f;   // 1 = fading in, 0 = fading out
  float  frip_od_coef_    = 1.f;   // per-sample one-pole ramp coef (set in Init)
  volatile bool frip_stop_pending_ = false;  // deferred commit: wait for fade-out

  // Texture (K4)
  float tape_amt_ = 0.f, digi_amt_ = 0.f;
  float tape_drive_ = 1.f, tape_makeup_ = 1.f, decim_hold_ = 1.f, crush_bits_ = 16.f;
  float decim_phase_ = 0.f, decim_hold_val_ = 0.f;

  // Recording
  volatile bool recording_ = false;
  volatile bool rec_full_  = false;
  int    rec_slot_ = 0;
  size_t rec_idx_  = 0;
  // Seam-crossfade overhang: loop end is set at EndRecording, then we record
  // `overhang_left_` more samples before committing (commit_pending_).
  volatile bool commit_pending_ = false;
  size_t pending_len_   = 0;
  int    overhang_left_ = 0;

  // Transport / capture
  bool     muted_        = false;
  bool     clear_latched_= false;
  bool     auto_armed_   = false;
  int      flash_        = 0;
  uint32_t silence_since_= 0;
  float    auto_thresh_  = VESTIGE_AUTO_THRESH_MIN;
  float    env_          = 0.f;

  int blink_ = 0;
};
