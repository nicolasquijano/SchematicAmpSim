// Fits the behavioural "Lite" power amp of an amp definition to measurements of its full power
// amp circuit, the same procedure used for the JCM800:
//   1. small-signal response vs presence  -> base gain, high-pass corner, presence boost, shelf corner
//   2. static saturation curve at 150 Hz   -> ceiling (light/heavy), decay, knee (fitted to output rms)
//   3. rail sag vs drive                   -> sag table
// Usage: aes_fit_lite <amp id>   (prints a "lite" JSON block and the residual errors)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "aes/amp_def.h"
#include "aes/embedded_amps.h"
#include "aes/poweramp.h"
#include "aes/poweramp_lite.h"

namespace {

constexpr double kFs = 96000.0;
constexpr double kPi = 3.14159265358979323846;

using Process = std::function<double(double)>;

// RMS gain in dB of `proc` for a sine at `hz`, after a settling time.
double toneGainDb(const Process& proc, double hz, double amp) {
    const std::size_t settle = std::size_t(0.35 * kFs);
    const double cycles = std::max(8.0, std::ceil(hz * 0.2));
    const std::size_t len = std::size_t(cycles * kFs / hz);
    double acc = 0;
    for (std::size_t i = 0; i < settle + len; ++i) {
        const double y = proc(amp * std::sin(2 * kPi * hz * double(i) / kFs));
        if (i >= settle) acc += y * y;
    }
    return 20.0 * std::log10(std::sqrt(acc / double(len)) / (amp / std::sqrt(2.0)));
}

// Output rms (and mean rail) of `proc` for a sine, measured over the second half of `seconds`.
struct Level { double rms, meanRail; };
Level levelAt(const Process& proc, const std::function<double()>& rail, double hz, double amp, double seconds) {
    const std::size_t n = std::size_t(seconds * kFs);
    double acc = 0, rl = 0;
    std::size_t cnt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = proc(amp * std::sin(2 * kPi * hz * double(i) / kFs));
        if (i >= n / 2) {
            acc += y * y;
            rl += rail();
            ++cnt;
        }
    }
    return {std::sqrt(acc / double(cnt)), rl / double(cnt)};
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

}  // namespace

int main(int argc, char** argv) {
    const std::string id = argc > 1 ? argv[1] : "plexi_1959";
    aes::AmpDef def = aes::loadEmbeddedAmp(id);
    std::printf("fitting Lite for %s (%s)\n", def.id.c_str(), def.name.c_str());

    // ---------------------------------------------------------------- 1. small signal
    const double presences[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    const std::vector<double> freqs = {30, 125, 500, 1250, 3150, 6000};
    double g[5][6];
    for (int p = 0; p < 5; ++p) {
        aes::PowerAmp amp(def, kFs);
        amp.setPresence(presences[p]);
        for (std::size_t f = 0; f < freqs.size(); ++f)
            g[p][f] = toneGainDb([&](double x) { return amp.process(x); }, freqs[f], 0.05);
    }
    std::printf("\nmeasured small-signal gain (dB), rows = presence 0 .25 .5 .75 1\n  Hz   ");
    for (double f : freqs) std::printf("%8.0f", f);
    std::printf("\n");
    for (int p = 0; p < 5; ++p) {
        std::printf("  %.2f  ", presences[p]);
        for (std::size_t f = 0; f < freqs.size(); ++f) std::printf("%8.2f", g[p][f]);
        std::printf("\n");
    }

    aes::LiteParams& L = def.lite;
    L.baseGainDb = g[0][3];  // 1250 Hz, presence 0
    const double r = std::pow(10.0, (g[0][0] - g[0][3]) / 20.0);
    // A bass lift at 30 Hz (r > 1) cannot be modelled by a high-pass: leave it at a DC blocker.
    L.highPassHz = r >= 1.0 ? 1.0 : 30.0 * std::sqrt(1.0 / (r * r) - 1.0);
    for (int p = 0; p < 5; ++p) L.presenceDb[std::size_t(p)] = g[p][5] - g[0][5];

    // Shelf corner: least squares over the mid/high frequencies at p = .5, .75, 1.
    auto modelGain = [&](double shelfHz, int p, std::size_t f) {
        aes::AmpDef d = def;
        d.lite.shelfHz = shelfHz;
        aes::PowerAmpLite lite(d, kFs);
        lite.setPresence(presences[p]);
        return toneGainDb([&](double x) { return lite.process(x); }, freqs[f], 0.05);
    };
    double bestShelf = L.shelfHz, bestCost = 1e30;
    for (double s = 150; s <= 3500; s *= 1.18) {
        double cost = 0;
        for (int p : {2, 3, 4})
            for (std::size_t f : {2u, 3u, 4u}) {
                const double e = modelGain(s, p, f) - g[p][f];
                cost += e * e;
            }
        if (cost < bestCost) { bestCost = cost; bestShelf = s; }
    }
    L.shelfHz = bestShelf;
    // Re-centre the base gain on the median error at presence 0 over the passband.
    {
        std::vector<double> e;
        for (std::size_t f : {1u, 2u, 3u, 4u}) e.push_back(g[0][f] - modelGain(L.shelfHz, 0, f));
        std::sort(e.begin(), e.end());
        L.baseGainDb += 0.5 * (e[1] + e[2]);
    }
    std::printf("\nfitted: base gain %.2f dB, high-pass %.1f Hz, shelf %.0f Hz, presence dB:", L.baseGainDb, L.highPassHz, L.shelfHz);
    for (double v : L.presenceDb) std::printf(" %.2f", v);
    std::printf("\nresidual (Lite - circuit, dB):\n  Hz   ");
    for (double f : freqs) std::printf("%8.0f", f);
    std::printf("\n");
    double worst = 0;
    for (int p = 0; p < 5; ++p) {
        std::printf("  %.2f  ", presences[p]);
        for (std::size_t f = 0; f < freqs.size(); ++f) {
            const double e = modelGain(L.shelfHz, p, f) - g[p][f];
            std::printf("%8.2f", e);
            if (f != 0) worst = std::max(worst, std::abs(e));
        }
        std::printf("\n");
    }
    std::printf("  worst |error| above 30 Hz: %.2f dB\n", worst);

    // ---------------------------------------------------------------- 2/3. saturation and sag
    const double linGain = std::pow(10.0, L.baseGainDb / 20.0);
    const std::vector<double> targets = {4, 8, 15, 25, 35, 45, 55, 70, 90, 130, 200};  // unclipped peak output volts
    struct Point { double vin, rmsOut, rail, unclippedMeanAbs; };
    std::vector<Point> pts;
    double railIdle = 0;
    {
        // Same duration and averaging window as the driven runs: the rail has a ~2 s time constant,
        // so an idle value taken at another time would show up as fake sag.
        aes::PowerAmp idle(def, kFs);
        idle.setPresence(0.5);
        railIdle = levelAt([&](double x) { return idle.process(x); }, [&] { return idle.railX(); }, 150.0, 0.0, 1.6).meanRail;
    }
    for (double t : targets) {
        const double vin = t / linGain;
        aes::PowerAmp amp(def, kFs);
        amp.setPresence(0.5);
        const auto lv = levelAt([&](double x) { return amp.process(x); }, [&] { return amp.railX(); }, 150.0, vin, 1.6);
        pts.push_back({vin, lv.rms, lv.meanRail, 2.0 / kPi * t});
    }

    // Fit ceiling/decay/knee to the output rms (what is heard), as for the JCM800.
    auto satCost = [&](const aes::LiteParams& lp) {
        aes::AmpDef d = def;
        d.lite = lp;
        double cost = 0;
        for (const auto& pt : pts) {
            aes::PowerAmpLite lite(d, kFs);
            lite.setPresence(0.5);
            const auto lv = levelAt([&](double x) { return lite.process(x); }, [] { return 0.0; }, 150.0, pt.vin, 0.8);
            const double e = 20.0 * std::log10(lv.rms / pt.rmsOut);
            cost += e * e;
        }
        return cost;
    };
    aes::LiteParams best = L;
    double bestSat = satCost(best);
    double step = 0.3;
    for (int round = 0; round < 28; ++round, step *= 0.82) {
        for (int k = 0; k < 4; ++k) {
            for (double dir : {-1.0, 1.0}) {
                aes::LiteParams c = best;
                double* field[4] = {&c.ceilingLight, &c.ceilingHeavy, &c.ceilingDecay, &c.knee};
                *field[k] *= std::exp(dir * step);
                c.ceilingHeavy = std::min(c.ceilingHeavy, c.ceilingLight);
                const double cost = satCost(c);
                if (cost < bestSat) { bestSat = cost; best = c; }
            }
        }
    }
    L = best;

    // Sag table: rail drop at the circuit, expressed in HT-sag units (drop / rail fraction).
    L.railXIdle = railIdle;
    L.sagTable.clear();
    L.sagTable.push_back({0.0, 0.0});
    double prevSag = 0;
    for (const auto& pt : pts) {
        const double sag = std::max(prevSag, (railIdle - pt.rail) / L.railSagFraction);
        L.sagTable.push_back({pt.unclippedMeanAbs, sag});
        prevSag = sag;
    }

    std::printf("\nsaturation (150 Hz, presence .5): input pk V, circuit rms V, Lite rms V, error dB, rail drop V\n");
    for (const auto& pt : pts) {
        aes::PowerAmpLite lite(def, kFs);
        lite.setPresence(0.5);
        const auto lv = levelAt([&](double x) { return lite.process(x); }, [] { return 0.0; }, 150.0, pt.vin, 0.8);
        std::printf("  %8.3f  %8.2f  %8.2f  %6.2f  %7.2f\n", pt.vin, pt.rmsOut, lv.rms, 20 * std::log10(lv.rms / pt.rmsOut), railIdle - pt.rail);
    }

    // ---------------------------------------------------------------- JSON
    auto num = [](double v) {
        char b[64];
        std::snprintf(b, sizeof b, "%.6g", v);
        return std::string(b);
    };
    std::printf("\n\"lite\": {\n");
    std::printf("  \"base_gain_db\": %s,\n  \"high_pass_hz\": %s,\n  \"shelf_hz\": %s,\n", num(L.baseGainDb).c_str(), num(L.highPassHz).c_str(), num(L.shelfHz).c_str());
    std::printf("  \"presence_db\": [");
    for (std::size_t i = 0; i < L.presenceDb.size(); ++i) std::printf("%s%s", i ? ", " : "", num(L.presenceDb[i]).c_str());
    std::printf("],\n  \"sag_table\": [");
    for (std::size_t i = 0; i < L.sagTable.size(); ++i)
        std::printf("%s[%s, %s]", i ? ", " : "", num(L.sagTable[i].first).c_str(), num(L.sagTable[i].second).c_str());
    std::printf("],\n  \"rail_x_idle\": %s,\n  \"rail_sag_fraction\": %s,\n", num(L.railXIdle).c_str(), num(L.railSagFraction).c_str());
    std::printf("  \"ceiling_light\": %s,\n  \"ceiling_heavy\": %s,\n  \"ceiling_decay\": %s,\n  \"knee\": %s,\n", num(L.ceilingLight).c_str(),
                num(L.ceilingHeavy).c_str(), num(L.ceilingDecay).c_str(), num(L.knee).c_str());
    std::printf("  \"sag_attack_s\": %s,\n  \"sag_release_s\": %s\n}\n", num(L.sagAttackS).c_str(), num(L.sagReleaseS).c_str());
    (void)lerp;
}
