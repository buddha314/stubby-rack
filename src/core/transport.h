#pragma once
// The master clock's musical transport (spec master-clock "Musical transport",
// "Tempo clocks are whole multiples of the master"). The sample counter is the time
// base; musical position is piecewise linear in samples. Every tempo, start/stop or
// sample-rate change re-anchors at the current sample, so the position never jumps.
// Tick times are computed exactly from the anchor (anchor + ceil(k·spb/N)), never by
// accumulating, so clocks never drift.

#include <cmath>
#include <cstdint>
#include <limits>

namespace stubby {

class Transport {
public:
    static constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
    /// Rounding tolerance in samples: a tick computed at 31999.9999999 lands on 32000.
    static constexpr double kEpsilon = 1e-6;

    double sample_rate() const { return sample_rate_; }
    double bpm() const { return bpm_; }
    int beats_per_bar() const { return beats_per_bar_; }
    bool running() const { return running_; }
    /// Bumped on every change, so clocks know to recompute their next tick.
    uint32_t version() const { return version_; }
    double samples_per_beat() const { return 60.0 * sample_rate_ / bpm_; }

    /// Beats since the transport started, at `frame` (held while stopped).
    double beats_at(uint64_t frame) const {
        if (!running_ || frame <= anchor_frame_) return anchor_beats_;
        return anchor_beats_ + static_cast<double>(frame - anchor_frame_) / samples_per_beat();
    }
    /// The first frame at which the position reaches `beats` (kNever if stopped and not
    /// already there).
    uint64_t frame_of(double beats) const {
        if (beats <= anchor_beats_) return anchor_frame_;
        if (!running_) return kNever;
        const double offset = (beats - anchor_beats_) * samples_per_beat();
        return anchor_frame_ + static_cast<uint64_t>(std::ceil(offset - kEpsilon));
    }
    /// Frame of tick `k` of a clock at `multiple`× the master: beats k / multiple,
    /// computed as k·spb/multiple so whole-number multiples stay exact.
    uint64_t frame_of_tick(uint64_t k, int multiple) const {
        if (!running_) return kNever;
        const double target = static_cast<double>(k) * samples_per_beat() / multiple;
        const double anchor = anchor_beats_ * samples_per_beat();
        if (target <= anchor) return anchor_frame_;
        return anchor_frame_ + static_cast<uint64_t>(std::ceil(target - anchor - kEpsilon));
    }
    /// The first tick index at or after `frame`'s position for a clock at `multiple`×.
    uint64_t first_tick_index(uint64_t frame, int multiple) const {
        const double ticks = beats_at(frame) * multiple;
        return static_cast<uint64_t>(std::ceil(ticks - 1e-9));
    }

    // Changes, each at `frame` (the current sample).
    void set_tempo(uint64_t frame, double bpm) { rebase(frame); bpm_ = bpm > 0.0 ? bpm : bpm_; ++version_; }
    void set_meter(int beats_per_bar) { if (beats_per_bar > 0) beats_per_bar_ = beats_per_bar; ++version_; }
    void set_sample_rate(uint64_t frame, double sample_rate) { rebase(frame); sample_rate_ = sample_rate; ++version_; }
    void start(uint64_t frame) { if (!running_) { anchor_frame_ = frame; running_ = true; ++version_; } }
    void stop(uint64_t frame) { if (running_) { rebase(frame); running_ = false; ++version_; } }

private:
    void rebase(uint64_t frame) {
        anchor_beats_ = beats_at(frame);
        anchor_frame_ = frame;
    }
    double sample_rate_ = 48000.0;
    double bpm_ = 1.0;  // the shipped default; the game sets it from project settings
    int beats_per_bar_ = 4;
    bool running_ = true;
    uint64_t anchor_frame_ = 0;
    double anchor_beats_ = 0.0;
    uint32_t version_ = 0;
};

}  // namespace stubby
