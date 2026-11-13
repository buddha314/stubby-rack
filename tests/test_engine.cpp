// Spec audio-engine scenarios: the lock-free boundary, batches, rejections, realtime
// safety, silence; and modular-rack's budget.
#include "doctest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "alloc_guard.h"
#include "core/engine.h"
#include "core/modules/basic.h"
#include "core/patches.h"

using namespace stubby;
using stubby::test::AllocGuard;

TEST_CASE("A frame's edits land together: added modules and their cable start on one sample") {
    Engine e;
    e.render(100);  // the clock is already running
    e.add_module(10, std::make_unique<modules::Constant>(5.0f));
    e.add_module(11, std::make_unique<modules::Thru>());
    e.connect(10, 0, 11, 0);
    e.set_stem(0, 11, 0);
    e.commit();
    e.render(4);
    // Thru reads the constant through a 1-sample cable: 0 V on the first sample of the
    // block, 5 V from the second. Had the edits landed apart, the 5 V would come later.
    CHECK(e.stem(0)[0] == 0.0f);
    CHECK(e.stem(0)[1] == 5.0f);
    CHECK(e.frame() == 104);
}

TEST_CASE("Invalid command: rejected with an event naming it, and the rest of the batch applies") {
    Engine e;
    e.add_module(1, std::make_unique<modules::Constant>(2.0f));
    e.add_module(2, std::make_unique<modules::Thru>());
    CHECK_FALSE(e.connect(1, 0, 99, 0));  // no module 99
    e.connect(1, 0, 2, 0);
    e.set_stem(0, 2, 0);
    e.commit();
    e.render(4);
    auto events = e.take_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == Event::Kind::Rejected);
    CHECK(events[0].module == 99);
    CHECK(e.stem(0)[3] == 2.0f);
}

TEST_CASE("Fresh engine is silent: every stem is digital silence") {
    Engine e;
    e.render(512);
    for (int s = 0; s < 128; ++s)
        for (int i = 0; i < 512; ++i) REQUIRE(e.stem(s)[i] == 0.0f);
}

TEST_CASE("Remove frees the module on the main thread, unplugs its cables, and slots are reused") {
    Engine e;
    e.add_module(1, std::make_unique<modules::Constant>(3.0f));
    e.add_module(2, std::make_unique<modules::Thru>());
    e.connect(1, 0, 2, 0);
    e.set_stem(0, 2, 0);
    e.commit();
    e.render(4);
    CHECK(e.stem(0)[3] == 3.0f);
    e.remove_module(1);
    e.commit();
    e.render(4);
    e.collect();
    CHECK(e.stem(0)[3] == 0.0f);  // its cable was unplugged
    CHECK_FALSE(e.has_module(1));
    // Reuse: a new module takes the freed slots and starts from 0 V.
    e.add_module(3, std::make_unique<modules::Constant>(7.0f));
    e.connect(3, 0, 2, 0);
    e.commit();
    e.render(4);
    CHECK(e.stem(0)[3] == 7.0f);
    CHECK(e.take_events().empty());
}

TEST_CASE("Allocation guard: 1,000 blocks of a patch edited every frame allocate nothing on the render thread") {
    Engine e;
    patches::build_benchmark(e, 200);
    e.commit();
    AllocGuard::reset();
    for (int frame = 0; frame < 1000; ++frame) {
        // Main thread: edit every frame (these may allocate; they're not the audio thread).
        e.set_param(2 + (frame % 30) * 5, 0, 0.005f + 0.0001f * (frame % 7));
        if (frame % 10 == 0) e.disconnect(5, 0);
        if (frame % 10 == 5) e.connect(4, 0, 5, 0);
        if (frame % 50 == 0) e.add_module(5000 + frame, std::make_unique<modules::Constant>(1.0f));
        if (frame % 50 == 25) e.remove_module(5000 + frame - 25);
        e.commit();
        {
            AllocGuard guard;  // audio thread
            e.render(512);
        }
        e.collect();
    }
    CHECK(AllocGuard::count() == 0);
}

TEST_CASE("Budget: a 200-module patch renders a 512-frame block in under 15% of its duration") {
    Engine e;
    const int modules = patches::build_benchmark(e, 200);
    e.commit();
    for (int i = 0; i < 200; ++i) e.render(512);  // warm up
    std::vector<double> ms;
    for (int i = 0; i < 1000; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        e.render(512);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    double mean = 0;
    for (double x : ms) mean += x;
    mean /= ms.size();
    MESSAGE(modules << " modules: mean " << mean << " ms per block (budget 1.6 ms)");
    CHECK(modules >= 195);
    CHECK(mean < 1.6);
}

TEST_CASE("The allocation guard itself: an allocation inside a guard is counted") {
    AllocGuard::reset();
    {
        AllocGuard guard;
        // A direct call can't be elided (new-expressions can be, at -O2), and it's the
        // function std::allocator uses, so this is what the render-path check relies on.
        void *volatile p = ::operator new(64);
        ::operator delete(p);
    }
    CHECK(AllocGuard::count() >= 1);
    AllocGuard::reset();
}

TEST_CASE("The UI stem is the last stem: non-spatial, and silent until something plays on it") {
    Engine e;
    CHECK(e.ui_stem() == e.stem_count() - 1);
    e.render(64);
    for (int i = 0; i < 64; ++i) REQUIRE(e.stem(e.ui_stem())[i] == 0.0f);
}
