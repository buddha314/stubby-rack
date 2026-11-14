#pragma once
// Step sequencers (wait-for-stubs add-snail-shell-sequencer; spec step-sequencer).
// Written by hand: the logic is a few lines of edge handling, and the references
// (Peaks mini_sequencer, MIT; ML_modules, BSD-3) are bound to their own CV formats
// and UIs (design D3). Behaviour follows hardware step sequencers (Intellijel Steppy,
// KORG SQ-1): clock in, a gate and a CV per step.

#include <cstdint>

#include "core/module.h"
#include "core/volts.h"

namespace stubby::modules {

/// An N-step gate and CV sequencer (N = kMaxSteps at most; param 0 `steps`).
/// Params: 0 `steps`, then `cv_1…cv_N` (volts), then `gate_1…gate_N` (0 or 1).
/// In 0: clock. A rising edge (Schmitt) advances one step, wrapping after `steps`.
/// In 1: reset. A rising edge makes the next clock edge play step 1.
/// Out 0: gate, the clock input's level while the current step's gate is on, else 0.
/// Out 1: cv, the current step's voltage (held between edges).
/// Each advance is reported as a Tick event whose index is the step (0-based), on the
/// sample the edge is seen (one sample after the clock's, the cable delay).
/// Before the first clock edge no step is active: gate low, cv at step 1's voltage.
class StepSequencer : public Module {
public:
    static constexpr int kMaxSteps = 16;
    explicit StepSequencer(int steps = 4) : Module(2, 2, 1 + 2 * kMaxSteps) { set_param(0, static_cast<float>(steps)); }

    static int cv_param(int step) { return 1 + step; }
    static int gate_param(int step) { return 1 + kMaxSteps + step; }

    void process(const ProcessArgs &args, const PolyVoltage *const *in, PolyVoltage *out) override {
        const int steps = count();
        if (reset_.process(in[1]->mono())) step_ = -1;
        const float clock = in[0]->mono();
        if (clock_.process(clock)) {
            step_ = (step_ + 1) % steps;
            if (args.events) args.events->push({AudioEvent::Kind::Tick, args.handle, args.frame, static_cast<int64_t>(step_), 0});
        }
        if (step_ >= steps) step_ = steps - 1;  // the step count shrank
        const int at = step_ < 0 ? 0 : step_;
        const bool on = step_ >= 0 && param(gate_param(at)) >= 0.5f;
        out[0].set_mono(on ? volts::sanitize(clock) : 0.0f);
        out[1].set_mono(volts::sanitize(param(cv_param(at))));
    }

    /// The current step (0-based), or -1 before the first clock edge (tests).
    int step() const { return step_; }

private:
    int count() const {
        const int n = static_cast<int>(param(0));
        return n < 1 ? 1 : (n > kMaxSteps ? kMaxSteps : n);
    }
    volts::SchmittTrigger clock_, reset_;
    int step_ = -1;
};

}  // namespace stubby::modules
