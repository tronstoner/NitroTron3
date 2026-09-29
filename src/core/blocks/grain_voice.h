#pragma once
#include <cstddef>
#include <cmath>
#include "ring_buffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shared raised-cosine (half-Hann) lookup: HannRise(t) = 0.5*(1-cos(pi*t)),
// t in [0,1]. The grain window is the dominant per-grain CPU cost (a cosf every
// sample); this replaces it with an interpolated LUT (512 pts → error < 1e-4,
// audibly identical). One-time fill on first call.
static constexpr int GRAIN_WIN_LUT_N = 512;
inline float GrainHannRise(float t) {
    static float lut[GRAIN_WIN_LUT_N + 1];
    static bool  init = false;
    if (!init) {
        for (int i = 0; i <= GRAIN_WIN_LUT_N; i++)
            lut[i] = 0.5f * (1.f - cosf((float)M_PI * (float)i / (float)GRAIN_WIN_LUT_N));
        init = true;
    }
    if (t <= 0.f) return lut[0];
    if (t >= 1.f) return lut[GRAIN_WIN_LUT_N];
    float x = t * (float)GRAIN_WIN_LUT_N;
    int   i = (int)x;
    float f = x - (float)i;
    return lut[i] * (1.f - f) + lut[i + 1] * f;
}

// Single grain voice: reads from a ring buffer with adaptive Tukey window.
// Supports pitch shift, reverse, and looping (stutter).
// Optional read-time VIEW of a grain's source (see GrainVoice::SetView): a loop
// whose playable length differs from what is stored, without rewriting the
// stored material. Position p reads, in order:
//   p <  body_end               : body[p]  (x a linear fade-out over
//                                 [fade_start, body_end) when fade_start < body_end)
//   p <  loop_len               : 0        (padded silence)
//   p <  loop_len + guard_len   : guard[p - loop_len]  (seam + head continuation)
//   else                        : 0
// The caller keeps the view alive and unchanged while any grain reads it.
struct GrainView {
    const float* body = nullptr;
    size_t body_end   = 0;
    size_t fade_start = 0;
    float  fade_k     = 0.f;      // fade gain = (body_end - p) * fade_k
    size_t loop_len   = 0;
    const float* guard = nullptr;
    size_t guard_len  = 0;
    float At(size_t i) const {
        if (i < body_end) {
            const float x = body[i];
            return (i >= fade_start) ? x * static_cast<float>(body_end - i) * fade_k : x;
        }
        if (i < loop_len) return 0.f;
        const size_t k = i - loop_len;
        return (k < guard_len) ? guard[k] : 0.f;
    }
    float Read(float pos) const {
        const size_t i0 = static_cast<size_t>(pos);
        const float  f  = pos - static_cast<float>(i0);
        return At(i0) * (1.f - f) + At(i0 + 1) * f;
    }
};

class GrainVoice {
public:
    bool IsActive() const { return active_; }
    void Reset() { active_ = false; phase_ = 0; loops_left_ = 0; }  // hard-stop this voice

    // Start a grain. `loops` = number of times to play the fragment
    // (1 = once, >1 = stutter repeat).
    // alpha_override < 0 → length-based Tukey (default). >= 0 → forced Tukey
    // alpha (use 1.0 = full Hann for smooth grains so a 2× overlap sums to
    // constant amplitude — no overlap-add tremolo).
    // attack_scale: scales ONLY the fade-IN taper (release taper is unchanged),
    // giving an asymmetric window for the first grain of a fresh loop:
    //   1.0 = normal (default; all existing callers unchanged),
    //   0.0 = instant attack (start at full amplitude),
    //   in between = a proportionally shorter fade-in.
    void Trigger(const RingBuffer& buf, size_t delay, size_t length,
                 bool reverse = false, float rate = 1.f, float gain = 1.f,
                 int loops = 1, float alpha_override = -1.f,
                 float attack_scale = 1.f) {
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
        start_d_ = static_cast<double>(start);
        exact_ = false;
        view_ = nullptr;
        fo_n_ = 0; fo_k_ = 0;
        buf_len_f_ = static_cast<float>(bl);
        grain_len_ = length;
        phase_ = 0;
        gain_ = gain;
        loops_left_ = loops - 1;
        active_ = true;
        filt_on_ = false;   // band filter off unless SetBandFilter() is called after Trigger

        // Tukey alpha: forced override (smooth grains → full Hann), else
        // length-based (long grains → full Hann, short grains → mostly flat).
        float len_f = static_cast<float>(length);
        if (alpha_override >= 0.f) {
            alpha_ = alpha_override;
        } else {
            alpha_ = 0.2f + (len_f - 960.f) / (9600.f - 960.f) * 0.8f;
            if (alpha_ < 0.2f) alpha_ = 0.2f;
            if (alpha_ > 1.0f) alpha_ = 1.0f;
        }
        taper_samples_ = static_cast<size_t>(alpha_ * len_f * 0.5f);
        // Looping grains need enough taper to fade cleanly at the loop point
        size_t min_taper = (loops > 1) ? 240 : 1;  // 5 ms minimum for loops
        if (taper_samples_ < min_taper) taper_samples_ = min_taper;
        if (taper_samples_ > grain_len_ / 2) taper_samples_ = grain_len_ / 2;
        attack_taper_ = static_cast<size_t>(taper_samples_ * attack_scale);
    }

    // Move a just-triggered grain's start (and read position) by d samples,
    // 0 <= d < 1, so a grain can start on a FRACTIONAL source position (Trigger
    // takes an integer delay). Call right after Trigger. The same offset applies
    // to forward and reverse grains: it shifts where the read begins. Unused by
    // callers that do not call it (their grains are unchanged).
    void NudgeStart(float d) {
        start_pos_f_ += d;
        read_pos_f_  += d;
        start_d_     += static_cast<double>(d);
        if (start_pos_f_ >= buf_len_f_) start_pos_f_ -= buf_len_f_;
        if (read_pos_f_  >= buf_len_f_) read_pos_f_  -= buf_len_f_;
        if (start_d_ >= static_cast<double>(buf_len_f_)) start_d_ -= static_cast<double>(buf_len_f_);
    }

    // Opt-in exact tracking (call after Trigger / NudgeStart). The default read
    // position ACCUMULATES rate_ in float: exact for 1, 0.5, 2 (binary
    // fractions), but at other rates each add rounds with a bias, and far into
    // a long buffer that drifts the grain ~1e-3 samples per sample away from
    // where it should read. With this set, the position is recomputed every
    // sample as start + phase * rate in double — no accumulation. Off for every
    // caller that does not ask (their output is unchanged).
    void SetExactTrack() { exact_ = true; }

    // Opt-in: read through a GrainView instead of the ring's raw memory (call
    // after Trigger). Positions are the same; only what they read changes.
    // Unbound grains (every caller that does not ask) are unchanged.
    void SetView(const GrainView* v) { view_ = v; }

    // Opt-in: end this grain within n samples, multiplying what is left of it
    // by a Hann fall (1 -> 0), so a caller can crossfade it against a grain
    // started now with an n-sample Hann attack (the two curves sum to 1).
    // No effect unless called.
    void FadeOut(size_t n) { if (n < 1) n = 1; fo_n_ = n; fo_k_ = 0; }
    bool FadingOut() const { return fo_n_ > 0; }

    // Optional per-grain band filter (2-pole biquad), applied to the read sample
    // BEFORE windowing — so band-limiting a grain == granulating a pre-filtered
    // buffer (host-verified identical; the Hann fade-in masks the filter's start
    // transient). Call AFTER Trigger. Used by the multiband granular freeze.
    void SetBandFilter(float b0, float b1, float b2, float a1, float a2) {
        f_b0_ = b0; f_b1_ = b1; f_b2_ = b2; f_a1_ = a1; f_a2_ = a2;
        f_z1_ = f_z2_ = 0.f; filt_on_ = true;
    }

    float Process(const RingBuffer& buf) {
        if (!active_) return 0.f;

        // Tukey window: cosine taper at edges, flat in the middle. The fade-IN
        // uses attack_taper_ (scaled) so the first grain of a loop can start
        // immediately; the fade-OUT still uses the full taper_samples_.
        float window;
        if (phase_ < attack_taper_) {
            window = GrainHannRise(static_cast<float>(phase_)
                                   / static_cast<float>(attack_taper_));
        } else if (phase_ >= grain_len_ - taper_samples_) {
            window = GrainHannRise(static_cast<float>(grain_len_ - 1 - phase_)
                                   / static_cast<float>(taper_samples_));
        } else {
            window = 1.f;
        }

        if (exact_) {
            double p = start_d_ + static_cast<double>(phase_) * static_cast<double>(rate_);
            const double bl = static_cast<double>(buf_len_f_);
            while (p >= bl) p -= bl;
            while (p < 0.0) p += bl;
            float pf = static_cast<float>(p);
            if (pf >= buf_len_f_) pf = 0.f;                   // float rounding onto len
            read_pos_f_ = pf;
        }
        // read_pos_f_ is kept in [0, buf_len_f_) below, so use the wrap-free read
        // (ReadFrac's fmodf is redundant here and was a big per-grain cost).
        float sample = view_ ? view_->Read(read_pos_f_) : buf.ReadFracFast(read_pos_f_);
        if (filt_on_) {   // band-limit before windowing (TDF-II biquad)
            float y = f_b0_ * sample + f_z1_;
            f_z1_ = f_b1_ * sample - f_a1_ * y + f_z2_;
            f_z2_ = f_b2_ * sample - f_a2_ * y;
            // Anti-denormal: a grain's filter decays to ~0 every fade-out; without
            // this the states go denormal → huge software-FP penalty on the M7 that
            // scales with grain count (the multi-voice freeze overload). Flush the
            // sub-audible tail to zero. FPSCR.FZ alone isn't reliable in the IRQ.
            if (fabsf(f_z1_) < 1e-25f) f_z1_ = 0.f;
            if (fabsf(f_z2_) < 1e-25f) f_z2_ = 0.f;
            sample = y;
        }

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

        if (fo_n_ > 0) {
            const float fall = 1.f - GrainHannRise(static_cast<float>(fo_k_) / static_cast<float>(fo_n_));
            if (++fo_k_ >= fo_n_) active_ = false;
            return sample * window * gain_ * fall;
        }
        return sample * window * gain_;
    }

private:
    float start_pos_f_ = 0.f;
    double start_d_ = 0.0;      // exact-track start (SetExactTrack)
    const GrainView* view_ = nullptr;   // read-time view (SetView)
    size_t fo_n_ = 0, fo_k_ = 0;        // FadeOut length / progress
    bool  exact_ = false;
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
    size_t attack_taper_ = 1;   // fade-in taper length (scaled; release uses taper_samples_)
    // Optional per-grain band filter (off by default → zero cost, existing callers unchanged)
    bool  filt_on_ = false;
    float f_b0_ = 1.f, f_b1_ = 0.f, f_b2_ = 0.f, f_a1_ = 0.f, f_a2_ = 0.f, f_z1_ = 0.f, f_z2_ = 0.f;
};
