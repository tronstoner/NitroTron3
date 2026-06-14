#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"
#include "constants.h"
#include "moog_osc.h"
#include "moog_ladder.h"
#include "moog_ladder_v2.h"
#include "peak_limiter.h"
#include "env_follower.h"
#include "pitch_tracker.h"
#include "preset_system.h"
#include "ring_buffer.h"
#include "grain_voice.h"
#include "clouds/reverb.h"  // vendored, not yet instantiated
#include "resampler.h"
#include "phaser.h"
#include "grendel.h"
#include "glitch_zones.h"
#include "bitcrush.h"
#include "synth_osc_c.h"

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
MoogLadderV2 ladder_c_v2; // Mode C — SW2=UP, our tuned Moog (A/B winner)
Phaser       phaser_c;    // Mode C — SW2=DOWN (3-band parallel BPF, internal LFO)
Grendel      grendel_c;   // Mode C — SW2=MID Grendel formant filter
PeakLimiter  limiter_c;   // Mode C — post-filter peak limiter (2-band, fundamentals preserved)
BitCrush     bitcrush_c;  // Mode C — SW1=MID drive flavor (gated bit crusher)
ModeCSynth   synth_c;     // Mode C — SW1=DOWN pitch-tracked synth oscillator
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
static constexpr float WET_HPF_FREQ = 60.f;
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

// Mode B SW1 MIDDLE: event-driven digital glitch processor (stateful)
GlitchEvents glitch_events;

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

// Exponential mapping for filter cutoff (80 Hz – 8 kHz). Mode A (signed off).
static float MapCutoff(float knob) {
  constexpr float MIN_HZ = 80.f;
  constexpr float MAX_HZ = 8000.f;
  return MIN_HZ * powf(MAX_HZ / MIN_HZ, knob);
}

// Mode C cutoff mapping — extended low end (30 Hz) for a deeper "shut" position.
// Top end matches Mode A so env-mod headroom toward MODE_C_CUTOFF_MAX_HZ behaves the same.
static float MapCutoffModeC(float knob) {
  return MODE_C_CUTOFF_MIN_HZ *
         powf(MODE_C_CUTOFF_K1_MAX_HZ / MODE_C_CUTOFF_MIN_HZ, knob);
}

// K6 mix pre-warp — smoothstep on top of the sqrt equal-power crossfade.
// At noon: smoothstep(0.5) = 0.5, so sqrt(0.5) = 0.707 (−3 dB each), the
// same point as plain sqrt. At the extremes the curve flattens: a knob
// touch from full-dry pulls wet out of silence gently instead of jumping
// straight to ~−10 dB, and the dry vanishes earlier on the wet side
// instead of clinging on at ~−13 dB even at mix=0.95.
static float MixCurve(float mix) {
  return mix * mix * (3.f - 2.f * mix);
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

// Natural harmonic intervals (semitones). Asymmetric: floor at -1 octave,
// ceiling at +3 octaves, with upper-harmonic partials filled in.
static const int RESONANCES[] = {
    -12, -7, -5, 0, 5, 7, 12, 19, 24, 28, 31, 36
};
static constexpr int NUM_RES = 12;

// K1 → semitones, asymmetric range [-12, +36], noon = unison.
static int K1ToSemi(float k1) {
  if (k1 < 0.5f) return static_cast<int>(roundf((k1 - 0.5f) * 24.f));
  return static_cast<int>(roundf((k1 - 0.5f) * 72.f));
}

// Per-semitone feedback scale (SW2 UP / fixed-interval only).
// Unison piles up because each loop pass replays at the same pitch; pitch-down
// loses energy to the 150 Hz wet HPF each pass and needs compensation.
// Curve: unison cut to 0.45, up-side ramps back to 1.0 by +3 semi, down-side
// boosts +0.12 per semitone for a saturated growl, capped at 1.9.
static float FixedIntervalFeedbackScale(int k1_semi) {
  if (k1_semi == 0) return 0.45f;
  if (k1_semi > 0) {
    float t = static_cast<float>(k1_semi) / 3.f;
    if (t > 1.f) t = 1.f;
    return 0.45f + 0.55f * t;
  }
  float scale = 1.f + 0.12f * static_cast<float>(-k1_semi);
  if (scale > 1.9f) scale = 1.9f;
  return scale;
}

// Compute pitch ratio for one grain.
// harmony == 0 (SW2 UP): fixed interval, K1 = exact semitones in [-12, +36].
// harmony != 0 (SW2 MID/DOWN): grain picks uniformly from a ±1 entry window
// around K1's closest RESONANCES entry. No external variance — fixed cloud.
static float GrainPitchRatio(int harmony, float k1) {
  float semi;

  if (harmony == 0) {
    semi = static_cast<float>(K1ToSemi(k1));
  } else {
    int k1_semi = K1ToSemi(k1);
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

    const float m = MixCurve(mix);
    float dry_gain = sqrtf(1.f - m);
    float wet_gain = sqrtf(m);
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

  // Quadratic falloff keeps grains long across most of K3 travel; full CW
  // dives into stutter territory (480 samples ≈ 10 ms at unit k2_scale).
  float gc_sq = grain_character * grain_character;
  size_t grain_len = static_cast<size_t>(
      (9600.f - gc_sq * (9600.f - 480.f)) * k2_scale);
  if (grain_len < 64) grain_len = 64;
  // Sqrt curve: repeats kick in early — 2 loops by K3≈0.05, 3 by K3≈0.10.
  float gc_sqrt = sqrtf(grain_character);
  int max_loops = 1 + static_cast<int>(gc_sqrt * 7.f);  // 1 to 8

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
  // Reverse probability: sqrt curve so reverses appear early — 18% at K3=0.05,
  // 25% at K3=0.10, up to 80% at full CW.
  float reverse_chance = sqrtf(k3) * 0.8f;
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

  // Glitch (SW1 MIDDLE) — bipolar K4, event-driven. CCW = bit-flip events,
  // CW = timing events (freeze / stutter / reverse). Env-gated mix per
  // sample inside the processor. See docs/MODE_B_TEXTURE_IDEAS.md.
  float k4_centered = k4 - 0.5f;
  float glitch_magnitude = fabsf(k4_centered) * 2.f;  // 0..1
  int   glitch_side = (k4_centered < 0.f) ? 0 : 1;    // 0=CCW bit-flip, 1=CW timing
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

  // Even out perceived loudness across K1.
  // SW2 UP: scale by K1's exact semitone offset.
  // SW2 MID: scale by the closest resonance interval (same scan as GrainPitchRatio).
  // SW2 DOWN: untouched.
  if (eb.sw2 == 0) {
    feedback_amt *= FixedIntervalFeedbackScale(K1ToSemi(k1));
  } else if (eb.sw2 == 1) {
    int k1_semi = K1ToSemi(k1);
    int closest_semi = 0;
    int min_dist = 100;
    for (int i = 0; i < NUM_RES; i++) {
      int d = RESONANCES[i] - k1_semi;
      if (d < 0) d = -d;
      if (d < min_dist) { min_dist = d; closest_semi = RESONANCES[i]; }
    }
    feedback_amt *= FixedIntervalFeedbackScale(closest_semi);
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
    // so turning K2 back up reveals a buffer with textured material).
    // tanh on the feedback return tames runaway peaks at high K5 by turning
    // overshoot into soft saturation while preserving the additive character.
    grain_ring.Write(dry + tanhf(prev_wet * feedback_amt));

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
        bool reverse = (glitch_amount > 0.1f) && (RandFloat() < glitch_amount * 0.6f);

        // Pitch: UP holds a stable interval until K1 moves; MID re-rolls every
        // grain within its ±1 RESONANCES window.
        int k1_semi_now = K1ToSemi(k1);
        bool force_reroll = (k1_semi_now != harmony_cached_k1_semi);
        if (harmony == 0) {
          if (force_reroll || harmony_hold_counter <= 0) {
            harmony_cached_ratio = GrainPitchRatio(harmony, k1);
            harmony_cached_k1_semi = k1_semi_now;
            harmony_hold_counter = 1;
          }
        } else {
          harmony_cached_ratio = GrainPitchRatio(harmony, k1);
          harmony_cached_k1_semi = k1_semi_now;
        }
        float pitch_ratio = harmony_cached_ratio;
        float comp = 1.f / sqrtf(pitch_ratio);

        // Pitch-aware delay floor: a forward grain at rate R consumes L×R
        // input samples for L output, so start must be at least L×R behind
        // the write head or the read crosses into stale buffer content (and
        // K1 stops having an audible effect). Reverse grains only need L.
        float pitch_span = reverse ? 1.f
                                   : (pitch_ratio > 1.f ? pitch_ratio : 1.f);
        size_t required_delay = static_cast<size_t>(pitch_span * static_cast<float>(grain_len)) + 1;

        // Cap at max_range/2 so at extreme upward pitch the read deliberately
        // crosses into the buffer wrap region — grains converge on the same
        // stale slice and produce the coherent "shimmer" character. Below
        // the cap, K1 still tracks audibly.
        size_t pitch_cap = max_range / 2;
        if (required_delay > pitch_cap) required_delay = pitch_cap;

        size_t emit_delay = base_delay;
        if (required_delay > emit_delay) emit_delay = required_delay;

        // Scatter within whatever budget is left after the required delay.
        size_t scatter_range = (max_range > emit_delay) ? (max_range - emit_delay) : 0;
        size_t pos_offset = static_cast<size_t>(RandFloat() * glitch_amount * static_cast<float>(scatter_range));
        size_t delay = emit_delay + pos_offset;

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
      // Event-driven digital glitch: stochastic triggers, K4 alone controls density.
      wet = glitch_events.Process(wet, glitch_side, glitch_effect_pos, grain_env);
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

    // Wet HPF: 2-pole at 60 Hz to block DC/sub accumulation in the feedback
    // loop without touching bass fundamentals. Skipped in direct-texture
    // mode since the wet there is essentially a passthrough of the dry.
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
  const float m        = MixCurve(mix);
  const float dry_gain = sqrtf(1.f - m);
  const float wet_gain = sqrtf(m);
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

// Mode C filter-env smoother. Shape switches with K3 sign each sample:
//   CW  → peak follower (instant attack, one-pole release).
//   CCW → slow-rise env (one-pole attack, instant snap-back).
// Atk/rel coefs computed in main() from MODE_C_FILTER_ENV_*_MS constants.
float env_c_filter         = 0.f;
float env_c_filter_atk_coef = 0.f;
float env_c_filter_rel_coef = 0.f;

// Grendel env smoother — always swell shape (slow attack, instant snap-back),
// regardless of K3 sign. Snap-style envelopes are reserved for the Moog
// ladder filter. vowel_path is derived directly from this smoothed env in
// the block setup; no separate vowel_path slewing.
float env_c_grendel          = 0.f;
float env_c_grendel_atk_coef = 0.f;

void ProcessFreqShift(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                      size_t size) {
  const ModePresetData& eb = preset.GetEditBuffer();

  const uint8_t drive_mode  = eb.sw1;              // 0=UP sinefold, 1=MID bitcrush, 2=DOWN pitch-tracked synth
  const uint8_t filter_mode = eb.sw2;              // 0=UP Moog, 1=MID Grendel, 2=DOWN Phaser
  const float k1        = RemapKnob(eb.knobs[0]);
  const float k2        = RemapKnob(eb.knobs[1]);
  const float k3        = RemapKnob(eb.knobs[2]);
  const float fold_amt  = RemapKnob(eb.knobs[3]);  // K4
  const float drive_knob = RemapKnob(eb.knobs[4]); // K5: bipolar drive (CCW attenuate, noon unity, CW boost)
  const float mix       = RemapKnob(eb.knobs[5]);  // K6
  // Piecewise linear: [0..0.5] → [MIN..1.0], [0.5..1.0] → [1.0..MAX].
  const float drive_amt = (drive_knob < 0.5f)
      ? MODE_C_DRIVE_MIN + (drive_knob * 2.f) * (1.f - MODE_C_DRIVE_MIN)
      : 1.f + ((drive_knob - 0.5f) * 2.f) * (MODE_C_DRIVE_MAX - 1.f);
  const float m         = MixCurve(mix);
  const float dry_gain  = sqrtf(1.f - m);
  const float wet_gain  = sqrtf(m);

  // Sine wavefolder per-block constants (active only at SW1=UP).
  const float fold_blend = (drive_mode == 0) ? fold_amt : 0.f;
  const float fold_drive = 1.f + fold_blend * SINEFOLD_DRIVE_MAX;
  const float fold_comp  = 1.f - fold_blend * (1.f - SINEFOLD_COMP_AT_MAX);

  // K3 bipolar with center deadzone → signed env amount in [-1, +1].
  float k3_signed = (k3 - 0.5f) * 2.f;
  if (k3_signed > -K3_DEADZONE && k3_signed < K3_DEADZONE) k3_signed = 0.f;

  // Moog ladder per-block constants (active only at SW2=UP).
  const bool ladder_on    = (filter_mode == 0);
  const float base_cutoff = MapCutoffModeC(k1);
  const float k3_abs = (k3_signed >= 0.f) ? k3_signed : -k3_signed;
  // k3_abs is 0 inside the deadzone (k3_signed got snapped); outside it
  // ranges [K3_DEADZONE, 1]. Subtract the deadzone width so env contribution
  // ramps from 0 right at the deadzone edge -- otherwise k3_abs jumps from
  // 0 (inside) to K3_DEADZONE (just outside), which with the current
  // ENV_SCALE x ENV_MOD_RANGE gain produces a multi-x cutoff step during play.
  const float k3_norm = (k3_abs > 0.f)
      ? (k3_abs - K3_DEADZONE) / (1.f - K3_DEADZONE)
      : 0.f;
  const float env_lift_gain = k3_norm * MODE_C_ENV_SCALE * MODE_C_ENV_MOD_RANGE;
  if (ladder_on) {
    ladder_c_v2.SetDrive(drive_amt);
    // sqrt curve so resonance is audible across the full knob range.
    ladder_c_v2.SetResonance(sqrtf(k2) * MODE_C_LADDER_RES_MAX);
  }

  // Grendel per-block setup (active only at SW2=MID).
  // Direct deterministic mapping from the smoothed env:
  //   vowel_path = K1 − K3_signed × env_c_grendel × GRENDEL_TARGET_GAIN
  // K3 CCW (negative) → env pushes path toward ee (low→hi, opens brighter).
  // K3 CW  (positive) → env pushes path toward oo (hi→low, closes darker).
  // env_c_grendel is always a slow-swell smoother (400 ms attack, instant
  // snap-back). vowel_path is unclamped so hard plucks push into post-table
  // "virtual vowels".
  const bool grendel_on = (filter_mode == 1);
  if (grendel_on) {
    const float vowel_path =
        k1 - k3_signed * env_c_grendel * MODE_C_GRENDEL_TARGET_GAIN;
    const float size_base = GRENDEL_SIZE_MIN +
                            k2 * (GRENDEL_SIZE_MAX - GRENDEL_SIZE_MIN);
    // Coupled with K3 (same sign as vowel_path): CCW → size up on attack
    // (mouth tightens), CW → size down (mouth opens).
    const float size_mod = 1.f -
        k3_signed * env_c_grendel * MODE_C_GRENDEL_SIZE_MOD_AMT;
    grendel_c.SetVowel(vowel_path, size_base * size_mod);
  }

  // Phaser per-block constants (active only at SW2=DOWN). K3 here is LFO
  // speed + shape, NOT env amount — the phaser has no env routing.
  // k3_signed sign: negative → triangle LFO; positive → sample-and-hold.
  // Magnitude (with deadzone snap above) sets LFO rate; 0 → static.
  const bool phaser_on = (filter_mode == 2);
  if (phaser_on) phaser_c.SetParams(k1, k2, k3_signed);

  float env_val = prev_block_env_c;
  for (size_t i = 0; i < size; i++) {
    const float dry = in[0][i];

    // Update shared envelope follower from raw bass each sample.
    env_val = env.Process(dry);

    // Feed pitch tracker every sample so its filters stay warm regardless of
    // SW1 position; consumed only when drive_mode == 2 (SW1=DOWN synth osc).
    tracker.Feed(dry, env_val);

    // Filter env (ladder) — shape switches with K3 sign:
    //   CW  → peak follower (instant attack, one-pole release) → snappy auto-wah
    //   CCW → slow-rise env (one-pole attack, instant snap-back) → swell
    if (k3_signed >= 0.f) {
      if (env_val > env_c_filter) env_c_filter = env_val;
      else env_c_filter += env_c_filter_rel_coef * (env_val - env_c_filter);
    } else {
      if (env_val > env_c_filter)
        env_c_filter += env_c_filter_atk_coef * (env_val - env_c_filter);
      else env_c_filter = env_val;
    }

    // Grendel env smoother — always slow-swell shape (slow attack, instant
    // snap-back), regardless of K3 sign. Snap-style envelopes are reserved
    // for the Moog ladder filter above.
    if (env_val > env_c_grendel)
      env_c_grendel += env_c_grendel_atk_coef * (env_val - env_c_grendel);
    else env_c_grendel = env_val;

    // Drive stage (SW1).
    //   0=UP sinefold, 1=MID bitcrush (gated), 2=DOWN pitch-tracked synth osc.
    float wet = dry;
    if (drive_mode == 0 && fold_blend > 0.001f) {
      const float folded = sinf(dry * fold_drive * 1.5707963f);
      wet = dry * (1.f - fold_blend) + folded * fold_blend * fold_comp;
    } else if (drive_mode == 1) {
      wet = bitcrush_c.Process(dry, fold_amt, env_val);
    } else if (drive_mode == 2) {
      // Pitch-tracked synth osc → raw-env VCA (Mode A style) → SW2 filter.
      // K4 (fold_amt) is the timbre morph; YIN pitch quantized to semitones.
      const float f0 = MidiToFreq(tracker.GetMidiNote());
      const float osc_out = synth_c.Process(f0, fold_amt);
      wet = osc_out * env_val * MODE_C_SYNTH_VCA_GAIN;
    }

    // Filter stage (SW2). Filter modulation reads env_c_filter (smoothed).
    if (ladder_on) {
      // CW and CCW both open the filter from K1 upward as env grows; only the
      // env shape (smoother above) differs. CW = snappy auto-wah, CCW = swell.
      const float lift = 1.f + env_c_filter * env_lift_gain;
      float mod_cutoff = base_cutoff * lift;
      if (mod_cutoff > MODE_C_CUTOFF_MAX_HZ) mod_cutoff = MODE_C_CUTOFF_MAX_HZ;
      ladder_c_v2.SetCutoff(mod_cutoff);
      // Spectrum-shaping filter: pad before drive so K5 noon sits in its sweet zone.
      wet = ladder_c_v2.Process(wet * MODE_C_MOOG_INPUT_PAD);
    } else if (grendel_on) {
      // Spectrum-shaping filter: pad before pre-tanh so K5 noon stays clean.
      wet = tanhf(wet * drive_amt * MODE_C_GRENDEL_INPUT_PAD);
      wet = grendel_c.Process(wet);
    } else if (phaser_on) {
      // Clean parallel-BPF filter; pad keeps K5 noon in the linear zone,
      // K5 CW still drives via the soft pre-tanh.
      wet = tanhf(wet * drive_amt * MODE_C_PHASER_INPUT_PAD);
      wet = phaser_c.Process(wet);
    }

    // Slight post-filter lift so the limiter has something to grab on quieter
    // notes without driving the VCA stage. Applied pre-limiter on purpose.
    wet *= MODE_C_POST_FILTER_GAIN;

    // Post-filter peak limiter (2-band split, fundamentals preserved).
    wet = limiter_c.Process(wet);

    // Amp-env VCA — TEMPORARILY DISABLED to audition the wet path without
    // env-driven amplitude shaping. Reinstate by uncommenting the line below.
    // wet *= env_val * MODE_C_VCA_GAIN;

    out[0][i] = out[1][i] = dry * dry_gain + wet * wet_gain;
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
  ladder_c_v2.Init(sr);
  phaser_c.Init(sr);
  grendel_c.Init(sr);
  limiter_c.Init(sr);
  bitcrush_c.Init();
  synth_c.Init(sr);
  env.Init(sr);
  env.SetCutoff(ENV_LP_CUTOFF_HZ);

  // Mode C filter-env smoother: precompute atk/rel coefs. coef = 1 - exp(-1 / (tau * sr))
  env_c_filter_atk_coef = 1.f - expf(-1.f /
      (MODE_C_FILTER_ENV_ATTACK_MS  * 0.001f * sr));
  env_c_filter_rel_coef = 1.f - expf(-1.f /
      (MODE_C_FILTER_ENV_RELEASE_MS * 0.001f * sr));
  env_c_grendel_atk_coef = 1.f - expf(-1.f /
      (MODE_C_GRENDEL_ENV_ATTACK_MS  * 0.001f * sr));
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

  glitch_events.Init();
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

    // Pitch tracker: run YIN in main loop (Mode A, Mode B, Mode C SW1=DOWN).
    tracker.Update();

    // Bootloader: FS1 held 2 s (Phase 1 — proven safe)
    hw.CheckResetToBootloader();
  }

  return 0;
}
