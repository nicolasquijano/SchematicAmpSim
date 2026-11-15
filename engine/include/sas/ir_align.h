#pragma once
// Puts two impulse responses in phase. Two microphones in front of the same speaker hear it at slightly different moments (and a
// microphone behind the cone, or a cable wired the other way round, hears it upside down): mixed, the two copies of a low note partly cancel.
// alignImpulses() looks at the cross-correlation of the two impulses for the time offset and the polarity that make them add up, so the
// caller can delay the earlier one and flip the other.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sas {

struct IrAlignment {
    int delayA = 0;         // samples to delay A by (zeros in front)
    int delayB = 0;         // samples to delay B by
    bool invertB = false;   // B upside down
    double correlation = 0.0;  // normalised correlation after the alignment (1 = identical shapes)
};

// a, b: the two impulses at the same sample rate. maxLagSamples: the largest offset to look for (about 3 ms: microphones a metre apart).
// Only the first `windowSamples` of each (the direct sound, before the room) take part.
inline IrAlignment alignImpulses(const float* a, std::size_t na, const float* b, std::size_t nb, int maxLagSamples, std::size_t windowSamples) {
    IrAlignment out;
    const std::size_t n = std::min({na, nb, windowSamples});
    if (n < 8 || maxLagSamples < 1) return out;
    double ea = 0.0, eb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ea += double(a[i]) * a[i];
        eb += double(b[i]) * b[i];
    }
    if (!(ea > 0.0) || !(eb > 0.0)) return out;
    // c(lag) = sum a[i] * b[i + lag]: positive lag = B arrives after A.
    double best = 0.0;
    int bestLag = 0;
    for (int lag = -maxLagSamples; lag <= maxLagSamples; ++lag) {
        double c = 0.0;
        const std::size_t from = lag < 0 ? std::size_t(-lag) : 0;
        for (std::size_t i = from; i < n; ++i) {
            const std::size_t j = std::size_t(std::ptrdiff_t(i) + lag);
            if (j >= n) break;
            c += double(a[i]) * b[j];
        }
        if (std::abs(c) > std::abs(best)) {
            best = c;
            bestLag = lag;
        }
    }
    out.invertB = best < 0.0;
    if (bestLag > 0) out.delayA = bestLag;   // B is late: hold A back
    else out.delayB = -bestLag;              // B is early: hold B back
    out.correlation = std::abs(best) / std::sqrt(ea * eb);
    return out;
}

}  // namespace sas
