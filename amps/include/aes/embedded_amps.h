#pragma once
// The amp folders compiled into the binary.

#include <map>
#include <string>
#include <vector>

#include "aes/amp_def.h"

namespace aes {

// folder id -> (file name -> content). Generated at configure time from amps/<id>/.
const std::map<std::string, std::map<std::string, std::string>>& embeddedAmpFiles();

std::vector<std::string> embeddedAmpIds();
// Throws if `id` is unknown or its files are malformed.
AmpDef loadEmbeddedAmp(const std::string& id);

}  // namespace aes
