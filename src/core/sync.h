#pragma once
// The sync point (spec master-clock "The game reads audio time without locks"; spike
// 1.6). Each block the audio thread records (first frame, wall time of the render).
// Drivers like PulseAudio render steps back to back, so a raw render time can be up to
// a block early; the just-in-time estimate is the upper envelope of
// (render time − frame time) over the last 16 renders, accurate to ~0.4 ms in the
// spike. Published through a seqlock: the reader never blocks the audio thread.

#include <atomic>
#include <cstdint>

namespace stubby {

class SyncPoint {
public:
    /// Audio thread, once per block.
    void update(uint64_t frame, int64_t now_ns, double sample_rate) {
        const double offset = static_cast<double>(now_ns) - static_cast<double>(frame) * 1e9 / sample_rate;
        offsets_[n_++ % kWindow] = offset;
        double best = offset;
        const uint64_t count = n_ < kWindow ? n_ : kWindow;
        for (uint64_t i = 0; i < count; ++i) best = offsets_[i] > best ? offsets_[i] : best;
        seq_.fetch_add(1, std::memory_order_acq_rel);  // odd: writing
        offset_ns_.store(best, std::memory_order_relaxed);
        sample_rate_.store(sample_rate, std::memory_order_relaxed);
        valid_.store(true, std::memory_order_relaxed);
        seq_.fetch_add(1, std::memory_order_release);  // even: done
    }
    /// Any thread: the predicted wall time (same clock as now_ns) at which `frame` is
    /// heard, adding the output latency. False until the first update.
    bool predict_ns(uint64_t frame, int64_t output_latency_ns, int64_t &out) const {
        double offset = 0, sr = 0;
        bool valid = false;
        for (;;) {
            const uint32_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1u) continue;
            offset = offset_ns_.load(std::memory_order_relaxed);
            sr = sample_rate_.load(std::memory_order_relaxed);
            valid = valid_.load(std::memory_order_relaxed);
            if (seq_.load(std::memory_order_acquire) == s0) break;
        }
        if (!valid || sr <= 0) return false;
        out = static_cast<int64_t>(static_cast<double>(frame) * 1e9 / sr + offset) + output_latency_ns;
        return true;
    }
private:
    static constexpr uint64_t kWindow = 16;
    double offsets_[kWindow] = {};
    uint64_t n_ = 0;
    std::atomic<uint32_t> seq_{0};
    std::atomic<double> offset_ns_{0.0};
    std::atomic<double> sample_rate_{48000.0};
    std::atomic<bool> valid_{false};
};

}  // namespace stubby
