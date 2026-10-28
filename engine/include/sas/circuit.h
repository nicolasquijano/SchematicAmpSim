#pragma once
// Circuit description and a per-sample DK-method solver. Port of jcm800/src/circuit.rs.
//
// Linear parts (resistors, capacitors, coupled inductors) are stamped into a nodal matrix with
// trapezoidal companion models and inverted ahead of time. Nonlinear devices expose "ports" whose
// voltages are solved per sample by damped Newton, with a bounded continuation fallback.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sas/devices.h"
#include "sas/linalg.h"

namespace sas {

using Node = std::size_t;  // 0 is ground
inline constexpr Node GND = 0;

struct ResId { std::size_t i; };
struct SrcId { std::size_t i; };

class Circuit {
public:
    enum class Kind { Triode, Pentode, Diode, Bjt, OpampOut, Npn, Ota };
    struct Dev {
        Kind kind;
        Node a = 0, b = 0, c = 0, d = 0;  // triode g,p,k | pentode g1,p,g2,k | diode a,k | bjt b,c,e | opamp sense,ref,out
        double scale = 1.0;               // pentodes in parallel
        TriodeModel triode{};
        PentodeModel pentode{};
        DiodeModel diode{};
        BjtModel bjt{};
        OpampModel opamp{};
        NpnModel npn{};
        OtaModel ota{};  // Ota: a = plus, b = minus, c = out, d = control pin
    };
    struct Source { Node node; double ohms; double dc; };
    struct CoupledDef {
        std::vector<std::pair<Node, Node>> windings;
        std::vector<double> l;
        // Core saturation (optional): across the series chain of `satWindings` an extra magnetising current
        // (lambda / Ltot) * (|lambda| / satFlux)^satExp flows, lambda being their summed flux linkage in volt-seconds.
        double satFlux = 0.0;
        double satExp = 4.0;
        std::vector<std::size_t> satWindings;
    };
    struct Res { Node a, b; double ohms; };
    struct Cap { Node a, b; double farads; };
    struct Vccs { Node into, ctrlPlus, ctrlMinus; double gm; };

    Circuit() { names_.push_back("gnd"); }

    // Returns the node with this name, creating it if needed.
    Node node(const std::string& name);
    ResId resistor(Node a, Node b, double ohms);
    void setResistor(ResId id, double ohms) { res_[id.i].ohms = ohms; }
    void moveResistor(ResId id, Node a, Node b) { res_[id.i].a = a; res_[id.i].b = b; }
    void capacitor(Node a, Node b, double farads);
    // `scale` identical triodes in parallel (grids, plates and cathodes tied): currents and
    // interelectrode capacitances scale, the Newton problem stays the size of one triode.
    void triode(Node g, Node p, Node k, const TriodeModel& model, double scale = 1.0);
    void pentode(Node g1, Node p, Node g2, Node k, const PentodeModel& model, double scale);
    void diode(Node anode, Node cathode, const DiodeModel& model);
    void bjt(Node b, Node c, Node e, const BjtModel& model);
    void npn(Node b, Node c, Node e, const NpnModel& model);
    // Transconductance amplifier: output current into `out`, set by v(plus) - v(minus) and the current in the control pin `ctrl`.
    void ota(Node plus, Node minus, Node out, Node ctrl, const OtaModel& model);
    // gm * (v(ctrlPlus) - v(ctrlMinus)) amps injected into `into`.
    void vccs(Node into, Node ctrlPlus, Node ctrlMinus, double gm);
    // Op-amp biased around vref; inputs draw no current.
    void opamp(Node plus, Node minus, Node out, Node vref, const OpampModel& model);
    // The same op-amp without the output limit: a linear gm / dominant pole / buffer, so it adds no nonlinear port to the Newton system
    // (for stages that stay well inside the supply).
    void opampLinear(Node plus, Node minus, Node out, Node vref, const OpampModel& model);
    SrcId source(Node node, double ohms, double dc);
    SrcId supply(Node node, double volts, double ohms) { return source(node, ohms, volts); }
    void setSourceDc(SrcId id, double dc) { sources_[id.i].dc = dc; }
    double sourceDc(SrcId id) const { return sources_[id.i].dc; }
    const Res& resistorAt(ResId id) const { return res_[id.i]; }
    SrcId input(Node node, double ohms);
    // windings[i] = (from, to); l = full symmetric inductance matrix, row-major, henries.
    void coupledInductors(const std::vector<std::pair<Node, Node>>& windings, const std::vector<double>& l,
                          double satFlux = 0.0, double satExp = 4.0, const std::vector<std::size_t>& satWindings = {});

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

class TriodeTable;
class PentodeTable;

class Solver {
public:
    // useTables: evaluate triodes through interpolation tables (sas/tables.h), cheaper and equal to
    // the formulas within ~1e-6; the exact formulas stay the default (the Rust-parity modes).
    Solver(Circuit circuit, double fs, bool useTables = false);
    ~Solver();
    Solver(Solver&&) noexcept;
    Solver& operator=(Solver&&) noexcept;

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
    // Chord mode: the first Newton iteration of a sample reuses the previous sample's LU factors instead of factorising
    // again (a stale Jacobian costs a few more iterations but saves most factorisations; a failed first step refactors).
    bool chord = false;
    double luKeepRatio = 0.0625;  // a full step that cuts the squared residual below this fraction keeps the LU factors
    bool dcConverged = false;
    std::uint64_t continuationRuns = 0, jacobianFactorizations = 0, newtonFailures = 0,
                  newtonSoftFailures = 0, newtonIterations = 0;
    double worstFailedResidual = 0.0;

private:
    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);

    struct DevInst { Circuit::Dev dev; std::size_t base, np, derOff; const TriodeTable* table = nullptr; const PentodeTable* ptable = nullptr; };
    struct Coupled {
        std::vector<std::pair<std::size_t, std::size_t>> ends;
        std::vector<double> gl, vPrev, iPrev, h;
        std::vector<double> l;  // inductance matrix, for the flux linkages
        double satFlux = 0.0, satExp = 4.0, satInvL = 0.0;
        std::vector<std::size_t> satWindings;
        std::size_t satA = 0, satB = 0;  // series chain of the saturating windings (solver node indices)
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
    bool newton(std::size_t maxIter, bool reuseLu = false);
    bool continuation();
    void dcOperatingPoint();
    void computeSrAndP();
    void reconstructVoltages();

    std::size_t n_, p_;
    double fs_;
    std::vector<std::unique_ptr<TriodeTable>> tables_;  // one per distinct triode model
    std::vector<std::unique_ptr<PentodeTable>> ptables_;  // one per distinct pentode model
    bool fast_ = false;  // the cheap modes: tables and the pivot-free Jacobian factorisation

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

}  // namespace sas
