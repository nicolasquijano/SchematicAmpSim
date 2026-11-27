// A command line over the engine, for scripts and for the MCP server (tools/mcp/sas_mcp.py): list the amps and their controls, render a
// guitar recording (or a built-in riff) through one amp setting and measure the result. Everything goes out as JSON on stdout; errors as
// {"error": "..."} with a non-zero exit code.
//
//   sas_cli list
//   sas_cli info <amp>
//   sas_cli render --amp <id> [--in di.wav] [--out out.wav] [--preset <name>] [--set param=0..10 ...] [--jack <name>] [--sel name=pos ...]
//                  [--input-db x] [--output-db x] [--quality normal|high|lite|eco|efficient] [--ts drive,tone,level (0..10)] [--cab on|off]
//                  [--noise 0..1] [--seconds n] [--seed n]
//   sas_cli analyze <file.wav> [--ref <other.wav>]
//
// Not covered (yet): the effects rack and the cabinet impulse responses; what is rendered is the amp chain with the cab filter.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sas/chain.h"
#include "sas/condition.h"
#include "sas/embedded_amps.h"

using nlohmann::json;

namespace {

constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------------------------- WAV
struct Audio {
    std::vector<float> mono;
    double rate = kFs;
};

std::uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t rd16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }

Audio readWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    const std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) throw std::runtime_error(path + " is not a WAV file");
    int fmt = 0, ch = 1, bits = 16;
    double rate = kFs;
    const unsigned char* data = nullptr;
    std::size_t dataBytes = 0;
    for (std::size_t p = 12; p + 8 <= d.size();) {
        const std::uint32_t sz = rd32(&d[p + 4]);
        const unsigned char* body = &d[p + 8];
        if (std::memcmp(&d[p], "fmt ", 4) == 0 && p + 8 + 16 <= d.size()) {
            fmt = rd16(body);
            ch = rd16(body + 2);
            rate = rd32(body + 4);
            bits = rd16(body + 14);
            if (fmt == 0xFFFE && sz >= 26) fmt = rd16(body + 24);  // extensible: the sub-format
        } else if (std::memcmp(&d[p], "data", 4) == 0) {
            data = body;
            dataBytes = std::min<std::size_t>(sz, d.size() - (p + 8));
            break;
        }
        p += 8 + std::size_t(sz) + (sz & 1);
    }
    if (!data || ch < 1) throw std::runtime_error(path + ": no audio data");
    const std::size_t bps = std::size_t(bits / 8), frames = dataBytes / (bps * std::size_t(ch));
    if (!((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) throw std::runtime_error(path + ": unsupported WAV format (use PCM 16/24/32 or float 32)");
    Audio a;
    a.rate = rate;
    a.mono.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (int c = 0; c < ch; ++c) {
            const unsigned char* s = data + (i * std::size_t(ch) + std::size_t(c)) * bps;
            double v;
            if (fmt == 3) { float x; std::memcpy(&x, s, 4); v = x; }
            else if (bits == 16) v = double(std::int16_t(rd16(s))) / 32768.0;
            else if (bits == 24) v = double((std::int32_t(s[0] << 8 | s[1] << 16 | s[2] << 24)) >> 8) / 8388608.0;
            else v = double(std::int32_t(rd32(s))) / 2147483648.0;
            sum += v;
        }
        a.mono[i] = float(sum / ch);
    }
    return a;
}

void writeWav(const std::string& path, const std::vector<float>& x, double rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    auto w32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t bytes = std::uint32_t(x.size() * 4);
    f.write("RIFF", 4); w32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(3); w16(1); w32(std::uint32_t(rate)); w32(std::uint32_t(rate) * 4); w16(4); w16(32);
    f.write("data", 4); w32(bytes);
    f.write(reinterpret_cast<const char*>(x.data()), std::streamsize(bytes));
}

std::vector<float> resample(const std::vector<float>& x, double from, double to) {
    if (std::abs(from - to) < 0.5) return x;
    const std::size_t n = std::size_t(double(x.size()) * to / from);
    std::vector<float> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = double(i) * from / to;
        const std::size_t k = std::size_t(pos);
        const double fr = pos - double(k);
        y[i] = float(x[std::min(k, x.size() - 1)] * (1 - fr) + x[std::min(k + 1, x.size() - 1)] * fr);
    }
    return y;
}

// ------------------------------------------------------------------------------------------------- the built-in riff
// Plucked strings (Karplus-Strong): power chords then a few single notes, peak about 0.5 V like a hard-picked guitar. Deterministic.
void pluck(std::vector<float>& out, double hz, double startS, double durS, double gain, std::uint64_t& rng) {
    const std::size_t len = std::max<std::size_t>(2, std::size_t(kFs / hz));
    std::vector<double> line(len);
    for (auto& v : line) {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        v = double(rng >> 11) / double(1ull << 53) * 2.0 - 1.0;
    }
    // The noise burst is smoothed a few times first: a pick on a real string is far duller than white noise.
    for (int pass = 0; pass < 3; ++pass) {
        double prev = line[len - 1];
        for (std::size_t i = 0; i < len; ++i) { const double cur = line[i]; line[i] = 0.5 * cur + 0.5 * prev; prev = cur; }
    }
    double mx = 1e-9;
    for (double v : line) mx = std::max(mx, std::abs(v));
    for (auto& v : line) v /= mx;
    const std::size_t s0 = std::size_t(startS * kFs), n = std::size_t(durS * kFs);
    std::size_t idx = 0;
    for (std::size_t i = 0; i < n && s0 + i < out.size(); ++i) {
        const std::size_t nx = (idx + 1) % len;
        const double y = line[idx];
        line[idx] = 0.4985 * (line[idx] + line[nx]);  // averaging low-pass, a little under 0.5: the note rings for a couple of seconds
        idx = nx;
        const double env = std::min(1.0, double(n - i) / (0.05 * kFs));
        out[s0 + i] += float(gain * y * env);
    }
}

std::vector<float> builtinRiff(double seconds, std::uint64_t seed) {
    std::vector<float> x(std::size_t(seconds * kFs), 0.0f);
    std::uint64_t rng = 0x9e3779b97f4a7c15ull ^ seed;
    auto chord = [&](double t, double root, double dur) {
        pluck(x, root, t, dur, 1.0, rng);
        pluck(x, root * 1.4983, t + 0.012, dur, 0.9, rng);
        pluck(x, root * 2.0, t + 0.024, dur, 0.9, rng);
    };
    const double e2 = 82.41;
    chord(0.0, e2, 1.6);
    chord(2.0, e2 * 1.1892, 1.6);   // G
    chord(4.0, e2 * 1.3348, 1.6);   // A
    chord(6.0, e2 * 1.1225, 1.8);   // F#
    // single notes, E string: a short run after the chords
    const double notes[] = {e2 * 2, e2 * 2.2449, e2 * 2.5198, e2 * 3.0};
    for (int i = 0; i < 4; ++i) pluck(x, notes[i], 8.0 + 0.5 * i, 0.7, 1.0, rng);
    x.resize(std::size_t(std::max(seconds, 10.5) * kFs), 0.0f);
    double peak = 0;
    for (float v : x) peak = std::max(peak, double(std::abs(v)));
    const float k = peak > 0 ? float(0.5 / peak) : 1.0f;
    for (auto& v : x) v *= k;
    return x;
}

// ------------------------------------------------------------------------------------------------------- analysis
struct Bands {
    std::vector<double> centreHz, db;  // 1/3-octave bands 40 Hz .. 16 kHz, power averaged over the active frames, dB (absolute, re digital full scale)
};

void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double toDb(double power) { return 10.0 * std::log10(std::max(power, 1e-20)); }

struct Metrics {
    double rmsDb = -200, peakDb = -200, crestDb = 0, centroidHz = 0;
    double lowDb = -200, lowMidDb = -200, midDb = -200, presenceDb = -200, airDb = -200;  // power in each region, dB re digital full scale
    double activeSeconds = 0;
    Bands bands;
};

Metrics analyse(const std::vector<float>& x, double rate) {
    Metrics m;
    constexpr std::size_t N = 8192;
    double sumSq = 0, peak = 0;
    for (float v : x) { sumSq += double(v) * v; peak = std::max(peak, double(std::abs(v))); }
    if (x.empty()) return m;
    // The rms is taken over the active parts only (above -50 dB of the loudest 100 ms window), so a recording with silence compares fairly.
    const std::size_t win = std::size_t(0.1 * rate);
    std::vector<double> winPow;
    for (std::size_t i = 0; i + win <= x.size(); i += win) {
        double a = 0;
        for (std::size_t k = 0; k < win; ++k) a += double(x[i + k]) * x[i + k];
        winPow.push_back(a / double(win));
    }
    double maxPow = 0;
    for (double p : winPow) maxPow = std::max(maxPow, p);
    double actSum = 0;
    std::size_t actN = 0;
    for (double p : winPow) if (p > maxPow * 1e-5) { actSum += p; ++actN; }
    const double actPow = actN ? actSum / double(actN) : sumSq / double(x.size());
    m.activeSeconds = double(actN) * 0.1;
    m.rmsDb = toDb(actPow);
    m.peakDb = 20.0 * std::log10(std::max(peak, 1e-10));
    m.crestDb = m.peakDb - m.rmsDb;
    // average power spectrum of the frames that are active
    std::vector<double> spec(N / 2 + 1, 0.0);
    std::size_t frames = 0;
    std::vector<double> hann(N);
    double wsum = 0;
    for (std::size_t i = 0; i < N; ++i) { hann[i] = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(N)); wsum += hann[i] * hann[i]; }
    for (std::size_t s = 0; s + N <= x.size(); s += N / 2) {
        double fp = 0;
        for (std::size_t k = 0; k < N; ++k) fp += double(x[s + k]) * x[s + k];
        if (fp / double(N) < maxPow * 1e-5) continue;
        std::vector<std::complex<double>> a(N);
        for (std::size_t k = 0; k < N; ++k) a[k] = double(x[s + k]) * hann[k];
        fft(a);
        for (std::size_t k = 0; k <= N / 2; ++k) spec[k] += std::norm(a[k]) / wsum / double(N) * (k == 0 || k == N / 2 ? 1.0 : 2.0);
        ++frames;
    }
    if (frames == 0) return m;
    for (auto& v : spec) v /= double(frames);
    const double df = rate / double(N);
    double cNum = 0, cDen = 0;
    double reg[5] = {0, 0, 0, 0, 0};
    for (std::size_t k = 1; k <= N / 2; ++k) {
        const double f = double(k) * df;
        cNum += f * spec[k];
        cDen += spec[k];
        const int r = f < 200 ? 0 : f < 800 ? 1 : f < 2500 ? 2 : f < 6000 ? 3 : 4;
        reg[r] += spec[k];
    }
    m.centroidHz = cDen > 0 ? cNum / cDen : 0;
    m.lowDb = toDb(reg[0]); m.lowMidDb = toDb(reg[1]); m.midDb = toDb(reg[2]); m.presenceDb = toDb(reg[3]); m.airDb = toDb(reg[4]);
    const double r2 = std::pow(2.0, 1.0 / 6.0);
    for (double c = 40.0; c < 16001.0; c *= std::pow(2.0, 1.0 / 3.0)) {
        if (c * r2 > rate / 2) break;
        double p = 0;
        for (std::size_t k = 1; k <= N / 2; ++k) { const double f = double(k) * df; if (f >= c / r2 && f < c * r2) p += spec[k]; }
        m.bands.centreHz.push_back(std::round(c));
        m.bands.db.push_back(toDb(p));
    }
    return m;
}

json metricsJson(const Metrics& m) {
    json j;
    auto r1 = [](double v) { return std::round(v * 10.0) / 10.0; };
    j["rms_dbfs"] = r1(m.rmsDb);
    j["peak_dbfs"] = r1(m.peakDb);
    j["crest_db"] = r1(m.crestDb);
    j["centroid_hz"] = std::round(m.centroidHz);
    j["active_seconds"] = r1(m.activeSeconds);
    // Share of the power in each region, in dB re the total: how the tone is balanced (what the ear calls dark / bright / scooped).
    const double tot = toDb(std::pow(10, m.lowDb / 10) + std::pow(10, m.lowMidDb / 10) + std::pow(10, m.midDb / 10) + std::pow(10, m.presenceDb / 10) + std::pow(10, m.airDb / 10));
    j["balance_db"] = {{"low_<200", r1(m.lowDb - tot)}, {"lowmid_200-800", r1(m.lowMidDb - tot)}, {"mid_800-2.5k", r1(m.midDb - tot)}, {"presence_2.5k-6k", r1(m.presenceDb - tot)}, {"air_>6k", r1(m.airDb - tot)}};
    json b = json::object();
    for (std::size_t i = 0; i < m.bands.db.size(); ++i) b[std::to_string(int(m.bands.centreHz[i]))] = r1(m.bands.db[i] - tot);
    j["bands_db"] = b;
    return j;
}

json compareJson(const Metrics& a, const Metrics& ref) {
    // Both normalised to the same total power: only the tone balance differs, not the loudness.
    auto total = [](const Metrics& m) { return toDb(std::pow(10, m.lowDb / 10) + std::pow(10, m.lowMidDb / 10) + std::pow(10, m.midDb / 10) + std::pow(10, m.presenceDb / 10) + std::pow(10, m.airDb / 10)); };
    const double ta = total(a), tr = total(ref);
    auto r1 = [](double v) { return std::round(v * 10.0) / 10.0; };
    json j;
    j["loudness_difference_db"] = r1(a.rmsDb - ref.rmsDb);
    j["crest_difference_db"] = r1(a.crestDb - ref.crestDb);
    j["centroid_ratio"] = ref.centroidHz > 0 ? std::round(a.centroidHz / ref.centroidHz * 100.0) / 100.0 : 0.0;
    j["balance_difference_db"] = {{"low_<200", r1((a.lowDb - ta) - (ref.lowDb - tr))}, {"lowmid_200-800", r1((a.lowMidDb - ta) - (ref.lowMidDb - tr))},
                                  {"mid_800-2.5k", r1((a.midDb - ta) - (ref.midDb - tr))}, {"presence_2.5k-6k", r1((a.presenceDb - ta) - (ref.presenceDb - tr))},
                                  {"air_>6k", r1((a.airDb - ta) - (ref.airDb - tr))}};
    json b = json::object();
    double sq = 0;
    std::size_t n = std::min(a.bands.db.size(), ref.bands.db.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double d = (a.bands.db[i] - ta) - (ref.bands.db[i] - tr);
        b[std::to_string(int(a.bands.centreHz[i]))] = r1(d);
        sq += d * d;
    }
    j["bands_difference_db"] = b;
    j["spectral_distance_db_rms"] = n ? r1(std::sqrt(sq / double(n))) : 0.0;  // 0 = same tone balance; < 3 is close
    j["note"] = "positive = this render has MORE of that region than the reference";
    return j;
}

// --------------------------------------------------------------------------------------------------- amp settings
const sas::UiKnob* findUi(const sas::AmpDef& def, const std::string& p) {
    for (const auto& u : def.ui) if (u.param == p) return &u;
    return nullptr;
}

void setParam(const sas::AmpDef& def, sas::ChainSettings& s, const std::string& param, double v10) {
    const sas::UiKnob* u = findUi(def, param);
    if (!u) throw std::runtime_error("amp " + def.id + " has no control '" + param + "' (see: sas_cli info " + def.id + ")");
    const double v = std::clamp(v10 / 10.0, 0.0, 1.0);
    if (!u->knob.empty()) s.knobs[def.knobIndex(u->knob)] = v;
    else if (!u->powerKnob.empty()) s.powerKnobs[def.powerKnobIndex(u->powerKnob)] = v;
    else if (param == "presence") s.presence = v;
    else throw std::runtime_error("control '" + param + "' cannot be set from here");
}

void setSelector(const sas::AmpDef& def, sas::ChainSettings& s, const std::string& name, int pos) {
    for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
        if (def.preamp.selectors[i].name == name) {
            if (pos < 0 || std::size_t(pos) >= def.preamp.selectors[i].positions.size()) throw std::runtime_error("selector " + name + " has no position " + std::to_string(pos));
            s.selectors[i] = std::size_t(pos);
            return;
        }
    throw std::runtime_error("amp " + def.id + " has no selector '" + name + "'");
}

json infoJson(const sas::AmpDef& def) {
    json j;
    j["id"] = def.id;
    j["name"] = def.name;
    j["category"] = def.category;
    j["nam_loader"] = def.nam;
    json ui = json::array();
    for (const auto& u : def.ui) {
        double d = 5.0;
        if (!u.knob.empty()) d = def.preamp.knobs[def.knobIndex(u.knob)].def * 10.0;
        else if (!u.powerKnob.empty()) d = def.power.knobs[def.powerKnobIndex(u.powerKnob)].def * 10.0;
        else if (u.param == "presence") d = def.power.presenceDefault * 10.0;
        ui.push_back({{"param", u.param}, {"label", u.label}, {"default", std::round(d * 10) / 10}, {"scale", "0..10"}});
    }
    j["controls"] = ui;
    json jacks = json::array();
    for (const auto& k : def.preamp.jacks) jacks.push_back({{"name", k.name}, {"label", k.label.empty() ? k.name : k.label}});
    j["jacks"] = jacks;
    j["jack_caption"] = def.preamp.jackLabel;
    json sels = json::array();
    for (const auto& sl : def.preamp.selectors) {
        json pos = json::array();
        for (std::size_t i = 0; i < sl.positions.size(); ++i) pos.push_back({{"index", i}, {"label", sl.positions[i].label}});
        sels.push_back({{"name", sl.name}, {"label", sl.label}, {"positions", pos}});
    }
    j["selectors"] = sels;
    json pre = json::array();
    for (const auto& p : def.presets) {
        json pp = json::object();
        for (const auto& kv : p.params) pp[kv.first] = kv.second;
        json ss = json::object();
        for (const auto& kv : p.selectors) ss[kv.first] = kv.second;
        pre.push_back({{"name", p.name}, {"jack", p.jack}, {"params", pp}, {"selectors", ss}});
    }
    j["presets"] = pre;
    for (const char* section : {"preamp_voicings", "power_voicings"}) {
        json vo = json::array();
        for (const auto& v : std::string(section) == "power_voicings" ? def.powerVoicings : def.preampVoicings) {
            json pos = json::array();
            for (std::size_t i = 0; i < v.positions.size(); ++i) pos.push_back({{"index", i}, {"label", v.positions[i].label}});
            vo.push_back({{"name", v.name}, {"label", v.label}, {"positions", pos}});
        }
        j[section] = vo;
    }
    return j;
}

sas::ChainSettings defaults(const sas::AmpDef& def) {
    sas::ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
    s.presence = def.power.presenceDefault;
    s.selectors.assign(def.preamp.selectors.size(), 0);
    s.cabFilter = true;
    return s;
}

// ------------------------------------------------------------------------------------------------------ arguments
struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::vector<std::string>> opt;
    bool has(const std::string& k) const { return opt.count(k) > 0; }
    std::string get(const std::string& k, const std::string& d = "") const { auto it = opt.find(k); return it == opt.end() ? d : it->second.back(); }
    double num(const std::string& k, double d) const { return has(k) ? std::atof(get(k).c_str()) : d; }
};

Args parse(int argc, char** argv, int from) {
    Args a;
    for (int i = from; i < argc; ++i) {
        const std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + s);
            a.opt[s.substr(2)].push_back(argv[++i]);
        } else a.pos.push_back(s);
    }
    return a;
}

sas::Quality parseQuality(const std::string& q) {
    if (q == "normal" || q.empty()) return sas::Quality::Normal;
    if (q == "high") return sas::Quality::High;
    if (q == "lite") return sas::Quality::Lite;
    if (q == "eco") return sas::Quality::Eco;
    if (q == "efficient") return sas::Quality::Efficient;
    throw std::runtime_error("quality must be normal, high, lite, eco or efficient");
}

json doRender(const Args& a) {
    if (!a.has("amp")) throw std::runtime_error("--amp <id> is required");
    sas::AmpDef def = sas::loadEmbeddedAmp(a.get("amp"));
    if (def.nam) throw std::runtime_error("the NAM loader amp needs a .nam model: not supported by this tool");
    sas::ChainSettings s = defaults(def);
    json applied = json::object();
    if (a.has("preset")) {
        const std::string want = a.get("preset");
        const sas::AmpPresetDef* found = nullptr;
        for (const auto& p : def.presets) if (p.name == want) found = &p;
        if (!found) throw std::runtime_error("amp " + def.id + " has no preset '" + want + "'");
        if (!found->jack.empty()) s.jack = def.jackIndex(found->jack);
        for (const auto& kv : found->params) setParam(def, s, kv.first, kv.second);
        for (const auto& kv : found->selectors) setSelector(def, s, kv.first, kv.second);
        applied["preset"] = want;
    }
    if (a.has("jack")) s.jack = def.jackIndex(a.get("jack"));
    for (const auto& kv : a.opt) {
        if (kv.first == "set")
            for (const auto& item : kv.second) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) throw std::runtime_error("--set expects param=value, got '" + item + "'");
                setParam(def, s, item.substr(0, eq), std::atof(item.c_str() + eq + 1));
            }
        if (kv.first == "sel")
            for (const auto& item : kv.second) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) throw std::runtime_error("--sel expects name=position, got '" + item + "'");
                setSelector(def, s, item.substr(0, eq), std::atoi(item.c_str() + eq + 1));
            }
    }
    // The power amp's voicings (--voicing name=position) and edits of single parts (--part NAME=factor for the preamp, --power-part NAME=factor).
    sas::Condition cond;
    if (a.has("voicing"))
        for (const auto& item : a.opt.at("voicing")) {
            const auto eq = item.find('=');
            if (eq == std::string::npos) throw std::runtime_error("--voicing expects name=position, got '" + item + "'");
            const std::string vn = item.substr(0, eq);
            const std::size_t pos = std::size_t(std::atoi(item.c_str() + eq + 1));
            const sas::VoicingDef* found = nullptr;
            bool power = false;
            for (const auto& v : def.preampVoicings) if (v.name == vn) found = &v;
            for (const auto& v : def.powerVoicings) if (v.name == vn) { found = &v; power = true; }
            if (!found) throw std::runtime_error("amp " + def.id + " has no voicing '" + vn + "'");
            if (pos >= found->positions.size()) throw std::runtime_error("voicing " + vn + " has no position " + std::to_string(pos));
            for (const auto& [part, f] : found->positions[pos].factors) cond.parts[(power ? "P:" : "") + part] = f;
        }
    for (const auto& [opt, prefix] : {std::pair<const char*, const char*>{"part", ""}, {"power-part", "P:"}})
        if (a.has(opt))
            for (const auto& item : a.opt.at(opt)) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) throw std::runtime_error(std::string("--") + opt + " expects NAME=factor, got '" + item + "'");
                cond.parts[prefix + item.substr(0, eq)] = std::atof(item.c_str() + eq + 1);
            }
    def = sas::applyCondition(def, cond);
    s.inputDb = a.num("input-db", 0.0);
    s.outputDb = a.num("output-db", 0.0);
    s.quality = parseQuality(a.get("quality"));
    s.cabFilter = a.get("cab", "on") != "off";
    s.noise = a.num("noise", 0.0);
    if (a.has("ts")) {
        double d = 5, t = 5, l = 5;
        if (std::sscanf(a.get("ts").c_str(), "%lf,%lf,%lf", &d, &t, &l) != 3) throw std::runtime_error("--ts expects drive,tone,level (0..10)");
        s.tsOn = true;
        s.tsDrive = std::clamp(d / 10, 0.0, 1.0);
        s.tsTone = std::clamp(t / 10, 0.0, 1.0);
        s.tsLevel = std::clamp(l / 10, 0.0, 1.0);
    }

    std::vector<float> in;
    std::string source = "built-in riff (E5 G5 A5 F#5 chords, then single notes)";
    if (a.has("in")) {
        const Audio au = readWav(a.get("in"));
        in = resample(au.mono, au.rate, kFs);
        source = a.get("in");
        const double sec = a.num("seconds", 0.0);
        if (sec > 0 && in.size() > std::size_t(sec * kFs)) in.resize(std::size_t(sec * kFs));
    } else {
        in = builtinRiff(a.num("seconds", 10.5), std::uint64_t(a.num("seed", 1)));
    }
    if (in.empty()) throw std::runtime_error("the input is empty");

    sas::Chain chain(def, kFs);
    chain.snap(s);
    const std::size_t pad = std::size_t(0.3 * kFs);  // settle the controls and the supply before the part that is kept
    std::vector<float> src(pad + in.size(), 0.0f), out(src.size());
    std::copy(in.begin(), in.end(), src.begin() + std::ptrdiff_t(pad));
    for (std::size_t i = 0; i < src.size(); i += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, src.size() - i);
        chain.processAmp(src.data() + i, out.data() + i, n);
        chain.applyOutput(out.data() + i, n);
    }
    std::vector<float> res(out.begin() + std::ptrdiff_t(pad), out.end());
    bool bad = false;
    for (float v : res) if (!std::isfinite(v)) { bad = true; break; }
    if (bad) throw std::runtime_error("the render produced NaN or infinite samples");

    json j;
    j["amp"] = def.id;
    j["source"] = source;
    j["quality"] = a.get("quality", "normal");
    j["seconds"] = std::round(double(res.size()) / kFs * 10) / 10;
    if (a.has("out")) { writeWav(a.get("out"), res, kFs); j["out"] = a.get("out"); }
    j["output"] = metricsJson(analyse(res, kFs));
    j["input"] = metricsJson(analyse(in, kFs));
    if (a.has("ref")) {
        const Audio ref = readWav(a.get("ref"));
        j["compare_to_reference"] = compareJson(analyse(res, kFs), analyse(resample(ref.mono, ref.rate, kFs), kFs));
    }
    return j;
}

json doAnalyze(const Args& a) {
    if (a.pos.empty()) throw std::runtime_error("usage: sas_cli analyze <file.wav> [--ref other.wav]");
    const Audio au = readWav(a.pos[0]);
    const auto x = resample(au.mono, au.rate, kFs);
    json j;
    j["file"] = a.pos[0];
    j["seconds"] = std::round(double(x.size()) / kFs * 10) / 10;
    const Metrics m = analyse(x, kFs);
    j["metrics"] = metricsJson(m);
    if (a.has("ref")) {
        const Audio ref = readWav(a.get("ref"));
        j["compare_to_reference"] = compareJson(m, analyse(resample(ref.mono, ref.rate, kFs), kFs));
    }
    return j;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: sas_cli list | info <amp> | render --amp <id> ... | analyze <file.wav> [--ref other.wav]");
        const std::string cmd = argv[1];
        json out;
        if (cmd == "list") {
            out = json::array();
            for (const auto& id : sas::embeddedAmpIds()) {
                const sas::AmpDef def = sas::loadEmbeddedAmp(id);
                out.push_back({{"id", id}, {"name", def.name}, {"category", def.category}});
            }
        } else if (cmd == "info") {
            if (argc < 3) throw std::runtime_error("usage: sas_cli info <amp>");
            out = infoJson(sas::loadEmbeddedAmp(argv[2]));
        } else if (cmd == "render") out = doRender(parse(argc, argv, 2));
        else if (cmd == "analyze") out = doAnalyze(parse(argc, argv, 2));
        else throw std::runtime_error("unknown command '" + cmd + "'");
        std::printf("%s\n", out.dump(1).c_str());
        return 0;
    } catch (const std::exception& e) {
        std::printf("%s\n", json{{"error", e.what()}}.dump().c_str());
        return 1;
    }
}
