#include "sas/circuit.h"
#include "sas/profile.h"
#include "sas/tables.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sas {

namespace {

constexpr double kContFirstStep = 0.25;
constexpr double kContGrowth = 1.6;
constexpr std::size_t kContStageIters = 14;
constexpr double kGmin = 1e-12;   // conductance from every node to ground
constexpr double kGShort = 1e3;   // shorts inductor windings in the DC solve
constexpr double kPi = 3.14159265358979323846;

// SPICE-style junction limiting for a grid-cathode voltage (see circuit.rs).
inline double limitGrid(double vnew, double vold) {
    constexpr double onset = 0.4, scale = 0.1;
    if (vnew > onset && vnew - vold > 2.0 * scale) {
        if (vold > 0.0) return vold + scale * std::log(1.0 + (vnew - vold) / scale);
        return scale * std::log(vnew / scale);
    }
    return vnew;
}

void stamp(std::vector<double>& m, std::size_t n, std::size_t a, std::size_t b, double g) {
    constexpr std::size_t none = static_cast<std::size_t>(-1);
    if (a != none) m[a * n + a] += g;
    if (b != none) m[b * n + b] += g;
    if (a != none && b != none) {
        m[a * n + b] -= g;
        m[b * n + a] -= g;
    }
}

}  // namespace

// ---------------------------------------------------------------- Circuit

Node Circuit::node(const std::string& name) {
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == name) return i;
    names_.push_back(name);
    return names_.size() - 1;
}

ResId Circuit::resistor(Node a, Node b, double ohms) {
    res_.push_back({a, b, ohms});
    return {res_.size() - 1};
}

void Circuit::capacitor(Node a, Node b, double farads) { caps_.push_back({a, b, farads}); }

void Circuit::triode(Node g, Node p, Node k, const TriodeModel& model, double scale) {
    capacitor(g, k, model.cgk * scale);
    capacitor(g, p, model.cgp * scale);
    capacitor(p, k, model.cpk * scale);
    Dev d{Kind::Triode};
    d.a = g; d.b = p; d.c = k; d.triode = model; d.scale = scale;
    devices_.push_back(d);
}

void Circuit::pentode(Node g1, Node p, Node g2, Node k, const PentodeModel& model, double scale) {
    capacitor(g1, k, model.cg1k * scale);
    capacitor(g1, p, model.cg1p * scale);
    capacitor(p, k, model.cpk * scale);
    Dev d{Kind::Pentode};
    d.a = g1; d.b = p; d.c = g2; d.d = k; d.pentode = model; d.scale = scale;
    devices_.push_back(d);
}

void Circuit::diode(Node anode, Node cathode, const DiodeModel& model) {
    Dev d{Kind::Diode};
    d.a = anode; d.b = cathode; d.diode = model;
    devices_.push_back(d);
}

void Circuit::bjt(Node b, Node c, Node e, const BjtModel& model) {
    Dev d{Kind::Bjt};
    d.a = b; d.b = c; d.c = e; d.bjt = model;
    devices_.push_back(d);
}

void Circuit::vccs(Node into, Node ctrlPlus, Node ctrlMinus, double gm) {
    vccs_.push_back({into, ctrlPlus, ctrlMinus, gm});
}

void Circuit::opamp(Node plus, Node minus, Node out, Node vref, const OpampModel& model) {
    const Node internal = node("opamp_internal_" + std::to_string(names_.size()));
    vccs_.push_back({internal, plus, minus, model.gm});
    resistor(internal, vref, model.a0 / model.gm);
    capacitor(internal, vref, model.gm / (2.0 * kPi * model.gbw));
    resistor(out, vref, model.rout);
    Dev d{Kind::OpampOut};
    d.a = internal; d.b = vref; d.c = out; d.opamp = model;
    devices_.push_back(d);
}

SrcId Circuit::source(Node node, double ohms, double dc) {
    sources_.push_back({node, ohms, dc});
    return {sources_.size() - 1};
}

SrcId Circuit::input(Node node, double ohms) {
    const SrcId id = source(node, ohms, 0.0);
    hasInput_ = true;
    input_ = id;
    return id;
}

void Circuit::coupledInductors(const std::vector<std::pair<Node, Node>>& windings, const std::vector<double>& l, double satFlux,
                               double satExp, const std::vector<std::size_t>& satWindings) {
    if (l.size() != windings.size() * windings.size()) throw std::invalid_argument("inductance matrix size");
    for (auto k : satWindings)
        if (k >= windings.size()) throw std::invalid_argument("saturating winding index");
    CoupledDef d{windings, l};
    d.satFlux = satFlux;
    d.satExp = satExp;
    d.satWindings = satWindings;
    coupled_.push_back(std::move(d));
}

// ---------------------------------------------------------------- Solver

Solver::~Solver() = default;
Solver::Solver(Solver&&) noexcept = default;
Solver& Solver::operator=(Solver&&) noexcept = default;

Solver::Solver(Circuit circuit, double fs, bool useTables)
    : n_(circuit.nodeCount()), p_(0), fs_(fs), fast_(useTables), inverter_(circuit.nodeCount()) {
    const std::size_t n = n_;
    const double t = 1.0 / fs;

    std::size_t derLen = 0;
    for (const auto& d : circuit.devices_) {
        struct Port { Node a, b; std::vector<std::pair<Node, double>> injection; };
        auto leg = [](Node a, Node b) { return Port{a, b, {{a, 1.0}, {b, -1.0}}}; };
        std::vector<Port> ports;
        std::size_t np = 0;
        switch (d.kind) {
            case Circuit::Kind::Triode:
                ports = {leg(d.a, d.c), leg(d.b, d.c)}; np = 2;
                gridPort_.insert(gridPort_.end(), {1, 0});
                gridUpLimit_.insert(gridUpLimit_.end(), {1.0, std::numeric_limits<double>::infinity()});
                break;
            case Circuit::Kind::Pentode:
                ports = {leg(d.a, d.d), leg(d.b, d.d), leg(d.c, d.d)}; np = 3;
                gridPort_.insert(gridPort_.end(), {1, 0, 0});
                gridUpLimit_.insert(gridUpLimit_.end(),
                                    {8.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()});
                break;
            case Circuit::Kind::Diode:
                ports = {leg(d.a, d.b)}; np = 1;
                gridPort_.push_back(0);
                gridUpLimit_.push_back(std::numeric_limits<double>::infinity());
                break;
            case Circuit::Kind::Bjt:
                ports = {Port{d.a, d.c, {{d.a, 1.0}, {d.b, d.bjt.beta}, {d.c, -(1.0 + d.bjt.beta)}}}}; np = 1;
                gridPort_.push_back(0);
                gridUpLimit_.push_back(std::numeric_limits<double>::infinity());
                break;
            case Circuit::Kind::OpampOut:
                ports = {Port{d.a, d.b, {{d.c, -1.0}}}}; np = 1;
                gridPort_.push_back(0);
                gridUpLimit_.push_back(std::numeric_limits<double>::infinity());
                break;
        }
        for (std::size_t m = 0; m < np; ++m)
            for (std::size_t c = 0; c < np; ++c) {
                // A triode's grid current does not depend on its plate voltage.
                const bool structuralZero = d.kind == Circuit::Kind::Triode && m == 0 && c == 1;
                if (!structuralZero) dPattern_.push_back({brA_.size() + m, brA_.size() + c, derLen + m * np + c});
            }
        const TriodeTable* table = nullptr;
        if (useTables && d.kind == Circuit::Kind::Triode) {
            for (const auto& tb : tables_)
                if (std::memcmp(&tb->model(), &d.triode, sizeof(TriodeModel)) == 0) table = tb.get();
            if (!table) {
                tables_.push_back(std::make_unique<TriodeTable>(d.triode));
                table = tables_.back().get();
            }
        }
        devs_.push_back({d, brA_.size(), np, derLen, table});
        derLen += np * np;
        for (const auto& port : ports) {
            brA_.push_back(gx(port.a));
            brB_.push_back(gx(port.b));
            std::vector<Inj> list;
            for (const auto& [node, coeff] : port.injection)
                if (node != GND) list.push_back({gx(node), coeff});
            inj_.push_back(std::move(list));
        }
    }
    p_ = brA_.size();
    const std::size_t p = p_;

    for (const auto& c : circuit.coupled_) {
        const std::size_t w = c.windings.size();
        Inverter inv(w);
        const double* gamma = inv.invert(c.l.data());
        if (!gamma) throw std::runtime_error("inductance matrix is singular");
        Coupled cp;
        for (const auto& wd : c.windings) cp.ends.push_back({gx(wd.first), gx(wd.second)});
        for (std::size_t i = 0; i < w * w; ++i) cp.gl.push_back(gamma[i] * t / 2.0);
        cp.vPrev.assign(w, 0.0);
        cp.iPrev.assign(w, 0.0);
        cp.h.assign(w, 0.0);
        cp.l = c.l;
        if (c.satFlux > 0.0 && !c.satWindings.empty()) {
            // The saturating windings are in series: the branch goes from the first one's start to the last one's end.
            double ltot = 0.0;
            for (auto a : c.satWindings)
                for (auto b : c.satWindings) ltot += c.l[a * w + b];
            cp.satFlux = c.satFlux;
            cp.satExp = c.satExp;
            cp.satInvL = 1.0 / ltot;
            cp.satWindings = c.satWindings;
            cp.satA = cp.ends[c.satWindings.front()].first;
            cp.satB = cp.ends[c.satWindings.back()].second;
        }
        coupled_.push_back(std::move(cp));
    }

    for (const auto& s : circuit.sources_) sources_.push_back({gx(s.node), 1.0 / s.ohms, s.dc});
    for (const auto& s : sources_) srcVal_.push_back(s.dc);
    hasInput_ = circuit.hasInput_;
    inputSrc_ = circuit.input_.i;

    res_ = circuit.res_;
    caps_ = circuit.caps_;
    for (const auto& c : caps_) {
        capG_.push_back(2.0 * c.farads * fs);
        capIx_.push_back({gx(c.a), gx(c.b)});
    }
    capI_.assign(caps_.size(), 0.0);
    capV_.assign(caps_.size(), 0.0);
    for (const auto& v : circuit.vccs_) vccs_.push_back({gx(v.into), gx(v.ctrlPlus), gx(v.ctrlMinus), v.gm});

    m_.assign(n * n, 0.0);
    s_.assign(n * n, 0.0);
    ks_.assign(n * p, 0.0);
    k_.assign(p * p, 0.0);
    x_.assign(p, 0.0);
    v_.assign(n + 1, 0.0);
    r_.assign(n + 1, 0.0);
    sr_.assign(n + 1, 0.0);
    pvec_.assign(p, 0.0);
    ivec_.assign(p, 0.0);
    der_.assign(derLen, 0.0);
    f_.assign(p, 0.0);
    lu_.assign(p * p, 0.0);
    piv_.assign(p, 0);
    dx_.assign(p, 0.0);
    xt_.assign(p, 0.0);
    xGood_.assign(p, 0.0);
    pGood_.assign(p, 0.0);
    pTarget_.assign(p, 0.0);
    xSave_.assign(p, 0.0);

    dcOperatingPoint();
}

void Solver::setResistor(ResId id, double ohms) {
    if (res_[id.i].ohms != ohms) {
        res_[id.i].ohms = ohms;
        dirty_ = true;
    }
}

void Solver::refresh() {
    if (dirty_) {
        build(false);
        dirty_ = false;
    }
}

void Solver::build(bool dc) {
    const std::size_t n = n_, p = p_;
    std::fill(m_.begin(), m_.end(), 0.0);
    for (const auto& r : res_) stamp(m_, n, ix(r.a), ix(r.b), 1.0 / r.ohms);
    if (!dc)
        for (std::size_t c = 0; c < caps_.size(); ++c) stamp(m_, n, ix(caps_[c].a), ix(caps_[c].b), capG_[c]);
    auto ng = [n](std::size_t i) { return i == n ? NONE : i; };
    for (const auto& set : coupled_) {
        const std::size_t w = set.ends.size();
        if (dc) {
            for (const auto& e : set.ends) stamp(m_, n, ng(e.first), ng(e.second), kGShort);
        } else {
            for (std::size_t i = 0; i < w; ++i)
                for (std::size_t j = 0; j < w; ++j) {
                    const double g = set.gl[i * w + j];
                    const auto ai = ng(set.ends[i].first), bi = ng(set.ends[i].second);
                    const auto aj = ng(set.ends[j].first), bj = ng(set.ends[j].second);
                    if (ai != NONE && aj != NONE) m_[ai * n + aj] += g;
                    if (ai != NONE && bj != NONE) m_[ai * n + bj] -= g;
                    if (bi != NONE && aj != NONE) m_[bi * n + aj] -= g;
                    if (bi != NONE && bj != NONE) m_[bi * n + bj] += g;
                }
        }
    }
    for (const auto& v : vccs_) {
        if (v.into != n) {
            if (v.cp != n) m_[v.into * n + v.cp] -= v.gm;
            if (v.cn != n) m_[v.into * n + v.cn] += v.gm;
        }
    }
    for (std::size_t i = 0; i < n; ++i) m_[i * n + i] += kGmin;
    for (const auto& s : sources_) stamp(m_, n, ng(s.node), NONE, s.g);

    const double* inv = inverter_.invert(m_.data());
    if (!inv) throw std::runtime_error("circuit matrix is singular");
    std::copy(inv, inv + n * n, s_.begin());
    luValid_ = false;
    haveGood_ = false;

    for (std::size_t j = 0; j < p; ++j)
        for (std::size_t row = 0; row < n; ++row) {
            double v = 0.0;
            for (const auto& in : inj_[j]) v += in.coeff * s_[row * n + in.node];
            ks_[row * p + j] = v;
        }
    for (std::size_t i = 0; i < p; ++i) {
        const std::size_t a = brA_[i], b = brB_[i];
        for (std::size_t j = 0; j < p; ++j) {
            double v = 0.0;
            if (a != n) v += ks_[a * p + j];
            if (b != n) v -= ks_[b * p + j];
            k_[i * p + j] = v;
        }
    }
}

double Solver::residualCached() {
    const std::size_t p = p_;
    if (p == 0) { fmax_ = 0.0; return 0.0; }
    double worst = 0.0, merit = 0.0;
    for (std::size_t r = 0; r < p; ++r) {
        double v = x_[r] - pvec_[r];
        const double* krow = &k_[r * p];
        for (std::size_t c = 0; c < p; ++c) v += krow[c] * ivec_[c];
        f_[r] = v;
        worst = std::max(worst, std::abs(v));
        merit += v * v;
    }
    fmax_ = worst;
    return merit;
}

double Solver::eval(bool useTrial) {
    SAS_PROF(ProfEval);
    const std::size_t p = p_;
    if (p == 0) { fmax_ = 0.0; return 0.0; }
    const std::vector<double>& xs = useTrial ? xt_ : x_;
    for (const auto& d : devs_) {
        const std::size_t b = d.base;
        switch (d.dev.kind) {
            case Circuit::Kind::Triode: {
                const double vgk = xs[b], vpk = xs[b + 1];
                const auto g = d.table ? d.table->grid(vgk) : d.dev.triode.grid(vgk);
                const auto pl = d.table ? d.table->plate(vgk, vpk) : d.dev.triode.plate(vgk, vpk);
                const double sc = d.dev.scale;
                ivec_[b] = g.ig * sc;
                ivec_[b + 1] = pl.ip * sc;
                der_[d.derOff] = g.dvgk * sc;
                der_[d.derOff + 1] = 0.0;
                der_[d.derOff + 2] = pl.dvgk * sc;
                der_[d.derOff + 3] = pl.dvpk * sc;
                break;
            }
            case Circuit::Kind::Pentode: {
                const auto e = d.dev.pentode.eval(xs[b], xs[b + 1], xs[b + 2]);
                const double sc = d.dev.scale;
                for (std::size_t m = 0; m < 3; ++m) {
                    ivec_[b + m] = e.cur[m] * sc;
                    for (std::size_t c = 0; c < 3; ++c) der_[d.derOff + m * 3 + c] = e.d[m][c] * sc;
                }
                break;
            }
            case Circuit::Kind::Diode: {
                const auto e = d.dev.diode.eval(xs[b]);
                ivec_[b] = e.i;
                der_[d.derOff] = e.g;
                break;
            }
            case Circuit::Kind::Bjt: {
                const auto e = d.dev.bjt.baseCurrent(xs[b]);
                ivec_[b] = e.ib;
                der_[d.derOff] = e.g;
                break;
            }
            case Circuit::Kind::OpampOut: {
                const auto e = d.dev.opamp.stage(xs[b]);
                ivec_[b] = e.i;
                der_[d.derOff] = e.g;
                break;
            }
        }
    }
    double worst = 0.0, merit = 0.0;
    for (std::size_t r = 0; r < p; ++r) {
        double v = xs[r] - pvec_[r];
        const double* krow = &k_[r * p];
        for (std::size_t c = 0; c < p; ++c) v += krow[c] * ivec_[c];
        f_[r] = v;
        worst = std::max(worst, std::abs(v));
        merit += v * v;
    }
    fmax_ = worst;
    return merit;
}

bool Solver::factorJacobian() {
    SAS_PROF(ProfFactor);
    const std::size_t p = p_;
    ++jacobianFactorizations;
    auto buildJacobian = [&] {
        std::fill(lu_.begin(), lu_.end(), 0.0);
        for (std::size_t r = 0; r < p; ++r) {
            double* jrow = &lu_[r * p];
            const double* krow = &k_[r * p];
            jrow[r] = 1.0;
            for (const auto& pt : dPattern_) jrow[pt.col] += krow[pt.row] * der_[pt.di];
        }
    };
    buildJacobian();
    // Fast modes try the pivot-free factorisation first and fall back to partial pivoting.
    if (fast_ && luFactorNoPivotFast(lu_.data(), piv_.data(), p)) {
        luValid_ = true;
        return true;
    }
    if (fast_) buildJacobian();
    luValid_ = luFactorFast(lu_.data(), piv_.data(), p);
    return luValid_;
}

bool Solver::newtonDirection() {
    SAS_PROF(ProfSolve);
    if (!luValid_ && !factorJacobian()) return false;
    for (std::size_t i = 0; i < p_; ++i) dx_[i] = -f_[i];
    luSolveFast(lu_.data(), piv_.data(), dx_.data(), p_);
    return true;
}

bool Solver::newton(std::size_t maxIter) {
    const std::size_t p = p_;
    if (p == 0) return true;
    const double tol = tolerance;
    luValid_ = false;
    double merit = residualCached();
    for (std::size_t it = 0; it < maxIter; ++it) {
        if (fmax_ < tol) return true;
        ++newtonIterations;
        if (!newtonDirection()) return false;
        double lambda = 1.0;
        for (std::size_t r = 0; r < p; ++r) {
            if (gridPort_[r] && dx_[r] > 0.0) {
                const double limited = limitGrid(x_[r] + dx_[r], x_[r]);
                lambda = std::min(lambda, std::max((limited - x_[r]) / dx_[r], 1e-3));
                lambda = std::min(lambda, std::max(gridUpLimit_[r] / dx_[r], 1e-3));
            }
        }
        lambda = std::min(lambda, lambdaHint_);

        bool accepted = false, firstTrial = true;
        for (int tries = 0; tries < 12; ++tries) {
            for (std::size_t r = 0; r < p; ++r) xt_[r] = x_[r] + lambda * dx_[r];
            const double tm = eval(true);
            if (tm < merit || lambda < 1e-3) {
                if (lambda < 1.0 || tm > 0.0625 * merit) luValid_ = false;
                lambdaHint_ = firstTrial ? std::min(lambda * 2.0, 1.0) : lambda;
                x_ = xt_;
                merit = tm;
                accepted = true;
                break;
            }
            firstTrial = false;
            lambda *= 0.5;
        }
        if (!accepted) {
            luValid_ = false;
            eval(false);
            return false;
        }
    }
    return fmax_ < tol * 100.0;
}

bool Solver::continuation() {
    if (!haveGood_ || p_ == 0) return false;
    const std::size_t p = p_;
    ++continuationRuns;
    pTarget_ = pvec_;
    x_ = xGood_;
    eval(false);
    double tau = 0.0, dtau = kContFirstStep;
    int budget = 40;  // bounded so one hard sample cannot stall an audio callback
    while (tau < 1.0 && budget > 0) {
        --budget;
        const double t = std::min(tau + dtau, 1.0);
        for (std::size_t j = 0; j < p; ++j) pvec_[j] = pGood_[j] + t * (pTarget_[j] - pGood_[j]);
        xSave_ = x_;
        if (newton(kContStageIters)) {
            tau = t;
            dtau = std::min(dtau * kContGrowth, 1.0);
        } else {
            x_ = xSave_;
            eval(false);
            dtau *= 0.5;
            if (dtau < 1e-3) break;
        }
    }
    pvec_ = pTarget_;
    if (tau >= 1.0) return true;
    residualCached();
    return false;
}

void Solver::dcOperatingPoint() {
    const std::size_t n = n_;
    build(true);
    constexpr int steps = 80;
    bool ok = true;
    for (int step = 1; step <= steps; ++step) {
        const double alpha = double(step) / steps;
        std::fill(r_.begin(), r_.end(), 0.0);
        for (const auto& s : sources_) r_[s.node] += alpha * s.dc * s.g;
        computeSrAndP();
        eval(false);
        ok = newton(80);
    }
    dcConverged = ok;
    reconstructVoltages();

    for (std::size_t c = 0; c < capIx_.size(); ++c) {
        capV_[c] = v_[capIx_[c].first] - v_[capIx_[c].second];
        capI_[c] = 0.0;
    }
    for (auto& set : coupled_)
        for (std::size_t i = 0; i < set.ends.size(); ++i) {
            const double dv = v_[set.ends[i].first] - v_[set.ends[i].second];
            set.vPrev[i] = dv;
            set.iPrev[i] = kGShort * dv;
        }
    for (std::size_t i = 0; i < sources_.size(); ++i) srcVal_[i] = sources_[i].dc;
    (void)n;
    build(false);
}

void Solver::computeSrAndP() {
    SAS_PROF(ProfMatvec);
    const std::size_t n = n_;
    r_[n] = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double acc = 0.0;
        const double* srow = &s_[i * n];
        for (std::size_t j = 0; j < n; ++j) acc += srow[j] * r_[j];
        sr_[i] = acc;
    }
    sr_[n] = 0.0;
    for (std::size_t i = 0; i < p_; ++i) pvec_[i] = sr_[brA_[i]] - sr_[brB_[i]];
}

void Solver::reconstructVoltages() {
    SAS_PROF(ProfReconstruct);
    const std::size_t p = p_, n = n_;
    if (p == 0) {
        std::copy(sr_.begin(), sr_.begin() + n, v_.begin());
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        double acc = sr_[i];
        const double* ksrow = &ks_[i * p];
        for (std::size_t j = 0; j < p; ++j) acc -= ksrow[j] * ivec_[j];
        v_[i] = acc;
    }
}

void Solver::step() {
    SAS_PROF(ProfStep);
    std::fill(r_.begin(), r_.end(), 0.0);
    for (std::size_t i = 0; i < sources_.size(); ++i) r_[sources_[i].node] += srcVal_[i] * sources_[i].g;
    for (std::size_t c = 0; c < capIx_.size(); ++c) {
        const double hist = capG_[c] * capV_[c] + capI_[c];
        r_[capIx_[c].first] += hist;
        r_[capIx_[c].second] -= hist;
    }
    for (auto& set : coupled_) {
        const std::size_t w = set.ends.size();
        for (std::size_t i = 0; i < w; ++i) {
            double h = set.iPrev[i];
            for (std::size_t j = 0; j < w; ++j) h += set.gl[i * w + j] * set.vPrev[j];
            set.h[i] = h;
            r_[set.ends[i].first] -= h;
            r_[set.ends[i].second] += h;
        }
        if (set.satFlux > 0.0) {
            // Core saturation: the flux linkage of the previous sample sets an extra magnetising current, kept to a few
            // multiples of the knee so the explicit update stays well below the circuit's time constants.
            double lambda = 0.0;
            for (auto a : set.satWindings)
                for (std::size_t j = 0; j < w; ++j) lambda += set.l[a * w + j] * set.iPrev[j];
            const double x = std::min(std::abs(lambda) / set.satFlux, 2.5);
            const double isat = lambda * set.satInvL * std::pow(x, set.satExp);
            r_[set.satA] -= isat;
            r_[set.satB] += isat;
        }
    }
    computeSrAndP();
    const bool plainOk = newton(plainIterations);
    const bool solved = plainOk || continuation() || newton(30);
    if (solved) {
        xGood_ = x_;
        pGood_ = pvec_;
        haveGood_ = true;
    } else {
        if (fmax_ > 1.0) ++newtonFailures;
        else ++newtonSoftFailures;
        worstFailedResidual = std::max(worstFailedResidual, fmax_);
    }
    reconstructVoltages();

    for (std::size_t c = 0; c < capIx_.size(); ++c) {
        const double hist = capG_[c] * capV_[c] + capI_[c];
        const double vc = v_[capIx_[c].first] - v_[capIx_[c].second];
        capI_[c] = capG_[c] * vc - hist;
        capV_[c] = vc;
    }
    for (auto& set : coupled_) {
        const std::size_t w = set.ends.size();
        double vw[8] = {};
        for (std::size_t i = 0; i < w; ++i) vw[i] = v_[set.ends[i].first] - v_[set.ends[i].second];
        for (std::size_t i = 0; i < w; ++i) {
            double cur = set.h[i];
            for (std::size_t j = 0; j < w; ++j) cur += set.gl[i * w + j] * vw[j];
            set.iPrev[i] = cur;
        }
        for (std::size_t i = 0; i < w; ++i) set.vPrev[i] = vw[i];
    }
}

}  // namespace sas
