#pragma once
// Loads the JCM800 definition once and maps golden-case parameters onto its knobs by name.
//
// The golden data comes from the Rust implementation, which used the stock Koren 12AX7 and a Lite power
// amp fitted to it. The shipped amp now uses the 12AX7 fitted to the GE data sheet and a Lite fit made with
// it, so the parity tests run the JCM800 as it was: "12AX7" in its netlists becomes the stock "ECC83" and
// the Lite parameters are the original ones.

#include <string>
#include <vector>

#include "sas/amp_def.h"
#include "golden_io.h"

#ifndef SAS_AMPS_DIR
#error "SAS_AMPS_DIR must point at the amps/ folder"
#endif

inline void useStockTriodes(std::string& text) {
    for (std::size_t at = text.find(" 12AX7"); at != std::string::npos; at = text.find(" 12AX7", at + 6)) text.replace(at + 1, 5, "ECC83");
}

// The Rust implementation had a linear output transformer: the golden data is run without the core saturation.
inline void withoutCoreSaturation(std::string& text) {
    for (std::size_t at = text.find(" SAT="); at != std::string::npos; at = text.find(" SAT=")) {
        const std::size_t end = text.find('\n', at);
        text.erase(at, end == std::string::npos ? std::string::npos : end - at);
    }
}

// ...and without the guitar pickup / cable network, which adds nodes the Rust circuit does not have.
inline void withoutGuitarNetwork(std::string& text) {
    const auto from = text.find("* The guitar behind its source impedance.");
    const auto last = text.find("C CCAB4 pkk4 0 800p");
    if (from == std::string::npos || last == std::string::npos) return;
    const auto eol = text.find('\n', last);  // the line may end in CR LF
    text.erase(from, (eol == std::string::npos ? text.size() : eol + 1) - from);
}

inline const sas::AmpDef& jcm800() {
    static const sas::AmpDef def = [] {
        sas::AmpDef d = sas::loadAmpDefFromFolder(std::string(SAS_AMPS_DIR) + "/jcm800_2203");
        useStockTriodes(d.preamp.netlistText);
        withoutGuitarNetwork(d.preamp.netlistText);
        for (auto& s : d.preamp.stages) useStockTriodes(s.netlistText);
        useStockTriodes(d.power.netlistText);
        withoutCoreSaturation(d.power.netlistText);
        d.lite.baseGainDb = 25.72;
        d.lite.highPassHz = 21.9;
        d.lite.shelfHz = 564.0;
        d.lite.presenceDb = {0.0, 0.21, 0.58, 1.42, 5.06};
        d.lite.sagTable = {{0, 0},     {6, 0.8},    {12.3, 5.6}, {18.4, 12.6}, {24.6, 20.7}, {30.6, 29.2}, {36.3, 37.4},
                           {49, 47.8}, {61, 50.7},  {86, 55.6},  {123, 58.5},  {185, 59.8},  {307, 60.1}};
        d.lite.railXIdle = 391.0;
        d.lite.railSagFraction = 0.58;
        d.lite.ceilingLight = 66.5;
        d.lite.ceilingHeavy = 48.5;
        d.lite.ceilingDecay = 119.7;
        d.lite.knee = 6.44;
        d.lite.sagAttackS = 0.010;
        d.lite.sagReleaseS = 0.060;
        return d;
    }();
    return def;
}

inline std::vector<double> knobsOf(const sas::AmpDef& def, const golden::Case& c) {
    std::vector<double> v(def.preamp.knobs.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = c.num(def.preamp.knobs[i].name);
    return v;
}
