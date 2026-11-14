// Spec step-sequencer (wait-for-stubs add-snail-shell-sequencer): stepping on clock
// edges, reset, gate on lit steps only, CV held, a step event per advance on the
// sample the edge is seen, and no allocation while running.
#include "doctest.h"

#include <vector>

#include "alloc_guard.h"
#include "core/modules/sequencers.h"

using namespace stubby;
using stubby::modules::StepSequencer;
using stubby::test::AllocGuard;

namespace {

/// Drives a sequencer one sample at a time with a clock and reset level.
struct Bench {
    StepSequencer seq{4};
    EventSink sink{64};
    PolyVoltage clock{}, reset{}, out[2]{};
    uint64_t frame = 0;

    Bench() {
        clock.channels = 1;
        reset.channels = 1;
    }
    void sample(float clock_v, float reset_v = 0.0f) {
        clock.v[0] = clock_v;
        reset.v[0] = reset_v;
        const PolyVoltage *in[2] = {&clock, &reset};
        ProcessArgs a;
        a.frame = frame++;
        a.events = &sink;
        a.handle = 7;
        seq.process(a, in, out);
    }
    /// One clock period: `high` samples at 10 V, then `low` at 0 V. Returns the gate
    /// output seen while the clock was high.
    float pulse(int high = 4, int low = 4) {
        float gate = 0.0f;
        for (int i = 0; i < high; ++i) {
            sample(10.0f);
            gate = out[0].mono() > gate ? out[0].mono() : gate;
        }
        for (int i = 0; i < low; ++i) sample(0.0f);
        return gate;
    }
    std::vector<AudioEvent> events() {
        std::vector<AudioEvent> e;
        AudioEvent ev;
        while (sink.pop(ev)) e.push_back(ev);
        return e;
    }
};

}  // namespace

TEST_CASE("step sequencer: steps 1 to 4 on clock edges, then wraps") {
    Bench b;
    CHECK(b.seq.step() == -1);
    std::vector<int> seen;
    for (int i = 0; i < 5; ++i) {
        b.pulse();
        seen.push_back(b.seq.step());
    }
    CHECK(seen == std::vector<int>{0, 1, 2, 3, 0});
}

TEST_CASE("step sequencer: reset makes the next clock play step 1") {
    Bench b;
    for (int i = 0; i < 3; ++i) b.pulse();
    CHECK(b.seq.step() == 2);
    b.sample(0.0f, 10.0f);
    b.sample(0.0f, 0.0f);
    b.pulse();
    CHECK(b.seq.step() == 0);
}

TEST_CASE("step sequencer: gate only on lit steps, and it follows the clock pulse") {
    Bench b;
    b.seq.set_param(StepSequencer::gate_param(1), 1.0f);  // step 2
    b.seq.set_param(StepSequencer::gate_param(3), 1.0f);  // step 4
    std::vector<float> gates;
    for (int i = 0; i < 4; ++i) gates.push_back(b.pulse());
    CHECK(gates == std::vector<float>{0.0f, 10.0f, 0.0f, 10.0f});
    CHECK(b.out[0].mono() == 0.0f);  // low between pulses, even on a lit step
}

TEST_CASE("step sequencer: CV holds the current step's voltage") {
    Bench b;
    const float cv[4] = {0.0f, 0.25f, 0.5f, 1.0f};
    for (int i = 0; i < 4; ++i) b.seq.set_param(StepSequencer::cv_param(i), cv[i]);
    for (int i = 0; i < 4; ++i) {
        b.pulse();
        CHECK(b.out[1].mono() == cv[i]);
    }
}

TEST_CASE("step sequencer: one event per step, index = step, on the sample the edge is seen") {
    Bench b;
    b.pulse(4, 4);  // edge seen at frame 0
    b.pulse(4, 4);  // edge seen at frame 8
    const std::vector<AudioEvent> e = b.events();
    REQUIRE(e.size() == 2);
    CHECK(e[0].handle == 7);
    CHECK(e[0].sample == 0);
    CHECK(e[0].value == 0);
    CHECK(e[1].sample == 8);
    CHECK(e[1].value == 1);
}

TEST_CASE("step sequencer: no allocation while running") {
    Bench b;
    AllocGuard::reset();
    {
        AllocGuard guard;
        for (int i = 0; i < 64; ++i) b.pulse();
    }
    CHECK(AllocGuard::count() == 0);
}
