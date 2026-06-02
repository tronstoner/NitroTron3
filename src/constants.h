#pragma once
#include <cstdint>

// --- Oscillator (Tuning Page 1) ---
constexpr float OSC_K            = 0.480f;  // parabolic curve: 0=linear saw, 0.5+=very round
constexpr float OSC_DC_TRIM      = 0.000f;  // fine DC offset after shaping
constexpr float OSC_FOLD_AMT     = 0.150f;  // triangle-core fold (reserved)
constexpr float OSC_PEAK_GAIN    = 1.000f;  // pre-filter oscillator trim
constexpr float OSC_SAW_GAIN     = 1.500f;  // per-waveform level trim
constexpr float OSC_TRI_GAIN     = 1.400f;  // reference level
constexpr float OSC_SQR_GAIN     = 1.400f;

// --- Envelope follower (Tuning Page 2) --- (Stage 3)
constexpr float ENV_LP_CUTOFF_HZ = 50.0f;   // envelope LP cutoff (higher = faster tracking)
constexpr float ENV_PRE_GAIN     = 1.000f;   // input gain before rectifier
constexpr float ENV_ATTACK_BIAS  = 1.000f;   // filter asymmetry, attack
constexpr float ENV_RELEASE_BIAS = 1.000f;   // filter asymmetry, release


// --- Envelope modulation ---
constexpr float ENV_FILTER_MOD   = 0.500f;   // envelope → filter cutoff (subtle opening)
constexpr float ENV_FOLD_MOD     = 0.250f;   // envelope → wavefold amount (×5 internally)

// --- Preset system timing (from ux-demo.html) ---

// LED 1: Roman numeral preset blink
constexpr uint32_t LED_SHORT_ON_MS     = 150;   // I symbol on duration
constexpr uint32_t LED_LONG_ON_MS      = 950;   // V symbol on duration
constexpr uint32_t LED_ELEM_GAP_MS     = 200;   // gap between symbols
constexpr uint32_t LED_REPEAT_GAP_MS   = 600;   // gap before pattern repeats

// LED 2: dirty indicator
constexpr uint32_t LED_DIRTY_ON_MS     = 50;    // dirty flash on time
constexpr uint32_t LED_DIRTY_OFF_MS    = 50;    // dirty flash off time

// LED 2: save mode
constexpr uint32_t LED_SAVE_MODE_ON_MS  = 150;  // save mode blink on
constexpr uint32_t LED_SAVE_MODE_OFF_MS = 150;  // save mode blink off

// LED 2: save confirm burst
constexpr uint32_t LED_SAVE_CONFIRM_DUR_MS = 500; // total burst duration
constexpr uint32_t LED_SAVE_CONFIRM_ON_MS  = 75;  // burst on time
constexpr uint32_t LED_SAVE_CONFIRM_OFF_MS = 75;  // burst off time

// Footswitch timing
constexpr uint32_t FS_LONG_PRESS_MS    = 700;   // long press threshold
// Bootloader: handled by hw.CheckResetToBootloader() at 2000 ms

// Knob dirty detection
constexpr float KNOB_DIRTY_THRESHOLD   = 0.02f; // 2% travel to trigger dirty

// Pitch tracking
constexpr int TRACKING_WRAP_NOTE       = 9;     // octave-locked wrap point (9 = A)

// --- Stage / mix / ladder (Tuning Page 3) --- (Stage 2–3)
constexpr float OSC_GAIN         = 1.500f;   // final osc level into mix
constexpr float LADDER_DRIVE     = 1.800f;   // ladder input gain (higher = more tanh warmth)
constexpr float LADDER_CUTOFF_OFFSET = 0.000f; // tone knob trim
constexpr float DRY_TRIM         = 1.000f;   // dry path level trim

// --- Mode C — Schism ---
// Sine wavefolder (SW1=UP, K4 = fold amount). Compensation curve ear-tuned in C.2.
constexpr float SINEFOLD_DRIVE_MAX   = 35.0f;  // pre-sin drive at K4=1 (1× at K4=0)
constexpr float SINEFOLD_COMP_AT_MAX = 0.55f;  // post-fold gain at K4=1 (1.0 at K4=0)

// Moog ladder (SW2=UP, K1 cutoff / K2 resonance / K3 env amount).
constexpr float MODE_C_LADDER_RES_MAX = 1.2f;  // pushed past ~1.0 self-osc threshold; in-loop tanh bounds it
constexpr float MODE_C_ENV_MOD_RANGE  = 30.0f; // depth on normalized env; lift = 1 + env_scaled * |k3| * range
constexpr float MODE_C_ENV_SCALE      = 10.0f; // passive-bass env normalization (matches Mode A's effective ×10)
constexpr float MODE_C_CUTOFF_MAX_HZ  = 10000.f; // top clamp for env-modulated cutoff (keeps Huovilainen stable)
constexpr float K3_DEADZONE           = 0.05f; // bipolar K3 noon ±deadzone → env amount = 0

// Mode C filter drive (K5 — bipolar around noon, applies across all SW2 filter modes).
// CCW → attenuate, NOON → unity (1.0), CW → boost. Piecewise linear in dB-ish space.
constexpr float MODE_C_DRIVE_MIN      = 0.25f; // K5 full CCW → ~−12 dB attenuation
constexpr float MODE_C_DRIVE_MAX      = 8.0f;  // K5 full CW  → very hot pre-filter saturation

// Per-filter internal input pads — apply BEFORE the filter's own saturator.
// Moog and Grendel are spectrum-shaping filters with sweet spots at line-level
// (~0.2–0.3 amplitude). K5 noon (1.0) × pad puts them in their clean zone, while
// K5 CW still drives them hard. Plague is intentionally not padded — it's a
// feedback-saturating filter that needs hot input to engage its tanh fold.
constexpr float MODE_C_MOOG_INPUT_PAD    = 0.3f;
constexpr float MODE_C_GRENDEL_INPUT_PAD = 0.3f;

// Amp-env VCA — final wet-path gate (same pattern as Mode A's drone gating).
// Multiplies wet by env_val × MODE_C_VCA_GAIN so the wet path is silent when the
// bass is silent (kills self-resonance ringing alone), and opens up as you play.
// Passive bass env peaks ~0.06–0.1, so gain 12 puts the gate "fully open"
// around typical playing dynamics with mild boost on hard plucks.
constexpr float MODE_C_VCA_GAIN = 12.0f;

// Post-filter peak limiter (Mode C only, all SW2 modes).
// 2-band split: LF (≤ SPLIT_HZ) passes through untouched so bass fundamentals
// don't duck when resonance peaks fire the limiter. HF is soft-knee limited.
// "Warmth-when-working" — gain reduction modulates a touch of tanh saturation,
// so peaks gain mild character without harmonics on quiet/clean signals.
constexpr float MODE_C_LIMIT_SPLIT_HZ  = 160.f; // 2-band crossover (one-pole)
constexpr float MODE_C_LIMIT_THR       = 0.6f;  // amplitude threshold (linear)
constexpr float MODE_C_LIMIT_RATIO_INV = 0.5f;  // 1/ratio — 0.5 ≈ 2:1 soft slope
constexpr float MODE_C_LIMIT_ATK_MS    = 2.0f;  // catches resonance transients
constexpr float MODE_C_LIMIT_REL_MS    = 60.0f; // slow enough to avoid sideband mod
constexpr float MODE_C_LIMIT_WARM_DRV  = 1.5f;  // tanh drive at full GR
constexpr float MODE_C_LIMIT_WARM_MIX  = 0.35f; // tanh blend at full GR

// Grendel formant filter (SW2=MID). K1 = vowel path, K2 = size, K3 = env on path.
constexpr int   GRENDEL_NUM_FORMANTS = 4;
constexpr int   GRENDEL_NUM_VOWELS   = 5;
constexpr float GRENDEL_FORMANT_Q    = 12.0f;  // mid-high Q for vowel-like ringing
constexpr float GRENDEL_OUT_GAIN     = 4.0f;   // BPFs attenuate heavily; ear-tune in C.5

// Vowel path: ee → eh → ah → oh → oo (F1..F4 in Hz, adult-male typicals).
constexpr float GRENDEL_VOWELS[GRENDEL_NUM_VOWELS][GRENDEL_NUM_FORMANTS] = {
  {270.f, 2290.f, 3010.f, 3500.f},  // 0: ee /i/
  {530.f, 1840.f, 2480.f, 3500.f},  // 1: eh /ɛ/
  {730.f, 1090.f, 2440.f, 3400.f},  // 2: ah /ɑ/
  {570.f,  840.f, 2410.f, 3300.f},  // 3: oh /o/
  {300.f,  870.f, 2240.f, 3200.f},  // 4: oo /u/
};
constexpr float GRENDEL_FORMANT_GAIN[GRENDEL_NUM_FORMANTS] = {1.0f, 0.85f, 0.6f, 0.35f};

constexpr float GRENDEL_SIZE_MIN       = 0.5f;  // K2=0 → centers × 0.5 (large mouth)
constexpr float GRENDEL_SIZE_MAX       = 1.6f;  // K2=1 → centers × 1.6 (small mouth)
constexpr float GRENDEL_ENV_PATH_RANGE = 1.0f;  // env at K3=±1 shifts path by ±1.0 (full vowel space)

// Plague filter (SW2=DOWN). Initial values; ear-tune in C.4.
constexpr float PLAGUE_LOW_HZ            = 220.0f;  // lo band SatSVF center
constexpr float PLAGUE_HIGH_HZ           = 1800.0f; // hi band SatSVF center
constexpr float PLAGUE_INPUT_RATIO       = 6.0f;    // pre-saturation drive at K2=1 (0 at K2=0)
constexpr float PLAGUE_FB_BASE           = 0.5f;    // feedback drive at K2=0 (mild ring)
constexpr float PLAGUE_FB_RANGE          = 0.5f;    // additional feedback drive at K2=1 (tanh in loop prevents runaway)
constexpr float PLAGUE_BALANCE_ENV_SCALE = 0.5f;    // K3·env contribution to balance shift (±0.5 traverses full range)
constexpr float PLAGUE_OUT_GAIN          = 1.0f;    // post-sum loudness comp — ear-tune in C.4

// --- Mode B SW1 MIDDLE — Event-Driven Digital Glitch ---
// Bipolar K4: noon = clean. CCW = bit-flip events, CW = timing events
// (freeze / stutter / reverse). Buchla SoU style: stochastic trigger
// timing, randomised per-event parameters, env-gated mix (silence-in →
// silence-out). See docs/MODE_B_TEXTURE_IDEAS.md.
constexpr float GLITCH_DEADZONE             = 0.05f; // ±5% around noon → clean
constexpr int   GLITCH_XOR_MAX_BIT          = 13;    // highest bit flipped at full CCW (±0.25 of full scale)
constexpr float GLITCH_EVENT_RATE_HZ_MAX    = 25.0f; // events/sec at full effect_pos × full env
constexpr int   GLITCH_EVENT_DUR_MIN_SAMPLES = 240;  // 5 ms at 48 kHz
constexpr int   GLITCH_EVENT_DUR_MAX_SAMPLES = 2400; // 50 ms at 48 kHz
constexpr int   GLITCH_BUFFER_SAMPLES       = 2400;  // 50 ms ring buffer for CW timing payload
constexpr int   GLITCH_RAMP_SAMPLES         = 48;    // 1 ms click-free wet/dry ramp

// --- Mode B reverb + bipolar K5 ---
constexpr float K5_CENTER_DEADZONE = 0.05f;  // ±5% deadzone around center
constexpr float REVERB_INPUT_GAIN  = 0.40f;  // gain into the Clouds reverb
constexpr float REVERB_TIME        = 0.70f;  // reverb decay (krt in Clouds)
constexpr float REVERB_MAX_FEEDBACK = 0.95f; // CW side: existing ring-buffer feedback ceiling
constexpr float REVERB_AMT_SMOOTH_COEF = 0.002f; // one-pole on K5 reverb amount, ~10 ms tc
