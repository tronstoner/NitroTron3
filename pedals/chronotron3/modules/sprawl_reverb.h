#pragma once
//
// sprawl — reverb pipeline. Thin binding of sprawl's tuning onto the shared
// core block `clouds_reverb_48k.h` (48<->32 kHz resampling + Clouds FDN +
// smoothed blend), which was extracted from THIS file when mnemonic became its
// second user. The arithmetic is unchanged, so the sound is unchanged.
//
// K5-CCW sweeps the blend amount only; the decay time is fixed at REVERB_TIME.
// (mnemonic sweeps decay as well — see MNEM_REVERB_TIME_MIN/MAX. If that turns
// out to be the better feel, it ports here by calling SetTime() per block.)
//
#include "sprawl_constants.h"
#include "constants.h"            // pedals/chronotron3 — CT3_BLOCK_SIZE
#include "clouds_reverb_48k.h"    // core/blocks
#include <cstddef>
#include <cstdint>

class SprawlReverb {
 public:
  // `slab` = externally allocated SDRAM storage (uint16_t[16384]).
  void Init(uint16_t* slab) {
    rev_.Init(slab, RESAMPLER_CUTOFF_HZ, RESAMPLER_PROTO_FS_HZ,
              REVERB_INPUT_GAIN, REVERB_TIME, REVERB_AMT_SMOOTH_COEF);
  }

  // Always run so the reverb tail doesn't snap off when K5 leaves CCW;
  // contribution is gated by reverb_amt at the mix point.
  void ProcessBlock(const float* wet_block, size_t size, float reverb_amt,
                    float* out_wet) {
    rev_.ProcessBlock(wet_block, size, reverb_amt, out_wet);
  }

 private:
  CloudsReverb48k<CT3_BLOCK_SIZE> rev_;
};
