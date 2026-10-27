#pragma once
// Chromatic tuner: pitch detection on the guitar signal.
//
//  - TunerFeed (audio thread): decimates the input to about 24 kHz and keeps the latest few thousand samples in a ring buffer, so the
//    audio thread does almost nothing and the analysis runs elsewhere (the editor's timer).
//  - PitchDetector: the McLeod method. The normalised square difference function of the latest samples has a peak at the period of the
//    signal; picking the FIRST key maximum above a fraction of the highest one avoids the octave errors that plain autocorrelation makes
//    on a guitar's weak low fundamental, and parabolic interpolation refines it to a fraction of a sample.
//  - noteFor(): the nearest note of the equal-tempered scale for a reference A, and the deviation in cents.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct PitchResult {
    bool valid = false;
    double hz = 0.0;
    double clarity = 0.0;  // 0..1: how periodic the signal is (a clean note is above 0.9)
};

class PitchDetector {
public:
    // `x`: `n` samples at `rate` Hz (n should cover at least three periods of the lowest note: 2048+ at 24 kHz).
    static PitchResult detect(const float* x, std::size_t n, double rate, double minHz = 60.0, double maxHz = 1400.0) {
        PitchResult r;
        if (n < 256 || rate <= 0.0) return r;
        double energy = 0.0;
        for (std::size_t i = 0; i < n; ++i) energy += double(x[i]) * x[i];
        if (std::sqrt(energy / double(n)) < kSilence) return r;

        const std::size_t maxLag = std::min<std::size_t>(std::size_t(rate / minHz) + 2, n / 2);
        const std::size_t minLag = std::max<std::size_t>(2, std::size_t(rate / maxHz));
        if (maxLag <= minLag + 2) return r;

        // Normalised square difference function: nsdf(t) = 2 r(t) / m(t), r the autocorrelation, m the sum of both energies.
        std::vector<double> nsdf(maxLag + 1, 0.0);
        const std::size_t w = n - maxLag;  // the same window for every lag, so the values are comparable
        double m0 = 0.0;
        for (std::size_t j = 0; j < w; ++j) m0 += double(x[j]) * x[j];
        double mLag = 0.0;
        for (std::size_t j = 0; j < w; ++j) mLag += double(x[j]) * x[j];
        for (std::size_t t = 0; t <= maxLag; ++t) {
            double acf = 0.0;
            for (std::size_t j = 0; j < w; ++j) acf += double(x[j]) * x[j + t];
            if (t > 0) mLag += double(x[w - 1 + t]) * x[w - 1 + t] - double(x[t - 1]) * x[t - 1];  // slide the second window
            // (m0 is the energy of x[0..w), mLag that of x[t..t+w))
            nsdf[t] = (m0 + mLag) > 1e-18 ? 2.0 * acf / (m0 + mLag) : 0.0;
        }

        // Key maxima: the highest point between a positive-going and the next negative-going zero crossing.
        struct Peak { double pos, value; };
        std::vector<Peak> peaks;
        std::size_t t = 1;
        while (t < maxLag && nsdf[t] > 0.0) ++t;  // skip the lobe around lag 0
        while (t < maxLag) {
            while (t < maxLag && nsdf[t] <= 0.0) ++t;
            std::size_t best = t;
            while (t < maxLag && nsdf[t] > 0.0) {
                if (nsdf[t] > nsdf[best]) best = t;
                ++t;
            }
            if (best >= minLag && best < maxLag && nsdf[best] > 0.0 && best > 0 && nsdf[best] >= nsdf[best - 1] && nsdf[best] >= nsdf[best + 1]) {
                // Parabolic interpolation around the top.
                const double a = nsdf[best - 1], b = nsdf[best], c = nsdf[best + 1];
                const double den = a - 2.0 * b + c;
                const double shift = std::abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
                peaks.push_back({double(best) + shift, b - 0.25 * (a - c) * shift});
            }
        }
        if (peaks.empty()) return r;
        double top = 0.0;
        for (const auto& p : peaks) top = std::max(top, p.value);
        for (const auto& p : peaks) {
            if (p.value >= kKeyThreshold * top) {  // the first one that is close to the best: the fundamental, not a subharmonic
                r.valid = top > kMinClarity;
                r.hz = rate / p.pos;
                r.clarity = p.value;
                break;
            }
        }
        return r;
    }

    static constexpr double kSilence = 0.002;       // rms below which there is no note (about -54 dBFS)
    static constexpr double kKeyThreshold = 0.9;
    static constexpr double kMinClarity = 0.75;
};

struct NoteInfo {
    int midi = 0;          // 69 = A4
    double cents = 0.0;    // -50..+50 from the nearest note
    int octave = 0;        // scientific pitch: A4 = 440 Hz, middle C = C4
    int pitchClass = 0;    // 0 = C ... 11 = B
    const char* name = "";
};

inline NoteInfo noteFor(double hz, double referenceA = 440.0) {
    static constexpr const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    NoteInfo n;
    const double semis = 12.0 * std::log2(hz / referenceA) + 69.0;
    n.midi = int(std::lround(semis));
    n.cents = (semis - double(n.midi)) * 100.0;
    n.pitchClass = ((n.midi % 12) + 12) % 12;
    n.octave = (n.midi - n.pitchClass) / 12 - 1;
    n.name = names[n.pitchClass];
    return n;
}

class TunerFeed {
public:
    static constexpr std::size_t kSize = 8192;  // ring buffer; a power of two

    void prepare(double hostRate) {
        factor_ = std::max<int>(1, int(std::lround(hostRate / 24000.0)));
        rate_.store(hostRate / double(factor_));
        acc_ = 0.0;
        count_ = 0;
        lp_ = 0.0;
        write_.store(0);
    }
    double rate() const { return rate_.load(); }
    // Audio thread. A box average of `factor` samples (a crude low-pass: guitar harmonics above 6 kHz do not matter for the pitch).
    void push(const float* x, std::size_t n) {
        std::size_t w = write_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            acc_ += double(x[i]);
            if (++count_ >= factor_) {
                const double v = acc_ / double(factor_);
                acc_ = 0.0;
                count_ = 0;
                lp_ += 0.7 * (v - lp_);
                buffer_[w & (kSize - 1)].store(float(lp_), std::memory_order_relaxed);
                ++w;
            }
        }
        write_.store(w, std::memory_order_release);
    }
    // Any thread: the latest `n` decimated samples, oldest first. Returns false while fewer than n have arrived.
    bool copyLatest(float* dst, std::size_t n) const {
        const std::size_t w = write_.load(std::memory_order_acquire);
        if (n > kSize || w < n) return false;
        for (std::size_t i = 0; i < n; ++i) dst[i] = buffer_[(w - n + i) & (kSize - 1)].load(std::memory_order_relaxed);
        return true;
    }

private:
    int factor_ = 2;
    std::atomic<double> rate_{24000.0};
    double acc_ = 0.0, lp_ = 0.0;
    int count_ = 0;
    std::atomic<std::size_t> write_{0};
    std::array<std::atomic<float>, kSize> buffer_{};
};

}  // namespace sas
