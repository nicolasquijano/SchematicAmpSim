// sas_leslie_spring_check: for the Leslie, the level of each channel in 50 ms blocks for a 2 kHz tone (the horn), stopped then slow then fast; for the spring
// reverb, the rms of 100 ms blocks after a 0.2 s noise burst, per tank, and the wet level against the dry one.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "sas/leslie.h"
#include "sas/spring_reverb.h"

int main() {
    constexpr double fs = 48000.0, pi = 3.14159265358979323846;
    {
        sas::Leslie les;
        les.prepare(fs);
        for (int speed : {1, 2}) {
            sas::LeslieParams p;
            p.on = true;
            p.speed = speed;
            les.setParams(p);
            std::printf("Leslie speed %d, 2 kHz tone, L / R rms (dB) per 100 ms from t = 6 s:", speed);
            std::vector<float> l(std::size_t(fs * 6.0)), r(l.size());
            std::size_t t0 = 0;
            for (int b = 0; b < 12; ++b) {
                std::vector<float> x(std::size_t(0.1 * fs)), y(x.size());
                for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(0.1 * std::sin(2 * pi * 2000.0 * double(t0 + i) / fs));
                t0 += x.size();
                les.process(x.data(), y.data(), x.size());
                if (b >= 0) {
                    double a = 0, c = 0;
                    for (std::size_t i = 0; i < x.size(); ++i) { a += double(x[i]) * x[i]; c += double(y[i]) * y[i]; }
                    std::printf(" %.0f/%.0f", 10 * std::log10(a / double(x.size()) / 0.005), 10 * std::log10(c / double(x.size()) / 0.005));
                }
            }
            std::printf("\n");
        }
    }
    {
        sas::SpringReverb sp;
        sp.prepare(fs);
        for (int type = 0; type < 3; ++type) {
            sas::SpringReverbParams p;
            p.on = true;
            p.type = type;
            p.mix = 1.0;
            sp.setParams(p);
            std::printf("Spring tank %d, rms x1000 per 100 ms after a 0.2 s noise burst:", type);
            std::uint32_t seed = 99;
            for (int b = 0; b < 16; ++b) {
                std::vector<float> l(std::size_t(0.1 * fs)), r(l.size());
                for (std::size_t i = 0; i < l.size(); ++i) {
                    seed = seed * 1664525u + 1013904223u;
                    l[i] = b < 2 ? float((double(seed >> 8) / double(1u << 24) - 0.5) * 0.4) : 0.0f;
                    r[i] = l[i];
                }
                sp.process(l.data(), r.data(), l.size());
                double s = 0;
                for (float v : l) s += double(v) * v;
                std::printf(" %.0f", 1000 * std::sqrt(s / double(l.size())));
            }
            std::printf("\n");
        }
    }
    return 0;
}
