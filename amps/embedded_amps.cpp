#include "aes/embedded_amps.h"

#include <stdexcept>

namespace aes {

std::vector<std::string> embeddedAmpIds() {
    std::vector<std::string> ids;
    for (const auto& [id, files] : embeddedAmpFiles()) ids.push_back(id);
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
