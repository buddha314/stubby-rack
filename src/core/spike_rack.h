#pragma once
// Spike 1.5 (wait-for-stubs add-rack-audio-engine): a port of stubby-music's
// spikes/spike_rack patch to the C++ core, to measure the VCV-style scheduler's cost.
// Every module runs once per sample; cables read the previous sample's outputs
// (double-buffered slots, a 1-sample delay), so feedback is legal. Modules use dynamic
// dispatch like the Rust spike (Box<dyn Module>), so ns per module-sample compares
// fairly. Oscillators come from DaisySP (MIT). Realtime-safe after build: render()
// allocates nothing.

#include <cstdint>
#include <memory>
#include <vector>

namespace stubby::spike {

constexpr int kMaxInputs = 4;
constexpr uint32_t kNone = 0xffffffffu;

struct Module {
    virtual ~Module() = default;
    virtual int num_inputs() const = 0;
    virtual int num_outputs() const = 0;
    /// One sample. `in` holds this module's inputs as of the previous sample.
    virtual void process(const float *in, float *out) = 0;
};

class Rack {
public:
    int add(std::unique_ptr<Module> m);
    /// Cable from (module, output) to (module, input). Any topology, cycles included.
    void cable(int from_module, int from_port, int to_module, int to_port);
    /// Render `frames` samples; stems_out is stem-major (stem s, frame i at s*frames+i).
    void render(int frames, float *stems_out);
    void tap_stem(int module, int port = 0) { stems_.push_back(out_base_[module] + port); }

    int module_count() const { return static_cast<int>(modules_.size()); }
    int cable_count() const;
    int stem_count() const { return static_cast<int>(stems_.size()); }

private:
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<int> in_base_, out_base_, n_in_, n_out_;
    std::vector<uint32_t> in_src_;
    std::vector<float> slots_[2];
    int parity_ = 0;
    std::vector<int> stems_;
};

/// The spike's patch of about `n` modules: voices of clock → env, lfo → vco.voct,
/// vco → vca ← env, vca → vco.fm (feedback), every 4 voices summed by a mixer.
/// The first 32 VCA outputs are emission stems.
std::unique_ptr<Rack> build_patch(int n, float sample_rate);

}  // namespace stubby::spike
