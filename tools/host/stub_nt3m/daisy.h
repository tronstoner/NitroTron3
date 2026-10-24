#pragma once
// Host stub of the libDaisy surface used by pedals/nitrotron3/main.cpp,
// pedals/nitrotron3m/main.cpp and pedals/nitrotron3/preset_system.h — for
// tools/host/nt3m_ab.cpp only (the other host tests use tools/host/stub/).
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#define DSY_SDRAM_BSS
#define DTCM_MEM_SECTION

#define REV_ID_V 0x2003U
static inline uint32_t HAL_GetREVID() { return REV_ID_V; }
// FPSCR (flush-to-zero) has no host equivalent here: x86 SSE keeps denormals,
// for BOTH sides of the A/B, so the comparison is unaffected.
static inline uint32_t __get_FPSCR() { return 0; }
static inline void __set_FPSCR(uint32_t) {}

namespace daisy {

struct Pin { int port = 0, pin = 0; };
struct QSPIHandle {};

class Led {
 public:
  void Init(Pin, bool, float = 1000.f) {}
  void Set(float v) { v_ = v; }
  void Update() {}
  float v_ = 0.f;
};

struct System {
  enum class BootloaderMode { STM = 0, DAISY, DAISY_SKIP_TIMEOUT, DAISY_INFINITE_TIMEOUT };
  static uint32_t now_ms;
  static uint32_t GetNow() { return now_ms; }
  static uint32_t GetUs() { return now_ms * 1000u; }
  static void Delay(uint32_t) {}
  static void ResetToBootloader(BootloaderMode = BootloaderMode::STM) {
    std::fprintf(stderr, "host stub: unexpected ResetToBootloader\n");
    std::exit(3);
  }
};

struct AudioHandle {
  typedef const float* const* InputBuffer;
  typedef float** OutputBuffer;
  typedef void (*AudioCallback)(InputBuffer in, OutputBuffer out, size_t size);
};

struct SaiHandle {
  struct Config {
    enum class SampleRate { SAI_8KHZ, SAI_16KHZ, SAI_32KHZ, SAI_48KHZ, SAI_96KHZ };
  };
};

class CpuLoadMeter {
 public:
  void Init(float, size_t, float = 1.f) {}
  void OnBlockStart() {}
  void OnBlockEnd() {}
  float GetMaxCpuLoad() const { return 0.f; }
  void Reset() {}
};

}  // namespace daisy
