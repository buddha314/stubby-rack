#pragma once
// Spike 1.2 (wait-for-stubs add-rack-audio-engine, design D4): the stem fan-out.
// One renderer owns the master sample frontier (the clock) and a ring of rendered
// frames per stem. Every playback - the always-on driver and each emitter's stem -
// calls read() once per Godot mix step, in whatever order Godot mixes them.
//
// Godot exposes no mix counter, so the renderer detects mix steps itself. A new step
// begins when (a) the gap since the previous read exceeds kStepGapNs (one step's reads
// arrive microseconds apart; steps are milliseconds apart), or (b) a reader that has
// already read in the current step reads again (proof that a new step began). The first
// read of a step renders the next block; every read in the step gets that same block, so
// all emitters stay sample-aligned by construction, whatever order Godot mixes them in.
// A reader that misses steps (its step id jumps by more than one) is counted as a skip:
// that's the audible failure the spike measures.
//
// Realtime-safe on the audio thread: no allocation (buffers sized in the constructor),
// no locks, no I/O. Main-thread readers use only the atomics.

#include <atomic>
#include <cstdint>
#include <vector>

#include "core/spike_rack.h"

namespace stubby {

class StemRenderer {
public:
    static constexpr int kRingFrames = 16384;  // power of two, about 340 ms at 48 kHz
    // A quarter of a 512-frame block. Real steps are ~10.7 ms apart (PulseAudio pairs them
    // back to back; the repeat rule splits those). 250 µs split one step in ~2,000 on a
    // preempted audio thread (10-minute run); a quarter block leaves room for preemption.
    static constexpr int64_t kStepGapNs = 2'666'667;

    explicit StemRenderer(int max_stems = 128);

    using Clock = int64_t (*)();
    /// Monotonic nanoseconds (steady_clock). Tests inject their own.
    static int64_t steady_ns();

    // Main thread, before anything plays (the spike only; the engine will use queues).
    void configure(float sample_rate, float tone_hz, float tone_volts, int stems = -1);
    void set_clock(Clock clock) { clock_ = clock; }
    /// Spike 1.5: render a rack instead of the test tone (main thread, before playing).
    /// Stem s reads the rack's stem s % rack->stem_count().
    void set_rack(spike::Rack *rack) { rack_ = rack; }

    // Audio thread.
    /// The frames rendered so far: the master clock.
    uint64_t frontier() const { return frontier_.load(std::memory_order_acquire); }
    /// Copies the current mix step's block of stem `stem` (volts, mono) into out.
    /// `reader_step` is the reader's own state: the id of the last step it read
    /// (start it at kUnset). Step gaps are measured from the end of the previous
    /// read, so render time never looks like a gap.
    void read(int stem, uint64_t &reader_step, float *out, int frames);
    /// The current step's first frame (for tests and the trace).
    uint64_t step_start() const { return step_start_; }

    static constexpr uint64_t kUnset = ~uint64_t(0);

    struct Stats {
        std::atomic<uint64_t> renders{0};          // blocks rendered (= mix steps)
        std::atomic<uint64_t> reads{0};            // read() calls
        std::atomic<uint64_t> skips{0};            // a reader missed one or more steps
        std::atomic<uint64_t> repeat_steps{0};     // steps found by a repeated reader, not the gap
        std::atomic<uint64_t> joins{0};            // readers' first reads
    };
    Stats stats;
    /// Spike 1.6: the sync point of every render (step's first sample, wall ns).
    static constexpr int kSyncLog = 1 << 17;
    uint64_t sync_sample[kSyncLog] = {};
    int64_t sync_ns[kSyncLog] = {};
    std::atomic<uint64_t> sync_n{0};
    /// Debug trace of recent reads (spike only): stem, pos, frontier before, frames, ns.
    struct Trace { int stem; uint64_t pos; uint64_t front; int frames; int64_t ns; };
    static constexpr int kTrace = 256;
    Trace trace[kTrace] = {};
    std::atomic<uint64_t> trace_n{0};
    int max_stems() const { return max_stems_; }

private:
    void render_to(uint64_t end);

    Clock clock_ = &steady_ns;
    spike::Rack *rack_ = nullptr;
    std::vector<float> rack_out_;  // stem-major scratch, sized in the constructor
    int max_stems_;
    int used_stems_;
    std::vector<float> ring_;  // max_stems_ × kRingFrames
    std::atomic<uint64_t> frontier_{0};
    uint64_t step_id_ = 0;      // current mix step; 0 = none yet
    uint64_t step_start_ = 0;   // its first frame
    int64_t last_read_ns_ = 0;
    double phase_ = 0.0;
    double phase_step_ = 0.0;
    float amplitude_ = 0.0f;
};

}  // namespace stubby
