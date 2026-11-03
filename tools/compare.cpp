// Output comparison for speed work on the Normal mode: the same input through a REFERENCE amp (full circuits, device formulas, no LU
// reuse, every solver at a tolerance of 1e-9) and through the amp exactly as the Normal mode builds it, and how far apart they are:
//   diff   rms of (normal - reference) re the reference, dB (sample by sample; meaningful while the circuit is not chaotic)
//   level  rms of normal re reference, dB
//   bands  largest difference of the power in third-octave bands from 50 Hz to 16 kHz, dB (only bands within 60 dB of the loudest)
// and the solver work of each (Newton iterations per sample over all solvers, continuation runs, failures).
// Acceptance used in docs/PLAN_CPU.md: clean and crunch diff below -80 dB; lead (and every case) level within 0.1 dB, bands within 0.2 dB.
// SAS_BENCH_AMP=<id> runs one amp.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"

namespace {

constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;

std::vector<double> signal(double amp, std::size_t n) {
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

// Power in third-octave bands (Welch, Hann, 8192 points, half overlap).
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

struct Work {
    double itersPerSample = 0;
    unsigned long long continuations = 0, failures = 0;
};

template <class Amp>
Work work(Amp& a, std::size_t n) {
    Work w;
    std::uint64_t it = a.power().solver().newtonIterations, cont = a.power().solver().continuationRuns;
    for (std::size_t s = 0; s < a.pre().stageCount(); ++s) {
        it += a.pre().solver(s).newtonIterations;
        cont += a.pre().solver(s).continuationRuns;
    }
    w.itersPerSample = double(it) / double(n);
    w.continuations = cont;
    w.failures = a.newtonFailures();
    return w;
}

}  // namespace

// Experiments: SAS_PLAIN, SAS_FINAL, SAS_CONT_STAGE, SAS_CONT_BUDGET override the solver's work limits.
static void applyLimits(sas::Solver& s, bool power) {
    if (const char* v = std::getenv("SAS_PLAIN")) s.plainIterations = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_FINAL")) s.finalIterations = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_CONT_STAGE")) s.contStageIters = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_CONT_BUDGET")) s.contBudget = std::atoi(v);
    if (const char* v = std::getenv(power ? "SAS_TOL_PWR" : "SAS_TOL_PRE")) s.tolerance *= std::atof(v);  // a scale on the amp's own tolerance
}

int main() {
    const std::size_t n = std::size_t(kFs * 2.0), skip = std::size_t(kFs * 0.3);
    struct Case { const char* name; double volume, master, amp; };
    const Case cases[] = {{"clean", 0.2, 0.5, 0.05}, {"crunch", 0.6, 0.5, 0.15}, {"lead", 1.0, 0.8, 0.12}, {"lead-hot", 1.0, 0.8, 0.3}};
    const char* only = std::getenv("SAS_BENCH_AMP");
    std::printf("%-16s %-9s %9s %8s %8s | %22s | %22s\n", "amp", "case", "diff dB", "level dB", "bands dB", "reference it/s cont fail", "normal it/s cont fail");
    for (const auto& id : sas::embeddedAmpIds()) {
        if (only && id != only) continue;
        const sas::AmpDef def = sas::loadEmbeddedAmp(id);
        for (const auto& c : cases) {
            std::vector<double> knobs;
            for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
            knobs[0] = c.volume;
            for (std::size_t i = 0; i < knobs.size(); ++i)
                if (def.preamp.knobs[i].name == "master") knobs[i] = c.master;
            const auto in = signal(c.amp, n);

            sas::FullAmp ref(def, kFs);  // device formulas, no LU reuse
            for (std::size_t s = 0; s < ref.pre().stageCount(); ++s) ref.pre().solver(s).tolerance = 1e-9;
            ref.power().solver().tolerance = 1e-9;
            ref.setKnobs(knobs, def.power.presenceDefault);
            sas::FullAmp normal(def, kFs, false, true);  // exactly what the Normal mode builds (at 2x of a 48 kHz host)
            for (std::size_t s = 0; s < normal.pre().stageCount(); ++s) applyLimits(normal.pre().solver(s), false);
            applyLimits(normal.power().solver(), true);
            normal.setKnobs(knobs, def.power.presenceDefault);

            std::vector<double> a(n), b(n);
            for (std::size_t i = 0; i < n; ++i) {
                a[i] = ref.process(in[i]);
                b[i] = normal.process(in[i]);
            }
            double ea = 0, eb = 0, ed = 0;
            for (std::size_t i = skip; i < n; ++i) {
                ea += a[i] * a[i];
                eb += b[i] * b[i];
                ed += (b[i] - a[i]) * (b[i] - a[i]);
            }
            const double diff = 10.0 * std::log10(ed / ea + 1e-30), level = 10.0 * std::log10(eb / ea);
            const auto ba = bands(a, skip), bb = bands(b, skip);
            const double loud = *std::max_element(ba.begin(), ba.end());
            double worst = 0.0;
            for (std::size_t k = 0; k < ba.size(); ++k)
                if (ba[k] > loud * 1e-6) worst = std::max(worst, std::fabs(10.0 * std::log10(bb[k] / ba[k])));
            const Work wr = work(ref, n), wn = work(normal, n);
            std::printf("%-16s %-9s %9.1f %8.3f %8.3f | %7.2f %7llu %6llu | %7.2f %7llu %6llu\n", id.c_str(), c.name, diff, level, worst,
                        wr.itersPerSample, wr.continuations, wr.failures, wn.itersPerSample, wn.continuations, wn.failures);
        }
    }
}
