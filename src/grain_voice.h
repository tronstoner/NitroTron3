#pragma once
#include <cstddef>
#include <cmath>
#include "ring_buffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Single grain voice: reads from a ring buffer with adaptive Tukey window.
// Supports pitch shift, reverse, and looping (stutter).
class GrainVoice {
public:
    bool IsActive() const { return active_; }

    // Start a grain. `loops` = number of times to play the fragment
    // (1 = once, >1 = stutter repeat).
    void Trigger(const RingBuffer& buf, size_t delay, size_t length,
                 bool reverse = false, float rate = 1.f, float gain = 1.f,
                 int loops = 1) {
        size_t wp = buf.GetWritePos();
        size_t bl = buf.GetLength();
        float start;
        if (reverse) {
            start = static_cast<float>((wp + bl - delay + length) % bl);
            rate_ = -rate;
        } else {
            start = static_cast<float>((wp + bl - delay) % bl);
            rate_ = rate;
        }
        start_pos_f_ = start;
        read_pos_f_ = start;
        buf_len_f_ = static_cast<float>(bl);
        grain_len_ = length;
        phase_ = 0;
        gain_ = gain;
        loops_left_ = loops - 1;
        active_ = true;

        // Tukey alpha: long grains → full Hann, short grains → mostly flat
        float len_f = static_cast<float>(length);
        alpha_ = 0.2f + (len_f - 960.f) / (9600.f - 960.f) * 0.8f;
        if (alpha_ < 0.2f) alpha_ = 0.2f;
        if (alpha_ > 1.0f) alpha_ = 1.0f;
        taper_samples_ = static_cast<size_t>(alpha_ * len_f * 0.5f);
        // Looping grains need enough taper to fade cleanly at the loop point
        size_t min_taper = (loops > 1) ? 240 : 1;  // 5 ms minimum for loops
        if (taper_samples_ < min_taper) taper_samples_ = min_taper;
        if (taper_samples_ > grain_len_ / 2) taper_samples_ = grain_len_ / 2;
    }

    float Process(const RingBuffer& buf) {
        if (!active_) return 0.f;

        // Tukey window: cosine taper at edges, flat in the middle
        float window;
        if (phase_ < taper_samples_) {
            float t = static_cast<float>(phase_) / static_cast<float>(taper_samples_);
            window = 0.5f * (1.f - cosf(static_cast<float>(M_PI) * t));
        } else if (phase_ >= grain_len_ - taper_samples_) {
            float t = static_cast<float>(grain_len_ - 1 - phase_)
                      / static_cast<float>(taper_samples_);
            window = 0.5f * (1.f - cosf(static_cast<float>(M_PI) * t));
        } else {
            window = 1.f;
        }

        float sample = buf.ReadFrac(read_pos_f_);

        read_pos_f_ += rate_;
        if (read_pos_f_ >= buf_len_f_) read_pos_f_ -= buf_len_f_;
        if (read_pos_f_ < 0.f) read_pos_f_ += buf_len_f_;

        phase_++;
        if (phase_ >= grain_len_) {
            if (loops_left_ > 0) {
                // Stutter: snap back to start, replay
                phase_ = 0;
                read_pos_f_ = start_pos_f_;
                loops_left_--;
            } else {
                active_ = false;
            }
        }

        return sample * window * gain_;
    }

private:
    float start_pos_f_ = 0.f;
    float read_pos_f_ = 0.f;
    float buf_len_f_ = 1.f;
    float rate_ = 1.f;
    size_t grain_len_ = 1;
    size_t phase_ = 0;
    float gain_ = 1.f;
    float alpha_ = 1.f;
    size_t taper_samples_ = 1;
    int loops_left_ = 0;
    bool active_ = false;
};
