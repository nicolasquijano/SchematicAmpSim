#pragma once
// Loads the JCM800 definition once and maps golden-case parameters onto its knobs by name.

#include <vector>

#include "aes/amp_def.h"
#include "golden_io.h"

#ifndef AES_AMPS_DIR
#error "AES_AMPS_DIR must point at the amps/ folder"
#endif

inline const aes::AmpDef& jcm800() {
    static const aes::AmpDef def = aes::loadAmpDefFromFolder(std::string(AES_AMPS_DIR) + "/jcm800_2203");
    return def;
}

inline std::vector<double> knobsOf(const aes::AmpDef& def, const golden::Case& c) {
    std::vector<double> v(def.preamp.knobs.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = c.num(def.preamp.knobs[i].name);
    return v;
}
