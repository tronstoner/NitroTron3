#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"
#include "constants.h"
#include "moog_osc.h"
#include "moog_ladder.h"
#include "env_follower.h"
#include "pitch_tracker.h"
#include "usbd_def.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;

Hothouse hw;

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------
MoogOsc      osc1;
MoogOsc      osc2;
MoogLadder   ladder;
EnvFollower  env;
PitchTracker tracker;

// ---------------------------------------------------------------------------
// Drone sub-modes (Switch 2)
// ---------------------------------------------------------------------------
enum DroneMode { DRONE_FIXED, DRONE_TRACK, DRONE_TRACK_DIRECT };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
Led led_bypass;
Led led_status;
bool bypass = true;
float last_env = 0.f;   // smoothed envelope for per-block modulation
int wrap_note = 9;       // wrap point for tracking mode, set by mode 1 (default A)

// LED blink state
uint32_t led_blink_counter = 0;

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
constexpr float KNOB_MIN = 0.000f;
constexpr float KNOB_MAX = 0.968f;

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
// Audio callback
// ---------------------------------------------------------------------------
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  // Bypass toggle — FOOTSWITCH_2
  bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();

  // Waveform select — Switch 1 (both oscillators share waveform)
  MoogOsc::Waveform wf = MoogOsc::SAW;
  switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)) {
    case Hothouse::TOGGLESWITCH_UP:     wf = MoogOsc::SAW;    break;
    case Hothouse::TOGGLESWITCH_MIDDLE: wf = MoogOsc::TRI;    break;
    case Hothouse::TOGGLESWITCH_DOWN:   wf = MoogOsc::SQUARE; break;
    default: break;
  }
  osc1.waveform = wf;
  osc2.waveform = wf;

  // Drone sub-mode — Switch 2
  DroneMode drone_mode = DRONE_FIXED;
  switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)) {
    case Hothouse::TOGGLESWITCH_UP:     drone_mode = DRONE_FIXED; break;
    case Hothouse::TOGGLESWITCH_MIDDLE: drone_mode = DRONE_TRACK; break;
    case Hothouse::TOGGLESWITCH_DOWN:   drone_mode = DRONE_TRACK_DIRECT; break;
    default: break;
  }

  // --- Pitch controls (mode-dependent) ---
  float midi_note = 0.f;
  float fine = Mapf(hw.GetKnobValue(Hothouse::KNOB_3), -0.5f, 0.5f);

  if (drone_mode == DRONE_FIXED) {
    // K1: ±12 semitone offset from A, center dead zone = A
    float k1 = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_1));
    int semi_offset = MapDetuneKnob(k1, 12);
    // Store the selected note as wrap point for tracking mode
    wrap_note = (9 + semi_offset % 12 + 12) % 12;
    // K2: octave (7 positions: C-1 through C5)
    int octave = Quantize(hw.GetKnobValue(Hothouse::KNOB_2), 7);
    int base_note = 12 + octave * 12;
    midi_note = static_cast<float>(base_note + 9 + semi_offset);
  } else if (drone_mode == DRONE_TRACK) {
    // Extract pitch class, wrapping at the note set by mode 1's K1.
    // Octave errors are harmless: E2 and E3 both give the same pitch class.
    int tracked = static_cast<int>(tracker.GetMidiNote());
    int pitch_class = ((tracked - wrap_note) % 12 + 12) % 12;
    // K1: semitone offset (-12 to +12, center dead zone = unison)
    float k1 = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_1));
    int semi_offset = MapDetuneKnob(k1, 12);
    // K2: target octave (same as fixed mode: C-1 through C5)
    int octave = Quantize(hw.GetKnobValue(Hothouse::KNOB_2), 7);
    int base_note = 12 + octave * 12;
    midi_note = static_cast<float>(base_note + wrap_note + pitch_class + semi_offset);
  } else {
    // DRONE_TRACK_DIRECT: osc follows exact tracked pitch, K1/K2 are relative offsets
    float tracked = tracker.GetMidiNote();
    float k1 = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_1));
    int semi_offset = MapDetuneKnob(k1, 12);
    int oct_offset = Quantize(hw.GetKnobValue(Hothouse::KNOB_2), 7) - 3;
    midi_note = tracked + static_cast<float>(semi_offset + oct_offset * 12);
  }

  float freq1 = MidiToFreq(midi_note + fine);

  // K4: tone / wavefold
  float k4 = hw.GetKnobValue(Hothouse::KNOB_4);
  float fold_amount = 0.f;

  float base_cutoff;
  if (wf == MoogOsc::TRI) {
    float cutoff_knob = (k4 < 0.5f) ? (k4 * 2.f) : 1.f;
    base_cutoff = MapCutoff(cutoff_knob);
    if (k4 > 0.5f) fold_amount = (k4 - 0.5f) * 2.f;
  } else {
    base_cutoff = MapCutoff(k4);
  }
  // Envelope opens the filter slightly when playing harder
  float mod_cutoff = base_cutoff * (1.f + last_env * ENV_FILTER_MOD * 20.f);
  if (mod_cutoff > 10000.f) mod_cutoff = 10000.f;
  ladder.SetCutoff(mod_cutoff + LADDER_CUTOFF_OFFSET);
  ladder.SetDrive(LADDER_DRIVE);

  // --- K5: osc2 detune (-12 to +12 semitones, center dead zone) ---
  float k5 = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_5));
  int osc2_semi = MapDetuneKnob(k5, 12);
  float osc2_level = (osc2_semi == 0 && k5 >= 0.46f && k5 <= 0.54f) ? 0.f : 1.f;

  // Osc2 frequency: same base note (no fine tune), offset by detune semitones
  float freq2 = MidiToFreq(midi_note + static_cast<float>(osc2_semi));

  // K6: mix (dry/wet blend, 0 = full dry, 1 = full wet)
  float mix = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_6));

  // Render audio
  for (size_t i = 0; i < size; i++) {
    if (bypass) {
      out[0][i] = out[1][i] = in[0][i];
    } else {
      float dry = in[0][i];

      // Envelope follower tracks input amplitude
      float env_val = env.Process(dry);

      // Feed pitch tracker (cheap — just filters and buffers)
      if (drone_mode != DRONE_FIXED) tracker.Feed(dry, env_val);

      // Both oscillators — wavefold always applied in TRI mode
      float o1 = osc1.Process(freq1);
      float o2 = osc2.Process(freq2);
      if (wf == MoogOsc::TRI) {
        // Envelope adds dynamic grit, scaled up to be audible, capped for safety
        float dyn_fold = fold_amount + env_val * ENV_FOLD_MOD * 5.f;
        if (dyn_fold > 1.f) dyn_fold = 1.f;
        o1 = Wavefold(o1, dyn_fold);
        o2 = Wavefold(o2, dyn_fold);
      }
      o2 *= osc2_level;

      // Update smoothed envelope for next block's filter modulation
      last_env += 0.1f * (env_val - last_env);

      // Unity gain compensation
      float gain = 1.f / sqrtf(1.f + osc2_level * osc2_level);
      float osc_mix = (o1 + o2) * gain;

      // Ladder filter → VCA → mix
      float filtered = ladder.Process(osc_mix);
      float wet = filtered * env_val * OSC_GAIN;

      // Equal-power crossfade — no volume dip at center
      float dry_gain = sqrtf(1.f - mix);
      float wet_gain = sqrtf(mix);
      out[0][i] = out[1][i] = dry * DRY_TRIM * dry_gain + wet * wet_gain;
    }
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

  led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);
  led_status.Init(hw.seed.GetPin(Hothouse::LED_1), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  hw.seed.StartLog(false);

  uint32_t log_counter = 0;

  while (true) {
    hw.DelayMs(10);
    led_blink_counter++;

    // --- Serial: print raw knob values every ~2 s (200 × 10 ms) ---
    // Only print when USB CDC host is connected (prevents freeze on disconnect)
    if (++log_counter >= 200) {
      log_counter = 0;
      if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        float k1 = hw.GetKnobValue(Hothouse::KNOB_1);
        float k2 = hw.GetKnobValue(Hothouse::KNOB_2);
        float k3 = hw.GetKnobValue(Hothouse::KNOB_3);
        float k4 = hw.GetKnobValue(Hothouse::KNOB_4);
        float k5 = hw.GetKnobValue(Hothouse::KNOB_5);
        float k6 = hw.GetKnobValue(Hothouse::KNOB_6);
        hw.seed.PrintLine("[KNOB] k1=" FLT_FMT3 " k2=" FLT_FMT3
                          " k3=" FLT_FMT3 " k4=" FLT_FMT3
                          " k5=" FLT_FMT3 " k6=" FLT_FMT3,
                          FLT_VAR3(k1), FLT_VAR3(k2),
                          FLT_VAR3(k3), FLT_VAR3(k4),
                          FLT_VAR3(k5), FLT_VAR3(k6));
      }
    }

    // --- LED 2: bypass indicator (on = effect active) ---
    led_bypass.Set(bypass ? 0.f : 1.f);
    led_bypass.Update();

    // --- LED 1: waveform indicator (blink rate) ---
    bool led1_on = false;
    switch (osc1.waveform) {
      case MoogOsc::SAW:    led1_on = true; break;
      case MoogOsc::TRI:    led1_on = (led_blink_counter % 100) < 50; break;
      case MoogOsc::SQUARE: led1_on = (led_blink_counter % 25) < 12;  break;
    }
    led_status.Set(led1_on ? 1.f : 0.f);
    led_status.Update();

    // --- Pitch tracker: run YIN in main loop (heavy, not time-critical) ---
    tracker.Update();

    // --- FS1: hold 2 s → DFU bootloader ---
    hw.CheckResetToBootloader();
  }

  return 0;
}
