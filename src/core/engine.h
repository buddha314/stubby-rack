#pragma once
// The engine (spec audio-engine): a main-thread controller and the audio-thread render,
// joined only by lock-free single-reader/single-writer FIFOs (CHOC).
//
// Main thread: queue commands during a frame (add/remove module, connect/disconnect,
// set a parameter, assign a stem), then commit() once per frame. The controller
// validates every command against its own bookkeeping (module IDs are assigned by the
// game), resolves IDs to port slots, and rejects bad commands as events naming them;
// the rest of the batch still applies. A committed batch lands on one sample: the
// first sample of the next block rendered after it arrives.
//
// Audio thread: render(frames) applies every complete batch, renders the rack, and
// advances the sample counter (the master clock). It never allocates: modules are
// constructed by the caller and removed ones go back through a return FIFO, to be
// deleted on the main thread by collect().

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <limits>  // CHOC's FIFO header uses std::numeric_limits without including it

#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "core/rack.h"

namespace stubby {

using ModuleId = uint32_t;

struct Event {
    enum class Kind : uint8_t { Rejected };
    Kind kind = Kind::Rejected;
    uint64_t sample = 0;  // master-clock sample the event refers to
    ModuleId module = 0;
    std::string reason;   // main-thread events only (rejections are built there)
};

struct EngineConfig {
    float sample_rate = 48000.0f;
    RackCapacity capacity;
    int fifo_commands = 16384;
};

class Engine {
public:
    explicit Engine(const EngineConfig &config = {});
    ~Engine();

    // ---- main thread ------------------------------------------------------------
    /// Each returns false (and records a Rejected event) if the command is invalid.
    bool add_module(ModuleId id, std::unique_ptr<Module> module);
    bool remove_module(ModuleId id);
    bool connect(ModuleId from, int output, ModuleId to, int input);
    bool disconnect(ModuleId to, int input);
    bool set_param(ModuleId id, int param, float value);
    bool set_stem(int stem, ModuleId id, int output);
    bool clear_stem(int stem);
    /// Sends this frame's commands as one batch. False if the FIFO is full; the batch
    /// stays pending and is retried on the next commit.
    bool commit();
    /// Frees modules the audio thread has removed. Call once per frame.
    void collect();
    /// Events since the last call.
    std::vector<Event> take_events();
    bool has_module(ModuleId id) const { return modules_.count(id) != 0; }

    // ---- audio thread -------------------------------------------------------------
    /// Applies complete batches, then renders `frames` samples (≤ capacity.max_block).
    void render(int frames);
    const float *stem(int s) const { return rack_.stem(s); }
    int stem_count() const { return config_.capacity.stems; }
    /// Stems 0 .. stem_count() - 2 are emitters (positioned by the game). The last is
    /// the non-spatial UI stem: UI sounds, unaffected by the pause mute (task 4.6).
    int ui_stem() const { return config_.capacity.stems - 1; }

    // ---- any thread ---------------------------------------------------------------
    /// The master clock: samples rendered so far.
    uint64_t frame() const { return frame_.load(std::memory_order_acquire); }
    float sample_rate() const { return config_.sample_rate; }

private:
    struct Command {
        enum class Op : uint8_t { Add, Remove, Connect, Disconnect, SetParam, SetStem, EndBatch };
        Op op = Op::EndBatch;
        int handle = 0, a = 0, b = 0;
        float value = 0.0f;
        Module *module = nullptr;
    };
    struct ModuleRecord {
        int handle;
        int in_base, out_base, inputs, outputs;
    };

    void reject(ModuleId id, const std::string &reason);
    int alloc_range(std::vector<bool> &used, int count);
    void free_range(std::vector<bool> &used, int base, int count);
    void apply(const Command &c);

    EngineConfig config_;
    Rack rack_;
    std::atomic<uint64_t> frame_{0};

    // Main-thread bookkeeping.
    std::unordered_map<ModuleId, ModuleRecord> modules_;
    std::vector<int> free_handles_;
    std::vector<bool> in_used_, out_used_;
    std::unordered_map<int, int> cables_;  // input slot → output slot
    std::vector<Command> pending_;
    std::vector<Event> events_;

    // Crossing threads.
    choc::fifo::SingleReaderSingleWriterFIFO<Command> commands_;
    choc::fifo::SingleReaderSingleWriterFIFO<Module *> returned_;
    // Audio-thread staging for a batch whose end hasn't arrived yet.
    std::vector<Command> staged_;
    int staged_count_ = 0;
};

}  // namespace stubby
