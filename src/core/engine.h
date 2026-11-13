#pragma once
// The engine (spec audio-engine, master-clock): a main-thread controller and the
// audio-thread render, joined only by lock-free single-reader/single-writer FIFOs.
//
// Main thread: queue commands during a frame (modules, cables, parameters, stems, the
// transport, pause), then commit() once per frame. The controller validates every
// command against its own bookkeeping (module IDs are assigned by the game), resolves
// IDs to port slots, and rejects bad commands as events naming them; the rest of the
// batch still applies. Commands take effect at the start of the next block, or at a
// scheduled time: at(When) attaches a schedule to the commands that follow it (an
// absolute sample, or the next beat, bar or beat division).
//
// Audio thread: render(frames) applies complete batches, splits the block at every
// scheduled action so it lands on its exact sample, renders the rack, fades the
// emitter stems when paused, and advances the sample counter: the master clock. An
// action whose time has already been rendered applies at the block start and is
// reported late. It never allocates: modules are constructed by the caller, and
// removed ones go back through a return FIFO to be deleted by collect().

#include <atomic>
#include <cstdint>
#include <limits>  // CHOC's FIFO header uses std::numeric_limits without including it
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "core/events.h"
#include "core/rack.h"
#include "core/sync.h"
#include "core/transport.h"

namespace stubby {

using ModuleId = uint32_t;
using ActionId = uint32_t;

/// When a command takes effect.
struct When {
    enum class Kind : uint8_t { Now, Sample, NextBeat, NextBar, NextDivision };
    Kind kind = Kind::Now;
    uint64_t sample = 0;  // Sample
    int division = 1;     // NextDivision: divisions per beat

    static When now() { return {}; }
    static When at_sample(uint64_t s) { return {Kind::Sample, s, 1}; }
    static When next_beat() { return {Kind::NextBeat, 0, 1}; }
    static When next_bar() { return {Kind::NextBar, 0, 1}; }
    static When next_division(int per_beat) { return {Kind::NextDivision, 0, per_beat > 0 ? per_beat : 1}; }
};

struct Event {
    enum class Kind : uint8_t { Rejected, Tick, Late };
    Kind kind = Kind::Rejected;
    uint64_t sample = 0;  // master-clock sample (Tick: when; Late: when applied)
    ModuleId module = 0;  // Rejected, Tick
    int64_t value = 0;    // Tick: tick index; Late: samples missed
    ActionId action = 0;  // Late
    std::string reason;   // Rejected
};

/// Musical position, read lock-free (spec master-clock "Musical transport").
struct Position {
    uint64_t frame = 0;
    double beats = 0.0;  // since the transport started
    int64_t bar = 1;     // 1-based
    int beat = 1;        // 1-based within the bar
    double phase = 0.0;  // 0..1 within the beat
    double bpm = 0.0;
    int beats_per_bar = 4;
    bool running = true;
};

struct EngineConfig {
    float sample_rate = 48000.0f;
    double tempo_bpm = 1.0;  // project setting audio/master_clock/tempo_bpm
    int beats_per_bar = 4;   // project setting audio/master_clock/beats_per_bar
    RackCapacity capacity;
    int fifo_commands = 16384;
    int fifo_events = 16384;
    int scheduled_capacity = 4096;
    float pause_fade_seconds = 0.03f;  // 20-50 ms per spec
};

class Engine {
public:
    explicit Engine(const EngineConfig &config = {});
    ~Engine();

    // ---- main thread ------------------------------------------------------------
    /// Commands queued after this take effect at `when` (When::now() ends scheduling).
    /// Returns the action id that late notices for them will carry.
    ActionId at(When when);
    /// Each returns false (and records a Rejected event) if the command is invalid.
    bool add_module(ModuleId id, std::unique_ptr<Module> module);
    bool remove_module(ModuleId id);
    bool connect(ModuleId from, int output, ModuleId to, int input);
    bool disconnect(ModuleId to, int input);
    bool set_param(ModuleId id, int param, float value);
    bool set_stem(int stem, ModuleId id, int output);
    bool clear_stem(int stem);
    bool set_tempo(double bpm);
    bool set_meter(int beats_per_bar);
    bool start_transport();
    bool stop_transport();
    /// A device change: the transport keeps its tempo and musical position.
    bool set_sample_rate(double sample_rate);
    /// Pause mutes the emitter stems with a short fade; the clock and the UI stem run on.
    bool set_paused(bool paused);
    /// Sends this frame's commands as one batch. False if the FIFO is full; the batch
    /// stays pending and is retried on the next commit.
    bool commit();
    /// Frees modules the audio thread has removed. Call once per frame.
    void collect();
    /// Events since the last call: rejections, clock ticks, late notices.
    std::vector<Event> take_events();
    bool has_module(ModuleId id) const { return modules_.count(id) != 0; }

    // ---- audio thread -------------------------------------------------------------
    /// Applies complete batches, then renders `frames` samples (≤ capacity.max_block).
    /// `now_ns` (a monotonic wall time; < 0 to skip) feeds the sync point.
    void render(int frames, int64_t now_ns = -1);
    const float *stem(int s) const { return rack_.stem(s); }
    int stem_count() const { return config_.capacity.stems; }
    /// Stems 0 .. stem_count() - 2 are emitters (positioned by the game). The last is
    /// the non-spatial UI stem: UI sounds, unaffected by the pause mute.
    int ui_stem() const { return config_.capacity.stems - 1; }

    // ---- any thread ---------------------------------------------------------------
    /// The master clock: samples rendered so far.
    uint64_t frame() const { return frame_.load(std::memory_order_acquire); }
    float sample_rate() const { return static_cast<float>(sample_rate_.load(std::memory_order_acquire)); }
    /// The musical position at the end of the last rendered block.
    Position position() const;
    /// The predicted wall time at which `frame` is heard (see SyncPoint).
    bool predict_ns(uint64_t frame, int64_t output_latency_ns, int64_t &out) const {
        return sync_.predict_ns(frame, output_latency_ns, out);
    }
    uint64_t dropped_events() const { return events_.dropped(); }

private:
    struct Command {
        enum class Op : uint8_t {
            Add, Remove, Connect, Disconnect, SetParam, SetStem,
            SetTempo, SetMeter, Start, Stop, SetSampleRate, SetPaused, EndBatch
        };
        Op op = Op::EndBatch;
        int handle = 0, a = 0, b = 0;
        double value = 0.0;
        Module *module = nullptr;
        When when;
        ActionId action = 0;
        double target_beats = 0.0;  // audio thread: a quantized schedule's musical time
    };
    struct ModuleRecord {
        int handle;
        int in_base, out_base, inputs, outputs;
    };

    void reject(ModuleId id, const std::string &reason);
    void queue(Command c);
    int alloc_range(std::vector<bool> &used, int count);
    void free_range(std::vector<bool> &used, int base, int count);
    // Audio thread.
    void receive(const Command &c, uint64_t at_frame);
    void apply(const Command &c, uint64_t at_frame);
    uint64_t due_frame(const Command &c) const;
    void publish_position();

    EngineConfig config_;
    Rack rack_;
    std::atomic<uint64_t> frame_{0};
    std::atomic<double> sample_rate_;

    // Main-thread bookkeeping.
    std::unordered_map<ModuleId, ModuleRecord> modules_;
    std::vector<int64_t> handle_ids_;  // handle → module id, or -1
    std::vector<int> free_handles_;
    std::vector<bool> in_used_, out_used_;
    std::unordered_map<int, int> cables_;  // input slot → output slot
    std::vector<Command> pending_;
    std::vector<Event> events_main_;
    When when_;
    ActionId action_ = 0, next_action_ = 1;

    // Crossing threads.
    choc::fifo::SingleReaderSingleWriterFIFO<Command> commands_;
    choc::fifo::SingleReaderSingleWriterFIFO<Module *> returned_;
    EventSink events_;
    SyncPoint sync_;
    std::atomic<uint32_t> pos_seq_{0};
    std::atomic<uint64_t> pos_frame_{0};
    std::atomic<double> pos_beats_{0.0}, pos_bpm_{0.0};
    std::atomic<int> pos_bpb_{4};
    std::atomic<bool> pos_running_{true};

    // Audio thread.
    Transport transport_;
    std::vector<Command> staged_;
    int staged_count_ = 0;
    std::vector<Command> scheduled_;
    int scheduled_count_ = 0;
    bool paused_ = false;
    float gain_ = 1.0f;
};

}  // namespace stubby
