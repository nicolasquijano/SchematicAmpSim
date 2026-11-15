#pragma once
// The multi impulse loader (cabinet mode MULTI): up to eight impulse responses mixed with a cursor on a plane. Each impulse sits at a point of the
// unit square; the closer the cursor, the more of that impulse is heard, and the total power stays the same wherever the cursor is. Each
// impulse also has its own polarity, delay and high / low-pass, which are baked into it when it is loaded (so they cost nothing while playing).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "sas/cab.h"
#include "sas/ir_align.h"

namespace sas {

inline constexpr int kMultiIrSlots = 8;

// Where the impulses sit when a project does not say: on a ring around the centre, so every one is reachable.
inline void multiIrDefaultPosition(int slot, float& x, float& y) {
    constexpr float pi = 3.14159265358979323846f;
    const float a = 2.0f * pi * (float(slot) + 0.5f) / float(kMultiIrSlots);
    x = 0.5f + 0.38f * std::cos(a);
    y = 0.5f + 0.38f * std::sin(a);
}

// The weight of every impulse for a cursor at (cx, cy): a bell around each point (the loaded ones only, `gain` is 0 for the others), scaled so
// the weights squared add up to 1. A weight under `floor` after that is cut to 0 (the convolution of that impulse is not run at all).
// `spread` is the width of the bell in units of the square (0.2 makes the neighbours of a point fade out about half way to the next one).
inline void multiIrWeights(float cx, float cy, const float* xs, const float* ys, const float* gain, int n, float spread, float floor, float* out) {
    double sum2 = 0.0;
    int nearest = -1;
    double nearestD2 = 1e30;
    const double two = 2.0 * double(spread) * double(spread);
    for (int k = 0; k < n; ++k) {
        out[k] = 0.0f;
        if (!(gain[k] > 0.0f)) continue;
        const double dx = double(cx) - xs[k], dy = double(cy) - ys[k];
        const double d2 = dx * dx + dy * dy;
        if (d2 < nearestD2) {
            nearestD2 = d2;
            nearest = k;
        }
        const double w = std::exp(-d2 / two);
        out[k] = float(w);
        sum2 += w * w;
    }
    if (nearest < 0) return;
    if (!(sum2 > 1e-30)) {  // so far from every point that all the bells are zero: the nearest one takes it
        out[nearest] = gain[nearest];
        return;
    }
    const double norm = 1.0 / std::sqrt(sum2);
    for (int k = 0; k < n; ++k) {
        float w = float(double(out[k]) * norm);
        if (w < floor) w = 0.0f;
        out[k] = w * gain[k];
    }
}

// Is the impulse upside down under this polarity setting? (0 = automatic: the biggest swing is negative; 1 = no; 2 = yes.)
inline bool impulseFlipped(const float* ir, std::size_t n, int polarity) {
    if (polarity == 2) return true;
    if (polarity != 0 || n == 0) return false;
    std::size_t at = 0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
        if (std::abs(ir[i]) > peak) {
            peak = std::abs(ir[i]);
            at = i;
        }
    return ir[at] < 0.0f;
}

struct IrBake {
    int polarity = 0;       // 0 = automatic (the biggest swing of the impulse is made positive), 1 = as it is, 2 = upside down
    double delayMs = 0.0;   // zeros in front, to line this impulse up with the others
    double hpHz = 0.0;      // 20 Hz or less = no high-pass
    double lpHz = 0.0;      // 0 or above 19.5 kHz = no low-pass
};

// The impulse as it will be convolved: polarity, then delay, then the filters (a 2nd-order high-pass and low-pass; a little silence is added at the
// end so the filters can ring out).
inline std::vector<float> bakeImpulse(const float* ir, std::size_t n, double rate, const IrBake& p) {
    std::vector<float> out;
    if (n == 0 || !(rate > 0.0)) return out;
    const bool flip = impulseFlipped(ir, n, p.polarity);
    const std::size_t delay = std::size_t(std::max(0.0, std::round(p.delayMs * rate / 1000.0)));
    const bool hp = p.hpHz > 20.5, lp = p.lpHz > 0.0 && p.lpHz < 19500.0 && p.lpHz < 0.45 * rate;
    const std::size_t tail = (hp || lp) ? std::size_t(rate * 0.02) : 0;
    out.assign(delay + n + tail, 0.0f);
    for (std::size_t i = 0; i < n; ++i) out[delay + i] = flip ? -ir[i] : ir[i];
    if (hp) {
        Biquad f = Biquad::highpass(float(rate), float(p.hpHz), 0.707f);
        for (auto& v : out) v = f.process(v);
    }
    if (lp) {
        Biquad f = Biquad::lowpass(float(rate), float(p.lpHz), 0.707f);
        for (auto& v : out) v = f.process(v);
    }
    return out;
}

// Lines all the loaded impulses up with the first one: for each, the polarity and the time offset that make it add up with the first (the
// cross-correlation of their direct sound, see alignImpulses), then every impulse is delayed so the latest one needs no delay at all. `irs[k]`
// must already have its own polarity and its leading silence taken care of. Impulses at a different sample rate than the first are left alone.
struct MultiAlignment {
    int extraDelay[kMultiIrSlots] = {};  // samples to delay each by
    bool flip[kMultiIrSlots] = {};       // upside down relative to the first
    int reference = -1;                  // the slot the others were lined up with (-1: fewer than two impulses)
};

inline MultiAlignment alignMultiImpulses(const float* const* irs, const std::size_t* lengths, const double* rates, const bool* loaded) {
    MultiAlignment out;
    int ref = -1;
    for (int k = 0; k < kMultiIrSlots; ++k)
        if (loaded[k] && lengths[k] > 0) {
            ref = k;
            break;
        }
    int late[kMultiIrSlots] = {};
    bool aligned[kMultiIrSlots] = {};
    int count = 0;
    if (ref >= 0) {
        aligned[ref] = true;
        count = 1;
        for (int k = ref + 1; k < kMultiIrSlots; ++k) {
            if (!loaded[k] || lengths[k] == 0 || rates[k] != rates[ref]) continue;
            const auto r = alignImpulses(irs[ref], lengths[ref], irs[k], lengths[k], int(rates[ref] * 0.003), std::size_t(rates[ref] * 0.05));
            late[k] = r.delayA - r.delayB;  // positive: this one arrives after the first
            out.flip[k] = r.invertB;
            aligned[k] = true;
            ++count;
        }
    }
    if (count < 2) return out;
    out.reference = ref;
    int latest = 0;
    for (int k = 0; k < kMultiIrSlots; ++k)
        if (aligned[k]) latest = std::max(latest, late[k]);
    for (int k = 0; k < kMultiIrSlots; ++k)
        if (aligned[k]) out.extraDelay[k] = latest - late[k];
    return out;
}

}  // namespace sas
