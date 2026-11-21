// A / B check of two amps' preamps (typically a preamp split into stages against the same circuit in fewer netlists, kept as another amp id):
// both run in the Normal mode (tables, 96 kHz) on the same guitar-like input, with the given selector settings and drive levels, and the tool prints
// the level difference, the worst third-octave band difference (50 Hz .. 16 kHz, bands within 60 dB of the loudest) and the waveform error.
//   sas_ab_check <amp A> <amp B> "selector=pos,...,jack=N" ...      (each argument after the two ids is one setting; "" = all selectors and the jack at 0)
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "sas/embedded_amps.h"
#include "sas/preamp.h"

namespace {
constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;

std::vector<std::size_t> parseSelectors(const sas::AmpDef& def, const std::string& text) {
    std::vector<std::size_t> v(def.preamp.selectors.size(), 0);
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos)
            for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
                if (def.preamp.selectors[i].name == item.substr(0, eq)) v[i] = std::size_t(std::atoi(item.c_str() + eq + 1));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return v;
}

// "jack=N" in a setting picks the jack by index (default 0).
std::size_t parseJack(const std::string& text) {
    const std::size_t at = text.find("jack=");
    return at == std::string::npos ? 0 : std::size_t(std::atoi(text.c_str() + at + 5));
}

// SAS_NODES="p1,p5,...": also print each node's swing (peak, DC removed) over the last second, to follow the gain through the stages.
std::vector<std::string> nodeList() {
    std::vector<std::string> v;
    if (const char* e = std::getenv("SAS_NODES")) {
        std::string s(e);
        for (std::size_t a = 0; a < s.size();) { std::size_t c = s.find(',', a); if (c == std::string::npos) c = s.size(); v.push_back(s.substr(a, c - a)); a = c + 1; }
    }
    return v;
}

std::vector<double> run(const sas::AmpDef& defIn, const std::vector<std::size_t>& sel, std::size_t jack, double peak, std::size_t n) {
    sas::AmpDef def = defIn;
    const auto nodes = nodeList();
    if (!nodes.empty()) { def.probes.clear(); for (const auto& nd : nodes) def.probes.push_back({nd, nd, 1.0}); }
    sas::Preamp pre(def, kFs, 400.0, true);
    pre.setJack(jack);
    for (std::size_t i = 0; i < def.preamp.knobs.size(); ++i) pre.setKnob(i, def.preamp.knobs[i].def);
    for (std::size_t i = 0; i < sel.size(); ++i) pre.setSelector(i, sel[i]);
    std::vector<double> out(n);
    std::uint64_t st = 0x123456789abcdef0ull;
    for (std::size_t i = 0; i < n; ++i) {
        st = st * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = double(st >> 11) / double(1ull << 53) - 0.5, t = double(i) / kFs;
        const double vin = peak * (0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t) + 0.02 * noise);
        out[i] = pre.process(vin);
        if (!nodes.empty() && i == std::size_t(kFs * 0.6)) for (std::size_t k = 0; k < nodes.size(); ++k) pre.takeProbePeak(k);
    }
    if (!nodes.empty()) {
        std::printf("   ");
        for (std::size_t k = 0; k < nodes.size(); ++k) std::printf(" %s=%.4g", nodes[k].c_str(), pre.takeProbePeak(k));
        std::printf("\n");
    }
    return out;
}

double rmsDb(const std::vector<double>& x, std::size_t from) {
    double e = 0;
    for (std::size_t i = from; i < x.size(); ++i) e += x[i] * x[i];
    return 10.0 * std::log10(e / double(x.size() - from) + 1e-30);
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
        const double ang = -2.0 * kPi / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Third-octave band powers (dB) of the last 2^16 samples.
std::vector<double> bands(const std::vector<double>& x) {
    const std::size_t n = 65536;
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n));
        a[i] = x[x.size() - n + i] * w;
    }
    fft(a);
    std::vector<double> b;
    for (double f = 50.0; f < 16000.0; f *= std::pow(2.0, 1.0 / 3.0)) {
        const double lo = f / std::pow(2.0, 1.0 / 6.0), hi = f * std::pow(2.0, 1.0 / 6.0);
        double p = 1e-30;
        for (std::size_t k = std::size_t(lo * n / kFs); k <= std::size_t(hi * n / kFs) && k < n / 2; ++k) p += std::norm(a[k]);
        b.push_back(10.0 * std::log10(p));
    }
    return b;
}
}  // namespace

int run(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: sas_ab_check <amp A> <amp B> \"selector=pos,...\" ...\n"); return 1; }
    const sas::AmpDef a = sas::loadEmbeddedAmp(argv[1]), b = sas::loadEmbeddedAmp(argv[2]);
    std::vector<std::string> settings;
    for (int i = 3; i < argc; ++i) settings.push_back(argv[i]);
    if (settings.empty()) settings.push_back("");
    // SAS_PEAKS="0,0.05": the drive levels (default 0.05, 0.15, 0.3; 0 = silence, to see the noise or an oscillation).
    std::vector<double> peaks{0.05, 0.15, 0.3};
    if (const char* e = std::getenv("SAS_PEAKS")) {
        peaks.clear();
        for (const char* q = e; *q;) { peaks.push_back(std::atof(q)); while (*q && *q != ',') ++q; if (*q) ++q; }
    }
    const std::size_t n = std::size_t(kFs * 1.6), from = std::size_t(kFs * 0.6);
    std::printf("%-30s %6s %9s %9s %9s\n", "setting", "peak", "level dB", "bands dB", "diff dB");
    for (const auto& s : settings)
        for (double peak : peaks) {
            const auto ya = run(a, parseSelectors(a, s), parseJack(s), peak, n), yb = run(b, parseSelectors(b, s), parseJack(s), peak, n);
            if (const char* dump = std::getenv("SAS_DUMP")) {  // A's output as raw little-endian doubles (96 kHz), for a closer look
                if (FILE* fo = std::fopen(dump, "wb")) { std::fwrite(ya.data(), sizeof(double), ya.size(), fo); std::fclose(fo); }
            }
            std::vector<double> d(n);
            for (std::size_t i = 0; i < n; ++i) d[i] = ya[i] - yb[i];
            const double la = rmsDb(ya, from), lb = rmsDb(yb, from);
            const auto ba = bands(ya), bb = bands(yb);
            const double top = *std::max_element(bb.begin(), bb.end());
            double worst = 0, worstHz = 0, worstUnder = 0, f = 50.0;
            for (std::size_t k = 0; k < ba.size(); ++k, f *= std::pow(2.0, 1.0 / 3.0))
                if (bb[k] > top - 60.0 && std::abs(ba[k] - bb[k]) > worst) { worst = std::abs(ba[k] - bb[k]); worstHz = f; worstUnder = top - bb[k]; }
            std::printf("%-30s %6.2f %+9.2f %9.2f %+9.1f   (worst band %.0f Hz, %.0f dB under the loudest)\n", s.empty() ? "(defaults)" : s.c_str(), peak, la - lb, worst,
                        rmsDb(d, from) - lb, worstHz, worstUnder);
            if (std::getenv("SAS_PEAKS")) std::printf("%38s A %+7.1f dB   B %+7.1f dB  (A's and B's output re 1 V)\n", "", la, lb);
        }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
