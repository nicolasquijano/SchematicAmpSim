#pragma once
// Loads the JCM800 definition once and maps golden-case parameters onto its knobs by name.

#include <vector>

#include "sas/amp_def.h"
#include "golden_io.h"

#ifndef SAS_AMPS_DIR
#error "SAS_AMPS_DIR must point at the amps/ folder"
#endif

inline const sas::AmpDef& jcm800() {
    static const sas::AmpDef def = sas::loadAmpDefFromFolder(std::string(SAS_AMPS_DIR) + "/jcm800_2203");
    return def;
}

inline std::vector<double> knobsOf(const sas::AmpDef& def, const golden::Case& c) {
    std::vector<double> v(def.preamp.knobs.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = c.num(def.preamp.knobs[i].name);
    return v;
}
