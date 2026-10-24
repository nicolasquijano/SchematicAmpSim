#pragma once
// Loads the reference data written by the Rust `dump_golden` example.

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AES_GOLDEN_DIR
#error "AES_GOLDEN_DIR must point at the golden/ folder"
#endif

namespace golden {

struct Case {
    std::string name;
    std::map<std::string, std::string> kv;  // kind, fs, volume, ...
    std::vector<double> in, out;

    double num(const std::string& key) const { return std::stod(kv.at(key)); }
    const std::string& str(const std::string& key) const { return kv.at(key); }
};

inline std::vector<double> readF64(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open " + path);
    const auto bytes = std::size_t(f.tellg());
    std::vector<double> v(bytes / sizeof(double));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), std::streamsize(bytes));
    return v;
}

inline Case load(const std::string& name) {
    const std::string dir = AES_GOLDEN_DIR;
    std::ifstream man(dir + "/manifest.txt");
    std::string line;
    while (std::getline(man, line)) {
        std::istringstream is(line);
        std::string first;
        is >> first;
        if (first != name) continue;
        Case c;
        c.name = name;
        std::string tok;
        while (is >> tok) {
            const auto eq = tok.find('=');
            if (eq != std::string::npos) c.kv[tok.substr(0, eq)] = tok.substr(eq + 1);
        }
        c.in = readF64(dir + "/" + name + ".in.f64");
        c.out = readF64(dir + "/" + name + ".out.f64");
        return c;
    }
    throw std::runtime_error("golden case not found: " + name);
}

// DC node voltages: lines "<node> <volts>" in node order (node 0 = first unknown).
inline std::vector<double> loadDc(const std::string& file) {
    std::ifstream f(std::string(AES_GOLDEN_DIR) + "/" + file);
    if (!f) throw std::runtime_error("cannot open " + file);
    std::vector<double> v;
    std::size_t node;
    double volts;
    while (f >> node >> volts) v.push_back(volts);
    return v;
}

// Signal comparison helpers.
struct Compare {
    double rmsRef = 0, rmsErr = 0, peakErr = 0;
    double errDb() const { return 20.0 * std::log10((rmsErr + 1e-300) / (rmsRef + 1e-300)); }
};

inline Compare compare(const std::vector<double>& got, const std::vector<double>& ref) {
    Compare c;
    const std::size_t n = std::min(got.size(), ref.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double e = got[i] - ref[i];
        c.rmsRef += ref[i] * ref[i];
        c.rmsErr += e * e;
        c.peakErr = std::max(c.peakErr, std::abs(e));
    }
    c.rmsRef = std::sqrt(c.rmsRef / double(n));
    c.rmsErr = std::sqrt(c.rmsErr / double(n));
    return c;
}

}  // namespace golden
