// Task 5.4 (design D4): the engine driver's step detection, ported from the spike's
// StemRenderer tests; plus the registry (task 6.2's core side) and the sampler (5.5).
#include "doctest.h"

#include <cmath>
#include <vector>

#include "alloc_guard.h"
#include "core/driver.h"
#include "core/registry.h"

using namespace stubby;
using stubby::test::AllocGuard;

namespace {
int64_t g_now = 0;
int64_t fake_clock() { return g_now; }
constexpr int kBlock = 512;
constexpr int64_t kStepNs = 10'666'667;  // 512 frames at 48 kHz
constexpr int64_t kReadNs = 5'000;       // readers in one step arrive microseconds apart
constexpr uint64_t kUnset = EngineDriver::kUnset;

/// An engine with a sine VCO on stems 1 and 2 (stem 0 silent).
void tone(Engine &e) {
    BuildContext c;
    e.add_module(1, make_module("vco_sine", c));
    e.set_stem(1, 1, 0);
    e.set_stem(2, 1, 0);
    e.commit();
}
}  // namespace

TEST_CASE("Driver: any playback order gets the same block, rendered once per step") {
    Engine e;
    tone(e);
    EngineDriver d(e);
    d.set_clock(&fake_clock);
    uint64_t driver = kUnset, a = kUnset, b = kUnset;
    std::vector<float> o(kBlock), x(kBlock), y(kBlock);
    for (int step = 0; step < 50; ++step) {
        const int64_t t = step * kStepNs;
        if (step % 2) {
            g_now = t; d.read(1, a, x.data(), kBlock);
            g_now = t + kReadNs; d.read(-1, driver, o.data(), kBlock);
            g_now = t + 2 * kReadNs; d.read(2, b, y.data(), kBlock);
        } else {
            g_now = t; d.read(-1, driver, o.data(), kBlock);
            g_now = t + kReadNs; d.read(2, b, y.data(), kBlock);
            g_now = t + 2 * kReadNs; d.read(1, a, x.data(), kBlock);
        }
        CHECK(a == b);
        CHECK(a == driver);
        CHECK(x == y);  // the same samples on every stem in a step
        CHECK(d.step_start() == uint64_t(step) * kBlock);
        for (float v : o) REQUIRE(v == 0.0f);  // the driver reads silence
    }
    CHECK(e.frame() == 50 * kBlock);
    CHECK(d.stats.renders == 50);
    CHECK(d.stats.skips == 0);
    CHECK(std::fabs(x[100]) > 0.0f);
}

TEST_CASE("Driver: stems started mid-game join the current step, before or after the driver") {
    Engine e;
    EngineDriver d(e);
    d.set_clock(&fake_clock);
    uint64_t driver = kUnset;
    std::vector<float> out(kBlock);
    for (int step = 0; step < 10; ++step) { g_now = step * kStepNs; d.read(-1, driver, out.data(), kBlock); }
    uint64_t late = kUnset, early = kUnset;
    g_now = 10 * kStepNs; d.read(-1, driver, out.data(), kBlock);
    g_now = 10 * kStepNs + kReadNs; d.read(0, late, out.data(), kBlock);
    CHECK(late == driver);
    g_now = 11 * kStepNs; d.read(1, early, out.data(), kBlock);
    g_now = 11 * kStepNs + kReadNs; d.read(-1, driver, out.data(), kBlock);
    g_now = 11 * kStepNs + 2 * kReadNs; d.read(0, late, out.data(), kBlock);
    CHECK(early == driver);
    CHECK(late == driver);
    CHECK(d.stats.skips == 0);
    CHECK(d.stats.joins == 3);
}

TEST_CASE("Driver: bursty steps closer than the gap are split by a repeated reader") {
    Engine e;
    EngineDriver d(e);
    d.set_clock(&fake_clock);
    uint64_t driver = kUnset, s = kUnset;
    std::vector<float> out(kBlock);
    for (int step = 0; step < 20; ++step) {  // steps only 100 µs apart (a dummy driver)
        g_now = step * 100'000; d.read(-1, driver, out.data(), kBlock);
        g_now = step * 100'000 + kReadNs; d.read(0, s, out.data(), kBlock);
        CHECK(s == driver);
    }
    CHECK(d.stats.renders == 20);
    CHECK(d.stats.repeat_steps == 19);
}

TEST_CASE("Driver: a stopped reader that resumes is counted as a skip, then realigned") {
    Engine e;
    EngineDriver d(e);
    d.set_clock(&fake_clock);
    uint64_t driver = kUnset, s = kUnset;
    std::vector<float> out(kBlock);
    g_now = 0; d.read(-1, driver, out.data(), kBlock);
    g_now = kReadNs; d.read(0, s, out.data(), kBlock);
    for (int step = 1; step < 5; ++step) { g_now = step * kStepNs; d.read(-1, driver, out.data(), kBlock); }
    g_now = 4 * kStepNs + kReadNs; d.read(0, s, out.data(), kBlock);
    CHECK(d.stats.skips == 1);
    CHECK(s == driver);
}

TEST_CASE("Driver: the step feeds the engine's sync point, and reading never allocates") {
    Engine e;
    tone(e);
    EngineDriver d(e);
    d.set_clock(&fake_clock);
    uint64_t driver = kUnset, s = kUnset;
    std::vector<float> out(kBlock);
    AllocGuard::reset();
    {
        AllocGuard guard;
        for (int step = 0; step < 100; ++step) {
            g_now = 1'000'000'000 + step * kStepNs; d.read(-1, driver, out.data(), kBlock);
            g_now += kReadNs; d.read(1, s, out.data(), kBlock, 0.1f);
        }
    }
    CHECK(AllocGuard::count() == 0);
    int64_t heard = 0;
    REQUIRE(e.predict_ns(100 * kBlock, 0, heard));
    CHECK(std::llabs(heard - (1'000'000'000 + 100 * kStepNs)) < 1'000'000);
}

TEST_CASE("Registry: every kind builds, with the ports and parameters it declares") {
    BuildContext c;
    REQUIRE(module_kinds().size() >= 12);
    for (const KindInfo &k : module_kinds()) {
        auto m = make_module(k.kind, c);
        REQUIRE_MESSAGE(m, k.kind);
        CHECK_MESSAGE(m->num_inputs() == static_cast<int>(k.inputs.size()), k.kind);
        CHECK_MESSAGE(m->num_outputs() == static_cast<int>(k.outputs.size()), k.kind);
        CHECK_MESSAGE(m->num_params() == static_cast<int>(k.params.size()), k.kind);
        for (size_t p = 0; p < k.params.size(); ++p) CHECK(m->param(static_cast<int>(p)) == k.params[p].def);
    }
    CHECK(make_module("no_such_kind", c) == nullptr);
    CHECK(find_kind("vco_square") != nullptr);
}

TEST_CASE("Sampler: a scheduled trigger starts its sample on the exact sample, resampled") {
    SampleBank bank;
    std::vector<float> click(441, 0.0f);  // 10 ms at 44.1 kHz
    click[0] = 1.0f;
    for (size_t i = 1; i < click.size(); ++i) click[i] = 0.5f;
    const int id = bank.add(click, 44100.0);
    REQUIRE(id == 0);
    BuildContext c;
    c.bank = &bank;
    Engine e;
    e.add_module(1, make_module("sampler", c));
    e.set_stem(e.ui_stem(), 1, 0);
    e.set_param(1, 0, static_cast<float>(id));
    e.commit();
    e.render(512);
    e.at(When::at_sample(1000));
    e.set_param(1, 1, 1.0f);  // voice 1's trigger
    e.at(When::now());
    e.commit();
    e.render(512);
    const float *ui = e.stem(e.ui_stem());
    CHECK(ui[1000 - 512 - 1] == 0.0f);
    CHECK(ui[1000 - 512] == doctest::Approx(5.0f));  // full scale is 5 V
    e.render(512);
    // 441 samples at 44.1 kHz play for 480 samples at 48 kHz.
    CHECK(e.stem(e.ui_stem())[1000 + 470 - 1024] == doctest::Approx(2.5f));
    CHECK(e.stem(e.ui_stem())[1000 + 485 - 1024] == 0.0f);
}
