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
constexpr float SINEFOLD_DRIVE_MAX   = 8.0f;   // pre-sin drive at K4=1 (1× at K4=0)
constexpr float SINEFOLD_COMP_AT_MAX = 0.7f;   // post-fold gain at K4=1 (1.0 at K4=0)

// Moog ladder (SW2=UP, K1 cutoff / K2 resonance / K3 env amount).
constexpr float MODE_C_LADDER_DRIVE   = 1.2f;  // input drive into ladder (Mode A uses 1.8 for osc)
constexpr float MODE_C_LADDER_RES_MAX = 0.95f; // resonance ceiling — stays just below self-osc
constexpr float MODE_C_ENV_MOD_RANGE  = 4.0f;  // env at K3=±1 multiplies cutoff by exp(±range)
constexpr float K3_DEADZONE           = 0.05f; // bipolar K3 noon ±deadzone → env amount = 0

// Plague filter (SW2=DOWN). Initial values; ear-tune in C.4.
constexpr float PLAGUE_LOW_HZ            = 220.0f;  // lo band SatSVF center
constexpr float PLAGUE_HIGH_HZ           = 1800.0f; // hi band SatSVF center
constexpr float PLAGUE_INPUT_RATIO       = 1.5f;    // pre-saturation drive at K2=1 (0 at K2=0)
constexpr float PLAGUE_FB_BASE           = 0.5f;    // feedback drive at K2=0 (mild ring)
constexpr float PLAGUE_FB_RANGE          = 0.45f;   // additional feedback drive at K2=1 (stays below self-osc with tanh in loop)
constexpr float PLAGUE_BALANCE_ENV_SCALE = 0.5f;    // K3·env contribution to balance shift (±0.5 traverses full range)
constexpr float PLAGUE_OUT_GAIN          = 1.0f;    // post-sum loudness comp — ear-tune in C.4

// --- Mode B reverb + bipolar K5 ---
constexpr float K5_CENTER_DEADZONE = 0.05f;  // ±5% deadzone around center
constexpr float REVERB_INPUT_GAIN  = 0.40f;  // gain into the Clouds reverb
constexpr float REVERB_TIME        = 0.70f;  // reverb decay (krt in Clouds)
constexpr float REVERB_MAX_FEEDBACK = 0.95f; // CW side: existing ring-buffer feedback ceiling
constexpr float REVERB_AMT_SMOOTH_COEF = 0.002f; // one-pole on K5 reverb amount, ~10 ms tc
