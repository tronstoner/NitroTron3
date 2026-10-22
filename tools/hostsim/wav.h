#pragma once
// Minimal WAV I/O for the host simulator.
// Read: PCM 16 / 24 / 32-bit int, 32-bit float (also WAVE_FORMAT_EXTENSIBLE);
//       multi-channel files: channel 0 only. Write: mono 24-bit PCM.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace wav {

struct Info { int channels = 0, rate = 0, bits = 0; bool is_float = false; };

static inline uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Returns an error message, or empty on success.
inline std::string Read(const char* path, std::vector<float>& out, Info& info) {
  FILE* f = fopen(path, "rb");
  if (!f) return std::string("cannot open ") + path;
  std::vector<uint8_t> b;
  uint8_t tmp[65536];
  size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) b.insert(b.end(), tmp, tmp + n);
  fclose(f);
  if (b.size() < 12 || memcmp(&b[0], "RIFF", 4) || memcmp(&b[8], "WAVE", 4))
    return std::string(path) + ": not a RIFF/WAVE file";
  size_t pos = 12;
  int fmt_tag = -1;
  const uint8_t* data = nullptr;
  size_t data_len = 0;
  while (pos + 8 <= b.size()) {
    const uint8_t* c = &b[pos];
    uint32_t len = U32(c + 4);
    const uint8_t* body = c + 8;
    size_t avail = b.size() - pos - 8;
    if (!memcmp(c, "fmt ", 4) && len >= 16 && avail >= 16) {
      fmt_tag = U16(body);
      info.channels = U16(body + 2);
      info.rate = (int)U32(body + 4);
      info.bits = U16(body + 14);
      if (fmt_tag == 0xFFFE && len >= 40 && avail >= 40) fmt_tag = U16(body + 24);  // sub-format GUID
    } else if (!memcmp(c, "data", 4)) {
      data = body;
      data_len = (len <= avail) ? len : avail;   // tolerate truncated / streaming headers
    }
    pos += 8 + (size_t)len + (len & 1);
  }
  if (fmt_tag < 0 || !data) return std::string(path) + ": missing fmt or data chunk";
  if (fmt_tag != 1 && fmt_tag != 3) return std::string(path) + ": unsupported WAV format tag";
  info.is_float = (fmt_tag == 3);
  if (info.is_float && info.bits != 32) return std::string(path) + ": only 32-bit float supported";
  if (!info.is_float && info.bits != 16 && info.bits != 24 && info.bits != 32)
    return std::string(path) + ": only 16 / 24 / 32-bit PCM supported";
  if (info.channels < 1) return std::string(path) + ": bad channel count";
  const int bps = info.bits / 8;
  const size_t frame = (size_t)bps * info.channels;
  const size_t frames = data_len / frame;
  out.resize(frames);
  for (size_t i = 0; i < frames; i++) {
    const uint8_t* p = data + i * frame;   // channel 0
    float v;
    if (info.is_float) { memcpy(&v, p, 4); }
    else if (bps == 2) { v = (float)(int16_t)U16(p) / 32768.f; }
    else if (bps == 3) { int32_t s = (int32_t)((p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8;
                         v = (float)s / 8388608.f; }
    else               { v = (float)((double)(int32_t)U32(p) / 2147483648.0); }
    out[i] = v;
  }
  return "";
}

static inline void Put32(FILE* f, uint32_t v) { uint8_t p[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; fwrite(p, 1, 4, f); }
static inline void Put16(FILE* f, uint16_t v) { uint8_t p[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; fwrite(p, 1, 2, f); }

// Mono 24-bit PCM. Samples outside [-1, 1) are clipped (count returned via `clipped`).
inline std::string Write24(const char* path, const std::vector<float>& x, int rate, size_t* clipped = nullptr) {
  FILE* f = fopen(path, "wb");
  if (!f) return std::string("cannot write ") + path;
  const uint32_t data_len = (uint32_t)(x.size() * 3);
  fwrite("RIFF", 1, 4, f); Put32(f, 36 + data_len + (data_len & 1)); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); Put32(f, 16); Put16(f, 1); Put16(f, 1);
  Put32(f, (uint32_t)rate); Put32(f, (uint32_t)rate * 3); Put16(f, 3); Put16(f, 24);
  fwrite("data", 1, 4, f); Put32(f, data_len);
  size_t clip = 0;
  for (float v : x) {
    double s = std::nearbyint((double)v * 8388608.0);
    if (s > 8388607.0) { s = 8388607.0; clip++; }
    if (s < -8388608.0) { s = -8388608.0; clip++; }
    const int32_t i = (int32_t)s;
    uint8_t p[3] = {(uint8_t)i, (uint8_t)(i >> 8), (uint8_t)(i >> 16)};
    fwrite(p, 1, 3, f);
  }
  if (data_len & 1) fputc(0, f);
  fclose(f);
  if (clipped) *clipped = clip;
  return "";
}

}  // namespace wav
