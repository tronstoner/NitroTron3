#pragma once
//
// ignis — impulse synth / resonator / drone.  SW3 DOWN.
//
// SCAFFOLD STUB — passthrough placeholder so the bundle builds. The real
// stage-1 implementation (input conditioning + resonator bank + output filter)
// replaces this file; keep the class name `Ignis` and the Module interface.
// Spec: docs/ChronoTron3/impulse resonator - ignis/IMPULSE_SYNTH_SPEC.md
//
#include "module.h"

class Ignis : public Module {
 public:
  void Init(float sr) override { sr_ = sr; }

  void Controls(const ControlSurface& /*cs*/,
                daisy::Led& led1, daisy::Led& led2) override {
    led1.Set(0.f);
    led2.Set(0.f);
  }

  void Process(const float* in, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) wet[i] = in[i];  // STUB passthrough
  }

 private:
  float sr_ = CT3_SAMPLE_RATE_HZ;
};
