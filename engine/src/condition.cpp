#include "sas/condition.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>

#include "sas/netlist.h"

namespace sas {

namespace {

// Deterministic value in [-1, 1) for (unit, kind, name).
double unitNoise(int unit, char kind, const std::string& name) {
    std::uint64_t h = 0xcbf29ce484222325ull ^ (std::uint64_t(unit) * 0x9e3779b97f4a7c15ull);
    h = (h ^ std::uint64_t(kind)) * 0x100000001b3ull;
    for (char ch : name) h = (h ^ std::uint64_t(static_cast<unsigned char>(ch))) * 0x100000001b3ull;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return double(h >> 11) / double(1ull << 52) - 1.0;  // 53 bits -> [0,2) -> [-1,1)
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::string num(double v) {
    std::ostringstream os;
    os.precision(9);
    os << v;
    return os.str();
}

std::vector<std::string> tokens(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> t;
    for (std::string w; is >> w;) t.push_back(w);
    return t;
}

struct Rules {
    Condition cond;
    double wear = 0.0;
    std::set<std::string> protectedResistors;
    const PowerTubeDef* tube = nullptr;
    std::string biasSource;
    double biasScale = 1.0;
};

std::string factor(const std::string& v, double f) { return "(" + v + ")*" + num(f); }

// Rewrites one netlist line (comment already cut off); returns it unchanged when no rule applies.
std::string rewriteLine(const std::string& line, const Rules& r) {
    auto t = tokens(line);
    if (t.empty()) return line;
    const char kind = t[0].size() == 1 ? t[0][0] : '\0';
    switch (kind) {
        case 'R': {
            if (t.size() != 5) return line;
            const std::string& name = t[1];
            if (startsWith(name, "VR_") || startsWith(name, "SW") || startsWith(name, "RPU") || startsWith(name, "RPL") ||
                startsWith(name, "RSRC") || startsWith(name, "DCR_") || r.protectedResistors.count(name))
                return line;
            double ohms = 0.0;
            try { ohms = evalExpression(t[4]); } catch (const std::exception&) { return line; }
            if (ohms < 10.0 || ohms > 1e8 || r.cond.unit <= 0) return line;
            t[4] = factor(t[4], 1.0 + 0.05 * unitNoise(r.cond.unit, 'R', name));
            break;
        }
        case 'C': {
            if (t.size() != 5) return line;
            const std::string& name = t[1];
            if (startsWith(name, "CPU") || startsWith(name, "CCAB")) return line;
            double farads = 0.0;
            try { farads = evalExpression(t[4]); } catch (const std::exception&) { return line; }
            const bool electrolytic = farads >= 4.7e-6;
            double f = 1.0;
            if (r.cond.unit > 0) f *= 1.0 + (electrolytic ? 0.20 : 0.10) * unitNoise(r.cond.unit, 'C', name);
            if (electrolytic) f *= 1.0 - 0.45 * r.wear;
            if (f == 1.0) return line;
            t[4] = factor(t[4], f);
            break;
        }
        case 'T': {
            if (t.size() != 6 && t.size() != 7) return line;
            double f = 1.0 - 0.15 * r.wear;  // preamp tubes lose some emission, hence some gain
            if (r.cond.unit > 0) f *= 1.0 + 0.05 * unitNoise(r.cond.unit, 'T', t[1]);
            if (f == 1.0) return line;
            const std::string base = t.size() == 7 ? t[6] : "1";
            if (t.size() == 6) t.push_back("");
            t[6] = factor(base, f);
            break;
        }
        case 'P': {
            if (t.size() != 8) return line;
            if (r.tube) t[6] = r.tube->model;
            double f = 1.0 - 0.30 * r.wear;  // worn output tubes pull less current at the same bias
            if (r.cond.unit > 0) f *= 1.0 + 0.08 * unitNoise(r.cond.unit, 'P', t[1]);
            if (f != 1.0) t[7] = factor(t[7], f);
            break;
        }
        case 'S': {
            if (t.size() != 5 || r.biasSource.empty() || t[1] != r.biasSource || r.biasScale == 1.0) return line;
            t[4] = factor(t[4], r.biasScale);
            break;
        }
        default:
            return line;
    }
    std::string out;
    for (const auto& w : t) {
        if (!out.empty()) out += ' ';
        out += w;
    }
    return out;
}

std::string rewriteNetlist(const std::string& text, const Rules& r) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    std::istringstream is(text);
    for (std::string line; std::getline(is, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string body = line, comment;
        if (const auto semi = line.find(';'); semi != std::string::npos) {
            body = line.substr(0, semi);
            comment = line.substr(semi);
        }
        const auto first = body.find_first_not_of(" \t");
        if (first != std::string::npos && body[first] != '*' && body[first] != '.') {
            const std::string rewritten = rewriteLine(body, r);
            if (rewritten != body) body = rewritten + (comment.empty() ? "" : " ");
        }
        out += body + comment + "\n";
    }
    return out;
}

}  // namespace

AmpDef applyCondition(const AmpDef& def, const Condition& c) {
    if (c == Condition{}) return def;
    AmpDef out = def;
    Rules r;
    r.cond = c;
    r.wear = std::clamp(c.wear, 0, 10) / 10.0;
    r.protectedResistors.insert(def.power.loadResistor);
    if (!def.power.nfbResistor.empty()) r.protectedResistors.insert(def.power.nfbResistor);
    if (!def.power.presenceResistor.empty()) r.protectedResistors.insert(def.power.presenceResistor);
    if (!def.power.presenceComplement.empty()) r.protectedResistors.insert(def.power.presenceComplement);
    for (const auto& k : def.power.knobs) r.protectedResistors.insert(k.resistor);
    for (const auto& s : def.preamp.sourceResistors) r.protectedResistors.insert(s);
    for (const auto& k : def.preamp.knobs) {
        for (const auto* n : {&k.pot.top, &k.pot.bottom, &k.pot.resistor})
            if (!n->empty()) r.protectedResistors.insert(*n);
        for (const auto& a : k.pot.also) {
            r.protectedResistors.insert(a.first);
            r.protectedResistors.insert(a.second);
        }
    }

    // Output tube type and the bias that keeps the same fraction of its rating.
    if (!def.power.tubes.empty()) {
        const int idx = std::clamp(c.tube, 0, int(def.power.tubes.size()) - 1);
        if (idx > 0) {
            r.tube = &def.power.tubes[std::size_t(idx)];
            r.biasScale = r.tube->biasScale;
        }
    }
    r.biasSource = def.power.bias.source;

    auto preamp = [&](PreampDef& p) {
        Rules pr = r;
        pr.tube = nullptr;
        pr.biasSource.clear();
        p.netlistText = rewriteNetlist(p.netlistText, pr);
        for (auto& s : p.stages) s.netlistText = rewriteNetlist(s.netlistText, pr);
    };
    preamp(out.preamp);
    if (out.hasEcoPreamp) preamp(out.ecoPreamp);
    out.power.netlistText = rewriteNetlist(def.power.netlistText, r);
    out.power.supply.reservoirFarads = def.power.supply.reservoirFarads * (1.0 - 0.45 * r.wear);
    return out;
}

}  // namespace sas
