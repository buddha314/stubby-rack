#pragma once
// A cable's value: 1 to 16 channels of volts (spec modular-rack "Polyphony-ready
// cables"). Monophonic modules read channel 0 and write one channel. An unconnected
// input reads kUnconnected: 0 channels, all zero.

namespace stubby {

constexpr int kMaxChannels = 16;

struct PolyVoltage {
    float v[kMaxChannels] = {};
    int channels = 1;

    float mono() const { return v[0]; }
    void set_mono(float volts) {
        v[0] = volts;
        channels = 1;
    }
};

inline const PolyVoltage kUnconnected{{}, 0};

}  // namespace stubby
