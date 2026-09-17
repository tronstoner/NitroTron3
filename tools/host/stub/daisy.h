#pragma once
#include <cstdint>
#include <cstddef>
#define DSY_SDRAM_BSS
namespace daisy {
struct Led { float v = 0.f; void Set(float x) { v = x; } void Update() {} };
struct System { static uint32_t now_ms; static uint32_t GetNow() { return now_ms; } };
}
