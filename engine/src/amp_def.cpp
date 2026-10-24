#include "aes/amp_def.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace aes {

namespace {

using nlohmann::json;

[[noreturn]] void bad(const std::string& path, const std::string& what) {
    throw std::runtime_error("amp.json: " + path + ": " + what);
}

const json& need(const json& j, const char* key, const std::string& path) {
    if (!j.is_object() || !j.contains(key)) bad(path, std::string("missing '") + key + "'");
    return j.at(key);
}

double num(const json& j, const char* key, const std::string& path) {
    const json& v = need(j, key, path);
    if (!v.is_number()) bad(path + "." + key, "expected a number");
    return v.get<double>();
}

std::string str(const json& j, const char* key, const std::string& path) {
    const json& v = need(j, key, path);
    if (!v.is_string()) bad(path + "." + key, "expected a string");
    return v.get<std::string>();
}

double numOr(const json& j, const char* key, double fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}

Taper taper(const json& j, const std::string& path) {
    const std::string t = str(j, "taper", path);
    if (t == "log") return Taper::Log;
    if (t == "linear") return Taper::Linear;
    bad(path + ".taper", "expected 'log' or 'linear', got '" + t + "'");
}

PreampDef parsePreamp(const json& j, const FileReader& read) {
    PreampDef p;
    p.netlist = str(j, "netlist", "preamp");
    p.netlistText = read(p.netlist);
    p.tolerance = num(j, "tolerance", "preamp");
    if (j.contains("stages"))
        for (const auto& s : j.at("stages")) {
            PreampStageDef sd;
            sd.netlist = str(s, "netlist", "preamp.stages");
            sd.netlistText = read(sd.netlist);
            sd.linkFrom = str(s, "link_from", "preamp.stages." + sd.netlist);
            sd.linkSource = str(s, "link_source", "preamp.stages." + sd.netlist);
            p.stages.push_back(std::move(sd));
        }
    p.railSource = str(j, "rail_source", "preamp");
    p.inputSource = str(j, "input_source", "preamp");
    p.outputNode = str(j, "output_node", "preamp");
    p.potMinOhms = numOr(j, "pot_min_ohms", 100);

    for (const auto& k : need(j, "knobs", "preamp")) {
        KnobDef kd;
        kd.name = str(k, "name", "preamp.knobs");
        const std::string path = "preamp.knobs." + kd.name;
        kd.def = num(k, "default", path);
        const json& pot = need(k, "pot", path);
        const std::string type = str(pot, "type", path + ".pot");
        kd.pot.total = num(pot, "total", path + ".pot");
        kd.pot.taper = taper(pot, path + ".pot");
        if (type == "wiper") {
            kd.pot.type = PotDef::Type::Wiper;
            kd.pot.top = str(pot, "top", path + ".pot");
            kd.pot.bottom = str(pot, "bottom", path + ".pot");
        } else if (type == "variable") {
            kd.pot.type = PotDef::Type::Variable;
            kd.pot.resistor = str(pot, "resistor", path + ".pot");
        } else {
            bad(path + ".pot.type", "expected 'wiper' or 'variable'");
        }
        p.knobs.push_back(std::move(kd));
    }

    const json& so = need(j, "source_ohms", "preamp");
    for (const auto& r : need(so, "resistors", "preamp.source_ohms")) p.sourceResistors.push_back(r.get<std::string>());
    p.sourceOhmsDefault = num(so, "default", "preamp.source_ohms");
    p.sourceOhmsMin = num(so, "min", "preamp.source_ohms");

    for (const auto& jk : need(j, "jacks", "preamp")) {
        JackDef jd;
        jd.name = str(jk, "name", "preamp.jacks");
        jd.label = jk.contains("label") ? str(jk, "label", "preamp.jacks." + jd.name) : jd.name;
        jd.levelDb = numOr(jk, "level_db", 0.0);
        jd.drive = str(jk, "drive", "preamp.jacks." + jd.name);
        for (const auto& [name, ohms] : need(jk, "resistors", "preamp.jacks." + jd.name).items())
            jd.resistors.push_back({name, ohms.get<double>()});
        p.jacks.push_back(std::move(jd));
    }
    if (p.jacks.empty()) bad("preamp.jacks", "at least one jack is required");
    if (j.contains("jack_label")) p.jackLabel = str(j, "jack_label", "preamp");
    return p;
}

PowerDef parsePower(const json& j, const FileReader& read) {
    PowerDef p;
    p.netlist = str(j, "netlist", "power");
    p.netlistText = read(p.netlist);
    p.tolerance = num(j, "tolerance", "power");
    p.inputSource = str(j, "input_source", "power");
    p.htSource = str(j, "ht_source", "power");
    p.htNode = str(j, "ht_node", "power");
    p.railNode = str(j, "rail_node", "power");
    p.outputNode = str(j, "output_node", "power");
    p.loadResistor = str(j, "load_resistor", "power");
    p.nfbResistor = str(j, "nfb_resistor", "power");
    const json& pr = need(j, "presence", "power");
    p.presenceResistor = str(pr, "resistor", "power.presence");
    p.presenceScale = num(pr, "scale", "power.presence");
    p.presenceMinOhms = num(pr, "min_ohms", "power.presence");
    p.presenceDefault = num(pr, "default", "power.presence");
    if (pr.contains("complement")) p.presenceComplement = str(pr, "complement", "power.presence");
    if (j.contains("knobs"))
        for (const auto& k : j.at("knobs")) {
            PowerKnobDef kd;
            kd.name = str(k, "name", "power.knobs");
            const std::string path = "power.knobs." + kd.name;
            kd.resistor = str(k, "resistor", path);
            kd.scale = num(k, "scale", path);
            kd.minOhms = numOr(k, "min_ohms", 50);
            kd.def = num(k, "default", path);
            p.knobs.push_back(std::move(kd));
        }
    const json& s = need(j, "supply", "power");
    p.supply.mainsHz = num(s, "mains_hz", "power.supply");
    p.supply.vacPeak = num(s, "vac_peak", "power.supply");
    p.supply.rectSourceOhms = num(s, "rect_source_ohms", "power.supply");
    p.supply.htDcSource = num(s, "ht_dc_source", "power.supply");
    p.supply.rectDrop = num(s, "rect_drop", "power.supply");
    p.supply.reservoirFarads = num(s, "reservoir_farads", "power.supply");
    p.supply.tieOhms = num(s, "tie_ohms", "power.supply");
    p.supply.settleSeconds = num(s, "settle_seconds", "power.supply");
    return p;
}

LiteParams parseLite(const json& j) {
    LiteParams l;
    l.baseGainDb = num(j, "base_gain_db", "lite");
    l.highPassHz = num(j, "high_pass_hz", "lite");
    l.shelfHz = num(j, "shelf_hz", "lite");
    l.presenceDb.clear();
    for (const auto& v : need(j, "presence_db", "lite")) l.presenceDb.push_back(v.get<double>());
    if (l.presenceDb.size() != 5) bad("lite.presence_db", "expected 5 values (presence 0, .25, .5, .75, 1)");
    l.sagTable.clear();
    for (const auto& row : need(j, "sag_table", "lite")) {
        if (!row.is_array() || row.size() != 2) bad("lite.sag_table", "rows are [level, sag] pairs");
        l.sagTable.push_back({row[0].get<double>(), row[1].get<double>()});
    }
    if (l.sagTable.size() < 2) bad("lite.sag_table", "needs at least two rows");
    l.railXIdle = num(j, "rail_x_idle", "lite");
    l.railSagFraction = num(j, "rail_sag_fraction", "lite");
    l.ceilingLight = num(j, "ceiling_light", "lite");
    l.ceilingHeavy = num(j, "ceiling_heavy", "lite");
    l.ceilingDecay = num(j, "ceiling_decay", "lite");
    l.knee = num(j, "knee", "lite");
    l.sagAttackS = num(j, "sag_attack_s", "lite");
    l.sagReleaseS = num(j, "sag_release_s", "lite");
    return l;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

}  // namespace

std::size_t AmpDef::knobIndex(const std::string& n) const {
    for (std::size_t i = 0; i < preamp.knobs.size(); ++i)
        if (preamp.knobs[i].name == n) return i;
    throw std::runtime_error("amp '" + id + "' has no knob '" + n + "'");
}

std::size_t AmpDef::powerKnobIndex(const std::string& n) const {
    for (std::size_t i = 0; i < power.knobs.size(); ++i)
        if (power.knobs[i].name == n) return i;
    throw std::runtime_error("amp '" + id + "' has no power knob '" + n + "'");
}

std::size_t AmpDef::jackIndex(const std::string& n) const {
    for (std::size_t i = 0; i < preamp.jacks.size(); ++i)
        if (preamp.jacks[i].name == n) return i;
    throw std::runtime_error("amp '" + id + "' has no jack '" + n + "'");
}

AmpDef loadAmpDef(const std::string& jsonText, const FileReader& read) {
    json j;
    try {
        j = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("amp.json: ") + e.what());
    }
    AmpDef d;
    d.id = str(j, "id", "");
    d.name = str(j, "name", "");
    d.order = int(numOr(j, "order", 1000));
    d.outputDb = numOr(j, "output_db", 0.0);
    if (j.contains("ui"))
        for (const auto& u : j.at("ui")) {
            UiKnob k;
            k.param = str(u, "param", "ui");
            k.label = str(u, "label", "ui." + k.param);
            if (u.contains("knob")) k.knob = str(u, "knob", "ui." + k.param);
            if (u.contains("power_knob")) k.powerKnob = str(u, "power_knob", "ui." + k.param);
            d.ui.push_back(std::move(k));
        }
    d.preamp = parsePreamp(need(j, "preamp", ""), read);
    if (j.contains("eco_preamp")) {
        json merged = j.at("preamp");
        for (const auto& [key, value] : j.at("eco_preamp").items()) merged[key] = value;
        d.ecoPreamp = parsePreamp(merged, read);
        d.hasEcoPreamp = true;
    }
    d.power = parsePower(need(j, "power", ""), read);
    d.lite = parseLite(need(j, "lite", ""));
    return d;
}

AmpDef loadAmpDefFromFolder(const std::string& folder) {
    return loadAmpDef(readFile(folder + "/amp.json"), [&](const std::string& f) { return readFile(folder + "/" + f); });
}

}  // namespace aes
