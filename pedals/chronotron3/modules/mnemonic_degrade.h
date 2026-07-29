#pragma once
//
// mnemonic_degrade.h — K3 degradation colour: bipolar BBD (CCW) / Tape (CW).
// Spec: docs/ChronoTron3/mnemonic-degradation-colour-spec.md
//
// Two self-contained chains, one active at a time, clean dead-zone at centre, no
// crossfade through centre (10 ms fade against bypass on chain switch). Written
// to run INSIDE mnemonic's feedback loop: ColourProcess() colours the loop write
// signal (repeats age cumulatively); the tape SPEED IRREGULARITY is exported via
// TapePitchCents() to modulate mnemonic's MAIN varispeed read tap (it *is* the
// tape speed — no separate read buffer). The always-on base tape drive + the
// feedback-bloom compressor live in the module, not here.
//
// Control-rate work (coeffs, powf, envelope->gain, mod jitter, OU, Poisson) runs
// every MNEMD_CTRL samples; per-sample work is filter state, S&H, shaper, noise,
// interpolated gain. Pure DSP — no daisy/hothouse dependency.
//
#include <math.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Tuning constants (spec §3–§5). MNEMD_ prefix. Starting brackets; tune by ear.
// ---------------------------------------------------------------------------
static constexpr int   MNEMD_CTRL = 32;         // internal control-rate period (samples, ~1.5 ms)

// dead-zone / depth (spec §1)
static constexpr float MNEMD_DEADZONE = 0.03f;

// shared (spec §2)
static constexpr float MNEMD_ENV_ATK_MS = 5.f;
static constexpr float MNEMD_ENV_REL_MS = 80.f;
static constexpr float MNEMD_DCBLOCK_HZ = 20.f;
static constexpr float MNEMD_XFADE_MS   = 10.f; // chain-switch crossfade against bypass

// --- BBD (spec §3) ---
static constexpr float MNEMD_FCLK_D0  = 48000.f;   // f_clk at d=0
static constexpr float MNEMD_FCLK_D1  = 2500.f;    // f_clk at d=1 (exp map) — pushed way down so full-CCW is a true lo-fi extreme (old "good" ~5.5k now lands mid-travel)
static constexpr float MNEMD_BBD_AA   = 0.35f;     // input/recon LPF factor x f_clk — lower = less ZOH imaging fizz, more muffled (less "decimator")
static constexpr float MNEMD_BBD_LOSS = 0.28f;     // stage-loss LPF factor x f_clk — more HF roll-off
static constexpr float MNEMD_BBD_COMP_EXP = 0.5f;  // compander exponent base
static constexpr float MNEMD_BBD_CDET_ATK_MS = 2.f, MNEMD_BBD_CDET_REL_MS = 50.f;
static constexpr float MNEMD_BBD_NOISE_LP_HZ = 6000.f;
static constexpr float MNEMD_BBD_NOISE_DB0 = -70.f, MNEMD_BBD_NOISE_DB1 = -40.f;
// Aged-BBD clock instability: the BBD chain borrows the modulation block for a
// SLOW pitch wander (fraction of the tape depth) so repeats aren't dead-steady.
static constexpr float MNEMD_BBD_WANDER_SC = 0.45f;
static constexpr float MNEMD_BBD_NL_DRIVE  = 4.5f; // tanh drive = 1 + NL_DRIVE*d (Tier 2) — unity-gain, so more grind at same level

// --- Tape (spec §4) ---
static constexpr float MNEMD_TAPE_DEV_CENTS = 70.f;  // max ± speed deviation at d=1
static constexpr float MNEMD_WOW_HZ = 0.7f,  MNEMD_WOW_SH  = 0.60f;
static constexpr float MNEMD_FL1_HZ = 4.3f,  MNEMD_FL1_SH  = 0.30f;
static constexpr float MNEMD_FL2_HZ = 11.7f, MNEMD_FL2_SH  = 0.20f;
static constexpr float MNEMD_JITTER_MS   = 250.f;   // amp re-draw interval
static constexpr float MNEMD_JITTER_TAU_MS = 150.f; // amp smoothing
static constexpr float MNEMD_JITTER_AMT  = 0.35f;   // ±35 %
static constexpr float MNEMD_OU_TAU_S    = 0.5f;    // OU time constant
static constexpr float MNEMD_OU_SHARE    = 0.25f;   // OU share of total deviation
static constexpr float MNEMD_TAPE_LP_D0 = 18000.f, MNEMD_TAPE_LP_D1 = 3500.f; // HF loss
static constexpr float MNEMD_HEADBUMP_HZ = 70.f, MNEMD_HEADBUMP_Q = 1.2f, MNEMD_HEADBUMP_DB1 = 4.f;
static constexpr float MNEMD_TAPE_HP_D0 = 30.f, MNEMD_TAPE_HP_D1 = 45.f;
static constexpr float MNEMD_SAT_K  = 4.0f;   // k = 1 + SAT_K*sqrt(d)  (k tops out ~5.0 at full CW)
static constexpr float MNEMD_SAT_A  = 0.15f;  // a = SAT_A*d (asymmetry)
static constexpr float MNEMD_SAT_BIAS = 0.0f; // bias deadzone t = SAT_BIAS*d (Tier 2) — OFF for now (grit source)
static constexpr float MNEMD_TAPE_NOISE_DB0 = -80.f, MNEMD_TAPE_NOISE_DB1 = -58.f;
static constexpr float MNEMD_TAPE_NOISE_ENV_DB = 8.f;  // env-modulated term at d=1
// dropouts / snags (Tier 2)
static constexpr float MNEMD_DROP_RATE1 = 1.2f;   // events/s at d=1
static constexpr float MNEMD_DROP_MIN_MS = 5.f,  MNEMD_DROP_MAX_MS = 40.f;
static constexpr float MNEMD_DROP_DB_MIN = 2.f,  MNEMD_DROP_DB_MAX = 10.f;  // capped: waver, not silence
static constexpr float MNEMD_DROP_FALL_MS = 3.f, MNEMD_DROP_REC_MS = 12.f;
static constexpr float MNEMD_SNAG_RATE_SC = 1.0f; // snag rate = 1.0 x drop rate
static constexpr float MNEMD_SNAG_CENTS_MIN = 50.f, MNEMD_SNAG_CENTS_MAX = 200.f;
static constexpr float MNEMD_SNAG_FALL_MS = 15.f, MNEMD_SNAG_REC_MS = 60.f;

// ---------------------------------------------------------------------------
// Small DSP helpers
// ---------------------------------------------------------------------------
struct MnemdOnePole {                       // one-pole low-pass
  float z = 0.f, a = 0.f;
  void SetLP(float fc, float sr) { a = expf(-2.f * 3.14159265f * fc / sr); }
  float LP(float x) { z = x * (1.f - a) + z * a; return z; }
  void Reset() { z = 0.f; }
};
struct MnemdDCBlock {                        // one-pole high-pass (DC blocker)
  float x1 = 0.f, y1 = 0.f, R = 0.995f;
  void Set(float fc, float sr) { R = 1.f - 2.f * 3.14159265f * fc / sr; }
  float HP(float x) { float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
  void Reset() { x1 = y1 = 0.f; }
};
struct MnemdBiquad {                         // RBJ biquad (TDF-II)
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void LP(float fc, float Q, float sr) {
    float w = 2.f * 3.14159265f * fc / sr, cs = cosf(w), sn = sinf(w), al = sn / (2.f * Q);
    float a0 = 1.f + al; b0 = (1.f - cs) * 0.5f / a0; b1 = (1.f - cs) / a0; b2 = b0;
    a1 = -2.f * cs / a0; a2 = (1.f - al) / a0;
  }
  void Peak(float fc, float Q, float dB, float sr) {
    float A = powf(10.f, dB / 40.f), w = 2.f * 3.14159265f * fc / sr;
    float cs = cosf(w), sn = sinf(w), al = sn / (2.f * Q);
    float a0 = 1.f + al / A; b0 = (1.f + al * A) / a0; b1 = -2.f * cs / a0;
    b2 = (1.f - al * A) / a0; a1 = -2.f * cs / a0; a2 = (1.f - al / A) / a0;
  }
  float Process(float x) {
    float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y;
  }
  void Reset() { z1 = z2 = 0.f; }
};

class MnemDegrade {
 public:
  void Init(float sr) {
    sr_ = sr;
    env_atk_ = 1.f - expf(-1.f / (MNEMD_ENV_ATK_MS * 0.001f * sr_));
    env_rel_ = 1.f - expf(-1.f / (MNEMD_ENV_REL_MS * 0.001f * sr_));
    cdet_atk_ = 1.f - expf(-1.f / (MNEMD_BBD_CDET_ATK_MS * 0.001f * sr_));
    cdet_rel_ = 1.f - expf(-1.f / (MNEMD_BBD_CDET_REL_MS * 0.001f * sr_));
    gain_smooth_ = 1.f - expf(-1.f / (0.001f * sr_));                 // 1 ms gain interp
    jitter_smooth_ = 1.f - expf(-1.f / (MNEMD_JITTER_TAU_MS * 0.001f * sr_));
    xfade_coef_ = 1.f - expf(-1.f / (MNEMD_XFADE_MS * 0.001f * sr_));
    drop_coef_ = 1.f - expf(-1.f / (0.004f * sr_));                  // ~4 ms dropout declick
    snag_keep_ = expf(-1.f / (MNEMD_SNAG_REC_MS * 0.001f * sr_));    // snag pitch recovery
    dc_.Set(MNEMD_DCBLOCK_HZ, sr_);
    bbd_noise_lp_.SetLP(MNEMD_BBD_NOISE_LP_HZ, sr_);
    tape_noise_lp1_.SetLP(2000.f, sr_);
    tape_noise_lp2_.SetLP(200.f, sr_);
    for (int i = 0; i < 3; i++) sine_amp_[i] = sine_amp_tgt_[i] = base_share_[i];
    RecomputeControl();
  }

  // Noise-injection duck (0..1), driven by the module's bypass noise gate: in
  // bypass the module fades this toward 0 as the trail decays, so the medium
  // hiss dies with the trail instead of sustaining a bed. 1 = full noise.
  void  SetNoiseGate(float g) { noise_gate_ = g; }
  // Current injected-noise amplitude (linear) of the active chain — the module
  // uses it to set the gate threshold just above the hiss floor (tracks K3).
  float NoiseFloorLin() const {
    return (active_chain_ == -1) ? bbd_noise_lin_
         : (active_chain_ == +1) ? tape_noise_lin_ : 0.f;
  }

  // Control-rate (from mnemonic Controls, ~10 ms): set bipolar position.
  void SetDepth(float p) {
    int tgt = (p < -MNEMD_DEADZONE) ? -1 : (p > MNEMD_DEADZONE) ? +1 : 0;
    target_chain_ = tgt;
    d_target_ = (tgt == 0) ? 0.f : (fabsf(p) - MNEMD_DEADZONE) / (1.f - MNEMD_DEADZONE);
  }

  // Per-sample, called BEFORE the main read: advances the modulation and returns
  // the tape pitch offset in cents (0 unless the tape chain is active).
  float TapePitchCents() {
    snag_cents_ *= snag_keep_;                                  // Tier-2 snag pitch recovery
    // sine phases (always advance; cheap)
    float dev = 0.f;
    for (int i = 0; i < 3; i++) {
      sine_ph_[i] += sine_inc_[i]; if (sine_ph_[i] >= 1.f) sine_ph_[i] -= 1.f;
      sine_amp_[i] += (sine_amp_tgt_[i] - sine_amp_[i]) * jitter_smooth_;
      dev += sinf(6.2831853f * sine_ph_[i]) * sine_amp_[i];
    }
    dev = dev * (1.f - MNEMD_OU_SHARE) + ou_ * MNEMD_OU_SHARE;   // blend sines + OU
    float cents = dev * MNEMD_TAPE_DEV_CENTS * d_;               // scale by depth
    cents += snag_cents_;                                        // Tier-2 snag pitch env
    if (active_chain_ == +1) return cents * mix_;                // tape: full wander
    if (active_chain_ == -1) return cents * MNEMD_BBD_WANDER_SC * mix_; // BBD: slow clock drift
    return 0.f;
  }

  // Per-sample colour of the loop write signal. Applies the active chain; blends
  // against dry via the switch crossfade. Advances the internal control tick.
  float ColourProcess(float x) {
    if (--ctrl_ctr_ <= 0) { ctrl_ctr_ = MNEMD_CTRL; RecomputeControl(); }

    // chain-switch crossfade through bypass: fade to dry, swap, fade back up.
    float mix_tgt;
    if (target_chain_ != active_chain_) {
      mix_tgt = 0.f;
      if (mix_ < 0.02f) { active_chain_ = target_chain_; ResetChain(); }
    } else {
      mix_tgt = (active_chain_ == 0) ? 0.f : 1.f;
    }
    mix_ += (mix_tgt - mix_) * xfade_coef_;
    d_ += (d_target_ - d_) * xfade_coef_;

    float colored = x;
    if (active_chain_ == -1)      colored = Bbd(x);
    else if (active_chain_ == +1) colored = Tape(x);
    float out = x * (1.f - mix_) + colored * mix_;
    return dc_.HP(out);
  }

 private:
  // ---- control-rate updates ---------------------------------------------
  void RecomputeControl() {
    // BBD clock + coupled coeffs. Quantise f_clk to an EXACT integer divisor of
    // the sample rate so the ZOH holds a whole number of samples at every knob
    // position — otherwise sr/f_clk lands between integers and the hold length
    // jitters N<->N+1, which reads as digital-decimator sizzle (the artefact
    // that fluctuates in/out as K3 moves). Integer hold = the clean "correct" tone.
    float f_target = MNEMD_FCLK_D0 * powf(MNEMD_FCLK_D1 / MNEMD_FCLK_D0, d_);
    bbd_hold_len_ = (int)(sr_ / f_target + 0.5f);
    if (bbd_hold_len_ < 1) bbd_hold_len_ = 1;
    f_clk_ = sr_ / (float)bbd_hold_len_;              // the actual, quantised clock
    bbd_in_lp_.LP(MNEMD_BBD_AA * f_clk_, 0.707f, sr_);
    bbd_rec_lp_.LP(MNEMD_BBD_AA * f_clk_, 0.707f, sr_);
    bbd_loss_.SetLP(MNEMD_BBD_LOSS * f_clk_, sr_);
    comp_exp_ = MNEMD_BBD_COMP_EXP * (0.3f + 0.7f * d_);
    bbd_noise_lin_ = powf(10.f, (MNEMD_BBD_NOISE_DB0 +
                          (MNEMD_BBD_NOISE_DB1 - MNEMD_BBD_NOISE_DB0) * d_) / 20.f);
    bbd_nl_drive_ = 1.f + MNEMD_BBD_NL_DRIVE * d_;

    // Tape coeffs
    tape_lp_.SetLP(MNEMD_TAPE_LP_D0 * powf(MNEMD_TAPE_LP_D1 / MNEMD_TAPE_LP_D0, d_), sr_);
    tape_hp_hz_ = MNEMD_TAPE_HP_D0 + (MNEMD_TAPE_HP_D1 - MNEMD_TAPE_HP_D0) * d_;
    tape_hp_.Set(tape_hp_hz_, sr_);
    head_bump_.Peak(MNEMD_HEADBUMP_HZ, MNEMD_HEADBUMP_Q, MNEMD_HEADBUMP_DB1 * d_, sr_);
    // Saturation + noise use a shaped depth (sqrt) so they ramp in SOONER on K3
    // than the wow/flutter/snag terms (which stay linear in d_).
    float ds = sqrtf(d_);
    sat_k_ = 1.f + MNEMD_SAT_K * ds;
    sat_a_ = MNEMD_SAT_A * ds;
    sat_bias_ = MNEMD_SAT_BIAS * ds;
    tape_noise_lin_ = powf(10.f, (MNEMD_TAPE_NOISE_DB0 +
                            (MNEMD_TAPE_NOISE_DB1 - MNEMD_TAPE_NOISE_DB0) * ds) / 20.f);
    tape_noise_env_ = MNEMD_TAPE_NOISE_ENV_DB * ds;

    sine_inc_[0] = MNEMD_WOW_HZ / sr_; sine_inc_[1] = MNEMD_FL1_HZ / sr_; sine_inc_[2] = MNEMD_FL2_HZ / sr_;

    // jitter: re-draw sine amp targets every 250 ms
    jitter_ctr_ += MNEMD_CTRL;
    if (jitter_ctr_ >= (int)(MNEMD_JITTER_MS * 0.001f * sr_)) {
      jitter_ctr_ = 0;
      for (int i = 0; i < 3; i++)
        sine_amp_tgt_[i] = base_share_[i] * (1.f + MNEMD_JITTER_AMT * (Rand() * 2.f - 1.f));
    }
    // OU random walk (control-rate dt)
    float dt = (float)MNEMD_CTRL / sr_;
    ou_ += (-ou_ / MNEMD_OU_TAU_S + 3.f * (Rand() * 2.f - 1.f)) * dt;
    if (ou_ > 1.f) ou_ = 1.f;
    else if (ou_ < -1.f) ou_ = -1.f;

    // Poisson events (dropout + snag), tape only
    if (active_chain_ == +1) {
      float pblk = (float)MNEMD_CTRL / sr_;
      if (drop_left_ <= 0 && snag_cents_ < 0.5f) {
        if (Rand() < MNEMD_DROP_RATE1 * d_ * pblk) StartDropout();
        else if (Rand() < MNEMD_DROP_RATE1 * MNEMD_SNAG_RATE_SC * d_ * pblk) StartSnag();
      }
    }
  }

  void ResetChain() {
    bbd_in_lp_.Reset(); bbd_rec_lp_.Reset(); bbd_loss_.Reset(); bbd_noise_lp_.Reset();
    bbd_samp_ctr_ = 0; bbd_hold_ = 0.f; comp_g_ = exp_g_ = 1.f; comp_env_ = exp_env_ = 0.f;
    tape_lp_.Reset(); tape_hp_.Reset(); head_bump_.Reset();
    tape_noise_lp1_.Reset(); tape_noise_lp2_.Reset(); sat_x1_ = 0.f;
    env_ = 0.f; drop_left_ = 0; drop_gain_ = drop_g_cur_ = 1.f; snag_cents_ = 0.f;
  }

  // ---- BBD chain (spec §3) ----------------------------------------------
  float Bbd(float x) {
    x = tanhf(x * bbd_nl_drive_) / bbd_nl_drive_;          // A.6 nonlinearity
    x = bbd_in_lp_.Process(x);                             // A.2 input LPF (shallow)
    // A.3 compressor
    float ad = fabsf(x);
    comp_env_ += (ad > comp_env_ ? cdet_atk_ : cdet_rel_) * (ad - comp_env_);
    float cg = powf(comp_env_ + 1e-5f, -comp_exp_);
    comp_g_ += (cg - comp_g_) * gain_smooth_;
    x *= comp_g_;
    // A.1 decimate ZOH @ f_clk (collapsed line, in-place) + A.5 noise.
    // Integer sample counter -> exactly bbd_hold_len_ samples per hold, no
    // fractional jitter (see RecomputeControl: quantised f_clk).
    if (++bbd_samp_ctr_ >= bbd_hold_len_) {
      bbd_samp_ctr_ = 0;
      float n = bbd_noise_lp_.LP((Rand() * 2.f - 1.f)) * bbd_noise_lin_ * noise_gate_;
      bbd_hold_ = x + n;
    }
    x = bbd_hold_;                                         // zero-order hold (imaging kept)
    x = bbd_loss_.LP(x);                                   // A.4 stage loss
    x = bbd_rec_lp_.Process(x);                            // A.2 reconstruction LPF
    // A.3 expander (detector on post-line signal)
    float ed = fabsf(x);
    exp_env_ += (ed > exp_env_ ? cdet_atk_ : cdet_rel_) * (ed - exp_env_);
    float eg = powf(exp_env_ + 1e-5f, comp_exp_);
    exp_g_ += (eg - exp_g_) * gain_smooth_;
    x *= exp_g_;
    return x * bbd_makeup_;
  }

  // ---- Tape chain (spec §4) ---------------------------------------------
  float Tape(float x) {
    // envelope (for level-dependent noise + snag/drop already handled at ctrl)
    float ax = fabsf(x);
    env_ += (ax > env_ ? env_atk_ : env_rel_) * (ax - env_);
    // B.3 saturation, 2x oversampled (pragmatic anti-alias; ADAA1 impractical for
    //     tanh(kx+ax^2), no closed-form antiderivative — flagged in the plan)
    float mid = 0.5f * (sat_x1_ + x);
    float s = 0.5f * (Shape(mid) + Shape(x));
    sat_x1_ = x;
    x = s;
    // B.4 noise: pink-ish, base + env-modulated
    float n = (tape_noise_lp1_.LP(Rand() * 2.f - 1.f) * 0.7f +
               tape_noise_lp2_.LP(Rand() * 2.f - 1.f) * 0.3f);
    float nlvl = tape_noise_lin_ * powf(10.f, (tape_noise_env_ * env_) / 20.f);
    x += n * nlvl * noise_gate_;
    // (modulated read = mnemonic main tap, via TapePitchCents)
    // B.2 loss filters
    x = tape_lp_.LP(x);                                   // HF loss
    x = head_bump_.Process(x);                            // head bump
    x = tape_hp_.HP(x);                                   // HP
    // B.5 dropout gain (event set at control rate; declicked ramp both edges)
    if (drop_left_ > 0) drop_left_--;
    float dtgt = (drop_left_ > 0) ? drop_gain_ : 1.f;
    drop_g_cur_ += (dtgt - drop_g_cur_) * drop_coef_;
    x *= drop_g_cur_;
    return x * tape_makeup_;
  }
  inline float Shape(float x) {                            // asym waveshaper + bias deadzone
    // Normalise to unity small-signal gain (d/dx at 0 = sat_k) so the shaper
    // colours WITHOUT boosting level — otherwise it adds up to sat_k x gain and
    // clips at input ~1/sat_k (the "too loud / too distorted at any setting" bug).
    float y = tanhf(sat_k_ * x + sat_a_ * x * x) / sat_k_;
    if (sat_bias_ > 1e-4f) { float t = sat_bias_; y *= (x * x) / (x * x + t * t); }
    return y;
  }

  // ---- Tier-2 events -----------------------------------------------------
  void StartDropout() {
    float dur = MNEMD_DROP_MIN_MS + Rand() * (MNEMD_DROP_MAX_MS - MNEMD_DROP_MIN_MS);
    drop_left_ = (int)(dur * 0.001f * sr_);
    float dB = (MNEMD_DROP_DB_MIN + Rand() * (MNEMD_DROP_DB_MAX - MNEMD_DROP_DB_MIN)) * d_;
    drop_gain_ = powf(10.f, -dB / 20.f);                   // (envelope simplified to a hold dip)
  }
  void StartSnag() {                                     // pitch bump, decays via snag_keep_
    snag_cents_ = (MNEMD_SNAG_CENTS_MIN +
                   Rand() * (MNEMD_SNAG_CENTS_MAX - MNEMD_SNAG_CENTS_MIN)) * d_;
  }

  float Rand() {                                           // xorshift32 -> [0,1)
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (float)rng_ / 4294967295.f;
  }

  // ---- state -------------------------------------------------------------
  float sr_ = 48000.f;
  float noise_gate_ = 1.f;                       // bypass noise-duck (1 = full hiss)
  int   active_chain_ = 0, target_chain_ = 0;    // -1 BBD · 0 bypass · +1 tape
  float d_ = 0.f, d_target_ = 0.f, mix_ = 0.f, xfade_coef_ = 0.f;
  int   ctrl_ctr_ = 1;
  uint32_t rng_ = 0x51ed3a7bu;

  // shared
  float env_ = 0.f, env_atk_ = 0.f, env_rel_ = 0.f, gain_smooth_ = 0.f;
  MnemdDCBlock dc_;

  // BBD
  MnemdBiquad  bbd_in_lp_, bbd_rec_lp_;
  MnemdOnePole bbd_loss_, bbd_noise_lp_;
  float f_clk_ = 48000.f, bbd_hold_ = 0.f;
  int   bbd_hold_len_ = 1, bbd_samp_ctr_ = 0;
  float comp_env_ = 0.f, exp_env_ = 0.f, comp_g_ = 1.f, exp_g_ = 1.f, comp_exp_ = 0.5f;
  float cdet_atk_ = 0.f, cdet_rel_ = 0.f, bbd_noise_lin_ = 0.f, bbd_nl_drive_ = 1.f;
  float bbd_makeup_ = 0.7f;                       // level match (tune per §1)

  // Tape
  MnemdOnePole tape_lp_, tape_noise_lp1_, tape_noise_lp2_;
  MnemdDCBlock tape_hp_;
  MnemdBiquad  head_bump_;
  float tape_hp_hz_ = 30.f, sat_k_ = 1.f, sat_a_ = 0.f, sat_bias_ = 0.f, sat_x1_ = 0.f;
  float tape_noise_lin_ = 0.f, tape_noise_env_ = 0.f, tape_makeup_ = 1.0f;

  // modulation (§5)
  float sine_ph_[3] = {0, 0, 0}, sine_inc_[3] = {0, 0, 0};
  float sine_amp_[3] = {0, 0, 0}, sine_amp_tgt_[3] = {0, 0, 0};
  const float base_share_[3] = {MNEMD_WOW_SH, MNEMD_FL1_SH, MNEMD_FL2_SH};
  float ou_ = 0.f, jitter_smooth_ = 0.f;
  int   jitter_ctr_ = 0;

  // events
  int   drop_left_ = 0;
  float drop_gain_ = 1.f, drop_g_cur_ = 1.f, drop_coef_ = 0.f;
  float snag_cents_ = 0.f, snag_keep_ = 0.f;
};
