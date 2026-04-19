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
MoogOsc     osc;
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

// ---------------------------------------------------------------------------
// Audio callback
// ---------------------------------------------------------------------------
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  // Bypass toggle — FOOTSWITCH_2
  bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();

  // Waveform select — Switch 1
  switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)) {
    case Hothouse::TOGGLESWITCH_UP:     osc.waveform = MoogOsc::SAW;    break;
    case Hothouse::TOGGLESWITCH_MIDDLE: osc.waveform = MoogOsc::TRI;    break;
    case Hothouse::TOGGLESWITCH_DOWN:   osc.waveform = MoogOsc::SQUARE; break;
    default: break;
  }

  // --- Pitch controls ---
  // K1: semitone (0–11 quantized, C through B)
  int semitone = Quantize(hw.GetKnobValue(Hothouse::KNOB_1), 12);

  // K2: octave (7 positions: C-1 through C5)
  int octave = Quantize(hw.GetKnobValue(Hothouse::KNOB_2), 7);
  int base_note = 12 + octave * 12;

  // K3: fine tune (±50 cents = ±0.5 semitones)
  float fine = Mapf(hw.GetKnobValue(Hothouse::KNOB_3), -0.5f, 0.5f);

  float midi_note = static_cast<float>(base_note + semitone) + fine;
  float freq = MidiToFreq(midi_note);

  // K4: tone — ladder cutoff (exponential 80 Hz – 8 kHz)
  float cutoff = MapCutoff(hw.GetKnobValue(Hothouse::KNOB_4));
  ladder.SetCutoff(cutoff + LADDER_CUTOFF_OFFSET);
  ladder.SetDrive(LADDER_DRIVE);

  // K6: mix (dry/wet blend, 0 = full dry, 1 = full wet)
  float mix = hw.GetKnobValue(Hothouse::KNOB_6);
  // Remap pot range — physical pots don't reach true 0.0/1.0
  mix = (mix - 0.01f) / 0.96f;
  if (mix < 0.f) mix = 0.f;
  if (mix > 1.f) mix = 1.f;

  // Render audio
  for (size_t i = 0; i < size; i++) {
    if (bypass) {
      out[0][i] = out[1][i] = in[0][i];
    } else {
      float dry = in[0][i];

      // Envelope follower tracks input amplitude
      float env_val = env.Process(dry);

      // Oscillator → ladder filter → VCA (envelope controls amplitude)
      float osc_out  = osc.Process(freq);
      float filtered = ladder.Process(osc_out);
      float wet      = filtered * env_val * OSC_GAIN;

      // Mix dry + wet
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
  osc.Init(sr);
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
    switch (osc.waveform) {
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
