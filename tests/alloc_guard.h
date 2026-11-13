#pragma once
// Allocation guard (spec audio-engine "Realtime safety"): counts heap allocations made
// on this thread while a guard is alive. The global operator new is replaced in
// alloc_guard.cpp, so this covers everything linked into the test binary.
#include <cstddef>

namespace stubby::test {
struct AllocGuard {
    AllocGuard();
    ~AllocGuard();
    static size_t count();  // allocations seen while any guard was alive on this thread
    static void reset();
};
}  // namespace stubby::test
