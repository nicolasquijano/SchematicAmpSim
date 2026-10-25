#pragma once
// 2x / 4x oversampling from cascaded linear-phase halfband FIR stages (port of oversample.rs).
// Differences from the Rust code that do not change the arithmetic: history lives in a doubled
// ring buffer instead of being shifted every sample, and the callback is a template parameter.

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

class HalfbandStage {
public:
    // `half` = M: 2M+1 taps, group delay of M samples at the fast rate.
    explicit HalfbandStage(std::size_t half) : half_(half), len_(2 * half + 1), up_(len_), down_(len_) {
        constexpr double pi = 3.14159265358979323846;
        std::vector<double> taps(len_);
        for (std::size_t i = 0; i < len_; ++i) {
            const long n = long(i) - long(half);
            const double ideal = n == 0 ? 0.5 : (n % 2 == 0 ? 0.0 : std::sin(pi * n / 2.0) / (pi * n));
            const double x = double(i) / double(len_ - 1);
            const double w = 0.35875 - 0.48829 * std::cos(2.0 * pi * x) + 0.14128 * std::cos(4.0 * pi * x)
                             - 0.01168 * std::cos(6.0 * pi * x);
            taps[i] = ideal * w;
        }
        double sum = 0.0;
        for (double t : taps) sum += t;
        for (std::size_t i = 0; i < len_; ++i)
            if (taps[i] != 0.0) {
                idx_.push_back(i);
                val_.push_back(float(taps[i] / sum));
            }
    }

    std::size_t half() const { return half_; }

    void reset() { up_.clear(len_); down_.clear(len_); }

    std::array<float, 2> up(float x) {
        up_.push(2.0f * x, len_);
        const float a = dot(up_);
        up_.push(0.0f, len_);
        const float b = dot(up_);
        return {a, b};
    }

    float down(float a, float b) {
        down_.push(a, len_);
        down_.push(b, len_);
        return dot(down_);
    }

private:
    // buf[pos + i] is the sample i steps back; every write is mirrored `len` slots up so the
    // window is always contiguous.
    struct History {
        std::vector<float> buf;
        std::size_t pos = 0;
        explicit History(std::size_t len) : buf(2 * len, 0.0f) {}
        void push(float x, std::size_t len) {
            pos = pos == 0 ? len - 1 : pos - 1;
            buf[pos] = x;
            buf[pos + len] = x;
        }
        void clear(std::size_t) { std::fill(buf.begin(), buf.end(), 0.0f); }
        const float* window() const { return buf.data() + pos; }
    };

    float dot(const History& h) const {
        const float* w = h.window();
        float acc = 0.0f;
        for (std::size_t k = 0; k < idx_.size(); ++k) acc += w[idx_[k]] * val_[k];
        return acc;
    }

    std::size_t half_, len_;
    std::vector<std::size_t> idx_;
    std::vector<float> val_;
    History up_, down_;
};

class Oversampler {
public:
    // stages = 1 for 2x, 2 for 4x.
    explicit Oversampler(std::size_t stages) : stages_(stages), s0_(23), s1_(15) {}

    std::size_t factor() const { return std::size_t(1) << stages_; }

    // Round-trip latency in samples at the base rate.
    float latencySamples() const {
        float sum = 2.0f * float(s0_.half()) / 2.0f;
        if (stages_ == 2) sum += 2.0f * float(s1_.half()) / 4.0f;
        return sum;
    }

    void reset() { s0_.reset(); s1_.reset(); }

    // Runs `f` at the oversampled rate on one input sample and returns the decimated result.
    template <class F>
    float process(float x, F&& f) {
        const auto [a, b] = s0_.up(x);
        if (stages_ == 1) {
            const float fa = f(a), fb = f(b);
            return s0_.down(fa, fb);
        }
        std::array<float, 2> z{};
        std::size_t i = 0;
        for (float v : {a, b}) {
            const auto [c, d] = s1_.up(v);
            const float fc = f(c), fd = f(d);
            z[i++] = s1_.down(fc, fd);
        }
        return s0_.down(z[0], z[1]);
    }

private:
    std::size_t stages_;
    HalfbandStage s0_, s1_;
};

}  // namespace sas
