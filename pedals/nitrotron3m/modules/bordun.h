#pragma once
//
// bordun — NitroTron3 Mode A (Drone), carved VERBATIM from
// pedals/nitrotron3/main.cpp into the Module interface.
//
// What moved here (no behaviour change, no tuning change):
//   * the Mode A state (main.cpp:38-40 drone_osc / ladder / mode_a_hpf,
//     main.cpp:270 last_env, main.cpp:232 smix_a) -> members;
//   * the drone sub-mode enum (main.cpp:261-263) and the Mode A helpers
//     MapDetuneKnob + Wavefold (main.cpp:282-306) -> file scope;
//   * ProcessDrone's body (main.cpp:429-550) -> Process(), character for
//     character. The only lines around it are the adapters that give the body
//     the names it had: `in` / `out` (mono in, final out written to `wet` — the
//     module owns its output, the shell does not mix K6), `eb` (the edit-buffer
//     copy taken in Controls()), and `env` / `tracker` (the shell-owned
//     NT3Shared instances, nt3_shared.h).
//
// Controls (unchanged): K1 detune, K2 octave, K3 fine, K4 filter / fold, K5 unison
// cloud / FM, K6 mix (own equal-power law), SW1 waveform, SW2 drone sub-mode.
// No footswitches, no LEDs (the preset system owns both).
//
// Requires daisy.h + hothouse.h + constants.h + knob_map.h + preset_controls.h
// + module.h + nt3_shared.h included before this file.

// ---- verbatim from pedals/nitrotron3/main.cpp:260-263 ----
// ---------------------------------------------------------------------------
// Drone sub-modes (Switch 2)
// ---------------------------------------------------------------------------
enum DroneMode { DRONE_FIXED, DRONE_TRACK, DRONE_TRACK_DIRECT };

// ---- verbatim from pedals/nitrotron3/main.cpp:282-306 ----
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

class Bordun : public Module {
 public:
  // Shell-owned shared state (env follower + pitch tracker). Call before Init().
  void Attach(NT3Shared* sh) { sh_ = sh; }

  // pedals/nitrotron3/main.cpp:1619-1621 (the Mode A part of main()'s init).
  void Init(float sr) override {
    drone_osc.Init(sr);
    ladder.Init(sr);
    mode_a_hpf.Init(sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& /*led1*/, daisy::Led& /*led2*/) override {
    NT3ReadControls(cs, eb_);
  }

  // The shipping mode computes its own dry/wet (K6): the shell passes `wet`
  // through as the final output.
  bool OwnsOutput() const override { return true; }

  void Process(const float* in_mono, float* wet, size_t size) override {
    // Adapters: the names ProcessDrone's body uses.
    const float* const in[1]  = { in_mono };
    float* const       out[2] = { wet, wet };   // mono pedal: out[0] = out[1]
    EnvFollower&  env     = sh_->env;
    PitchTracker& tracker = sh_->tracker;
    const NT3Controls& eb = eb_;

    // ---- verbatim from pedals/nitrotron3/main.cpp:429-550 (ProcessDrone) ----

  // Waveform select — from edit buffer SW1
  MoogOsc::Waveform wf = MoogOsc::SAW;
  switch (eb.sw1) {
    case 0: wf = MoogOsc::SAW;    break;  // UP
    case 1: wf = MoogOsc::TRI;    break;  // MIDDLE
    case 2: wf = MoogOsc::SQUARE; break;  // DOWN
    default: break;
  }
  drone_osc.SetWaveform(wf);

  // Drone sub-mode — from edit buffer SW2
  DroneMode drone_mode = DRONE_FIXED;
  switch (eb.sw2) {
    case 0: drone_mode = DRONE_FIXED;         break;  // UP
    case 1: drone_mode = DRONE_TRACK;         break;  // MIDDLE
    case 2: drone_mode = DRONE_TRACK_DIRECT;  break;  // DOWN
    default: break;
  }

  // --- Pitch controls (from edit buffer knobs, remapped) ---
  float midi_note = 0.f;
  float fine = Mapf(RemapKnob(eb.knobs[2]), -0.5f, 0.5f);  // K3

  if (drone_mode == DRONE_FIXED) {
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    int octave = Quantize(RemapKnob(eb.knobs[1]), 7) + MODE_A_K2_OCTAVE_SHIFT;  // K2
    int base_note = 12 + octave * 12;
    midi_note = static_cast<float>(base_note + TRACKING_WRAP_NOTE + semi_offset);
  } else if (drone_mode == DRONE_TRACK) {
    // Octave-locked, CONTINUOUS: fold the tracked pitch class into K2's octave
    // without quantizing to a semitone, so a detuned / microtonal bass is
    // preserved. Fold boundary is G# (TRACKING_FOLD_NOTE), one semitone below A,
    // so a played A sits mid-octave and doesn't flip when it drifts flat/sharp.
    static int drone_fold_k = 0;
    float cont = tracker.GetMidiNoteContinuous();
    float folded = (cont - static_cast<float>(TRACKING_FOLD_NOTE))
                   - 12.f * static_cast<float>(drone_fold_k);
    while (folded >= 12.f + PITCH_FOLD_HYSTERESIS_SEMI) { drone_fold_k++; folded -= 12.f; }
    while (folded <  0.f  - PITCH_FOLD_HYSTERESIS_SEMI) { drone_fold_k--; folded += 12.f; }
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    int octave = Quantize(RemapKnob(eb.knobs[1]), 7) + MODE_A_K2_OCTAVE_SHIFT;
    int base_note = 12 + (octave + TRACKING_OCTAVE_SHIFT) * 12;
    midi_note = static_cast<float>(base_note + TRACKING_FOLD_NOTE + semi_offset) + folded;
  } else {
    // DRONE_TRACK_DIRECT — continuous pitch so the drone follows bends/slides.
    float tracked = tracker.GetMidiNoteContinuous();
    float k1 = RemapKnob(eb.knobs[0]);
    int semi_offset = MapDetuneKnob(k1, 12);
    // -1: K2 noon = A3 for a played A, matching the fixed drone and octave-locked.
    int oct_offset = Quantize(RemapKnob(eb.knobs[1]), 7) - 1 + MODE_A_K2_OCTAVE_SHIFT;
    midi_note = tracked + static_cast<float>(semi_offset + oct_offset * 12);
  }

  float freq1 = MidiToFreq(midi_note + fine);

  // K4: bipolar filter (saw/square) or LP-sweep + wavefold (triangle)
  float k4 = RemapKnob(eb.knobs[3]);
  float fold_amount = 0.f;
  float base_cutoff;
  float hpf_cutoff = MODE_A_HPF_MIN_HZ;   // transparent unless K4 is on the CW/HPF side

  if (wf == MoogOsc::TRI) {
    // Triangle path unchanged: CCW LP sweep + CW wavefold; HPF stays transparent.
    float cutoff_knob = (k4 < 0.5f) ? (k4 * 2.f) : 1.f;
    base_cutoff = MapCutoff(cutoff_knob);
    if (k4 > 0.5f) fold_amount = (k4 - 0.5f) * 2.f;
  } else if (k4 <= 0.5f) {
    // Saw/square CCW: ladder LP from wide open (noon) down to a raised floor.
    float t = k4 / 0.5f;   // 0 at full CCW, 1 at noon
    base_cutoff = MODE_A_LP_FLOOR_HZ * powf(MODE_A_LP_MAX_HZ / MODE_A_LP_FLOOR_HZ, t);
  } else {
    // Saw/square CW: ladder held wide open, HPF fades in to thin the low end.
    base_cutoff = MODE_A_LP_MAX_HZ;
    float t = (k4 - 0.5f) / 0.5f;   // 0 at noon, 1 at full CW
    hpf_cutoff = MODE_A_HPF_MIN_HZ * powf(MODE_A_HPF_MAX_HZ / MODE_A_HPF_MIN_HZ, t);
  }

  float mod_cutoff = base_cutoff * (1.f + last_env * ENV_FILTER_MOD * 20.f);
  if (mod_cutoff > 10000.f) mod_cutoff = 10000.f;
  ladder.SetCutoff(mod_cutoff + LADDER_CUTOFF_OFFSET);
  // Drive the ladder harder as the LP closes (noon → full CCW) so closed settings
  // are fat/saturated rather than muffled. Not applied on the HPF (CW) side.
  float ladder_drive = LADDER_DRIVE;
  if (k4 < 0.5f)
    ladder_drive += (0.5f - k4) * 2.f * (MODE_A_LADDER_DRIVE_CCW_MAX - LADDER_DRIVE);
  ladder.SetDrive(ladder_drive);
  mode_a_hpf.SetCutoff(hpf_cutoff);

  // K5: bipolar — CCW = detuned unison cloud, noon = single osc, CW = audio-rate FM
  float k5 = RemapKnob(eb.knobs[4]);

  // K6: mix
  float mix = RemapKnob(eb.knobs[5]);

  for (size_t i = 0; i < size; i++) {
    float dry = in[0][i];

    float env_val = env.Process(dry);

    if (drone_mode != DRONE_FIXED) tracker.Feed(dry, env_val);

    float osc_mix = drone_osc.Process(freq1, k5, dry, env_val);
    if (wf == MoogOsc::TRI) {
      float dyn_fold = fold_amount + env_val * ENV_FOLD_MOD * 5.f;
      if (dyn_fold > 1.f) dyn_fold = 1.f;
      osc_mix = Wavefold(osc_mix, dyn_fold);
    }

    last_env += 0.1f * (env_val - last_env);

    float filtered = ladder.Process(osc_mix);
    filtered = mode_a_hpf.Process(filtered);   // K4-CW high-pass (transparent otherwise)
    float wet = filtered * EnvExpand(env_val) * OSC_GAIN;

    const float m = MixCurve(smix_a(mix));  // smoothed mix → no K6 zipper
    float dry_gain = sqrtf(1.f - m);
    float wet_gain = sqrtf(m);
    out[0][i] = out[1][i] = dry * DRY_TRIM * dry_gain + wet * wet_gain;
  }
  }

 private:
  NT3Shared*  sh_ = nullptr;
  NT3Controls eb_ = {};

  // ---- verbatim from pedals/nitrotron3/main.cpp:38-40 ----
DroneOsc     drone_osc;  // Mode A — unison cloud (K5 CCW) + audio-rate FM (K5 CW)
MoogLadder   ladder;     // Mode A
ModeAHpf     mode_a_hpf; // Mode A — K4-CW high-pass (saw/square)
  // ---- from pedals/nitrotron3/main.cpp:270 and :232 ----
float last_env = 0.f;   // smoothed envelope for per-block modulation
Smoother smix_a;                          // A: K6 (raw mix → MixCurve)
};
