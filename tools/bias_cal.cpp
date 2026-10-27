// Finds, for every amp with alternative output tubes, the bias_scale that gives the alternative tube the
// same idle dissipation fraction as the stock one:  idle_alt = idle_stock * pmax_alt / pmax_stock.
//   sas_bias_cal            prints one line per (amp, tube): the scale to put in amp.json "power.tubes"
// The idle current is the average plate current of one tube with no signal, read the way the plugin
// shows it (PowerAmp::idleMilliamps).
#include <cmath>
#include <cstdio>

#include "sas/condition.h"
#include "sas/embedded_amps.h"
#include "sas/poweramp.h"

namespace {

constexpr double kFs = 48000.0;

double idleMa(const sas::AmpDef& def, int tube, double biasScale) {
    sas::AmpDef d = def;
    d.power.tubes[std::size_t(tube)].biasScale = biasScale;
    sas::PowerAmp pa(sas::applyCondition(d, {tube, 0, 0}), kFs);
    for (std::size_t i = 0, n = std::size_t(1.8 * kFs); i < n; ++i) pa.process(0.0);
    return pa.idleMilliamps();
}

}  // namespace

int main() {
    for (const auto& id : sas::embeddedAmpIds()) {
        const sas::AmpDef def = sas::loadEmbeddedAmp(id);
        if (def.power.tubes.size() < 2 || !def.power.bias.adjustable()) continue;
        const double stock = idleMa(def, 0, 1.0);
        std::printf("%s: stock %s idle %.2f mA (%.1f W rating)\n", id.c_str(), def.power.tubes[0].name.c_str(), stock, def.power.tubes[0].pmaxW);
        for (std::size_t k = 1; k < def.power.tubes.size(); ++k) {
            const double target = stock * def.power.tubes[k].pmaxW / def.power.tubes[0].pmaxW;
            // Bisection on the scale (a larger scale is a colder bias).
            double lo = 0.4, hi = 2.0;
            for (int it = 0; it < 14; ++it) {
                const double mid = 0.5 * (lo + hi);
                (idleMa(def, int(k), mid) > target ? lo : hi) = mid;
            }
            const double s = 0.5 * (lo + hi);
            std::printf("  %-6s target %.2f mA  bias_scale %.4f  (idle there %.2f mA)\n", def.power.tubes[k].name.c_str(), target, s, idleMa(def, int(k), s));
        }
    }
    return 0;
}
