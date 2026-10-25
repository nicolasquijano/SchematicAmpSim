#pragma once
// Optional cycle counters for the solver, compiled in only with -DSAS_PROFILE (see tools/bench).
#include <cstdint>

#ifdef SAS_PROFILE
#include <intrin.h>
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
