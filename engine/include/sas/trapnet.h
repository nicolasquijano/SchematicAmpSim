#pragma once
// A small linear network of resistors and capacitors, integrated with the trapezoidal rule (what the nodal solver does) and solved with a matrix that is inverted once, when
// the network is built or a pot moves. One node can be driven by a Norton source (a voltage behind a resistance: an op-amp's output stage). The pedals' hand-made models
// (sas/bd2_fast.h) are built from a few of these with a Newton equation for each nonlinear thing that touches them (an op-amp's saturation, a diode pair).
//
//   per sample:  history()      works out what the capacitors remember (y0 = Minv * history)
//                node(i, e)     voltage of node i for the drive voltage e: q(i) * e + y0(i)
//                commit(e, ...) takes the nodes' voltages (a current taken out of one node by a diode, if there is one) and updates the capacitors
// Ground is node -1. The nodes are deviations from their DC values (the capacitors start uncharged), so DC biasing is not part of it.

#include <cmath>
#include <utility>
#include <vector>

namespace sas {

class TrapNet {
public:
    // Starts a new list of elements. The capacitors' states of the list that was there are kept for the new one when it has the same capacitors in the same order
    // (a pot moved: the network is built again with other resistors).
    void init(int nodes, double fs) {
        old_.clear();
        for (const auto& c : cs_) old_.push_back({c.v, c.i});
        n_ = nodes;
        fs_ = fs;
        rs_.clear();
        cs_.clear();
        drive_ = -1;
        gd_ = 0.0;
    }
    void addR(int a, int b, double ohms) { rs_.push_back({a, b, 1.0 / ohms, false}); }
    // A resistor from `a` to the voltage of `ctrl` that does not load `ctrl` (a follower drives it).
    void addDependentR(int a, int ctrl, double ohms) { rs_.push_back({a, ctrl, 1.0 / ohms, true}); }
    void addC(int a, int b, double farads) { cs_.push_back({a, b, 2.0 * farads * fs_, 0.0, 0.0}); }
    void setDrive(int node, double rout) {
        drive_ = node;
        gd_ = 1.0 / rout;
    }

    // Builds the matrix and inverts it (Gauss-Jordan). Keeps the capacitors' states.
    void finish() {
        const int n = n_;
        std::vector<double> m(std::size_t(n) * std::size_t(2 * n), 0.0);
        auto at = [&](int r, int c) -> double& { return m[std::size_t(r) * std::size_t(2 * n) + std::size_t(c)]; };
        auto stamp = [&](int a, int b, double g, bool dep) {
            if (a >= 0) at(a, a) += g;
            if (a >= 0 && b >= 0) at(a, b) -= g;
            if (!dep) {
                if (b >= 0) at(b, b) += g;
                if (a >= 0 && b >= 0) at(b, a) -= g;
            }
        };
        for (const auto& r : rs_) stamp(r.a, r.b, r.g, r.dependent);
        for (const auto& c : cs_) stamp(c.a, c.b, c.g, false);
        if (drive_ >= 0) at(drive_, drive_) += gd_;
        for (int i = 0; i < n; ++i) at(i, n + i) = 1.0;
        for (int c = 0; c < n; ++c) {
            int piv = c;
            for (int r = c + 1; r < n; ++r)
                if (std::abs(at(r, c)) > std::abs(at(piv, c))) piv = r;
            if (piv != c)
                for (int k = 0; k < 2 * n; ++k) std::swap(at(c, k), at(piv, k));
            const double d = at(c, c);
            for (int k = 0; k < 2 * n; ++k) at(c, k) /= d;
            for (int r = 0; r < n; ++r)
                if (r != c) {
                    const double f = at(r, c);
                    if (f != 0.0)
                        for (int k = 0; k < 2 * n; ++k) at(r, k) -= f * at(c, k);
                }
        }
        minv_.assign(std::size_t(n) * std::size_t(n), 0.0);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) minv_[std::size_t(i) * std::size_t(n) + std::size_t(j)] = at(i, n + j);
        y0_.assign(std::size_t(n), 0.0);
        y_.assign(std::size_t(n), 0.0);
        h_.assign(std::size_t(n), 0.0);
        if (old_.size() == cs_.size())
            for (std::size_t k = 0; k < cs_.size(); ++k) {
                cs_[k].v = old_[k].first;
                cs_[k].i = old_[k].second;
            }
    }

    // What the capacitors remember, as the voltages it gives the nodes with no drive: y0.
    void history() {
        std::fill(h_.begin(), h_.end(), 0.0);
        for (const auto& c : cs_) {
            const double hh = c.g * c.v + c.i;
            if (c.a >= 0) h_[std::size_t(c.a)] += hh;
            if (c.b >= 0) h_[std::size_t(c.b)] -= hh;
        }
        for (int i = 0; i < n_; ++i) {
            double s = 0.0;
            for (int j = 0; j < n_; ++j) s += minv_[std::size_t(i) * std::size_t(n_) + std::size_t(j)] * h_[std::size_t(j)];
            y0_[std::size_t(i)] = s;
        }
    }
    double y0(int i) const { return y0_[std::size_t(i)]; }
    // The response of node i to the drive voltage, and the node's voltage for a given drive.
    double q(int i) const { return minv_[std::size_t(i) * std::size_t(n_) + std::size_t(drive_)] * gd_; }
    double node(int i, double e) const { return q(i) * e + y0_[std::size_t(i)]; }
    // The network's impedance seen from node i (the response of node i to a current injected there).
    double rth(int i) const { return minv_[std::size_t(i) * std::size_t(n_) + std::size_t(i)]; }
    double minv(int i, int j) const { return minv_[std::size_t(i) * std::size_t(n_) + std::size_t(j)]; }

    // Settles the sample: the drive `e` and, if given, a current `takenAmps` that something nonlinear takes out of node `takenFrom`. Returns the voltage of the nodes in node().
    void commit(double e, int takenFrom = -1, double takenAmps = 0.0) {
        for (int i = 0; i < n_; ++i) {
            double v = q(i) * e + y0_[std::size_t(i)];
            if (takenFrom >= 0) v -= minv(i, takenFrom) * takenAmps;
            y_[std::size_t(i)] = v;
        }
        for (auto& c : cs_) {
            const double va = c.a >= 0 ? y_[std::size_t(c.a)] : 0.0, vb = c.b >= 0 ? y_[std::size_t(c.b)] : 0.0;
            const double v = va - vb;
            c.i = c.g * v - (c.g * c.v + c.i);
            c.v = v;
        }
    }
    double voltage(int i) const { return y_[std::size_t(i)]; }
    int nodes() const { return n_; }

private:
    struct Res {
        int a, b;
        double g;
        bool dependent;
    };
    struct Cap {
        int a, b;
        double g, v, i;
    };
    int n_ = 0, drive_ = -1;
    double fs_ = 48000.0, gd_ = 0.0;
    std::vector<Res> rs_;
    std::vector<Cap> cs_;
    std::vector<double> minv_, y0_, y_, h_;
    std::vector<std::pair<double, double>> old_;
};

}  // namespace sas
