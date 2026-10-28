#pragma once
// Optional cycle counters for the solver, compiled in only with -DSAS_PROFILE (see tools/bench).
#include <cstdint>

#ifdef SAS_PROFILE
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#error "SAS_PROFILE needs the x86 time-stamp counter (__rdtsc)"
#endif
namespace sas {
enum ProfSlot { ProfStep, ProfEval, ProfFactor, ProfSolve, ProfMatvec, ProfReconstruct, ProfSlots };
inline std::uint64_t g_profile[ProfSlots] = {};
struct ProfScope {
    ProfSlot slot;
    std::uint64_t t0;
    explicit ProfScope(ProfSlot s) : slot(s), t0(__rdtsc()) {}
    ~ProfScope() { g_profile[slot] += __rdtsc() - t0; }
};
}  // namespace sas
#define SAS_PROF(slot) ::sas::ProfScope sasProfScope_##slot(::sas::slot)
#else
#define SAS_PROF(slot) ((void)0)
#endif
