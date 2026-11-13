#pragma once
// The audio-thread rack (spec modular-rack). Every module runs once per sample;
// outputs are double-buffered, so every cable delivers the previous sample's value
// (a 1-sample delay), order never matters, and any topology including feedback is
// valid. Capacities are fixed at construction: the audio thread never allocates.
//
// The rack is driven only by already-resolved operations from Engine (engine.h): the
// main-thread controller owns ID bookkeeping and port-slot allocation, so the rack
// just writes into its arrays.

#include <cstdint>
#include <vector>

#include "core/module.h"
#include "core/voltage.h"

namespace stubby {

struct RackCapacity {
    int modules = 1024;
    int inputs = 8192;
    int outputs = 8192;
    int stems = 128;
    int max_block = 4096;
    int max_module_inputs = 32;
};

class Rack {
public:
    explicit Rack(const RackCapacity &cap = {});

    // Audio thread: structure edits (resolved by the controller).
    void add(int handle, Module *module, int in_base, int out_base);
    /// Removes the module and returns it (for freeing on the main thread).
    Module *remove(int handle);
    void connect(int input_slot, int output_slot) { in_src_[static_cast<size_t>(input_slot)] = output_slot; }
    void disconnect(int input_slot) { in_src_[static_cast<size_t>(input_slot)] = -1; }
    void set_param(int handle, int param, float value);
    /// Stem `stem` taps output slot `output_slot` (-1 = silent).
    void set_stem(int stem, int output_slot) {
        stem_src_[static_cast<size_t>(stem)] = output_slot;
        if (stem >= stem_high_) stem_high_ = stem + 1;
    }

    /// Audio thread: render `frames` samples starting at master frame `first_frame`
    /// into the stem buffers (volts, mono: channel 0).
    void render(int frames, uint64_t first_frame, float sample_rate);

    /// Stem `s`'s samples for the last render (frames long).
    const float *stem(int s) const { return &stems_[static_cast<size_t>(s) * cap_.max_block]; }
    int module_count() const { return active_count_; }
    const RackCapacity &capacity() const { return cap_; }

private:
    struct Slot {
        Module *module = nullptr;
        int in_base = 0, out_base = 0;
        int position = -1;  // index in active_
    };
    RackCapacity cap_;
    std::vector<Slot> slots_;           // by handle
    std::vector<int> active_;           // handles of live modules, dense
    int active_count_ = 0;
    std::vector<int> in_src_;           // input slot → output slot, or -1
    std::vector<PolyVoltage> out_[2];   // double-buffered output slots
    int parity_ = 0;
    std::vector<int> stem_src_;         // stem → output slot, or -1
    int stem_high_ = 0;                 // stems [0, stem_high_) have ever been assigned
    std::vector<float> stems_;          // stem-major, max_block per stem
    std::vector<const PolyVoltage *> in_ptrs_;  // scratch: one module's inputs
};

}  // namespace stubby
