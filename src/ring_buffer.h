#pragma once
#include <cstddef>
#include <cstring>
#include <cmath>

// Mono circular buffer for granular source material.
// Buffer memory must be allocated externally (SDRAM for large buffers).
class RingBuffer {
public:
    void Init(float* buffer, size_t length) {
        buf_ = buffer;
        len_ = length;
        write_pos_ = 0;
        memset(buf_, 0, len_ * sizeof(float));
    }

    void Write(float sample) {
        buf_[write_pos_] = sample;
        write_pos_++;
        if (write_pos_ >= len_) write_pos_ = 0;
    }

    // Read from absolute position (wraps)
    float ReadAbs(size_t pos) const {
        return buf_[pos % len_];
    }

    // Read from fractional position with linear interpolation (wraps)
    float ReadFrac(float pos) const {
        float wrapped = fmodf(pos, static_cast<float>(len_));
        if (wrapped < 0.f) wrapped += static_cast<float>(len_);
        size_t idx = static_cast<size_t>(wrapped);
        size_t next = (idx + 1 < len_) ? idx + 1 : 0;
        float frac = wrapped - static_cast<float>(idx);
        return buf_[idx] * (1.f - frac) + buf_[next] * frac;
    }

    // Read from `delay` samples behind current write head
    float ReadDelay(size_t delay) const {
        size_t pos = (write_pos_ + len_ - delay) % len_;
        return buf_[pos];
    }

    size_t GetWritePos() const { return write_pos_; }
    size_t GetLength() const { return len_; }

private:
    float* buf_ = nullptr;
    size_t len_ = 0;
    size_t write_pos_ = 0;
};
