#pragma once
//
// ChronoTron3 — Module interface (the swappable unit).
//
// A module = one selectable mode (vestige / mnemonic / ignis). It owns its
// control mapping, its footswitch policy, and its DSP. The shell (main.cpp)
// owns: SW3 mode selection, the K6 dry/wet mix (equal-power), and the reserved
// both-footswitch bootloader gesture. Everything else on the surface is the
// active module's to define.
//
// Requires daisy.h + hothouse.h + control_surface.h included before this file.
//
// Two rates:
//   Controls() — control-rate, called from the main loop each Tick (~10 ms),
//                after the shell has Tick()'d the ControlSurface. Read knobs /
//                switches / footswitch events, update LEDs, set params.
//   Process()  — audio-rate, called from the audio callback each block. Produce
//                mono WET output; the shell crossfades dry/wet via K6.

class Module {
 public:
  virtual ~Module() {}

  // One-time setup. sr = audio sample rate (Hz).
  virtual void Init(float sr) = 0;

  // Mode transitions (SW3). Default no-op; override to reset/seed state.
  virtual void Activate() {}
  virtual void Deactivate() {}

  // Control-rate hook (main loop). `cs` is already Tick()'d this pass.
  virtual void Controls(const ControlSurface& cs,
                        daisy::Led& led1, daisy::Led& led2) = 0;

  // Audio-rate hook. `in` = mono input block, `wet` = mono wet output block to
  // fill (size samples). The shell mixes wet against dry via K6.
  virtual void Process(const float* in, float* wet, size_t size) = 0;
};
