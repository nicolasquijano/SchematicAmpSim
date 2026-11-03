#pragma once
// The amp folders compiled into the binary.

#include <map>
#include <string>
#include <vector>

#include "sas/amp_def.h"

namespace sas {

// folder id -> (file name -> content). Generated at configure time from amps/<id>/.
const std::map<std::string, std::map<std::string, std::string>>& embeddedAmpFiles();

// The circuit amps by `order`; with `includeNam` also the Neural Amp Modeler loader (the plugin lists it, the circuit tools and tests do not).
std::vector<std::string> embeddedAmpIds(bool includeNam = false);
// Throws if `id` is unknown or its files are malformed.
AmpDef loadEmbeddedAmp(const std::string& id);

}  // namespace sas
