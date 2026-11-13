// Scenarios from modular-rack "VCV voltage standards".
#include "doctest.h"

#include <limits>

#include "core/volts.h"

using namespace stubby::volts;

TEST_CASE("Pitch: +1 V on an oscillator is 523.25 Hz") {
    CHECK(voct_to_hz(1.0f) == doctest::Approx(523.25).epsilon(0.1 / 523.25));
}

TEST_CASE("Clock reference: 0 V is 2 Hz (120 BPM)") {
    CHECK(voct_to_hz(0.0f, kClockF0) == doctest::Approx(2.0));
}

TEST_CASE("Schmitt trigger: rising to 0.9 V and back never fires; 1.0 V fires once") {
    SchmittTrigger t;
    int fired = 0;
    for (int i = 0; i <= 90; ++i) fired += t.process(i * 0.01f);
    for (int i = 90; i >= 0; --i) fired += t.process(i * 0.01f);
    CHECK(fired == 0);
    CHECK(t.process(1.0f));
    CHECK_FALSE(t.process(1.0f));
}

TEST_CASE("Non-finite output becomes 0 V; finite values are never clamped") {
    CHECK(sanitize(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(sanitize(std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(sanitize(11.7f) == 11.7f);
}
