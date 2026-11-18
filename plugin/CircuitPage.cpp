#include "CircuitPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>

#include "I18n.h"

#ifdef SAS_HAS_FACES
#include "BinaryData.h"
#endif

namespace {
// "4k7" is 4700, "0.082u" 8.2e-8: the netlists' notation.
double parseValue(const juce::String& s) {
    int at = -1;
    for (int i = 0; i < s.length() && at < 0; ++i)
        if (juce::String("pnumkKMGR").containsChar(s[i])) at = i;
    if (at < 0) return s.getDoubleValue();
    double mult = 1.0;
    switch (s[at]) {
        case 'p': mult = 1e-12; break;
        case 'n': mult = 1e-9; break;
        case 'u': mult = 1e-6; break;
        case 'm': mult = 1e-3; break;
        case 'k': case 'K': mult = 1e3; break;
        case 'M': mult = 1e6; break;
        case 'G': mult = 1e9; break;
        default: break;
    }
    const auto head = s.substring(0, at), tail = s.substring(at + 1);
    return (tail.isEmpty() ? head : head + "." + tail).getDoubleValue() * mult;
}

juce::String formatValue(double v, bool cap) {
    auto g = [](double x) { return juce::String::formatted("%.3g", x); };
    if (cap) {
        if (v >= 1e-6) return g(v * 1e6) + juce::String(juce::CharPointer_UTF8("\xc2\xb5"));
        return v >= 1e-9 ? g(v * 1e9) + "n" : g(v * 1e12) + "p";
    }
    return v >= 1e6 ? g(v / 1e6) + "M" : v >= 1e3 ? g(v / 1e3) + "k" : g(v);
}

// The next preferred value (E12) above or below v.
double stepE12(double v, int dir) {
    static const double e12[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
    const double e = std::floor(std::log10(v)), m = v / std::pow(10.0, e);
    if (dir > 0) {
        for (const double s : e12)
            if (s > m * 1.01) return s * std::pow(10.0, e);
        return std::pow(10.0, e + 1.0);
    }
    for (int i = 11; i >= 0; --i)
        if (e12[i] < m * 0.99) return e12[i] * std::pow(10.0, e);
    return 8.2 * std::pow(10.0, e - 1.0);
}

// What each component of the preamp does, read from where it sits in the netlists: a resistor from a plate to a supply is a plate load, one from a
// cathode to ground a cathode resistor, a capacitor across it a bypass, and so on. The names repeat from stage to stage only for the same part.
struct PowerHints {  // what the power amp's netlist is made of: its output node, the load, the input source and the supply nodes
    std::string output, load, input, ht, rail;
};

void classifyNetlist(const std::string& text, std::map<juce::String, juce::String>& roles, std::map<juce::String, std::pair<juce::String, bool>>& tubes,
                     const PowerHints* hints = nullptr) {
    struct El {
        std::string name, a, b, value;
        bool cap;
    };
    std::vector<El> els;
    std::set<std::string> grids, plates, cathodes, screens, supplies;
    std::vector<std::array<std::string, 3>> opamps;  // plus, minus, out
    struct Triode {
        std::string name, model, plate, cathode;
    };
    std::vector<Triode> triodes;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (const auto c = line.find(';'); c != std::string::npos) line.resize(c);
        std::istringstream ls(line);
        std::vector<std::string> t;
        for (std::string w; ls >> w;) t.push_back(w);
        if (t.size() < 4 || t[0][0] == '*') continue;
        const auto& kind = t[0];
        if (kind == "R" || kind == "C") els.push_back({t[1], t[2], t[3], t.size() > 4 ? t[4] : std::string(), kind == "C"});
        else if (kind == "T" && t.size() >= 5) {
            grids.insert(t[2]); plates.insert(t[3]); cathodes.insert(t[4]);
            triodes.push_back({t[1], t.size() > 5 ? t[5] : std::string(), t[3], t[4]});
        }
        else if (kind == "P" && t.size() >= 6) { grids.insert(t[2]); plates.insert(t[3]); screens.insert(t[4]); cathodes.insert(t[5]); }
        else if ((kind == "O" || kind == "U") && t.size() >= 5) opamps.push_back({t[2], t[3], t[4]});
        else if (kind == "V") supplies.insert(t[2]);
        else if (kind == "S" && hints && t[1] != hints->input) supplies.insert(t[2]);  // the power amp's bias and screen supplies
    }
    if (hints) {
        if (!hints->ht.empty()) supplies.insert(hints->ht);
        if (!hints->rail.empty()) supplies.insert(hints->rail);
    }
    std::map<std::string, int> cathodeUsers;  // two triodes on one cathode node: a long-tailed pair (the phase inverter)
    for (const auto& tt : triodes) ++cathodeUsers[tt.cathode];
    std::set<std::string> pairPlates, pairCathodes;
    for (const auto& tt : triodes)
        if (cathodeUsers[tt.cathode] >= 2) {
            pairPlates.insert(tt.plate);
            pairCathodes.insert(tt.cathode);
        }
    auto ground = [](const std::string& n) { return n == "0" || n == "gnd"; };
    std::set<std::pair<std::string, std::string>> resistorPairs;  // the nodes of every resistor and pot section
    for (const auto& e : els)
        if (!e.cap) resistorPairs.insert({std::min(e.a, e.b), std::max(e.a, e.b)});
    auto has = [](const std::set<std::string>& s, const std::string& n) { return s.count(n) != 0; };
    for (const auto& tt : triodes) tubes[juce::String(tt.name)] = {juce::String(tt.model), has(supplies, tt.plate)};
    for (const auto& e : els) {
        if (e.name.rfind("SW_", 0) == 0 || e.name.rfind("VR", 0) == 0) continue;
        const bool anyGround = ground(e.a) || ground(e.b);
        auto touches = [&](const std::set<std::string>& s) { return has(s, e.a) || has(s, e.b); };
        auto other = [&](const std::set<std::string>& s) { return has(s, e.a) ? e.b : e.a; };
        const char* role = nullptr;
        bool feedback = false;
        for (const auto& o : opamps) feedback = feedback || ((e.a == o[1] && e.b == o[2]) || (e.b == o[1] && e.a == o[2]));
        if (feedback) {
            role = e.cap ? "feedback_c" : "feedback_r";
        } else if (hints && !e.cap && (e.a == hints->output || e.b == hints->output) && e.name != hints->load && e.name.rfind("DCR", 0) != 0) {
            role = "nfb";  // from the output transformer's tap back to the phase inverter
        } else if (e.cap) {
            if (touches(cathodes)) role = "bypass";
            else if ((has(supplies, e.a) && ground(e.b)) || (has(supplies, e.b) && ground(e.a))) role = "filter";
            else if (resistorPairs.count({std::min(e.a, e.b), std::max(e.a, e.b)}) != 0) role = "bright";
            else if (anyGround) role = "shunt_c";
            else role = touches(plates) || touches(grids) ? "coupling" : "tone_c";
        } else if (touches(plates) && has(supplies, other(plates))) {
            role = has(pairPlates, has(plates, e.a) ? e.a : e.b) ? "pi_plate" : "plate";
        } else if (touches(cathodes)) {
            role = touches(pairCathodes) ? "pi_tail" : "cathode";
        } else if (touches(screens)) {
            role = "screen";
        } else if (touches(grids)) {
            role = anyGround || parseValue(juce::String(e.value)) >= 100e3 ? "grid_leak" : "grid_stopper";
        } else if (!anyGround && (has(supplies, e.a) || has(supplies, e.b))) {
            role = "feed_r";
        } else {
            role = anyGround ? "shunt_r" : "series_r";
        }
        roles[juce::String(e.name)] = role;
    }
}

struct RoleText {
    const char *role, *title, *body;
};
const RoleText kRoleTexts[] = {
    {"plate", "Plate load", "Sets the gain of this stage. More ohms: more gain, earlier clipping and a little less treble. Fewer: cleaner and brighter."},
    {"cathode", "Cathode resistor", "Sets the tube's bias. More ohms: less current, colder and tighter, with less gain. Fewer: hotter and fatter, with more gain."},
    {"bypass", "Cathode bypass", "Lets the stage keep its gain at low frequencies. More capacity: fatter, more bass and gain. Less: tighter and thinner."},
    {"grid_leak", "Grid leak", "Loads the stage before it. Fewer ohms: less level and less bass. More ohms: more level, but the tube can block when pushed hard."},
    {"grid_stopper", "Grid stopper", "With the tube's own capacitance it filters the highs and softens the clipping. More ohms: darker and smoother."},
    {"screen", "Screen resistor", "Limits the screen current. More ohms: softer, with less power. Fewer: harder and with more power."},
    {"pi_plate", "Phase inverter plate load", "With the load of the other half it sets the balance between the two output tubes. Unequal loads: more even-order distortion and less clean power."},
    {"pi_tail", "Phase inverter tail resistor", "Sets the inverter's gain and balance. More ohms: better balance but less gain and headroom. Fewer: more gain and less balance."},
    {"nfb", "Negative feedback", "Brings the output back to the phase inverter. More ohms: less feedback, so looser, with more gain and a livelier feel. Fewer: tighter and cleaner, with less gain."},
    {"feed_r", "Supply or bias resistor", "Feeds a supply or a bias voltage. More ohms: more drop and a softer supply. Fewer: stiffer."},
    {"series_r", "Series resistor", "Part of the signal path, it forms a divider or a filter with its neighbours. More ohms: less level and a darker tone."},
    {"shunt_r", "Resistor to ground", "Divides or loads the signal with the parts around it. More ohms: more level and less loading. Fewer: less level."},
    {"coupling", "Coupling capacitor", "Passes the signal on and blocks the DC. More capacity: more bass gets through. Less: tighter and thinner."},
    {"tone_c", "Tone capacitor", "Sets the frequency where the tone network acts. More capacity: the corner moves down, with more bass. Less: it moves up."},
    {"shunt_c", "Capacitor to ground", "Sends highs to ground. More capacity: darker. Less: brighter."},
    {"bright", "Bright capacitor", "Lets the highs skip a resistor or a pot. More capacity: more bite, above all with the control turned down. Less: subtler."},
    {"filter", "Supply filter", "Smooths the supply. Less capacity: more hum and more sag. More: steadier."},
    {"feedback_r", "Feedback resistor", "Sits in the op-amp's loop. More ohms: more gain. Fewer: less gain and a cleaner stage."},
    {"feedback_c", "Feedback capacitor", "Rolls off the highs of the op-amp stage. More capacity: darker. Less: brighter."},
};
}  // namespace

CircuitPage::CircuitPage(SasProcessor& processor, const AmpLookAndFeel& look) : processor_(processor), look_(look) {
    addAndMakeVisible(host_);  // before the buttons, so they stay above the drawings
    host_.setInterceptsMouseClicks(false, false);
    valuesButton_.setButtonText(tr("VALUES"));
    valuesButton_.setClickingTogglesState(true);
    valuesButton_.setToggleState(showValues_, juce::dontSendNotification);
    valuesButton_.onClick = [this] {
        showValues_ = valuesButton_.getToggleState();
        repaint();
    };
    addAndMakeVisible(valuesButton_);
    resetButton_.setButtonText(tr("RESET"));
    resetButton_.onClick = [this] {
        pending_.clear();
        processor_.clearPartFactors(ampIndex_);
        repaint();
    };
    resetButton_.setVisible(false);
    addChildComponent(resetButton_);
}

CircuitPage::~CircuitPage() = default;

float CircuitPage::fractionOf(float ratio) {
    const float db = 20.0f * std::log10(std::max(ratio, 1.0e-5f));
    return std::clamp((db + kRangeDb) / kRangeDb, 0.0f, 1.0f);
}

// Quiet stages stay the colour of an unlit wire; from about -30 dB they go green, amber from -12 and red from -3.
juce::Colour CircuitPage::levelColour(float db) {
    const juce::Colour quiet(0xff6b5c44), green(0xff6aa84f), amber(0xfff0b53a), red(0xffe5533a);
    if (db <= -40.0f) return quiet;
    if (db < -30.0f) return quiet.interpolatedWith(green, (db + 40.0f) / 10.0f);
    if (db < -12.0f) return green;
    if (db < -3.0f) return green.interpolatedWith(amber, (db + 12.0f) / 9.0f);
    if (db < 0.0f) return amber.interpolatedWith(red, (db + 3.0f) / 3.0f);
    return red;
}

void CircuitPage::collect(Drawing& dr, juce::Drawable& d) {
    const auto id = d.getName();
    if (auto* shape = dynamic_cast<juce::DrawableShape*>(&d)) {
        if (id.startsWith("wire_")) {
            // "wire_V1", or "wire_MASTER-2" for a second piece of the same stage's wire
            const auto key = id.fromFirstOccurrenceOf("wire_", false, false).upToFirstOccurrenceOf("-", false, false);
            for (std::size_t i = 0; i < stages_.size(); ++i)
                if (stages_[i].key == key) dr.wires.push_back({i, shape});
        } else if (id.startsWith("ptr_")) {
            if (auto* path = dynamic_cast<juce::DrawablePath*>(shape)) dr.pointers[id.fromFirstOccurrenceOf("ptr_", false, false)] = path;
        } else if (id.startsWith("glow_")) {
            dr.glows[id.fromFirstOccurrenceOf("glow_", false, false).upToFirstOccurrenceOf("_", false, false)].push_back(shape);
        }
    }
    if (id.startsWith("pot_")) {
        const auto param = id.fromFirstOccurrenceOf("pot_", false, false).upToFirstOccurrenceOf("-", false, false);  // "treble-2": the same knob drawn again
        const auto it = uiLabels_.find(param);  // the name the amp gives its knob ("VOLUME", "MASTER"...)
        if (!id.contains("-")) dr.tags.push_back({&d, it != uiLabels_.end() ? it->second : param.toUpperCase(), 0});  // the copies carry no name
    } else if (id.startsWith("tube_")) {
        const auto rest = id.fromFirstOccurrenceOf("tube_", false, false);  // "V1", or "PA-EL34": what follows the dash is the name shown
        dr.tags.push_back({&d, rest.contains("-") ? rest.fromFirstOccurrenceOf("-", false, false) : rest, 1});
        // A triode of the preamp (its name is an element of the netlist, or the envelope of two: V1 for V1A and V1B) can be swapped for another model.
        const auto key = rest.upToFirstOccurrenceOf("-", false, false);
        Drawing::Tube tube;
        for (const auto& [name, info] : loadingPower_ ? powerTubes_ : tubes_) {  // in the power amp, "PI" is the phase inverter: its triodes
            const bool match = name == key || (name.length() == key.length() + 1 && name.startsWith(key) && juce::CharacterFunctions::isUpperCase(name.getLastCharacter()))
                               || (loadingPower_ && key == "PI");
            if (!match) continue;
            if (tube.key.isEmpty()) {
                tube.key = key;
                tube.drawn = info.model;
                tube.follower = info.follower;
                tube.phase = loadingPower_;
            }
        }
        if (tube.key.isEmpty() && loadingPower_ && key.isNotEmpty()) {  // the other tubes of the power amp are the output tubes
            tube.key = key;
            tube.drawn = rest.contains("-") ? rest.fromFirstOccurrenceOf("-", false, false) : key;
            tube.output = true;
        }
        if (tube.key.isNotEmpty()) {
            tube.item = &d;
            dr.tubes.push_back(tube);
        }
    } else if (id == "jack_in") {
        dr.tags.push_back({&d, "INPUT", 0});
    } else if (id.startsWith("jack_") && !id.endsWith("_hole")) {  // an amp with several inputs: "jack_BRIGHT" is named BRIGHT
        dr.tags.push_back({&d, id.fromFirstOccurrenceOf("jack_", false, false), 0});
    } else if (id == "spk_cone" || id == "spk_cone_p") {
        dr.tags.push_back({&d, "SPEAKER", 0});
    } else if (id.startsWith("ttl_")) {  // the name of a box of parts
        dr.titles.push_back({&d, id.fromFirstOccurrenceOf("ttl_", false, false).replaceCharacter('_', ' ')});
    } else if (id == "next_power") {
        dr.next = &d;
    } else if (id.length() > 6 && id.substring(0, 3) == "val" && id.substring(4, 5) == "_") {
        juce::String text = id.substring(5).fromFirstOccurrenceOf("_", false, false), comp;
        if (text.contains("@")) {
            comp = text.fromFirstOccurrenceOf("@", false, false);
            text = text.upToFirstOccurrenceOf("@", false, false);
        }
        const juce::String raw = text;
        if (text.endsWithChar('u')) text = text.dropLastCharacters(1) + juce::String(juce::CharPointer_UTF8("\xc2\xb5"));
        else if (text.endsWithChar('R')) text = text.dropLastCharacters(1) + juce::String(juce::CharPointer_UTF8("\xce\xa9"));
        Drawing::Value value{&d, text, char(id[3])};
        if (comp.length() > 2 && comp[1] == ':') {
            value.isCap = comp[0] == 'C';
            value.name = comp.substring(2);
            value.base = parseValue(raw);
            if (!(value.base > 0.0)) value.name = {};
        }
        dr.values.push_back(value);
    } else if (id.startsWith("swc_") || id.startsWith("swo_")) {  // "swc_1_SW_NAME": the closed blade of a switch that is closed in the netlist
        const auto rest = id.substring(4);
        auto& sw = dr.switches[rest.fromFirstOccurrenceOf("_", false, false)];
        sw.defaultClosed = rest.startsWithChar('1');
        (id.startsWith("swc_") ? sw.closed : sw.open) = &d;
    } else if (id.startsWith("diode_")) {  // "diode_<stage>_<n>"
        const auto stage = id.substring(6).upToFirstOccurrenceOf("_", false, false);
        if (auto* shape = dynamic_cast<juce::DrawableShape*>(&d))
            for (std::size_t i = 0; i < stages_.size(); ++i)
                if (stages_[i].key == stage) dr.diodes.push_back({shape, i});
    } else if (id.startsWith("lbl_")) {
        dr.labels[id.fromFirstOccurrenceOf("lbl_", false, false)] = &d;
    }
    for (auto* child : d.getChildren())
        if (auto* sub = dynamic_cast<juce::Drawable*>(child)) collect(dr, *sub);
}

void CircuitPage::load(int which, const sas::AmpDef& def) {
    auto& dr = drawings_[std::size_t(which)];
    if (dr.art) host_.removeChildComponent(dr.art.get());
    dr = Drawing{};
#ifdef SAS_HAS_FACES
    int size = 0;
    const std::string resource = def.id + (which == powerIndex() ? "_circuit_power_svg" : which == 0 ? "_circuit_svg" : "_circuit_" + std::to_string(which) + "_svg");
    if (const char* data = BinaryData::getNamedResource(resource.c_str(), size))
        if (auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(data, size)))
            dr.art = juce::Drawable::createFromSVG(*xml);
#else
    juce::ignoreUnused(def);
#endif
    if (!dr.art) return;
    host_.addChildComponent(*dr.art);
    dr.art->setInterceptsMouseClicks(false, false);
    loadingPower_ = which == powerIndex();
    collect(dr, *dr.art);
    if (loadingPower_)
        for (auto& v : dr.values) v.fixed = true;  // the parts of the power amp have a tip, they are not edited
    loadingPower_ = false;
    for (const auto& t : dr.tags) {  // a pot whose name is a knob of the amp is a live control
        if (!t.item->getName().startsWith("pot_")) continue;
        const auto name = t.item->getName().fromFirstOccurrenceOf("pot_", false, false);
        const auto base = name.upToFirstOccurrenceOf("-", false, false);
        if (std::find(uiParams_.begin(), uiParams_.end(), base) == uiParams_.end()) continue;
        Pot p;
        p.body = t.item;
        p.name = name;
        p.param = base;
        p.centre = t.item->getDrawableBounds().getCentre();
        p.length = t.item->getDrawableBounds().getWidth() * 0.31f;
        if (const auto it = dr.pointers.find(name); it != dr.pointers.end()) p.pointer = it->second;
        dr.pots.push_back(p);
    }
}

void CircuitPage::setAmp(const sas::AmpDef& def) {
    pending_.clear();
    roles_.clear();
    tubes_.clear();
    std::map<juce::String, std::pair<juce::String, bool>> found;
    classifyNetlist(def.preamp.netlistText, roles_, found);
    for (const auto& st : def.preamp.stages) classifyNetlist(st.netlistText, roles_, found);
    for (const auto& [name, info] : found) tubes_[name] = {info.first, info.second};
    powerTubes_.clear();
    {
        std::map<juce::String, juce::String> unused;
        std::map<juce::String, std::pair<juce::String, bool>> foundPower;
        const PowerHints hints{def.power.outputNode, def.power.loadResistor, def.power.inputSource, def.power.htNode, def.power.railNode};
        powerRoles_.clear();
        classifyNetlist(def.power.netlistText, powerRoles_, foundPower, &hints);
        for (const auto& [name, info] : foundPower) powerTubes_[name] = {info.first, info.second};
    }
    tubeHover_ = tubeDown_ = -1;
    ampIndex_ = 0;
    for (std::size_t i = 0; i < processor_.amps().size(); ++i)
        if (processor_.amps()[i].id == def.id) ampIndex_ = int(i);
    stages_.clear();
    auto add = [&](const juce::String& key) {
        Stage s;
        s.key = key;
        s.name = tr(key);
        stages_.push_back(s);
    };
    add("INPUT");
    if (!def.nam) {
        if (def.probes.empty()) add("PREAMP");
        for (const auto& p : def.probes) add(juce::String(p.label));
        for (const auto& p : def.powerProbes) add(juce::String(p.label));
    }
    add("SPEAKER");

    jackRes_.clear();
    selRes_.clear();
    for (const auto& jk : def.preamp.jacks) {
        jackRes_.emplace_back();
        for (const auto& [name, ohms] : jk.resistors) jackRes_.back()[juce::String(name)] = ohms;
    }
    for (const auto& sd : def.preamp.selectors) {
        selRes_.emplace_back();
        for (const auto& pos : sd.positions) {
            selRes_.back().emplace_back();
            for (const auto& [name, ohms] : pos.resistors) selRes_.back().back()[juce::String(name)] = ohms;
        }
    }
    uiParams_.clear();
    uiLabels_.clear();
    for (const auto& u : def.ui) {
        uiParams_.push_back(juce::String(u.param));
        uiLabels_[juce::String(u.param)] = juce::String(u.label);
    }
    hover_ = drag_ = {};
    for (auto& dr : drawings_)
        if (dr.art) host_.removeChildComponent(dr.art.get());
    channels_ = def.circuitChannels;
    channelSel_.clear();
    for (const auto& ch : channels_) {
        int index = -1;
        for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
            if (!ch.selector.empty() && def.preamp.selectors[i].name == ch.selector) index = int(i);
        channelSel_.push_back(index);
    }
    drawings_.clear();
    drawings_.resize(std::max<std::size_t>(1, channels_.size()) + 1);
    for (int i = 0; i < int(drawings_.size()); ++i) load(i, def);
    // The buttons: one per channel drawing (named as the amp names the channel), then the power amp.
    viewButtons_.clear();
    const bool several = channelCount() > 1 || hasPowerDrawing();
    for (int i = 0; i < int(drawings_.size()); ++i) {
        if (!drawings_[std::size_t(i)].art) continue;
        auto b = std::make_unique<juce::TextButton>(i == powerIndex() ? tr("POWER") : channels_.empty() ? tr("PREAMP") : tr(juce::String(channels_[std::size_t(i)].label)));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(31);
        b->getProperties().set("view", i);
        b->onClick = [this, i] { setView(i); };
        addChildComponent(*b);
        b->setVisible(several);
        viewButtons_.push_back(std::move(b));
    }
    if (view_ >= int(drawings_.size()) || !drawings_[std::size_t(view_)].art) view_ = 0;
    lastChannel_ = -1;
    setView(view_);
    updatePots();
}

// The channel the amp is set to: the input jack, or a selector, as the amp's own channels are chosen.
int CircuitPage::currentChannel() const {
    const int jack = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        const auto& ch = channels_[i];
        if (!ch.jacks.empty() && std::find(ch.jacks.begin(), ch.jacks.end(), jack) != ch.jacks.end()) return int(i);
        if (channelSel_[i] >= 0 && channelSel_[i] < 6 &&
            std::find(ch.positions.begin(), ch.positions.end(), int(processor_.apvts.getRawParameterValue(ids::sel[channelSel_[i]])->load() + 0.5f)) != ch.positions.end())
            return int(i);
    }
    return -1;
}

void CircuitPage::setView(int v) {
    view_ = (v >= 0 && v < int(drawings_.size()) && drawings_[std::size_t(v)].art) ? v : 0;
    for (std::size_t i = 0; i < drawings_.size(); ++i)
        if (drawings_[i].art) drawings_[i].art->setVisible(int(i) == view_);
    for (auto& b : viewButtons_) b->setToggleState(int(b->getProperties()["view"]) == view_, juce::dontSendNotification);
    hover_ = {};
    zoom_ = 1.0f;
    pan_ = {};
    overview_ = false;
    resized();
    repaint();
}

void CircuitPage::resized() {
    auto area = getLocalBounds().toFloat().reduced(10.0f, 6.0f);
    area.removeFromTop(22.0f);
    strip_ = area.removeFromBottom(64.0f);
    area.removeFromBottom(26.0f);  // room for the names and values under the pots
    {
        auto bar = getLocalBounds().reduced(10, 4).removeFromTop(22);
        for (int i = int(viewButtons_.size()) - 1; i >= 0; --i) {
            viewButtons_[std::size_t(i)]->setBounds(bar.removeFromRight(std::max(72, viewButtons_[std::size_t(i)]->getBestWidthForHeight(22) + 10)));
            bar.removeFromRight(4);
        }
        valuesButton_.setBounds(bar.removeFromRight(std::max(72, valuesButton_.getBestWidthForHeight(22) + 10)));
        bar.removeFromRight(4);
        resetButton_.setBounds(bar.removeFromRight(std::max(64, resetButton_.getBestWidthForHeight(22) + 10)));
    }
    // The drawings live in a host that clips them to the schematic's area; a wide one starts at a readable size (not squeezed to fit) and the player
    // drags it along, wheels to zoom and double-clicks to see all of it.
    host_.setBounds(area.toNearestInt());
    const auto local = juce::Rectangle<float>(0.0f, 0.0f, float(host_.getWidth()), float(host_.getHeight()));
    for (auto& dr : drawings_) {
        if (!dr.art) continue;
        const auto src = dr.art->getDrawableBounds();
        const juce::RectanglePlacement placement(juce::RectanglePlacement::centred);
        auto base = local;
        const float fitScale = std::min(local.getWidth() / src.getWidth(), local.getHeight() / src.getHeight());
        if (!overview_ && fitScale < kReadableScale)
            base = juce::Rectangle<float>(0.0f, std::max(0.0f, (local.getHeight() - src.getHeight() * kReadableScale) * 0.5f), src.getWidth() * kReadableScale, src.getHeight() * kReadableScale);
        const auto c = local.getCentre();
        const auto shown = juce::Rectangle<float>(base.getWidth() * zoom_, base.getHeight() * zoom_).withCentre(c + (base.getCentre() - c) * zoom_ + pan_);
        dr.fit = placement.getTransformToFit(src, shown).translated(float(host_.getX()), float(host_.getY()));
        dr.art->setTransformToFit(shown, placement);
    }
}

double CircuitPage::factorOf(const juce::String& name) const {
    if (const auto it = pending_.find(name); it != pending_.end()) return it->second;
    return processor_.partFactor(ampIndex_, name.toStdString());
}

juce::String CircuitPage::shownText(const Drawing::Value& v) const {
    if (v.name.isEmpty()) return v.text;
    const double f = valueFactor(v);
    return std::abs(f - 1.0) < 1e-6 ? v.text : formatValue(v.base * f, v.isCap);
}

juce::Rectangle<float> CircuitPage::valueBox(const Drawing& dr, const Drawing::Value& v) const {
    const auto c = v.item->getDrawableBounds().transformedBy(dr.fit).getCentre();
    juce::GlyphArrangement ga;
    ga.addLineOfText(juce::Font(AmpLookAndFeel::font(9.5f)), shownText(v), 0.0f, 0.0f);
    const float w = std::max(14.0f, ga.getBoundingBox(0, -1, true).getWidth() + 4.0f);
    return v.align == 'R' ? juce::Rectangle<float>(c.x - w, c.y - 7.0f, w, 14.0f)
           : v.align == 'C' ? juce::Rectangle<float>(c.x - w * 0.5f, c.y - 7.0f, w, 14.0f)
                             : juce::Rectangle<float>(c.x, c.y - 7.0f, w, 14.0f);
}

int CircuitPage::valueAt(juce::Point<float> pos, bool editableOnly) const {
    if (!showValues_ || !hasDrawing() || view_ >= int(drawings_.size()) || !host_.getBounds().toFloat().contains(pos)) return -1;
    const auto& dr = drawings_[std::size_t(view_)];
    for (std::size_t i = 0; i < dr.values.size(); ++i)
        if (dr.values[i].name.isNotEmpty() && !(editableOnly && dr.values[i].fixed) && valueBox(dr, dr.values[i]).expanded(2.0f).contains(pos)) return int(i);
    return -1;
}

// The triode models a tube can be swapped for (the engine's list: factor 2 is the first). The model as drawn comes first.
static const char* const kTubeModels[] = {"12AX7", "12AT7", "12AY7"};

juce::StringArray CircuitPage::tubeOptions(const Drawing::Tube& t) const {
    juce::StringArray o;
    o.add(t.drawn);
    for (const auto* m : kTubeModels)
        if (t.drawn != m) o.add(m);
    return o;
}

int CircuitPage::tubeChoice(const Drawing::Tube& t) const {
    const int k = int(std::lround(factorOf("T:" + t.key))) - 2;
    if (k < 0 || k >= int(std::size(kTubeModels))) return 0;
    const auto o = tubeOptions(t);
    return std::max(0, o.indexOf(kTubeModels[k]));
}

juce::String CircuitPage::outputTubeName() const {
    const auto& tubes = processor_.amps()[std::size_t(ampIndex_)].power.tubes;
    if (tubes.empty()) return {};
    return juce::String(tubes[std::size_t(juce::jlimit(0, int(tubes.size()) - 1, processor_.wantedCondition(0, false).tube))].name);
}

void CircuitPage::stepTube(const Drawing::Tube& t, int dir) {
    if (t.output) return;  // the output tubes are chosen with the selector
    const auto o = tubeOptions(t);
    const int next = (tubeChoice(t) + dir + o.size()) % o.size();
    double f = 1.0;  // back on the tube as drawn: no edit
    if (next > 0)
        for (int k = 0; k < int(std::size(kTubeModels)); ++k)
            if (o[next] == kTubeModels[k]) f = 2.0 + k;
    setPending("T:" + t.key, f);
}

int CircuitPage::tubeAt(juce::Point<float> pos) const {
    if (!hasDrawing() || view_ >= int(drawings_.size()) || !host_.getBounds().toFloat().contains(pos)) return -1;
    const auto& dr = drawings_[std::size_t(view_)];
    for (std::size_t i = 0; i < dr.tubes.size(); ++i)
        if (dr.tubes[i].item->getDrawableBounds().transformedBy(dr.fit).expanded(4.0f).contains(pos)) return int(i);
    return -1;
}

void CircuitPage::setPending(const juce::String& name, double factor) {
    pending_[name] = factor;
    applyAtMs_ = juce::Time::getMillisecondCounter() + 350;
    repaint();
}

void CircuitPage::stepValue(const Drawing::Value& v, int dir) {
    const double cur = v.base * factorOf(v.name);
    double next = stepE12(cur, dir);
    if ((cur - v.base) * (next - v.base) < 0.0) next = v.base;  // a value that is not in the series: the step passes through it on the way back
    double f = std::clamp(next / v.base, 0.1, 10.0);
    if (std::abs(f - 1.0) < 0.02) f = 1.0;  // back on the value as drawn
    setPending(v.name, f);
}

int CircuitPage::editableIndex(int i) const {
    const auto& dr = drawings_[std::size_t(view_)];
    for (std::size_t k = 0; k < dr.values.size(); ++k)
        if (dr.values[k].name.isNotEmpty() && !dr.values[k].fixed && i-- == 0) return int(k);
    return 0;
}

int CircuitPage::editableCount() const {
    if (!hasDrawing() || view_ >= int(drawings_.size())) return 0;
    int n = 0;
    for (const auto& v : drawings_[std::size_t(view_)].values) n += v.name.isNotEmpty() && !v.fixed ? 1 : 0;
    return n;
}

void CircuitPage::applyEdits() {
    for (const auto& [name, f] : pending_) processor_.setPartFactor(ampIndex_, name.toStdString(), f);
    pending_.clear();
}

void CircuitPage::tick() {
    const int n = processor_.meterCount();
    const auto now = juce::Time::getMillisecondCounter();
    if (!pending_.empty() && static_cast<juce::int32>(now - applyAtMs_) >= 0) applyEdits();
    const bool edited = !pending_.empty() || processor_.partEditCount(ampIndex_) > 0;
    if (edited != resetButton_.isVisible()) resetButton_.setVisible(edited);
    for (int i = 0; i < int(stages_.size()); ++i) {
        auto& s = stages_[std::size_t(i)];
        const float ratio = i < n ? processor_.takeMeter(i) : 0.0f;
        const float f = fractionOf(ratio);
        s.levelDb = 20.0f * std::log10(std::max(ratio, 1.0e-5f));
        s.shown = f > s.shown ? f : s.shown * 0.82f + f * 0.18f;  // up at once, down smoothly
        if (f >= s.peak || now - s.peakAtMs > 1200) {
            s.peak = f;
            s.peakAtMs = now;
        }
        if (ratio >= 1.0f) s.clipAtMs = now;
    }
    // The drawings: each wire takes the colour of its stage's level, each tube glows with the signal that goes through it.
    for (auto& dr : drawings_) {
        for (auto& w : dr.wires) {
            const auto& s = stages_[w.stage];
            w.shape->setStrokeFill(juce::FillType(levelColour(-kRangeDb + s.shown * kRangeDb)));
        }
        for (auto& [name, shapes] : dr.glows)
            for (std::size_t i = 0; i < stages_.size(); ++i)
                if (stages_[i].key == name)
                    for (auto* g : shapes) g->setAlpha(0.35f + 0.65f * std::clamp(stages_[i].shown * 1.2f, 0.0f, 1.0f));
    }
    // The tubes of the power amp take the level of the stage that drives them.
    for (auto& [name, key] : {std::pair<const char*, const char*>{"EL34A", "POWER"}, {"EL34B", "POWER"}, {"V3", "PHASE"}})
        for (auto& dr : drawings_)
            if (const auto it = dr.glows.find(name); it != dr.glows.end())
                for (const auto& s : stages_)
                    if (s.key == key)
                        for (auto* g : it->second) g->setAlpha(0.35f + 0.65f * std::clamp(s.shown * 1.2f, 0.0f, 1.0f));
    // The drawing follows the amp's channel when the player changes it (the buttons choose freely until then).
    if (const int ch = currentChannel(); ch != lastChannel_) {
        lastChannel_ = ch;
        if (ch >= 0 && view_ != powerIndex() && view_ != ch) setView(ch);
    }
    updateSwitches();
    // The diodes light up as the signal that reaches them grows: from about -18 dB below their stage's limit they glow, amber then red.
    for (auto& dr : drawings_)
        for (auto& dd : dr.diodes) {
            const float db = -kRangeDb + stages_[dd.stage].shown * kRangeDb;
            const float a = std::clamp((db + 18.0f) / 18.0f, 0.0f, 1.0f);
            dd.shape->setFill(juce::FillType(juce::Colour(0xff3a332a).interpolatedWith(a < 0.7f ? juce::Colour(0xfff0b53a) : juce::Colour(0xffe5533a), a)));
        }
    updatePots();
    repaint();
}

// A switch is closed or open as the amp's jack and selectors set it (what they say overrides the netlist's own state).
void CircuitPage::updateSwitches() {
    const int jack = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    for (auto& dr : drawings_)
        for (auto& [name, sw] : dr.switches) {
            bool closed = sw.defaultClosed;
            if (jack >= 0 && jack < int(jackRes_.size()))
                if (const auto it = jackRes_[std::size_t(jack)].find(name); it != jackRes_[std::size_t(jack)].end()) closed = it->second < 1000.0;
            for (std::size_t i = 0; i < selRes_.size() && i < 6; ++i) {
                const int pos = int(processor_.apvts.getRawParameterValue(ids::sel[i])->load() + 0.5f);
                if (pos < 0 || pos >= int(selRes_[i].size())) continue;
                if (const auto it = selRes_[i][std::size_t(pos)].find(name); it != selRes_[i][std::size_t(pos)].end()) closed = it->second < 1000.0;
            }
            if (sw.closed) sw.closed->setVisible(closed);
            if (sw.open) sw.open->setVisible(!closed);
        }
}

// The pointer of each pot follows its parameter: the knobs turn through 270 degrees, from lower left to lower right.
void CircuitPage::updatePots() {
    for (auto& dr : drawings_)
        for (auto& p : dr.pots) {
            const auto* param = processor_.apvts.getParameter(p.param);
            if (!param || !p.pointer) continue;
            const float v = param->getValue();
            if (std::abs(v - p.shown) < 1.0e-4f) continue;
            p.shown = v;
            const float a = juce::degreesToRadians(-135.0f + 270.0f * v);
            const auto tip = p.centre + juce::Point<float>(std::sin(a), -std::cos(a)) * p.length;
            juce::Path line;
            line.startNewSubPath(p.centre);
            line.lineTo(tip);
            p.pointer->setPath(line);
        }
}

CircuitPage::PotRef CircuitPage::potAt(juce::Point<float> pos) const {
    const auto& dr = drawings_[std::size_t(view_)];
    for (std::size_t i = 0; i < dr.pots.size(); ++i) {
        const auto b = dr.pots[i].body->getDrawableBounds().transformedBy(dr.fit);
        if (b.expanded(4.0f).contains(pos)) return {view_, int(i)};
    }
    return {};
}

juce::String CircuitPage::potParam(int i) const {
    for (const auto& dr : drawings_) {
        if (i < int(dr.pots.size())) return dr.pots[std::size_t(i)].param;
        i -= int(dr.pots.size());
    }
    return {};
}

void CircuitPage::nudgePot(int i, float delta) {
    Pot* pot = nullptr;
    for (auto& dr : drawings_) {
        if (i < int(dr.pots.size())) {
            pot = &dr.pots[std::size_t(i)];
            break;
        }
        i -= int(dr.pots.size());
    }
    if (!pot) return;
    if (auto* param = processor_.apvts.getParameter(pot->param)) {
        param->setValueNotifyingHost(std::clamp(param->getValue() + delta, 0.0f, 1.0f));
        updatePots();
        repaint();
    }
}

void CircuitPage::mouseMove(const juce::MouseEvent& e) {
    if (const int ti = tubeAt(e.position); ti != tubeHover_) {
        tubeHover_ = ti;
        repaint();
    }
    if (tubeHover_ >= 0) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (const int vi = valueAt(e.position); vi >= 0 || valueHover_ >= 0) {
        if (vi != valueHover_) {
            valueHover_ = vi;
            repaint();
        }
        if (vi >= 0 && !drawings_[std::size_t(view_)].values[std::size_t(vi)].fixed) {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            return;
        }
    }
    const auto h = potAt(e.position);
    const auto& dr = drawings_[std::size_t(view_)];
    const bool onNext = dr.next && dr.next->getDrawableBounds().transformedBy(dr.fit).expanded(6.0f).contains(e.position);
    if (h.drawing != hover_.drawing || h.index != hover_.index) {
        hover_ = h;
        repaint();
    }
    setMouseCursor(h.drawing >= 0 ? juce::MouseCursor::UpDownResizeCursor : onNext ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void CircuitPage::mouseExit(const juce::MouseEvent&) {
    if (tubeHover_ >= 0) {
        tubeHover_ = -1;
        repaint();
    }
    if (valueHover_ >= 0) {
        valueHover_ = -1;
        repaint();
    }
    if (hover_.drawing >= 0 && drag_.drawing < 0) {
        hover_ = {};
        repaint();
    }
}

void CircuitPage::mouseDown(const juce::MouseEvent& e) {
    tubeDown_ = tubeAt(e.position);
    if (tubeDown_ >= 0) {  // a click on a tube takes the next model when the button goes up
        tubeDownAt_ = e.position;
        return;
    }
    valueDrag_ = valueAt(e.position, true);
    if (valueDrag_ >= 0) {
        valueDragY_ = e.position.y;
        return;
    }
    drag_ = potAt(e.position);
    if (drag_.drawing < 0) {  // the arrow at the end of the preamp leads on to the power amp
        const auto& dr = drawings_[std::size_t(view_)];
        if (view_ != powerIndex() && hasPowerDrawing() && dr.next && dr.next->getDrawableBounds().transformedBy(dr.fit).expanded(6.0f).contains(e.position)) {
            setView(powerIndex());
            return;
        }
        if (host_.getBounds().toFloat().contains(e.position)) {  // anywhere else on the drawing: drag it along
            panning_ = true;
            panAnchor_ = e.position - pan_;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return;
    }
    hover_ = drag_;
    dragY_ = e.position.y;
    if (const auto* pot = potOf(drag_))
        if (auto* param = processor_.apvts.getParameter(pot->param)) param->beginChangeGesture();
}

void CircuitPage::mouseDrag(const juce::MouseEvent& e) {
    if (tubeDown_ >= 0) return;
    if (valueDrag_ >= 0) {  // one step of the series for every 9 pixels
        const auto& v = drawings_[std::size_t(view_)].values[std::size_t(valueDrag_)];
        while (valueDragY_ - e.position.y >= 9.0f) {
            stepValue(v, 1);
            valueDragY_ -= 9.0f;
        }
        while (e.position.y - valueDragY_ >= 9.0f) {
            stepValue(v, -1);
            valueDragY_ += 9.0f;
        }
        return;
    }
    if (panning_) {
        pan_ = e.position - panAnchor_;
        resized();
        repaint();
        return;
    }
    if (drag_.drawing < 0) return;
    const float dy = dragY_ - e.position.y;
    dragY_ = e.position.y;
    int flat = drag_.index;
    for (int d = 0; d < drag_.drawing; ++d) flat += int(drawings_[std::size_t(d)].pots.size());
    nudgePot(flat, dy / (e.mods.isShiftDown() ? 800.0f : 200.0f));
}

void CircuitPage::mouseUp(const juce::MouseEvent& e) {
    if (tubeDown_ >= 0) {
        const int i = tubeDown_;
        tubeDown_ = -1;
        if (e.position.getDistanceFrom(tubeDownAt_) < 5.0f && i < int(drawings_[std::size_t(view_)].tubes.size()))
            stepTube(drawings_[std::size_t(view_)].tubes[std::size_t(i)], e.mods.isShiftDown() ? -1 : 1);
        return;
    }
    if (valueDrag_ >= 0) {
        valueDrag_ = -1;
        mouseMove(e);
        return;
    }
    if (panning_) {
        panning_ = false;
        mouseMove(e);
        return;
    }
    if (drag_.drawing < 0) return;
    if (const auto* pot = potOf(drag_))
        if (auto* param = processor_.apvts.getParameter(pot->param)) param->endChangeGesture();
    drag_ = {};
    mouseMove(e);
}

void CircuitPage::mouseDoubleClick(const juce::MouseEvent& e) {
    if (const int ti = tubeAt(e.position); ti >= 0) {  // a tube goes back to the drawn model
        tubeDown_ = -1;
        if (!drawings_[std::size_t(view_)].tubes[std::size_t(ti)].output) setPending("T:" + drawings_[std::size_t(view_)].tubes[std::size_t(ti)].key, 1.0);
        return;
    }
    if (const int vi = valueAt(e.position, true); vi >= 0) {  // a component's value goes back to the drawn one
        setPending(drawings_[std::size_t(view_)].values[std::size_t(vi)].name, 1.0);
        return;
    }
    const auto r = potAt(e.position);
    const auto* pot = potOf(r);
    if (!pot) {  // on the drawing: all of it, or back to the readable size
        if (host_.getBounds().toFloat().contains(e.position)) {
            overview_ = !overview_;
            zoom_ = 1.0f;
            pan_ = {};
            resized();
            repaint();
        }
        return;
    }
    if (auto* param = processor_.apvts.getParameter(pot->param)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
        updatePots();
        repaint();
    }
}

void CircuitPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (const int ti = tubeAt(e.position); ti >= 0 && w.deltaY != 0.0f) {
        stepTube(drawings_[std::size_t(view_)].tubes[std::size_t(ti)], w.deltaY > 0 ? 1 : -1);
        return;
    }
    if (const int vi = valueAt(e.position, true); vi >= 0 && w.deltaY != 0.0f) {
        stepValue(drawings_[std::size_t(view_)].values[std::size_t(vi)], w.deltaY > 0 ? 1 : -1);
        return;
    }
    const auto r = potAt(e.position);
    const auto* pot = potOf(r);
    if (!pot) {  // the wheel over the drawing zooms it about the pointer
        if (!host_.getBounds().toFloat().contains(e.position) || w.deltaY == 0.0f) return;
        const float newZoom = std::clamp(zoom_ * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f), 0.5f, 8.0f);
        const float f = newZoom / zoom_;
        const auto p = e.position - host_.getBounds().toFloat().getCentre();
        pan_ = p - (p - pan_) * f;
        zoom_ = newZoom;
        resized();
        repaint();
        return;
    }
    auto* param = processor_.apvts.getParameter(pot->param);
    if (!param) return;
    int flat = r.index;
    for (int d = 0; d < r.drawing; ++d) flat += int(drawings_[std::size_t(d)].pots.size());
    param->beginChangeGesture();
    nudgePot(flat, (w.deltaY > 0 ? 1.0f : -1.0f) * 0.02f);
    param->endChangeGesture();
}

void CircuitPage::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat().reduced(10.0f, 6.0f);
    g.setColour(look_.label());
    g.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    g.drawText(tr("SIGNAL CHAIN"), area.withHeight(16.0f).toNearestInt(), juce::Justification::centredLeft);
    if (!hasDrawing()) paintBlocks(g, area);
}

void CircuitPage::paintOverChildren(juce::Graphics& g) {
    if (!hasDrawing()) return;
    const auto& dr = drawings_[std::size_t(view_)];
    g.saveState();
    g.reduceClipRegion(host_.getBounds());  // the names and values stay inside the schematic's area when it is zoomed or dragged
    // The names of the tubes and pots, under or beside them.
    g.setFont(AmpLookAndFeel::font(11.5f, true, 0.06f));
    for (const auto& t : dr.tags) {
        const auto b = t.item->getDrawableBounds().transformedBy(dr.fit);
        g.setColour(look_.label());
        juce::String text = tr(t.text);
        for (const auto& tb : dr.tubes) {
            if (tb.item != t.item) continue;
            if (tb.output) {  // an output tube: the one the amp is set to, in the accent colour when it is not the stock one
                if (const auto name = outputTubeName(); name.isNotEmpty()) text = name;
                if (processor_.wantedCondition(0, false).tube > 0) g.setColour(look_.accent());
            } else if (tubeChoice(tb) > 0) {  // a tube the user swapped: its new model, in the accent colour
                text = tubeOptions(tb)[tubeChoice(tb)];
                g.setColour(look_.accent());
            }
        }
        if (t.kind == 1) g.drawText(text, juce::Rectangle<float>(b.getRight() + 6.0f, b.getCentreY() - 8.0f, 70.0f, 16.0f).toNearestInt(), juce::Justification::centredLeft);
        else if (const auto name = t.item->getName(); name.startsWith("pot_") && dr.labels.count(name.fromFirstOccurrenceOf("pot_", false, false)) != 0) {
            const auto c = dr.labels.at(name.fromFirstOccurrenceOf("pot_", false, false))->getDrawableBounds().transformedBy(dr.fit).getCentre();
            g.drawText(text, juce::Rectangle<float>(c.x - 60.0f, c.y - 16.0f, 120.0f, 16.0f).toNearestInt(), juce::Justification::centred);
        } else {
            // A name is as wide as the room between the pots (a panel with a dozen of them): longer ones shrink to fit.
            float room = 100.0f;
            for (const auto& other : dr.tags)
                if (other.item != t.item && other.kind == 0 && std::abs(other.item->getDrawableBounds().transformedBy(dr.fit).getCentreY() - b.getCentreY()) < 4.0f)
                    room = std::min(room, std::abs(other.item->getDrawableBounds().transformedBy(dr.fit).getCentreX() - b.getCentreX()) - 4.0f);
            auto box = juce::Rectangle<float>(b.getCentreX() - room * 0.5f, b.getBottom() + (t.item->getName().startsWith("pot_") ? 28.0f : 4.0f), room, 16.0f);
            box.setX(std::max(box.getX(), 4.0f));  // a jack at the edge of the drawing keeps its name inside the page
            g.drawFittedText(text, box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }
    g.setColour(look_.label());  // the names of the boxes of parts of the power amp
    g.setFont(AmpLookAndFeel::font(10.5f, true, 0.06f));
    for (const auto& t : dr.titles) {
        const auto b = t.item->getDrawableBounds().transformedBy(dr.fit);
        g.drawText(tr(t.text), juce::Rectangle<float>(b.getCentreX() - 120.0f, b.getCentreY() - 8.0f, 240.0f, 16.0f).toNearestInt(), juce::Justification::centred);
    }
    g.setFont(AmpLookAndFeel::font(9.5f));  // the values of the components: the ones the user changed in the accent colour
    for (std::size_t i = 0; i < dr.values.size() && showValues_; ++i) {
        const auto& v = dr.values[i];
        const bool edited = v.name.isNotEmpty() && std::abs(valueFactor(v) - 1.0) > 1e-6;
        g.setColour(edited ? look_.accent() : int(i) == valueHover_ ? look_.text() : look_.label().withAlpha(0.9f));
        const auto box = valueBox(dr, v).expanded(6.0f, 0.0f);
        g.drawText(shownText(v), box.toNearestInt(), v.align == 'R' ? juce::Justification::centredRight : v.align == 'C' ? juce::Justification::centred : juce::Justification::centredLeft);
    }
    if (view_ != powerIndex() && hasPowerDrawing() && dr.next) {  // the way on to the power amp
        const auto b = dr.next->getDrawableBounds().transformedBy(dr.fit);
        g.setColour(look_.label());
        g.drawText(tr("POWER"), juce::Rectangle<float>(b.getCentreX() - 50.0f, b.getBottom() + 4.0f, 100.0f, 16.0f).toNearestInt(), juce::Justification::centred);
    }
    for (std::size_t i = 0; i < dr.pots.size(); ++i) {  // the value of each pot under its name; a ring on the one under the mouse
        const auto b = dr.pots[i].body->getDrawableBounds().transformedBy(dr.fit);
        const bool hovered = hover_.drawing == view_ && hover_.index == int(i);
        if (dr.pots[i].name != dr.pots[i].param && !hovered) continue;  // the copies of a knob show no value
        if (const auto* param = processor_.apvts.getParameter(dr.pots[i].param)) {
            g.setColour(hovered ? look_.accent() : look_.text().withAlpha(0.8f));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
            auto at = juce::Rectangle<float>(b.getCentreX() - 30.0f, b.getBottom() + 42.0f, 60.0f, 14.0f);
            if (const auto it = dr.labels.find(dr.pots[i].name); it != dr.labels.end()) {
                const auto c = it->second->getDrawableBounds().transformedBy(dr.fit).getCentre();
                at = juce::Rectangle<float>(c.x - 30.0f, c.y + 1.0f, 60.0f, 14.0f);
            }
            g.drawText(param->getCurrentValueAsText(), at.toNearestInt(), juce::Justification::centred);
        }
        if (hovered) {
            g.setColour(look_.accent());
            g.drawEllipse(b.expanded(4.0f), 2.0f);
        }
    }
    if (valueHover_ >= 0 && showValues_ && valueDrag_ < 0 && valueHover_ < int(dr.values.size())) paintTip(g, dr, dr.values[std::size_t(valueHover_)]);
    else if (tubeHover_ >= 0 && tubeHover_ < int(dr.tubes.size())) {  // what the tube is, what its model does, and how to swap it
        const auto& tb = dr.tubes[std::size_t(tubeHover_)];
        if (tb.output) {
            const auto name = outputTubeName().isEmpty() ? tb.drawn : outputTubeName();
            const auto up = name.toUpperCase();
            juce::String body = up.startsWith("EL34") ? tr("The British output tube: strong, a little hard mids and an early, aggressive breakup.")
                              : up.startsWith("6L6") ? tr("The American output tube: more headroom and a fuller low end, with a rounder breakup.")
                              : up.startsWith("6550") ? tr("A big, stiff tube: the most power and headroom, tight lows and a controlled breakup.")
                              : up.startsWith("6V6") ? tr("A small, smooth tube: it breaks up early with a sweet compression, and has little power.")
                              : up.startsWith("EL84") ? tr("The small British tube of the Vox amps: chime, bright mids and an early, lively breakup.")
                                                      : juce::String();
            paintTipBox(g, tb.item->getDrawableBounds().transformedBy(dr.fit), tr("Output tube") + "  " + name, body, tr("Change them with the POWER TUBES selector."), processor_.wantedCondition(0, false).tube > 0);
        } else {
        const int choice = tubeChoice(tb);
        const auto model = tubeOptions(tb)[choice];
        juce::String body = model == "12AT7" ? tr("Medium gain (mu 60) and a lower plate resistance: cleaner, with more headroom and a tighter low end.")
                          : model == "12AY7" ? tr("Low gain (mu 40): soft and clean, the tube of the early tweeds. A lot more headroom.")
                          : model == "12AX7" || model == "ECC83" ? tr("High gain (mu 100): the usual preamp tube, with the most gain and the earliest breakup.")
                                                                 : juce::String();
        if (tb.phase) body = tr("The phase inverter splits the signal for the two output tubes. Its model changes the headroom of the power stage and how early it breaks up.") + (body.isEmpty() ? "" : " ") + body;
        if (tb.follower) body += (body.isEmpty() ? "" : " ") + tr("This one is a cathode follower: it hardly adds gain, so another model changes little.");
        paintTipBox(g, tb.item->getDrawableBounds().transformedBy(dr.fit), tr("Tube") + "  " + tb.key + "  " + model + (choice > 0 ? "  (" + tb.drawn + ")" : juce::String()), body,
                    tr("Click or wheel: another model. Double-click: back to the drawn one."), choice > 0);
        }
    }
    g.restoreState();
    paintStrip(g, strip_);
}

// The tip over a component the mouse is on: what it is, and what a bigger or a smaller value does to the sound.
void CircuitPage::paintTip(juce::Graphics& g, const Drawing& dr, const Drawing::Value& v) {
    const auto role = roleOf(v.name);
    const RoleText* text = nullptr;
    for (const auto& r : kRoleTexts)
        if (role == r.role) text = &r;
    if (!text) return;
    const bool edited = std::abs(valueFactor(v) - 1.0) > 1e-6;
    paintTipBox(g, valueBox(dr, v), tr(text->title) + "  " + v.name + "  " + shownText(v) + (edited ? "  (" + v.text + ")" : juce::String()), tr(text->body),
                v.fixed ? tr("Fixed in the power amp: its bias and supply are calibrated together.") : tr("Wheel or drag: change the value. Double-click: back to the drawn one."), edited);
}

// The box of a tip near `anchor`: a title line, what it does, and how to change it.
void CircuitPage::paintTipBox(juce::Graphics& g, juce::Rectangle<float> anchor, const juce::String& title, const juce::String& body, const juce::String& hint, bool edited) {
    juce::AttributedString s;
    s.setJustification(juce::Justification::topLeft);
    s.append(title + "\n", AmpLookAndFeel::font(11.5f, true), edited ? look_.accent() : look_.text());
    s.append(body + "\n", AmpLookAndFeel::font(10.5f), look_.text().withAlpha(0.9f));
    s.append(hint, AmpLookAndFeel::font(9.5f), look_.label());
    juce::TextLayout layout;
    layout.createLayout(s, 250.0f);
    const float w = layout.getWidth() + 16.0f, h = layout.getHeight() + 12.0f;
    const auto area = host_.getBounds().toFloat();
    float x = anchor.getCentreX() - w * 0.5f, y = anchor.getBottom() + 10.0f;
    if (y + h > area.getBottom()) y = anchor.getY() - h - 10.0f;  // no room below: above it
    x = juce::jlimit(area.getX() + 4.0f, std::max(area.getX() + 4.0f, area.getRight() - w - 4.0f), x);
    const auto box = juce::Rectangle<float>(x, y, w, h);
    g.setColour(look_.background().darker(0.4f).withAlpha(0.96f));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(look_.accent().withAlpha(0.8f));
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    layout.draw(g, box.reduced(8.0f, 6.0f));
}

// The stages as a row of horizontal bars (the schematic's meters): the name, the bar with its recent peak, the level in dB.
void CircuitPage::paintStrip(juce::Graphics& g, juce::Rectangle<float> area) {
    if (stages_.empty()) return;
    const auto now = juce::Time::getMillisecondCounter();
    g.setColour(look_.panelEdge().withAlpha(0.6f));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1.0f);
    const int n = int(stages_.size());
    const float colW = area.getWidth() / float(n);
    for (int i = 0; i < n; ++i) {
        const auto& s = stages_[std::size_t(i)];
        const auto col = area.withX(area.getX() + colW * float(i)).withWidth(colW).reduced(6.0f, 8.0f);
        const bool clipping = now - s.clipAtMs < 1500;
        g.setColour(clipping ? juce::Colour(0xfff08a70) : look_.text());
        g.setFont(AmpLookAndFeel::font(11.0f, true, 0.04f));
        g.drawText(s.name, col.withHeight(16.0f).toNearestInt(), juce::Justification::centredLeft);
        const auto well = juce::Rectangle<float>(col.getX(), col.getY() + 20.0f, col.getWidth(), 10.0f);
        g.setColour(look_.background().brighter(0.05f));
        g.fillRoundedRectangle(well, 3.0f);
        const float db = -kRangeDb + s.shown * kRangeDb;
        g.setColour(levelColour(std::max(db, -29.0f)));
        g.fillRoundedRectangle(well.withWidth(well.getWidth() * s.shown), 3.0f);
        g.setColour(look_.text().withAlpha(0.85f));
        g.fillRect(well.getX() + well.getWidth() * s.peak - 1.0f, well.getY() - 2.0f, 2.0f, well.getHeight() + 4.0f);
        g.setColour(clipping ? juce::Colour(0xffff5a3c) : look_.label());
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        const juce::String dbText = s.levelDb <= -99.0f ? juce::String("--") : juce::String(juce::roundToInt(s.levelDb)) + " dB";
        g.drawText(dbText, juce::Rectangle<float>(col.getX(), col.getY() + 33.0f, col.getWidth(), 14.0f).toNearestInt(), juce::Justification::centredLeft);
    }
}

// An amp without a drawing: the stages as blocks in a row, each with a vertical bar.
void CircuitPage::paintBlocks(juce::Graphics& g, juce::Rectangle<float> area) {
    if (stages_.empty()) return;
    const auto now = juce::Time::getMillisecondCounter();
    const int n = int(stages_.size());
    const float colW = area.getWidth() / float(n);
    const float boxW = std::min(colW - 16.0f, 120.0f);
    const float boxH = 44.0f;
    const float top = area.getY() + 26.0f;
    const float barTop = top + boxH + 14.0f;
    const float barBottom = area.getBottom() - 64.0f;
    const float barW = 22.0f;

    for (int i = 0; i < n; ++i) {
        const auto& s = stages_[std::size_t(i)];
        const float cx = area.getX() + colW * (float(i) + 0.5f);
        const bool clipping = now - s.clipAtMs < 1500;
        const auto edge = clipping ? juce::Colour(0xffd9573c) : look_.panelEdge();
        const juce::Rectangle<float> box(cx - boxW * 0.5f, top, boxW, boxH);
        g.setColour(clipping ? juce::Colour(0xff3a1f1a) : look_.panel());
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(box, 5.0f, 1.2f);
        g.setColour(clipping ? juce::Colour(0xfff08a70) : look_.text());
        g.setFont(AmpLookAndFeel::font(13.0f, true, 0.04f));
        g.drawText(s.name, box.toNearestInt(), juce::Justification::centred);
        if (i + 1 < n) {  // the wire to the next stage
            g.setColour(look_.panelEdge());
            const float y = top + boxH * 0.5f;
            g.drawLine(cx + boxW * 0.5f, y, cx + colW - boxW * 0.5f, y, 1.5f);
        }

        // The meter: a bar from green to amber to red over kRangeDb, the recent peak as a line.
        const juce::Rectangle<float> well(cx - barW * 0.5f, barTop, barW, barBottom - barTop);
        g.setColour(look_.background().brighter(0.05f));
        g.fillRoundedRectangle(well, 3.0f);
        const float h = well.getHeight() * s.shown;
        const auto fillRect = juce::Rectangle<float>(well.getX(), well.getBottom() - h, well.getWidth(), h);
        const float db = -kRangeDb + s.shown * kRangeDb;
        g.setColour(db > -3.0f ? juce::Colour(0xffd9573c) : db > -12.0f ? look_.accent() : juce::Colour(0xff6aa84f));
        g.fillRoundedRectangle(fillRect, 3.0f);
        const float py = well.getBottom() - well.getHeight() * s.peak;
        g.setColour(look_.text().withAlpha(0.85f));
        g.fillRect(well.getX() - 3.0f, py - 1.0f, well.getWidth() + 6.0f, 2.0f);

        g.setColour(clipping ? juce::Colour(0xffff5a3c) : look_.panelEdge());
        g.fillEllipse(cx - 4.0f, barBottom + 10.0f, 8.0f, 8.0f);
        g.setColour(look_.label());
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        const juce::String dbText = s.levelDb <= -99.0f ? juce::String("--") : juce::String(juce::roundToInt(s.levelDb)) + " dB";
        g.drawText(dbText, juce::Rectangle<float>(cx - colW * 0.5f, barBottom + 22.0f, colW, 16.0f).toNearestInt(), juce::Justification::centred);
    }

    g.setColour(look_.label().withAlpha(0.8f));
    g.setFont(AmpLookAndFeel::font(10.5f));
    g.drawText(tr("Bar: level now. Line: recent peak. Red: at the limit."), area.withTrimmedTop(area.getHeight() - 16.0f).toNearestInt(),
               juce::Justification::centredLeft);
}
