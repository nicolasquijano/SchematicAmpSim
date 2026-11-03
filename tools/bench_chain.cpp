// CPU of the whole Chain per quality, for each embedded amp and a few input levels (% of a core at 48 kHz). SAS_BENCH_AMP=<id> runs one amp.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sas/amp.h"
#include "sas/chain.h"
#include "sas/embedded_amps.h"

int main() {
    constexpr double fs = 48000.0;
    const std::size_t n = std::size_t(fs * 3.0);
    const char* only = std::getenv("SAS_BENCH_AMP");
    const double peakEnv = std::getenv("SAS_PEAK") ? std::atof(std::getenv("SAS_PEAK")) : 0.1;
    if (const char* wav = std::getenv("SAS_WAV")) {
        // A real recording (16-bit mono PCM) through the chain per quality: mean and worst 512-sample block, with fixed knobs and with
        // knobs that jump every ~100 ms. SAS_GAIN scales the file.
        std::FILE* f = std::fopen(wav, "rb");
        if (!f) return 1;
        std::vector<unsigned char> raw;
        unsigned char tmp[65536];
        for (std::size_t r; (r = std::fread(tmp, 1, sizeof tmp, f)) > 0;) raw.insert(raw.end(), tmp, tmp + r);
        std::fclose(f);
        std::size_t pos = 12, dataAt = 0, dataLen = 0;
        double rate = 44100.0;
        while (pos + 8 <= raw.size()) {
            const std::size_t len = raw[pos + 4] | raw[pos + 5] << 8 | raw[pos + 6] << 16 | std::size_t(raw[pos + 7]) << 24;
            if (!std::memcmp(&raw[pos], "fmt ", 4)) rate = double(raw[pos + 12] | raw[pos + 13] << 8 | raw[pos + 14] << 16);
            if (!std::memcmp(&raw[pos], "data", 4)) { dataAt = pos + 8; dataLen = std::min(len, raw.size() - dataAt); break; }
            pos += 8 + len + (len & 1);
        }
        const double gain = std::getenv("SAS_GAIN") ? std::atof(std::getenv("SAS_GAIN")) : 1.0;
        std::vector<float> sig(dataLen / 2);
        double pk = 0;
        for (std::size_t i = 0; i < sig.size(); ++i) {
            sig[i] = float(gain * double(short(raw[dataAt + 2 * i] | raw[dataAt + 2 * i + 1] << 8)) / 32768.0);
            pk = std::max(pk, double(std::fabs(sig[i])));
        }
        std::printf("%s: %zu samples at %.0f Hz, peak %.2f\n", wav, sig.size(), rate, pk);
        for (const auto& id : sas::embeddedAmpIds()) {
            if (only && id != only) continue;
            const sas::AmpDef def = sas::loadEmbeddedAmp(id);
            if (std::getenv("SAS_STATS")) {
                // The solver's own counts for the same material straight through the amp (no chain): 2x with the oversampler, and 1x.
                for (int mode = 0; mode < 3; ++mode) {
                    if (mode == 2) {  // preamp at 2x, power amp at the host rate (the hybrid)
                        sas::PowerAmp power(def, rate, true);
                        sas::Preamp pre(def, rate * 2.0, power.railX(), true, false);
                        std::vector<double> kn;
                        for (const auto& k : def.preamp.knobs) kn.push_back(k.def);
                        pre.setKnobs(kn);
                        sas::HalfbandStage up(23), down(23);
                        const double scl = std::getenv("SAS_SCALE") ? std::atof(std::getenv("SAS_SCALE")) : 0.1;
                        const auto t0 = std::chrono::steady_clock::now();
                        for (float x : sig) {
                            const auto ab = up.up(x);
                            pre.setRail(power.railX());
                            const float pa = float(pre.process(double(ab[0]) * scl));
                            pre.setRail(power.railX());
                            const float pb = float(pre.process(double(ab[1]) * scl));
                            (void)power.process(double(down.down(pa, pb)));
                        }
                        const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                        std::printf("%s hybrid: pre fail %llu | power fail %llu | %.1f%%\n", id.c_str(), (unsigned long long)pre.newtonFailures(),
                                    (unsigned long long)power.solver().newtonFailures, 100.0 * dt.count() / (double(sig.size()) / rate));
                        continue;
                    }
                    sas::FullAmp amp(def, rate * (mode ? 1.0 : 2.0), false, true);
                    std::vector<double> kn;
                    for (const auto& k : def.preamp.knobs) kn.push_back(k.def);
                    amp.setKnobs(kn, def.power.presenceDefault);
                    if (const char* pl = std::getenv("SAS_PLAIN")) {
                        for (std::size_t k = 0; k < amp.pre().stageCount(); ++k) amp.pre().solver(k).plainIterations = std::size_t(std::atoi(pl));
                        amp.power().solver().plainIterations = std::size_t(std::atoi(pl));
                    }
                    sas::Oversampler os(1);
                    const auto t0 = std::chrono::steady_clock::now();
                    const double scl = std::getenv("SAS_SCALE") ? std::atof(std::getenv("SAS_SCALE")) : 0.1;
                    double slowSum = 0, allSum = 0, worstS = 0;
                    std::size_t slowN = 0;
                    for (float x : sig) {
                        const auto s0 = std::chrono::steady_clock::now();
                        if (mode) (void)amp.process(double(x) * scl);
                        else (void)os.process(x, [&](float v) { return float(amp.process(double(v) * scl)); });
                        const double us = std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count() * 1e6;
                        allSum += us;
                        worstS = std::max(worstS, us);
                        if (us > 40.0) { slowSum += us; ++slowN; }
                    }
                    std::printf("   samples >40us: %zu = %.1f%% of the time, worst %.0f us, mean %.2f us\n", slowN, 100.0 * slowSum / allSum, worstS,
                                allSum / double(sig.size()));
                    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                    std::uint64_t cont = 0, fac = 0, fail = 0, it = 0;
                    for (std::size_t k = 0; k < amp.pre().stageCount(); ++k) {
                        cont += amp.pre().solver(k).continuationRuns; fac += amp.pre().solver(k).jacobianFactorizations;
                        fail += amp.pre().solver(k).newtonFailures; it += amp.pre().solver(k).newtonIterations;
                    }
                    const auto& ps = amp.power().solver();
                    std::printf("%s %s: pre cont %llu fac %llu fail %llu iter %llu | power cont %llu fac %llu fail %llu iter %llu | %.1f%%\n", id.c_str(),
                                mode ? "1x" : "2x", (unsigned long long)cont, (unsigned long long)fac, (unsigned long long)fail,
                                (unsigned long long)it, (unsigned long long)ps.continuationRuns, (unsigned long long)ps.jacobianFactorizations,
                                (unsigned long long)ps.newtonFailures, (unsigned long long)ps.newtonIterations,
                                100.0 * dt.count() / (double(sig.size()) / rate));
                }
                continue;
            }
            for (int jump = 0; jump < 2; ++jump) {
                std::printf("%-16s %-6s", id.c_str(), jump ? "jumps" : "fixed");
                for (auto q : {sas::Quality::Normal, sas::Quality::Efficient}) {
                    sas::Chain chain(def, rate);
                    sas::ChainSettings s;
                    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
                    s.quality = q;
                    chain.setSettings(s);
                    chain.snap(s);
                    std::vector<float> out(512);
                    std::uint64_t st = 12345;
                    double total = 0, worst = 0;
                    int blocks = 0;
                    for (std::size_t b = 0; b + 512 <= sig.size(); b += 512, ++blocks) {
                        if (jump && blocks % 9 == 0)
                            for (auto& k : s.knobs) {
                                st = st * 6364136223846793005ull + 1442695040888963407ull;
                                k = double(st >> 11) / double(1ull << 53);
                            }
                        chain.setSettings(s);
                        const auto t0 = std::chrono::steady_clock::now();
                        chain.processAmp(sig.data() + b, out.data(), 512);
                        chain.applyOutput(out.data(), 512);
                        const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                        const double pct = 100.0 * dt.count() / (512.0 / rate);
                        total += pct;
                        worst = std::max(worst, pct);
                    }
                    std::printf("  mean %6.1f%% worst %7.1f%% |", total / blocks, worst);
                }
                std::printf("\n");
                std::fflush(stdout);
            }
        }
        return 0;
    }
    if (std::getenv("SAS_JUMPS")) {
        // Knobs that jump (a new random value for every knob every 100 ms, as dragging fast or an automation step would):
        // the mean and the worst 512-sample block, in % of the real time of a block, per quality.
        for (const auto& id : sas::embeddedAmpIds()) {
            if (only && id != only) continue;
            const sas::AmpDef def = sas::loadEmbeddedAmp(id);
            std::printf("%-16s", id.c_str());
            for (auto q : {sas::Quality::Normal, sas::Quality::Efficient}) {
                sas::Chain chain(def, fs);
                sas::ChainSettings s;
                for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
                s.quality = q;
                chain.setSettings(s);
                chain.snap(s);
                std::vector<float> in(512), out(512);
                std::uint64_t st = 12345;
                double total = 0, worst = 0;
                int blocks = 0;
                for (std::size_t b = 0; b < n; b += 512, ++blocks) {
                    if (blocks % 9 == 0)
                        for (auto& k : s.knobs) {
                            st = st * 6364136223846793005ull + 1442695040888963407ull;
                            k = double(st >> 11) / double(1ull << 53);
                        }
                    chain.setSettings(s);
                    for (std::size_t i = 0; i < 512; ++i) in[i] = float(peakEnv * (0.6 * std::sin(6.2831853 * 110.0 * double(b + i) / fs) + 0.3 * std::sin(6.2831853 * 330.7 * double(b + i) / fs)));
                    const auto t0 = std::chrono::steady_clock::now();
                    chain.processAmp(in.data(), out.data(), 512);
                    chain.applyOutput(out.data(), 512);
                    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                    const double pct = 100.0 * dt.count() / (512.0 / fs);
                    total += pct;
                    worst = std::max(worst, pct);
                }
                std::printf("  mean %6.1f%% worst %7.1f%% |", total / blocks, worst);
            }
            std::printf("\n");
            std::fflush(stdout);
        }
        return 0;
    }
    const sas::Quality qs[] = {sas::Quality::Normal, sas::Quality::Efficient, sas::Quality::Lite};
    std::printf("%-16s %-8s %9s %9s %9s\n", "amp", "peak", "Normal", "Efficient", "Lite");
    for (const auto& id : sas::embeddedAmpIds()) {
        if (only && id != only) continue;
        const sas::AmpDef def = sas::loadEmbeddedAmp(id);
        for (double peak : {0.0, 0.02, 0.1, 0.3}) {
            std::vector<float> in(n), out(n);
            for (std::size_t i = 0; i < n; ++i)
                in[i] = float(peak * (0.6 * std::sin(6.2831853 * 110.0 * double(i) / fs) + 0.3 * std::sin(6.2831853 * 330.7 * double(i) / fs)));
            std::printf("%-16s %-8.2f", id.c_str(), peak);
            for (auto q : qs) {
                sas::Chain chain(def, fs);
                sas::ChainSettings s;
                for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
                s.quality = q;
                chain.setSettings(s);
                chain.snap(s);
                const auto t0 = std::chrono::steady_clock::now();
                for (std::size_t b = 0; b < n; b += 512) {
                    const std::size_t m = std::min<std::size_t>(512, n - b);
                    chain.processAmp(in.data() + b, out.data() + b, m);
                    chain.applyOutput(out.data() + b, m);
                }
                const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                std::printf(" %8.1f%%", 100.0 * dt.count() / (double(n) / fs));
            }
            // Switched live from Normal, as the plugin does when the quality is changed (no snap): the cost after the switch.
            {
                sas::Chain chain(def, fs);
                sas::ChainSettings s;
                for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
                chain.setSettings(s);
                chain.snap(s);
                for (std::size_t b = 0; b + 512 <= n / 2; b += 512) chain.processAmp(in.data() + b, out.data() + b, 512);
                s.quality = sas::Quality::Efficient;
                chain.setSettings(s);
                const auto t0 = std::chrono::steady_clock::now();
                for (std::size_t b = n / 2; b + 512 <= n; b += 512) {
                    chain.processAmp(in.data() + b, out.data() + b, 512);
                    chain.applyOutput(out.data() + b, 512);
                }
                const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
                std::printf("  | switched: %5.1f%%", 100.0 * dt.count() / (double(n / 2) / fs));
            }
            std::printf("\n");
            std::fflush(stdout);
        }
    }
}
