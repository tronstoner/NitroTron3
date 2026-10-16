#pragma once
//
// NitroLab — empty pass-through placeholder module.
//
// Used for SW3 MIDDLE and SW3 DOWN until a real module is wired into those
// slots. Produces silence as its wet signal; the shell's K6 equal-power
// dry/wet mix then passes the dry signal through at any mix setting (at
// K6 fully wet, output is silence — there is nothing else to mix in).
// No controls, no LEDs, no state.

class Empty : public Module {
 public:
  void Init(float /*sr*/) override {}

  void Controls(const ControlSurface& /*cs*/,
                daisy::Led& led1, daisy::Led& led2) override {
    led1.Set(0.f);
    led2.Set(0.f);
  }

  void Process(const float* /*in*/, float* wet, size_t size) override {
    for (size_t i = 0; i < size; i++) wet[i] = 0.f;
  }
};
