// Spec master-clock scenarios: the transport, scheduling on exact samples and musical
// boundaries, late notices, tempo clocks locked to whole multiples of the master,
// seeded random clocks, determinism, device changes, and pause.
#include "doctest.h"

#include <cmath>
#include <vector>

#include "alloc_guard.h"
#include "core/engine.h"
#include "core/modules/basic.h"
#include "core/modules/clocks.h"

using namespace stubby;
using stubby::test::AllocGuard;

namespace {

EngineConfig at_bpm(double bpm) {
    EngineConfig c;
    c.tempo_bpm = bpm;
    return c;
}

struct Tick {
    ModuleId module;
    uint64_t sample;
    int64_t index;
    bool operator==(const Tick &o) const { return module == o.module && sample == o.sample && index == o.index; }
};

/// Renders up to `until` in blocks of at most `block`, collecting ticks.
void run_to(Engine &e, uint64_t until, int block, std::vector<Tick> *ticks = nullptr,
            std::vector<Event> *late = nullptr) {
    while (e.frame() < until) {
        const uint64_t left = until - e.frame();
        e.render(left < static_cast<uint64_t>(block) ? static_cast<int>(left) : block);
        for (const Event &ev : e.take_events()) {
            if (ev.kind == Event::Kind::Tick && ticks) ticks->push_back({ev.module, ev.sample, ev.value});
            if (ev.kind == Event::Kind::Late && late) late->push_back(ev);
        }
    }
}

std::vector<Tick> of(const std::vector<Tick> &ticks, ModuleId m) {
    std::vector<Tick> out;
    for (const Tick &t : ticks)
        if (t.module == m) out.push_back(t);
    return out;
}

/// The first frame at or after `from` at which stem 0 reaches `value`, rendering in
/// blocks of `block`; UINT64_MAX if not by `until`.
uint64_t first_at(Engine &e, int stem, float value, uint64_t until, int block) {
    while (e.frame() < until) {
        const uint64_t start = e.frame();
        e.render(block);
        e.take_events();
        for (int i = 0; i < block; ++i)
            if (e.stem(stem)[i] == value) return start + static_cast<uint64_t>(i);
    }
    return UINT64_MAX;
}

}  // namespace

TEST_CASE("Transport: at 1 BPM a beat is 2,880,000 samples; at 120 BPM beat 2 and bar 2") {
    Transport t;
    CHECK(t.samples_per_beat() == 2880000.0);
    Engine one;
    CHECK(one.position().bpm == 1.0);
    run_to(one, 2880000, 4096);
    CHECK(one.position().beat == 2);
    CHECK(one.position().phase == doctest::Approx(0.0));

    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::TempoClock>(1));
    e.commit();
    std::vector<Tick> ticks;
    run_to(e, 96001, 512, &ticks);
    REQUIRE(ticks.size() >= 5);
    CHECK(ticks[0].sample == 0);
    CHECK(ticks[1].sample == 24000);  // beat 2
    CHECK(ticks[4].sample == 96000);  // bar 2, beat 1
    const Position p = e.position();
    CHECK(p.bar == 2);
    CHECK(p.beat == 1);
}

TEST_CASE("Tempo change mid-beat keeps the phase: the rest of the beat runs at the new tempo") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::TempoClock>(1));
    e.commit();
    std::vector<Tick> ticks;
    run_to(e, 12000, 500, &ticks);  // half way through beat 1
    CHECK(e.position().phase == doctest::Approx(0.5));
    e.set_tempo(90);
    e.commit();
    run_to(e, 60001, 500, &ticks);
    CHECK(e.position().bpm == 90.0);
    // Half a beat at 90 BPM is 16,000 samples.
    REQUIRE(ticks.size() >= 3);
    CHECK(ticks[1].sample == 28000);
    CHECK(ticks[2].sample == 28000 + 32000);
}

TEST_CASE("Next-beat quantize lands on the first sample of the next beat") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::Constant>(0.0f));
    e.set_stem(0, 1, 0);
    e.commit();
    run_to(e, 50000, 512);  // beat 3 (index 2.08)
    const ActionId id = e.at(When::next_beat());
    CHECK(id != 0);
    e.set_param(1, 0, 7.0f);
    e.at(When::now());
    e.commit();
    // Beat 4 begins at 3 × 24,000. Odd block sizes split around it.
    CHECK(first_at(e, 0, 7.0f, 100000, 333) == 72000);
}

TEST_CASE("Next bar and beat divisions quantize to their boundaries") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::Constant>(0.0f));
    e.add_module(2, std::make_unique<modules::Constant>(0.0f));
    e.set_stem(0, 1, 0);
    e.set_stem(1, 2, 0);
    e.commit();
    run_to(e, 30000, 512);
    e.at(When::next_bar());
    e.set_param(1, 0, 1.0f);
    e.at(When::next_division(4));  // sixteenths: 6,000 samples
    e.set_param(2, 0, 1.0f);
    e.at(When::now());
    e.commit();
    // The batch arrives at 30,000 (5 sixteenths exactly): the next one is 36,000.
    CHECK(first_at(e, 1, 1.0f, 200000, 512) == 36000);
    CHECK(first_at(e, 0, 1.0f, 200000, 512) == 96000);
}

TEST_CASE("Exact-sample actions land on their sample; late ones apply at the block start with a notice") {
    Engine e;
    e.add_module(1, std::make_unique<modules::Constant>(0.0f));
    e.set_stem(0, 1, 0);
    e.commit();
    e.render(1000);
    e.at(When::at_sample(1700));
    e.set_param(1, 0, 3.0f);
    e.at(When::now());
    e.commit();
    CHECK(first_at(e, 0, 3.0f, 4000, 512) == 1700);
    CHECK(e.take_events().empty());

    const uint64_t now = e.frame();
    const ActionId late = e.at(When::at_sample(100));
    e.set_param(1, 0, 4.0f);
    e.at(When::now());
    e.commit();
    std::vector<Event> notices;
    e.render(64);
    for (const Event &ev : e.take_events())
        if (ev.kind == Event::Kind::Late) notices.push_back(ev);
    CHECK(e.stem(0)[0] == 4.0f);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].action == late);
    CHECK(notices[0].sample == now);
    CHECK(notices[0].value == static_cast<int64_t>(now - 100));
}

TEST_CASE("Locked clocks: ×1 and ×2 tick together on every master beat") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::TempoClock>(1));
    e.add_module(2, std::make_unique<modules::TempoClock>(2));
    e.commit();
    std::vector<Tick> ticks;
    run_to(e, 240000, 480, &ticks);
    const auto a = of(ticks, 1), b = of(ticks, 2);
    REQUIRE(a.size() == 10);
    REQUIRE(b.size() == 20);
    for (size_t k = 0; k < a.size(); ++k) {
        CHECK(a[k].sample == 24000 * k);
        CHECK(b[2 * k].sample == a[k].sample);
        CHECK(b[2 * k + 1].sample == a[k].sample + 12000);
    }
}

TEST_CASE("×90 and ×120 clocks at 1 BPM share every master downbeat, with no drift over an hour") {
    Engine e;  // 1 BPM
    e.add_module(1, std::make_unique<modules::TempoClock>(90));
    e.add_module(2, std::make_unique<modules::TempoClock>(120));
    e.commit();
    std::vector<Tick> ticks;
    const uint64_t hour = 60ull * 2880000ull;  // 60 master beats
    run_to(e, hour + 1, 4096, &ticks);
    const auto a = of(ticks, 1), b = of(ticks, 2);
    REQUIRE(a.size() == 5401);
    REQUIRE(b.size() == 7201);
    for (uint64_t beat = 0; beat <= 60; ++beat) {
        REQUIRE(a[beat * 90].sample == beat * 2880000);
        REQUIRE(b[beat * 120].sample == beat * 2880000);
    }
    // Every tick is exact: ×90 is 32,000 samples, ×120 is 24,000.
    for (size_t k = 0; k < a.size(); ++k) REQUIRE(a[k].sample == 32000 * k);
    for (size_t k = 0; k < b.size(); ++k) REQUIRE(b[k].sample == 24000 * k);
    CHECK(e.dropped_events() == 0);
}

TEST_CASE("Retuning ×90 to ×120 mid-tick: the next tick is the next 1/120 boundary") {
    Engine e;
    e.add_module(1, std::make_unique<modules::TempoClock>(90));
    e.commit();
    std::vector<Tick> ticks;
    const uint64_t retune = 60 * 32000 + 16000;  // phase 0.5 of ×90 tick 60
    run_to(e, retune, 1000, &ticks);
    e.set_param(1, 0, 120.0f);
    e.commit();
    run_to(e, retune + 100000, 1000, &ticks);
    std::vector<Tick> after;
    for (const Tick &t : ticks)
        if (t.sample >= retune) after.push_back(t);
    REQUIRE(after.size() >= 2);
    // Position 60.5/90 beats = 80.67/120: the next ×120 tick is 81, at 81 × 24,000.
    CHECK(after[0].index == 81);
    CHECK(after[0].sample == 81 * 24000);
    CHECK(after[1].sample == 82 * 24000);
}

TEST_CASE("Seeded random clocks: same seed, same ticks; different seeds independent") {
    auto ticks_of = [](std::vector<uint64_t> seeds) {
        Engine e;
        for (size_t i = 0; i < seeds.size(); ++i)
            e.add_module(static_cast<ModuleId>(i + 1), std::make_unique<modules::RandomClock>(seeds[i], 0.5f, 3.0f));
        e.commit();
        std::vector<Tick> t;
        run_to(e, 48000 * 60, 512, &t);
        return t;
    };
    const auto alone = of(ticks_of({7}), 1);
    const auto pair = ticks_of({7, 8});
    CHECK(alone.size() > 15);
    CHECK(of(pair, 1) == alone);  // a second bug doesn't disturb the first
    const auto other = of(pair, 2);
    REQUIRE(!other.empty());
    CHECK(other[0].sample != alone[0].sample);
}

TEST_CASE("Offline renders are deterministic and independent of block size") {
    auto render = [](int block) {
        Engine e(at_bpm(60));
        e.add_module(1, std::make_unique<modules::TempoClock>(3));
        e.add_module(2, std::make_unique<modules::RandomClock>(42, 0.1f, 0.4f));
        e.set_stem(0, 1, 0);
        e.commit();
        e.at(When::next_division(2));
        e.set_param(1, 0, 4.0f);
        e.at(When::now());
        e.commit();
        std::vector<Tick> t;
        run_to(e, 48000 * 10, block, &t);
        return t;
    };
    const auto a = render(512);
    CHECK(a.size() > 30);
    CHECK(render(512) == a);
    CHECK(render(37) == a);
    CHECK(render(4096) == a);
}

TEST_CASE("Device change 48k → 44.1k keeps the musical position; a bar-6 action still lands on bar 6") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::Constant>(0.0f));
    e.set_stem(0, 1, 0);
    e.commit();
    run_to(e, 432000, 512);  // bar 5, beat 3 (18 beats)
    e.at(When::next_bar());
    e.set_param(1, 0, 6.0f);
    e.at(When::now());
    e.commit();
    e.render(512);
    const double before = e.position().beats;
    const uint64_t change = e.frame();
    e.set_sample_rate(44100);
    e.commit();
    e.render(64);
    CHECK(e.position().bar == 5);
    CHECK(e.position().beats == doctest::Approx(before + 64.0 / 22050.0));
    CHECK(e.sample_rate() == 44100.0f);
    const uint64_t landed = first_at(e, 0, 6.0f, change + 200000, 512);
    REQUIRE(landed != UINT64_MAX);
    // Bar 6 begins at beat 20: the remaining beats now run at 22,050 samples each.
    const double expected = static_cast<double>(change) + (20.0 - before) * 22050.0;
    CHECK(landed == static_cast<uint64_t>(std::ceil(expected - 1e-6)));
}

TEST_CASE("Pause: the clock runs on, emitters fade out without a click, the UI stem plays") {
    Engine e;  // 1 BPM
    e.add_module(1, std::make_unique<modules::TempoClock>(90));
    e.add_module(2, std::make_unique<modules::Constant>(5.0f));
    e.add_module(3, std::make_unique<modules::Constant>(2.0f));
    e.set_stem(0, 2, 0);
    e.set_stem(e.ui_stem(), 3, 0);
    e.commit();
    const uint64_t pause_at = 8ull * 2880000;  // bar 3, beat 1
    run_to(e, pause_at, 4096);
    e.set_paused(true);
    e.commit();

    // The fade: 30 ms (1,440 samples), monotonic, no step bigger than a ramp step.
    e.render(4096);
    float prev = 5.0f, worst = 0.0f;
    int silent_from = -1;
    for (int i = 0; i < 4096; ++i) {
        const float v = e.stem(0)[i];
        CHECK(v <= prev);
        worst = std::fmax(worst, prev - v);
        prev = v;
        if (v == 0.0f && silent_from < 0) silent_from = i;
        REQUIRE(e.stem(e.ui_stem())[i] == 2.0f);
    }
    CHECK(worst <= 5.0f / 1440.0f + 1e-5f);
    REQUIRE(silent_from >= 0);
    CHECK(silent_from <= 2400);  // within 50 ms
    e.take_events();

    // 180 s of pause: 3 master beats, 270 ticks of the ×90 clock, all silent.
    std::vector<Tick> ticks;
    const uint64_t end = pause_at + 3ull * 2880000;
    bool silent = true;
    while (e.frame() < end) {
        const uint64_t left = end - e.frame();
        const int n = left < 4096 ? static_cast<int>(left) : 4096;
        e.render(n);
        for (int i = 0; i < n; ++i) silent = silent && e.stem(0)[i] == 0.0f;
        for (const Event &ev : e.take_events())
            if (ev.kind == Event::Kind::Tick) ticks.push_back({ev.module, ev.sample, ev.value});
    }
    CHECK(silent);
    CHECK(of(ticks, 1).size() == 270 - 1);  // the tick at the pause sample itself was taken above
    const Position p = e.position();
    CHECK(p.bar == 3);
    CHECK(p.beat == 4);

    e.set_paused(false);
    e.commit();
    e.render(4096);
    CHECK(e.stem(0)[4095] == 5.0f);
}

TEST_CASE("Realtime safety: rendering clocks, schedules and tempo changes never allocates") {
    Engine e(at_bpm(120));
    e.add_module(1, std::make_unique<modules::TempoClock>(4));
    e.add_module(2, std::make_unique<modules::RandomClock>(3, 0.01f, 0.05f));
    e.add_module(3, std::make_unique<modules::Constant>(1.0f));
    e.set_stem(0, 3, 0);
    e.commit();
    e.at(When::next_beat());
    e.set_tempo(90);
    e.at(When::next_division(8));
    e.set_param(3, 0, 2.0f);
    e.at(When::now());
    e.set_paused(true);
    e.commit();
    AllocGuard::reset();
    {
        AllocGuard guard;
        for (int b = 0; b < 200; ++b) e.render(512);
    }
    CHECK(AllocGuard::count() == 0);
    CHECK(e.position().bpm == 90.0);
}
