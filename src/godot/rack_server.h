#pragma once
// RackServer: the engine singleton scripts talk to (design D6). For now it only
// reports its version and exposes the core's voltage helpers, which proves the
// extension loads and the core links.

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/stem_renderer.h"

namespace godot {

class RackServer : public Object {
    GDCLASS(RackServer, Object)

public:
    static RackServer *get_singleton() { return singleton; }
    RackServer();
    ~RackServer() override;

    String get_version() const;
    double voct_to_hz(double volts) const;

    // Spike 1.2-1.4: the stem renderer and measurements (design D4).
    stubby::StemRenderer &renderer() { return *renderer_; }
    void spike_configure(double tone_hz, double tone_volts, int stems);
    /// Spike 1.5: render stubby-music's feedback patch of about `modules` modules (0 = tone).
    int spike_use_rack(int modules);
    int64_t get_frontier() const;
    Dictionary get_spike_stats() const;
    Array get_trace() const;
    PackedInt64Array get_sync_log() const;
    /// The renderer's clock (steady, ns): the time base of the sync points.
    int64_t now_ns() const;
    void reset_spike_stats();
    /// Audio thread, once per driver mix: step interval and audio-thread CPU per step.
    void measure_step(int64_t now_ns, int64_t cpu_ns, int frames);
    void add_stem_ns(int64_t ns) { stem_ns_.fetch_add(ns, std::memory_order_relaxed); }

protected:
    static void _bind_methods();

private:
    static RackServer *singleton;
    std::unique_ptr<stubby::StemRenderer> renderer_;
    std::unique_ptr<stubby::spike::Rack> rack_;
    // Audio-thread measurements, read on the main thread.
    int64_t last_step_ns_ = 0, last_cpu_ns_ = 0;
    std::atomic<int64_t> steps_{0}, late_steps_{0}, max_interval_ns_{0}, cpu_ns_total_{0}, max_cpu_ns_{0};
    std::atomic<int64_t> stem_ns_{0};
    std::atomic<int64_t> block_frames_{0};
};

}  // namespace godot
