#pragma once
// The graphic equaliser of an amp (amp.json "graphic_eq"): a few fixed bands, each a peaking filter whose boost or cut is set by a slider (0..1, 0.5 flat,
// up to +-`range_db`). It sits at the output of the amp, before the speaker; on the amp it comes from it is in front of the power stage, which for a
// linear shaping like this gives nearly the same tone. Gains glide over 20 ms; with every slider flat it costs nothing.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "sas/amp_def.h"

namespace sas {

class GraphicEq {
public:
    static constexpr std::size_t kMax = 8;

    void prepare(const GraphicEqDef& def, double fs) {
        fs_ = fs;
        bands_ = std::min(def.bands.size(), kMax);
        for (std::size_t i = 0; i < bands_; ++i) def_[i] = def.bands[i];
        reset();
    }
    void reset() {
        for (auto& z : z_) z = {0.0, 0.0};
        cur_.fill(0.0);
        redesign();
    }
    // Sliders 0..1 with 0.5 flat, in the order of the bands.
    void setSliders(const std::array<double, kMax>& s) {
        for (std::size_t i = 0; i < bands_; ++i) target_[i] = (std::clamp(s[i], 0.0, 1.0) - 0.5) * 2.0 * def_[i].rangeDb;
    }
    bool active() const { return flat_ == false; }

    double process(double x) {
        if (bands_ == 0) return x;
        if (++count_ >= 32) {
            count_ = 0;
            const double k = 1.0 - std::exp(-32.0 / (0.02 * fs_));
            bool moved = false;
            for (std::size_t i = 0; i < bands_; ++i) {
                const double d = target_[i] - cur_[i];
                if (std::abs(d) > 1e-4) {
                    cur_[i] += d * k;
                    moved = true;
                }
            }
            if (moved) redesign();
        }
        if (flat_) return x;
        for (std::size_t i = 0; i < bands_; ++i) {
            const Coef& c = coef_[i];
            auto& z = z_[i];
            const double y = c.b0 * x + z[0];
            z[0] = c.b1 * x - c.a1 * y + z[1];
            z[1] = c.b2 * x - c.a2 * y;
            x = y;
        }
        return x;
    }

private:
    struct Coef {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    void redesign() {
        flat_ = true;
        for (std::size_t i = 0; i < bands_; ++i) {
            if (std::abs(cur_[i]) < 0.01) {
                coef_[i] = {};
                continue;
            }
            flat_ = false;
            const double A = std::pow(10.0, cur_[i] / 40.0), w = 2.0 * 3.14159265358979323846 * std::min(def_[i].hz, 0.45 * fs_) / fs_;
            const double alpha = std::sin(w) / (2.0 * def_[i].q), c = std::cos(w), a0 = 1.0 + alpha / A;
            coef_[i] = {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0};
        }
    }

    double fs_ = 48000.0;
    std::size_t bands_ = 0;
    std::array<GraphicEqBand, kMax> def_{};
    std::array<double, kMax> cur_{}, target_{};
    std::array<Coef, kMax> coef_{};
    std::array<std::array<double, 2>, kMax> z_{};
    bool flat_ = true;
    int count_ = 0;
};

}  // namespace sas
