#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

#include "sas/linalg.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("inverter, generic LU and in-place solve agree with the definition") {
    const double m[9] = {4, 3, 0, 3, 4, -1, 0, -1, 4};
    sas::Inverter inv(3);
    const double* i = inv.invert(m);
    REQUIRE(i != nullptr);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double v = 0;
            for (int k = 0; k < 3; ++k) v += m[r * 3 + k] * i[k * 3 + c];
            REQUIRE_THAT(v, WithinAbs(r == c ? 1.0 : 0.0, 1e-12));
        }

    double lu[9];
    std::copy(m, m + 9, lu);
    std::size_t piv[3];
    REQUIRE(sas::luFactor(lu, piv, 3));
    double x[3] = {1, 2, 3};
    sas::luSolve(lu, piv, x, 3);
    double a[9];
    std::copy(m, m + 9, a);
    double b[3] = {1, 2, 3};
    REQUIRE(sas::solveInPlace(a, b, 3));
    for (int r = 0; r < 3; ++r) {
        double vx = 0, vb = 0;
        for (int k = 0; k < 3; ++k) { vx += m[r * 3 + k] * x[k]; vb += m[r * 3 + k] * b[k]; }
        REQUIRE_THAT(vx, WithinAbs(r + 1.0, 1e-12));
        REQUIRE_THAT(vb, WithinAbs(r + 1.0, 1e-12));
    }
}

TEST_CASE("size-specialised LU matches the generic one") {
    for (std::size_t n : {6u, 8u, 10u, 12u, 14u, 9u}) {
        std::vector<double> big(n * n);
        for (std::size_t r = 0; r < n; ++r)
            for (std::size_t c = 0; c < n; ++c)
                big[r * n + c] = double((r * 7 + c * 3) % 11) * 0.37 + (r == c ? 4.0 : 0.0);
        std::vector<double> rhs(n);
        for (std::size_t i = 0; i < n; ++i) rhs[i] = double(i) - 2.5;
        auto l1 = big, l2 = big;
        auto x1 = rhs, x2 = rhs;
        std::vector<std::size_t> p1(n), p2(n);
        REQUIRE(sas::luFactor(l1.data(), p1.data(), n));
        sas::luSolve(l1.data(), p1.data(), x1.data(), n);
        REQUIRE(sas::luFactorFast(l2.data(), p2.data(), n));
        sas::luSolveFast(l2.data(), p2.data(), x2.data(), n);
        for (std::size_t i = 0; i < n; ++i) REQUIRE_THAT(x1[i], WithinAbs(x2[i], 1e-12));
    }
}

TEST_CASE("singular matrices are reported") {
    const double m[4] = {1, 2, 2, 4};
    sas::Inverter inv(2);
    REQUIRE(inv.invert(m) == nullptr);
}
