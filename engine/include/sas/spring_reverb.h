#pragma once
// A spring reverb as a module of the rack: the behavioural spring tank of the amps (sas/spring.h: two springs, each a delay with a chain of all-pass sections that
// disperses the lows and a loss filter, so the "drip" and the "boing") with a left and a right tank of slightly different lengths, so the pair is wider than the
// mono tank of an amp. Three tanks: Short (a small combo's two short springs), Medium (the springs of a 1960s combo) and Long (a three-spring tank with a long, dark
// decay). Tone is a low-pass on the wet signal (1.5 to 8 kHz); Mix is how much of the tank is added to the dry signal. The tank gains are normalised so that
// Mix at 1 adds a reverb about 6 dB below the dry level. Works on a stereo pair. Switching it on or off fades over 20 ms (the tail does not ring out).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <memory>

#include "sas/amp_def.h"
#include "sas/spring.h"

namespace sas {

struct SpringReverbParams {
    bool on = false;
    int type = 1;        // 0 short, 1 medium, 2 long
    double tone = 0.5;   // 0 .. 1
    double mix = 0.3;    // 0 .. 1
};

class SpringReverb {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (int t = 0; t < 3; ++t)
            for (int c = 0; c < 2; ++c) {
                // The tank's own gain is unity first; the measured level of noise through it then sets the real one.
                ReverbDef d = def(t, c);
                d.gain = 1.0;
                SpringTank probe(d, fs);
                double in = 0.0, out = 0.0;
                std::uint32_t seed = 12345u + std::uint32_t(t * 7 + c);
                const std::size_t n = std::size_t(1.2 * fs);
                for (std::size_t i = 0; i < n; ++i) {
                    seed = seed * 1664525u + 1013904223u;
                    const double x = (double(seed >> 8) / double(1u << 24) - 0.5) * 0.2;
                    const double y = probe.process(x);
                    if (i > n / 3) {
                        in += x * x;
                        out += y * y;
                    }
                }
                d.gain = out > 1e-18 ? 0.5 * std::sqrt(in / out) : 1.0;
                tank_[std::size_t(t)][std::size_t(c)] = std::make_unique<SpringTank>(d, fs);
            }
        running_ = false;
        ready_ = false;
        reset();
    }
    void reset() {
        for (auto& row : tank_)
            for (auto& t : row)
                if (t) t->reset();
        lp_.fill(0.0);
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const SpringReverbParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    // l is always processed; r only when given (a stereo pair).
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const int type = std::clamp(target_.type, 0, 2);
        if (!running_ || type != type_) {
            for (auto& t : tank_[std::size_t(type)])
                if (t) t->reset();
            lp_.fill(0.0);
            type_ = type;
            running_ = true;
        }
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const double fc = 1500.0 * std::pow(5.3, std::clamp(target_.tone, 0.0, 1.0));
        const double aLp = 1.0 - std::exp(-6.283185307179586 * std::min(fc, 0.45 * fs_) / fs_);
        const double mix = std::clamp(target_.mix, 0.0, 1.0);
        auto& tl = *tank_[std::size_t(type)][0];
        auto& tr = *tank_[std::size_t(type)][1];
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            const double inL = l[i], inR = r ? r[i] : inL;
            const double mono = r ? 0.5 * (inL + inR) : inL;
            lp_[0] += aLp * (tl.process(mono) - lp_[0]);
            l[i] = float(inL + lp_[0] * mix * amount_);
            if (r) {
                lp_[1] += aLp * (tr.process(mono) - lp_[1]);
                r[i] = float(inR + lp_[1] * mix * amount_);
            }
        }
        if (!target_.on && amount_ < 1e-4) running_ = false;
    }

private:
    static ReverbDef def(int type, int channel) {
        ReverbDef d;
        d.present = true;
        const double w = channel == 0 ? 1.0 : 1.09;  // the right tank is a little longer
        switch (type) {
            case 0:
                d.springs = {{22.0 * w, 60, -0.5, 0.84, 4000.0}, {28.0 * w, 60, -0.5, 0.84, 4000.0}};
                break;
            case 2:
                d.springs = {{45.0 * w, 100, -0.5, 0.95, 3000.0}, {57.0 * w, 100, -0.5, 0.95, 3000.0}, {71.0 * w, 100, -0.5, 0.94, 2800.0}};
                break;
            default:
                d.springs = {{33.0 * w, 80, -0.5, 0.92, 3500.0}, {41.0 * w, 80, -0.5, 0.92, 3500.0}};
                break;
        }
        return d;
    }

    double fs_ = 48000.0, amount_ = 0.0;
    bool ready_ = false, running_ = false;
    int type_ = 1;
    SpringReverbParams target_;
    std::array<std::array<std::unique_ptr<SpringTank>, 2>, 3> tank_;
    std::array<double, 2> lp_{};
};

}  // namespace sas
