#pragma once
// RackServer: the engine singleton scripts talk to (design D6), like AudioServer.
// It owns the core Engine and its driver. Every call queues a command on the main
// thread; the frame's batch is committed once per frame by the RackDriver node (which
// RackServer adds to the scene tree on first use), and engine events come back as
// signals carrying their master-clock sample.

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/driver.h"
#include "core/engine.h"
#include "core/sample_bank.h"

namespace godot {

class RackServer : public Object {
    GDCLASS(RackServer, Object)

public:
    enum When { WHEN_NOW, WHEN_SAMPLE, WHEN_NEXT_BEAT, WHEN_NEXT_BAR, WHEN_NEXT_DIVISION };

    static RackServer *get_singleton() { return singleton; }
    RackServer();
    ~RackServer() override;

    String get_version() const;
    double voct_to_hz(double volts) const;

    // Modules and patching (main thread). IDs are the game's; new_module_id() hands out
    // fresh ones.
    int64_t new_module_id();
    bool add_module(int64_t id, const String &kind, int64_t seed = 1);
    bool remove_module(int64_t id);
    bool connect_ports(int64_t from, int output, int64_t to, int input);
    bool disconnect_port(int64_t to, int input);
    bool set_param(int64_t id, int param, double value);
    bool set_stem(int stem, int64_t id, int output);
    bool clear_stem(int stem);
    /// An emitter stem not in use, or -1 when all are taken.
    int allocate_stem();
    void release_stem(int stem);
    int get_ui_stem() const { return engine_->ui_stem(); }
    int get_stem_count() const { return engine_->stem_count(); }
    Array get_module_kinds() const;

    // Time and the transport.
    /// Commands queued after this take effect at `when` (WHEN_SAMPLE: `value` is the
    /// sample; WHEN_NEXT_DIVISION: divisions per beat). Lasts until schedule(WHEN_NOW)
    /// or the end of the frame. Returns the action id late notices will carry.
    int64_t schedule(int when, int64_t value = 0);
    bool set_tempo(double bpm);
    bool set_meter(int beats_per_bar);
    bool start_transport();
    bool stop_transport();
    bool set_paused(bool paused);
    int64_t now_sample() const;
    Dictionary get_position() const;
    double get_sample_rate() const;
    /// The Time.get_ticks_usec() at which `sample` is heard (the sync point plus the
    /// output latency), or -1 before audio has started.
    int64_t sample_to_ticks_usec(int64_t sample) const;

    // One-shot sounds through the rack's sampler (task 5.5).
    /// Decodes `stream` (any AudioStream) to mono at the mix rate; cached per resource.
    int load_sample(const Ref<AudioStream> &stream);
    /// Plays sample `sample_id` at `when` on the UI stem's sampler (or `sampler`, a
    /// sampler module's id). Returns the action id, or -1 if rejected.
    int64_t play_sample(int sample_id, int when = WHEN_NOW, int64_t value = 0, double gain = 1.0, int64_t sampler = -1);

    // The frame loop.
    /// Sends this frame's batch. RackDriver calls it once per frame; scripts may too.
    bool commit();
    /// Called by RackDriver each frame: commit, free removed modules, emit events.
    void frame_update();
    /// Adds the RackDriver node to the scene tree if it isn't there yet.
    void ensure_driver();
    Dictionary get_driver_stats() const;

    // For the playbacks (audio thread): the driver, never null while the server lives.
    stubby::EngineDriver *driver() { return driver_.get(); }

protected:
    static void _bind_methods();

private:
    static RackServer *singleton;
    std::unique_ptr<stubby::SampleBank> bank_;
    std::unique_ptr<stubby::Engine> engine_;
    std::unique_ptr<stubby::EngineDriver> driver_;
    int64_t next_id_ = 1;
    int64_t ui_sampler_ = 0;
    float trigger_count_ = 0.0f;
    std::unordered_map<int64_t, int> sampler_voice_;  // round-robin per sampler module
    std::unordered_map<uint64_t, int> sample_cache_;  // AudioStream instance id → bank id
    std::vector<bool> stem_used_;
    uint64_t driver_node_ = 0;  // ObjectID of the RackDriver node
};

}  // namespace godot

VARIANT_ENUM_CAST(RackServer::When);
