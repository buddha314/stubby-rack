#include "alloc_guard.h"

#include <cstdlib>
#include <new>

namespace {
thread_local int g_depth = 0;
thread_local size_t g_count = 0;
void *checked(std::size_t size) {
    if (g_depth > 0) ++g_count;
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
}  // namespace

void *operator new(std::size_t size) { return checked(size); }
void *operator new[](std::size_t size) { return checked(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

namespace stubby::test {
AllocGuard::AllocGuard() { ++g_depth; }
AllocGuard::~AllocGuard() { --g_depth; }
size_t AllocGuard::count() { return g_count; }
void AllocGuard::reset() { g_count = 0; }
}  // namespace stubby::test
