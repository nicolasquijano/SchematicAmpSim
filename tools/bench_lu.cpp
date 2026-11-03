// Micro-benchmark of the fixed-size LU kernels the solver uses (sas/linalg.h): cycles per factorisation and per solve for the sizes
// the amps have, and a checksum of the results so two builds of the same code (e.g. with and without /arch:AVX2) can be compared bit for bit.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sas/linalg.h"

namespace {

template <std::size_t N>
void run() {
    constexpr int kMatrices = 64, kReps = 20000;
    std::vector<double> src(kMatrices * N * N), rhs(kMatrices * N);
    std::uint64_t s = 0x9e3779b97f4a7c15ull;
    auto rnd = [&] {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return double(s >> 11) / double(1ull << 53) - 0.5;
    };
    for (int m = 0; m < kMatrices; ++m) {
        for (std::size_t r = 0; r < N; ++r)
            for (std::size_t c = 0; c < N; ++c) src[std::size_t(m) * N * N + r * N + c] = (r == c ? 1.0 : 0.0) + 0.4 * rnd();  // like I + K D
        for (std::size_t r = 0; r < N; ++r) rhs[std::size_t(m) * N + r] = rnd();
    }
    std::vector<double> a(N * N), b(N);
    std::size_t piv[N];
    double checksum = 0.0;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (int rep = 0; rep < kReps; ++rep) {
        const int m = rep % kMatrices;
        std::memcpy(a.data(), &src[std::size_t(m) * N * N], N * N * sizeof(double));
        sas::luFactorFixedNoPivot<N>(a.data(), piv, 50.0);
        checksum += a[N * N - 1] + a[N + 1];
    }
    const double tf = std::chrono::duration<double>(clock::now() - t0).count() / kReps * 1e9;
    // copy only, to subtract it
    t0 = clock::now();
    for (int rep = 0; rep < kReps; ++rep) {
        const int m = rep % kMatrices;
        std::memcpy(a.data(), &src[std::size_t(m) * N * N], N * N * sizeof(double));
        checksum += a[3];
    }
    const double tc = std::chrono::duration<double>(clock::now() - t0).count() / kReps * 1e9;
    std::memcpy(a.data(), &src[0], N * N * sizeof(double));
    sas::luFactorFixedNoPivot<N>(a.data(), piv, 50.0);
    t0 = clock::now();
    for (int rep = 0; rep < kReps; ++rep) {
        const int m = rep % kMatrices;
        std::memcpy(b.data(), &rhs[std::size_t(m) * N], N * sizeof(double));
        sas::luSolveFixed<N>(a.data(), piv, b.data());
        checksum += b[N - 1];
    }
    const double ts = std::chrono::duration<double>(clock::now() - t0).count() / kReps * 1e9;
    std::printf("N=%2zu  factor %7.1f ns  solve %6.1f ns   checksum %.17g\n", N, tf - tc, ts, checksum);
}

}  // namespace

int main() {
    run<6>();
    run<8>();
    run<10>();
    run<12>();
    run<16>();
}
