#include "core/driver.h"

#include <chrono>

namespace stubby {

int64_t EngineDriver::steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EngineDriver::read(int stem, uint64_t &reader_step, float *out, int frames, float gain) {
    const int64_t now_ns = clock_();
    stats.reads.fetch_add(1, std::memory_order_relaxed);
    const bool gap = step_id_ == 0 || now_ns - last_read_ns_ > kStepGapNs;
    const bool repeat = !gap && reader_step == step_id_;
    if (gap || repeat) {
        if (repeat) stats.repeat_steps.fetch_add(1, std::memory_order_relaxed);
        ++step_id_;
        step_start_ = engine_.frame();
        step_frames_ = frames;
        engine_.render(frames, now_ns);
        stats.renders.fetch_add(1, std::memory_order_relaxed);
    }
    if (reader_step == kUnset) stats.joins.fetch_add(1, std::memory_order_relaxed);
    else if (step_id_ > reader_step + 1) stats.skips.fetch_add(1, std::memory_order_relaxed);
    reader_step = step_id_;
    const bool valid = stem >= 0 && stem < engine_.stem_count();
    const float *src = valid ? engine_.stem(stem) : nullptr;
    const int have = frames < step_frames_ ? frames : step_frames_;
    int i = 0;
    if (src)
        for (; i < have; ++i) out[i] = src[i] * gain;
    for (; i < frames; ++i) out[i] = 0.0f;
    last_read_ns_ = clock_();  // the gap to the next read starts here
}

}  // namespace stubby
