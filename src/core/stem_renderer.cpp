#include "core/stem_renderer.h"

#include <chrono>
#include <cmath>

namespace stubby {

StemRenderer::StemRenderer(int max_stems)
    : max_stems_(max_stems), used_stems_(max_stems), ring_(static_cast<size_t>(max_stems) * kRingFrames, 0.0f),
      rack_out_(static_cast<size_t>(32) * 4096, 0.0f) {}

int64_t StemRenderer::steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void StemRenderer::configure(float sample_rate, float tone_hz, float tone_volts, int stems) {
    phase_step_ = 2.0 * M_PI * tone_hz / sample_rate;
    amplitude_ = tone_volts;
    used_stems_ = (stems < 0 || stems > max_stems_) ? max_stems_ : stems;
}

void StemRenderer::render_to(uint64_t end) {
    uint64_t at = frontier_.load(std::memory_order_relaxed);
    if (end <= at) return;
    if (rack_ && rack_->stem_count() > 0 && end - at <= 4096) {
        const int frames = static_cast<int>(end - at);
        const int rack_stems = rack_->stem_count() < 32 ? rack_->stem_count() : 32;
        rack_->render(frames, rack_out_.data());
        for (int i = 0; i < frames; ++i) {
            const size_t slot = (at + static_cast<uint64_t>(i)) & (kRingFrames - 1);
            for (int s = 0; s < used_stems_; ++s)
                ring_[static_cast<size_t>(s) * kRingFrames + slot] = rack_out_[static_cast<size_t>(s % rack_stems) * frames + i];
        }
        frontier_.store(end, std::memory_order_release);
        return;
    }
    // The spike's "rack": one shared test tone written to every stem. The real core
    // renders the rack per sample here and writes each emitter's own stem.
    for (; at < end; ++at) {
        const float v = amplitude_ * static_cast<float>(std::sin(phase_));
        phase_ += phase_step_;
        if (phase_ > 2.0 * M_PI) phase_ -= 2.0 * M_PI;
        const size_t slot = at & (kRingFrames - 1);
        for (int s = 0; s < used_stems_; ++s) ring_[static_cast<size_t>(s) * kRingFrames + slot] = v;
    }
    frontier_.store(end, std::memory_order_release);
}

void StemRenderer::read(int stem, uint64_t &reader_step, float *out, int frames) {
    const int64_t now_ns = clock_();
    stats.reads.fetch_add(1, std::memory_order_relaxed);
    {
        const uint64_t n = trace_n.load(std::memory_order_relaxed);
        trace[n % kTrace] = Trace{stem, reader_step == kUnset ? ~uint64_t(0) : reader_step,
                                  frontier_.load(std::memory_order_relaxed), frames, now_ns};
        trace_n.store(n + 1, std::memory_order_relaxed);
    }
    const bool gap = step_id_ == 0 || now_ns - last_read_ns_ > kStepGapNs;
    const bool repeat = !gap && reader_step == step_id_;
    if (gap || repeat) {
        if (repeat) stats.repeat_steps.fetch_add(1, std::memory_order_relaxed);
        ++step_id_;
        step_start_ = frontier_.load(std::memory_order_relaxed);
        render_to(step_start_ + static_cast<uint64_t>(frames));
        stats.renders.fetch_add(1, std::memory_order_relaxed);
        const uint64_t n = sync_n.load(std::memory_order_relaxed);
        if (n < kSyncLog) {
            sync_sample[n] = step_start_;
            sync_ns[n] = now_ns;
            sync_n.store(n + 1, std::memory_order_release);
        }
    } else if (step_start_ + static_cast<uint64_t>(frames) > frontier_.load(std::memory_order_relaxed)) {
        render_to(step_start_ + static_cast<uint64_t>(frames));  // a longer request
    }
    if (reader_step == kUnset) stats.joins.fetch_add(1, std::memory_order_relaxed);
    else if (step_id_ > reader_step + 1) stats.skips.fetch_add(1, std::memory_order_relaxed);
    reader_step = step_id_;
    const bool valid = stem >= 0 && stem < used_stems_;
    const float *ring = valid ? &ring_[static_cast<size_t>(stem) * kRingFrames] : nullptr;
    for (int i = 0; i < frames; ++i)
        out[i] = ring ? ring[(step_start_ + static_cast<uint64_t>(i)) & (kRingFrames - 1)] : 0.0f;
    last_read_ns_ = clock_();  // the gap to the next read starts here
}

}  // namespace stubby
