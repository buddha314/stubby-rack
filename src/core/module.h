#pragma once
// The module interface (spec synth-modules "Module and port model", modular-rack).
// Modules are built and destroyed on the main thread; the rack only calls process()
// on the audio thread. process() must be realtime-safe: no allocation, locks, I/O or
// exceptions, and outputs are sanitised (NaN → 0 V) by the module.

#include <cstdint>
#include <vector>

#include "core/events.h"
#include "core/transport.h"
#include "core/voltage.h"

namespace stubby {

struct ProcessArgs {
    float sample_rate = 48000.0f;
    float sample_time = 1.0f / 48000.0f;
    uint64_t frame = 0;                    // master-clock sample being rendered
    const Transport *transport = nullptr;  // the master transport (never null in a rack)
    EventSink *events = nullptr;           // report ticks with their sample times
    int handle = -1;                       // this module's handle (for its events)
};

class Module {
public:
    Module(int inputs, int outputs, int params)
        : num_inputs_(inputs), num_outputs_(outputs), params_(static_cast<size_t>(params), 0.0f) {}
    virtual ~Module() = default;

    int num_inputs() const { return num_inputs_; }
    int num_outputs() const { return num_outputs_; }
    int num_params() const { return static_cast<int>(params_.size()); }

    /// Audio thread. `in[k]` is input k as of the previous sample (never null;
    /// kUnconnected when unpatched). Write every output.
    virtual void process(const ProcessArgs &args, const PolyVoltage *const *in, PolyVoltage *out) = 0;

    /// Audio thread (via SetParam). Parameters are plain values; smoothing is
    /// rack-control's job (task 8.1).
    void set_param(int index, float value) {
        if (index >= 0 && index < num_params()) params_[static_cast<size_t>(index)] = value;
    }
    float param(int index) const { return params_[static_cast<size_t>(index)]; }

private:
    int num_inputs_, num_outputs_;
    std::vector<float> params_;  // sized at construction, never resized
};

}  // namespace stubby
