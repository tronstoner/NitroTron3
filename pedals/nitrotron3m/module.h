#pragma once
//
// NitroTron3m — Module interface (the swappable unit). The class below is an
// unchanged copy of pedals/nitrolab/module.h (= pedals/chronotron3/module.h), so
// modules of either pedal compile here as they are; only this comment differs.
//
// On this pedal the shell installs a pedal-level policy (the NitroTron3 preset
// system): it owns both footswitches, both LEDs, bypass, the bootloader gesture
// and the mode (SW3 or the recalled preset's slot). The `ControlSurface` a
// module receives is preset_controls.h — the preset system's edit buffer, with
// idle footswitch events — and the LEDs it receives are sinks that are never
// shown. The modules (bordun / sprawl or armitage / schism) supply controls +
// DSP only.
//
// Requires daisy.h + hothouse.h + preset_controls.h included before this file.
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
  // fill (size samples). The shell mixes wet against dry via K6 — unless the
  // module OwnsOutput() (below), in which case `wet` is taken as the final out.
  virtual void Process(const float* in, float* wet, size_t size) = 0;

  // Optional pre-effect on the INPUT ("a pedal in front"): the shell applies it
  // to the input block BEFORE both Process() and its own dry path, so dry and
  // send are both processed. Default: none. In bypass the module must leave
  // `buf` untouched (the clean-bypass guarantee below relies on it).
  virtual void PreProcess(float* buf, size_t size) { (void)buf; (void)size; }

  // If true, the module has already routed the dry itself (e.g. looper volume +
  // dry bypass) and fills `wet` with the FINAL output; the shell outputs it
  // directly and skips the K6 crossfade. Default false = shell does the mix.
  virtual bool OwnsOutput() const { return false; }

  // If true, the module is bypassed: the shell must pass the CLEAN signal fully
  // UNTOUCHED (dry gain forced to unity, ignoring the K6 mix) — there is no
  // hardware bypass on this pedal, so this is the only guarantee that the clean
  // path is never attenuated/processed in bypass. Wet (any ringing trail) is
  // still mixed as usual. Only consulted when OwnsOutput() is false.
  virtual bool Bypassed() const { return false; }
};
