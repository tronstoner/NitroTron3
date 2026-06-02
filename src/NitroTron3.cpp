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
#include "clouds/reverb.h"  // vendored, not yet instantiated
#include "resampler.h"
#include "plague.h"
#include "grendel.h"
#include "glitch_zones.h"

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
MoogLadder   ladder;     // Mode A
MoogLadder   ladder_c;   // Mode C — separate instance so filter state can't leak across modes
Plague       plague_c;   // Mode C — SW2=DOWN Plague filter
Grendel      grendel_c;  // Mode C — SW2=MID Grendel formant filter
EnvFollower  env;        // shared between Mode A and Mode C (only one mode active at a time)
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
int          harmony_hold_counter = 0;  // grains remaining before re-rolling pitch
float        harmony_cached_ratio = 1.f; // cached pitch ratio for held harmony
int          harmony_cached_k1_semi = 999; // cached K1 target; force re-roll on change

static constexpr size_t GRAIN_MIN_RANGE  = 4800;   // min read range: 100 ms

// Feedback state
float prev_wet = 0.f;         // previous sample's wet output for feedback injection

// Direct-texture mode: stutter capture buffer
static constexpr size_t STUTTER_BUF_SIZE = 9600;   // 200 ms at 48 kHz
static constexpr size_t STUTTER_MIN_LOOP = 960;    // 20 ms min slice
float stutter_buf[STUTTER_BUF_SIZE] = {};
size_t stutter_write_pos = 0;
size_t stutter_buf_filled = 0;

// Stutter voice: Tukey-windowed single-play slice.  Flat in the middle,
// cosine taper at edges (TAPER_LEN samples ≈ 5 ms).  Two voices ping-pong
// with short overlap at the taper region for click-free crossfade.
struct StutterVoice {
    static constexpr size_t MIN_TAPER = 96;   // 2 ms at 48 kHz
    static constexpr size_t MAX_TAPER = 240;  // 5 ms at 48 kHz

    size_t start;      // start position in stutter_buf (circular)
    size_t length;     // slice length in samples
    size_t phase;      // current sample within slice
    size_t taper;      // computed at trigger: adaptive taper length
    bool   reverse;    // playback direction (latched at trigger)
    bool   active;
    float  window;     // current window value — exposed for complement crossfade

    void Trigger(size_t s, size_t len, bool rev) {
        start = s;
        length = len;
        phase = 0;
        reverse = rev;
        active = true;
        window = 0.f;
        // Adaptive taper: 10% of length, clamped 2–5 ms.
        taper = static_cast<size_t>(static_cast<float>(len) * 0.1f);
        if (taper < MIN_TAPER) taper = MIN_TAPER;
        if (taper > MAX_TAPER) taper = MAX_TAPER;
        if (taper > length / 2) taper = length / 2;
    }

    float Process(const float* buf, size_t buf_size) {
        if (!active) { window = 0.f; return 0.f; }

        // Tukey window: cosine taper at edges, flat (1.0) in the middle

        if (phase < taper) {
            float t = static_cast<float>(phase) / static_cast<float>(taper);
            window = 0.5f * (1.f - cosf(3.14159265f * t));
        } else if (phase >= length - taper) {
            float t = static_cast<float>(length - 1 - phase) / static_cast<float>(taper);
            window = 0.5f * (1.f - cosf(3.14159265f * t));
        } else {
            window = 1.f;
        }

        size_t offset = reverse ? (length - 1 - phase) : phase;
        size_t idx = (start + offset) % buf_size;
        float sample = buf[idx];

        phase++;
        if (phase >= length) { active = false; window = 0.f; }

        return sample * window;
    }
};

StutterVoice stutter_voices[2];
int  stutter_active_idx = 0;       // which voice is "current"
bool stutter_engaged = false;      // true while a stutter event is playing
bool stutter_next_armed = false;   // false once midpoint trigger has fired
int  stutter_reps_left = 0;        // remaining reps in current event
size_t stutter_snap_start = 0;     // latched start position
size_t stutter_snap_len = 0;       // latched chunk length
bool   stutter_snap_rev = false;   // latched reverse flag
bool   stutter_is_cutout = false;  // true = silence event (rhythmic gating)
size_t stutter_cutout_phase = 0;   // current sample within cut-out
size_t stutter_cutout_len = 0;     // total cut-out duration in samples
size_t stutter_fresh = 0;          // samples written since last event ended

// Texture shaper state
float decim_hold = 0.f;       // decimator sample-and-hold value
float decim_count = 0.f;      // decimator sample counter
float ringmod_phase = 0.f;    // ringmod carrier oscillator phase
float ringmod_lp_state = 0.f; // one-pole LPF after ringmod

// Wet HPF: 2-pole high-pass to keep wet out of bass sub range
static constexpr float WET_HPF_FREQ = 150.f;
float wet_hp_state[2] = {};
float wet_hp_coeff = 0.f;  // computed in Init

// Reverb sample-rate conversion. Clouds reverb runs internally at 32 kHz.
// 48->32 downsampler is mono (one shared input). 32->48 upsampler is per-
// channel so the reverb's L/R decorrelation survives end-to-end.
Resampler<2, 3, 16> rev_downsampler;
Resampler<3, 2, 16> rev_upsampler_l;
Resampler<3, 2, 16> rev_upsampler_r;
static constexpr float RESAMPLER_CUTOFF_HZ = 15000.f;
static constexpr float RESAMPLER_PROTO_FS_HZ = 96000.f;

// Clouds Reverb (Mode B wet path). 16384-sample uint16_t buffer in SDRAM.
uint16_t DSY_SDRAM_BSS reverb_buf[16384];
clouds::Reverb reverb_instance;

// Smoothed K5 reverb amount — kills zipper noise across block boundaries.
float reverb_amt_smooth = 0.f;

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
// variance (0..1) scales the random ±1 neighbor window: at 0 always pick the
// closest harmonic (stable pitch), at 1 the full ±1 neighbor window is used.
static float GrainPitchRatio(int harmony, float k1, float variance) {
  int k1_semi = MapDetuneKnob(k1, 24);  // ±24 semitones
  float semi;

  if (harmony == 0) {
    semi = static_cast<float>(k1_semi);
  } else {
    int closest = 0;
    int min_dist = 100;
    for (int i = 0; i < NUM_RES; i++) {
      int d = RESONANCES[i] - k1_semi;
      if (d < 0) d = -d;
      if (d < min_dist) { min_dist = d; closest = i; }
    }
    int idx = closest;
    if (RandFloat() < variance) {
      int lo = (closest > 0) ? closest - 1 : 0;
      int hi = (closest < NUM_RES - 1) ? closest + 1 : NUM_RES - 1;
      idx = lo + static_cast<int>(RandFloat() * static_cast<float>(hi - lo + 1));
      if (idx > hi) idx = hi;
    }
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

  // K2 timescale factor: CCW = 0.5× (shorter/faster), CW = 2× (longer/slower)
  float k2_scale = 0.5f + k2 * 1.5f;

  size_t grain_len = static_cast<size_t>(
      (9600.f - grain_character * (9600.f - 960.f)) * k2_scale);
  if (grain_len < 64) grain_len = 64;
  int max_loops = 1 + static_cast<int>(grain_character * 7.f);  // 1 to 8

  float overlap = 4.f - glitch_amount * 3.f;
  size_t base_interval = static_cast<size_t>(
      static_cast<float>(grain_len) / overlap);
  if (base_interval < 32) base_interval = 32;

  // Stutter params (only used when direct_texture)
  // Base chunk: 200 ms at CCW → 50 ms at CW (randomized ±40% per event)
  float stutter_base_chunk = static_cast<float>(STUTTER_BUF_SIZE) -
      k3 * static_cast<float>(STUTTER_BUF_SIZE - STUTTER_MIN_LOOP);
  // Event probability per sample: quadratic, erratic at full CW (~every 30 ms)
  float stutter_prob = k3 * k3 * (1.f / 1440.f);
  // Reverse probability: 0% at low k3, up to 80% at full CW
  float reverse_chance = k3 * 0.8f;
  // Cut-out probability: 0% at low k3, up to 40% at full CW
  float cutout_chance = k3 * 0.4f;

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
  float decim_rate = 1.f + decim_amt * 47.f;  // 1 (clean at noon) to 48 (max crush at CCW)

  // Glitch zones (SW1 MIDDLE) — bipolar K4. See docs/MODE_B_TEXTURE_IDEAS.md.
  float k4_centered = k4 - 0.5f;
  float glitch_magnitude = fabsf(k4_centered) * 2.f;  // 0..1
  glitch_magnitude *= (1.f - GLITCH_ENV_DEPTH + GLITCH_ENV_DEPTH * grain_env);
  int   glitch_side = (k4_centered < 0.f) ? 0 : 1;    // 0=XOR, 1=ROT
  float glitch_effect_pos = (glitch_magnitude > GLITCH_DEADZONE)
      ? (glitch_magnitude - GLITCH_DEADZONE) / (1.f - GLITCH_DEADZONE)
      : 0.f;

  // K5: bipolar — CCW = reverb dry/wet (0→1), center = off (deadzone),
  //               CW = ring-buffer feedback (0→0.95, existing behavior).
  float k5 = RemapKnob(eb.knobs[4]);
  float reverb_amt;
  float feedback_amt;
  const float dead = K5_CENTER_DEADZONE;
  if (k5 < 0.5f - dead) {
    reverb_amt = (0.5f - dead - k5) / (0.5f - dead);  // 0 at edge of deadzone, 1 at full CCW
    feedback_amt = 0.f;
  } else if (k5 > 0.5f + dead) {
    feedback_amt = ((k5 - 0.5f - dead) / (0.5f - dead)) * REVERB_MAX_FEEDBACK;
    reverb_amt = 0.f;
  } else {
    reverb_amt = 0.f;
    feedback_amt = 0.f;
  }

  // Base delay scales with range, floored at grain length
  size_t base_delay = max_range / 8;
  if (base_delay < grain_len) base_delay = grain_len;

  // K6: dry/wet mix
  float mix = RemapKnob(eb.knobs[5]);

  // SW2: harmony mode
  int harmony = eb.sw2;  // 0=fixed, 1/2=resonance

  // Per-sample wet bus capture (used by block-based reverb pipeline below).
  float wet_block[48];

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
      // Two-voice Tukey-windowed stutter with probability-based events.
      // Repeats use voice engine; cut-outs use dedicated cosine envelope.

      // Write to capture buffer (freeze during repeat events, not cut-outs)
      if (!stutter_engaged || stutter_is_cutout) {
        stutter_buf[stutter_write_pos] = dry;
        stutter_write_pos++;
        if (stutter_write_pos >= STUTTER_BUF_SIZE) stutter_write_pos = 0;
        if (stutter_buf_filled < STUTTER_BUF_SIZE) stutter_buf_filled++;
        stutter_fresh++;
      }

      // --- Probability-based event trigger ---
      // Require enough fresh audio to fill the chunk — prevents stale/fresh
      // seam in the capture region when events fire in quick succession.
      size_t min_fresh = static_cast<size_t>(stutter_base_chunk * 1.4f);  // worst-case chunk
      if (min_fresh > STUTTER_BUF_SIZE) min_fresh = STUTTER_BUF_SIZE;
      bool any_active = stutter_voices[0].active || stutter_voices[1].active;
      if (k3 > 0.01f && stutter_buf_filled >= STUTTER_BUF_SIZE
          && !stutter_engaged && !any_active && !stutter_is_cutout
          && stutter_fresh >= min_fresh
          && RandFloat() < stutter_prob) {

          if (RandFloat() < cutout_chance) {
            // Cut-out: dedicated cosine envelope, no voice triggered
            stutter_is_cutout = true;
            stutter_cutout_len = 480 + static_cast<size_t>(RandFloat() * 1920.f);
            stutter_cutout_phase = 0;
            stutter_engaged = true;
          } else {
            // Repeat: randomize chunk length ±40%
            stutter_is_cutout = false;
            float rand_scale = 0.6f + RandFloat() * 0.8f;
            size_t chunk = static_cast<size_t>(stutter_base_chunk * rand_scale);
            if (chunk > STUTTER_BUF_SIZE) chunk = STUTTER_BUF_SIZE;
            if (chunk < STUTTER_MIN_LOOP) chunk = STUTTER_MIN_LOOP;
            stutter_snap_len = chunk;
            stutter_snap_start = (stutter_write_pos + STUTTER_BUF_SIZE - stutter_snap_len)
                                 % STUTTER_BUF_SIZE;
            stutter_snap_rev = (RandFloat() < reverse_chance);
            stutter_reps_left = 1 + static_cast<int>(RandFloat() * (1.f + k3 * 4.f));

            stutter_active_idx = 0;
            stutter_voices[0].Trigger(stutter_snap_start, stutter_snap_len, stutter_snap_rev);
            stutter_reps_left--;
            stutter_next_armed = true;
            stutter_engaged = true;
          }
          stutter_fresh = 0;  // reset fresh counter on any event start
      }

      if (stutter_is_cutout && stutter_engaged) {
        // --- Cut-out: cosine envelope ducks dry, no voices involved ---
        size_t taper = StutterVoice::MAX_TAPER;
        if (taper > stutter_cutout_len / 2) taper = stutter_cutout_len / 2;

        float duck;
        if (stutter_cutout_phase < taper) {
          // Fade out dry
          float t = static_cast<float>(stutter_cutout_phase) / static_cast<float>(taper);
          duck = 0.5f * (1.f - cosf(3.14159265f * t));
        } else if (stutter_cutout_phase >= stutter_cutout_len - taper) {
          // Fade in dry
          float t = static_cast<float>(stutter_cutout_len - 1 - stutter_cutout_phase)
                    / static_cast<float>(taper);
          duck = 0.5f * (1.f - cosf(3.14159265f * t));
        } else {
          duck = 1.f;  // full silence
        }

        wet = dry * (1.f - duck);

        stutter_cutout_phase++;
        if (stutter_cutout_phase >= stutter_cutout_len) {
          stutter_is_cutout = false;
          stutter_engaged = false;
        }
      } else {
        // --- Repeat path: two-voice Tukey crossfade ---

        // Overlap trigger: fire next voice during current voice's tail taper
        if (stutter_engaged) {
          StutterVoice& cur = stutter_voices[stutter_active_idx];
          int next_idx = 1 - stutter_active_idx;
          StutterVoice& nxt = stutter_voices[next_idx];

          if (stutter_next_armed && cur.active
              && cur.phase >= cur.length - cur.taper && !nxt.active) {
            if (stutter_reps_left > 0) {
              nxt.Trigger(stutter_snap_start, stutter_snap_len, stutter_snap_rev);
              stutter_active_idx = next_idx;
              stutter_reps_left--;
              stutter_next_armed = true;
            } else {
              stutter_next_armed = false;
            }
          }
        }

        // Process both voices
        float v0 = stutter_voices[0].Process(stutter_buf, STUTTER_BUF_SIZE);
        float v1 = stutter_voices[1].Process(stutter_buf, STUTTER_BUF_SIZE);

        // Combined window for complement crossfade (clamped to 1.0)
        float w = stutter_voices[0].window + stutter_voices[1].window;
        if (w > 1.f) w = 1.f;

        // Event ends when all voices finish
        if (stutter_engaged && !stutter_voices[0].active && !stutter_voices[1].active) {
          stutter_engaged = false;
        }

        // Complement crossfade
        wet = dry * (1.f - w) + v0 + v1;
      }

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

        // Harmony hold: re-roll pitch every N grains, where N falls from ~6
        // at K3=0 to 1 at K3=1. Also force re-roll if K1 target moved.
        int k1_semi_now = MapDetuneKnob(k1, 24);
        bool force_reroll = (k1_semi_now != harmony_cached_k1_semi);
        if (harmony_hold_counter <= 0 || force_reroll) {
          harmony_cached_ratio = GrainPitchRatio(harmony, k1, k3);
          harmony_cached_k1_semi = k1_semi_now;
          harmony_hold_counter = 1 + static_cast<int>((1.f - k3) * 5.f);
        }
        float pitch_ratio = harmony_cached_ratio;
        harmony_hold_counter--;
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
        float driven = wet * (1.f + fold_amt * 30.f);
        float folded = sinf(driven * 1.5707963f) * 0.15f;
        wet = wet * (1.f - fold_amt) + folded * fold_amt;
      }
      break;
    }
    case 1: {
      // Zoned digital glitch: bipolar K4, XOR CCW / bit-rotate CW, clean at noon.
      wet = ProcessGlitch(wet, glitch_side, glitch_effect_pos);
      break;
    }
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
    // Skip in direct-texture mode — wet IS the dry signal during passthrough,
    // and the 150 Hz HPF would strip the bass fundamental.
    if (!direct_texture) {
      wet_hp_state[0] += (1.f - wet_hp_coeff) * (wet - wet_hp_state[0]);
      float hp1 = wet - wet_hp_state[0];
      wet_hp_state[1] += (1.f - wet_hp_coeff) * (hp1 - wet_hp_state[1]);
      wet = hp1 - wet_hp_state[1];
    }

    // Store post-HPF wet for next sample's feedback injection (pre-reverb,
    // so reverb does not feed the ring buffer — per spec).
    prev_wet = wet;

    // Capture mono wet for the block-based reverb pipeline.
    wet_block[i] = wet;
  }

  // --- Reverb pipeline (block-based, runs at 32 kHz internally) ---
  // Always run so the reverb tail doesn't snap off when K5 leaves CCW;
  // contribution is gated by reverb_amt at the mix point.
  float mid_block[32];
  const size_t n_mid = rev_downsampler.Process(wet_block, size, mid_block);

  clouds::FloatFrame rev_frames[32];
  for (size_t i = 0; i < n_mid; ++i) {
    rev_frames[i].l = mid_block[i];
    rev_frames[i].r = mid_block[i];  // mono input, fed equally to L/R
  }
  reverb_instance.Process(rev_frames, n_mid);
  // With amount=1, rev_frames[i].l/.r now hold pure reverb wet (decorrelated).

  float mid_l[32], mid_r[32];
  for (size_t i = 0; i < n_mid; ++i) {
    mid_l[i] = rev_frames[i].l;
    mid_r[i] = rev_frames[i].r;
  }

  float wet_l_block[48], wet_r_block[48];
  rev_upsampler_l.Process(mid_l, n_mid, wet_l_block);
  rev_upsampler_r.Process(mid_r, n_mid, wet_r_block);

  // --- Final mix (single mono-collapse point, easy to remove for stereo) ---
  const float dry_gain = sqrtf(1.f - mix);
  const float wet_gain = sqrtf(mix);
  for (size_t i = 0; i < size; i++) {
    // Per-sample smoothing on reverb_amt to kill zipper across block boundaries.
    reverb_amt_smooth += REVERB_AMT_SMOOTH_COEF * (reverb_amt - reverb_amt_smooth);
    const float ra = reverb_amt_smooth;
    const float inv_ra = 1.f - ra;
    const float dry = in[0][i];
    const float wet_l = wet_block[i] * inv_ra + wet_l_block[i] * ra;
    const float wet_r = wet_block[i] * inv_ra + wet_r_block[i] * ra;
    const float wet_mono = (wet_l + wet_r) * 0.5f;  // remove for stereo
    out[0][i] = out[1][i] = dry * dry_gain + wet_mono * wet_gain;
  }
}

// ---------------------------------------------------------------------------
// Mode C — Schism (C.5: + Grendel formant filter under SW2=MID)
// ---------------------------------------------------------------------------
// Block-rate env value for Grendel coefficient updates (env LP is ~33 Hz, so
// updating biquad coefs per-sample would waste a lot of cosf/sinf calls).
float prev_block_env_c = 0.f;

void ProcessFreqShift(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                      size_t size) {
  const ModePresetData& eb = preset.GetEditBuffer();

  const uint8_t drive_mode  = eb.sw1;              // 0=UP sinefold, 1=MID TBD, 2=DOWN passthru
  const uint8_t filter_mode = eb.sw2;              // 0=UP Moog, 1=MID Grendel, 2=DOWN Plague
  const float k1        = RemapKnob(eb.knobs[0]);
  const float k2        = RemapKnob(eb.knobs[1]);
  const float k3        = RemapKnob(eb.knobs[2]);
  const float fold_amt  = RemapKnob(eb.knobs[3]);  // K4
  const float wet_level = RemapKnob(eb.knobs[4]);  // K5
  const float mix       = RemapKnob(eb.knobs[5]);  // K6
  const float dry_gain  = sqrtf(1.f - mix);
  const float wet_gain  = sqrtf(mix);

  // Sine wavefolder per-block constants (active only at SW1=UP).
  const float fold_blend = (drive_mode == 0) ? fold_amt : 0.f;
  const float fold_drive = 1.f + fold_blend * SINEFOLD_DRIVE_MAX;
  const float fold_comp  = 1.f - fold_blend * (1.f - SINEFOLD_COMP_AT_MAX);

  // K3 bipolar with center deadzone → signed env amount in [-1, +1].
  float k3_signed = (k3 - 0.5f) * 2.f;
  if (k3_signed > -K3_DEADZONE && k3_signed < K3_DEADZONE) k3_signed = 0.f;

  // Moog ladder per-block constants (active only at SW2=UP).
  const bool ladder_on = (filter_mode == 0);
  const float base_cutoff = MapCutoff(k1);
  const float k3_abs = (k3_signed >= 0.f) ? k3_signed : -k3_signed;
  const float env_lift_gain = k3_abs * MODE_C_ENV_SCALE * MODE_C_ENV_MOD_RANGE;
  if (ladder_on) {
    ladder_c.SetDrive(MODE_C_LADDER_DRIVE);
    ladder_c.SetResonance(k2 * MODE_C_LADDER_RES_MAX);
  }

  // Grendel per-block setup (active only at SW2=MID). Block-rate coef update.
  const bool grendel_on = (filter_mode == 1);
  if (grendel_on) {
    float vowel_path = k1 + k3_signed * prev_block_env_c * MODE_C_ENV_SCALE * GRENDEL_ENV_PATH_RANGE;
    if (vowel_path < 0.f) vowel_path = 0.f;
    if (vowel_path > 1.f) vowel_path = 1.f;
    const float size_scale = GRENDEL_SIZE_MIN +
                             k2 * (GRENDEL_SIZE_MAX - GRENDEL_SIZE_MIN);
    grendel_c.SetVowel(vowel_path, size_scale);
  }

  // Plague per-block constants (active only at SW2=DOWN).
  const bool plague_on = (filter_mode == 2);
  if (plague_on) plague_c.SetParams(k1, k2);

  float env_val = prev_block_env_c;
  for (size_t i = 0; i < size; i++) {
    const float dry = in[0][i];

    // Update shared envelope follower from raw bass each sample.
    env_val = env.Process(dry);

    // Drive stage (SW1).
    float wet = dry;
    if (fold_blend > 0.001f) {
      const float folded = sinf(dry * fold_drive * 1.5707963f);
      wet = dry * (1.f - fold_blend) + folded * fold_blend * fold_comp;
    }

    // Filter stage (SW2).
    if (ladder_on) {
      // Mode A-style linear lift, passive-bass scaled. K3>=0 opens, K3<0 closes.
      const float lift = 1.f + env_val * env_lift_gain;
      const float mod_factor = (k3_signed >= 0.f) ? lift : (1.f / lift);
      ladder_c.SetCutoff(base_cutoff * mod_factor);
      wet = ladder_c.Process(wet);
    } else if (grendel_on) {
      wet = grendel_c.Process(wet);
    } else if (plague_on) {
      // env_contribution: signed (K3 polarity) × envelope magnitude.
      wet = plague_c.Process(wet, k3_signed * env_val);
    }

    out[0][i] = out[1][i] = dry * dry_gain + wet * wet_level * wet_gain;
  }
  prev_block_env_c = env_val;
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
  ladder_c.Init(sr);
  plague_c.Init(sr);
  grendel_c.Init(sr);
  env.Init(sr);
  env.SetCutoff(ENV_LP_CUTOFF_HZ);
  tracker.Init(sr);
  grain_ring.Init(grain_sdram_buf, GRAIN_BUF_SAMPLES);

  // Wet HPF coefficient
  wet_hp_coeff = 1.f / (1.f + 2.f * 3.14159265f * WET_HPF_FREQ / sr);

  // Reverb resamplers + reverb engine
  rev_downsampler.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  rev_upsampler_l.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  rev_upsampler_r.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  reverb_instance.Init(reverb_buf);
  reverb_instance.set_amount(1.0f);  // pure wet; we crossfade externally via K5
  reverb_instance.set_input_gain(REVERB_INPUT_GAIN);
  reverb_instance.set_time(REVERB_TIME);
  // diffusion (0.625) and lp (0.7) set by Reverb::Init() defaults

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
