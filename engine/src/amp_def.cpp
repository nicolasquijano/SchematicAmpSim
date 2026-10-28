#include "sas/amp_def.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace sas {

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

std::uint32_t colourOr(const json& j, const char* key, std::uint32_t fallback) {
    if (!j.contains(key)) return fallback;
    const json& v = j.at(key);
    if (!v.is_string()) bad(std::string("theme.") + key, "expected a string like \"#d4a017\"");
    const std::string s = v.get<std::string>();
    if (s.size() != 7 || s[0] != '#') bad(std::string("theme.") + key, "expected #rrggbb, got '" + s + "'");
    try {
        return std::uint32_t(std::stoul(s.substr(1), nullptr, 16));
    } catch (const std::exception&) {
        bad(std::string("theme.") + key, "not a hex colour: '" + s + "'");
    }
}

double numOr(const json& j, const char* key, double fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}

Taper taper(const json& j, const std::string& path) {
    const std::string t = str(j, "taper", path);
    if (t == "log") return Taper::Log;
    if (t == "linear") return Taper::Linear;
    if (t == "log10") return Taper::Log10;
    if (t == "log30") return Taper::Log30;
    if (t == "revlog") return Taper::RevLog;
    bad(path + ".taper", "expected 'log', 'log10', 'log30', 'revlog' or 'linear', got '" + t + "'");
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
            if (pot.contains("also"))
                for (const auto& a : pot.at("also")) kd.pot.also.push_back({str(a, "top", path + ".pot.also"), str(a, "bottom", path + ".pot.also")});
        } else if (type == "variable") {
            kd.pot.type = PotDef::Type::Variable;
            kd.pot.resistor = str(pot, "resistor", path + ".pot");
            if (pot.contains("also"))
                for (const auto& a : pot.at("also")) kd.pot.also.push_back({str(a, "resistor", path + ".pot.also"), ""});
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
    if (j.contains("reverb")) {
        const json& r = j.at("reverb");
        ReverbDef& d = p.reverb;
        d.present = true;
        d.netlist = str(r, "netlist", "preamp.reverb");
        d.netlistText = read(d.netlist);
        d.railSource = str(r, "rail_source", "preamp.reverb");
        d.gridSource = str(r, "grid_source", "preamp.reverb");
        d.gridNode = str(r, "grid_node", "preamp.reverb");
        d.outNode = str(r, "out_node", "preamp.reverb");
        d.mainSource = str(r, "main_source", "preamp.reverb");
        d.tankInNode = str(r, "tank_in_node", "preamp.reverb");
        d.tankOutSource = str(r, "tank_out_source", "preamp.reverb");
        d.gain = num(r, "gain", "preamp.reverb");
        d.inputLowHz = numOr(r, "input_low_hz", d.inputLowHz);
        d.inputHighHz = numOr(r, "input_high_hz", d.inputHighHz);
        d.outputHighHz = numOr(r, "output_high_hz", d.outputHighHz);
        d.tankRateHz = numOr(r, "tank_rate_hz", d.tankRateHz);
        for (const auto& s : need(r, "springs", "preamp.reverb")) {
            SpringDef sp;
            sp.delayMs = num(s, "delay_ms", "preamp.reverb.springs");
            sp.sections = std::size_t(num(s, "sections", "preamp.reverb.springs"));
            sp.coefficient = num(s, "coefficient", "preamp.reverb.springs");
            sp.loopGain = num(s, "loop_gain", "preamp.reverb.springs");
            sp.lossHz = num(s, "loss_hz", "preamp.reverb.springs");
            d.springs.push_back(sp);
        }
        if (d.springs.empty()) bad("preamp.reverb.springs", "at least one spring is required");
    }
    if (j.contains("vibrato")) {
        const json& v = j.at("vibrato");
        VibratoDef& d = p.vibrato;
        d.present = true;
        d.netlist = str(v, "netlist", "preamp.vibrato");
        d.netlistText = read(d.netlist);
        d.railSource = str(v, "rail_source", "preamp.vibrato");
        const json& led = need(v, "led", "preamp.vibrato");
        d.ledFrom = str(led, "from", "preamp.vibrato.led");
        d.ledTo = str(led, "to", "preamp.vibrato.led");
        d.ledOhms = num(led, "ohms", "preamp.vibrato.led");
        const json& sw = need(v, "switch", "preamp.vibrato");
        d.switchResistor = str(sw, "resistor", "preamp.vibrato.switch");
        d.switchOnOhms = num(sw, "on_ohms", "preamp.vibrato.switch");
        d.switchOffOhms = num(sw, "off_ohms", "preamp.vibrato.switch");
        const json& sp = need(v, "speed", "preamp.vibrato");
        d.speedResistor = str(sp, "resistor", "preamp.vibrato.speed");
        d.speedTotal = num(sp, "total", "preamp.vibrato.speed");
        d.speedTaper = taper(sp, "preamp.vibrato.speed");
        d.speedInvert = sp.contains("invert") && sp.at("invert").get<bool>();
        d.speedDefault = num(sp, "default", "preamp.vibrato.speed");
        const json& c = need(v, "cell", "preamp.vibrato");
        d.cellResistor = str(c, "resistor", "preamp.vibrato.cell");
        d.cellDarkOhms = num(c, "dark_ohms", "preamp.vibrato.cell");
        d.cellOhmsAt1mA = num(c, "ohms_at_1ma", "preamp.vibrato.cell");
        d.cellExponent = num(c, "exponent", "preamp.vibrato.cell");
        d.cellAttackS = num(c, "attack_s", "preamp.vibrato.cell");
        d.cellReleaseS = num(c, "release_s", "preamp.vibrato.cell");
        d.controlSamples = std::size_t(numOr(v, "control_samples", 64));
    }
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
    if (j.contains("nfb_resistor")) p.nfbResistor = str(j, "nfb_resistor", "power");
    // An amp without a presence control (the Fender) omits the block.
    if (j.contains("presence")) {
        const json& pr = j.at("presence");
        p.presenceResistor = str(pr, "resistor", "power.presence");
        p.presenceScale = num(pr, "scale", "power.presence");
        p.presenceMinOhms = num(pr, "min_ohms", "power.presence");
        p.presenceDefault = num(pr, "default", "power.presence");
        if (pr.contains("complement")) p.presenceComplement = str(pr, "complement", "power.presence");
    }
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
    if (j.contains("tubes"))
        for (const auto& t : j.at("tubes")) {
            PowerTubeDef td;
            td.name = str(t, "name", "power.tubes");
            td.model = str(t, "model", "power.tubes");
            td.pmaxW = num(t, "pmax_w", "power.tubes");
            td.biasScale = numOr(t, "bias_scale", 1.0);
            p.tubes.push_back(std::move(td));
        }
    if (j.contains("bias")) {
        const json& b = j.at("bias");
        if (b.contains("source")) p.bias.source = str(b, "source", "power.bias");
        for (const auto& r : need(b, "idle_resistors", "power.bias")) p.bias.idleResistors.push_back(r.get<std::string>());
        p.bias.tubesPerHalf = num(b, "tubes_per_half", "power.bias");
    }
    if (j.contains("transformer_parasitics")) {
        const json& tp = j.at("transformer_parasitics");
        if (tp.contains("enabled")) p.transformer.enabled = tp.at("enabled").get<bool>();
        p.transformer.primaryPf = numOr(tp, "primary_pf", p.transformer.primaryPf);
        p.transformer.coreLossOhms = numOr(tp, "core_loss_ohms", p.transformer.coreLossOhms);
    }
    if (j.contains("speaker_load")) {
        const json& sl = j.at("speaker_load");
        if (sl.contains("enabled")) p.speakerLoad.enabled = sl.at("enabled").get<bool>();
        p.speakerLoad.reFraction = numOr(sl, "re_fraction", p.speakerLoad.reFraction);
        p.speakerLoad.leMhPerOhm = numOr(sl, "le_mh_per_ohm", p.speakerLoad.leMhPerOhm);
        p.speakerLoad.peakFraction = numOr(sl, "peak_fraction", p.speakerLoad.peakFraction);
        p.speakerLoad.resonanceHz = numOr(sl, "resonance_hz", p.speakerLoad.resonanceHz);
        p.speakerLoad.q = numOr(sl, "q", p.speakerLoad.q);
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
    if (s.contains("rect_tube")) {
        const json& t = s.at("rect_tube");
        p.supply.tubeVolts = num(t, "volts", "power.supply.rect_tube");
        p.supply.tubeAmps = num(t, "amps", "power.supply.rect_tube");
    }
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
    if (j.contains("theme")) {
        const json& t = j.at("theme");
        d.theme.background = colourOr(t, "background", d.theme.background);
        d.theme.panel = colourOr(t, "panel", d.theme.panel);
        d.theme.edge = colourOr(t, "edge", d.theme.edge);
        d.theme.accent = colourOr(t, "accent", d.theme.accent);
        d.theme.label = colourOr(t, "label", d.theme.label);
        d.theme.text = colourOr(t, "text", d.theme.text);
        d.theme.knobTop = colourOr(t, "knob_top", d.theme.knobTop);
        d.theme.knobBottom = colourOr(t, "knob_bottom", d.theme.knobBottom);
        d.theme.pointer = colourOr(t, "pointer", d.theme.pointer);
    }
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

}  // namespace sas
