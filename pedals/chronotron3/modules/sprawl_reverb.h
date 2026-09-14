#pragma once
//
// sprawl — reverb pipeline. 1:1 port of the block-based reverb stage at the end
// of NitroTron3's ProcessGranular(): 48→32 kHz downsample, Clouds reverb
// (amount = 1 → pure wet, crossfaded externally by K5-CCW), 32→48 kHz per-
// channel upsample, then the per-sample smoothed blend + mono collapse.
//
#include "sprawl_constants.h"
#include "constants.h"        // pedals/chronotron3 — CT3_BLOCK_SIZE
#include "resampler.h"        // core/blocks
#include "clouds/reverb.h"    // core/blocks (vendored MI Clouds)
#include <cstddef>
#include <cstdint>

// Mid-rate (32 kHz) frame count for one 48-sample block: ceil(48 · 2/3) = 32.
static constexpr size_t SPRAWL_REV_MID_MAX = (CT3_BLOCK_SIZE * 2 + 2) / 3;

class SprawlReverb {
 public:
  // `slab` = externally allocated SDRAM storage (uint16_t[16384]).
  void Init(uint16_t* slab) {
    rev_downsampler_.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
    rev_upsampler_l_.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
    rev_upsampler_r_.Init(RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ);
    reverb_instance_.Init(slab);
    reverb_instance_.set_amount(1.0f);  // pure wet; we crossfade externally via K5
    reverb_instance_.set_input_gain(REVERB_INPUT_GAIN);
    reverb_instance_.set_time(REVERB_TIME);
    // diffusion (0.625) and lp (0.7) set by Reverb::Init() defaults
  }

  // Always run so the reverb tail doesn't snap off when K5 leaves CCW;
  // contribution is gated by reverb_amt at the mix point.
  void ProcessBlock(const float* wet_block, size_t size, float reverb_amt,
                    float* out_wet) {
    float mid_block[SPRAWL_REV_MID_MAX];
    const size_t n_mid = rev_downsampler_.Process(wet_block, size, mid_block);

    clouds::FloatFrame rev_frames[SPRAWL_REV_MID_MAX];
    for (size_t i = 0; i < n_mid; ++i) {
      rev_frames[i].l = mid_block[i];
      rev_frames[i].r = mid_block[i];  // mono input, fed equally to L/R
    }
    reverb_instance_.Process(rev_frames, n_mid);
    // With amount=1, rev_frames[i].l/.r now hold pure reverb wet (decorrelated).

    float mid_l[SPRAWL_REV_MID_MAX], mid_r[SPRAWL_REV_MID_MAX];
    for (size_t i = 0; i < n_mid; ++i) {
      mid_l[i] = rev_frames[i].l;
      mid_r[i] = rev_frames[i].r;
    }

    float wet_l_block[CT3_BLOCK_SIZE], wet_r_block[CT3_BLOCK_SIZE];
    rev_upsampler_l_.Process(mid_l, n_mid, wet_l_block);
    rev_upsampler_r_.Process(mid_r, n_mid, wet_r_block);

    // --- Blend (single mono-collapse point, easy to remove for stereo) ---
    for (size_t i = 0; i < size; i++) {
      // Per-sample smoothing on reverb_amt to kill zipper across block boundaries.
      reverb_amt_smooth_ += REVERB_AMT_SMOOTH_COEF * (reverb_amt - reverb_amt_smooth_);
      const float ra = reverb_amt_smooth_;
      const float inv_ra = 1.f - ra;
      const float wet_l = wet_block[i] * inv_ra + wet_l_block[i] * ra;
      const float wet_r = wet_block[i] * inv_ra + wet_r_block[i] * ra;
      out_wet[i] = (wet_l + wet_r) * 0.5f;  // remove for stereo
    }
  }

 private:
  // Reverb sample-rate conversion. Clouds reverb runs internally at 32 kHz.
  // 48->32 downsampler is mono (one shared input). 32->48 upsampler is per-
  // channel so the reverb's L/R decorrelation survives end-to-end.
  Resampler<2, 3, 16> rev_downsampler_;
  Resampler<3, 2, 16> rev_upsampler_l_;
  Resampler<3, 2, 16> rev_upsampler_r_;
  clouds::Reverb reverb_instance_;
  // Smoothed K5 reverb amount — kills zipper noise across block boundaries.
  float reverb_amt_smooth_ = 0.f;
};
