#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"
#include "constants.h"
#include "moog_osc.h"
#include "moog_ladder.h"
#include "env_follower.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;

Hothouse hw;

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------
MoogOsc     osc1;
MoogOsc     osc2;
MoogLadder  ladder;
EnvFollower env;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
Led led_bypass;
Led led_status;
bool bypass = true;

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

// Remap pot range — physical pots don't reach true 0.0/1.0
static float RemapKnob(float raw) {
  float v = (raw - 0.01f) / 0.96f;
  if (v < 0.f) v = 0.f;
  if (v > 1.f) v = 1.f;
  return v;
}

// Wavefold: drive signal and reflect at ±PEAK boundaries
// Normalized so triangle's 1.4x boost passes through at amount=0
// amount 0 = passthrough, 1 = heavily folded
static float Wavefold(float x, float amount) {
  constexpr float PEAK = 1.4f;  // matches triangle boost in moog_osc.h
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

  // --- Pitch controls ---
  // K1: semitone (0–11 quantized, C through B)
  int semitone = Quantize(hw.GetKnobValue(Hothouse::KNOB_1), 12);

  // K2: octave (7 positions: C-1 through C5)
  int octave = Quantize(hw.GetKnobValue(Hothouse::KNOB_2), 7);
  int base_note = 12 + octave * 12;

  // K3: fine tune (±50 cents = ±0.5 semitones) — osc1 only
  float fine = Mapf(hw.GetKnobValue(Hothouse::KNOB_3), -0.5f, 0.5f);

  float midi_note = static_cast<float>(base_note + semitone);
  float freq1 = MidiToFreq(midi_note + fine);

  // K4: tone / wavefold
  // In SAW/SQUARE: full range controls ladder cutoff (80 Hz – 8 kHz)
  // In TRI: CCW→noon = cutoff (80 Hz – 8 kHz), noon→CW = wavefolding (filter stays open)
  float k4 = hw.GetKnobValue(Hothouse::KNOB_4);
  float fold_amount = 0.f;

  if (wf == MoogOsc::TRI) {
    float cutoff_knob = (k4 < 0.5f) ? (k4 * 2.f) : 1.f;  // full open at noon
    ladder.SetCutoff(MapCutoff(cutoff_knob) + LADDER_CUTOFF_OFFSET);
    if (k4 > 0.5f) fold_amount = (k4 - 0.5f) * 2.f;
  } else {
    ladder.SetCutoff(MapCutoff(k4) + LADDER_CUTOFF_OFFSET);
  }
  ladder.SetDrive(LADDER_DRIVE);

  // --- K5: osc2 detune (-12 to +12 semitones) ---
  // Dead zone at center = osc2 off. Outside = ±1–12 semitone steps.
  constexpr float DEAD_LO = 0.46f;
  constexpr float DEAD_HI = 0.54f;

  float k5 = RemapKnob(hw.GetKnobValue(Hothouse::KNOB_5));
  float osc2_level = 0.f;
  int   osc2_semi  = 0;

  if (k5 >= DEAD_LO && k5 <= DEAD_HI) {
    // Dead zone — osc2 off
    osc2_level = 0.f;
  } else if (k5 < DEAD_LO) {
    // Detune down: 0.0 → -12, approaching DEAD_LO → -1
    osc2_level = 1.f;
    float pos = k5 / DEAD_LO;
    osc2_semi = -(12 - Quantize(pos, 12));
  } else {
    // Detune up: DEAD_HI → +1, 1.0 → +12
    osc2_level = 1.f;
    float pos = (k5 - DEAD_HI) / (1.f - DEAD_HI);
    osc2_semi = Quantize(pos, 12) + 1;
  }

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

      // Both oscillators — wavefold always applied in TRI mode (no-op at amount=0)
      float o1 = osc1.Process(freq1);
      float o2 = osc2.Process(freq2);
      if (wf == MoogOsc::TRI) {
        o1 = Wavefold(o1, fold_amount);
        o2 = Wavefold(o2, fold_amount);
      }
      o2 *= osc2_level;

      // Unity gain compensation: scale by 1/sqrt(1 + level²)
      float gain = 1.f / sqrtf(1.f + osc2_level * osc2_level);
      float osc_mix = (o1 + o2) * gain;

      // Ladder filter → VCA → mix
      float filtered = ladder.Process(osc_mix);
      float wet = filtered * env_val * OSC_GAIN;

      out[0][i] = out[1][i] = dry * DRY_TRIM * (1.f - mix) + wet * mix;
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

  led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);
  led_status.Init(hw.seed.GetPin(Hothouse::LED_1), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);
    led_blink_counter++;

    // --- LED 2: bypass indicator (on = effect active) ---
    led_bypass.Set(bypass ? 0.f : 1.f);
    led_bypass.Update();

    // --- LED 1: waveform indicator (blink rate) ---
    //   Saw    = solid on
    //   Tri    = slow blink (~1 Hz)
    //   Square = fast blink (~4 Hz)
    bool led1_on = false;
    switch (osc1.waveform) {
      case MoogOsc::SAW:    led1_on = true; break;
      case MoogOsc::TRI:    led1_on = (led_blink_counter % 100) < 50; break;
      case MoogOsc::SQUARE: led1_on = (led_blink_counter % 25) < 12;  break;
    }
    led_status.Set(led1_on ? 1.f : 0.f);
    led_status.Update();

    // --- FS1: hold 2 s → DFU bootloader ---
    hw.CheckResetToBootloader();
  }

  return 0;
}
