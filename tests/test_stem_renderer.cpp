// Spike 1.2: the stem fan-out's step detection (design D4).
#include "doctest.h"

#include <vector>

#include "core/stem_renderer.h"

using stubby::StemRenderer;

namespace {
int64_t g_now = 0;
int64_t fake_clock() { return g_now; }
constexpr int kBlock = 512;
constexpr int64_t kStepNs = 10'666'667;  // 512 frames at 48 kHz
constexpr int64_t kReadNs = 5'000;       // readers in one step arrive microseconds apart
}

TEST_CASE("Any playback order: every reader in a step gets the same block, rendered once") {
    StemRenderer r(4);
    r.set_clock(&fake_clock);
    r.configure(48000.0f, 440.0f, 1.0f);
    uint64_t driver = StemRenderer::kUnset, a = StemRenderer::kUnset, b = StemRenderer::kUnset;
    std::vector<float> d(kBlock), x(kBlock), y(kBlock);
    for (int step = 0; step < 50; ++step) {
        const int64_t t = step * kStepNs;
        if (step % 2) { (g_now = t, r.read(1, a, x.data(), kBlock)); (g_now = t + kReadNs, r.read(-1, driver, d.data(), kBlock)); (g_now = t + 2 * kReadNs, r.read(2, b, y.data(), kBlock)); }
        else { (g_now = t, r.read(-1, driver, d.data(), kBlock)); (g_now = t + kReadNs, r.read(2, b, y.data(), kBlock)); (g_now = t + 2 * kReadNs, r.read(1, a, x.data(), kBlock)); }
        CHECK(a == b);
        CHECK(a == driver);
        CHECK(x == y);  // the same samples on every stem in a step
        CHECK(r.step_start() == uint64_t(step) * kBlock);
    }
    CHECK(r.frontier() == 50 * kBlock);
    CHECK(r.stats.renders == 50);
    CHECK(r.stats.skips == 0);
}

TEST_CASE("Stems started mid-game join the current step, before or after the driver") {
    StemRenderer r(4);
    r.set_clock(&fake_clock);
    uint64_t driver = StemRenderer::kUnset;
    std::vector<float> out(kBlock);
    for (int step = 0; step < 10; ++step) (g_now = step * kStepNs, r.read(-1, driver, out.data(), kBlock));
    uint64_t late = StemRenderer::kUnset, early = StemRenderer::kUnset;
    (g_now = 10 * kStepNs, r.read(-1, driver, out.data(), kBlock));           // driver first
    (g_now = 10 * kStepNs + kReadNs, r.read(0, late, out.data(), kBlock));    // then a new stem
    CHECK(late == driver);
    (g_now = 11 * kStepNs, r.read(1, early, out.data(), kBlock));             // a new stem first
    (g_now = 11 * kStepNs + kReadNs, r.read(-1, driver, out.data(), kBlock));
    (g_now = 11 * kStepNs + 2 * kReadNs, r.read(0, late, out.data(), kBlock));
    CHECK(early == driver);
    CHECK(late == driver);
    CHECK(r.stats.skips == 0);
    CHECK(r.stats.joins == 3);
}

TEST_CASE("Bursty steps closer than the gap are still split by a repeated reader") {
    StemRenderer r(2);
    r.set_clock(&fake_clock);
    uint64_t driver = StemRenderer::kUnset, s = StemRenderer::kUnset;
    std::vector<float> out(kBlock);
    for (int step = 0; step < 20; ++step) {  // steps only 100 µs apart (a dummy driver)
        (g_now = step * 100'000, r.read(-1, driver, out.data(), kBlock));
        (g_now = step * 100'000 + kReadNs, r.read(0, s, out.data(), kBlock));
        CHECK(s == driver);
    }
    CHECK(r.stats.renders == 20);
    CHECK(r.stats.repeat_steps == 19);
}

TEST_CASE("A stopped reader that resumes is counted as a skip, then realigned") {
    StemRenderer r(2);
    r.set_clock(&fake_clock);
    uint64_t driver = StemRenderer::kUnset, s = StemRenderer::kUnset;
    std::vector<float> out(kBlock);
    (g_now = 0, r.read(-1, driver, out.data(), kBlock));
    (g_now = kReadNs, r.read(0, s, out.data(), kBlock));
    for (int step = 1; step < 5; ++step) (g_now = step * kStepNs, r.read(-1, driver, out.data(), kBlock));
    (g_now = 4 * kStepNs + kReadNs, r.read(0, s, out.data(), kBlock));
    CHECK(r.stats.skips == 1);
    CHECK(s == driver);
}
