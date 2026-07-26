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
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++) { grain_src_[g] = &ring_[0]; grain_slot_[g] = 0; }
    // Sensible defaults so Process is silent before the first Controls pass.
    grain_len_    = VESTIGE_CCW_GRAIN_LEN;
    overlap_      = VESTIGE_CCW_OVERLAP;
    advance_rate_ = 1.f;
    spray_        = 0.f;
    jitter_       = 0.f;
    target_voices_ = 1;
  }

  void Activate() override {
    // Material persists across mode switches; nothing to reset.
  }
  void Deactivate() override {
    // Recording cannot straddle a mode switch — drop any in-flight capture.
    recording_ = false;
    rec_full_  = false;
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
    const FootswitchEvent f1 = cs.Foot(0); // FS1 = stop
    const FootswitchEvent f2 = cs.Foot(1); // FS2 = engage

    blink_++;

    // ---- Topology (K1): voice count / frippertronics -----------------------
    //   CCW..NOON_LO : voiced, 6 voices (full CCW) → 1 voice (noon)
    //   NOON_LO..HI  : voiced, 1 voice (padded noon)
    //   NOON_HI..CW  : frippertronics, decay 1.0 (just past noon) → 0.90 (full CW)
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
      frip_decay_ = Mapf(cw, VESTIGE_FRIP_DECAY_MAX, VESTIGE_FRIP_DECAY_MIN);
    }

    // ---- K5 loop fade in/out time → per-sample coefficient -----------------
    float fade_time_s = Mapf(k5, 0.f, VESTIGE_FADE_MAX_S);
    fade_coef_ = (fade_time_s < 1e-4f) ? 1.f
                                       : 1.f - expf(-1.f / (fade_time_s * sr_));

    // ---- K3 smoothness macro ----------------------------------------------
    const float s = k3;                    // 0 = looper (CCW), 1 = freeze (CW)
    float glen_f = Mapf(s, (float)VESTIGE_CCW_GRAIN_LEN, (float)VESTIGE_CW_GRAIN_LEN);
    grain_len_ = (size_t)glen_f;
    if (grain_len_ < VESTIGE_GRAIN_MIN_LEN) grain_len_ = VESTIGE_GRAIN_MIN_LEN;
    overlap_ = Mapf(s, VESTIGE_CCW_OVERLAP, VESTIGE_CW_OVERLAP);
    // Freeze is a time-stretch to a STANDSTILL, not a scatter: the read head
    // slows (advance 1×→0) and grains read a NARROW window around it so the
    // overlap phases against itself (consistent, evolving), not random jumps.
    advance_rate_ = 1.f - s;
    spray_  = s * (float)VESTIGE_FREEZE_SPRAY;
    jitter_ = s * VESTIGE_FREEZE_JITTER;

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
          if (active_[s]) fade_target_[s] = muted_ ? 0.f : 1.f;
      }
      clear_latched_ = false;
    }

    // ---- FS2 = main engage -------------------------------------------------
    const bool auto_mode = (sw1 == 1);
    if (f2.rising) {
      if (muted_) {
        muted_ = false;      // FS2 re-arm resumes the retained loops
        for (int s = 0; s < VESTIGE_SLOTS; s++)
          if (active_[s]) fade_target_[s] = 1.f;   // fade back in
        swallow_fs2_ = true; // consume this press; do not start a record
      } else if (auto_mode) {
        auto_armed_ = !auto_armed_;   // continuous-auto: record-arm toggle
      } else {
        StartRecording();    // manual (SW1 UP) and DOWN (TBD → manual)
      }
    }
    if (f2.falling) {
      if (swallow_fs2_) {
        swallow_fs2_ = false;
      } else if (!auto_mode && recording_) {
        CommitRecording();
      }
    }

    // ---- Continuous-auto capture state machine -----------------------------
    if (auto_mode && auto_armed_) {
      RunAutoCapture();
    } else if (recording_ && auto_mode) {
      // Disarmed mid-phrase → close it out.
      CommitRecording();
    }

    // ---- Recording auto-stop (buffer full) --------------------------------
    if (rec_full_) {
      rec_full_ = false;
      CommitRecording();
    }

    // ---- LEDs --------------------------------------------------------------
    UpdateLeds(led1, led2, auto_mode);
  }

  // -------------------------------------------------------------------------
  // Audio-rate. Recording writes, grain scheduler, grain sum, texture.
  // -------------------------------------------------------------------------
  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      const float x = in[i];

      // ---- Input envelope (drives continuous-auto gate) -------------------
      float a = fabsf(x);
      env_ += VESTIGE_ENV_COEF * (a - env_);

      // ---- Recording ------------------------------------------------------
      if (recording_) {
        float* m = vestige_slab[rec_slot_];
        if (fripp_mode_ && frip_len_ > 0) {
          // Overdub (sound-on-sound): decay existing, add new, wrap at loop len.
          m[rec_idx_] = m[rec_idx_] * frip_decay_ + x;
          rec_idx_++;
          if (rec_idx_ >= frip_len_) rec_idx_ = 0;
        } else {
          // Linear capture (voiced, or frippertronics first pass).
          m[rec_idx_] = x;
          rec_idx_++;
          if (rec_idx_ >= VESTIGE_LOOP_MAX_SAMPLES) rec_full_ = true;
        }
      }

      // ---- Grain scheduler + per-slot sum + fade envelope -----------------
      // Mute/unmute rides the per-slot fade (K5), so the scheduler runs even
      // while muted so the fade-out tail can play; fully-faded slots sum to 0.
      if (fripp_mode_) {
        ServiceSlot(VESTIGE_FRIP_SLOT);
      } else {
        for (int v = 0; v < VESTIGE_MAX_VOICES; v++) ServiceSlot(v);
      }
      float slot_sum[VESTIGE_SLOTS] = {0.f};
      for (int g = 0; g < VESTIGE_GRAINS; g++) {
        if (grains_[g].IsActive())
          slot_sum[grain_slot_[g]] += grains_[g].Process(*grain_src_[g]);
      }
      float y = 0.f;
      for (int s = 0; s < VESTIGE_SLOTS; s++) {
        fade_gain_[s] += fade_coef_ * (fade_target_[s] - fade_gain_[s]);
        y += slot_sum[s] * fade_gain_[s];
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

      wet[i] = y;
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

  void ServiceSlot(int s) {
    const size_t L = loop_len_[s];
    if (!active_[s] || L < VESTIGE_GRAIN_MIN_LEN) return;
    if (--timer_[s] > 0) return;

    size_t glen = SlotGrainLen(L);
    EmitGrain(s, glen);

    // Hop = grain/overlap, so overlap stays >= 1 (continuous output) whatever
    // the loop length. Ordered read advances 1× real time at CCW.
    size_t hop = (size_t)((float)glen / overlap_);
    if (hop < VESTIGE_MIN_INTERVAL) hop = VESTIGE_MIN_INTERVAL;

    // Advance the read head at advance_rate_ (1× looper → 0 = frozen freeze).
    play_pos_[s] += (size_t)((float)hop * advance_rate_);
    while (play_pos_[s] >= L) play_pos_[s] -= L;

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
    float off = (VestigeRand() * 2.f - 1.f) * spray_;
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
    // The first grain of a fresh loop skips its fade-IN (instant_attack) so
    // playback starts immediately; it keeps its fade-out for the grain handoff.
    grains_[g].Trigger(ring_[s], delay, glen, false, 1.f, gain_[s] * ov_comp, 1, 1.0f,
                       first_grain_[s]);
    first_grain_[s] = false;
  }

  // -------------------------------------------------------------------------
  // Recording lifecycle
  // -------------------------------------------------------------------------
  void StartRecording() {
    if (recording_) return;
    if (fripp_mode_) {
      rec_slot_ = VESTIGE_FRIP_SLOT;
      rec_idx_  = (frip_len_ > 0) ? play_pos_[VESTIGE_FRIP_SLOT] : 0; // overdub syncs to playback
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

  void CommitRecording() {
    if (!recording_) return;
    recording_ = false;

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
      StartFadeIn(s);
      return;
    }

    // ---- Voiced path: choose target NOW, copy scratch → target -------------
    size_t L = rec_idx_;
    if (L < VESTIGE_MIN_LOOP_SAMPLES) L = VESTIGE_MIN_LOOP_SAMPLES;
    if (L > VESTIGE_LOOP_MAX_SAMPLES) L = VESTIGE_LOOP_MAX_SAMPLES;

    const int target = AllocVoicedSlot();   // free slot if under target, else evict oldest
    memcpy(vestige_slab[target], vestige_slab[VESTIGE_REC_SLOT], L * sizeof(float));
    WriteGuard(target, L);
    loop_len_[target] = L;
    play_pos_[target] = 0;
    timer_[target]    = 0;      // fire the first grain immediately
    active_[target]   = true;
    age_[target]      = ++age_counter_;
    StartFadeIn(target);
  }

  // Begin a fade-in for a slot: silent now, ramping to unity over K5 time.
  void StartFadeIn(int s) {
    fade_gain_[s]   = 0.f;
    fade_target_[s] = 1.f;
    first_grain_[s] = true;   // first grain of this fresh loop starts instantly
  }

  // Copy the loop head into the guard region so grains reading across the loop
  // boundary continue seamlessly (main thread; guard is not yet read).
  void WriteGuard(int s, size_t L) {
    float* m = vestige_slab[s];
    for (size_t k = 0; k < VESTIGE_GUARD_SAMPLES; k++) m[L + k] = m[k % L];
  }

  // Pick a voiced slot for a new capture: a free slot if under target, else
  // evict the oldest (FIFO) and reuse it.
  int AllocVoicedSlot() {
    int n_active = 0;
    for (int v = 0; v < VESTIGE_MAX_VOICES; v++) if (active_[v]) n_active++;
    if (n_active < target_voices_) {
      for (int v = 0; v < VESTIGE_MAX_VOICES; v++) if (!active_[v]) return v;
    }
    return EvictOldest();
  }

  int EvictOldest() {
    int oldest = -1;
    uint32_t best = 0xFFFFFFFFu;
    for (int v = 0; v < VESTIGE_MAX_VOICES; v++) {
      if (active_[v] && age_[v] < best) { best = age_[v]; oldest = v; }
    }
    if (oldest < 0) oldest = 0;
    active_[oldest]   = false;
    loop_len_[oldest] = 0;
    return oldest;
  }

  // Reduce active voiced count to target, oldest-first (live K1 control).
  void EvictToTarget() {
    for (;;) {
      int n = 0;
      for (int v = 0; v < VESTIGE_MAX_VOICES; v++) if (active_[v]) n++;
      if (n <= target_voices_) break;
      EvictOldest();
    }
  }

  // Age-ramped fade over the FIFO stack. rank r (0 = newest); linear gain
  // (N-r)/N, normalised 1/sqrt(N) for the stacking law. Fixed slope (K5 now
  // drives the loop fade envelope, not this age-fade).
  void UpdateVoicedGains() {
    const int N = target_voices_ > 0 ? target_voices_ : 1;
    const float norm  = 1.f / sqrtf((float)N);
    for (int v = 0; v < VESTIGE_MAX_VOICES; v++) {
      if (!active_[v]) { gain_[v] = 0.f; continue; }
      int r = 0;   // number of active voices younger than v
      for (int w = 0; w < VESTIGE_MAX_VOICES; w++)
        if (active_[w] && age_[w] > age_[v]) r++;
      float base = (float)(N - r) / (float)N;
      if (base < 0.f) base = 0.f;
      gain_[v] = base * norm;
    }
  }

  // -------------------------------------------------------------------------
  // Frippertronics transitions (provisional — see Open items)
  // -------------------------------------------------------------------------
  void EnterFrippertronics() {
    // Seed the shared buffer from the newest voiced loop so playback continues.
    if (frip_len_ == 0) {
      int src = -1; uint32_t best = 0;
      for (int v = 0; v < VESTIGE_MAX_VOICES; v++)
        if (active_[v] && age_[v] >= best) { best = age_[v]; src = v; }
      if (src >= 0) {
        size_t L = loop_len_[src];
        float* d = vestige_slab[VESTIGE_FRIP_SLOT];
        float* srcm = vestige_slab[src];
        for (size_t k = 0; k < L; k++) d[k] = srcm[k];
        WriteGuard(VESTIGE_FRIP_SLOT, L);
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
          CommitRecording();               // sound → sustained silence: commit
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
    for (int s = 0; s < VESTIGE_SLOTS; s++) {
      active_[s]   = false;
      loop_len_[s] = 0;
      play_pos_[s] = 0;
      timer_[s]    = 0;
      gain_[s]     = 0.f;
      fade_gain_[s]   = 0.f;   // silence immediately (no fade)
      fade_target_[s] = 0.f;
    }
    for (int g = 0; g < VESTIGE_GRAINS; g++) grains_[g] = GrainVoice{};
    frip_len_ = 0;
    muted_    = false;
    auto_armed_ = false;
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
  uint32_t   age_[VESTIGE_SLOTS]      = {0};
  float      gain_[VESTIGE_SLOTS]     = {0.f};
  bool       first_grain_[VESTIGE_SLOTS] = {false};  // next grain skips its fade-in
  uint32_t   age_counter_ = 0;

  // Per-slot loop fade envelope (K5): multiplier on each slot's summed output.
  float      fade_gain_[VESTIGE_SLOTS]   = {0.f};  // current smoothed gain
  float      fade_target_[VESTIGE_SLOTS] = {0.f};  // 0 (fade out) or 1 (fade in)
  float      fade_coef_ = 1.f;                     // per-sample smoothing coef (K5)

  // Cached K3 grain-macro params (Controls → Process)
  size_t grain_len_    = VESTIGE_CCW_GRAIN_LEN;
  float  overlap_      = VESTIGE_CCW_OVERLAP;
  float  advance_rate_ = 1.f;   // read-head speed: 1× looper → 0 = frozen freeze
  float  spray_        = 0.f;   // ± phasing spray (samples) around the head
  float  jitter_       = 0.f;   // scheduler timing jitter

  // Topology
  int  target_voices_ = 1;
  bool fripp_mode_    = false;
  size_t frip_len_    = 0;
  float  frip_decay_  = 1.f;

  // Texture (K4)
  float tape_amt_ = 0.f, digi_amt_ = 0.f;
  float tape_drive_ = 1.f, tape_makeup_ = 1.f, decim_hold_ = 1.f, crush_bits_ = 16.f;
  float decim_phase_ = 0.f, decim_hold_val_ = 0.f;

  // Recording
  volatile bool recording_ = false;
  volatile bool rec_full_  = false;
  int    rec_slot_ = 0;
  size_t rec_idx_  = 0;

  // Transport / capture
  bool     muted_        = false;
  bool     swallow_fs2_  = false;
  bool     clear_latched_= false;
  bool     auto_armed_   = false;
  int      flash_        = 0;
  uint32_t silence_since_= 0;
  float    auto_thresh_  = VESTIGE_AUTO_THRESH_MIN;
  float    env_          = 0.f;

  int blink_ = 0;
};
