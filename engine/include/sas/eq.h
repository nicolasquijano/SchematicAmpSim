#pragma once
// Post equaliser: a high-pass, a low-pass and five bands (low shelf, three bells, high shelf), RBJ biquads on the stereo pair after the
// cabinet. Everything that is not moved is bypassed, so a flat EQ costs nothing and changes nothing. The parameters glide (15 ms)
// so dragging a node does not zip. `responseDb` is the same maths for drawing the curve.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct EqBand {
    double freq = 1000.0, gainDb = 0.0, q = 0.7;
};

struct EqParams {
    static constexpr int kBands = 5;
    bool on = false;
    double hpfHz = 20.0, lpfHz = 20000.0;  // at their ends the filters are off
    std::array<EqBand, kBands> band{{{100.0, 0.0, 0.7}, {250.0, 0.0, 1.0}, {1000.0, 0.0, 1.0}, {3500.0, 0.0, 1.0}, {6000.0, 0.0, 0.7}}};

    static constexpr double kHpfOff = 20.0, kLpfOff = 20000.0;
    bool flat() const {
        if (!on) return true;
        for (const auto& b : band) if (std::fabs(b.gainDb) > 1e-3) return false;
        return hpfHz <= kHpfOff + 1e-6 && lpfHz >= kLpfOff - 1e-6;
    }
};

class ParametricEq {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        cur_ = target_;
        for (auto& ch : state_) for (auto& s : ch) s = {};
        for (auto& f : filters_) f = {};
        coefficients();
    }

    void setParams(const EqParams& p) { target_ = p; }
    bool active() const { return !target_.flat() || !cur_.flat(); }

    // Processes both channels (n samples). Call with the same n for every block you want the glide to follow.
    void process(float* l, float* r, std::size_t n) {
        if (!active()) {
            cur_ = target_;
            return;
        }
        const double k = 1.0 - std::exp(-double(n) / (0.015 * fs_));
        auto glide = [k](double& a, double b) { a += (b - a) * k; };
        cur_.on = target_.on;
        glide(cur_.hpfHz, target_.hpfHz);
        glide(cur_.lpfHz, target_.lpfHz);
        for (int i = 0; i < EqParams::kBands; ++i) {
            // Frequencies glide in the log domain.
            cur_.band[i].freq = std::exp(std::log(cur_.band[i].freq) + (std::log(target_.band[i].freq) - std::log(cur_.band[i].freq)) * k);
            glide(cur_.band[i].gainDb, target_.band[i].gainDb);
            glide(cur_.band[i].q, target_.band[i].q);
        }
        coefficients();
        for (int f = 0; f < kFilters; ++f) {
            if (!used_[std::size_t(f)]) continue;
            const Coef& c = filters_[std::size_t(f)];
            run(c, state_[0][std::size_t(f)], l, n);
            run(c, state_[1][std::size_t(f)], r, n);
        }
    }

    // Magnitude of the whole chain (of the target parameters, the picture the user is editing) at `hz`, in dB.
    double responseDb(double hz) const {
        Coef c[kFilters];
        bool used[kFilters];
        design(target_, c, used);
        double db = 0.0;
        for (int f = 0; f < kFilters; ++f)
            if (used[f]) db += magnitudeDb(c[f], hz, fs_);
        return db;
    }

private:
    static constexpr int kFilters = EqParams::kBands + 2;  // bands, then the high-pass and the low-pass
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };
    static constexpr double kPi = 3.14159265358979323846;

    static void run(const Coef& c, State& s, float* x, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const double in = double(x[i]);
            const double out = c.b0 * in + s.z1;
            s.z1 = c.b1 * in - c.a1 * out + s.z2;
            s.z2 = c.b2 * in - c.a2 * out;
            x[i] = float(out);
        }
    }

    static Coef normalise(double b0, double b1, double b2, double a0, double a1, double a2) { return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0}; }

    static Coef peak(double fs, double f, double gainDb, double q) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs, alpha = std::sin(w) / (2.0 * q), cw = std::cos(w);
        return normalise(1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }
    static Coef lowShelf(double fs, double f, double gainDb, double q) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs, alpha = std::sin(w) / (2.0 * q), cw = std::cos(w), sa = 2.0 * std::sqrt(A) * alpha;
        return normalise(A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa),
                         (A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa);
    }
    static Coef highShelf(double fs, double f, double gainDb, double q) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs, alpha = std::sin(w) / (2.0 * q), cw = std::cos(w), sa = 2.0 * std::sqrt(A) * alpha;
        return normalise(A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa),
                         (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa);
    }
    static Coef highPass(double fs, double f) {
        const double w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs, alpha = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w);
        return normalise((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
    static Coef lowPass(double fs, double f) {
        const double w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs, alpha = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w);
        return normalise((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void design(const EqParams& p, Coef* c, bool* used) const {
        for (int i = 0; i < EqParams::kBands; ++i) {
            const EqBand& b = p.band[i];
            used[i] = p.on && std::fabs(b.gainDb) > 1e-3;
            c[i] = Coef{};
            if (!used[i]) continue;
            c[i] = i == 0 ? lowShelf(fs_, b.freq, b.gainDb, b.q) : i == EqParams::kBands - 1 ? highShelf(fs_, b.freq, b.gainDb, b.q) : peak(fs_, b.freq, b.gainDb, b.q);
        }
        used[EqParams::kBands] = p.on && p.hpfHz > EqParams::kHpfOff + 1e-6;
        c[EqParams::kBands] = used[EqParams::kBands] ? highPass(fs_, p.hpfHz) : Coef{};
        used[EqParams::kBands + 1] = p.on && p.lpfHz < EqParams::kLpfOff - 1e-6;
        c[EqParams::kBands + 1] = used[EqParams::kBands + 1] ? lowPass(fs_, p.lpfHz) : Coef{};
    }

    void coefficients() {
        bool used[kFilters];
        Coef c[kFilters];
        design(cur_, c, used);
        for (int f = 0; f < kFilters; ++f) {
            filters_[std::size_t(f)] = c[f];
            if (used[f] != used_[std::size_t(f)] && !used[f]) state_[0][std::size_t(f)] = state_[1][std::size_t(f)] = {};  // a filter that switches off starts clean
            used_[std::size_t(f)] = used[f];
        }
    }

    static double magnitudeDb(const Coef& c, double hz, double fs) {
        const double w = 2.0 * kPi * hz / fs, cw = std::cos(w), sw = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = c.b0 + c.b1 * cw + c.b2 * c2, ni = -(c.b1 * sw + c.b2 * s2);
        const double dr = 1.0 + c.a1 * cw + c.a2 * c2, di = -(c.a1 * sw + c.a2 * s2);
        return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di + 1e-300) + 1e-300);
    }

    double fs_ = 48000.0;
    EqParams target_, cur_;
    std::array<Coef, kFilters> filters_{};
    std::array<bool, kFilters> used_{};
    std::array<std::array<State, kFilters>, 2> state_{};
};

}  // namespace sas
