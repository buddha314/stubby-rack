// Spec modular-rack scenarios, through the Engine API.
#include "doctest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "core/engine.h"
#include "core/modules/basic.h"
#include "core/patches.h"

using namespace stubby;

namespace {
/// Records its input's channel 0 and channel count per sample (test-only module).
class Probe : public Module {
public:
    Probe() : Module(1, 0, 0) { values.reserve(4096); channels.reserve(4096); }
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *) override {
        if (values.size() < values.capacity()) {
            values.push_back(in[0]->mono());
            channels.push_back(in[0]->channels);
        }
    }
    std::vector<float> values;
    std::vector<int> channels;
};

int first_high(const std::vector<float> &v) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i] >= 1.0f) return static_cast<int>(i);
    return -1;
}
}  // namespace

TEST_CASE("Cable delay: a path through one utility arrives exactly one sample later") {
    Engine e;
    auto direct = std::make_unique<Probe>();
    auto via = std::make_unique<Probe>();
    Probe *d = direct.get(), *v = via.get();
    e.add_module(1, std::make_unique<modules::Clock>(120.0f));
    e.add_module(2, std::make_unique<modules::Thru>());
    e.add_module(3, std::move(direct));
    e.add_module(4, std::move(via));
    e.connect(1, 0, 3, 0);
    e.connect(1, 0, 2, 0);
    e.connect(2, 0, 4, 0);
    e.commit();
    e.render(64);
    REQUIRE(first_high(d->values) >= 0);
    CHECK(first_high(v->values) == first_high(d->values) + 1);
}

TEST_CASE("Order independence: the same patch added in two orders renders bit-identical") {
    Engine a, b;
    patches::build_benchmark(a, 100, 8, 1, false);
    patches::build_benchmark(b, 100, 8, 1, true);
    a.commit();
    b.commit();
    for (int block = 0; block < 20; ++block) {
        a.render(512);
        b.render(512);
        for (int s = 0; s < 8; ++s)
            for (int i = 0; i < 512; ++i) REQUIRE(a.stem(s)[i] == b.stem(s)[i]);
    }
}

TEST_CASE("Feedback: a VCO modulated through a VCA by itself is accepted and stays finite") {
    Engine e;
    e.add_module(1, std::make_unique<modules::Vco>(48000.0f, daisysp::Oscillator::WAVE_SIN, 220.0f, 0.5f));
    e.add_module(2, std::make_unique<modules::Vca>());
    e.add_module(3, std::make_unique<modules::Constant>(10.0f));
    CHECK(e.connect(1, 0, 2, 0));
    CHECK(e.connect(3, 0, 2, 1));
    CHECK(e.connect(2, 0, 1, 1));  // VCA back into the VCO's FM
    e.set_stem(0, 1, 0);
    e.commit();
    bool finite = true, moving = false;
    for (int block = 0; block < 100; ++block) {
        e.render(480);
        for (int i = 0; i < 480; ++i) {
            finite &= std::isfinite(e.stem(0)[i]);
            moving |= std::fabs(e.stem(0)[i]) > 0.1f;
        }
    }
    CHECK(finite);
    CHECK(moving);
    CHECK(e.take_events().empty());
}

TEST_CASE("Mono default: a mono output reaches an input as 1 channel; unpatched is 0") {
    Engine e;
    auto patched = std::make_unique<Probe>();
    auto open = std::make_unique<Probe>();
    Probe *p = patched.get(), *o = open.get();
    e.add_module(1, std::make_unique<modules::Constant>(1.0f));
    e.add_module(2, std::move(patched));
    e.add_module(3, std::move(open));
    e.connect(1, 0, 2, 0);
    e.commit();
    e.render(8);
    CHECK(p->channels.back() == 1);
    CHECK(o->channels.back() == 0);
}

TEST_CASE("Non-finite output: a module computing NaN outputs 0 V") {
    Engine e;
    e.add_module(1, std::make_unique<modules::Constant>(std::numeric_limits<float>::quiet_NaN()));
    e.set_stem(0, 1, 0);
    e.commit();
    e.render(4);
    CHECK(e.stem(0)[3] == 0.0f);
}

TEST_CASE("Determinism: a 200-module patch renders identically twice") {
    auto render = [] {
        Engine e;
        patches::build_benchmark(e, 200);
        e.commit();
        std::vector<float> out;
        for (int block = 0; block < 50; ++block) {
            e.render(512);
            out.insert(out.end(), e.stem(0), e.stem(0) + 512);
            out.insert(out.end(), e.stem(31), e.stem(31) + 512);
        }
        return out;
    };
    CHECK(render() == render());
}
