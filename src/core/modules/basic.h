#pragma once
// First versions of the engine's basic modules (spec synth-modules), enough for the
// core's tests and the 200-module budget patch. Group 6 checks each against its spec
// scenarios and adds RackModuleDefinition resources. Oscillators come from DaisySP
// (MIT); the rest is a few lines each (VCV voltage conventions, volts.h).

#include <cstdint>

#include "Synthesis/oscillator.h"
#include "core/module.h"
#include "core/volts.h"

namespace stubby::modules {

/// Output: param 0 volts.
class Constant : public Module {
public:
    explicit Constant(float volts = 0.0f) : Module(0, 1, 1) { set_param(0, volts); }
    void process(const ProcessArgs &, const PolyVoltage *const *, PolyVoltage *out) override {
        out[0].set_mono(volts::sanitize(param(0)));
    }
};

/// Free-running gate clock: 10 V for the first half of each beat at param 0 BPM.
/// (Master-following tempo clocks come with the master clock, task 4.3.)
class Clock : public Module {
public:
    explicit Clock(float bpm = 120.0f) : Module(0, 1, 1) { set_param(0, bpm); }
    void process(const ProcessArgs &args, const PolyVoltage *const *, PolyVoltage *out) override {
        const double spb = args.sample_rate * 60.0 / (param(0) > 0.0f ? param(0) : 1.0f);
        out[0].set_mono(pos_ < spb * 0.5 ? volts::kGateHigh : 0.0f);
        pos_ += 1.0;
        if (pos_ >= spb) pos_ -= spb;
    }
private:
    double pos_ = 0.0;
};

/// Attack/decay envelope, retriggered on a rising gate (Schmitt 0.1/1.0 V).
/// Params: attack per sample, decay per sample. In: gate. Out: 0–10 V.
class Envelope : public Module {
public:
    Envelope(float attack = 0.01f, float decay = 0.0002f) : Module(1, 1, 2) {
        set_param(0, attack);
        set_param(1, decay);
    }
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *out) override {
        if (trigger_.process(in[0]->mono())) rising_ = true;
        if (rising_) {
            level_ += param(0);
            if (level_ >= 1.0f) { level_ = 1.0f; rising_ = false; }
        } else {
            level_ = level_ - param(1) > 0.0f ? level_ - param(1) : 0.0f;
        }
        out[0].set_mono(level_ * volts::kGateHigh);
    }
private:
    volts::SchmittTrigger trigger_;
    float level_ = 0.0f;
    bool rising_ = false;
};

/// Oscillator (DaisySP). Param 0: frequency at 0 V (Hz); param 1: linear FM index.
/// In: V/oct, FM. Out: ±5 V.
class Vco : public Module {
public:
    Vco(float sample_rate, uint8_t waveform, float hz = volts::kOscillatorF0, float fm_index = 0.0f) : Module(2, 1, 2) {
        osc_.Init(sample_rate);
        osc_.SetWaveform(waveform);
        osc_.SetAmp(1.0f);
        set_param(0, hz);
        set_param(1, fm_index);
    }
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *out) override {
        const float hz = param(0) * std::exp2(in[0]->mono()) * (1.0f + param(1) * in[1]->mono());
        osc_.SetFreq(hz > 0.0f ? hz : 0.0f);
        out[0].set_mono(volts::sanitize(osc_.Process() * volts::kAudioPeak));
    }
private:
    daisysp::Oscillator osc_;
};

/// out = in × cv / 10 V. In: signal, cv.
class Vca : public Module {
public:
    Vca() : Module(2, 1, 0) {}
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *out) override {
        out[0].set_mono(volts::sanitize(in[0]->mono() * in[1]->mono() * 0.1f));
    }
};

/// Average of four inputs.
class Mix4 : public Module {
public:
    Mix4() : Module(4, 1, 0) {}
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *out) override {
        out[0].set_mono((in[0]->mono() + in[1]->mono() + in[2]->mono() + in[3]->mono()) * 0.25f);
    }
};

/// Passes its input through unchanged (a utility; one sample of cable delay).
class Thru : public Module {
public:
    Thru() : Module(1, 1, 0) {}
    void process(const ProcessArgs &, const PolyVoltage *const *in, PolyVoltage *out) override {
        out[0] = *in[0];
        if (out[0].channels == 0) out[0].channels = 1;
    }
};

}  // namespace stubby::modules
