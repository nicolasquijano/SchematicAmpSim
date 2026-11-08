// Works out the factory presets of the amps (amp.json "presets"). Each preset names a channel (jack), the knob that sets the gain and how hard the
// amp should be driven, and the tool finds the gain setting that gets there: it plays a sine at a guitar level and at a very low one through the
// amp (Normal mode) and measures the compression, the number of dB the loud one comes out below what the quiet one predicts. Clean is under a dB,
// Edge of breakup about 3, Crunch about 7 and Lead about 13 dB; when an amp cannot get that far (a Twin does not crunch at the preamp) the setting
// stays at the nearest end of the knob and the tool says so.
//   sas_amp_presets [amp id ...] [--json]     (no ids: every amp with a recipe; --json: only the "presets" blocks, for amp.json)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"

namespace {

constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;
constexpr double kHi = 0.10, kLo = 0.002;  // volts at the amp's input: a guitar, and a very quiet one
constexpr double kToneHz = 220.0;

struct Row {
    const char* name;
    double target;                                     // compression in dB to look for
    const char* jack;
    const char* gain;                                  // the ui parameter bisected
    std::vector<std::pair<const char*, double>> set;   // other ui parameters, on the panel's 0..10 scale
    std::vector<std::pair<const char*, int>> sel;      // selectors by name: the index of the position
    double minGain = 1.0;                              // the lowest gain setting tried (a very hot amp is clean only below 1)
};

struct Recipe {
    const char* id;
    std::vector<const char*> skip;  // ui parameters the presets leave alone (effects like the reverb of a Fender)
    std::vector<Row> rows;
    double hz = 220.0;              // the frequency of the test tone (a guitar's A string; a bass is lower)
    double volts = 0.10;            // the level of the loud tone at the amp's input
};

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> r{
        {"ac30_normal", {}, {
            {"Clean", 0.8, "normal", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Crunch", 7, "brilliant", "volume", {{"bass", 4}, {"treble", 6.5}}, {}},
            {"Lead", 13, "brilliant", "volume", {{"bass", 4.5}, {"treble", 6}}, {}}}},
        {"afd100", {}, {
            {"Clean", 0.8, "normal", "volume", {{"master", 6}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "afd", "volume", {{"master", 5}}, {}},
            {"Crunch", 7, "afd", "volume", {{"master", 5}}, {}},
            {"Lead", 13, "afd", "volume", {{"master", 5.5}, {"middle", 6}}, {}}}},
        {"bassman_5f6a", {}, {
            {"Clean", 0.8, "normal1", "volume2", {{"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal1", "volume2", {{"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Crunch", 7, "jumpered", "volume2", {{"volume", 6}, {"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Lead", 13, "jumpered", "volume", {{"volume2", 8}, {"bass", 4}, {"middle", 6}, {"treble", 6}}, {}}}},
        {"boutique_od", {}, {
            {"Clean", 0.8, "clean", "volume", {{"master", 5}}, {}},
            {"Edge of breakup", 3, "od", "reverb", {{"volume", 5}, {"volume2", 4}, {"vib_intensity", 2}, {"master", 5}}, {}},
            {"Crunch", 7, "od", "reverb", {{"volume", 5}, {"volume2", 5}, {"vib_intensity", 5}, {"master", 5}}, {}},
            {"Lead", 13, "od", "reverb", {{"volume", 5}, {"volume2", 6.5}, {"vib_intensity", 6}, {"master", 5}}, {}}}},
        {"deluxe_reverb", {"vib_speed", "vib_intensity", "reverb"}, {
            {"Clean", 0.8, "normal1", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal1", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Crunch", 7, "vibrato1", "volume", {{"bass", 5}, {"treble", 6}}, {}},
            {"Lead", 13, "vibrato1", "volume", {{"bass", 4}, {"treble", 6.5}}, {}}}},
        {"highgain_120", {}, {
            {"Clean", 0.8, "rhythm", "volume2", {{"power", 5}}, {}},
            {"Edge of breakup", 3, "rhythm", "volume2", {{"power", 5}}, {}},
            {"Crunch", 7, "crunch", "volume2", {{"power", 5}}, {}},
            {"Lead", 13, "lead", "volume", {{"master", 5}, {"middle", 5}}, {}, 0.3}}},
        {"american_5153", {}, {
            {"Clean", 0.8, "green", "volume2", {{"volume", 5}, {"bass", 5}, {"middle", 5}, {"treble", 6}}, {}, 0.05},
            {"Edge of breakup", 3, "green", "volume2", {{"volume", 5}, {"bass", 5}, {"middle", 5}, {"treble", 6}}, {}, 0.05},
            {"Crunch", 7, "blue", "volume2", {{"volume", 5}, {"bass", 5}, {"middle", 5}, {"treble", 6}}, {}, 0.05},
            {"Lead", 13, "red", "volume2", {{"volume", 5}, {"bass", 5}, {"middle", 6}, {"treble", 6.5}}, {}, 0.05}}},
        {"hiwatt_dr103", {}, {
            {"Clean", 0.8, "normal", "volume2", {{"master", 7}, {"bass", 4}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal", "volume2", {{"master", 7}, {"bass", 4}, {"treble", 6}}, {}},
            {"Crunch", 7, "jumpered", "volume2", {{"volume", 5}, {"master", 6}, {"bass", 4}, {"treble", 6}}, {}},
            {"Lead", 13, "jumpered", "volume", {{"volume2", 8}, {"master", 6}, {"bass", 4}, {"middle", 6}, {"treble", 6}}, {}}}},
        {"jazz_clean_120", {}, {
            {"Clean", 0.8, "high", "volume", {{"bass", 5}, {"middle", 5}, {"treble", 5}, {"volume2", 2}}, {{"dist", 0}}},
            {"Edge of breakup", 3, "high", "volume", {{"bass", 5}, {"middle", 5}, {"treble", 5.5}, {"volume2", 3}}, {{"dist", 0}}},
            {"Crunch", 7, "high", "master", {{"volume", 6}, {"bass", 5}, {"middle", 6}, {"treble", 5.5}, {"volume2", 3}}, {{"dist", 1}}},
            {"Lead", 13, "high", "master", {{"volume", 8}, {"bass", 5}, {"middle", 6.5}, {"treble", 6}, {"volume2", 3}}, {{"dist", 1}}}}},
        {"jcm800_2203", {}, {
            {"Clean", 0.8, "low", "volume", {{"master", 6}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "high", "volume", {{"master", 5}}, {}},
            {"Crunch", 7, "high", "volume", {{"master", 5}}, {}},
            {"Lead", 13, "high", "volume", {{"master", 6}, {"middle", 6.5}, {"treble", 6.5}}, {}}}},
        {"jcm800_2203kk", {}, {
            {"Clean", 0.8, "low", "volume", {{"master", 6}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "high", "volume", {{"master", 5}}, {}},
            {"Crunch", 7, "high", "volume", {{"master", 5}}, {}},
            {"Lead", 13, "high", "volume", {{"master", 6}, {"middle", 6.5}, {"treble", 6.5}}, {}}}},
        {"lead_c_plus", {}, {
            {"Clean", 0.8, "input", "volume", {{"master", 6}, {"treble", 6}}, {{"channel", 0}}},
            {"Edge of breakup", 3, "input", "volume", {{"master", 5}, {"treble", 6}}, {{"channel", 0}}},
            {"Crunch", 7, "input", "volume2", {{"reverb", 5}, {"master", 5}, {"treble", 6}}, {{"channel", 1}}, 0.05},
            {"Lead", 13, "input", "volume2", {{"reverb", 5}, {"master", 5}, {"middle", 6.5}, {"treble", 6.5}}, {{"channel", 1}}, 0.05}}},
        {"modbe_100", {}, {
            {"Clean", 0.8, "clean", "volume2", {{"master", 5}}, {}},
            {"Edge of breakup", 3, "be", "volume", {{"master", 5}}, {{"sat", 0}}},
            {"Crunch", 7, "be", "volume", {{"master", 5}}, {{"sat", 0}}},
            {"Lead", 13, "hbe", "volume", {{"master", 5}}, {{"sat", 1}, {"gainstruct", 1}}}}},
        {"orange_or120", {}, {
            {"Clean", 0.8, "fac6", "volume", {{"treble", 5.5}}, {}},
            {"Edge of breakup", 3, "fac4", "volume", {}, {}},
            {"Crunch", 7, "fac2", "volume", {}, {}},
            {"Lead", 13, "fac1", "volume", {{"treble", 5.5}}, {}}}},
        {"plexi_1959", {}, {
            {"Clean", 0.8, "normal", "volume2", {{"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal", "volume2", {{"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Crunch", 7, "jumpered", "volume2", {{"volume", 6}, {"bass", 4}, {"middle", 6}, {"treble", 6}}, {}},
            {"Lead", 13, "jumpered", "volume", {{"volume2", 8}, {"bass", 4}, {"middle", 6}, {"treble", 6}}, {}}}},
        {"superlead_100", {}, {
            {"Clean", 0.8, "clean", "volume2", {{"resonance", 5}}, {{"crunch", 1}}},
            {"Edge of breakup", 3, "clean", "volume2", {{"resonance", 5}}, {{"crunch", 0}}},
            {"Crunch", 7, "lead", "volume", {{"master", 5}}, {}},
            {"Lead", 13, "lead", "volume", {{"master", 6}}, {}}}},
        {"svt_bass300", {}, {
            // A bass amp: the presets are for a bass guitar (low E to the G string), measured with a low tone and a hotter pickup.
            {"Clean", 0.8, "ch1_normal", "volume", {{"master", 4}, {"bass", 5}, {"middle", 5}, {"treble", 5}}, {{"mid_freq", 0}}, 0.2},
            {"Warm tube", 3, "ch1_normal", "volume", {{"master", 4.5}, {"bass", 6}, {"middle", 5}, {"treble", 4}}, {{"mid_freq", 0}}, 0.2},
            {"Motown round", 1.5, "ch1_normal", "volume", {{"master", 4}, {"bass", 7}, {"middle", 5}, {"treble", 2.5}}, {{"mid_freq", 0}}, 0.2},
            {"Deep Ultra Lo", 2, "ch1_normal", "volume", {{"master", 4.5}, {"bass", 6}, {"middle", 4}, {"treble", 5}}, {{"ultra_lo", 1}, {"mid_freq", 0}}, 0.2},
            {"Slap scoop", 2, "ch1_bright", "volume", {{"master", 4.5}, {"bass", 7}, {"middle", 3}, {"treble", 7}}, {{"ultra_hi", 1}, {"mid_freq", 1}}, 0.2},
            {"Rock grind", 7, "ch2_normal", "volume2", {{"master", 8}, {"bass", 5}, {"middle", 6}, {"treble", 6}}, {{"mid_freq", 1}}},
            {"Overdriven", 13, "ch2_normal", "volume2", {{"master", 10}, {"bass", 5}, {"middle", 7}, {"treble", 5}}, {{"mid_freq", 1}}},
            {"Both channels", 5, "both_normal", "volume", {{"volume2", 4}, {"master", 5}, {"bass", 6}, {"middle", 5}, {"treble", 5}}, {{"mid_freq", 1}}, 0.2}},
         80.0, 0.15},
        {"triplestack_3ch", {}, {
            {"Clean", 0.8, "ch1_clean", "volume", {}, {}},
            {"Edge of breakup", 3, "ch1_pushed", "volume", {}, {}},
            {"Crunch", 7, "ch2_vintage", "volume", {}, {}},
            {"Lead", 13, "ch3_modern", "volume", {}, {}}}},
        {"tweed_5e3", {}, {
            {"Clean", 0.8, "normal_lo", "volume2", {}, {}},
            {"Edge of breakup", 3, "normal_hi", "volume2", {}, {}},
            {"Crunch", 7, "bright_hi", "volume", {}, {}},
            {"Lead", 13, "bright_hi", "volume", {{"treble", 7}}, {}}}},
        {"twin_65", {"vib_speed", "vib_intensity", "reverb"}, {
            {"Clean", 0.8, "normal1", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Edge of breakup", 3, "normal1", "volume2", {{"bass", 5}, {"treble", 6}}, {}},
            {"Crunch", 7, "vibrato1", "volume", {{"bass", 5}, {"treble", 6}}, {}},
            {"Lead", 13, "vibrato1", "volume", {{"bass", 4}, {"treble", 6.5}}, {}}}},
    };
    return r;
}

// The amplitude of the fundamental in the second half of the block (Goertzel).
double fundamental(const std::vector<double>& y, std::size_t from, double hz) {
    double re = 0, im = 0;
    const std::size_t n = y.size() - from;
    for (std::size_t i = from; i < y.size(); ++i) {
        const double ph = 2 * kPi * hz * double(i) / kFs;
        re += y[i] * std::cos(ph);
        im += y[i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / double(n);
}

struct State {
    std::map<std::string, double> params;  // ui parameter -> value on the 0..10 scale
    std::string jack;
    std::vector<std::pair<std::string, int>> sel;
};

// Compression in dB of an amp at a state.
double compression(const sas::AmpDef& def, const State& st, double hz, double volts) {
    std::vector<double> knobs;
    for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
    std::vector<double> power;
    for (const auto& k : def.power.knobs) power.push_back(k.def);
    double presence = def.power.presenceDefault;
    for (const auto& u : def.ui) {
        const auto it = st.params.find(u.param);
        if (it == st.params.end()) continue;
        if (!u.knob.empty()) knobs[def.knobIndex(u.knob)] = it->second / 10.0;
        else if (!u.powerKnob.empty()) power[def.powerKnobIndex(u.powerKnob)] = it->second / 10.0;
        else if (u.param == "presence") presence = it->second / 10.0;
    }
    double out[2] = {0, 0};
    const double vin[2] = {volts * kLo / kHi, volts};
    for (int v = 0; v < 2; ++v) {
        sas::FullAmp a(def, kFs, false, true);
        a.setKnobs(knobs, presence, power);
        a.setJack(def.jackIndex(st.jack));
        for (const auto& [name, pos] : st.sel)
            for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
                if (def.preamp.selectors[i].name == name) a.setSelector(i, std::size_t(pos));
        const std::size_t n = std::size_t(kFs * 0.45);
        std::vector<double> y(n);
        // The chain applies the jack's level compensation at the amp's input (the jacks play at a similar level).
        const double jackGain = std::pow(10.0, def.preamp.jacks[def.jackIndex(st.jack)].levelDb / 20.0);
        for (std::size_t i = 0; i < n; ++i) y[i] = a.process(jackGain * vin[v] * std::sin(2 * kPi * hz * double(i) / kFs));
        out[v] = fundamental(y, n / 2, hz);
    }
    return 20.0 * std::log10((out[0] / vin[0]) / std::max(out[1] / vin[1], 1e-12));
}

}  // namespace

int main(int argc, char** argv) {
    bool json = false;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json")) json = true;
        else only.push_back(argv[i]);
    }
    for (const auto& rec : recipes()) {
        if (!only.empty() && std::find(only.begin(), only.end(), rec.id) == only.end()) continue;
        const sas::AmpDef def = sas::loadEmbeddedAmp(rec.id);
        State base;
        for (const auto& u : def.ui) {
            if (std::find_if(rec.skip.begin(), rec.skip.end(), [&](const char* s) { return u.param == s; }) != rec.skip.end()) continue;
            double v = 5.0;
            if (!u.knob.empty()) v = def.preamp.knobs[def.knobIndex(u.knob)].def * 10.0;
            else if (!u.powerKnob.empty()) v = def.power.knobs[def.powerKnobIndex(u.powerKnob)].def * 10.0;
            else if (u.param == "presence") v = def.power.presenceDefault * 10.0;
            else continue;  // a slot that is not a knob (the vibrato switch...)
            base.params[u.param] = std::round(v * 10.0) / 10.0;
        }
        if (!json) std::printf("== %s\n", rec.id);
        std::string out = " \"presets\": [\n";
        for (std::size_t r = 0; r < rec.rows.size(); ++r) {
            const Row& row = rec.rows[r];
            State st = base;
            st.jack = row.jack;
            for (const auto& [p, v] : row.set) st.params[p] = v;
            for (const auto& [n, i] : row.sel) st.sel.emplace_back(n, i);
            // The gain: the compression grows with it, so a bisection finds the setting for the target.
            double lo = row.minGain, hi = 10.0, cLo, cHi;  // (a gain of 0 is silence: the measure means nothing there)
            st.params[row.gain] = lo;
            cLo = compression(def, st, rec.hz, rec.volts);
            st.params[row.gain] = hi;
            cHi = compression(def, st, rec.hz, rec.volts);
            double gain, achieved;
            if (row.target <= cLo) gain = lo, achieved = cLo;
            else if (row.target >= cHi) gain = hi, achieved = cHi;
            else {
                for (int it = 0; it < 9; ++it) {
                    const double mid = 0.5 * (lo + hi);
                    st.params[row.gain] = mid;
                    (compression(def, st, rec.hz, rec.volts) < row.target ? lo : hi) = mid;
                }
                gain = 0.5 * (lo + hi);
                st.params[row.gain] = gain;
                achieved = compression(def, st, rec.hz, rec.volts);
            }
            gain = std::round(gain * 10.0) / 10.0;
            st.params[row.gain] = gain;
            achieved = compression(def, st, rec.hz, rec.volts);
            if (!json)
                std::printf("  %-16s jack %-11s %s=%.1f  target %4.1f dB  got %5.1f dB%s\n", row.name, row.jack, row.gain, gain, row.target, achieved,
                            std::fabs(achieved - row.target) > 2.0 ? "   <-- far from the target" : "");
            char line[2048];
            std::string params;
            for (const auto& [p, v] : st.params) {
                std::snprintf(line, sizeof line, "%s\"%s\": %g", params.empty() ? "" : ", ", p.c_str(), v);
                params += line;
            }
            std::string sels;
            for (const auto& [n, i] : st.sel) {
                std::snprintf(line, sizeof line, "%s\"%s\": %d", sels.empty() ? "" : ", ", n.c_str(), i);
                sels += line;
            }
            std::snprintf(line, sizeof line, "  {\"name\": \"%s\", \"jack\": \"%s\", \"params\": {%s}%s%s%s}%s\n", row.name, row.jack, params.c_str(),
                          sels.empty() ? "" : ", \"selectors\": {", sels.c_str(), sels.empty() ? "" : "}", r + 1 < rec.rows.size() ? "," : "");
            out += line;
        }
        out += " ],\n";
        if (json) std::printf("%s:\n%s", rec.id, out.c_str());
        else std::printf("%s", out.c_str());
    }
    return 0;
}
