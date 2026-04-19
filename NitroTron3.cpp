#include "daisysp.h"
#include "hothouse.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;

Hothouse hw;

// Bypass vars
Led led_bypass;
bool bypass = true;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  // Toggle bypass when FOOTSWITCH_2 is pressed
  bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();

  for (size_t i = 0; i < size; ++i) {
    if (bypass) {
      out[0][i] = out[1][i] = in[0][i];
    } else {
      // TODO: replace silence with something awesome
      out[0][i] = out[1][i] = 0.0f;
    }
  }
}

int main() {
  hw.Init();
  hw.SetAudioBlockSize(48);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);

    led_bypass.Set(bypass ? 0.0f : 1.0f);
    led_bypass.Update();

    hw.CheckResetToBootloader();
  }
  return 0;
}
