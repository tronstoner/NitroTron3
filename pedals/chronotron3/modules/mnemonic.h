#pragma once
//
// mnemonic — tap-tempo delay. UNSPECCED (working title). Placeholder module:
// clean passthrough until it gets a spec. SW3 MIDDLE.
//
#include "module.h"

class Mnemonic : public Module {
 public:
  void Init(float sr) override { sr_ = sr; }

  void Controls(const ControlSurface& /*cs*/,
                daisy::Led& led1, daisy::Led& led2) override {
    // Placeholder: both LEDs off.
    led1.Set(0.f);
    led2.Set(0.f);
  }

  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) wet[i] = in[i];  // passthrough
  }

 private:
  float sr_ = CT3_SAMPLE_RATE_HZ;
};
