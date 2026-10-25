#pragma once
// Small dense linear algebra (row-major, double). Port of jcm800/src/linalg.rs.

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace sas {

// Gauss-Jordan inverter with preallocated scratch (no allocation after construction).
class Inverter {
public:
    explicit Inverter(std::size_t n) : n_(n), a_(n * n), inv_(n * n) {}

    // Inverts m (n x n). Returns nullptr if singular; otherwise the internal inverse buffer.
    const double* invert(const double* m) {
        const std::size_t n = n_;
        std::copy(m, m + n * n, a_.begin());
        std::fill(inv_.begin(), inv_.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) inv_[i * n + i] = 1.0;
        for (std::size_t col = 0; col < n; ++col) {
            std::size_t piv = col;
            double best = std::abs(a_[col * n + col]);
            for (std::size_t r = col + 1; r < n; ++r) {
                const double v = std::abs(a_[r * n + col]);
                if (v > best) { best = v; piv = r; }
            }
            if (best < 1e-300) return nullptr;
            if (piv != col) {
                for (std::size_t c = 0; c < n; ++c) {
                    std::swap(a_[piv * n + c], a_[col * n + c]);
                    std::swap(inv_[piv * n + c], inv_[col * n + c]);
                }
            }
            const double d = 1.0 / a_[col * n + col];
            for (std::size_t c = 0; c < n; ++c) {
                a_[col * n + c] *= d;
                inv_[col * n + c] *= d;
            }
            for (std::size_t r = 0; r < n; ++r) {
                if (r == col) continue;
                const double f = a_[r * n + col];
                if (f == 0.0) continue;
                for (std::size_t c = 0; c < n; ++c) {
                    a_[r * n + c] -= f * a_[col * n + c];
                    inv_[r * n + c] -= f * inv_[col * n + c];
                }
            }
        }
        return inv_.data();
    }

private:
    std::size_t n_;
    std::vector<double> a_, inv_;
};

// Solves a x = b in place (partial pivoting); a is destroyed, b becomes x. False if singular.
inline bool solveInPlace(double* a, double* b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t piv = col;
        double best = std::abs(a[col * n + col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double v = std::abs(a[r * n + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-300) return false;
        if (piv != col) {
            for (std::size_t c = 0; c < n; ++c) std::swap(a[piv * n + c], a[col * n + c]);
            std::swap(b[piv], b[col]);
        }
        const double inv = 1.0 / a[col * n + col];
        const double bc = b[col];
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r * n + col] * inv;
            if (f != 0.0) {
                for (std::size_t c = col; c < n; ++c) a[r * n + c] -= f * a[col * n + c];
                b[r] -= f * bc;
            }
        }
    }
    for (std::size_t r = n; r-- > 0;) {
        double sum = b[r];
        for (std::size_t c = r + 1; c < n; ++c) sum -= a[r * n + c] * b[c];
        b[r] = sum / a[r * n + r];
    }
    return true;
}

// In-place LU with partial pivoting (unit-lower L below the diagonal). piv[col] = row swapped
// into col. False if singular.
inline bool luFactor(double* a, std::size_t* piv, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t p = col;
        double best = std::abs(a[col * n + col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double v = std::abs(a[r * n + col]);
            if (v > best) { best = v; p = r; }
        }
        if (best < 1e-300) return false;
        piv[col] = p;
        if (p != col)
            for (std::size_t c = 0; c < n; ++c) std::swap(a[p * n + c], a[col * n + c]);
        const double inv = 1.0 / a[col * n + col];
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r * n + col] * inv;
            a[r * n + col] = f;
            if (f != 0.0)
                for (std::size_t c = col + 1; c < n; ++c) a[r * n + c] -= f * a[col * n + c];
        }
    }
    return true;
}

inline void luSolve(const double* lu, const std::size_t* piv, double* b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) std::swap(b[col], b[piv[col]]);
    for (std::size_t r = 1; r < n; ++r) {
        double sum = b[r];
        for (std::size_t c = 0; c < r; ++c) sum -= lu[r * n + c] * b[c];
        b[r] = sum;
    }
    for (std::size_t r = n; r-- > 0;) {
        double sum = b[r];
        for (std::size_t c = r + 1; c < n; ++c) sum -= lu[r * n + c] * b[c];
        b[r] = sum / lu[r * n + r];
    }
}

// Size known at compile time, so the loops unroll.
template <std::size_t N>
inline bool luFactorFixed(double* a, std::size_t* piv) {
    for (std::size_t col = 0; col < N; ++col) {
        std::size_t p = col;
        double best = std::abs(a[col * N + col]);
        for (std::size_t r = col + 1; r < N; ++r) {
            const double v = std::abs(a[r * N + col]);
            if (v > best) { best = v; p = r; }
        }
        if (best < 1e-300) return false;
        piv[col] = p;
        if (p != col)
            for (std::size_t c = 0; c < N; ++c) std::swap(a[p * N + c], a[col * N + c]);
        const double inv = 1.0 / a[col * N + col];
        for (std::size_t r = col + 1; r < N; ++r) {
            const double f = a[r * N + col] * inv;
            a[r * N + col] = f;
            for (std::size_t c = col + 1; c < N; ++c) a[r * N + c] -= f * a[col * N + c];
        }
    }
    return true;
}

template <std::size_t N>
inline void luSolveFixed(const double* lu, const std::size_t* piv, double* b) {
    for (std::size_t col = 0; col < N; ++col) std::swap(b[col], b[piv[col]]);
    for (std::size_t r = 1; r < N; ++r) {
        double sum = b[r];
        for (std::size_t c = 0; c < r; ++c) sum -= lu[r * N + c] * b[c];
        b[r] = sum;
    }
    for (std::size_t r = N; r-- > 0;) {
        double sum = b[r];
        for (std::size_t c = r + 1; c < N; ++c) sum -= lu[r * N + c] * b[c];
        b[r] = sum / lu[r * N + r];
    }
}

// LU without row exchanges, for matrices that are diagonally dominant in practice (the Newton
// Jacobian I + K D of these circuits). Returns false if a pivot is tiny or a multiplier exceeds
// `maxMultiplier` (growth), in which case the caller factors again with partial pivoting. The
// pivot search is the branchy part of luFactor, so this is noticeably faster. piv[i] = i, so the
// same luSolve works.
template <std::size_t N>
inline bool luFactorFixedNoPivot(double* a, std::size_t* piv, double maxMultiplier) {
    for (std::size_t col = 0; col < N; ++col) {
        const double pv = a[col * N + col];
        if (!(std::abs(pv) > 1e-9)) return false;
        piv[col] = col;
        const double inv = 1.0 / pv;
        for (std::size_t r = col + 1; r < N; ++r) {
            const double f = a[r * N + col] * inv;
            if (!(std::abs(f) <= maxMultiplier)) return false;
            a[r * N + col] = f;
            for (std::size_t c = col + 1; c < N; ++c) a[r * N + c] -= f * a[col * N + c];
        }
    }
    return true;
}

inline bool luFactorNoPivotFast(double* a, std::size_t* piv, std::size_t n, double maxMultiplier = 50.0) {
    switch (n) {
        case 6: return luFactorFixedNoPivot<6>(a, piv, maxMultiplier);
        case 8: return luFactorFixedNoPivot<8>(a, piv, maxMultiplier);
        case 10: return luFactorFixedNoPivot<10>(a, piv, maxMultiplier);
        case 12: return luFactorFixedNoPivot<12>(a, piv, maxMultiplier);
        case 14: return luFactorFixedNoPivot<14>(a, piv, maxMultiplier);
        case 16: return luFactorFixedNoPivot<16>(a, piv, maxMultiplier);
        case 18: return luFactorFixedNoPivot<18>(a, piv, maxMultiplier);
        default: return false;  // no specialised version: use the pivoting one
    }
}

inline bool luFactorFast(double* a, std::size_t* piv, std::size_t n) {
    switch (n) {
        case 6: return luFactorFixed<6>(a, piv);
        case 8: return luFactorFixed<8>(a, piv);
        case 10: return luFactorFixed<10>(a, piv);
        case 12: return luFactorFixed<12>(a, piv);
        case 14: return luFactorFixed<14>(a, piv);
        case 16: return luFactorFixed<16>(a, piv);
        case 18: return luFactorFixed<18>(a, piv);
        default: return luFactor(a, piv, n);
    }
}

inline void luSolveFast(const double* lu, const std::size_t* piv, double* b, std::size_t n) {
    switch (n) {
        case 6: luSolveFixed<6>(lu, piv, b); break;
        case 8: luSolveFixed<8>(lu, piv, b); break;
        case 10: luSolveFixed<10>(lu, piv, b); break;
        case 12: luSolveFixed<12>(lu, piv, b); break;
        case 14: luSolveFixed<14>(lu, piv, b); break;
        case 16: luSolveFixed<16>(lu, piv, b); break;
        case 18: luSolveFixed<18>(lu, piv, b); break;
        default: luSolve(lu, piv, b, n); break;
    }
}

}  // namespace sas
