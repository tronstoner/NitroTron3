#pragma once

// --- Oscillator (Tuning Page 1) ---
constexpr float OSC_K            = 0.350f;  // parabolic curve: 0=linear saw, 0.5+=very round
constexpr float OSC_DC_TRIM      = 0.000f;  // fine DC offset after shaping
constexpr float OSC_FOLD_AMT     = 0.150f;  // triangle-core fold (reserved)
constexpr float OSC_PEAK_GAIN    = 1.000f;  // pre-filter oscillator trim

// --- Envelope follower (Tuning Page 2) --- (Stage 3)
constexpr float ENV_LP_CUTOFF_HZ = 50.0f;   // envelope LP cutoff (higher = faster tracking)
constexpr float ENV_THRESHOLD    = 0.002f;   // gate threshold (low to avoid cutting quiet dynamics)
constexpr float ENV_PRE_GAIN     = 1.000f;   // input gain before rectifier
constexpr float ENV_ATTACK_BIAS  = 1.000f;   // filter asymmetry, attack
constexpr float ENV_RELEASE_BIAS = 1.000f;   // filter asymmetry, release


// --- Stage / mix / ladder (Tuning Page 3) --- (Stage 2–3)
constexpr float OSC_GAIN         = 2.000f;   // final osc level into mix
constexpr float LADDER_DRIVE     = 1.000f;   // ladder input gain
constexpr float LADDER_CUTOFF_OFFSET = 0.000f; // tone knob trim
constexpr float DRY_TRIM         = 1.000f;   // dry path level trim
