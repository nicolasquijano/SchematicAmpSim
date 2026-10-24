#pragma once
// Circuit description and a per-sample DK-method solver. Port of jcm800/src/circuit.rs.
//
// Linear parts (resistors, capacitors, coupled inductors) are stamped into a nodal matrix with
// trapezoidal companion models and inverted ahead of time. Nonlinear devices expose "ports" whose
// voltages are solved per sample by damped Newton, with a bounded continuation fallback.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "aes/devices.h"
#include "aes/linalg.h"

namespace aes {

using Node = std::size_t;  // 0 is ground
inline constexpr Node GND = 0;

struct ResId { std::size_t i; };
struct SrcId { std::size_t i; };

class Circuit {
public:
    enum class Kind { Triode, Pentode, Diode, Bjt, OpampOut };
    struct Dev {
        Kind kind;
        Node a = 0, b = 0, c = 0, d = 0;  // triode g,p,k | pentode g1,p,g2,k | diode a,k | bjt b,c,e | opamp sense,ref,out
        double scale = 1.0;               // pentodes in parallel
        TriodeModel triode{};
        PentodeModel pentode{};
        DiodeModel diode{};
        BjtModel bjt{};
        OpampModel opamp{};
    };
    struct Source { Node node; double ohms; double dc; };
    struct CoupledDef { std::vector<std::pair<Node, Node>> windings; std::vector<double> l; };
    struct Res { Node a, b; double ohms; };
    struct Cap { Node a, b; double farads; };
    struct Vccs { Node into, ctrlPlus, ctrlMinus; double gm; };

    Circuit() { names_.push_back("gnd"); }

    // Returns the node with this name, creating it if needed.
    Node node(const std::string& name);
    ResId resistor(Node a, Node b, double ohms);
    void setResistor(ResId id, double ohms) { res_[id.i].ohms = ohms; }
    void capacitor(Node a, Node b, double farads);
    void triode(Node g, Node p, Node k, const TriodeModel& model);
    void pentode(Node g1, Node p, Node g2, Node k, const PentodeModel& model, double scale);
    void diode(Node anode, Node cathode, const DiodeModel& model);
    void bjt(Node b, Node c, Node e, const BjtModel& model);
    // gm * (v(ctrlPlus) - v(ctrlMinus)) amps injected into `into`.
    void vccs(Node into, Node ctrlPlus, Node ctrlMinus, double gm);
    // Op-amp biased around vref; inputs draw no current.
    void opamp(Node plus, Node minus, Node out, Node vref, const OpampModel& model);
    SrcId source(Node node, double ohms, double dc);
    SrcId supply(Node node, double volts, double ohms) { return source(node, ohms, volts); }
    SrcId input(Node node, double ohms);
    // windings[i] = (from, to); l = full symmetric inductance matrix, row-major, henries.
    void coupledInductors(const std::vector<std::pair<Node, Node>>& windings, const std::vector<double>& l);

    std::size_t nodeCount() const { return names_.size() - 1; }

private:
    friend class Solver;
    std::vector<std::string> names_;
    std::vector<Res> res_;
    std::vector<Cap> caps_;
    std::vector<Dev> devices_;
    std::vector<Source> sources_;
    std::vector<CoupledDef> coupled_;
    std::vector<Vccs> vccs_;
    bool hasInput_ = false;
    SrcId input_{0};
};

class Solver {
public:
    Solver(Circuit circuit, double fs);

    double sampleRate() const { return fs_; }
    std::size_t nodeCount() const { return n_; }
    std::size_t portCount() const { return p_; }

    void setResistor(ResId id, double ohms);
    // Applies pending resistor changes.
    void refresh();
    void setSource(SrcId id, double volts) { srcVal_[id.i] = volts; }
    double voltage(Node node) const { return v_[gx(node)]; }
    double windingCurrent(std::size_t set, std::size_t i) const { return coupled_[set].iPrev[i]; }

    void step();
    void process(double vin) {
        if (hasInput_) srcVal_[inputSrc_] = vin;
        step();
    }

    // Tunables / counters (public like the Rust fields).
    std::size_t plainIterations = 24;
    double tolerance = 1e-3;
    bool dcConverged = false;
    std::uint64_t continuationRuns = 0, jacobianFactorizations = 0, newtonFailures = 0,
                  newtonSoftFailures = 0, newtonIterations = 0;
    double worstFailedResidual = 0.0;

private:
    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);

    struct DevInst { Circuit::Dev dev; std::size_t base, np, derOff; };
    struct Coupled {
        std::vector<std::pair<std::size_t, std::size_t>> ends;
        std::vector<double> gl, vPrev, iPrev, h;
    };
    struct SrcInst { std::size_t node; double g; double dc; };
    struct Inj { std::size_t node; double coeff; };
    struct Pattern { std::size_t row, col, di; };

    std::size_t gx(Node node) const { return node == GND ? n_ : node - 1; }
    static std::size_t ix(Node node) { return node == GND ? NONE : node - 1; }

    void build(bool dc);
    double residualCached();
    double eval(bool useTrial);
    bool factorJacobian();
    bool newtonDirection();
    bool newton(std::size_t maxIter);
    bool continuation();
    void dcOperatingPoint();
    void computeSrAndP();
    void reconstructVoltages();

    std::size_t n_, p_;
    double fs_;

    std::vector<Circuit::Res> res_;
    std::vector<Circuit::Cap> caps_;
    std::vector<double> capG_, capI_, capV_;
    std::vector<std::pair<std::size_t, std::size_t>> capIx_;
    std::vector<DevInst> devs_;
    std::vector<Coupled> coupled_;
    std::vector<SrcInst> sources_;
    std::vector<double> srcVal_;
    std::size_t inputSrc_ = 0;
    bool hasInput_ = false;

    std::vector<std::size_t> brA_, brB_;
    std::vector<std::vector<Inj>> inj_;
    struct VccsInst { std::size_t into, cp, cn; double gm; };
    std::vector<VccsInst> vccs_;

    Inverter inverter_;
    std::vector<double> m_, s_, ks_, k_;
    bool dirty_ = false;

    std::vector<double> x_, v_, r_, sr_, pvec_, ivec_, der_;
    std::vector<Pattern> dPattern_;
    std::vector<double> f_, lu_;
    std::vector<std::size_t> piv_;
    bool luValid_ = false;
    std::vector<double> dx_, xt_;

    double fmax_ = 0.0;
    std::vector<char> gridPort_;
    std::vector<double> xGood_, pGood_, pTarget_, xSave_;
    bool haveGood_ = false;
    std::vector<double> gridUpLimit_;
    double lambdaHint_ = 1.0;
};

}  // namespace aes
