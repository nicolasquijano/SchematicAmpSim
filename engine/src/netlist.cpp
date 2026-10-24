#include "aes/netlist.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace aes {

namespace {

// ------------------------------------------------------------- expressions

class ExprParser {
public:
    ExprParser(std::string_view s, const std::map<std::string, double>& params) : s_(s), params_(params) {}

    double parse() {
        const double v = sum();
        if (pos_ != s_.size()) fail("unexpected '" + std::string(1, s_[pos_]) + "'");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw NetlistError("expression '" + std::string(s_) + "': " + what);
    }

    bool eat(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    double sum() {
        double v = product();
        for (;;) {
            if (eat('+')) v += product();
            else if (eat('-')) v -= product();
            else return v;
        }
    }

    double product() {
        double v = unary();
        for (;;) {
            if (eat('*')) v *= unary();
            else if (eat('/')) v /= unary();
            else return v;
        }
    }

    double unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return primary();
    }

    double primary() {
        if (eat('(')) {
            const double v = sum();
            if (!eat(')')) fail("missing ')'");
            return v;
        }
        if (pos_ >= s_.size()) fail("unexpected end");
        const char c = s_[pos_];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return identifier();
        fail(std::string("unexpected '") + c + "'");
    }

    // Decimal literal with an optional exponent or SI suffix. The suffix becomes a decimal
    // exponent so the text goes through strtod exactly like the same literal in C++ source
    // (5.6k parses to the double 5.6e3, not 5.6 * 1000).
    double number() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.')) ++pos_;
        std::string lit(s_.substr(start, pos_ - start));
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            std::size_t p = pos_ + 1;
            if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) ++p;
            if (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) {
                while (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) ++p;
                lit += std::string(s_.substr(pos_, p - pos_));
                pos_ = p;
            }
        } else if (pos_ < s_.size()) {
            const char* exp = nullptr;
            switch (s_[pos_]) {
                case 'G': exp = "e9"; break;
                case 'M': exp = "e6"; break;
                case 'k': exp = "e3"; break;
                case 'm': exp = "e-3"; break;
                case 'u': exp = "e-6"; break;
                case 'n': exp = "e-9"; break;
                case 'p': exp = "e-12"; break;
                case 'f': exp = "e-15"; break;
                default: break;
            }
            if (exp) {
                ++pos_;
                // Schematic notation: the suffix stands in for the decimal point (4k7 = 4.7k, 1M0 = 1M).
                if (lit.find('.') == std::string::npos && pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                    lit += '.';
                    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) lit += s_[pos_++];
                }
                lit += exp;
            }
        }
        if (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
            fail("bad number near '" + lit + "'");
        char* end = nullptr;
        const double v = std::strtod(lit.c_str(), &end);
        if (end != lit.c_str() + lit.size()) fail("bad number '" + lit + "'");
        return v;
    }

    double identifier() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) ++pos_;
        const std::string name(s_.substr(start, pos_ - start));
        if (eat('(')) {
            const double a = sum();
            if (!eat(')')) fail("missing ')' after " + name);
            if (name == "sqrt") return std::sqrt(a);
            if (name == "exp") return std::exp(a);
            if (name == "ln") return std::log(a);
            if (name == "abs") return std::abs(a);
            fail("unknown function " + name);
        }
        const auto it = params_.find(name);
        if (it == params_.end()) fail("unknown parameter '" + name + "'");
        return it->second;
    }

    std::string_view s_;
    const std::map<std::string, double>& params_;
    std::size_t pos_ = 0;
};

// ------------------------------------------------------------- parser

std::vector<std::string> tokens(const std::string& line) {
    std::istringstream is(line);
    std::vector<std::string> t;
    for (std::string w; is >> w;) t.push_back(w);
    return t;
}

struct Parser {
    ParsedNetlist out;
    std::map<std::string, double> params;
    std::string origin;
    std::size_t lineNo = 0;

    // xfmr block state
    struct Winding { Node a, b; double turns; std::string group; };
    bool inXfmr = false;
    std::string xfmrName;
    double xfmrL = 0;
    std::vector<Winding> windings;
    std::map<std::pair<std::string, std::string>, double> couplings;

    [[noreturn]] void fail(const std::string& what) const {
        throw NetlistError(origin + ":" + std::to_string(lineNo) + ": " + what);
    }

    double value(const std::string& expr) const {
        try {
            return evalExpression(expr, params);
        } catch (const NetlistError& e) {
            fail(e.what());
        }
    }

    Node node(const std::string& name) {
        if (name == "0" || name == "gnd") return GND;
        const auto it = out.nodes.find(name);
        if (it != out.nodes.end()) return it->second;
        const Node n = out.circuit.node(name);
        out.nodes[name] = n;
        return n;
    }

    void need(const std::vector<std::string>& t, std::size_t n, const char* usage) const {
        if (t.size() != n) fail(std::string("expected: ") + usage);
    }

    template <class Model, class Lookup>
    Model model(const std::string& name, Lookup&& lookup) const {
        Model m{};
        if (!lookup(name, m)) fail("unknown model '" + name + "'");
        return m;
    }

    void element(const std::vector<std::string>& t) {
        const char kind = t[0].size() == 1 ? t[0][0] : '?';
        if (t.size() < 2) fail("element needs a name");
        const std::string& id = t[1];
        auto triodeModel = [](const std::string& n, TriodeModel& m) { if (n == "ECC83") { m = TriodeModel::ecc83(); return true; } return false; };
        auto pentodeModel = [](const std::string& n, PentodeModel& m) {
            if (n == "EL34") { m = PentodeModel::el34(); return true; }
            if (n == "6550") { m = PentodeModel::el6550(); return true; }
            return false;
        };
        auto diodeModel = [](const std::string& n, DiodeModel& m) {
            if (n == "SIGNAL") { m = DiodeModel::signal(); return true; }
            if (n == "RECTIFIER_BRIDGE") { m = DiodeModel::rectifierBridge(); return true; }
            return false;
        };
        auto bjtModel = [](const std::string& n, BjtModel& m) { if (n == "C1815") { m = BjtModel::c1815(); return true; } return false; };
        auto opampModel = [](const std::string& n, OpampModel& m) { if (n == "RC4558") { m = OpampModel::rc4558(); return true; } return false; };

        auto unique = [&](auto& registry) {
            if (registry.count(id)) fail("duplicate element name '" + id + "'");
        };
        Circuit& c = out.circuit;
        switch (kind) {
            case 'R':
                need(t, 5, "R name a b value");
                unique(out.resistors);
                if (const double ohms = value(t[4]); ohms > 0.0)
                    out.resistors[id] = c.resistor(node(t[2]), node(t[3]), ohms);
                else
                    fail("resistance must be positive");
                break;
            case 'C':
                need(t, 5, "C name a b value");
                c.capacitor(node(t[2]), node(t[3]), value(t[4]));
                break;
            case 'T':
                if (t.size() != 6 && t.size() != 7) fail("expected: T name g p k MODEL [scale]");
                c.triode(node(t[2]), node(t[3]), node(t[4]), model<TriodeModel>(t[5], triodeModel), t.size() == 7 ? value(t[6]) : 1.0);
                break;
            case 'P':
                need(t, 8, "P name g1 p g2 k MODEL scale");
                c.pentode(node(t[2]), node(t[3]), node(t[4]), node(t[5]), model<PentodeModel>(t[6], pentodeModel), value(t[7]));
                break;
            case 'D':
                need(t, 5, "D name anode cathode MODEL");
                c.diode(node(t[2]), node(t[3]), model<DiodeModel>(t[4], diodeModel));
                break;
            case 'Q':
                need(t, 6, "Q name b c e MODEL");
                c.bjt(node(t[2]), node(t[3]), node(t[4]), model<BjtModel>(t[5], bjtModel));
                break;
            case 'O':
                need(t, 7, "O name plus minus out vref MODEL");
                c.opamp(node(t[2]), node(t[3]), node(t[4]), node(t[5]), model<OpampModel>(t[6], opampModel));
                break;
            case 'G':
                need(t, 6, "G name into cp cn gm");
                c.vccs(node(t[2]), node(t[3]), node(t[4]), value(t[5]));
                break;
            case 'V':
                need(t, 5, "V name node ohms volts");
                unique(out.sources);
                out.sources[id] = c.supply(node(t[2]), value(t[4]), value(t[3]));
                break;
            case 'S':
                need(t, 5, "S name node ohms dc");
                unique(out.sources);
                out.sources[id] = c.source(node(t[2]), value(t[3]), value(t[4]));
                break;
            case 'I':
                need(t, 4, "I name node ohms");
                unique(out.sources);
                out.sources[id] = c.input(node(t[2]), value(t[3]));
                break;
            default:
                fail("unknown element type '" + t[0] + "'");
        }
    }

    void directive(const std::string& line, const std::vector<std::string>& t) {
        const std::string& d = t[0];
        if (d == ".param") {
            std::string rest;
            for (std::size_t i = 1; i < t.size(); ++i) rest += t[i];
            const auto eq = rest.find('=');
            if (eq == std::string::npos || eq == 0) fail("expected: .param name = expr");
            params[rest.substr(0, eq)] = value(rest.substr(eq + 1));
        } else if (d == ".nodes") {
            for (std::size_t i = 1; i < t.size(); ++i) node(t[i]);
        } else if (d == ".xfmr") {
            if (t.size() != 3 || t[2].rfind("L=", 0) != 0) fail("expected: .xfmr name L=expr");
            inXfmr = true;
            xfmrName = t[1];
            xfmrL = value(t[2].substr(2));
            windings.clear();
            couplings.clear();
        } else if (d == ".end") {
            if (!inXfmr) fail(".end without .xfmr");
            finishXfmr();
            inXfmr = false;
        } else {
            fail("unknown directive " + d);
        }
        (void)line;
    }

    void finishXfmr() {
        const std::size_t w = windings.size();
        if (w == 0) fail(".xfmr " + xfmrName + " has no windings");
        std::vector<double> l(w * w);
        std::vector<std::pair<Node, Node>> ends;
        for (const auto& wd : windings) ends.push_back({wd.a, wd.b});
        for (std::size_t i = 0; i < w; ++i)
            for (std::size_t j = 0; j < w; ++j) {
                double k = 1.0;
                if (i != j) {
                    auto it = couplings.find({windings[i].group, windings[j].group});
                    if (it == couplings.end()) it = couplings.find({windings[j].group, windings[i].group});
                    if (it == couplings.end())
                        fail(".xfmr " + xfmrName + ": no K for groups " + windings[i].group + "/" + windings[j].group);
                    k = it->second;
                }
                l[i * w + j] = xfmrL * windings[i].turns * windings[j].turns * k;
            }
        out.circuit.coupledInductors(ends, l);
    }

    void xfmrLine(const std::vector<std::string>& t) {
        if (t[0] == "W") {
            need(t, 5, "W a b turns group");
            windings.push_back({node(t[1]), node(t[2]), value(t[3]), t[4]});
        } else if (t[0] == "K") {
            need(t, 4, "K group group coupling");
            couplings[{t[1], t[2]}] = value(t[3]);
        } else {
            fail("only W and K lines are allowed inside .xfmr");
        }
    }
};

}  // namespace

double evalExpression(std::string_view expr, const std::map<std::string, double>& params) {
    return ExprParser(expr, params).parse();
}

Node ParsedNetlist::node(const std::string& name) const {
    if (name == "0" || name == "gnd") return GND;
    const auto it = nodes.find(name);
    if (it == nodes.end()) throw NetlistError("no node named '" + name + "'");
    return it->second;
}

ResId ParsedNetlist::resistor(const std::string& name) const {
    const auto it = resistors.find(name);
    if (it == resistors.end()) throw NetlistError("no resistor named '" + name + "'");
    return it->second;
}

SrcId ParsedNetlist::source(const std::string& name) const {
    const auto it = sources.find(name);
    if (it == sources.end()) throw NetlistError("no source named '" + name + "'");
    return it->second;
}

ParsedNetlist parseNetlist(std::string_view text, const std::string& origin) {
    Parser p;
    p.origin = origin;
    std::istringstream is{std::string(text)};
    for (std::string line; std::getline(is, line);) {
        ++p.lineNo;
        if (const auto semi = line.find(';'); semi != std::string::npos) line.resize(semi);
        auto t = tokens(line);
        if (t.empty() || t[0][0] == '*' || t[0][0] == '+') continue;
        if (p.inXfmr && t[0][0] != '.') p.xfmrLine(t);
        else if (t[0][0] == '.') p.directive(line, t);
        else p.element(t);
    }
    if (p.inXfmr) p.fail("missing .end for .xfmr " + p.xfmrName);
    return std::move(p.out);
}

}  // namespace aes
