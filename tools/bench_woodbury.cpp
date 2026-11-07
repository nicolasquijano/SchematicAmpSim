// Microbenchmark: Newton step kernel of a 12-port power stage, J = I + K*D.
//  A) today:    build J from the D pattern, LU without pivot (p x p), solve.
//  B) Woodbury: D has r < p non-zero rows (grids not conducting): J = I + U V, U = K[:,A], V = D[A,:];
//               factor M = I + V U (r x r) and solve  dx = f - U M^-1 (V f).
// Ports: 2 pentodes (g1,p,g2) + 3 triodes (g,p): 6 + 6 = 12. Grid rows of D are zero when the grids do not conduct.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

constexpr int P = 12;
static double K[P][P];
static double Dm[P][P];            // full D
static bool active[P];             // rows of D that are non-zero
static int A[P], r;

static unsigned rng = 12345;
static double rnd() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / double(1 << 24) - 0.5; }

static void luNoPivot(double* a, int n) {
    for (int k = 0; k < n; ++k) {
        const double inv = 1.0 / a[k * n + k];
        for (int i = k + 1; i < n; ++i) {
            const double m = a[i * n + k] * inv;
            a[i * n + k] = m;
            for (int j = k + 1; j < n; ++j) a[i * n + j] -= m * a[k * n + j];
        }
    }
}
static void luSolve(const double* a, double* x, int n) {
    for (int i = 1; i < n; ++i) { double s = x[i]; for (int j = 0; j < i; ++j) s -= a[i * n + j] * x[j]; x[i] = s; }
    for (int i = n - 1; i >= 0; --i) { double s = x[i]; for (int j = i + 1; j < n; ++j) s -= a[i * n + j] * x[j]; x[i] = s / a[i * n + i]; }
}

int main(int argc, char** argv) {
    const int nGridZero = argc > 1 ? std::atoi(argv[1]) : 5;  // how many grid rows are zero (0..5)
    // D structure: pentode blocks (3x3), triode blocks (2x2 with D[g][p] = 0). Row order g, p, (g2).
    for (auto& row : Dm) for (auto& v : row) v = 0;
    int base = 0;
    std::vector<int> gridRows;
    for (int t = 0; t < 2; ++t, base += 3) {  // pentodes
        for (int m = 0; m < 3; ++m) for (int c = 0; c < 3; ++c) Dm[base + m][base + c] = 0.02 * (1 + rnd());
        Dm[base][base + 1] = Dm[base][base + 2] = 0;  // g1 current depends on vg1k only
        gridRows.push_back(base);
    }
    for (int t = 0; t < 3; ++t, base += 2) {  // triodes
        Dm[base][base] = 0.02 * (1 + rnd());
        Dm[base + 1][base] = 0.02 * (1 + rnd()); Dm[base + 1][base + 1] = 0.02 * (1 + rnd());
        gridRows.push_back(base);
    }
    for (int i = 0; i < P; ++i) active[i] = true;
    for (int g = 0; g < nGridZero && g < (int)gridRows.size(); ++g) {
        active[gridRows[g]] = false;
        for (int c = 0; c < P; ++c) Dm[gridRows[g]][c] = 0;
    }
    r = 0;
    for (int i = 0; i < P; ++i) if (active[i]) A[r++] = i;
    for (auto& row : K) for (auto& v : row) v = 4.0 * rnd();

    // D pattern (non-zero entries) as the solver keeps it
    struct Pt { int row, col; }; std::vector<Pt> pat;
    for (int i = 0; i < P; ++i) for (int j = 0; j < P; ++j) if (Dm[i][j] != 0) pat.push_back({i, j});

    const int reps = 400000;
    double sink = 0;
    double f[P];
    for (int i = 0; i < P; ++i) f[i] = rnd();

    // ---- A: build + factor, then solve
    static double lu[P * P];
    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < reps; ++it) {
        for (int i = 0; i < P * P; ++i) lu[i] = 0;
        for (int i = 0; i < P; ++i) {
            lu[i * P + i] = 1.0;
            for (const auto& pt : pat) lu[i * P + pt.col] += K[i][pt.row] * Dm[pt.row][pt.col] * (1.0 + 1e-9 * it);
        }
        luNoPivot(lu, P);
        sink += lu[P * P - 1];
    }
    auto t1 = std::chrono::steady_clock::now();
    double xA[P];
    for (int it = 0; it < reps; ++it) {
        for (int i = 0; i < P; ++i) xA[i] = f[i] + 1e-9 * it;
        luSolve(lu, xA, P);
        sink += xA[0];
    }
    auto t2 = std::chrono::steady_clock::now();

    // ---- B: Woodbury
    static double M[P * P];
    auto t3 = std::chrono::steady_clock::now();
    for (int it = 0; it < reps; ++it) {
        // M = I_r + V U with V = D[A,:], U = K[:,A]
        for (int a = 0; a < r; ++a) for (int b = 0; b < r; ++b) M[a * r + b] = (a == b) ? 1.0 : 0.0;
        for (const auto& pt : pat) {
            const int a = [&] { for (int q = 0; q < r; ++q) if (A[q] == pt.row) return q; return -1; }();
            if (a < 0) continue;
            const double d = Dm[pt.row][pt.col] * (1.0 + 1e-9 * it);
            for (int b = 0; b < r; ++b) M[a * r + b] += d * K[pt.col][A[b]];
        }
        luNoPivot(M, r);
        sink += M[r * r - 1];
    }
    auto t4 = std::chrono::steady_clock::now();
    double xB[P], vf[P], y[P];
    for (int it = 0; it < reps; ++it) {
        for (int a = 0; a < r; ++a) vf[a] = 0;
        for (const auto& pt : pat) {
            for (int q = 0; q < r; ++q) if (A[q] == pt.row) { vf[q] += Dm[pt.row][pt.col] * (f[pt.col] + 1e-9 * it); break; }
        }
        for (int a = 0; a < r; ++a) y[a] = vf[a];
        luSolve(M, y, r);
        for (int i = 0; i < P; ++i) {
            double s = f[i] + 1e-9 * it;
            for (int b = 0; b < r; ++b) s -= K[i][A[b]] * y[b];
            xB[i] = s;
        }
        sink += xB[0];
    }
    auto t5 = std::chrono::steady_clock::now();

    // check both agree
    double maxdiff = 0;
    for (int i = 0; i < P; ++i) xA[i] = f[i];
    {  // refactor A at it=0 exactly for the comparison
        for (int i = 0; i < P * P; ++i) lu[i] = 0;
        for (int i = 0; i < P; ++i) { lu[i * P + i] = 1.0; for (const auto& pt : pat) lu[i * P + pt.col] += K[i][pt.row] * Dm[pt.row][pt.col]; }
        luNoPivot(lu, P); luSolve(lu, xA, P);
        for (int a = 0; a < r; ++a) for (int b = 0; b < r; ++b) M[a * r + b] = (a == b) ? 1.0 : 0.0;
        for (const auto& pt : pat) for (int q = 0; q < r; ++q) if (A[q] == pt.row) for (int b = 0; b < r; ++b) M[q * r + b] += Dm[pt.row][pt.col] * K[pt.col][A[b]];
        luNoPivot(M, r);
        for (int a = 0; a < r; ++a) vf[a] = 0;
        for (const auto& pt : pat) for (int q = 0; q < r; ++q) if (A[q] == pt.row) vf[q] += Dm[pt.row][pt.col] * f[pt.col];
        for (int a = 0; a < r; ++a) y[a] = vf[a];
        luSolve(M, y, r);
        for (int i = 0; i < P; ++i) { double s = f[i]; for (int b = 0; b < r; ++b) s -= K[i][A[b]] * y[b]; xB[i] = s; maxdiff = std::fmax(maxdiff, std::fabs(xA[i] - xB[i])); }
    }
    auto ns = [&](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count() / reps; };
    std::printf("zero grid rows %d -> r=%d (p=%d)   max|dxA-dxB| = %.2e   (sink %.1f)\n", nGridZero, r, P, maxdiff, sink);
    std::printf("  A today:    build+factor %6.1f ns   solve %6.1f ns\n", ns(t0, t1), ns(t1, t2));
    std::printf("  B Woodbury: build+factor %6.1f ns   solve %6.1f ns\n", ns(t3, t4), ns(t4, t5));
    return 0;
}
