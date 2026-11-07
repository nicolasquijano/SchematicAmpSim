// Checks a split preamp against the same preamp as one netlist: both run in the Normal mode (tables, 96 kHz) on the same guitar-like input
// for several jacks, knob positions, switches and drive levels; prints the level difference, the worst octave band difference and the
// error of the waveform. The one-netlist version is read from a file (the amp folder's preamp_full.net).
//   sas_split_check <amp id> <path to the one-netlist preamp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"

namespace {
constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;

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

std::vector<double> bands(const std::vector<double>& x) {  // octave band levels (dB), 63 Hz .. 8 kHz, of the last 2^16 samples
    const std::size_t n = 1 << 16;
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < n; ++i) a[i] = x[x.size() - n + i] * (0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n)));
    fft(a);
    std::vector<double> out;
    for (double fc = 63.0; fc < 9000.0; fc *= 2.0) {
        double e = 0;
        for (std::size_t k = 1; k < n / 2; ++k) {
            const double f = double(k) * kFs / double(n);
            if (f >= fc / std::sqrt(2.0) && f < fc * std::sqrt(2.0)) e += std::norm(a[k]);
        }
        out.push_back(10.0 * std::log10(e + 1e-30));
    }
    return out;
}

double rmsDb(const std::vector<double>& x, std::size_t from) {
    double e = 0;
    for (std::size_t i = from; i < x.size(); ++i) e += x[i] * x[i];
    return 10.0 * std::log10(e / double(x.size() - from) + 1e-30);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: sas_split_check <amp id> <one-netlist preamp file>\n"); return 1; }
    sas::AmpDef split = sas::loadEmbeddedAmp(argv[1]);
    sas::AmpDef full = split;
    {
        std::ifstream f(argv[2]);
        if (!f) { std::printf("cannot read %s\n", argv[2]); return 1; }
        full.preamp.netlistText.assign(std::istreambuf_iterator<char>(f), {});
        full.preamp.stages.clear();
    }
    struct Case { const char* name; std::size_t jack; double amp; std::vector<std::pair<const char*, double>> knobs; std::vector<std::pair<std::size_t, std::size_t>> sel; };
    const std::vector<Case> svtCases = {
        {"silence (hum only)", 0, 0.0, {}, {}},
        {"ch1 normal clean", 0, 0.05, {}, {}},
        {"ch1 normal crunch", 0, 0.15, {}, {}},
        {"ch1 normal lead", 0, 0.3, {{"vol1", 0.9}}, {}},
        {"ch1 bright crunch", 1, 0.15, {}, {}},
        {"ch2 normal clean", 2, 0.003, {}, {}},
        {"ch2 normal crunch", 2, 0.01, {}, {}},
        {"ch2 bright lead", 3, 0.02, {{"vol2", 0.9}}, {}},
        {"both normal crunch", 4, 0.15, {}, {}},
        {"ch1 bass+treble up", 0, 0.15, {{"bass", 0.9}, {"treble", 0.9}}, {}},
        {"ch1 mid cut, mid sw", 0, 0.15, {{"mid", 0.1}}, {{2, 1}}},
        {"ch1 ultra hi", 0, 0.15, {}, {{1, 1}}},
        {"ch1 ultra lo", 0, 0.15, {}, {{0, 1}}},
        {"ch2 ultra lo+hi", 2, 0.01, {}, {{0, 1}, {1, 1}}},
    };
    const std::vector<Case> slCases = {
        {"silence (hum only)", 0, 0.0, {}, {}},
        {"clean crunch-sw low", 0, 0.05, {}, {}},
        {"clean hot", 0, 0.3, {{"normal_volume", 0.9}}, {}},
        {"clean sw=Clean", 0, 0.15, {}, {{0, 1}}},
        {"clean bright", 0, 0.1, {}, {{1, 1}}},
        {"lead low gain", 1, 0.05, {{"od_volume", 0.3}}, {}},
        {"lead mid", 1, 0.15, {}, {}},
        {"lead hot", 1, 0.3, {{"od_volume", 0.9}}, {}},
        {"lead tone up", 1, 0.15, {{"bass", 0.9}, {"treble", 0.9}}, {}},
        {"lead tone down", 1, 0.15, {{"bass", 0.1}, {"middle", 0.1}, {"treble", 0.1}}, {}},
        {"lead bright", 1, 0.15, {}, {{1, 1}}},
    };
    const auto& cases = std::string(argv[1]) == "superlead_100" ? slCases : svtCases;
    std::printf("%-22s %9s %9s %9s %8s\n", "case", "level d", "band max", "wave err", "fails");
    for (const auto& c : cases) {
        std::vector<std::vector<double>> out(2);
        std::uint64_t fails = 0;
        for (int v = 0; v < 2; ++v) {
            const sas::AmpDef& def = v == 0 ? split : full;
            sas::FullAmp amp(def, kFs, false, true);
            std::vector<double> knobs;
            for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
            for (std::size_t i = 0; i < knobs.size(); ++i) {
                if (def.preamp.knobs[i].name == "master") knobs[i] = 0.5;
                if (def.preamp.knobs[i].name == "vol1" || def.preamp.knobs[i].name == "vol2") knobs[i] = 0.6;
                for (const auto& [n, val] : c.knobs) if (def.preamp.knobs[i].name == n) knobs[i] = val;
            }
            amp.setKnobs(knobs, def.power.presenceDefault);
            amp.setJack(c.jack);
            for (const auto& [i, p] : c.sel) amp.setSelector(i, p);
            const std::size_t n = std::size_t(kFs * 1.5);
            out[v].resize(n);
            std::uint64_t st = 0x123456789abcdef0ull;
            for (std::size_t i = 0; i < n; ++i) {
                st = st * 6364136223846793005ull + 1442695040888963407ull;
                const double noise = double(st >> 11) / double(1ull << 53) - 0.5, t = double(i) / kFs;
                const double vin = c.amp * (0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t) + 0.02 * noise);
                out[v][i] = amp.process(vin);
            }
            if (v == 0) fails = amp.newtonFailures();
        }
        const auto& a = out[0];
        const auto& b = out[1];
        double worst = 0;
        const auto ba = bands(a), bb = bands(b);
        if (std::getenv("SAS_BANDS")) {
            std::printf("   bands 63..8k (split - full dB, full level dB):");
            for (std::size_t i = 0; i < ba.size(); ++i) std::printf("  %+.2f(%.0f)", ba[i] - bb[i], bb[i]);
            std::printf("\n");
        }
        for (std::size_t i = 0; i < ba.size(); ++i) if (bb[i] > rmsDb(b, b.size() / 2) - 40.0) worst = std::max(worst, std::abs(ba[i] - bb[i]));
        double ed = 0, eb = 0;
        for (std::size_t i = b.size() / 2; i < b.size(); ++i) { ed += (a[i] - b[i]) * (a[i] - b[i]); eb += b[i] * b[i]; }
        // The same error after removing each one's mean, at the best lag of -3..3 samples (a cut that delays by a sample shows up as the lag).
        double best = 1e300, bestLag = 0, ma = 0, mb = 0, ebm = 0;
        const std::size_t from = b.size() / 2, to = b.size() - 8;
        for (std::size_t i = from; i < to; ++i) { ma += a[i]; mb += b[i]; }
        ma /= double(to - from);
        mb /= double(to - from);
        for (std::size_t i = from; i < to; ++i) ebm += (b[i] - mb) * (b[i] - mb);
        for (int lag = -3; lag <= 3; ++lag) {
            double e = 0;
            for (std::size_t i = from; i < to; ++i) { const double d = (a[std::size_t(long(i) + lag)] - ma) - (b[i] - mb); e += d * d; }
            if (e < best) { best = e; bestLag = lag; }
        }
        std::printf("%-22s %+8.2f %8.2f %8.1f dB  (no dc, lag %+.0f: %6.1f dB, dc %+.4f V) %4llu\n", c.name, rmsDb(a, a.size() / 2) - rmsDb(b, b.size() / 2), worst,
                    10.0 * std::log10(ed / (eb + 1e-30) + 1e-30), bestLag, 10.0 * std::log10(best / (ebm + 1e-30) + 1e-30), ma - mb, (unsigned long long)fails);
    }
    return 0;
}
