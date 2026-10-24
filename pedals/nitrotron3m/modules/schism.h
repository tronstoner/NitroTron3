#pragma once
//
// schism — NitroTron3 Mode C (Schism), carved VERBATIM from
// pedals/nitrotron3/main.cpp into the Module interface.
//
// What moved here (no behaviour change, no tuning change):
//   * the Mode C state (main.cpp:41-51, 234-240, 1202-1235) -> members;
//   * the Mode C helpers MapCutoffModeC, WrapFold (parked), Chebyshev and
//     Saturate (main.cpp:275-280, 308-344) -> file scope;
//   * ProcessFreqShift's body (main.cpp:1240-1584) -> Process(), character for
//     character, behind the same adapters as bordun.h (`in` / `out` / `eb` /
//     `env` / `tracker`);
//   * the Mode C part of main()'s init (main.cpp:1622-1628, 1632-1640,
//     1650-1653) -> Init().
//
// Controls (unchanged): K1 cutoff / vowel / phaser, K2 resonance / size, K3 env
// amount / LFO, K4 drive character, K5 drive, K6 mix (own law), SW1 drive
// stage, SW2 filter. No footswitches, no LEDs (the preset system owns both).
//
// Requires daisy.h + hothouse.h + constants.h + knob_map.h + preset_controls.h
// + module.h + nt3_shared.h included before this file.

// ---- verbatim from pedals/nitrotron3/main.cpp:275-280 ----
// Mode C cutoff mapping — extended low end (30 Hz) for a deeper "shut" position.
// Top end matches Mode A so env-mod headroom toward MODE_C_CUTOFF_MAX_HZ behaves the same.
static float MapCutoffModeC(float knob) {
  return MODE_C_CUTOFF_MIN_HZ *
         powf(MODE_C_CUTOFF_K1_MAX_HZ / MODE_C_CUTOFF_MIN_HZ, knob);
}

// ---- verbatim from pedals/nitrotron3/main.cpp:308-344 ----
// Digital wraparound (Mode C SW1=MID, K4 CCW). PARKED — Chebyshev is active in
// the slot; kept so the CCW branch can switch back. Wraps x modulo [-1, 1] like
// an overflowing DAC: as the driven signal climbs past +1 it jumps to -1 and
// keeps going. Sawtooth discontinuities → harsh buzzy digital distortion.
__attribute__((unused)) static float WrapFold(float x) {
  float y = fmodf(x + 1.f, 2.f);
  if (y < 0.f) y += 2.f;
  return y - 1.f;
}

// Chebyshev waveshaper (Mode C SW1=MID, K4 CCW). Input must be pre-driven and
// clamped to [-1, 1]. Sums T2..T5 — each a pure n-th-harmonic generator — per
// the MODE_C_CHEBY_H* mix → metallic / ring-mod timbre. The raw value at x=0 is
// removed so silent input → silent output (T2/T4 carry constant terms).
static float Chebyshev(float x) {
  const float x2 = x * x;
  const float t2 = 2.f * x2 - 1.f;
  const float t3 = (4.f * x2 - 3.f) * x;
  const float t4 = (8.f * x2 - 8.f) * x2 + 1.f;
  const float t5 = ((16.f * x2 - 20.f) * x2 + 5.f) * x;
  const float dc = -MODE_C_CHEBY_H2 + MODE_C_CHEBY_H4;  // raw value at x=0
  return MODE_C_CHEBY_H2 * t2 + MODE_C_CHEBY_H3 * t3 +
         MODE_C_CHEBY_H4 * t4 + MODE_C_CHEBY_H5 * t5 - dc;
}

// Shared symmetric saturation primitive for the Mode C SW1=MID overdrive (TS →
// amp). Both the pedal and amp stages use this one curve; asymmetry is added
// only by a bias offset at the call site (Saturate(x+bias) − Saturate(bias)),
// never baked into the curve.
// ACTIVE: clamped cubic x − x³/3 (unity slope at 0, knee at |x|=1, hard-clamped
// flat at ±2/3 beyond) — stays linear longer at low level then clips harder.
// Alternative (warmer/rounder, asymptotic, never hard-clips): x/(1+|x|).
static inline float Saturate(float x) {
  if (x >=  1.f) return  2.f / 3.f;
  if (x <= -1.f) return -2.f / 3.f;
  return x - x * x * x * (1.f / 3.f);
}

class Schism : public Module {
 public:
  // Shell-owned shared state (env follower + pitch tracker). Call before Init().
  void Attach(NT3Shared* sh) { sh_ = sh; }

  void Init(float sr) override {
    // ---- verbatim from pedals/nitrotron3/main.cpp:1622-1628 ----
  ladder_c_v2.Init(sr);
  phaser_c.Init(sr);
  grendel_c.Init(sr);
  limiter_c.Init(sr);
  bitcrush_c.Init();
  synth_c.Init(sr);
  polyoct_c.Init(sr);
    // ---- verbatim from pedals/nitrotron3/main.cpp:1632-1640 ----
  // Mode C filter-env smoother: seed atk/rel coefs at the noon (base) times;
  // ProcessFreqShift rescales them per block from K3 travel. coef = 1 - exp(-1 / (tau * sr))
  mode_c_sr = sr;
  env_c_filter_atk_coef = 1.f - expf(-1.f /
      (MODE_C_FILTER_ENV_ATTACK_MS  * 0.001f * sr));
  env_c_filter_rel_coef = 1.f - expf(-1.f /
      (MODE_C_FILTER_ENV_RELEASE_MS * 0.001f * sr));
  env_c_grendel_atk_coef = 1.f - expf(-1.f /
      (MODE_C_GRENDEL_ENV_ATTACK_MS  * 0.001f * sr));
    // ---- verbatim from pedals/nitrotron3/main.cpp:1650-1653 ----
  cheby_lp_g = 1.f - expf(-2.f * 3.14159265f * MODE_C_CHEBY_LP_HZ / sr);
  od_hp_g       = 1.f - expf(-2.f * 3.14159265f * MODE_C_OD_HP_HZ / sr);
  xorfuzz_hp_g  = 1.f - expf(-2.f * 3.14159265f * MODE_C_XOR_FUZZ_RECT_HP_HZ / sr);
  od_clean_lp_g = 1.f - expf(-2.f * 3.14159265f * MODE_C_OD_CLEAN_LP_HZ / sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& /*led1*/, daisy::Led& /*led2*/) override {
    NT3ReadControls(cs, eb_);
  }

  // The shipping mode computes its own dry/wet (K6): the shell passes `wet`
  // through as the final output.
  bool OwnsOutput() const override { return true; }

  void Process(const float* in_mono, float* wet, size_t size) override {
    // Adapters: the names ProcessFreqShift's body uses.
    const float* const in[1]  = { in_mono };
    float* const       out[2] = { wet, wet };   // mono pedal: out[0] = out[1]
    EnvFollower&  env     = sh_->env;
    PitchTracker& tracker = sh_->tracker;
    const NT3Controls& eb = eb_;

    // ---- verbatim from pedals/nitrotron3/main.cpp:1240-1584 (ProcessFreqShift) ----

  const uint8_t drive_mode  = eb.sw1;              // 0=UP sinefold, 1=MID bitcrush, 2=DOWN pitch-tracked synth
  const uint8_t filter_mode = eb.sw2;              // 0=UP Moog, 1=MID Grendel, 2=DOWN Phaser
  const float k1        = RemapKnob(eb.knobs[0]);
  const float k2        = RemapKnob(eb.knobs[1]);
  const float k3        = RemapKnob(eb.knobs[2]);
  const float fold_amt  = RemapKnob(eb.knobs[3]);  // K4
  const float drive_knob = RemapKnob(eb.knobs[4]); // K5: bipolar drive (CCW attenuate, noon unity, CW boost)
  const float mix       = RemapKnob(eb.knobs[5]);  // K6
  // Piecewise linear: [0..0.5] → [MIN..1.0], [0.5..1.0] → [1.0..MAX].
  const float drive_amt = (drive_knob < 0.5f)
      ? MODE_C_DRIVE_MIN + (drive_knob * 2.f) * (1.f - MODE_C_DRIVE_MIN)
      : 1.f + ((drive_knob - 0.5f) * 2.f) * (MODE_C_DRIVE_MAX - 1.f);
  // K5 fades in audio-rate cutoff self-FM for the Moog ladder (filter input as
  // the modulation source) — grit on the resonance. Begins a bit before noon
  // (MODE_C_MOOG_FM_START) so there's always a touch of FM, reaches max at CW.
  const float k5_fm_amt     = (drive_knob > MODE_C_MOOG_FM_START)
      ? (drive_knob - MODE_C_MOOG_FM_START) / (1.f - MODE_C_MOOG_FM_START)
      : 0.f;
  const float moog_fm_depth = k5_fm_amt * MODE_C_MOOG_FM_DEPTH;
  const float m         = MixCurve(mix);
  const float dry_gain  = sqrtf(1.f - m);
  const float wet_gain  = sqrtf(m);

  // K4 is bipolar around noon for SW1=UP and SW1=MID (noon = clean):
  //   CW  half drives the primary flavor (sine fold / bit-flip),
  //   CCW half drives the secondary flavor (Chebyshev / tanh overdrive).
  // SW1=DOWN keeps K4 as the full-range saw↔rect timbre morph (untouched).
  const float k4_cw  = (fold_amt > 0.5f) ? (fold_amt - 0.5f) * 2.f : 0.f;  // 0..1
  const float k4_ccw = (fold_amt < 0.5f) ? (0.5f - fold_amt) * 2.f : 0.f;  // 0..1

  // SW1=UP CW — sine wavefolder.
  const float fold_blend = (drive_mode == 0) ? k4_cw : 0.f;
  const float fold_drive = 1.f + fold_blend * SINEFOLD_DRIVE_MAX;
  const float fold_comp  = 1.f - fold_blend * (1.f - SINEFOLD_COMP_AT_MAX);
  // SW1=UP CCW — Chebyshev waveshaper (octave-up / metallic harmonic generator).
  const float cheby_blend = (drive_mode == 0) ? k4_ccw : 0.f;
  const float cheby_drive = 1.f + cheby_blend * (MODE_C_CHEBY_DRIVE_MAX - 1.f);
  // SW1=MID CCW — TS → amp overdrive. No dry/wet blend (clean is K6's job).
  // Gain staging across the CCW travel: the pedal (TS) stage builds FIRST —
  // subtle near noon via MODE_C_OD_K4_CURVE — while the amp stage holds at unity
  // until MODE_C_OD_AMP_KNEE, then ramps in only over the TOP of the travel.
  const float od_blend = (drive_mode == 1) ? k4_ccw : 0.f;  // raw amount: gate + comp
  const float od_ped_t = powf(od_blend, MODE_C_OD_K4_CURVE); // pedal gain ramp (curved)
  const float od_amp_t = (od_blend > MODE_C_OD_AMP_KNEE)
      ? (od_blend - MODE_C_OD_AMP_KNEE) / (1.f - MODE_C_OD_AMP_KNEE)
      : 0.f;                                                 // amp gain ramp (top only)
  const float od_drive     = 1.f + od_ped_t * (MODE_C_OD_DRIVE_MAX - 1.f);
  const float od_amp_drive = 1.f + od_amp_t * (MODE_C_OD_AMP_DRIVE - 1.f);
  const float od_comp      = MODE_C_OD_COMP_AT_NOON +
      od_blend * (MODE_C_OD_COMP_AT_MAX - MODE_C_OD_COMP_AT_NOON);

  // SW1=MID CCW — POG octave stack (replaces the OD when MODE_C_POG_ENABLE).
  // Staged travel: segment 1 crossfades clean → SUB (dry silent from SEG1_END
  // on), segment 2 fades UP1 in on top, segment 3 fades UP2 in. Loudness comp
  // interpolates across the travel and is folded into the four gain targets;
  // the per-sample Smoothers de-zipper them. At noon (pog_a = 0) the targets
  // are exactly dry-passthrough, so branch entry/exit is seamless.
  const float pog_a  = (MODE_C_POG_ENABLE && drive_mode == 1) ? k4_ccw : 0.f;
  const float pog_t1 = (pog_a >= MODE_C_POG_SEG1_END)
      ? 1.f : pog_a / MODE_C_POG_SEG1_END;
  const float pog_t2 = (pog_a <= MODE_C_POG_SEG1_END) ? 0.f
      : ((pog_a >= MODE_C_POG_SEG2_END) ? 1.f
         : (pog_a - MODE_C_POG_SEG1_END) /
           (MODE_C_POG_SEG2_END - MODE_C_POG_SEG1_END));
  const float pog_t3 = (pog_a <= MODE_C_POG_SEG2_END) ? 0.f
      : (pog_a - MODE_C_POG_SEG2_END) / (1.f - MODE_C_POG_SEG2_END);
  const float pog_comp = MODE_C_POG_COMP_AT_NOON +
      pog_a * (MODE_C_POG_COMP_AT_MAX - MODE_C_POG_COMP_AT_NOON);
  // Equal-power clean↔SUB crossfade (sqrt curves): a linear fade pair dips
  // ~-6 dB at mid-travel, which read as "way too quiet" in the first audition.
  const float pog_dry_t = sqrtf(1.f - pog_t1) * pog_comp;
  const float pog_sub_t = sqrtf(pog_t1) * MODE_C_POG_SUB_LEVEL * pog_comp;
  const float pog_up1_t = pog_t2 * MODE_C_POG_UP1_LEVEL * pog_comp;
  const float pog_up2_t = pog_t3 * MODE_C_POG_UP2_LEVEL * pog_comp;

  // SW1=MID CW — XOR post-fuzz ramp: full fuzz early (by RAMP_END), riding at
  // max for the rest of the travel while K4 keeps sweeping the XOR bit.
  const float xor_fuzz_t = (drive_mode == 1 && k4_cw > 0.f)
      ? ((k4_cw >= MODE_C_XOR_FUZZ_RAMP_END)
             ? 1.f : k4_cw / MODE_C_XOR_FUZZ_RAMP_END)
      : 0.f;

  // K3 bipolar with center deadzone → signed env amount in [-1, +1].
  float k3_signed = (k3 - 0.5f) * 2.f;
  if (k3_signed > -K3_DEADZONE && k3_signed < K3_DEADZONE) k3_signed = 0.f;

  // Moog ladder per-block constants (active only at SW2=UP).
  const bool ladder_on    = (filter_mode == 0);
  const float base_cutoff = MapCutoffModeC(k1);
  const float k3_abs = (k3_signed >= 0.f) ? k3_signed : -k3_signed;
  // k3_abs is 0 inside the deadzone (k3_signed got snapped); outside it
  // ranges [K3_DEADZONE, 1]. Subtract the deadzone width so env contribution
  // ramps from 0 right at the deadzone edge -- otherwise k3_abs jumps from
  // 0 (inside) to K3_DEADZONE (just outside), which with the current
  // ENV_SCALE x ENV_MOD_RANGE gain produces a multi-x cutoff step during play.
  const float k3_lin = (k3_abs > 0.f)
      ? (k3_abs - K3_DEADZONE) / (1.f - K3_DEADZONE)
      : 0.f;
  // Response curve: expand control resolution near noon, compress toward the
  // extremes. Endpoints are fixed (0→0, 1→1) so the tuned modulation bandwidth
  // is unchanged; only the dial feel shifts.
  const float k3_norm = powf(k3_lin, MODE_C_K3_CURVE);
  const float env_lift_gain = k3_norm * MODE_C_ENV_SCALE * MODE_C_ENV_MOD_RANGE;
  // K3 magnitude scales the CW release time only: further CW shortens the
  // release (snappier decay) toward RELEASE_MIN_MS. The CCW attack (swell) is
  // left at its constant seeded coef — scaling the swell time fought the
  // depth/clamp coupling and felt wrong. Only the active side's coef is
  // consumed in the loop below (shape keys off k3 sign).
  if (ladder_on) {
    const float rel_ms = MODE_C_FILTER_ENV_RELEASE_MS +
        k3_lin * (MODE_C_FILTER_ENV_RELEASE_MIN_MS - MODE_C_FILTER_ENV_RELEASE_MS);
    env_c_filter_rel_coef = 1.f - expf(-1.f / (rel_ms * 0.001f * mode_c_sr));
    // Drive is set per-sample in the loop (smoothed) to avoid zipper.
    // sqrt curve so resonance is audible across the full knob range.
    ladder_c_v2.SetResonance(sqrtf(k2) * MODE_C_LADDER_RES_MAX);
  }

  // Grendel per-block setup (active only at SW2=MID).
  // Direct deterministic mapping from the smoothed env:
  //   vowel_path = K1 − K3_signed × env_c_grendel × GRENDEL_TARGET_GAIN
  // K3 CCW (negative) → env pushes path toward ee (low→hi, opens brighter).
  // K3 CW  (positive) → env pushes path toward oo (hi→low, closes darker).
  // env_c_grendel is always a slow-swell smoother (400 ms attack, instant
  // snap-back). vowel_path is unclamped so hard plucks push into post-table
  // "virtual vowels".
  const bool grendel_on = (filter_mode == 1);
  if (grendel_on) {
    const float vowel_path =
        k1 - k3_signed * env_c_grendel * MODE_C_GRENDEL_TARGET_GAIN;
    const float size_base = GRENDEL_SIZE_MIN +
                            k2 * (GRENDEL_SIZE_MAX - GRENDEL_SIZE_MIN);
    // Coupled with K3 (same sign as vowel_path): CCW → size up on attack
    // (mouth tightens), CW → size down (mouth opens).
    const float size_mod = 1.f -
        k3_signed * env_c_grendel * MODE_C_GRENDEL_SIZE_MOD_AMT;
    grendel_c.SetVowel(vowel_path, size_base * size_mod);
  }

  // Phaser per-block constants (active only at SW2=DOWN). K3 here is LFO
  // speed + shape, NOT env amount — the phaser has no env routing.
  // k3_signed sign: negative → triangle LFO; positive → sample-and-hold.
  // Magnitude (with deadzone snap above) sets LFO rate; 0 → static.
  const bool phaser_on = (filter_mode == 2);
  if (phaser_on) phaser_c.SetParams(k1, k2, k3_signed);

  // POG pre-pass: fill the per-voice wet buffers for the whole block (the
  // filterbank runs at 8 kHz internally, so it consumes 6-sample chunks —
  // block size 48 divides evenly). Gated on SW1=MID + K4 on the CCW side:
  // the 80-band bank is the biggest CPU block in Mode C, and running it
  // unconditionally starved the main-loop YIN on the guitar profile (late
  // hops → glitchy, octave-hopping tracking). Entry staleness (~ms of filter
  // warmup) is masked by the voice gains smoothing up from 0.
  if (MODE_C_POG_ENABLE && drive_mode == 1 && k4_ccw > 0.001f) {
    polyoct_c.ProcessBlock(in[0], polyoct_sub, polyoct_up1, polyoct_up2, size);
  }

  float env_val = prev_block_env_c;
  for (size_t i = 0; i < size; i++) {
    const float dry = in[0][i];

    // Update shared envelope follower from raw bass each sample.
    env_val = env.Process(dry);

    // Keep the SW1=MID overdrive's pre-clip HPF / clean-LP states settled every
    // sample (even while the OD branch is inactive) so there's no stale-state
    // transient/click when K4 crosses into the CCW drive region.
    od_hp_z       += od_hp_g * (dry - od_hp_z);
    od_clean_lp_z += od_clean_lp_g * (dry - od_clean_lp_z);

    // Phaser S&H attack sync: env onset (rising crossing with hysteresis
    // re-arm) restarts the random LFO so each note gets a fresh step.
    if (phaser_on) {
      if (phaser_atk_armed && env_val > PHASER_SH_ATTACK_ON) {
        phaser_c.NoteAttack();
        phaser_atk_armed = false;
      } else if (!phaser_atk_armed && env_val < PHASER_SH_ATTACK_OFF) {
        phaser_atk_armed = true;
      }
    }

    // Per-sample control smoothing — de-zipper the block-rate knob gains for
    // K6 (mix), K5 (drive), and K4 (drive character). Smoothers run every
    // sample toward the per-block targets, so there's no stale-state transient
    // when a branch becomes active. Steady-state values are unchanged.
    const float dgc = sdg_c(dry_gain);
    const float wgc = swg_c(wet_gain);
    const float drv = sdrv_c(drive_amt);
    const float fbl = sfbl_c(fold_blend);
    const float fdr = sfdr_c(fold_drive);
    const float fco = sfco_c(fold_comp);
    const float cbl = scbl_c(cheby_blend);
    const float chd = schd_c(cheby_drive);
    const float odd = sodd_c(od_drive);
    const float oda = soda_c(od_amp_drive);
    const float odc = sodc_c(od_comp);
    const float pdry = spdry_c(pog_dry_t);
    const float psub = spsub_c(pog_sub_t);
    const float pup1 = spup1_c(pog_up1_t);
    const float pup2 = spup2_c(pog_up2_t);
    const float fzt  = sfzt_c(xor_fuzz_t);

    // Feed pitch tracker every sample so its filters stay warm regardless of
    // SW1 position; consumed only when drive_mode == 2 (SW1=DOWN synth osc).
    tracker.Feed(dry, env_val);

    // Filter env (ladder) — shape switches with K3 sign:
    //   CW  → peak follower (instant attack, one-pole release) → snappy auto-wah
    //   CCW → slow-rise env (one-pole attack, instant snap-back) → swell
    if (k3_signed >= 0.f) {
      if (env_val > env_c_filter) env_c_filter = env_val;
      else env_c_filter += env_c_filter_rel_coef * (env_val - env_c_filter);
    } else {
      if (env_val > env_c_filter)
        env_c_filter += env_c_filter_atk_coef * (env_val - env_c_filter);
      else env_c_filter = env_val;
    }

    // Grendel env smoother — always slow-swell shape (slow attack, instant
    // snap-back), regardless of K3 sign. Snap-style envelopes are reserved
    // for the Moog ladder filter above.
    if (env_val > env_c_grendel)
      env_c_grendel += env_c_grendel_atk_coef * (env_val - env_c_grendel);
    else env_c_grendel = env_val;

    // Drive stage (SW1).
    //   0=UP sinefold, 1=MID bitcrush (gated), 2=DOWN pitch-tracked synth osc.
    float wet = dry;
    if (drive_mode == 0) {
      // K4 CW = sine fold, K4 CCW = Chebyshev waveshaper, noon = clean dry.
      if (fold_blend > 0.001f) {
        const float folded = sinf(dry * fdr * 1.5707963f);
        wet = dry * (1.f - fbl) + folded * fbl * fco;
      } else if (cheby_blend > 0.001f) {
        // Pre-shaper 2-pole LP → near-sine so T2 makes a clean octave (Octavia
        // trick). Filter only feeds the shaper; the dry-blend term stays full.
        cheby_lp1 += cheby_lp_g * (dry - cheby_lp1);
        cheby_lp2 += cheby_lp_g * (cheby_lp1 - cheby_lp2);
        float xd = cheby_lp2 * chd;
        if (xd > 1.f) xd = 1.f;
        else if (xd < -1.f) xd = -1.f;
        const float shaped = Chebyshev(xd);
        wet = dry * (1.f - cbl) + shaped * cbl * MODE_C_CHEBY_COMP;
      }
    } else if (drive_mode == 1) {
      // K4 CW = gated bit-flipper, K4 CCW = tanh overdrive, noon = clean.
      if (k4_cw > 0.001f) {
        wet = bitcrush_c.Process(dry, k4_cw, env_val);
        // Post-fuzz: the XOR output drives a hard Saturate stage. Blend and
        // drive ride the same smoothed ramp (fzt); by RAMP_END it's all fuzz.
        if (fzt > 0.001f) {
          // Octave-fuzz chain: boost → full-wave rectifier blend (octave up)
          // → DC-block HPF (the rectifier's level-dependent DC pedestal
          // becomes a dynamic bias pump — sputter/breakup on decays) →
          // crossover dead-zone (starved-CMOS glitch) → two cascaded clip
          // stages. Both drives ride the fzt ramp so the whole thing stays
          // near-linear close to noon and fades in gradually.
          const float fdrive = 1.f + fzt * (MODE_C_XOR_FUZZ_DRIVE_MAX - 1.f);
          const float sdrive = 1.f + fzt * (MODE_C_XOR_FUZZ_STAGE2_DRIVE - 1.f);
          const float boosted = wet * fdrive;
          const float pre = boosted +
              MODE_C_XOR_FUZZ_RECT_MIX * (fabsf(boosted) - boosted);
          xorfuzz_hp_z += xorfuzz_hp_g * (pre - xorfuzz_hp_z);
          float fx = pre - xorfuzz_hp_z;
          const float az = fabsf(fx) - MODE_C_XOR_FUZZ_DEADZONE;
          fx = (az > 0.f) ? ((fx > 0.f) ? az : -az) : 0.f;
          const float fuzz1 =
              Saturate(fx + MODE_C_XOR_FUZZ_BIAS) -
              Saturate(MODE_C_XOR_FUZZ_BIAS);
          const float fuzz = Saturate(fuzz1 * sdrive);
          wet = wet * (1.f - fzt) + fuzz * fzt * MODE_C_XOR_FUZZ_LEVEL;
        }
      } else if (MODE_C_POG_ENABLE && k4_ccw > 0.001f) {
        // POG octave stack. The smoothed gains carry the whole staged travel
        // (clean→SUB xfade, then UP1, then UP2) and the loudness comp — this
        // branch just mixes the always-running voices.
        wet = dry * pdry + polyoct_sub[i] * psub + polyoct_up1[i] * pup1 +
              polyoct_up2[i] * pup2;
      } else if (od_blend > 0.001f) {
        // TS → tube amp. Pre-clip HPF keeps the lows out of the pedal stage
        // (anti-mud); a touch of low-passed clean is summed back before the amp
        // stage (TS body/character); the amp stage saturates the sum. One shared
        // symmetric Saturate(); asymmetry only via the bias offset, DC removed so
        // silence stays 0.
        const float hp = dry - od_hp_z;  // states updated at loop top
        const float pedal = Saturate(hp * odd + MODE_C_OD_BIAS) -
                            Saturate(MODE_C_OD_BIAS);
        const float amp_in = pedal + od_clean_lp_z * MODE_C_OD_CLEAN_MIX;
        const float driven =
            Saturate(amp_in * oda + MODE_C_OD_AMP_BIAS) -
            Saturate(MODE_C_OD_AMP_BIAS);
        // No dry/wet blend here — K4 sets drive only; clean is K6's job. Output
        // is the comp'd distorted signal (od_comp eases level as drive rises).
        wet = driven * odc;
      }
    } else if (drive_mode == 2) {
      // Pitch-tracked synth osc → raw-env VCA (Mode A style) → SW2 filter.
      // K4 (fold_amt) is the timbre morph; continuous pitch (follows bends).
      const float f0 = MidiToFreq(tracker.GetMidiNoteContinuous());
      const float osc_out = synth_c.Process(f0, fold_amt);
      wet = osc_out * EnvExpand(env_val) * MODE_C_SYNTH_VCA_GAIN;
    }

    // Filter stage (SW2). Filter modulation reads env_c_filter (smoothed).
    if (ladder_on) {
      // CW and CCW both open the filter from K1 upward as env grows; only the
      // env shape (smoother above) differs. CW = snappy auto-wah, CCW = swell.
      const float lift = 1.f + env_c_filter * env_lift_gain;
      float mod_cutoff = base_cutoff * lift;
      // Audio-rate self-FM (K5 noon→CW): modulate cutoff with the filter input
      // signal (wet = drive-stage output) so the resonant peak grows sidebands
      // — grittier, less sterile. moog_fm_depth is 0 below K5 noon.
      mod_cutoff *= 1.f + moog_fm_depth * wet;
      if (mod_cutoff < MODE_C_CUTOFF_MIN_HZ) mod_cutoff = MODE_C_CUTOFF_MIN_HZ;
      if (mod_cutoff > MODE_C_CUTOFF_MAX_HZ) mod_cutoff = MODE_C_CUTOFF_MAX_HZ;
      ladder_c_v2.SetCutoff(mod_cutoff);
      ladder_c_v2.SetDrive(drv);  // smoothed K5 drive (per sample) → no zipper
      // Spectrum-shaping filter: pad before drive so K5 noon sits in its sweet
      // zone; makeup after restores ≈ unity wet gain at K5 noon.
      wet = ladder_c_v2.Process(wet * MODE_C_MOOG_INPUT_PAD) * MODE_C_MOOG_MAKEUP;
    } else if (grendel_on) {
      // Spectrum-shaping filter: pad before pre-tanh so K5 noon stays clean.
      wet = tanhf(wet * drv * MODE_C_GRENDEL_INPUT_PAD);
      wet = grendel_c.Process(wet) * MODE_C_GRENDEL_MAKEUP;
    } else if (phaser_on) {
      // Clean parallel-BPF filter; pad keeps K5 noon in the linear zone,
      // K5 CW still drives via the soft pre-tanh.
      wet = tanhf(wet * drv * MODE_C_PHASER_INPUT_PAD);
      wet = phaser_c.Process(wet) * MODE_C_PHASER_MAKEUP;
    }

    // Slight post-filter lift so the limiter has something to grab on quieter
    // notes without driving the VCA stage. Applied pre-limiter on purpose.
    wet *= MODE_C_POST_FILTER_GAIN;

    // Post-filter peak limiter (2-band split, fundamentals preserved).
    wet = limiter_c.Process(wet);

    // Amp-env VCA — TEMPORARILY DISABLED to audition the wet path without
    // env-driven amplitude shaping. Reinstate by uncommenting the line below.
    // wet *= env_val * MODE_C_VCA_GAIN;

    out[0][i] = out[1][i] = dry * dgc + wet * wgc;  // smoothed K6 gains
  }
  prev_block_env_c = env_val;
  }

 private:
  NT3Shared*  sh_ = nullptr;
  NT3Controls eb_ = {};

  // ---- verbatim from pedals/nitrotron3/main.cpp:41-51 ----
MoogLadderV2 ladder_c_v2; // Mode C — SW2=UP, our tuned Moog (A/B winner)
Phaser       phaser_c;    // Mode C — SW2=DOWN (3-band parallel BPF, internal LFO)
Grendel      grendel_c;   // Mode C — SW2=MID Grendel formant filter
PeakLimiter  limiter_c;   // Mode C — post-filter peak limiter (2-band, fundamentals preserved)
BitCrush     bitcrush_c;  // Mode C — SW1=MID drive flavor (gated bit crusher)
ModeCSynth   synth_c;     // Mode C — SW1=DOWN pitch-tracked synth oscillator
// Mode C — SW1=MID K4-CCW POG octave stack (ERB-PS2 filterbank engine).
// ProcessBlock fills the per-voice wet buffers once per audio block; the
// per-sample loop mixes them with the smoothed staged gains.
polyoct::PolyOctave polyoct_c;
float polyoct_sub[256], polyoct_up1[256], polyoct_up2[256];  // >= max block size

  // ---- verbatim from pedals/nitrotron3/main.cpp:234-240 ----
Smoother sdg_c, swg_c;                    // C: K6 dry/wet gains
Smoother sdrv_c;                          // C: K5 pre-filter drive_amt
Smoother sfbl_c, sfdr_c, sfco_c;          // C: K4 sinefold blend/drive/comp
Smoother scbl_c, schd_c;                  // C: K4 Chebyshev blend/drive
Smoother sodd_c, soda_c, sodc_c;          // C: K4 overdrive drive/amp-drive/comp
Smoother spdry_c, spsub_c, spup1_c, spup2_c;  // C: K4 POG staged voice gains (comp folded in)
Smoother sfzt_c;                          // C: K4-CW XOR post-fuzz ramp (blend + drive)

  // ---- verbatim from pedals/nitrotron3/main.cpp:1202-1235 ----
// Block-rate env value for Grendel coefficient updates (env LP is ~33 Hz, so
// updating biquad coefs per-sample would waste a lot of cosf/sinf calls).
float prev_block_env_c = 0.f;

// Mode C filter-env smoother. Shape switches with K3 sign each sample:
//   CW  → peak follower (instant attack, one-pole release).
//   CCW → slow-rise env (one-pole attack, instant snap-back).
// Atk/rel coefs computed in main() from MODE_C_FILTER_ENV_*_MS constants.
float env_c_filter         = 0.f;
float env_c_filter_atk_coef = 0.f;
float env_c_filter_rel_coef = 0.f;
float mode_c_sr            = 48000.f;  // cached sample rate for per-block coef updates

// Grendel env smoother — always swell shape (slow attack, instant snap-back),
// regardless of K3 sign. Snap-style envelopes are reserved for the Moog
// ladder filter. vowel_path is derived directly from this smoothed env in
// the block setup; no separate vowel_path slewing.
float env_c_grendel          = 0.f;
float env_c_grendel_atk_coef = 0.f;

// Chebyshev pre-shaper low-pass (SW1=MID CCW). 2-pole (two cascaded one-poles)
// that isolates the fundamental so T2 makes a clean octave instead of intermod
// mush. Coef computed in main() from MODE_C_CHEBY_LP_HZ.
float cheby_lp1 = 0.f;
float cheby_lp2 = 0.f;
float cheby_lp_g = 0.f;
// Mode C SW1=MID overdrive (TS → amp) one-pole filter states + coeffs.
float od_hp_z = 0.f;        // pre-clip high-pass state (hp = x − lp)
float od_hp_g = 0.f;
float xorfuzz_hp_z = 0.f;   // XOR fuzz rectifier DC-block state (hp = x − lp)
float xorfuzz_hp_g = 0.f;
bool  phaser_atk_armed = true;  // S&H attack-sync re-arm state (env hysteresis)
float od_clean_lp_z = 0.f;  // clean-blend low-pass state
float od_clean_lp_g = 0.f;
};
