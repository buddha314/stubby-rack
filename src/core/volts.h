#pragma once
// VCV Rack voltage standards (https://vcvrack.com/manual/VoltageStandards).
// Spec: wait-for-stubs openspec/changes/add-rack-audio-engine, modular-rack
// "VCV voltage standards". Header-only, realtime-safe, no Godot.

#include <cmath>

namespace stubby::volts {

constexpr float kAudioPeak = 5.0f;          // audio is ±5 V
constexpr float kGateHigh = 10.0f;          // gates and triggers are 10 V
constexpr float kTriggerSeconds = 0.001f;   // trigger outputs last 1 ms
constexpr float kSchmittLow = 0.1f;         // Schmitt trigger thresholds for gate inputs
constexpr float kSchmittHigh = 1.0f;
constexpr float kFullScale = 10.0f;         // ±10 V = 0 dBFS at an emission stage
constexpr float kOscillatorF0 = 261.6256f;  // C4, 0 V on an oscillator's V/oct
constexpr float kClockF0 = 2.0f;            // 2 Hz (120 BPM), 0 V on a clock or LFO

/// 1 V/oct: f = f0 · 2^V.
inline float voct_to_hz(float volts, float f0 = kOscillatorF0) {
    return f0 * std::exp2(volts);
}

/// Modules output 0 V in place of NaN or infinity. Never clamp otherwise.
inline float sanitize(float volts) {
    return std::isfinite(volts) ? volts : 0.0f;
}

/// Gate/trigger input with hysteresis: fires once on crossing kSchmittHigh,
/// re-arms only after falling below kSchmittLow.
class SchmittTrigger {
public:
    /// Returns true on the sample the input goes high.
    bool process(float volts) {
        if (high_) {
            if (volts <= kSchmittLow) high_ = false;
            return false;
        }
        if (volts >= kSchmittHigh) {
            high_ = true;
            return true;
        }
        return false;
    }
    bool is_high() const { return high_; }
    void reset() { high_ = false; }

private:
    bool high_ = false;
};

}  // namespace stubby::volts
