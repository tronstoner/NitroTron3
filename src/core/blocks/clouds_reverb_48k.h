#pragma once
//
// core/blocks/clouds_reverb_48k.h
//
// 48 kHz block wrapper around the vendored MI Clouds reverb, which runs
// internally at 32 kHz:
//
//   48->32 downsample (mono, one shared input)
//     -> clouds::Reverb (amount = 1, so the frames come back PURE wet)
//       -> 32->48 upsample, PER CHANNEL so the reverb's L/R decorrelation
//          survives end to end
//         -> per-sample smoothed dry/wet blend + mono collapse
//
// Pedal-agnostic: every tuning value is passed to Init(), nothing is read from
// a pedal `constants.h`. Storage is external (the caller owns the SDRAM slab),
// as with the other core blocks.
//
// Extracted from ChronoTron3's sprawl module on its second use (mnemonic's
// bipolar K5). The arithmetic and its order are unchanged from that original,
// so sprawl sounds identical through it.
//
// kMaxBlock = the largest audio block this instance will ever be handed.
//
#include "resampler.h"
#include "clouds/reverb.h"
#include <cstddef>
#include <cstdint>

template <int kMaxBlock>
class CloudsReverb48k {
 public:
  // Mid-rate (32 kHz) frames for one block: ceil(kMaxBlock * 2/3).
  static constexpr size_t kMaxMid = (kMaxBlock * 2 + 2) / 3;

  // `slab` = externally allocated uint16_t[16384] (SDRAM). `amt_smooth` is the
  // one-pole coefficient for the wet-amount ramp, applied PER SAMPLE.
  void Init(uint16_t* slab, float resampler_cutoff_hz, float resampler_proto_fs_hz,
            float input_gain, float time, float amt_smooth) {
    down_.Init(resampler_cutoff_hz, resampler_proto_fs_hz);
    up_l_.Init(resampler_cutoff_hz, resampler_proto_fs_hz);
    up_r_.Init(resampler_cutoff_hz, resampler_proto_fs_hz);
    reverb_.Init(slab);
    reverb_.set_amount(1.0f);          // pure wet; the caller crossfades
    reverb_.set_input_gain(input_gain);
    reverb_.set_time(time);
    amt_smooth_ = amt_smooth;
    // diffusion (0.625) and lp (0.7) keep Reverb::Init()'s defaults
  }

  // Decay time (Clouds `krt`). Cheap — it is only read at the top of Process —
  // so it is safe to call per block for a swept decay.
  void SetTime(float t) { reverb_.set_time(t); }
  void SetInputGain(float g) { reverb_.set_input_gain(g); }

  // Blend `amt` (0..1) of reverb into `in`, writing `size` samples to `out`.
  // Run it unconditionally, even at amt = 0, so the tail never snaps off when
  // the knob leaves the reverb zone.
  // `out` MAY alias `in`: the input is fully consumed by the downsampler before
  // the blend loop, and the blend itself is index-local.
  void ProcessBlock(const float* in, size_t size, float amt, float* out) {
    float mid[kMaxMid];
    const size_t n_mid = down_.Process(in, size, mid);

    clouds::FloatFrame frames[kMaxMid];
    for (size_t i = 0; i < n_mid; ++i) {
      frames[i].l = mid[i];
      frames[i].r = mid[i];            // mono input, fed equally to L/R
    }
    reverb_.Process(frames, n_mid);
    // With amount = 1, frames[i].l/.r now hold pure reverb wet (decorrelated).

    float mid_l[kMaxMid], mid_r[kMaxMid];
    for (size_t i = 0; i < n_mid; ++i) {
      mid_l[i] = frames[i].l;
      mid_r[i] = frames[i].r;
    }

    float wet_l_block[kMaxBlock], wet_r_block[kMaxBlock];
    up_l_.Process(mid_l, n_mid, wet_l_block);
    up_r_.Process(mid_r, n_mid, wet_r_block);

    // --- Blend (single mono-collapse point, easy to remove for stereo) ---
    for (size_t i = 0; i < size; i++) {
      // Per-sample smoothing on the amount to kill zipper across block edges.
      amt_smoothed_ += amt_smooth_ * (amt - amt_smoothed_);
      const float ra = amt_smoothed_;
      const float inv_ra = 1.f - ra;
      const float wet_l = in[i] * inv_ra + wet_l_block[i] * ra;
      const float wet_r = in[i] * inv_ra + wet_r_block[i] * ra;
      out[i] = (wet_l + wet_r) * 0.5f;  // remove for stereo
    }
  }

 private:
  // Clouds runs at 32 kHz. The 48->32 downsampler is mono (one shared input);
  // the 32->48 upsamplers are per channel so the decorrelation survives.
  Resampler<2, 3, 16> down_;
  Resampler<3, 2, 16> up_l_;
  Resampler<3, 2, 16> up_r_;
  clouds::Reverb reverb_;
  float amt_smooth_   = 0.002f;
  float amt_smoothed_ = 0.f;
};
