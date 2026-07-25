#pragma once
//
// vestige — dynamic looper / freeze (grain-based).  SW3 UP.
//
// SCAFFOLD STUB — passthrough placeholder so the bundle builds. The real
// stage-1 implementation (grain looper: FS2 record, FS1 stop/clear, K3
// looper↔freeze macro, K1 voices, K6 mix) replaces this file; keep the class
// name `Vestige` and the Module interface.
// Spec: docs/ChronoTron3/dynamic-looper-concept.md
//
#include "module.h"

class Vestige : public Module {
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
