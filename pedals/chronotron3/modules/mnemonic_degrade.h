#pragma once
//
// mnemonic_degrade.h — degradation colour: bipolar BBD (CCW) / Tape (CW).
// Driven by K4 on BOTH hosts (mnemonic degrade, vestige texture) since the
// 2026-09-14 knob alignment; `depth` below is that knob's bipolar position.
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

// Signal-keyed noise gate (shared by BBD + tape): the injected hiss follows what
// you play — opens fast so noise arrives WITH the note (no swell-in lag), releases
// gently so the hiss tail decays naturally when you stop. A smooth one-pole slew on
// the gain IS the gate (no hard threshold curve). Threshold tracks the injected-noise
// floor so it closes right when the signal drops to hiss level.
static constexpr float MNEMD_NGATE_ATK_MS = 2.f;   // gate-gain open time  (short = noise not delayed)
static constexpr float MNEMD_NGATE_REL_MS = 6000.f; // LINEAR fade-out time, full 1->0 (no early lurch)
static constexpr float MNEMD_NGATE_DET_MS = 40.f;  // input follower release (anti-chatter; attack is instant)
static constexpr float MNEMD_NGATE_THR    = 3.f;   // fully-open threshold = injected noise floor x this
static constexpr float MNEMD_NGATE_KNEE   = 0.35f; // soft-knee floor: fully CLOSED below THR x this (0..1).
                                                   // Wider band (smaller) = smoother glide, less flutter.
static constexpr float MNEMD_DCBLOCK_HZ = 20.f;
static constexpr float MNEMD_XFADE_MS   = 10.f; // chain-switch crossfade against bypass

// --- BBD (spec §3) ---
static constexpr float MNEMD_FCLK_D0  = 48000.f;   // f_clk at d=0
static constexpr float MNEMD_FCLK_D1  = 2500.f;    // f_clk at d=1 (exp map) — full-CCW lo-fi extreme (the digital fold artefacts live here; keep low)
// Anti-alias / reconstruction split (x f_clk). The two levers that set the
// grit-vs-sizzle balance of the BBD (Memory-Boy character: crude low-mid grit,
// dark top, nothing hi-fi):
//   IN_AA  = input band-limit BEFORE the ZOH decimation. Decimator Nyquist is
//            0.5*f_clk; setting IN_AA ABOVE 0.5 lets content fold DOWN into the
//            mids = the crude aliasing grit. Higher = grittier/dirtier.
//   REC    = reconstruction LPF AFTER the line. Low value darkens the top and
//            kills the high ZOH imaging = no hi-fi sizzle. Lower = darker/cruder.
static constexpr float MNEMD_BBD_IN_AA = 0.60f;    // input anti-alias (>0.5 -> fold-down grit)
static constexpr float MNEMD_BBD_REC   = 0.25f;    // reconstruction LPF factor x f_clk (dark, kills high sizzle)
static constexpr float MNEMD_BBD_LOSS  = 0.28f;    // stage-loss LPF factor x f_clk — more HF roll-off
// Fixed cutoff FLOOR (Hz) for the reconstruction + stage-loss LPFs. At shallow
// BBD depths factor*f_clk is well above this (unchanged), but past ~9:00 the clock
// drops and factor*f_clk would plunge to a telephone-dark ~600 Hz — the floor stops
// it there, opening ONLY the deep/dark end while the shallow settings are untouched.
// Higher = the dark end stays brighter (more highs + digital fold through).
static constexpr float MNEMD_BBD_LPF_FLOOR_HZ = 900.f;
// Compander removed (was 2x powf/sample and restored the signal each feedback
// lap, preventing the repeats from crumbling). Its "breathing" is approximated
// by a cheap amplitude flicker reusing the wow/OU mod block:
static constexpr float MNEMD_BBD_BREATH = 0.12f;   // amplitude-flicker depth (0 = off; ~+-12% x d)
static constexpr float MNEMD_BBD_NOISE_LP_HZ = 6000.f;
static constexpr float MNEMD_BBD_NOISE_DB0 = -78.f, MNEMD_BBD_NOISE_DB1 = -56.f;
// Aged-BBD clock instability: the BBD chain borrows the modulation block for a
// SLOW pitch wander (fraction of the tape depth) so repeats aren't dead-steady.
static constexpr float MNEMD_BBD_WANDER_SC = 0.45f;
static constexpr float MNEMD_BBD_NL_DRIVE  = 4.5f; // tanh drive = 1 + NL_DRIVE*d (Tier 2) — unity-gain, so more grind at same level
// --- Dynamic sine-fold (restores mids/highs the dark BBD LPF removes) ---
// Models aging BBD stages that no longer hold their value: past a KNEE the darkened
// signal is tapped, run through a sine wavefolder, HIGH-PASSED (so it adds only
// restored mids/highs, never low-end mud), and blended back IN PARALLEL. The fold
// REGENERATES harmonics locked to the note (warm, not fizzy) instead of just
// re-opening the filter.
//
// DYNAMICS-REACTIVE by construction: the raw post-LPF BBD signal drives the sine
// folder through a fixed pre-GAIN. Soft playing barely reaches the first fold (near
// clean); digging in drives deeper -> more folds -> brighter/richer. The wide bass
// dynamic range IS the reactivity. GAIN sets where that transition sits. The fold
// return is HIGH-PASSED (adds only restored mids/highs) and blended in parallel; the
// dark BBD body is untouched. Blend ramps in only past KNEE (~9:00). Tune by ear.
static constexpr float MNEMD_FOLD_KNEE       = 0.15f; // BBD depth (0=centre..1=full CCW) where fold begins (~9:00)
static constexpr float MNEMD_FOLD_BLEND_MAX  = 0.3f;  // max parallel fold blend at full CCW (0 = off). in-loop, so this also feeds feedback — keep modest
static constexpr float MNEMD_FOLD_GAIN       = 80.f;  // DRIVE at FULL CCW = the fold CHARACTER reference. NOT loudness — set the sound here, adjust volume with MAKEUP.
static constexpr float MNEMD_FOLD_GAIN_MIN   = 10.f;  // DRIVE at the knee (knob onset). drive ramps GAIN_MIN..GAIN with depth -> shallow folds gently, deep gets gnarly.
static constexpr float MNEMD_FOLD_DRIVE_CURVE = 2.0f; // exponent on depth for the DRIVE ramp: 1 = linear, >1 = stays low longer then ramps hard toward full CCW
static constexpr float MNEMD_FOLD_MAKEUP     = 1.5f;  // LEVEL, compensated RELATIVE to gain: fold out = sin(GAIN*x) * MAKEUP/GAIN -> ceiling MAKEUP/GAIN (=0.05). raising GAIN won't get louder; MAKEUP sets loudness.
static constexpr bool  MNEMD_FOLD_HP_ON      = false; // HP the fold return? applied AFTER the folder. OFF = hear the full fold (raw overtones)
static constexpr float MNEMD_FOLD_HP_HZ      = 400.f; // HP corner (only used when HP_ON) — keep only the restored mids/highs
// DEBUG: solo the folded signal — the BBD wet becomes ONLY the HP'd fold return, so
// you can hear the folder in isolation (and its dynamics) and tune GAIN by ear. Turn
// K4 (vestige + mnemonic) CCW past the knee to engage. Keep mnemonic feedback (K5) LOW while
// soloing in mnemonic (the solo sits in the loop). Set false for normal.
static constexpr bool  MNEMD_FOLD_SOLO       = false;
static constexpr float MNEMD_FOLD_SOLO_GAIN  = 1.f;   // monitor gain for the soloed fold

// --- Amplitude quantiser (bit-crush) on the held sample ---------------------
// The rest of the BBD dirt is SMOOTH: tanh saturation plus alias harmonics, i.e.
// fuzz. This is the opposite kind of dirt — a coarse amplitude STEP, applied once
// per hold so it rides the staircase instead of adding a new rate. Quantisation
// error is a FIXED size, so unlike the tanh it does not compress: quiet playing
// gets proportionally grittier, loud playing stays intact. Off by default; hosts
// opt in with SetBbdCrush().
static constexpr float MNEMD_CRUSH_KNEE  = 0.15f; // BBD depth where the crush begins (~9:00, same as the fold)
static constexpr float MNEMD_CRUSH_STEP  = 0.008f;// quantiser step at FULL CCW, absolute (not bits): the grit CHARACTER. Bigger = coarser.
static constexpr float MNEMD_CRUSH_CURVE = 2.0f;  // depth->step ramp (>1 = stays fine longer, bites near CCW)

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

// --- BBD clock slip (CCW instability) -------------------------------------
// The BBD clock is deliberately quantised to an exact integer divisor of the
// sample rate, because a clock that wanders CONTINUOUSLY between N and N+1
// samples/hold reads as digital-decimator sizzle (see RecomputeControl). That
// stays true — this is the tape side's EVENT model instead: a Poisson "slip"
// that jams the hold at a different INTEGER length for a few tens of ms, then
// snaps back. The clock momentarily runs slow: aliasing imaging shifts down and
// the grit lurches, without the between-integers sizzle.
// The reconstruction/loss filters keep tracking the SMOOTH f_target, not the
// slipped clock — moving them too would recompute biquad coefficients on every
// event edge, which clicks.
// Off unless a host opts in with SetBbdSlip(); mnemonic and vestige do not.
static constexpr float MNEMD_BBD_SLIP_KNEE   = 0.5f;  // depth below which nothing happens (~9 o'clock)
static constexpr float MNEMD_BBD_SLIP_RATE   = 3.0f;  // events/s at full CCW, x the host's slip amount
// Event LENGTH. Short events read as blips/bloops — the clock jumps and snaps
// back before the ear hears it as movement. Longer ones read as the clock
// having genuinely wandered off and come back. Note the scheduler blocks a new
// event while one runs, so raising these also thins the effective rate.
static constexpr float MNEMD_BBD_SLIP_MIN_MS = 80.f;
static constexpr float MNEMD_BBD_SLIP_MAX_MS = 500.f;
// Optional: derive the event length from the HOST's time reference (sprawl's
// echo time) instead of these fixed ms, as a fraction of it. The TIMING stays
// Poisson — this scales the instability to the music's time-world without ever
// locking to the grid, which is the point: a failing medium does not know what
// you are playing. Capped so a multi-second echo does not give multi-second
// events. Inactive unless the host calls SetBbdSlipTimeRef with blend > 0.
static constexpr float MNEMD_BBD_SLIP_REF_MIN  = 0.15f;   // x the reference time
static constexpr float MNEMD_BBD_SLIP_REF_MAX  = 0.80f;
static constexpr float MNEMD_BBD_SLIP_REF_CAP_MS = 1500.f;
// --- Starving-capacitor clock -----------------------------------------------
// NOT a warble and NOT a timed excursion: the clock is never held at nominal and
// never released back to it. It SAGS continuously in one direction — usually
// slower, like a supply rail drooping — until an occasional BUMP lurches it and
// re-rolls the direction. Between bumps nothing oscillates; it just keeps
// leaving. The travel limits are the only thing that stops it.
static constexpr float MNEMD_BBD_SAG_RATE    = 0.04f; // hold-multiplier units per second: the DRIFT SPEED
static constexpr float MNEMD_BBD_SAG_DOWN    = 0.75f; // share of bumps that send it slower (down) rather than faster
static constexpr float MNEMD_BBD_BUMP_RATE   = 0.3f;  // bumps/s at full CCW (x host slip amount) — also sets SEGMENT LENGTH, since a bump re-rolls the direction
static constexpr float MNEMD_BBD_BUMP_DEPTH  = 0.15f; // size of the discrete lurch at a bump
static constexpr float MNEMD_BBD_SLIP_MULT   = 1.35f; // slowest the clock may sag to (hold-length stretch)
static constexpr float MNEMD_BBD_SLIP_MULT_LO = 0.85f;// fastest it may drift the other way (<1 = quicker than nominal)
// Event TYPE. There are two, chosen per event by SHARE.
//
//  - RATE SLIP: the ZOH clock jumps to another rate for the event. A ZOH's rate
//    IS a pitch, so this moves the staircase's note around; tuned any way, it
//    reads as melodic sample-and-hold. That is its character, not a fault.
//
//  - REPLAY: the rate is left alone and a contiguous run of recent held values
//    is replayed, looping, and SUMMED INTO the live path (see the mix below).
//    It is not heard as a repeat: summing a signal with a looping copy of
//    itself cancels at regular frequency intervals, so what you hear is a
//    hollow, metallic, drifting colour. NOTE that is an inherent consequence of
//    the sum — there is no comb-filter stage anywhere in this chain.
//
// SHARE 0 = all rate slips, 1 = all replays.
static constexpr float MNEMD_BBD_REPLAY_SHARE = 0.5f;

// The run must be CONTIGUOUS and looped. An earlier version picked a random
// held value per tick: an uncorrelated sequence, i.e. white noise ("chhhrrr").
// Its LENGTH sets how far back the summed copy is delayed, and therefore the
// spacing of the cancellation notches — 40-200 ms puts those 5-25 Hz apart,
// which is dense enough to read as colour rather than as an echo. This is the
// parameter that decides what the effect SOUNDS like.
static constexpr float MNEMD_BBD_REPLAY_MIN_MS = 40.f;
static constexpr float MNEMD_BBD_REPLAY_MAX_MS = 200.f;
static constexpr int   MNEMD_BBD_HIST_N        = 512;  // held-value history (power of 2)

// Boundary fade. Starting a replay jumps from what is playing NOW to something
// recorded tens of ms ago at an unrelated point in the waveform; stopping jumps
// back. MEASURED (deep CCW, 179 s): those two moments stepped ~10x an ordinary
// hold — 0.171 entering, 0.116 leaving, against a 0.015 median — while the loop
// point itself measured 0.014, i.e. perfectly normal. So it is the BOUNDARIES
// that click, not the join; an earlier loop-point search fixed the one join
// that was never broken. Measured boundary step vs that 0.0149 baseline: 0.171
// at 0 ms, 0.047 at 1, 0.034 at 2, 0.021 at 4, 0.0156 at 8 — it saturates at 8,
// and 12/16 were indistinguishable by ear while costing stutters (each event
// has TWO fades and the shortest are 15 % of the echo time).
// Polarity of the replayed run. -1 flips it against the live signal, so where
// the two overlap they cancel instead of reinforce. TEMP experiment — 1.f is normal.
static constexpr float MNEMD_BBD_REPLAY_POL = -1.f;
static constexpr float MNEMD_BBD_REPLAY_XF_MS = 8.f;
// The replayed run is used as a MODULATOR of the live path, not mixed with it.
// Crossfading in the replay made this a second stutter voice, which competes
// with the grain engine's own stutter on K3-CW; multiplying instead keeps the
// live signal as the carrier and lets the replay colour it. Because the
// modulator is a delayed copy of the same source, the two share a pitch with a
// drifting phase relationship, so the product is an octave-ish component whose
// amplitude walks in the stutter's rhythm — modulation, not repeats.
// The modulator is the raw replayed signal, NOT normalised — it keeps the level
// it was recorded at, so the modulation tracks playing dynamics. The entry/exit
// crossfade ramps the DEPTH, which is click-free by construction.
// How much the replay REPLACES the live signal during an event (A/B).
// 1 = the stutter alone, 0 = the replay is inaudible.
//
// Summing it in parallel was tried instead and does NOT colour the sound: a
// copy delayed by 40-200 ms does cancel frequencies, but they land 5-25 Hz
// apart, far too fine for the ear to hear as timbre, so it just reads as a
// second copy. Metallic/hollow colouring needs the two paths a FEW ms apart,
// the way overlapping grains are — a different mechanism, not a mix level.
// Ring modulation was also tried: multiplying nulls the output whenever the
// modulator crosses zero, so it reads as dropouts.
//
// It is summed INSIDE the reconstruction path, so it is filtered exactly like
// the live signal and sits in the same tonal world rather than on top of it.
//
// Kept deliberately BELOW the live path (see the depth constant): it should
// colour, not answer back. At full depth it is still the quieter of the two.
// Measured boundary step vs a 0.0149 ordinary step: 0.171 at 0 ms, 0.047 at
// 1, 0.034 at 2, 0.021 at 4, 0.0156 at 8 — i.e. it saturates at 8 and 12/16
// were indistinguishable by ear. Larger only costs stutters: each event has
// TWO fades and the shortest events are 15 % of the echo time, so at 16 ms a
// 500 ms echo spends 43 % of its briefest stutters fading (all of them at a
// 150 ms echo). 8 is the smallest fully clean value; 4 is nearly clean if a
// snappier onset is ever wanted.
// Continuous clock drift: a per-instance random walk on the hold length, so the
// decimator never sits at the same clock twice and the slip events below ride a
// moving target instead of repeating. Driven by the engine's OU random walk
// (NOT the wow/flutter sines — those are periodic and would sound like a cycle).
// This is the "between integers" region the integer quantisation used to avoid;
// hosts that want the clean quantised tone simply leave it at 0.
static constexpr float MNEMD_BBD_DRIFT_MAX = 0.60f;   // +-60 % hold at amount 1
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
    ngate_atk_      = 1.f - expf(-1.f / (MNEMD_NGATE_ATK_MS * 0.001f * sr_));
    ngate_rel_step_ = 1.f / (MNEMD_NGATE_REL_MS * 0.001f * sr_);   // linear per-sample decrement
    ngate_det_rel_  = 1.f - expf(-1.f / (MNEMD_NGATE_DET_MS * 0.001f * sr_));
    jitter_smooth_ = 1.f - expf(-1.f / (MNEMD_JITTER_TAU_MS * 0.001f * sr_));
    xfade_coef_ = 1.f - expf(-1.f / (MNEMD_XFADE_MS * 0.001f * sr_));
    drop_coef_ = 1.f - expf(-1.f / (0.004f * sr_));                  // ~4 ms dropout declick
    snag_keep_ = expf(-1.f / (MNEMD_SNAG_REC_MS * 0.001f * sr_));    // snag pitch recovery
    dc_.Set(MNEMD_DCBLOCK_HZ, sr_);
    bbd_noise_lp_.SetLP(MNEMD_BBD_NOISE_LP_HZ, sr_);
    fold_hp_lp_.SetLP(MNEMD_FOLD_HP_HZ, sr_);   // fold-return HP (subtract this LP)
    tape_noise_lp1_.SetLP(2000.f, sr_);
    tape_noise_lp2_.SetLP(200.f, sr_);
    for (int i = 0; i < 3; i++) sine_amp_[i] = sine_amp_tgt_[i] = base_share_[i];
    RecomputeControl();
  }

  // Noise-injection duck (0..1), driven by the module's bypass noise gate: in
  // bypass the module fades this toward 0 as the trail decays, so the medium
  // hiss dies with the trail instead of sustaining a bed. 1 = full noise.
  void  SetNoiseGate(float g) { noise_gate_ = g; }
  // Per-instance BBD fold brightness: scales how much regenerated fold (mids/
  // highs + grit) is mixed on top of the dark body. 1 = default (mnemonic);
  // vestige raises it for more clarity in the looper without losing distortion.
  void  SetFoldScale(float s) { fold_scale_ = s; }

  // Per-instance bit-crush amount: scales MNEMD_CRUSH_STEP. 0 = off (default,
  // so mnemonic and vestige are unchanged); 1 = the full step above.
  void  SetBbdCrush(float s) { crush_scale_ = s; }
  // Per-instance BBD brightness: raises the reconstruction + stage-loss LPF
  // FLOOR. 1 = default (mnemonic, vestige).
  //
  // The floor is deliberately the only thing scaled. Those cutoffs are
  // REC/LOSS x f_clk, and f_clk runs to 48 kHz at the clean end, so scaling the
  // proportional term shoves both biquads into the Nyquist clamp near the
  // deadzone — the exact thing the clamp below exists to prevent, and it did
  // blow up (a huge sample poisoned sprawl's always-on reverb FDN, which then
  // stayed silent). It also bought nothing there: both land on the same clamp.
  // The deep-CCW end is where the darkness actually lives — REC/LOSS x f_clk
  // falls to ~700 Hz and BOTH pin to the floor — so the floor is the control
  // that matters, and it is inherently safe.
  void  SetBbdLpfScale(float s) { bbd_lpf_scale_ = s; }
  // Per-instance tape drive: scales the saturator's drive and its asymmetry.
  // Shape() normalises by sat_k, so this buys DISTORTION, not level. 1 = default.
  void  SetTapeDriveScale(float s) { tape_drive_scale_ = s; }
  // Per-instance tape output level. 1 = default (mnemonic, vestige).
  void  SetTapeLevel(float g) { tape_level_ = g; }
  // Per-instance BBD output level (the CCW chain's counterpart to
  // SetTapeLevel). 1 = default (mnemonic, vestige).
  void  SetBbdLevel(float g) { bbd_level_ = g; }
  // Per-instance EXTENSION of the tape (CW) travel. The whole CW side is
  // driven by one depth value, so scaling it past 1 extrapolates EVERY tape
  // parameter together — HF loss, the high-pass, head bump, drive, hiss,
  // wow/flutter depth and dropout rate — keeping their relative balance and
  // simply reaching further than the stock endpoint. 1 = default
  // (mnemonic, vestige stop exactly where they always did).
  void  SetTapeDepthScale(float s) { tape_depth_scale_ = s; }
  // Per-instance BBD clock-slip amount (0 = off = default; 1 = full rate).
  // Scales the event rate only; the knee and the slip depth are fixed.
  void  SetBbdSlip(float a) { bbd_slip_amt_ = a; }
  // Per-instance continuous BBD clock drift, 0..1 (0 = off = default, i.e. the
  // exact integer clock mnemonic and vestige have always had).
  void  SetBbdDrift(float a) { bbd_drift_amt_ = a; }
  // Per-instance DEPTH compensation: the gain each chain should reach at FULL
  // depth, interpolated from 1.0 at the centre deadzone. Both chains lose level
  // as they get deeper (BBD to its LPFs, tape to the drive normalisation and HF
  // loss), so without this the extremes sit well below the clean noon level. It
  // is deliberately NOT an output level: at noon it is exactly 1, so it cannot
  // change the already-correct centre or the feedback drive there.
  // 1 = off = default (mnemonic, vestige).
  void  SetBbdDepthComp(float g)  { bbd_depth_comp_ = g; }
  void  SetTapeDepthComp(float g) { tape_depth_comp_ = g; }
  // Per-instance ring depth for the BBD stutter: 0 = off (the replay is simply
  // not heard), 1 = the live path fully multiplied by the replayed run.
  void  SetBbdReplayMix(float d) { bbd_replay_mix_ = d; }
  // Host time reference for slip-event LENGTH (control rate). `ref_ms` is the
  // host's musical time (sprawl: the echo time); `blend` crossfades from the
  // fixed MNEMD_BBD_SLIP_*_MS (0) to fully reference-scaled (1). Default 0,
  // so mnemonic and vestige keep fixed timings.
  void  SetBbdSlipTimeRef(float ref_ms, float blend) {
    slip_ref_ms_ = ref_ms; slip_ref_blend_ = blend;
  }

  // Read-only state snapshot for diagnostics (host or serial). No behaviour.
  struct DebugState {
    int   chain, tgt_chain, hold_len;
    bool  replay_on;                 // a replay event is running
    float slip_mult, hold_f;      // rate-slip factor, live fractional hold
    int   replay_pos, replay_len; // position within the replayed run
    float mix, d, d_tgt, noise_gate, noise_sgate, nz_det, env;
    float bbd_hold, in_z1, in_z2, rec_z1, rec_z2, loss_z, dc_x1, dc_y1;
    float sat_x1, tape_lp_z, hp_x1, hp_y1, hb_z1, hb_z2, drop_g, snag;
    float f_clk, fold_blend, fold_drive;
  };
  void DebugFill(DebugState& o) const {
    o.chain = active_chain_; o.tgt_chain = target_chain_; o.hold_len = bbd_hold_len_;
    o.replay_on = bbd_replay_on_ && bbd_slip_left_ > 0;
    o.slip_mult = bbd_slip_mult_; o.hold_f = bbd_hold_f_;
    o.replay_pos = bbd_replay_pos_; o.replay_len = bbd_replay_len_;
    o.mix = mix_; o.d = d_; o.d_tgt = d_target_;
    o.noise_gate = noise_gate_; o.noise_sgate = noise_sgate_; o.nz_det = nz_det_; o.env = env_;
    o.bbd_hold = bbd_hold_;
    o.in_z1 = bbd_in_lp_.z1;  o.in_z2 = bbd_in_lp_.z2;
    o.rec_z1 = bbd_rec_lp_.z1; o.rec_z2 = bbd_rec_lp_.z2;
    o.loss_z = bbd_loss_.z; o.dc_x1 = dc_.x1; o.dc_y1 = dc_.y1;
    o.sat_x1 = sat_x1_; o.tape_lp_z = tape_lp_.z;
    o.hp_x1 = tape_hp_.x1; o.hp_y1 = tape_hp_.y1;
    o.hb_z1 = head_bump_.z1; o.hb_z2 = head_bump_.z2;
    o.drop_g = drop_g_cur_; o.snag = snag_cents_;
    o.f_clk = f_clk_; o.fold_blend = fold_blend_; o.fold_drive = fold_drive_;
  }
  // Current injected-noise amplitude (linear) of the active chain — the module
  // uses it to set the gate threshold just above the hiss floor (tracks depth).
  float NoiseFloorLin() const {
    return (active_chain_ == -1) ? bbd_noise_lin_
         : (active_chain_ == +1) ? tape_noise_lin_ : 0.f;
  }

  // Control-rate (from mnemonic Controls, ~10 ms): set bipolar position.
  void SetDepth(float p) {
    int tgt = (p < -MNEMD_DEADZONE) ? -1 : (p > MNEMD_DEADZONE) ? +1 : 0;
    target_chain_ = tgt;
    d_target_ = (tgt == 0) ? 0.f : (fabsf(p) - MNEMD_DEADZONE) / (1.f - MNEMD_DEADZONE);
    // Tape side only: extend the travel past the stock endpoint (see
    // SetTapeDepthScale). The BBD side keeps its 0..1 range.
    if (tgt == +1) d_target_ *= tape_depth_scale_;
  }

  // True once fully disengaged (K4 in the clean centre deadzone AND any chain-
  // switch crossfade has finished). While idle the host can SKIP TapePitchCents()
  // and ColourProcess() entirely — they do audible nothing but still cost 3 sinf
  // + control-rate powf per sample, which is wasted CPU when K4 is clean.
  bool Idle() const { return active_chain_ == 0 && target_chain_ == 0 && mix_ < 1e-3f; }

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
    bbd_breath_ = dev;   // reuse the slow wow/OU fluctuation as the BBD amplitude "breath"
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

    // Signal-keyed noise gate: peak-follow the input (instant attack, gentle
    // release = anti-chatter presence detector), open/close the gain fast/slow.
    // Threshold tracks the injected-noise floor so it shuts once the signal
    // drops to hiss level. Applied to BOTH chains' injected noise below.
    float ax = fabsf(x);
    if (ax > nz_det_) nz_det_ = ax;
    else              nz_det_ += (ax - nz_det_) * ngate_det_rel_;
    // Soft knee: glide 0..1 across [KNEE..1]xTHR instead of a hard flip, so a
    // decaying note easing through the threshold doesn't chatter the gate.
    const float thr_hi = NoiseFloorLin() * MNEMD_NGATE_THR;
    const float thr_lo = thr_hi * MNEMD_NGATE_KNEE;
    float ng_tgt;
    if      (nz_det_ <= thr_lo) ng_tgt = 0.f;
    else if (nz_det_ >= thr_hi) ng_tgt = 1.f;
    else { float u = (nz_det_ - thr_lo) / (thr_hi - thr_lo); ng_tgt = u * u * (3.f - 2.f * u); } // smoothstep
    // Fast one-pole attack (noise arrives with the note); slow LINEAR release
    // (even, unhurried fade-out — no exponential early lurch).
    if (ng_tgt > noise_sgate_) noise_sgate_ += (ng_tgt - noise_sgate_) * ngate_atk_;
    else { noise_sgate_ -= ngate_rel_step_; if (noise_sgate_ < ng_tgt) noise_sgate_ = ng_tgt; }

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
    // that fluctuates in/out as the knob moves). Integer hold = the clean "correct" tone.
    float f_target = MNEMD_FCLK_D0 * powf(MNEMD_FCLK_D1 / MNEMD_FCLK_D0, d_);
    // Hold length is FRACTIONAL (see the ZOH in Bbd()): the drift below moves it
    // continuously, so quantising to an integer would both defeat the drift and
    // make the slip events land on a small repeating set of clocks. With
    // drift = 0 and no slip this is the integer value it always was.
    bbd_hold_f_ = sr_ / f_target * bbd_slip_mult_
                * (1.f + MNEMD_BBD_DRIFT_MAX * bbd_drift_amt_ * ou_);
    if (bbd_hold_f_ < 1.f) bbd_hold_f_ = 1.f;
    bbd_hold_len_ = (int)(bbd_hold_f_ + 0.5f);   // reported only (diagnostics)
    f_clk_ = sr_ / (float)bbd_hold_len_;              // the actual, quantised clock
    // Filter cutoffs track the SMOOTH target clock, NOT the quantised f_clk_. The
    // ZOH hold length stays integer (no ratio sizzle), but the reconstruction /
    // loss / anti-alias LPFs glide continuously as the knob turns — otherwise
    // f_clk_ steps at each integer boundary and the biquads recompute coeffs
    // instantly, which reads as a click between settings. They only roll off
    // imaging; they don't need to lock to the exact quantised clock.
    // Clamp cutoffs safely below Nyquist: near the clean centre the clock approaches the
    // system rate, so IN_AA*f can exceed sr/2 and blow up the RBJ biquad.
    // (No folding happens up there anyway — it's the near-clean zone.)
    const float nyq = sr_ * 0.49f;
    float in_fc  = MNEMD_BBD_IN_AA * f_target; if (in_fc  > nyq) in_fc  = nyq;
    // Reconstruction + stage-loss LPFs: factor*f_target, but floored so the deep/dark
    // end opens up while shallow (>= ~9:00) settings — already above the floor —
    // stay exactly as they were. Then clamp below Nyquist for filter stability.
    // Floor only (see SetBbdLpfScale), and hard-capped well clear of Nyquist so
    // no per-instance value can ever walk these biquads to the edge.
    float lpf_floor = MNEMD_BBD_LPF_FLOOR_HZ * bbd_lpf_scale_;
    if (lpf_floor > sr_ * 0.25f) lpf_floor = sr_ * 0.25f;
    float rec_fc  = MNEMD_BBD_REC  * f_target; if (rec_fc  < lpf_floor) rec_fc  = lpf_floor;
    float loss_fc = MNEMD_BBD_LOSS * f_target; if (loss_fc < lpf_floor) loss_fc = lpf_floor;
    if (rec_fc  > nyq) rec_fc  = nyq;
    if (loss_fc > nyq) loss_fc = nyq;
    bbd_in_lp_.LP(in_fc,  0.707f, sr_);   // pre-decimation: >0.5 f_clk folds = grit
    bbd_rec_lp_.LP(rec_fc, 0.707f, sr_);  // post-line: floored so the dark end isn't muffled
    bbd_loss_.SetLP(loss_fc, sr_);
    // d^2, not d: neither chain loses much until it is some way in, so a linear
    // blend makes the shallow end HOTTER than the clean centre it is matching.
    // Clamp at 1 first: the tape travel can be EXTENDED past full depth
    // (SetTapeDepthScale), and this constant means 'gain at full depth', not
    // 'gain per unit depth' — unclamped, a 1.5x travel squared to 2.25x comp.
    { const float dq = d_ > 1.f ? 1.f : d_; const float dc = dq * dq;
      bbd_comp_  = 1.f + (bbd_depth_comp_  - 1.f) * dc;   // exactly 1 at the deadzone
      tape_comp_ = 1.f + (tape_depth_comp_ - 1.f) * dc; }
    bbd_noise_lin_ = powf(10.f, (MNEMD_BBD_NOISE_DB0 +
                          (MNEMD_BBD_NOISE_DB1 - MNEMD_BBD_NOISE_DB0) * d_) / 20.f);
    bbd_nl_drive_ = 1.f + MNEMD_BBD_NL_DRIVE * d_;
    // Sine-fold: 0 up to KNEE (shallow BBD untouched), ramps to MAX at full CCW.
    // depth scales BOTH the mix (fold_blend_) AND the drive (fold_drive_): gentle
    // at the knee, gnarly at full CCW. Relative comp divides by the LIVE drive so the
    // level stays controlled across the sweep (full CCW == the approved fixed voicing).
    float fbp = (d_ - MNEMD_FOLD_KNEE) / (1.f - MNEMD_FOLD_KNEE);
    if (fbp < 0.f) fbp = 0.f;
    fold_blend_ = fbp * MNEMD_FOLD_BLEND_MAX;
    float dp = powf(fbp, MNEMD_FOLD_DRIVE_CURVE);   // shape the drive ramp (>1 = low longer, hard near CCW)
    fold_drive_ = MNEMD_FOLD_GAIN_MIN + (MNEMD_FOLD_GAIN - MNEMD_FOLD_GAIN_MIN) * dp;
    fold_out_   = MNEMD_FOLD_MAKEUP / fold_drive_;

    // Crush step: 0 up to KNEE, ramping to STEP*scale at full CCW.
    float cbp = (d_ - MNEMD_CRUSH_KNEE) / (1.f - MNEMD_CRUSH_KNEE);
    if (cbp < 0.f) cbp = 0.f;
    crush_step_ = MNEMD_CRUSH_STEP * crush_scale_ * powf(cbp, MNEMD_CRUSH_CURVE);

    // Tape coeffs
    tape_lp_.SetLP(MNEMD_TAPE_LP_D0 * powf(MNEMD_TAPE_LP_D1 / MNEMD_TAPE_LP_D0, d_), sr_);
    tape_hp_hz_ = MNEMD_TAPE_HP_D0 + (MNEMD_TAPE_HP_D1 - MNEMD_TAPE_HP_D0) * d_;
    // d_ can exceed 1 (SetTapeDepthScale), so bound the HP: MnemdDCBlock's
    // R = 1 - 2*pi*fc/sr goes negative (unstable) above sr/2pi ~= 7.6 kHz.
    // 5 % of sr = 2.4 kHz is far above any musical setting and never reached
    // at the stock endpoint (45 Hz), so this changes nothing today.
    if (tape_hp_hz_ > sr_ * 0.05f) tape_hp_hz_ = sr_ * 0.05f;
    tape_hp_.Set(tape_hp_hz_, sr_);
    head_bump_.Peak(MNEMD_HEADBUMP_HZ, MNEMD_HEADBUMP_Q, MNEMD_HEADBUMP_DB1 * d_, sr_);
    // Saturation + noise use a shaped depth (sqrt) so they ramp in SOONER on
    // than the wow/flutter/snag terms (which stay linear in d_).
    float ds = sqrtf(d_);
    sat_k_ = 1.f + MNEMD_SAT_K * tape_drive_scale_ * ds;
    sat_a_ = MNEMD_SAT_A * tape_drive_scale_ * ds;
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

    // Poisson clock-slip events, BBD only, above the knee. Same shape as the
    // tape dropout/snag scheduler below.
    if (active_chain_ == -1) {
      const float dd   = (d_ - MNEMD_BBD_SLIP_KNEE) / (1.f - MNEMD_BBD_SLIP_KNEE);
      const float pblk = (float)MNEMD_CTRL / sr_;
      const bool  armed = (bbd_slip_amt_ > 0.f && d_ > MNEMD_BBD_SLIP_KNEE);
      // Replay (stutter) events: timed, unchanged.
      if (bbd_slip_left_ > 0) {
        bbd_slip_left_ -= MNEMD_CTRL;
        if (bbd_slip_left_ <= 0) bbd_replay_on_ = false;
      } else if (armed) {
        if (Rand() < MNEMD_BBD_SLIP_RATE * MNEMD_BBD_REPLAY_SHARE
                     * bbd_slip_amt_ * dd * pblk) StartBbdSlip(dd);
      }
      // Starving capacitor: sag continuously in the current direction, and bump
      // (rarely) to a new one. No target, no release — it only ever drifts away.
      if (armed) {
        bbd_slip_mult_ += bbd_sag_dir_ * MNEMD_BBD_SAG_RATE * dd * dt;
        if (Rand() < MNEMD_BBD_BUMP_RATE * bbd_slip_amt_ * dd * pblk) {
          bbd_sag_dir_    = (Rand() < MNEMD_BBD_SAG_DOWN) ? +1.f : -1.f;
          bbd_slip_mult_ += bbd_sag_dir_ * MNEMD_BBD_BUMP_DEPTH * dd;
        }
        // Travel limits. Hitting one turns it around, so it never parks.
        const float hi = 1.f + (MNEMD_BBD_SLIP_MULT - 1.f) * dd;
        const float lo = 1.f - (1.f - MNEMD_BBD_SLIP_MULT_LO) * dd;
        if (bbd_slip_mult_ > hi) { bbd_slip_mult_ = hi; bbd_sag_dir_ = -1.f; }
        else if (bbd_slip_mult_ < lo) { bbd_slip_mult_ = lo; bbd_sag_dir_ = +1.f; }
      } else {
        bbd_slip_mult_ = 1.f;
      }
    } else {
      bbd_slip_left_ = 0; bbd_slip_mult_ = 1.f; bbd_replay_on_ = false;
    }

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
    bbd_samp_ctr_ = 0; bbd_phase_ = 0.f; bbd_hold_ = 0.f;
    bbd_slip_left_ = 0; bbd_slip_mult_ = 1.f; bbd_sag_dir_ = 1.f;
    crush_step_ = 0.f;
    tape_lp_.Reset(); tape_hp_.Reset(); head_bump_.Reset();
    tape_noise_lp1_.Reset(); tape_noise_lp2_.Reset(); sat_x1_ = 0.f;
    env_ = 0.f; drop_left_ = 0; drop_gain_ = drop_g_cur_ = 1.f; snag_cents_ = 0.f;
  }

  // ---- BBD chain (spec §3) ----------------------------------------------
  float Bbd(float x) {
    // NO compander (removed): it burned two powf/sample and, in the feedback
    // loop, kept RESTORING the signal each lap so the repeats never crumbled.
    // Without it the nonlinearity + ZOH + loss + raw noise accumulate lap over
    // lap -> repeats get darker, hissier, dirtier (the real BBD-in-feedback
    // behaviour). The "breathing" is approximated far more cheaply below.
    x = tanhf(x * bbd_nl_drive_) / bbd_nl_drive_;          // nonlinearity (grit; compounds)
    x = bbd_in_lp_.Process(x);                             // input anti-alias (fold-down grit)
    // decimate ZOH @ f_clk (collapsed line, in-place) + noise. Integer sample
    // counter -> exactly bbd_hold_len_ samples/hold (quantised f_clk, no jitter).
    // Fractional-phase ZOH: accumulate 1 per sample and fire when the phase
    // passes the (moving, fractional) hold length, carrying the remainder over.
    // At a constant integer hold this is exactly the old integer counter.
    bbd_phase_ += 1.f;
    if (bbd_phase_ >= bbd_hold_f_) {
      bbd_phase_ -= bbd_hold_f_;
      if (bbd_phase_ >= bbd_hold_f_) bbd_phase_ = 0.f;   // hold shrank under us
      float n = bbd_noise_lp_.LP((Rand() * 2.f - 1.f)) * bbd_noise_lin_ * noise_gate_ * noise_sgate_;
      bbd_hist_[bbd_hist_w_ & (MNEMD_BBD_HIST_N - 1)] = x + n;   // history of held values
      bbd_hist_w_++;
      const float live = x + n;                            // raw noise -> accumulates in feedback
      const int   xf_n = (int)(MNEMD_BBD_REPLAY_XF_MS * 0.001f * sr_);
      const bool  replaying = (bbd_replay_on_ && bbd_slip_left_ > 0 && bbd_replay_len_ > 1);
      // Fade out once the event is within a crossfade of its end, so the return
      // to live is as gradual as the entry. Stepped per hold, hence hold_f.
      const float xf_tgt  = (replaying && bbd_slip_left_ > xf_n) ? 1.f : 0.f;
      const float xf_step = bbd_hold_f_ / (float)(xf_n > 1 ? xf_n : 1);
      if      (bbd_xf_ < xf_tgt) { bbd_xf_ += xf_step; if (bbd_xf_ > 1.f) bbd_xf_ = 1.f; }
      else if (bbd_xf_ > xf_tgt) { bbd_xf_ -= xf_step; if (bbd_xf_ < 0.f) bbd_xf_ = 0.f; }

      if (replaying || bbd_xf_ > 0.f) {
        const float rep = bbd_hist_[(bbd_replay_start_ + bbd_replay_pos_)
                                    & (MNEMD_BBD_HIST_N - 1)];
        if (++bbd_replay_pos_ >= bbd_replay_len_) bbd_replay_pos_ = 0;
        // A/B: the replay REPLACES the live signal for the event, faded in and
        // out by bbd_xf_. At mix 1.0 you hear the stutter alone.
        bbd_hold_ = live + (rep * MNEMD_BBD_REPLAY_POL - live)
                           * (bbd_xf_ * bbd_replay_mix_);
      } else {
        bbd_hold_ = live;
      }
      // Quantise the held sample. Once per hold, so it coarsens the existing
      // staircase rather than introducing a second rate.
      if (crush_step_ > 0.f)
        bbd_hold_ = crush_step_ * floorf(bbd_hold_ / crush_step_ + 0.5f);
    }
    x = bbd_hold_;                                         // zero-order hold (imaging kept)
    // Ring modulation, at audio rate so the modulator can be smoothed out of its
    // own staircase. Always runs the filter so it never restarts from stale
    // state; bbd_xf_ ramps the depth, keeping the boundaries click-free.
    x = bbd_loss_.LP(x);                                   // stage loss (darkening; compounds)
    x = bbd_rec_lp_.Process(x);                            // reconstruction LP (dark, tames imaging)
    // Sum the replayed run in here, AFTER reconstruction (see the note above).
    // bbd_xf_ ramps it in and out, so the boundaries stay click-free.

    // Parallel sine-fold (aging stages overflow): regenerate mids/highs the dark
    // LPF removed and blend on top by depth. Dynamics-reactive by construction —
    // the raw signal level drives the fold depth (louder in = more overtones).
    if (fold_blend_ > 1e-4f || MNEMD_FOLD_SOLO) {
      float f = sinf(x * fold_drive_);                     // sine wavefold — depth-scaled drive; signal level drives fold depth (dynamics-reactive)
      if (MNEMD_FOLD_HP_ON) f -= fold_hp_lp_.LP(f);        // optional high-pass, AFTER the folder (keep only restored mids/highs)
      f *= fold_out_;                                      // level compensated RELATIVE to the live drive (drive harder without getting louder)
      if (MNEMD_FOLD_SOLO)                                 // DEBUG: hear ONLY the folded signal
        return f * MNEMD_FOLD_SOLO_GAIN * bbd_makeup_;
      x += fold_blend_ * fold_scale_ * f;                  // add on top; BBD body untouched.
                                                           // fold_scale_ = per-instance brightness
                                                           // (1 = default; vestige boosts for clarity)
    }
    // "Breathing": a subtle amplitude flicker driven by the shared wow/OU
    // fluctuation (same slow signal as the BBD clock drift) — approximates the
    // compander's level breathing for ~1 mult, no powf, and does NOT restore.
    x *= 1.f + MNEMD_BBD_BREATH * bbd_breath_ * d_;
    return x * bbd_makeup_ * bbd_level_ * bbd_comp_;
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
    x += n * nlvl * noise_gate_ * noise_sgate_;
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
    return x * tape_makeup_ * tape_level_ * tape_comp_;
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
  // Event length in ms. Shared by the replay events and the rate excursions.
  float BbdSlipDurMs() {
    float lo = MNEMD_BBD_SLIP_MIN_MS, hi = MNEMD_BBD_SLIP_MAX_MS;
    if (slip_ref_blend_ > 0.f && slip_ref_ms_ > 0.f) {
      float rlo = slip_ref_ms_ * MNEMD_BBD_SLIP_REF_MIN;
      float rhi = slip_ref_ms_ * MNEMD_BBD_SLIP_REF_MAX;
      if (rhi > MNEMD_BBD_SLIP_REF_CAP_MS) rhi = MNEMD_BBD_SLIP_REF_CAP_MS;
      if (rlo > rhi) rlo = rhi;
      lo += (rlo - lo) * slip_ref_blend_;
      hi += (rhi - hi) * slip_ref_blend_;
    }
    return lo + Rand() * (hi - lo);
  }
  void StartBbdSlip(float dd) {                           // dd = 0..1 above the knee
    (void)dd;
    bbd_slip_left_ = (int)(BbdSlipDurMs() * 0.001f * sr_);
    {
      bbd_replay_on_ = true;                 // replay: rate untouched
      // Loop length in ms -> whole held values at the CURRENT clock, bounded by
      // the history we actually have.
      const float lms = MNEMD_BBD_REPLAY_MIN_MS +
                        Rand() * (MNEMD_BBD_REPLAY_MAX_MS - MNEMD_BBD_REPLAY_MIN_MS);
      int w = (int)(lms * 0.001f * sr_ / (bbd_hold_f_ > 1.f ? bbd_hold_f_ : 1.f));
      if (w < 2) w = 2;
      if (w > MNEMD_BBD_HIST_N - 1) w = MNEMD_BBD_HIST_N - 1;
      bbd_replay_len_   = w;
      bbd_replay_start_ = bbd_hist_w_ - (unsigned)w;   // the run just captured
      bbd_replay_pos_   = 0;
    }
    // The rate excursion lives in the control tick now, not here.
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
  float noise_sgate_ = 0.f;                      // signal-keyed noise gate gain (0 closed .. 1 open)
  float nz_det_ = 0.f;                           // input presence follower (peak, gentle release)
  float ngate_atk_ = 0.f, ngate_rel_step_ = 0.f, ngate_det_rel_ = 0.f;
  int   active_chain_ = 0, target_chain_ = 0;    // -1 BBD · 0 bypass · +1 tape
  float d_ = 0.f, d_target_ = 0.f, mix_ = 0.f, xfade_coef_ = 0.f;
  int   ctrl_ctr_ = 1;
  uint32_t rng_ = 0x51ed3a7bu;

  // shared
  float env_ = 0.f, env_atk_ = 0.f, env_rel_ = 0.f;
  MnemdDCBlock dc_;

  // BBD
  MnemdBiquad  bbd_in_lp_, bbd_rec_lp_;
  MnemdOnePole bbd_loss_, bbd_noise_lp_;
  float f_clk_ = 48000.f, bbd_hold_ = 0.f;
  int   bbd_hold_len_ = 1, bbd_samp_ctr_ = 0;
  float bbd_noise_lin_ = 0.f, bbd_nl_drive_ = 1.f;
  float bbd_breath_ = 0.f;                        // slow amplitude flicker (from the wow/OU mod block)
  MnemdOnePole fold_hp_lp_;                        // fold-return HP (via subtract-LP)
  float fold_blend_ = 0.f;                         // parallel sine-fold mix (0 until past KNEE)
  float fold_drive_ = MNEMD_FOLD_GAIN;             // depth-scaled fold drive (GAIN_MIN..GAIN)
  float fold_out_   = MNEMD_FOLD_MAKEUP / MNEMD_FOLD_GAIN; // relative level comp = MAKEUP/fold_drive_
  float fold_scale_ = 1.f;                         // per-instance fold brightness (SetFoldScale; 1 = default)
  float bbd_lpf_scale_    = 1.f;                   // per-instance BBD brightness (SetBbdLpfScale)
  float crush_scale_ = 0.f, crush_step_ = 0.f;   // bit-crush (SetBbdCrush)
  float tape_drive_scale_ = 1.f;                   // per-instance tape drive (SetTapeDriveScale)
  float tape_level_       = 1.f;                   // per-instance tape output level (SetTapeLevel)
  float bbd_level_        = 1.f;                   // per-instance BBD output level (SetBbdLevel)
  float bbd_depth_comp_  = 1.f, tape_depth_comp_ = 1.f;  // gain at FULL depth (1 = off)
  float bbd_comp_ = 1.f, tape_comp_ = 1.f;               // ... interpolated by depth
  float tape_depth_scale_ = 1.f;                   // per-instance CW travel extension (SetTapeDepthScale)
  float bbd_slip_amt_  = 0.f;                      // per-instance BBD clock-slip amount (SetBbdSlip)
  float bbd_slip_mult_ = 1.f;                      // active slip: hold-length stretch (1 = none)
  float bbd_sag_dir_   = 1.f;                      // +1 = sagging slower, -1 = drifting faster
  int   bbd_slip_left_ = 0;                        // samples remaining in the current slip
  float bbd_drift_amt_ = 0.f;                      // per-instance continuous clock drift (SetBbdDrift)
  bool  bbd_replay_on_    = false;                    // current event corrupts CONTENT, not rate
  float bbd_hist_[MNEMD_BBD_HIST_N] = {};          // recent held values (the replay source)
  unsigned bbd_hist_w_ = 0;
  unsigned bbd_replay_start_ = 0;                  // first held value of the replayed run
  int      bbd_replay_len_ = 0, bbd_replay_pos_ = 0;
  float    bbd_xf_ = 0.f;                          // live <-> replay crossfade (0..1)
  float    bbd_replay_mix_ = 0.f;                  // per-instance ring depth (SetBbdReplayMix)
  float slip_ref_ms_    = 0.f;                     // host time reference for slip length
  float slip_ref_blend_ = 0.f;                     // 0 = fixed ms, 1 = fully reference-scaled
  float bbd_hold_f_    = 1.f;                      // fractional hold length (the live clock)
  float bbd_phase_     = 0.f;                      // ZOH phase accumulator
  float bbd_makeup_ = 1.25f;                      // level match — raised after compander removal (restores loop gain / self-osc)

  // Tape
  MnemdOnePole tape_lp_, tape_noise_lp1_, tape_noise_lp2_;
  MnemdDCBlock tape_hp_;
  MnemdBiquad  head_bump_;
  float tape_hp_hz_ = 30.f, sat_k_ = 1.f, sat_a_ = 0.f, sat_bias_ = 0.f, sat_x1_ = 0.f;
  float tape_noise_lin_ = 0.f, tape_noise_env_ = 0.f, tape_makeup_ = 1.3f;

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
