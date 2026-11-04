#pragma once
//
// core/io/diag_link.h
//
// DIAG builds only: answer a `?` sent over the USB serial port with one line
//   BUILD <pedal> <git hash[-dirty]> DIAG <build time>
// so the host logger (tools/diag/log.py, `task log`) can tell which firmware
// is talking. The Makefile defines DIAG_GIT_HASH / DIAG_BUILD_TIME only when
// DIAG=1 (together with CT3_DIAG_BUILD).
//
// Usage in a pedal shell (both macros expand to nothing in non-DIAG builds,
// so those binaries are byte-identical):
//   DIAG_LINK_INIT();                  // after hw.seed.StartLog(false)
//   DIAG_LINK_POLL(hw.seed, "nitrolab"); // in the main loop (never the ISR)
//
// The USB receive callback runs in interrupt context: it only scans for '?'
// and sets a volatile flag. The print happens in the main loop.
//
// Requires daisy.h included before this file.

#ifdef CT3_DIAG_BUILD

#ifndef DIAG_GIT_HASH
#define DIAG_GIT_HASH "unknown"
#endif
#ifndef DIAG_BUILD_TIME
#define DIAG_BUILD_TIME "unknown"
#endif

namespace diag_link {

// C++14 (no inline variables): one flag via a function-local static.
inline volatile bool& Query() {
  static volatile bool q = false;
  return q;
}

inline void OnRx(uint8_t* buf, uint32_t* len) {
  for (uint32_t i = 0; i < *len; i++)
    if (buf[i] == '?') Query() = true;
}

inline void Init() {
  static daisy::UsbHandle usb;   // stateless handle, shared with the logger
  usb.SetReceiveCallback(OnRx, daisy::UsbHandle::FS_INTERNAL);
}

template <typename Seed>
inline void Poll(Seed& seed, const char* pedal) {
  if (!Query()) return;
  Query() = false;
  seed.PrintLine("BUILD %s %s DIAG %s", pedal, DIAG_GIT_HASH, DIAG_BUILD_TIME);
}

}  // namespace diag_link

#define DIAG_LINK_INIT() diag_link::Init()
#define DIAG_LINK_POLL(seed, pedal) diag_link::Poll(seed, pedal)

#else

#define DIAG_LINK_INIT() ((void)0)
#define DIAG_LINK_POLL(seed, pedal) ((void)0)

#endif
