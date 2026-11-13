#pragma once
// The engine driver (design D4, task 5.4): joins Godot's mix to the Engine. Every
// playback - the always-on driver and each emitter's stem - calls read() once per
// Godot mix step, in whatever order Godot mixes them.
//
// Godot exposes no mix counter, so the driver detects steps itself (as the spike's
// StemRenderer proved): a new step begins when the gap since the end of the previous
// read exceeds kStepGapNs, or when a reader that already read in the current step
// reads again. The first read of a step renders one Engine block; every read in the
// step copies that block, so all emitters stay sample-aligned whatever the order.
//
// Audio thread only (Godot mixes all playbacks on one thread). No allocation, locks or
// Godot objects; the stats are atomics the main thread may read.

#include <atomic>
#include <cstdint>

#include "core/engine.h"

namespace stubby {

class EngineDriver {
public:
    // A quarter of a 512-frame block (spike 1.2: steps are ~10.7 ms apart, a step's reads
    // microseconds apart; PulseAudio pairs steps back to back, the repeat rule splits them).
    static constexpr int64_t kStepGapNs = 2'666'667;
    static constexpr uint64_t kUnset = ~uint64_t(0);
    using Clock = int64_t (*)();

    explicit EngineDriver(Engine &engine) : engine_(engine) {}
    /// Monotonic nanoseconds (steady_clock, CLOCK_MONOTONIC on Linux).
    static int64_t steady_ns();
    void set_clock(Clock clock) { clock_ = clock; }

    /// Copies stem `stem` (volts × `gain`) of the current step's block into out; a stem
    /// out of range (the driver passes -1) reads silence. `reader_step` is the reader's
    /// own state, starting at kUnset.
    void read(int stem, uint64_t &reader_step, float *out, int frames, float gain = 1.0f);
    /// The current step's first frame.
    uint64_t step_start() const { return step_start_; }

    struct Stats {
        std::atomic<uint64_t> renders{0};       // blocks rendered (= mix steps)
        std::atomic<uint64_t> reads{0};
        std::atomic<uint64_t> skips{0};         // a reader missed one or more steps
        std::atomic<uint64_t> repeat_steps{0};  // steps found by a repeated reader
        std::atomic<uint64_t> joins{0};         // readers' first reads
    };
    Stats stats;

private:
    Engine &engine_;
    Clock clock_ = &steady_ns;
    uint64_t step_id_ = 0;  // 0 = none yet
    uint64_t step_start_ = 0;
    int step_frames_ = 0;
    int64_t last_read_ns_ = 0;
};

}  // namespace stubby
