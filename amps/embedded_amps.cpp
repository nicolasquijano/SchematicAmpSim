#include "aes/embedded_amps.h"

#include <algorithm>
#include <stdexcept>

namespace aes {

std::vector<std::string> embeddedAmpIds() {
    std::vector<std::pair<std::pair<int, std::string>, std::string>> keyed;
    for (const auto& [id, files] : embeddedAmpFiles()) keyed.push_back({{loadEmbeddedAmp(id).order, id}, id});
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::string> ids;
    for (const auto& k : keyed) ids.push_back(k.second);
    return ids;
}

AmpDef loadEmbeddedAmp(const std::string& id) {
    const auto& all = embeddedAmpFiles();
    const auto it = all.find(id);
    if (it == all.end()) throw std::runtime_error("no embedded amp '" + id + "'");
    const auto& files = it->second;
    auto read = [&](const std::string& name) {
        const auto f = files.find(name);
        if (f == files.end()) throw std::runtime_error("amp '" + id + "' has no file '" + name + "'");
        return f->second;
    };
    return loadAmpDef(read("amp.json"), read);
}

}  // namespace aes
