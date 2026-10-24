#pragma once
//
// sprawl — NitroTron3 Mode B (Granular Glitch), carved VERBATIM from
// pedals/nitrotron3/main.cpp into the Module interface. (Not ChronoTron3's
// sprawl.h — that one has diverged; this is the shipping NitroTron3 Mode B.)
//
// What moved here (no behaviour change, no tuning change):
//   * the Mode B state (main.cpp:58-216, 233, 243) -> members, except what
//     cannot or should not live in the object: the SDRAM buffers
//     (grain_sdram_buf, reverb_buf), the dormant stutter capture buffer (only
//     referenced by the retired `if (false)` stutter block, so the linker drops
//     it, as in the shipping build) and its constants, StutterVoice, and the
//     grain RNG (rng_state / RandFloat) -> file scope, same declarations;
//   * the Mode B harmony logic (main.cpp:359-421) -> file scope;
//   * ProcessGranular's body (main.cpp:559-1196) -> Process(), character for
//     character, behind the same adapters as bordun.h (`in` / `out` / `eb` /
//     `env` / `tracker`);
//   * the Mode B part of main()'s init (main.cpp:1642-1643, 1645-1649,
//     1655-1665) -> Init().
//
// Controls (unchanged): K1 interval / shift, K2 range + direction, K3 density /
// character, K4 texture, K5 reverb / feedback, K6 mix (own law), SW1 texture,
// SW2 harmony. No footswitches, no LEDs (the preset system owns both).
//
// Requires daisy.h + hothouse.h + constants.h + knob_map.h + preset_controls.h
// + module.h + nt3_shared.h included before this file.

// ---- verbatim from pedals/nitrotron3/main.cpp:58-59 ----
static constexpr size_t GRAIN_BUF_SAMPLES = 48000 * 8;  // 8 s at 48 kHz
float DSY_SDRAM_BSS grain_sdram_buf[GRAIN_BUF_SAMPLES];

// ---- verbatim from pedals/nitrotron3/main.cpp:80-83 ----
// Direct-texture mode: stutter capture buffer
static constexpr size_t STUTTER_BUF_SIZE = 9600;   // 200 ms at 48 kHz
static constexpr size_t STUTTER_MIN_LOOP = 960;    // 20 ms min slice
float stutter_buf[STUTTER_BUF_SIZE] = {};

// ---- verbatim from pedals/nitrotron3/main.cpp:87-150 ----
// Stutter voice: Tukey-windowed single-play slice.  Flat in the middle,
// cosine taper at edges (TAPER_LEN samples ≈ 5 ms).  Two voices ping-pong
// with short overlap at the taper region for click-free crossfade.
struct StutterVoice {
    static constexpr size_t MIN_TAPER = 96;   // 2 ms at 48 kHz
    static constexpr size_t MAX_TAPER = 240;  // 5 ms at 48 kHz

    size_t length;     // slice output duration in samples (drives window + timing)
    size_t phase;      // current output sample within slice
    size_t taper;      // computed at trigger: adaptive taper length
    bool   active;
    float  window;     // current window value — exposed for complement crossfade
    float  read_pos_f; // fractional read position in stutter_buf (circular)
    float  rate;       // signed playback rate (|rate| = pitch ratio, sign = dir)

    // start = chunk start in stutter_buf; len = output duration; r = signed rate
    // (negative = reverse). The source span consumed is |r|·len; reverse begins
    // at the far end of that span and reads backward.
    void Trigger(size_t start, size_t len, float r) {
        length = len;
        phase = 0;
        active = true;
        window = 0.f;
        rate = r;
        read_pos_f = (r < 0.f)
            ? static_cast<float>(start) + (-r) * static_cast<float>(len)  // reverse: start at end
            : static_cast<float>(start);
        // Adaptive taper: 10% of length, clamped 2–5 ms.
        taper = static_cast<size_t>(static_cast<float>(len) * 0.1f);
        if (taper < MIN_TAPER) taper = MIN_TAPER;
        if (taper > MAX_TAPER) taper = MAX_TAPER;
        if (taper > length / 2) taper = length / 2;
    }

    float Process(const float* buf, size_t buf_size) {
        if (!active) { window = 0.f; return 0.f; }

        // Tukey window: cosine taper at edges, flat (1.0) in the middle
        if (phase < taper) {
            float t = static_cast<float>(phase) / static_cast<float>(taper);
            window = 0.5f * (1.f - cosf(3.14159265f * t));
        } else if (phase >= length - taper) {
            float t = static_cast<float>(length - 1 - phase) / static_cast<float>(taper);
            window = 0.5f * (1.f - cosf(3.14159265f * t));
        } else {
            window = 1.f;
        }

        // Fractional read with linear interpolation, circular — lets the slice
        // play at an arbitrary (pitch-shifted) rate, forward or reverse.
        float wp = fmodf(read_pos_f, static_cast<float>(buf_size));
        if (wp < 0.f) wp += static_cast<float>(buf_size);
        size_t i0 = static_cast<size_t>(wp);
        size_t i1 = (i0 + 1 < buf_size) ? i0 + 1 : 0;
        float frac = wp - static_cast<float>(i0);
        float sample = buf[i0] * (1.f - frac) + buf[i1] * frac;

        read_pos_f += rate;
        phase++;
        if (phase >= length) { active = false; window = 0.f; }

        return sample * window;
    }
};

// ---- verbatim from pedals/nitrotron3/main.cpp:211-212 ----
// Clouds Reverb (Mode B wet path). 16384-sample uint16_t buffer in SDRAM.
uint16_t DSY_SDRAM_BSS reverb_buf[16384];

// ---- verbatim from pedals/nitrotron3/main.cpp:245-252 ----
// Simple xorshift32 RNG for grain scatter
static uint32_t rng_state = 12345;
static float RandFloat() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return static_cast<float>(rng_state) / 4294967295.f;
}

// ---- verbatim from pedals/nitrotron3/main.cpp:359-421 ----
// ---------------------------------------------------------------------------
// Mode B harmony logic
// ---------------------------------------------------------------------------

// Natural harmonic intervals (semitones). Asymmetric: floor at -1 octave,
// ceiling at +3 octaves, with upper-harmonic partials filled in.
static const int RESONANCES[] = {
    -12, -7, -5, 0, 5, 7, 12, 19, 24, 28, 31, 36
};
static constexpr int NUM_RES = 12;

// K1 → semitones. Lower half always covers -12..0; upper half spans
// 0..max_up. UP mode uses max_up=12 (symmetric ±12); MID/DOWN uses
// max_up=36 so the resonance table can reach upper partials.
static int K1ToSemi(float k1, int max_up) {
  if (k1 < 0.5f) return static_cast<int>(roundf((k1 - 0.5f) * 24.f));
  return static_cast<int>(roundf((k1 - 0.5f) * 2.f * static_cast<float>(max_up)));
}

// Per-semitone feedback scale (SW2 UP / fixed-interval only).
// Unison piles up because each loop pass replays at the same pitch; pitch-down
// loses energy to the wet HPF each pass and needs compensation.
// Curve: unison cut to FB_UNISON_SCALE, up-side ramps back to 1.0 by +3 semi,
// down-side boosts +0.12 per semitone for a saturated growl, capped at 1.9.
static float FixedIntervalFeedbackScale(int k1_semi) {
  if (k1_semi == 0) return FB_UNISON_SCALE;
  if (k1_semi > 0) {
    float t = static_cast<float>(k1_semi) / 3.f;
    if (t > 1.f) t = 1.f;
    return FB_UNISON_SCALE + (1.f - FB_UNISON_SCALE) * t;
  }
  float scale = 1.f + 0.12f * static_cast<float>(-k1_semi);
  if (scale > 1.9f) scale = 1.9f;
  return scale;
}

// Compute pitch ratio for one grain.
// harmony == 0 (SW2 UP): fixed interval, K1 = exact semitones in [-12, +12].
// harmony != 0 (SW2 MID/DOWN): grain picks uniformly from a ±1 entry window
// around K1's closest RESONANCES entry. K1 spans -12..+36 for the table scan.
static float GrainPitchRatio(int harmony, float k1) {
  float semi;

  if (harmony == 0) {
    semi = static_cast<float>(K1ToSemi(k1, 12));
  } else {
    int k1_semi = K1ToSemi(k1, 36);
    int closest = 0;
    int min_dist = 100;
    for (int i = 0; i < NUM_RES; i++) {
      int d = RESONANCES[i] - k1_semi;
      if (d < 0) d = -d;
      if (d < min_dist) { min_dist = d; closest = i; }
    }
    int lo = (closest > 0) ? closest - 1 : 0;
    int hi = (closest < NUM_RES - 1) ? closest + 1 : NUM_RES - 1;
    int idx = lo + static_cast<int>(RandFloat() * static_cast<float>(hi - lo + 1));
    if (idx > hi) idx = hi;
    semi = static_cast<float>(RESONANCES[idx]);
  }

  return powf(2.f, semi / 12.f);
}

class Sprawl : public Module {
 public:
  // Shell-owned shared state (env follower + pitch tracker). Call before Init().
  void Attach(NT3Shared* sh) { sh_ = sh; }

  void Init(float sr) override {
    // ---- verbatim from pedals/nitrotron3/main.cpp:1642-1643 ----
  grain_ring.Init(grain_sdram_buf, GRAIN_BUF_SAMPLES);
  b_shifter.Init(sr);
    // ---- verbatim from pedals/nitrotron3/main.cpp:1645-1649 ----
  // Wet HPF coefficient
  wet_hp_coeff = 1.f / (1.f + 2.f * 3.14159265f * WET_HPF_FREQ / sr);
  fb_duck_atk_g = 1.f - expf(-1.f / (FB_DUCK_ATTACK_MS  * 0.001f * sr));
  fb_duck_rel_g = 1.f - expf(-1.f / (FB_DUCK_RELEASE_MS * 0.001f * sr));
  on_play_rel_g = 1.f - expf(-1.f / (ON_PLAY_RELEASE_MS * 0.001f * sr));
    // ---- verbatim from pedals/nitrotron3/main.cpp:1655-1665 ----
  // Reverb resamplers + reverb engine
  rev_downsampler.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  rev_upsampler_l.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  rev_upsampler_r.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
  reverb_instance.Init(reverb_buf);
  reverb_instance.set_amount(1.0f);  // pure wet; we crossfade externally via K5
  reverb_instance.set_input_gain(REVERB_INPUT_GAIN);
  reverb_instance.set_time(REVERB_TIME);

  glitch_events.Init();
  // diffusion (0.625) and lp (0.7) set by Reverb::Init() defaults
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& /*led1*/, daisy::Led& /*led2*/) override {
    NT3ReadControls(cs, eb_);
  }

  // The shipping mode computes its own dry/wet (K6): the shell passes `wet`
  // through as the final output.
  bool OwnsOutput() const override { return true; }

  void Process(const float* in_mono, float* wet, size_t size) override {
    // Adapters: the names ProcessGranular's body uses.
    const float* const in[1]  = { in_mono };
    float* const       out[2] = { wet, wet };   // mono pedal: out[0] = out[1]
    EnvFollower&  env     = sh_->env;
    PitchTracker& tracker = sh_->tracker;
    const NT3Controls& eb = eb_;

    // ---- verbatim from pedals/nitrotron3/main.cpp:559-1196 (ProcessGranular) ----

  // K1: interval (±24 semi, centered with dead zone)
  float k1 = RemapKnob(eb.knobs[0]);

  // K2: bipolar. Magnitude (|K2-0.5| past the deadzone) = buffer range +
  // timescale, exactly as the old unipolar K2. Sign = global grain playback
  // direction (CW = forward, CCW = backward), replacing randomized per-grain
  // reverse as the *direction* source. Noon deadzone = direct-texture
  // passthrough (grain engine bypassed). See docs/MODE_B_DISCOVERY.md.
  float k2c = RemapKnob(eb.knobs[1]) - 0.5f;         // [-0.5, +0.5]
  bool  direct_texture = (fabsf(k2c) < GRAIN_K2_DEADZONE);
  bool  buf_reverse    = (k2c < -GRAIN_K2_DEADZONE); // backward only past the CCW deadzone; live/deadzone stays forward
  float k2 = (fabsf(k2c) - GRAIN_K2_DEADZONE) / (0.5f - GRAIN_K2_DEADZONE);
  if (k2 < 0.f) k2 = 0.f;
  if (k2 > 1.f) k2 = 1.f;
  size_t max_range = GRAIN_MIN_RANGE +
      static_cast<size_t>(k2 * static_cast<float>(GRAIN_BUF_SAMPLES - GRAIN_MIN_RANGE));

  // K3: bipolar. Noon (padded) = neutral single coherent stream. CW half =
  // character/glitch (as the old unipolar K3). CCW half = Clouds-style
  // deterministic density. The raw knob `k3` is still used by the
  // direct-texture micro-stutter (made bipolar in Stage 4).
  float k3 = RemapKnob(eb.knobs[2]);
  float k3c = k3 - 0.5f;
  bool  cloud_mode = (k3c < -GRAIN_K3_DEADZONE);
  float k3mag = (fabsf(k3c) - GRAIN_K3_DEADZONE) / (0.5f - GRAIN_K3_DEADZONE);
  if (k3mag < 0.f) k3mag = 0.f;
  if (k3mag > 1.f) k3mag = 1.f;

  // Grain params (only used when !direct_texture, but cheap to compute always).
  // CW/neutral: k3mag drives grain shortening + glitch. Cloud (CCW): both are
  // held at 0 so grain length stays fixed and scatter/jitter/loops/random-
  // reverse all switch off — only the emission density (overlap below) sweeps.
  float grain_character = cloud_mode ? 0.f : k3mag;
  float glitch_amount   = cloud_mode ? 0.f : k3mag;

  // K2 timescale factor: CCW = 0.5× (shorter/faster), CW = 2× (longer/slower)
  float k2_scale = 0.5f + k2 * 1.5f;

  float gc_sq = grain_character * grain_character;

  // Grain length + emission density — one K3 axis through the noon origin
  // (GRAIN_NEUTRAL_LEN / GRAIN_NEUTRAL_OVERLAP). At k3mag=0 both branches equal
  // the origin, so crossing noon is seamless in either direction.
  //  Cloud (CCW): k3mag lengthens neutral→CLOUD_LEN_MAX; overlap held at neutral
  //    so the emission rate falls as grains grow — a long, slow smear.
  //  CW/neutral: gc_sq shortens the *block base* only to a coarse floor
  //    (GRAIN_CW_LEN_FLOOR) — the short fast-stutter grains come from per-grain
  //    variation at trigger, not from collapsing the base. Overlap thins
  //    neutral→1× as chaos rises.
  // Overlap anchor is context-dependent. Echo (K2 engaged) + SW2 MID blooms
  // (dense); everything else — including the whole K2-noon live/glitch zone —
  // stays sparse. The K3-CW side still thins the anchor toward 1× for glitch.
  float ovl_anchor = (!direct_texture && eb.sw2 == 1) ? GRAIN_OVERLAP_MID_ECHO
                                                      : GRAIN_NEUTRAL_OVERLAP;
  size_t grain_len;
  float  overlap;
  if (cloud_mode) {
    // CCW lengthens grains neutral→CLOUD_LEN_MAX (linear, felt across the whole
    // travel). Overlap held at neutral, so the emission RATE falls as grains
    // grow — a long, slow smear that leans on the deep buffer.
    float t = k3mag;
    grain_len = static_cast<size_t>(
        (GRAIN_NEUTRAL_LEN + t * (CLOUD_LEN_MAX - GRAIN_NEUTRAL_LEN)) * k2_scale);
    overlap = ovl_anchor;
  } else {
    grain_len = static_cast<size_t>(
        (GRAIN_NEUTRAL_LEN - gc_sq * (GRAIN_NEUTRAL_LEN - GRAIN_CW_LEN_FLOOR)) * k2_scale);
    overlap = ovl_anchor - glitch_amount * (ovl_anchor - 1.f);
  }
  if (grain_len < GRAIN_MIN_LEN) grain_len = GRAIN_MIN_LEN;
  size_t base_interval = static_cast<size_t>(
      static_cast<float>(grain_len) / overlap);
  if (base_interval < 32) base_interval = 32;

  // Grain window: smooth grains (cloud + neutral, glitch_amount≈0) use a full
  // Hann window so the constant-overlap sum is flat (no tremolo at unison).
  // CW character grains keep the length-based Tukey (flatter = more present).
  float grain_alpha = (glitch_amount < 0.01f) ? 1.0f : -1.0f;

  // Stutter params (direct-texture CW probabilistic path). Driven by k3mag so
  // noon = clean and the probabilistic micro-stutter lives on the CW half only.
  // Base chunk: short slice at low k3mag → long at full CW (randomized ±40%).
  float stutter_base_chunk = static_cast<float>(STUTTER_BUF_SIZE) -
      k3mag * static_cast<float>(STUTTER_BUF_SIZE - STUTTER_MIN_LOOP);
  // Event probability per sample: quadratic, erratic at full CW (~every 30 ms)
  float stutter_prob = k3mag * k3mag * (1.f / 1440.f);
  // Reverse probability: sqrt curve so reverses appear early — up to 80% at CW.
  float reverse_chance = sqrtf(k3mag) * 0.8f;
  // Cut-out probability: 0% at low k3mag, up to 40% at full CW
  float cutout_chance = k3mag * 0.4f;

  // K4: texture amount (0 = clean, CW = full effect)
  float k4 = RemapKnob(eb.knobs[3]);

  // SW1: texture mode (0=decimate/fold bipolar, 1=free, 2=ringmod)
  int texture_mode = eb.sw1;

  // Ringmod: sine carrier, keytracked LPF
  // K4 < 30% = tremolo (1–15 Hz), K4 >= 30% = bell partials (3.5× at noon)
  float ringmod_inc;
  float ringmod_lp_g = 1.f;
  if (k4 < 0.3f) {
    // Tremolo: 1–15 Hz, not pitch-tracked, no compensation needed
    float trem_freq = 1.f + (k4 / 0.3f) * 14.f;
    ringmod_inc = trem_freq / 48000.f;
  } else {
    // Bell/metallic partials — 3.5× lands at noon, inharmonic spread
    static const float RATIOS[] = {1.5f, 2.76f, 3.5f, 4.2f, 5.4f, 6.5f, 7.3f};
    int idx = static_cast<int>((k4 - 0.3f) / 0.7f * 7.f);
    if (idx > 6) idx = 6;
    float ratio = RATIOS[idx];
    float carrier_freq = MidiToFreq(tracker.GetMidiNote()) * ratio;
    ringmod_inc = carrier_freq / 48000.f;
    // Keytracked LPF: cutoff = carrier × 6 (gentle top-end rolloff,
    // only tames the extreme highs at high ratios). One-pole coefficient.
    float lp_cutoff = carrier_freq * 6.f;
    if (lp_cutoff > 20000.f) lp_cutoff = 20000.f;
    ringmod_lp_g = 1.f - expf(-2.f * 3.14159265f * lp_cutoff / 48000.f);
  }

  // Bipolar K4 helpers for case 0 (decimator/folder)
  // CCW→noon (k4 0→0.5): decimator amount, inverted so CCW = max crush
  // noon→CW (k4 0.5→1): fold amount
  float decim_amt = (k4 < 0.5f) ? (1.f - k4 / 0.5f) : 0.f;   // 1 at CCW, 0 at noon
  float fold_amt  = (k4 > 0.5f) ? ((k4 - 0.5f) / 0.5f) : 0.f; // 0 at noon, 1 at CW
  float decim_rate = 1.f + decim_amt * 47.f;  // 1 (clean at noon) to 48 (max crush at CCW)

  // Glitch (SW1 MIDDLE) — bipolar K4, event-driven. CCW = bit-flip events,
  // CW = timing events (freeze / stutter / reverse). Env-gated mix per
  // sample inside the processor. See docs/MODE_B_TEXTURE_IDEAS.md.
  float k4_centered = k4 - 0.5f;
  float glitch_magnitude = fabsf(k4_centered) * 2.f;  // 0..1
  int   glitch_side = (k4_centered < 0.f) ? 0 : 1;    // 0=CCW bit-flip, 1=CW timing
  float glitch_effect_pos = (glitch_magnitude > GLITCH_DEADZONE)
      ? (glitch_magnitude - GLITCH_DEADZONE) / (1.f - GLITCH_DEADZONE)
      : 0.f;

  // K5: bipolar — CCW = reverb dry/wet (0→1), center = off (deadzone),
  //               CW = ring-buffer feedback (0→0.95, existing behavior).
  float k5 = RemapKnob(eb.knobs[4]);
  float reverb_amt;
  float feedback_amt;
  const float dead = K5_CENTER_DEADZONE;
  if (k5 < 0.5f - dead) {
    reverb_amt = (0.5f - dead - k5) / (0.5f - dead);  // 0 at edge of deadzone, 1 at full CCW
    feedback_amt = 0.f;
  } else if (k5 > 0.5f + dead) {
    feedback_amt = ((k5 - 0.5f - dead) / (0.5f - dead)) * FEEDBACK_MAX;
    reverb_amt = 0.f;
  } else {
    reverb_amt = 0.f;
    feedback_amt = 0.f;
  }

  // Even out perceived loudness across K1.
  // SW2 UP: scale by K1's exact semitone offset.
  // SW2 MID: scale by the closest resonance interval (same scan as GrainPitchRatio).
  // SW2 DOWN: untouched.
  if (eb.sw2 == 0) {
    feedback_amt *= FixedIntervalFeedbackScale(K1ToSemi(k1, 12));
  } else if (eb.sw2 == 1) {
    int k1_semi = K1ToSemi(k1, 36);
    int closest_semi = 0;
    int min_dist = 100;
    for (int i = 0; i < NUM_RES; i++) {
      int d = RESONANCES[i] - k1_semi;
      if (d < 0) d = -d;
      if (d < min_dist) { min_dist = d; closest_semi = RESONANCES[i]; }
    }
    feedback_amt *= FixedIntervalFeedbackScale(closest_semi);
  }

  // Live-grain mode: the whole K2-noon zone (direct-texture) runs the grain
  // engine on the live ring for a near-zero-latency granular texture. K3 then
  // behaves identically to buffer mode (neutral / cloud-CCW / character-CW) —
  // K2 only sets read-back depth (live here → deep trails when engaged). HPF
  // stays off here (gated on !direct_texture below) for the full-range live feel.
  bool live_grain = direct_texture;
  // Read-back depth (how far behind the write head grains read). Buffer
  // engaged: scales with K2 range, floored at grain length (trails). Live: 0 —
  // grains follow the write head; the per-grain safety floor at trigger adds
  // only the minimum a reverse/pitch-up grain physically needs, so a
  // forward-unison grain stays ~0 latency (dry passthrough at the neutral).
  size_t base_delay;
  if (live_grain) {
    base_delay = 0;
  } else {
    base_delay = max_range / 8;
    if (base_delay < grain_len) base_delay = grain_len;
  }

  // K6: dry/wet mix
  float mix = RemapKnob(eb.knobs[5]);

  // SW2: harmony mode
  int harmony = eb.sw2;  // 0=fixed, 1/2=resonance

  // SW2 DOWN: K1 drives a Bode SSB frequency shifter on the wet bus (inside
  // the feedback loop). Buffer-read pitch is forced to unison so K1 isn't
  // doing two jobs at once.
  bool freq_shift_active = (eb.sw2 == 2);
  if (freq_shift_active) {
    float k1_norm = (k1 - 0.5f) * 2.f;  // [-1, 1]
    float abs_k = fabsf(k1_norm);
    float shift_hz = 0.f;
    if (abs_k > FREQ_SHIFT_DEADZONE) {
      float t = (abs_k - FREQ_SHIFT_DEADZONE) / (1.f - FREQ_SHIFT_DEADZONE);
      shift_hz = (expf(t * FREQ_SHIFT_CURVE) - 1.f)
               / (expf(FREQ_SHIFT_CURVE) - 1.f) * FREQ_SHIFT_MAX_HZ;
      if (k1_norm > 0.f) shift_hz = -shift_hz;  // CCW = down-shift (bass), CW = up-shift (bright)
    }
    b_shifter.SetShiftHz(shift_hz);
  }

  // Per-sample wet bus capture (used by block-based reverb pipeline below).
  float wet_block[48];

  for (size_t i = 0; i < size; i++) {
    float dry = in[0][i];

    // Envelope follower (feeds pitch tracker gating)
    grain_env = env.Process(dry);

    // Note-on detection (shared): rising edge — env jumps a factor above a slow
    // baseline, gated above the noise floor, with a refractory lock so one pluck
    // = one trigger. Drives two reactive features off the SAME detected attack:
    //   • grain engine — seeds a burst (K3-CW glitch side only; see scheduler);
    //   • SW1-MID glitch effect — forces an event (passed into GlitchEvents).
    // Baseline/refractory update every sample so state never goes stale.
    trans_slow += TRANSIENT_SLOW_COEF * (grain_env - trans_slow);
    if (trans_refractory > 0) trans_refractory--;
    bool note_on = (trans_refractory == 0 && grain_env > TRANSIENT_GATE
                    && grain_env > trans_slow * TRANSIENT_RISE);
    if (note_on) trans_refractory = TRANSIENT_REFRACTORY;
    // Grain burst only on the CW glitch side (glitch_amount > 0 ⇒ K3 past the
    // neutral pad): the CCW cloud and K3-neutral passthrough stay untriggered.
    if (note_on && glitch_amount > 0.01f) {
      grain_burst_left = TRANSIENT_BURST;
      grain_timer = 0;                 // fire on the next scheduler tick
    }

    // Feed pitch tracker
    tracker.Feed(dry, grain_env);

    // Write input + feedback into ring buffer (even in direct-texture mode,
    // so turning K2 back up reveals a buffer with textured material).
    // tanh on the feedback return tames runaway peaks at high K5 by turning
    // overshoot into soft saturation while preserving the additive character.
    // Build-up ducker: track |prev_wet| with slow attack / slow release,
    // pull feedback_amt down by THRESHOLD/env once env exceeds threshold.
    float abs_pw = fabsf(prev_wet);
    float g_env = (abs_pw > fb_duck_env) ? fb_duck_atk_g : fb_duck_rel_g;
    fb_duck_env += g_env * (abs_pw - fb_duck_env);
    float duck_gain = (fb_duck_env > FB_DUCK_THRESHOLD)
                    ? (FB_DUCK_THRESHOLD / fb_duck_env)
                    : 1.f;
    // On-play ducker: dry env opens an instant duck, releases slowly so
    // the feedback gain doesn't pulse with individual notes.
    on_play_env = (grain_env > on_play_env)
                ? grain_env
                : on_play_env + on_play_rel_g * (grain_env - on_play_env);
    float on_play_norm = (on_play_env - ON_PLAY_ENV_GATE) * ON_PLAY_ENV_SCALE;
    if (on_play_norm < 0.f) on_play_norm = 0.f;
    if (on_play_norm > 1.f) on_play_norm = 1.f;
    float on_play_gain = 1.f - on_play_norm * ON_PLAY_AMOUNT;

    // 2-pole HPF on the feedback return ONLY (always on) — blocks sub/DC from
    // accumulating in the loop. Kept off the wet output so there's no gated-
    // filter click at the K2 noon boundary and the wet keeps its full range.
    wet_hp_state[0] += (1.f - wet_hp_coeff) * (prev_wet - wet_hp_state[0]);
    float fb_hp1 = prev_wet - wet_hp_state[0];
    wet_hp_state[1] += (1.f - wet_hp_coeff) * (fb_hp1 - wet_hp_state[1]);
    float fb_hp = fb_hp1 - wet_hp_state[1];

    float fb_amt_eff = feedback_amt * duck_gain * on_play_gain;
    grain_ring.Write(dry + tanhf(fb_hp * fb_amt_eff * FB_SAT_DRIVE) / FB_SAT_DRIVE);

    float wet;

    // Stutter engine RETIRED: all of K2-noon now flows through the live grain
    // engine (see live_grain), so K3-CW is the same grain character/glitch as
    // buffer mode, just live. This block is kept dormant for one test pass and
    // will be deleted once the live CW glitch is confirmed by ear.
    if (false) {
      // (dormant) former probabilistic micro-stutter — reuses the two-voice
      // Tukey crossfade; pitch via GrainPitchRatio, SW2 DOWN unison.

      // Write to capture buffer. Frozen only during CW repeat events.
      if (!stutter_engaged || stutter_is_cutout) {
        stutter_buf[stutter_write_pos] = dry;
        stutter_write_pos++;
        if (stutter_write_pos >= STUTTER_BUF_SIZE) stutter_write_pos = 0;
        if (stutter_buf_filled < STUTTER_BUF_SIZE) stutter_buf_filled++;
        stutter_fresh++;
      }

      // --- Probability-based event trigger ---
      // Require enough fresh audio to fill the chunk — prevents stale/fresh
      // seam in the capture region when events fire in quick succession.
      size_t min_fresh = static_cast<size_t>(stutter_base_chunk * 1.4f);  // worst-case chunk
      if (min_fresh > STUTTER_BUF_SIZE) min_fresh = STUTTER_BUF_SIZE;
      bool any_active = stutter_voices[0].active || stutter_voices[1].active;
      if (k3mag > 0.01f && stutter_buf_filled >= STUTTER_BUF_SIZE
          && !stutter_engaged && !any_active && !stutter_is_cutout
          && stutter_fresh >= min_fresh
          && RandFloat() < stutter_prob) {

        if (RandFloat() < cutout_chance) {
          // Cut-out: dedicated cosine envelope, no voice triggered
          stutter_is_cutout = true;
          stutter_cutout_len = 480 + static_cast<size_t>(RandFloat() * 1920.f);
          stutter_cutout_phase = 0;
          stutter_engaged = true;
        } else {
          // Repeat: randomize chunk length ±40%
          stutter_is_cutout = false;
          float rand_scale = 0.6f + RandFloat() * 0.8f;
          size_t chunk = static_cast<size_t>(stutter_base_chunk * rand_scale);
          if (chunk > STUTTER_BUF_SIZE) chunk = STUTTER_BUF_SIZE;
          if (chunk < STUTTER_MIN_LOOP) chunk = STUTTER_MIN_LOOP;
          stutter_snap_len = chunk;
          stutter_snap_start = (stutter_write_pos + STUTTER_BUF_SIZE - stutter_snap_len)
                               % STUTTER_BUF_SIZE;
          float pitch = freq_shift_active ? 1.f : GrainPitchRatio(harmony, k1);
          stutter_snap_rate = (RandFloat() < reverse_chance) ? -pitch : pitch;
          stutter_reps_left = 1 + static_cast<int>(RandFloat() * (1.f + k3mag * 4.f));

          stutter_active_idx = 0;
          stutter_voices[0].Trigger(stutter_snap_start, stutter_snap_len, stutter_snap_rate);
          stutter_reps_left--;
          stutter_next_armed = true;
          stutter_engaged = true;
        }
        stutter_fresh = 0;  // reset fresh counter on any event start
      }

      if (stutter_is_cutout && stutter_engaged) {
        // --- Cut-out: cosine envelope ducks dry, no voices involved ---
        size_t taper = StutterVoice::MAX_TAPER;
        if (taper > stutter_cutout_len / 2) taper = stutter_cutout_len / 2;

        float duck;
        if (stutter_cutout_phase < taper) {
          // Fade out dry
          float t = static_cast<float>(stutter_cutout_phase) / static_cast<float>(taper);
          duck = 0.5f * (1.f - cosf(3.14159265f * t));
        } else if (stutter_cutout_phase >= stutter_cutout_len - taper) {
          // Fade in dry
          float t = static_cast<float>(stutter_cutout_len - 1 - stutter_cutout_phase)
                    / static_cast<float>(taper);
          duck = 0.5f * (1.f - cosf(3.14159265f * t));
        } else {
          duck = 1.f;  // full silence
        }

        wet = dry * (1.f - duck);

        stutter_cutout_phase++;
        if (stutter_cutout_phase >= stutter_cutout_len) {
          stutter_is_cutout = false;
          stutter_engaged = false;
        }
      } else {
        // --- Repeat path: two-voice Tukey crossfade ---

        // Overlap trigger: fire next voice during current voice's tail taper
        if (stutter_engaged) {
          StutterVoice& cur = stutter_voices[stutter_active_idx];
          int next_idx = 1 - stutter_active_idx;
          StutterVoice& nxt = stutter_voices[next_idx];

          if (stutter_next_armed && cur.active
              && cur.phase >= cur.length - cur.taper && !nxt.active) {
            if (stutter_reps_left > 0) {
              nxt.Trigger(stutter_snap_start, stutter_snap_len, stutter_snap_rate);
              stutter_active_idx = next_idx;
              stutter_reps_left--;
              stutter_next_armed = true;
            } else {
              stutter_next_armed = false;
            }
          }
        }

        // Process both voices
        float v0 = stutter_voices[0].Process(stutter_buf, STUTTER_BUF_SIZE);
        float v1 = stutter_voices[1].Process(stutter_buf, STUTTER_BUF_SIZE);

        // Combined window for complement crossfade (clamped to 1.0)
        float w = stutter_voices[0].window + stutter_voices[1].window;
        if (w > 1.f) w = 1.f;

        // Event ends when all voices finish
        if (stutter_engaged && !stutter_voices[0].active && !stutter_voices[1].active) {
          stutter_engaged = false;
        }

        // Complement crossfade
        wet = dry * (1.f - w) + v0 + v1;
      }

    } else {

      // Grain scheduler — runs for buffer mode AND live-grain mode (K2 noon +
      // K3 CCW). Continuous stream; K3-CW adds chaos, K3-CCW is the clean cloud.
      grain_timer--;
      if (grain_timer <= 0) {
        // Delay: base offset + scatter within K5 range. Burst grains (fired by a
        // note-on) anchor to delay 0 instead — the overrun safety below floors
        // that to the physical minimum, so they read the FRESHEST content (the
        // note just played) regardless of the K5 read-back depth.
        bool burst = (grain_burst_left > 0);
        size_t delay;
        if (burst) {
          delay = 0;
        } else {
          size_t scatter_range = max_range - base_delay;
          size_t pos_offset = static_cast<size_t>(RandFloat() * glitch_amount * static_cast<float>(scatter_range));
          delay = base_delay + pos_offset;
          if (delay > max_range) delay = max_range;
        }

        // Direction: K2 sign sets the base (CW forward / CCW backward). K3
        // character adds occasional flips against that base — so a forward K2
        // stream reverses occasionally, a backward K2 stream plays forward
        // occasionally. At K3=0 the stream is purely K2's direction.
        bool flip = (glitch_amount > 0.1f) && (RandFloat() < glitch_amount * GRAIN_REVERSE_BIAS);
        bool reverse = buf_reverse ? !flip : flip;

        // Pitch re-roll. UP holds a fixed interval (only changes when K1
        // moves — deterministic, so no per-grain re-roll). MID/DOWN roll a new
        // random pick from the ±1 RESONANCES window every `reroll` grains:
        // change every grain (interval 1) across neutral + the CW glitch half,
        // then HELD progressively longer down the CCW cloud (up to
        // GRAIN_PITCH_HOLD_MAX) so the slow smear settles onto stable pitches.
        // K1-range for change detection uses the mode's effective span so UP
        // doesn't spuriously re-roll when K1 clamps above +12.
        int k1_semi_now = (harmony == 0) ? K1ToSemi(k1, 12) : K1ToSemi(k1, 36);
        bool k1_changed = (k1_semi_now != harmony_cached_k1_semi);
        // Baseline change-every-grain; CCW ramps the hold interval 1 →
        // GRAIN_PITCH_HOLD_MAX with k3mag (longer holds the further out you go).
        int reroll_interval = cloud_mode
            ? 1 + static_cast<int>(k3mag * static_cast<float>(GRAIN_PITCH_HOLD_MAX - 1))
            : 1;
        if (k1_changed || (harmony != 0 && harmony_hold_counter <= 0)) {
          harmony_cached_ratio = GrainPitchRatio(harmony, k1);
          harmony_cached_k1_semi = k1_semi_now;
          harmony_hold_counter = (harmony == 0) ? 1 : reroll_interval;
        }
        if (harmony != 0 && harmony_hold_counter > 0) harmony_hold_counter--;
        float pitch_ratio = harmony_cached_ratio;
        if (freq_shift_active) pitch_ratio = 1.f;  // SW2 DOWN: buffer pitch held at unison
        float comp = 1.f / sqrtf(pitch_ratio);

        // Per-grain length variation → randomized stutter frequency. Scatter
        // THIS grain below the coarse block base by a skewed factor whose depth
        // grows with glitch_amount: most grains stay near base, but occasionally
        // one is dramatically short (a fast CD-hang). Repeats are COUPLED to the
        // result — repeats ≈ base_len / this_len — so a short grain gets many
        // reps (sustained fast hang) and a long grain gets one; the footprint
        // stays ≈ one block base length while the rate varies grain-to-grain.
        // Geometric shortening toward an ABSOLUTE audio-rate length (LEN_MIN),
        // not a fraction of base — that is what turns a slow CD-hang into a
        // pitched buzz (brrr→friii→kriii) as the repeat cycle climbs past ~20 Hz.
        // t is skewed toward 0 (most grains near base) and its reach grows with
        // glitch_amount, so the buzzes get higher/more frequent as K3 opens.
        float len_var = powf(RandFloat(), GRAIN_LEN_VAR_SKEW);   // 0..1, mostly small
        // Reach curve: glitch_amount^gamma keeps mid-CW percussive and bends into
        // audio-rate only near full CW (gamma>1). DEPTH scales the full extent.
        float reach = powf(glitch_amount, GRAIN_STUTTER_REACH_GAMMA);
        float t = reach * GRAIN_LEN_VAR_DEPTH * len_var;         // 0..1 shorten strength
        float base_f = static_cast<float>(grain_len);
        float min_f = GRAIN_STUTTER_LEN_MIN < base_f ? GRAIN_STUTTER_LEN_MIN : base_f;
        size_t this_len = static_cast<size_t>(base_f * powf(min_f / base_f, t));
        if (this_len < static_cast<size_t>(GRAIN_STUTTER_LEN_MIN))
            this_len = static_cast<size_t>(GRAIN_STUTTER_LEN_MIN);
        // Repeats fill ≈ one block-base footprint: short grain → many reps
        // (sustained buzz), long grain → one. Rate varies grain-to-grain.
        int loops = static_cast<int>(base_f / static_cast<float>(this_len) + 0.5f);
        if (loops < 1) loops = 1;
        if (loops > GRAIN_STUTTER_MAX_LOOPS) loops = GRAIN_STUTTER_MAX_LOOPS;

        // Read-overrun safety (ALL grains). A forward grain consumes
        // rate·grain_len source samples; a reverse grain starts a full
        // grain_len ahead of its delay point. Either can read PAST the moving
        // write head into stale 8 s-old ring content unless it starts far
        // enough back: forward pitch-up needs delay ≥ grain_len·(ratio−1),
        // reverse needs ≥ grain_len. Egregious in SW2 MID (RESONANCES reach
        // +36 semi = 8× rate). Applied after the max_range clamp, so a
        // pitched-up grain reaches into valid recent history rather than
        // wrapping. Forward+unison/down needs ~0, so live stays a passthrough.
        {
          size_t safety = reverse
              ? (this_len + 64)
              : (pitch_ratio > 1.f
                    ? static_cast<size_t>(this_len * (pitch_ratio - 1.f)) + 64
                    : 64);
          if (delay < safety) delay = safety;
        }

        // Find an inactive voice — never steal mid-playback
        int voice = -1;
        for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
          int idx = (grain_next_voice + v) % NUM_GRAIN_VOICES;
          if (!grain_voices[idx].IsActive()) {
            voice = idx;
            grain_next_voice = (idx + 1) % NUM_GRAIN_VOICES;
            break;
          }
        }
        if (voice >= 0) {
          grain_voices[voice].Trigger(
              grain_ring, delay, this_len, reverse, pitch_ratio, comp, loops, grain_alpha);
        }

        if (burst) {
          // Space the burst grains closely, then resume the metronomic stream.
          grain_burst_left--;
          grain_timer = TRANSIENT_BURST_SPACING;
        } else {
          float jitter = (RandFloat() * 2.f - 1.f) * glitch_amount * 0.8f;
          grain_timer = static_cast<int>(
              static_cast<float>(base_interval) * (1.f + jitter));
          if (grain_timer < 32) grain_timer = 32;
        }
      }

      // Sum all active voices
      wet = 0.f;
      for (int v = 0; v < NUM_GRAIN_VOICES; v++) {
        wet += grain_voices[v].Process(grain_ring);
      }
    }

    // Texture shaper — K4 meaning depends on SW1 mode
    switch (texture_mode) {
    case 0: {
      // Bipolar: CCW = decimator, noon = clean, CW = wavefolder
      if (decim_amt > 0.01f) {
        decim_count += 1.f;
        if (decim_count >= decim_rate) {
          decim_count -= decim_rate;
          decim_hold = wet;
        }
        wet = wet * (1.f - decim_amt) + decim_hold * decim_amt;
      }
      if (fold_amt > 0.01f) {
        float driven = wet * (1.f + fold_amt * 30.f);
        float folded = sinf(driven * 1.5707963f) * 0.15f;
        wet = wet * (1.f - fold_amt) + folded * fold_amt;
      }
      break;
    }
    case 1: {
      // Event-driven digital glitch: stochastic triggers, K4 alone controls density.
      wet = glitch_events.Process(wet, glitch_side, glitch_effect_pos, grain_env, note_on);
      break;
    }
    case 2: {
      // Ringmod: sine carrier, keytracked LPF
      float carrier = sinf(2.f * 3.14159265f * ringmod_phase);
      ringmod_phase += ringmod_inc;
      if (ringmod_phase >= 1.f) ringmod_phase -= 1.f;
      float rm;
      if (k4 < 0.3f) {
        // Tremolo region: AM (50:50 clean/modulated)
        rm = wet * (0.5f + 0.5f * carrier);
      } else {
        // Bell region: true ringmod
        rm = wet * carrier;
      }
      // Keytracked one-pole LPF to tame highs
      ringmod_lp_state += ringmod_lp_g * (rm - ringmod_lp_state);
      wet = ringmod_lp_state;
      break;
    }
    }

    // SW2 DOWN: Bode SSB frequency shifter, applied inside the feedback loop
    // (output feeds prev_wet → cascading shift each pass). HPF below cleans
    // any sub-audio energy the shifter introduces near unison.
    if (freq_shift_active) {
      wet = b_shifter.Process(wet);
    }

    // Wet output is full-range now — the HPF moved to the feedback return only
    // (see the injection above), so there's no gated-filter click at the K2
    // noon boundary and the live/wet signal keeps its lows.

    // Store wet for next sample's feedback injection (pre-reverb, so reverb
    // does not feed the ring buffer).
    prev_wet = wet;

    // Capture mono wet for the block-based reverb pipeline.
    wet_block[i] = wet;
  }

  // --- Reverb pipeline (block-based, runs at 32 kHz internally) ---
  // Always run so the reverb tail doesn't snap off when K5 leaves CCW;
  // contribution is gated by reverb_amt at the mix point.
  float mid_block[32];
  const size_t n_mid = rev_downsampler.Process(wet_block, size, mid_block);

  clouds::FloatFrame rev_frames[32];
  for (size_t i = 0; i < n_mid; ++i) {
    rev_frames[i].l = mid_block[i];
    rev_frames[i].r = mid_block[i];  // mono input, fed equally to L/R
  }
  reverb_instance.Process(rev_frames, n_mid);
  // With amount=1, rev_frames[i].l/.r now hold pure reverb wet (decorrelated).

  float mid_l[32], mid_r[32];
  for (size_t i = 0; i < n_mid; ++i) {
    mid_l[i] = rev_frames[i].l;
    mid_r[i] = rev_frames[i].r;
  }

  float wet_l_block[48], wet_r_block[48];
  rev_upsampler_l.Process(mid_l, n_mid, wet_l_block);
  rev_upsampler_r.Process(mid_r, n_mid, wet_r_block);

  // --- Final mix (single mono-collapse point, easy to remove for stereo) ---
  const float m        = MixCurve(mix);
  const float dry_gain = sqrtf(1.f - m);
  const float wet_gain = sqrtf(m);
  for (size_t i = 0; i < size; i++) {
    // Per-sample smoothing on reverb_amt to kill zipper across block boundaries.
    reverb_amt_smooth += REVERB_AMT_SMOOTH_COEF * (reverb_amt - reverb_amt_smooth);
    const float ra = reverb_amt_smooth;
    const float inv_ra = 1.f - ra;
    const float dry = in[0][i];
    const float wet_l = wet_block[i] * inv_ra + wet_l_block[i] * ra;
    const float wet_r = wet_block[i] * inv_ra + wet_r_block[i] * ra;
    const float wet_mono = (wet_l + wet_r) * 0.5f;  // remove for stereo
    // Smooth the K6 gains per sample → no zipper when the mix knob moves.
    out[0][i] = out[1][i] = dry * sdg_b(dry_gain) + wet_mono * swg_b(wet_gain);
  }
  }

 private:
  NT3Shared*  sh_ = nullptr;
  NT3Controls eb_ = {};

  // ---- verbatim from pedals/nitrotron3/main.cpp:60-78 ----
RingBuffer   grain_ring;
FreqShifter  b_shifter;   // Mode B SW2 DOWN — Bode SSB frequency shifter

static constexpr int NUM_GRAIN_VOICES = 8;
GrainVoice   grain_voices[NUM_GRAIN_VOICES];
int          grain_next_voice = 0;
int          grain_timer = 0;        // samples until next event
int          grain_burst_left = 0;   // grains remaining in current burst
float        grain_env = 0.f;        // envelope value for grain amplitude
float        trans_slow = 0.f;       // slow envelope baseline for note-on detection
int          trans_refractory = 0;   // samples until another attack can trigger
int          harmony_hold_counter = 0;  // grains remaining before re-rolling pitch
float        harmony_cached_ratio = 1.f; // cached pitch ratio for held harmony
int          harmony_cached_k1_semi = 999; // cached K1 target; force re-roll on change

static constexpr size_t GRAIN_MIN_RANGE  = 4800;   // min read range: 100 ms

// Feedback state
float prev_wet = 0.f;         // previous sample's wet output for feedback injection

  // ---- verbatim from pedals/nitrotron3/main.cpp:84-85 ----
size_t stutter_write_pos = 0;
size_t stutter_buf_filled = 0;

  // ---- verbatim from pedals/nitrotron3/main.cpp:152-216 ----
StutterVoice stutter_voices[2];
int  stutter_active_idx = 0;       // which voice is "current"
bool stutter_engaged = false;      // true while a stutter event is playing
bool stutter_next_armed = false;   // false once midpoint trigger has fired
int  stutter_reps_left = 0;        // remaining reps in current event
size_t stutter_snap_start = 0;     // latched start position
size_t stutter_snap_len = 0;       // latched chunk length
float  stutter_snap_rate = 1.f;    // latched signed playback rate (pitch × dir)
bool   stutter_is_cutout = false;  // true = silence event (rhythmic gating)
size_t stutter_cutout_phase = 0;   // current sample within cut-out
size_t stutter_cutout_len = 0;     // total cut-out duration in samples
size_t stutter_fresh = 0;          // samples written since last event ended

// Texture shaper state
float decim_hold = 0.f;       // decimator sample-and-hold value
float decim_count = 0.f;      // decimator sample counter
float ringmod_phase = 0.f;    // ringmod carrier oscillator phase
float ringmod_lp_state = 0.f; // one-pole LPF after ringmod

// Wet HPF: 2-pole high-pass keeping the wet bus above the dry instrument's
// low range — cutoff WET_HPF_FREQ in constants.h (instrument-profiled).

// Feedback saturation drive: pre-multiplies the tanh input so distortion
// kicks in earlier and the loop self-limits at lower volume.
static constexpr float FB_SAT_DRIVE = 8.f;
float wet_hp_state[2] = {};
float wet_hp_coeff = 0.f;  // computed in Init

// Feedback build-up ducker: one-pole envelope on prev_wet drives a soft
// 1:∞ attenuation of feedback_amt when the loop level rises above
// THRESHOLD. Slow attack passes transients; slow release lets the loop
// simmer down between gestures instead of re-igniting instantly.
static constexpr float FB_DUCK_THRESHOLD  = 0.20f;  // average wet level above which ducking starts
static constexpr float FB_DUCK_ATTACK_MS  = 500.f;
static constexpr float FB_DUCK_RELEASE_MS = 800.f;
float fb_duck_env = 0.f;
float fb_duck_atk_g = 0.f;  // computed in Init
float fb_duck_rel_g = 0.f;

// On-play ducker: dry-input envelope (reuse grain_env, ×10 normalized per
// passive-bass scale) reduces feedback_amt while playing. Instant attack,
// slow release so the duck doesn't pulse between notes. AMOUNT caps the
// reduction so feedback isn't completely killed when playing hard.
static constexpr float ON_PLAY_RELEASE_MS = 400.f;
static constexpr float ON_PLAY_ENV_GATE   = 0.02f;
static constexpr float ON_PLAY_ENV_SCALE  = 10.f;
static constexpr float ON_PLAY_AMOUNT     = 0.5f;  // max GR while playing
float on_play_env = 0.f;
float on_play_rel_g = 0.f;  // computed in Init

// Reverb sample-rate conversion. Clouds reverb runs internally at 32 kHz.
// 48->32 downsampler is mono (one shared input). 32->48 upsampler is per-
// channel so the reverb's L/R decorrelation survives end-to-end.
Resampler<2, 3, 16> rev_downsampler;
Resampler<3, 2, 16> rev_upsampler_l;
Resampler<3, 2, 16> rev_upsampler_r;
static constexpr float RESAMPLER_CUTOFF_HZ = 15000.f;
static constexpr float RESAMPLER_PROTO_FS_HZ = 96000.f;

clouds::Reverb reverb_instance;

// Smoothed K5 reverb amount — kills zipper noise across block boundaries.
float reverb_amt_smooth = 0.f;

  // ---- from pedals/nitrotron3/main.cpp:233 ----
Smoother sdg_b, swg_b;                    // B: K6 dry/wet gains

  // ---- verbatim from pedals/nitrotron3/main.cpp:242-243 ----
// Mode B SW1 MIDDLE: event-driven digital glitch processor (stateful)
GlitchEvents glitch_events;
};
