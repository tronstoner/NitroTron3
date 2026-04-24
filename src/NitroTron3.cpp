#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"
#include "constants.h"
#include "moog_osc.h"
#include "moog_ladder.h"
#include "env_follower.h"
#include "pitch_tracker.h"
#include "preset_system.h"
#include "ring_buffer.h"
#include "grain_voice.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;

Hothouse hw;

// ---------------------------------------------------------------------------
// DSP — Mode A (Drone)
// ---------------------------------------------------------------------------
MoogOsc      osc1;
MoogOsc      osc2;
MoogLadder   ladder;
EnvFollower  env;
PitchTracker tracker;

// ---------------------------------------------------------------------------
// DSP — Mode B (Granular Glitch)
// ---------------------------------------------------------------------------
static constexpr size_t GRAIN_BUF_SAMPLES = 48000 * 8;  // 8 s at 48 kHz
float DSY_SDRAM_BSS grain_sdram_buf[GRAIN_BUF_SAMPLES];
RingBuffer   grain_ring;

static constexpr int NUM_GRAIN_VOICES = 8;
GrainVoice   grain_voices[NUM_GRAIN_VOICES];
int          grain_next_voice = 0;
int          grain_timer = 0;        // samples until next event
int          grain_burst_left = 0;   // grains remaining in current burst
float        grain_env = 0.f;        // envelope value for grain amplitude

static constexpr size_t GRAIN_MIN_RANGE  = 4800;   // min read range: 100 ms

// Feedback state
float prev_wet = 0.f;         // previous sample's wet output for feedback injection

// Direct-texture mode: stutter
static constexpr size_t STUTTER_BUF_SIZE = 9600;   // 200 ms at 48 kHz
static constexpr size_t STUTTER_MIN_LOOP = 2400;   // 50 ms min slice
float stutter_buf[STUTTER_BUF_SIZE] = {};
size_t stutter_write_pos = 0;
size_t stutter_buf_filled = 0;
bool   stutter_writing = true;     // false = writes frozen during stutter

// Stutter voice: Hann-windowed slice playback
struct StutterVoice {
    size_t start;      // start position in stutter_buf (circular)
    size_t length;     // slice length in samples
    size_t phase;      // current sample within slice
    bool   reverse;    // playback direction (latched at start)
    bool   active;     // currently playing

    void Trigger(size_t s, size_t len, bool rev) {
        start = s;
        length = len;
        phase = 0;
        reverse = rev;
        active = true;
    }

    float Process(const float* buf, size_t buf_size) {
        if (!active) return 0.f;

        // Hann window: both value AND slope are zero at endpoints
        float t = static_cast<float>(phase) / static_cast<float>(length);
        float window = 0.5f * (1.f - cosf(3.14159265f * 2.f * t));
        // Note: Hann over full length means window=0 at phase=0 and phase=length.
        // Peak at phase=length/2.

        // Read from circular buffer
        size_t offset = reverse ? (length - 1 - phase) : phase;
        size_t idx = (start + offset) % buf_size;
        float sample = buf[idx];

        phase++;
        if (phase >= length) active = false;

        return sample * window;
    }
};

StutterVoice stutter_voices[2];
int stutter_active_idx = 0;        // which voice was last triggered
bool stutter_engaged = false;      // true when K3 > 0 and stutter is running

// Texture shaper state
float decim_hold = 0.f;       // decimator sample-and-hold value
float decim_count = 0.f;      // decimator sample counter
float ringmod_phase = 0.f;    // ringmod carrier oscillator phase
float ringmod_lp_state = 0.f; // one-pole LPF after ringmod

// Wet HPF: 2-pole high-pass to keep wet out of bass sub range
static constexpr float WET_HPF_FREQ = 150.f;
float wet_hp_state[2] = {};
float wet_hp_coeff = 0.f;  // computed in Init

// Simple xorshift32 RNG for grain scatter
static uint32_t rng_state = 12345;
static float RandFloat() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return static_cast<float>(rng_state) / 4294967295.f;
}

// ---------------------------------------------------------------------------
// Preset system + flash storage
// ---------------------------------------------------------------------------
PresetSystem preset;
daisy::PersistentStorage<StorageData> flash_storage(hw.seed.qspi);

// ---------------------------------------------------------------------------
// Drone sub-modes (Switch 2)
// ---------------------------------------------------------------------------
enum DroneMode { DRONE_FIXED, DRONE_TRACK, DRONE_TRACK_DIRECT };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
Led led_bypass;
Led led_status;
float last_env = 0.f;   // smoothed envelope for per-block modulation

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static float Mapf(float in, float min, float max) {
  return min + in * (max - min);
}

// Exponential mapping for filter cutoff (80 Hz – 8 kHz)
static float MapCutoff(float knob) {
  constexpr float MIN_HZ = 80.f;
  constexpr float MAX_HZ = 8000.f;
  return MIN_HZ * powf(MAX_HZ / MIN_HZ, knob);
}

// Quantize knob (0–1) into N equal steps, returning 0..N-1
static int Quantize(float knob, int steps) {
  int val = static_cast<int>(knob * steps);
  if (val >= steps) val = steps - 1;
  return val;
}

// MIDI note to frequency: f = 440 * 2^((note - 69) / 12)
static float MidiToFreq(float note) {
  return 440.f * powf(2.f, (note - 69.f) / 12.f);
}

// Remap pot range — measured 0.000–0.968 at physical extremes
constexpr float KNOB_MIN = 0.004f;
constexpr float KNOB_MAX = 0.964f;

static float RemapKnob(float raw) {
  float v = (raw - KNOB_MIN) / (KNOB_MAX - KNOB_MIN);
  if (v < 0.f) v = 0.f;
  if (v > 1.f) v = 1.f;
  return v;
}

// Map knob with center dead zone to ±N semitone steps.
// Returns 0 in dead zone, -N..-1 below, +1..+N above.
static int MapDetuneKnob(float knob, int steps) {
  constexpr float DEAD_LO = 0.46f;
  constexpr float DEAD_HI = 0.54f;
  if (knob >= DEAD_LO && knob <= DEAD_HI) return 0;
  if (knob < DEAD_LO) {
    float pos = knob / DEAD_LO;
    return -(steps - Quantize(pos, steps));
  }
  float pos = (knob - DEAD_HI) / (1.f - DEAD_HI);
  return Quantize(pos, steps) + 1;
}

// Wavefold: drive signal and reflect at ±PEAK boundaries
// Normalized so triangle gain passes through at amount=0
static float Wavefold(float x, float amount) {
  const float PEAK = OSC_TRI_GAIN;
  x *= 1.f + amount * 4.f;
  float norm = x / PEAK;
  float t = fmodf(norm + 1.f, 4.f);
  if (t < 0.f) t += 4.f;
  float folded = (t < 2.f) ? (t - 1.f) : (3.f - t);
  return folded * PEAK;
}

// ---------------------------------------------------------------------------
// Mode B harmony logic
// ---------------------------------------------------------------------------

// Natural harmonic intervals (semitones) — octaves, fifths, fourths.
// Spanning ±2 octaves for sympathetic resonance.
static const int RESONANCES[] = {
    -24, -19, -12, -7, -5, 0, 5, 7, 12, 19, 24
};
static constexpr int NUM_RES = 11;

// Compute pitch ratio for one grain.
// harmony == 0 (SW2 UP): fixed interval, K1 = exact semitones.
// harmony != 0 (SW2 MID/DOWN): resonance, grains lock onto nearby harmonics.
// Returns ratio with amplitude compensation (1/sqrt(ratio)) baked in as
// a negative sign convention — caller extracts via fabsf and applies gain.
static float GrainPitchRatio(int harmony, float k1) {
  int k1_semi = MapDetuneKnob(k1, 24);  // ±24 semitones
  float semi;

  if (harmony == 0) {
    semi = static_cast<float>(k1_semi);
  } else {
    // Resonance: find closest harmonic interval to K1, pick from ±1 neighbor
    int closest = 0;
    int min_dist = 100;
    for (int i = 0; i < NUM_RES; i++) {
      int d = RESONANCES[i] - k1_semi;
      if (d < 0) d = -d;
      if (d < min_dist) { min_dist = d; closest = i; }
    }
    int lo = (closest > 0) ? closest - 1 : 0;
    int hi = (closest < NUM_RES - 1) ? closest + 1 : NUM_RES - 1;
    int idx = lo + static_cast<int>(RandFloat() * static_cast<float>(hi - lo + 1));
    if (idx > hi) idx = hi;
    semi = static_cast<float>(RESONANCES[idx]);
  }

  return powf(2.f, semi / 12.f);
}

// ---------------------------------------------------------------------------
// Mode A — Drone OSC
// ---------------------------------------------------------------------------
void ProcessDrone(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                  size_t size) {
  const ModePresetData& eb = preset.GetEditBuffer();

  // Waveform select — from edit buffer SW1
  MoogOsc::Waveform wf = MoogOsc::SAW;
  switch (eb.sw1) {
    case 0: wf = MoogOsc::SAW;    break;  // UP
    case 1: wf = MoogOsc::TRI;    break;  // MIDDLE
    case 2: wf = MoogOsc::SQUARE; break;  // DOWN
    default: break;
  }
  osc1.waveform = wf;
  osc2.waveform = wf;

  // Drone sub-mode — from edit buffer SW2
  DroneMode drone_mode = DRONE_FIXED;
  switch (eb.sw2) {
    case 0: drone_mode = DRONE_FIXED;         break;  // UP
    case 1: drone_mode = DRONE_TRACK;         break;  // MIDDLE
    case 2: drone_mode = DRONE_TRACK_DIRECT;  break;  // DOWN
    default: break;
  }

  // --- Pitch controls (from edit buffer knobs, remapped) ---
  float midi_note = 0.f;
  float fine = Mapf(RemapKnob(eb.knobs[2]), -0.5f, 0.5f);  // K3

  if (drone_mode == DRONE_FIXED) {
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    int octave = Quantize(RemapKnob(eb.knobs[1]), 7);  // K2
    int base_note = 12 + octave * 12;
    midi_note = static_cast<float>(base_note + TRACKING_WRAP_NOTE + semi_offset);
  } else if (drone_mode == DRONE_TRACK) {
    int tracked = static_cast<int>(tracker.GetMidiNote());
    int pitch_class = ((tracked - TRACKING_WRAP_NOTE) % 12 + 12) % 12;
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    int octave = Quantize(RemapKnob(eb.knobs[1]), 7);
    int base_note = 12 + octave * 12;
    midi_note = static_cast<float>(base_note + TRACKING_WRAP_NOTE + pitch_class + semi_offset);
  } else {
    float tracked = tracker.GetMidiNote();
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    int oct_offset = Quantize(RemapKnob(eb.knobs[1]), 7) - 3;
    midi_note = tracked + static_cast<float>(semi_offset + oct_offset * 12);
  }

  float freq1 = MidiToFreq(midi_note + fine);

  // K4: tone / wavefold
  float k4 = RemapKnob(eb.knobs[3]);
  float fold_amount = 0.f;

  float base_cutoff;
  if (wf == MoogOsc::TRI) {
    float cutoff_knob = (k4 < 0.5f) ? (k4 * 2.f) : 1.f;
    base_cutoff = MapCutoff(cutoff_knob);
    if (k4 > 0.5f) fold_amount = (k4 - 0.5f) * 2.f;
  } else {
    base_cutoff = MapCutoff(k4);
  }
  float mod_cutoff = base_cutoff * (1.f + last_env * ENV_FILTER_MOD * 20.f);
  if (mod_cutoff > 10000.f) mod_cutoff = 10000.f;
  ladder.SetCutoff(mod_cutoff + LADDER_CUTOFF_OFFSET);
  ladder.SetDrive(LADDER_DRIVE);

  // K5: osc2 detune
  float k5 = RemapKnob(eb.knobs[4]);
  int osc2_semi = MapDetuneKnob(k5, 12);
  float osc2_level = (osc2_semi == 0 && k5 >= 0.46f && k5 <= 0.54f) ? 0.f : 1.f;
  float freq2 = MidiToFreq(midi_note + static_cast<float>(osc2_semi));

  // K6: mix
  float mix = RemapKnob(eb.knobs[5]);

  for (size_t i = 0; i < size; i++) {
    float dry = in[0][i];

    float env_val = env.Process(dry);

    if (drone_mode != DRONE_FIXED) tracker.Feed(dry, env_val);

    float o1 = osc1.Process(freq1);
    float o2 = osc2.Process(freq2);
    if (wf == MoogOsc::TRI) {
      float dyn_fold = fold_amount + env_val * ENV_FOLD_MOD * 5.f;
      if (dyn_fold > 1.f) dyn_fold = 1.f;
      o1 = Wavefold(o1, dyn_fold);
      o2 = Wavefold(o2, dyn_fold);
    }
    o2 *= osc2_level;

    last_env += 0.1f * (env_val - last_env);

    float gain = 1.f / sqrtf(1.f + osc2_level * osc2_level);
    float osc_mix = (o1 + o2) * gain;

    float filtered = ladder.Process(osc_mix);
    float wet = filtered * env_val * OSC_GAIN;

    float dry_gain = sqrtf(1.f - mix);
    float wet_gain = sqrtf(mix);
    out[0][i] = out[1][i] = dry * DRY_TRIM * dry_gain + wet * wet_gain;
  }
}

// ---------------------------------------------------------------------------
// Mode B — Granular Glitch
// ---------------------------------------------------------------------------
void ProcessGranular(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                     size_t size) {
  const ModePresetData& eb = preset.GetEditBuffer();

  // K1: interval (±24 semi, centered with dead zone)
  float k1 = RemapKnob(eb.knobs[0]);

  // K2: buffer range — CCW = tight (100 ms, recent audio only),
  //                     CW = deep (full 8 s, long trails)
  // Fully CCW (<2%) enters direct-texture mode: bypass grain engine
  float k2 = RemapKnob(eb.knobs[1]);
  bool direct_texture = (k2 < 0.02f);
  size_t max_range = GRAIN_MIN_RANGE +
      static_cast<size_t>(k2 * static_cast<float>(GRAIN_BUF_SAMPLES - GRAIN_MIN_RANGE));

  // K3: in grain mode = character + glitch (merged)
  //     in direct-texture mode = micro-stutter probability/duration
  float k3 = RemapKnob(eb.knobs[2]);

  // Grain params (only used when !direct_texture, but cheap to compute always)
  float grain_character = k3;
  float glitch_amount   = k3;

  size_t grain_len = static_cast<size_t>(9600.f - grain_character * (9600.f - 960.f));
  if (grain_len < 64) grain_len = 64;
  int max_loops = 1 + static_cast<int>(grain_character * 7.f);  // 1 to 8

  float overlap = 4.f - glitch_amount * 3.f;
  size_t base_interval = static_cast<size_t>(
      static_cast<float>(grain_len) / overlap);
  if (base_interval < 32) base_interval = 32;

  // Stutter params (only used when direct_texture)
  // Base chunk: 200 ms at CCW → 50 ms at CW (randomized ±40% per event at trigger time)
  float stutter_base_chunk = static_cast<float>(STUTTER_BUF_SIZE) -
      k3 * static_cast<float>(STUTTER_BUF_SIZE - STUTTER_MIN_LOOP);
  // Event probability per sample: quadratic, erratic at full CW (~every 30 ms)
  float stutter_prob = k3 * k3 * (1.f / 1440.f);
  // Cut-out probability: 0% at low k3, up to 40% at full CW
  float cutout_chance = k3 * 0.4f;
  // Reverse probability: 0% at low k3, up to 60% at full CW
  float reverse_chance = k3 * 0.6f;

  // K4: texture amount (0 = clean, CW = full effect)
  float k4 = RemapKnob(eb.knobs[3]);

  // SW1: texture mode (0=decimate/fold bipolar, 1=free, 2=ringmod)
  int texture_mode = eb.sw1;

  // Ringmod: sine carrier, keytracked LPF
  // K4 < 30% = tremolo (1–15 Hz), K4 >= 30% = bell partials (3.5× at noon)
  float ringmod_inc;
  float ringmod_lp_g = 1.f;
  if (k4 < 0.3f) {
    // Tremolo: 1–15 Hz, not pitch-tracked, no compensation needed
    float trem_freq = 1.f + (k4 / 0.3f) * 14.f;
    ringmod_inc = trem_freq / 48000.f;
  } else {
    // Bell/metallic partials — 3.5× lands at noon, inharmonic spread
    static const float RATIOS[] = {1.5f, 2.76f, 3.5f, 4.2f, 5.4f, 6.5f, 7.3f};
    int idx = static_cast<int>((k4 - 0.3f) / 0.7f * 7.f);
    if (idx > 6) idx = 6;
    float ratio = RATIOS[idx];
    float carrier_freq = MidiToFreq(tracker.GetMidiNote()) * ratio;
    ringmod_inc = carrier_freq / 48000.f;
    // Keytracked LPF: cutoff = carrier × 6 (gentle top-end rolloff,
    // only tames the extreme highs at high ratios). One-pole coefficient.
    float lp_cutoff = carrier_freq * 6.f;
    if (lp_cutoff > 20000.f) lp_cutoff = 20000.f;
    ringmod_lp_g = 1.f - expf(-2.f * 3.14159265f * lp_cutoff / 48000.f);
  }

  // Bipolar K4 helpers for case 0 (decimator/folder)
  // CCW→noon (k4 0→0.5): decimator amount, inverted so CCW = max crush
  // noon→CW (k4 0.5→1): fold amount
  float decim_amt = (k4 < 0.5f) ? (1.f - k4 / 0.5f) : 0.f;   // 1 at CCW, 0 at noon
  float fold_amt  = (k4 > 0.5f) ? ((k4 - 0.5f) / 0.5f) : 0.f; // 0 at noon, 1 at CW
  float decim_rate = 1.f + decim_amt * 19.f;  // 1 (clean at noon) to 20 (max crush at CCW)

  // K5: feedback — 0 = none, CW = max (0.95 ceiling)
  float feedback_amt = RemapKnob(eb.knobs[4]) * 0.95f;

  // Base delay scales with range, floored at grain length
  size_t base_delay = max_range / 8;
  if (base_delay < grain_len) base_delay = grain_len;

  // K6: dry/wet mix
  float mix = RemapKnob(eb.knobs[5]);

  // SW2: harmony mode
  int harmony = eb.sw2;  // 0=fixed, 1/2=resonance

  for (size_t i = 0; i < size; i++) {
    float dry = in[0][i];

    // Envelope follower (feeds pitch tracker gating)
    grain_env = env.Process(dry);

    // Feed pitch tracker
    tracker.Feed(dry, grain_env);

    // Write input + feedback into ring buffer (even in direct-texture mode,
    // so turning K2 back up reveals a buffer with textured material)
    grain_ring.Write(dry + prev_wet * feedback_amt);

    float wet;

    if (direct_texture) {
      // Step 2: two-voice Hann crossfade. Voices overlap at the midpoint
      // so the sum is always ~1.0. No hard switch from/to dry.
      // Still fixed params, no randomness for diagnostic.

      // Write to capture buffer when no voice is active
      bool any_active = stutter_voices[0].active || stutter_voices[1].active;
      if (!any_active) {
        stutter_buf[stutter_write_pos] = dry;
        stutter_write_pos++;
        if (stutter_write_pos >= STUTTER_BUF_SIZE) stutter_write_pos = 0;
        if (stutter_buf_filled < STUTTER_BUF_SIZE) stutter_buf_filled++;
      }

      // Trigger logic: start next voice when current voice reaches midpoint
      StutterVoice& cur = stutter_voices[stutter_active_idx];
      int next_idx = 1 - stutter_active_idx;
      StutterVoice& nxt = stutter_voices[next_idx];

      if (k3 > 0.01f && stutter_buf_filled >= STUTTER_BUF_SIZE) {
        if (!stutter_engaged) {
          // First engagement: trigger voice A
          size_t slen = STUTTER_BUF_SIZE;
          size_t start = (stutter_write_pos + STUTTER_BUF_SIZE - slen) % STUTTER_BUF_SIZE;
          cur.Trigger(start, slen, false);
          stutter_engaged = true;
        } else if (cur.active && cur.phase == cur.length / 2 && !nxt.active) {
          // Current voice at midpoint (Hann peak, about to fade out):
          // start next voice so they overlap
          size_t slen = STUTTER_BUF_SIZE;
          size_t start = (stutter_write_pos + STUTTER_BUF_SIZE - slen) % STUTTER_BUF_SIZE;
          nxt.Trigger(start, slen, false);
          stutter_active_idx = next_idx;
        }
      } else {
        stutter_engaged = false;
      }

      // Sum both voices
      float v0 = stutter_voices[0].Process(stutter_buf, STUTTER_BUF_SIZE);
      float v1 = stutter_voices[1].Process(stutter_buf, STUTTER_BUF_SIZE);
      float voice_sum = v0 + v1;

      // When voices are active, output is the voice sum.
      // When no voices active, output is dry.
      // The Hann windows handle the transition (both start/end at zero with zero slope).
      wet = any_active ? voice_sum : dry;

    } else {

      // Scheduler: continuous stream, K3 adds chaos
      grain_timer--;
      if (grain_timer <= 0) {
        // Delay: base offset + scatter within K5 range
        size_t scatter_range = max_range - base_delay;
        size_t pos_offset = static_cast<size_t>(RandFloat() * glitch_amount * static_cast<float>(scatter_range));
        size_t delay = base_delay + pos_offset;
        if (delay > max_range) delay = max_range;

        bool reverse = (glitch_amount > 0.1f) && (RandFloat() < glitch_amount * 0.6f);

        float pitch_ratio = GrainPitchRatio(harmony, k1);
        float comp = 1.f / sqrtf(pitch_ratio);

        int loops = 1 + static_cast<int>(RandFloat() * static_cast<float>(max_loops));
        if (loops > max_loops) loops = max_loops;

        // Find an inactive voice — never steal mid-playback
        int voice = -1;
        for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
          int idx = (grain_next_voice + v) % NUM_GRAIN_VOICES;
          if (!grain_voices[idx].IsActive()) {
            voice = idx;
            grain_next_voice = (idx + 1) % NUM_GRAIN_VOICES;
            break;
          }
        }
        if (voice >= 0) {
          grain_voices[voice].Trigger(
              grain_ring, delay, grain_len, reverse, pitch_ratio, comp, loops);
        }

        float jitter = (RandFloat() * 2.f - 1.f) * glitch_amount * 0.8f;
        grain_timer = static_cast<int>(
            static_cast<float>(base_interval) * (1.f + jitter));
        if (grain_timer < 32) grain_timer = 32;
      }

      // Sum all active voices
      wet = 0.f;
      for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
        wet += grain_voices[v].Process(grain_ring);
      }
    }

    // Texture shaper — K4 meaning depends on SW1 mode
    switch (texture_mode) {
    case 0: {
      // Bipolar: CCW = decimator, noon = clean, CW = wavefolder
      if (decim_amt > 0.01f) {
        decim_count += 1.f;
        if (decim_count >= decim_rate) {
          decim_count -= decim_rate;
          decim_hold = wet;
        }
        wet = wet * (1.f - decim_amt) + decim_hold * decim_amt;
      }
      if (fold_amt > 0.01f) {
        float driven = wet * (1.f + fold_amt * 7.f);
        float folded = sinf(driven * 1.5707963f) * 0.15f;
        wet = wet * (1.f - fold_amt) + folded * fold_amt;
      }
      break;
    }
    case 1:
      // Free — clean passthrough
      break;
    case 2: {
      // Ringmod: sine carrier, keytracked LPF
      float carrier = sinf(2.f * 3.14159265f * ringmod_phase);
      ringmod_phase += ringmod_inc;
      if (ringmod_phase >= 1.f) ringmod_phase -= 1.f;
      float rm;
      if (k4 < 0.3f) {
        // Tremolo region: AM (50:50 clean/modulated)
        rm = wet * (0.5f + 0.5f * carrier);
      } else {
        // Bell region: true ringmod
        rm = wet * carrier;
      }
      // Keytracked one-pole LPF to tame highs
      ringmod_lp_state += ringmod_lp_g * (rm - ringmod_lp_state);
      wet = ringmod_lp_state;
      break;
    }
    }

    // Wet HPF: 2-pole (two cascaded one-pole HP)
    wet_hp_state[0] += (1.f - wet_hp_coeff) * (wet - wet_hp_state[0]);
    float hp1 = wet - wet_hp_state[0];
    wet_hp_state[1] += (1.f - wet_hp_coeff) * (hp1 - wet_hp_state[1]);
    wet = hp1 - wet_hp_state[1];

    // Store post-HPF wet for next sample's feedback injection
    prev_wet = wet;

    float dry_gain = sqrtf(1.f - mix);
    float wet_gain = sqrtf(mix);
    out[0][i] = out[1][i] = dry * dry_gain + wet * wet_gain;
  }
}

// ---------------------------------------------------------------------------
// Mode C — Frequency Shifter (stub: dry passthrough)
// ---------------------------------------------------------------------------
void ProcessFreqShift(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                      size_t size) {
  for (size_t i = 0; i < size; i++) {
    out[0][i] = out[1][i] = in[0][i];
  }
}

// ---------------------------------------------------------------------------
// Audio callback — dispatches to active mode
// ---------------------------------------------------------------------------
enum Mode { MODE_DRONE = 0, MODE_GRANULAR = 1, MODE_FREQSHIFT = 2 };

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  if (preset.IsBypassed()) {
    for (size_t i = 0; i < size; i++)
      out[0][i] = out[1][i] = in[0][i];
    return;
  }

  switch (preset.GetCurrentMode()) {
    case MODE_DRONE:     ProcessDrone(in, out, size);     break;
    case MODE_GRANULAR:  ProcessGranular(in, out, size);  break;
    case MODE_FREQSHIFT: ProcessFreqShift(in, out, size); break;
    default:             ProcessDrone(in, out, size);     break;
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  hw.Init();
  hw.SetAudioBlockSize(48);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  float sr = hw.AudioSampleRate();
  osc1.Init(sr);
  osc2.Init(sr);
  ladder.Init(sr);
  env.Init(sr);
  env.SetCutoff(ENV_LP_CUTOFF_HZ);
  tracker.Init(sr);
  grain_ring.Init(grain_sdram_buf, GRAIN_BUF_SAMPLES);

  // Wet HPF coefficient
  wet_hp_coeff = 1.f / (1.f + 2.f * 3.14159265f * WET_HPF_FREQ / sr);

  led_status.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  // Init preset system (owns LEDs, footswitches, flash)
  preset.Init(hw, led_status, led_bypass, flash_storage);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);

    // Preset system: footswitches, knobs, LEDs, mode switching, flash
    preset.Tick(10);

    // Pitch tracker: run YIN in main loop (Mode A + Mode B)
    if (preset.GetCurrentMode() != MODE_FREQSHIFT)
      tracker.Update();

    // Bootloader: FS1 held 2 s (Phase 1 — proven safe)
    hw.CheckResetToBootloader();
  }

  return 0;
}
