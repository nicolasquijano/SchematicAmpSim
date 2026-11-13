#pragma once
// An acoustic guitar simulator for an electric guitar: it takes the magnetic pickup's sound and gives it what a flat-top guitar has and a solid-body
// does not: the resonances of the box. A high-pass takes away the pickup's boom, then six peaking filters (the body modes: the air resonance near
// 100 Hz, the top and the back plate a little above, a scoop in the low mids and the presence of the strings in the upper mids) shape the body, and
// two shelves set the brightness. A gentle compressor flattens the dynamics the way a piezo pickup does. The four types are the modes of a dreadnought,
// a jumbo, a parlor guitar and an undersaddle piezo (thin, quacky and bright). Body scales how far the resonances go (0 is a plain, filtered electric),
// Top tilts the strings' brightness (5 is flat), Air opens the highs above 9 kHz, Squash is the compression. The level is kept: the average gain of the
// filters across the guitar's range is taken off. Works on one channel or on a stereo pair (the same on both). Switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace sas {

struct AcousticParams {
    bool on = false;
    int type = 0;           // 0 dreadnought, 1 jumbo, 2 parlor, 3 piezo
    double body = 0.6;      // 0 .. 1: how far the body resonances go
    double top = 0.5;       // 0 .. 1: brightness of the strings; 0.5 is flat
    double air = 0.4;       // 0 .. 1: the highs above 9 kHz
    double squash = 0.4;    // 0 .. 1: compression
    double mix = 1.0;       // 0 .. 1: how much of the effect is in the output; applied by the rack module
};

class Acoustic {
public:
    void prepare(double fs) {
        fs_ = fs;
        ready_ = false;
        reset();
    }
    void reset() {
        for (auto& c : z_)
            for (auto& s : c) s = {0.0, 0.0};
        env_ = 0.0;
        gain_ = 1.0;
        gainTarget_ = 1.0;
        count_ = 0;
        amount_ = target_.on ? 1.0 : 0.0;
        body_ = target_.body;
        top_ = target_.top;
        air_ = target_.air;
        squash_ = target_.squash;
        design();
    }
    void setParams(const AcousticParams& p) {
        if (!ready_) {
            target_ = p;
            amount_ = p.on ? 1.0 : 0.0;
            body_ = p.body;
            top_ = p.top;
            air_ = p.air;
            squash_ = p.squash;
            ready_ = true;
            design();
            return;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        // The knobs glide: one step per block, with the time constant of 20 ms.
        const double kBlock = 1.0 - std::exp(-double(n) / (0.02 * fs_));
        const bool typeChanged = target_.type != type_;
        if (typeChanged || std::abs(target_.body - body_) > 1e-6 || std::abs(target_.top - top_) > 1e-6 || std::abs(target_.air - air_) > 1e-6) {
            body_ += (target_.body - body_) * kBlock;
            top_ += (target_.top - top_) * kBlock;
            air_ += (target_.air - air_) * kBlock;
            design();
        }
        squash_ += (target_.squash - squash_) * kBlock;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const double kGain = 1.0 - std::exp(-1.0 / (0.002 * fs_));
        const double kAttack = 1.0 - std::exp(-1.0 / (0.003 * fs_)), kRelease = 1.0 - std::exp(-1.0 / (0.12 * fs_));
        const double ratio = 1.0 + 5.0 * std::clamp(squash_, 0.0, 1.0);
        const double slope = 1.0 - 1.0 / ratio;
        const double makeupDb = -kThresholdDb * slope * 0.4;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            const double dryL = l[i], dryR = r ? r[i] : 0.0;
            double wl = filter(0, dryL), wr = r ? filter(1, dryR) : 0.0;
            wl *= comp_;
            wr *= comp_;
            const double peak = std::max(std::abs(wl), std::abs(wr));
            env_ += (peak > env_ ? kAttack : kRelease) * (peak - env_);
            if (++count_ >= 16) {
                count_ = 0;
                const double levelDb = 20.0 * std::log10(env_ + 1e-9);
                const double over = std::max(0.0, levelDb - kThresholdDb);
                gainTarget_ = std::pow(10.0, (-over * slope + makeupDb * (squash_ > 0.001 ? 1.0 : 0.0)) / 20.0);
            }
            gain_ += (gainTarget_ - gain_) * kGain;
            wl *= gain_;
            wr *= gain_;
            l[i] = float(dryL + (wl - dryL) * amount_);
            if (r) r[i] = float(dryR + (wr - dryR) * amount_);
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kThresholdDb = -26.0;
    static constexpr int kModes = 6;
    static constexpr int kStages = 1 + kModes + 2;  // high-pass, the modes, top shelf, air shelf

    struct Mode {
        double hz, q, db;
    };
    struct Coef {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };

    static const std::array<Mode, kModes>& modes(int type) {
        static const std::array<std::array<Mode, kModes>, 4> t{{
            {{{95, 3.5, 5.0}, {200, 2.5, 4.0}, {310, 3.0, 2.5}, {480, 1.4, -3.5}, {1800, 1.0, 2.0}, {3600, 1.1, 4.0}}},    // dreadnought
            {{{85, 3.5, 6.0}, {170, 2.5, 4.5}, {260, 3.0, 3.0}, {400, 1.4, -2.5}, {2200, 1.0, 2.5}, {4500, 1.1, 3.0}}},    // jumbo
            {{{140, 3.5, 3.5}, {260, 2.5, 4.0}, {400, 3.0, 3.0}, {620, 1.4, -2.5}, {1600, 1.0, 3.0}, {3000, 1.1, 4.5}}},   // parlor
            {{{100, 3.0, 2.0}, {250, 2.0, -2.0}, {850, 1.2, -5.0}, {2400, 1.0, 2.0}, {3800, 1.1, 6.0}, {7000, 1.0, 3.0}}}  // piezo
        }};
        return t[std::size_t(std::clamp(type, 0, 3))];
    }

    Coef peaking(double hz, double q, double db) const {
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * kPi * std::min(hz, 0.45 * fs_) / fs_;
        const double alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        const double a0 = 1.0 + alpha / A;
        return {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0};
    }
    Coef highpass(double hz) const {
        const double w = 2.0 * kPi * hz / fs_, alpha = std::sin(w) / (2.0 * 0.7071), c = std::cos(w);
        const double a0 = 1.0 + alpha;
        return {(1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0};
    }
    Coef highShelf(double hz, double db) const {
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * kPi * std::min(hz, 0.45 * fs_) / fs_;
        const double c = std::cos(w), alpha = std::sin(w) / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * c + sq;
        return {A * ((A + 1.0) + (A - 1.0) * c + sq) / a0, -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0, A * ((A + 1.0) + (A - 1.0) * c - sq) / a0,
                2.0 * ((A - 1.0) - (A + 1.0) * c) / a0, ((A + 1.0) - (A - 1.0) * c - sq) / a0};
    }
    static double magnitudeDb(const Coef& k, double w) {
        using C = std::complex<double>;
        const C z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        return 20.0 * std::log10(std::abs((k.b0 + k.b1 * z1 + k.b2 * z2) / (1.0 + k.a1 * z1 + k.a2 * z2)));
    }

    // Recomputes the filters for the current (gliding) knobs, and the gain that keeps the level.
    void design() {
        type_ = target_.type;
        const auto& m = modes(type_);
        const double b = std::clamp(body_, 0.0, 1.0) * 2.6;  // the modes at full Body reach 2.6 times the table (it was 1.6: the effect was too mild)
        coef_[0] = highpass(type_ == 3 ? 50.0 : 70.0);
        for (int i = 0; i < kModes; ++i) coef_[std::size_t(1 + i)] = peaking(m[std::size_t(i)].hz, m[std::size_t(i)].q * 0.75, m[std::size_t(i)].db * b);
        coef_[std::size_t(1 + kModes)] = highShelf(3500.0, (std::clamp(top_, 0.0, 1.0) - 0.5) * 16.0);
        coef_[std::size_t(2 + kModes)] = highShelf(9000.0, std::clamp(air_, 0.0, 1.0) * 8.0);
        // The average gain of the shaping filters over the guitar's range (80 Hz to 6 kHz, even on a log scale).
        double sum = 0.0;
        constexpr int kPoints = 24;
        for (int p = 0; p < kPoints; ++p) {
            const double hz = 80.0 * std::pow(6000.0 / 80.0, double(p) / double(kPoints - 1)), w = 2.0 * kPi * hz / fs_;
            for (int s = 1; s < kStages; ++s) sum += magnitudeDb(coef_[std::size_t(s)], w);
        }
        comp_ = std::pow(10.0, -0.8 * sum / double(kPoints) / 20.0);
    }

    double filter(int ch, double x) {
        for (int s = 0; s < kStages; ++s) {
            const Coef& k = coef_[std::size_t(s)];
            auto& z = z_[std::size_t(ch)][std::size_t(s)];
            const double y = k.b0 * x + z[0];
            z[0] = k.b1 * x - k.a1 * y + z[1];
            z[1] = k.b2 * x - k.a2 * y;
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0;
    bool ready_ = false;
    AcousticParams target_;
    int type_ = 0;
    double body_ = 0.6, top_ = 0.5, air_ = 0.4, squash_ = 0.4, amount_ = 0.0;
    double comp_ = 1.0, env_ = 0.0, gain_ = 1.0, gainTarget_ = 1.0;
    int count_ = 0;
    std::array<Coef, kStages> coef_{};
    std::array<std::array<std::array<double, 2>, kStages>, 2> z_{};
};

}  // namespace sas
