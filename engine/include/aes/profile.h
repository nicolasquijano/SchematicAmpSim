#pragma once
// Optional cycle counters for the solver, compiled in only with -DAES_PROFILE (see tools/bench).
#include <cstdint>

#ifdef AES_PROFILE
#include <intrin.h>
namespace aes {
enum ProfSlot { ProfStep, ProfEval, ProfFactor, ProfSolve, ProfMatvec, ProfReconstruct, ProfSlots };
inline std::uint64_t g_profile[ProfSlots] = {};
struct ProfScope {
    ProfSlot slot;
    std::uint64_t t0;
    explicit ProfScope(ProfSlot s) : slot(s), t0(__rdtsc()) {}
    ~ProfScope() { g_profile[slot] += __rdtsc() - t0; }
};
}  // namespace aes
#define AES_PROF(slot) ::aes::ProfScope aesProfScope_##slot(::aes::slot)
#else
#define AES_PROF(slot) ((void)0)
#endif
