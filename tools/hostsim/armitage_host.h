#pragma once
//
// Includes the UNMODIFIED nitrolab armitage module for the host simulator,
// with its constants as runtime tunables (host_tunables.h).
//
// Include order matters:
//  1. host_tunables.h defines ARM_TUNABLE (runtime variable) before the
//     constants file sees its own firmware fallback.
//  2. Everything armitage.h needs (std headers, stubs, shell headers, the
//     constants) is included first, untouched.
//  3. armitage.h (and armitage_bands.h, which it includes) is then parsed with
//     `constexpr` read as `const` and `static_assert` dropped: a handful of
//     expressions there are constexpr over tunables (DeltaGain(), kDenFloor,
//     the soft clipper's kIn / kOut, the SinCos static_assert). Integral
//     `const` with constant initialisers (kMask, kBands, kMM, ...) stay
//     constant expressions, so the arrays keep their sizes.
//  4. The dropped static_assert on tunables is checked at runtime by
//     ValidateTunables() (call it after applying --set).

#include "host_tunables.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "daisy.h"              // tools/host/stub
#include "hothouse.h"           // tools/host/stub
#include "control_surface.h"    // tools/host/stub
#include "constants.h"          // pedals/nitrolab
#include "knob_map.h"           // src/core/util
#include "module.h"             // pedals/nitrolab
#include "modules/armitage_constants.h"

#define constexpr const
#define static_assert(...)
#include "modules/armitage.h"
#undef static_assert
#undef constexpr

// Runtime form of the static_asserts in armitage.h that read tunables.
// Returns an error message, or empty when every constraint holds.
inline std::string ValidateTunables() {
  using namespace armitage_k;
  if (!(ARM_BETA_CLAMP + ARM_XMOD_MAX < 90.f))
    return "ARM_BETA_CLAMP + ARM_XMOD_MAX must be < 90 (SinCos offset covers |theta| < ~100 rad)";
  if (ARM_ADD_MID < 1 || ARM_ADD_MID > ARM_ADD_MMAX)
    return "ARM_ADD_MID must be in [1, ARM_ADD_MMAX] (ARM_ADD_MMAX is fixed: array size)";
  return "";
}
