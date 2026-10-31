#pragma once
// A spectrum display for the EQ page: the shape of what is sounding.
//
//  - SpectrumFeed (audio thread): keeps the latest samples of the output (the two channels averaged) in a ring buffer, so the audio thread does
//    almost nothing and the analysis runs elsewhere (the editor's timer).
//  - SpectrumAnalyzer: a Hann-windowed FFT of the latest samples, turned into the level at log-spaced points from 20 Hz to 20 kHz (power in
//    a sixth of an octave around each), with a tilt of +4.5 dB per octave so a guitar's natural fall-off does not hide the highs, and a fast
//    rise and slow fall over time so the picture is readable.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace sas {

class SpectrumFeed {
public:
    static constexpr std::size_t kSize = 16384;  // a power of two
    void push(const float* l, const float* r, std::size_t n) {
        std::size_t w = write_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) ring_[(w + i) & (kSize - 1)] = 0.5f * (l[i] + r[i]);
        write_.store(w + n, std::memory_order_release);
    }
    // The latest `n` samples (n <= kSize), oldest first.
    void latest(float* dst, std::size_t n) const {
        const std::size_t w = write_.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < n; ++i) dst[i] = ring_[(w - n + i) & (kSize - 1)];
    }

private:
    std::array<float, kSize> ring_{};
    std::atomic<std::size_t> write_{0};
};

class SpectrumAnalyzer {
public:
    static constexpr int kFft = 4096;
    static constexpr int kPoints = 192;
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr float kFloorDb = -120.0f;

    SpectrumAnalyzer() {
        window_.resize(kFft);
        double sum = 0.0;
        for (int i = 0; i < kFft; ++i) {
            window_[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(kFft)));
            sum += double(window_[std::size_t(i)]);
        }
        norm_ = 2.0 / sum;  // a sine of amplitude A gives a peak of A
        db_.fill(kFloorDb);
        buf_.resize(kFft);
        twiddle_.resize(kFft / 2);
        for (int i = 0; i < kFft / 2; ++i) twiddle_[std::size_t(i)] = std::polar(1.0f, float(-2.0 * 3.14159265358979323846 * double(i) / double(kFft)));
    }

    void setSampleRate(double fs) { fs_ = fs; }
    static double freqOf(int i) { return kMinHz * std::pow(kMaxHz / kMinHz, double(i) / double(kPoints - 1)); }

    // `x`: kFft samples (the latest). Updates the smoothed levels.
    void analyze(const float* x) {
        for (int i = 0; i < kFft; ++i) buf_[std::size_t(i)] = {x[i] * window_[std::size_t(i)], 0.0f};
        fft();
        const int bins = kFft / 2;
        std::vector<float> power(std::size_t(bins) + 1);
        for (int k = 0; k <= bins; ++k) {
            const double a = double(std::abs(buf_[std::size_t(k)])) * norm_;
            power[std::size_t(k)] = float(a * a);
        }
        const double binHz = fs_ / double(kFft);
        const double half = std::pow(2.0, 1.0 / 12.0);  // a sixth of an octave wide: +- 1/12
        for (int i = 0; i < kPoints; ++i) {
            const double f = freqOf(i);
            const double lo = f / half / binHz, hi = f * half / binHz;
            double p = 0.0;
            const int k0 = int(std::ceil(lo)), k1 = int(std::floor(hi));
            if (k1 >= k0) {
                for (int k = std::max(k0, 1); k <= std::min(k1, bins); ++k) p += double(power[std::size_t(k)]);
            } else {  // narrower than a bin (the lows): interpolate between the neighbours
                const double pos = f / binHz;
                const int k = std::clamp(int(std::floor(pos)), 0, bins - 1);
                const double fr = pos - double(k);
                p = double(power[std::size_t(k)]) * (1.0 - fr) + double(power[std::size_t(k) + 1]) * fr;
            }
            float db = float(10.0 * std::log10(p + 1e-14)) + float(4.5 * std::log2(f / 1000.0));
            db = std::max(db, kFloorDb);
            float& cur = db_[std::size_t(i)];
            cur += (db - cur) * (db > cur ? 0.6f : 0.12f);  // fast rise, slow fall
        }
    }
    void reset() { db_.fill(kFloorDb); }
    const std::array<float, kPoints>& db() const { return db_; }

private:
    void fft() {  // iterative radix-2, in place
        const int n = kFft;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(buf_[std::size_t(i)], buf_[std::size_t(j)]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; ++k) {
                    const std::complex<float> u = buf_[std::size_t(i + k)], v = buf_[std::size_t(i + k + len / 2)] * twiddle_[std::size_t(k * step)];
                    buf_[std::size_t(i + k)] = u + v;
                    buf_[std::size_t(i + k + len / 2)] = u - v;
                }
        }
    }

    double fs_ = 48000.0, norm_ = 1.0;
    std::vector<float> window_;
    std::vector<std::complex<float>> buf_, twiddle_;
    std::array<float, kPoints> db_{};
};

}  // namespace sas
