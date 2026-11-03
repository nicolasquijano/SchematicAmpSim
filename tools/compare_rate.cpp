// What would running the POWER amp at the host rate (48 kHz) instead of at 2x (96 kHz) do to the sound and to the cost?
//   reference: the Normal mode as it is: 2x oversampling, preamp and power amp both at 96 kHz.
//   candidate: the preamp at 96 kHz, its output decimated to 48 kHz by the same half-band filter the chain uses, and the power amp at 48 kHz.
// Both take the same 48 kHz input and give a 48 kHz output with the same latency (one up and one down half-band).
// Reports per amp and case: diff (rms of candidate - reference re the reference, dB), level, the largest third-octave band difference, the
// aliasing floor of a pure 997 Hz tone through each (power that is NOT at a harmonic of the tone, re the total, dB: lower is cleaner), and
// the CPU of each as % of a core.  SAS_BENCH_AMP=<id> runs one amp.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/oversample.h"

namespace {

constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

std::vector<double> multiSine(double amp, std::size_t n) {
    std::uint64_t state = 0x123456789abcdef0ull;
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = double(state >> 11) / double(1ull << 53) - 0.5;
        const double t = double(i) / kFs;
        const double s = 0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t);
        v[i] = amp * (s + 0.02 * noise);
    }
    return v;
}

std::vector<double> tone(double amp, std::size_t n, double hz) {
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * std::sin(2 * kPi * hz * double(i) / kFs);
    return v;
}

void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2.0 * kPi / double(len));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> wk = 1.0;
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                wk *= w;
            }
        }
    }
}

std::vector<double> bands(const std::vector<double>& x, std::size_t from) {
    constexpr std::size_t N = 8192;
    std::vector<double> psd(N / 2 + 1, 0.0);
    for (std::size_t s = from; s + N <= x.size(); s += N / 2) {
        std::vector<std::complex<double>> a(N);
        for (std::size_t i = 0; i < N; ++i) a[i] = x[s + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(N)));
        fft(a);
        for (std::size_t k = 0; k <= N / 2; ++k) psd[k] += std::norm(a[k]);
    }
    std::vector<double> out;
    for (double fc = 50.0; fc <= 16000.0; fc *= std::pow(2.0, 1.0 / 3.0)) {
        const double lo = fc / std::pow(2.0, 1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
        double p = 0.0;
        for (std::size_t k = std::size_t(std::ceil(lo / kFs * N)); k <= std::size_t(hi / kFs * N) && k <= N / 2; ++k) p += psd[k];
        out.push_back(p);
    }
    return out;
}

// Power outside +-6 bins of every harmonic of f0 (Blackman-Harris window so the leakage is far below what is measured), re the total, dB.
double aliasFloorDb(const std::vector<double>& x, std::size_t from, double f0) {
    constexpr std::size_t N = 1 << 17;
    if (x.size() < from + N) return 0.0;
    std::vector<std::complex<double>> a(N);
    for (std::size_t i = 0; i < N; ++i) {
        const double w = 0.35875 - 0.48829 * std::cos(2 * kPi * double(i) / double(N - 1)) + 0.14128 * std::cos(4 * kPi * double(i) / double(N - 1)) -
                         0.01168 * std::cos(6 * kPi * double(i) / double(N - 1));
        a[i] = x[from + i] * w;
    }
    fft(a);
    std::vector<char> harmonic(N / 2 + 1, 0);
    for (int k = 1; f0 * k < kFs / 2; ++k) {
        const long bin = long(std::lround(f0 * k / kFs * double(N)));
        for (long b = bin - 6; b <= bin + 6; ++b)
            if (b >= 0 && b <= long(N / 2)) harmonic[std::size_t(b)] = 1;
    }
    double total = 0.0, spur = 0.0;
    for (std::size_t k = 3; k <= N / 2; ++k) {  // skip DC
        const double p = std::norm(a[k]);
        total += p;
        if (!harmonic[k]) spur += p;
    }
    return 10.0 * std::log10(spur / total + 1e-30);
}

struct Run {
    std::vector<double> out;
    double percent = 0.0;
};

struct Rig {
    const sas::AmpDef& def;
    std::vector<double> knobs;
    double volume, master;
};

// The Normal mode as it is (2x, both at 96 kHz), through the chain's oversampler.
Run runReference(const sas::AmpDef& def, const std::vector<double>& knobs, const std::vector<double>& in) {
    sas::FullAmp amp(def, kFs * 2.0, false, true);
    amp.setKnobs(knobs, def.power.presenceDefault);
    sas::Oversampler os(1);
    Run r;
    r.out.resize(in.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < in.size(); ++i) r.out[i] = double(os.process(float(in[i]), [&](float v) { return float(amp.process(double(v))); }));
    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
    r.percent = 100.0 * dt.count() / (double(in.size()) / kFs);
    return r;
}

// The preamp at 96 kHz, decimated to 48 kHz, the power amp at 48 kHz.
Run runCandidate(const sas::AmpDef& def, const std::vector<double>& knobs, const std::vector<double>& in) {
    sas::PowerAmp power(def, kFs, true);
    sas::Preamp pre(def, kFs * 2.0, power.railX(), true, false);
    pre.setKnobs(knobs);
    if (std::abs(power.presence() - def.power.presenceDefault) > 1e-9) power.setPresence(def.power.presenceDefault);
    sas::HalfbandStage up(23), down(23);  // the same filters the chain's oversampler uses
    Run r;
    r.out.resize(in.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < in.size(); ++i) {
        const auto ab = up.up(float(in[i]));
        pre.setRail(power.railX());
        const float pa = float(pre.process(double(ab[0])));
        pre.setRail(power.railX());
        const float pb = float(pre.process(double(ab[1])));
        r.out[i] = power.process(double(down.down(pa, pb)));
    }
    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
    r.percent = 100.0 * dt.count() / (double(in.size()) / kFs);
    return r;
}

// Everything at the host rate, no oversampling at all (SAS_MODE=x1).
Run runX1(const sas::AmpDef& def, const std::vector<double>& knobs, const std::vector<double>& in) {
    sas::FullAmp amp(def, kFs, false, true);
    amp.setKnobs(knobs, def.power.presenceDefault);
    Run r;
    r.out.resize(in.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < in.size(); ++i) r.out[i] = amp.process(in[i]);
    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
    r.percent = 100.0 * dt.count() / (double(in.size()) / kFs);
    return r;
}

}  // namespace

int main() {
    const std::size_t n = std::size_t(kFs * 3.0), skip = std::size_t(kFs * 0.4);
    struct Case { const char* name; double volume, master, amp; };
    const Case cases[] = {{"clean", 0.2, 0.5, 0.05}, {"crunch", 0.6, 0.5, 0.15}, {"lead", 1.0, 0.8, 0.12}, {"lead-hot", 1.0, 0.8, 0.3}};
    const char* only = std::getenv("SAS_BENCH_AMP");
    std::printf("%-16s %-9s %8s %7s %7s | %9s %9s | %8s %8s\n", "amp", "case", "diff dB", "level", "bands", "alias ref", "alias new", "ref CPU", "new CPU");
    for (const auto& id : sas::embeddedAmpIds()) {
        if (only && id != only) continue;
        const sas::AmpDef def = sas::loadEmbeddedAmp(id);
        for (const auto& c : cases) {
            std::vector<double> knobs;
            for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
            knobs[0] = c.volume;
            for (std::size_t i = 0; i < knobs.size(); ++i)
                if (def.preamp.knobs[i].name == "master") knobs[i] = c.master;
            const auto in = multiSine(c.amp, n);
            const bool x1 = std::getenv("SAS_MODE") && std::string(std::getenv("SAS_MODE")) == "x1";
            const Run ref = runReference(def, knobs, in), cand = x1 ? runX1(def, knobs, in) : runCandidate(def, knobs, in);
            double ea = 0, eb = 0, ed = 0;
            // The candidate may be a sample or two late or early (the power amp's own step): take the best alignment and say which.
            int bestLag = 0;
            double bestD = 1e300;
            for (int lag = -3; lag <= 3; ++lag) {
                double d = 0;
                for (std::size_t i = skip; i + 4 < n; ++i) {
                    const double x = cand.out[std::size_t(long(i) + lag)] - ref.out[i];
                    d += x * x;
                }
                if (d < bestD) { bestD = d; bestLag = lag; }
            }
            for (std::size_t i = skip; i + 4 < n; ++i) {
                const double rv = ref.out[i], cv = cand.out[std::size_t(long(i) + bestLag)];
                ea += rv * rv;
                eb += cv * cv;
                ed += (cv - rv) * (cv - rv);
            }
            const auto ba = bands(ref.out, skip), bb = bands(cand.out, skip);
            const double loud = *std::max_element(ba.begin(), ba.end());
            double worst = 0.0;
            for (std::size_t k = 0; k < ba.size(); ++k)
                if (ba[k] > loud * 1e-6) worst = std::max(worst, std::fabs(10.0 * std::log10(bb[k] / ba[k])));
            // The pure-tone test, for the aliasing floor.
            const std::size_t nt = std::size_t(kFs * 4.0);
            const auto tin = tone(c.amp, nt, 997.0);
            const Run tref = runReference(def, knobs, tin), tcand = x1 ? runX1(def, knobs, tin) : runCandidate(def, knobs, tin);
            std::printf("%-16s %-9s %8.1f %7.3f %7.3f | %9.1f %9.1f | %7.1f%% %7.1f%%\n", id.c_str(), c.name, 10.0 * std::log10(ed / ea + 1e-30),
                        10.0 * std::log10(eb / ea), worst, aliasFloorDb(tref.out, std::size_t(kFs * 0.5), 997.0),
                        aliasFloorDb(tcand.out, std::size_t(kFs * 0.5), 997.0), ref.percent, cand.percent);
            std::fflush(stdout);
        }
    }
}
