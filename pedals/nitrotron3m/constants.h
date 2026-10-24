#pragma once
//
// NitroTron3m — bundle constants.
//
// All NitroTron3 tuning comes from the shipping pedal's own constants file, so
// there is ONE source of truth: the carved modules, the shared blocks (which
// `#include "constants.h"` and resolve to this file through -Ipedals/nitrotron3m)
// and the preset system read exactly the values pedals/nitrotron3 ships with,
// including the INSTRUMENT=guitar profile. Only shell-wide values of this
// prototype live below.

#include "../nitrotron3/constants.h"

// Audio (same as the shipping pedal: main.cpp hw.SetAudioBlockSize(48), 48 kHz).
static constexpr int   NT3M_BLOCK_SIZE = 48;

// Mode slots (SW3 / the recalled preset's mode field): 0 = A, 1 = B, 2 = C.
enum Nt3mMode {
  NT3M_MODE_A     = 0,   // bordun
  NT3M_MODE_B     = 1,   // sprawl, or armitage (NT3M_SLOT_B=armitage)
  NT3M_MODE_C     = 2,   // schism
  NT3M_MODE_COUNT = 3
};

// K6 dry/wet smoothing for modules that do NOT own their output (armitage):
// the NitroLab shell's law (pedals/nitrolab/constants.h NL_MIX_SMOOTH).
static constexpr float NT3M_MIX_SMOOTH = 0.002f;

// CPU clock: 480 MHz on silicon revision V (the NitroLab / ChronoTron3 boost,
// gated on the chip revision at boot); false = 400 MHz like shipping NitroTron3.
constexpr bool NT3M_CPU_BOOST = true;

// Diagnostics build: `make PEDAL=nitrotron3m DIAG=1` (the root Makefile defines
// CT3_DIAG_BUILD for every pedal). DIAG adds a USB-serial CPU heartbeat every
// NT3M_DIAG_HB_MS; the default build has none of it (dead-code eliminated).
#ifdef CT3_DIAG_BUILD
constexpr bool NT3M_DIAG = true;
#else
constexpr bool NT3M_DIAG = false;
#endif
static constexpr uint32_t NT3M_DIAG_HB_MS = 2000;

// armitage (pedals/nitrolab/modules/armitage.h, included directly) gates its
// "note this setting" lines on NL_DIAG; their printer lives in the NitroLab
// shell, so they stay off here in every build.
constexpr bool NL_DIAG = false;
