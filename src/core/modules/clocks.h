#pragma once
// The master clock's clock modules (spec master-clock "Tempo clocks are whole
// multiples of the master", synth-modules "Clock module").

#include <cstdint>

#include "core/module.h"
#include "core/volts.h"

namespace stubby::modules {

/// A tempo clock at a whole multiple N of the master transport (param 0). Its tick k
/// is at master beats k/N, computed exactly from the transport, so clocks never drift
/// and all of them tick together on every master beat. Changing N (or the tempo, or
/// the sample rate) takes effect from the current position: the next tick is the next
/// k/N boundary. Out 0: gate, 10 V for the first half of each tick period. Every tick
/// is reported as an event with its exact sample. Silent while the transport is stopped.
class TempoClock : public Module {
public:
    explicit TempoClock(int multiple = 1) : Module(0, 1, 1) { set_param(0, static_cast<float>(multiple)); }
    void process(const ProcessArgs &args, const PolyVoltage *const *, PolyVoltage *out) override {
        const Transport &t = *args.transport;
        const int n = param(0) >= 1.0f ? static_cast<int>(param(0)) : 1;
        if (n != multiple_ || t.version() != version_ || next_ == Transport::kNever) {
            multiple_ = n;
            version_ = t.version();
            // The first tick at or after the current position (a boundary reached on
            // this very sample still fires, once).
            next_k_ = t.first_tick_index(args.frame, n);
            next_ = t.frame_of_tick(next_k_, n);
            half_ = static_cast<uint64_t>(t.samples_per_beat() / n / 2.0);
        }
        if (next_ != Transport::kNever && args.frame >= next_) {
            last_ = args.frame;
            if (args.events) args.events->push({AudioEvent::Kind::Tick, args.handle, args.frame, static_cast<int64_t>(next_k_), 0});
            ++next_k_;
            next_ = t.frame_of_tick(next_k_, n);
        }
        const bool high = t.running() && last_ != Transport::kNever && args.frame < last_ + half_;
        out[0].set_mono(high ? volts::kGateHigh : 0.0f);
    }
private:
    int multiple_ = 0;
    uint32_t version_ = ~0u;
    uint64_t next_k_ = 0, next_ = Transport::kNever, last_ = Transport::kNever, half_ = 1;
};

/// A seeded random clock (a lightning bug's): free-running, intervals drawn uniformly
/// in [min, max] seconds (params 0, 1) from its own seeded generator, but timed in
/// master samples. Same seed, same ticks. Out 0: a 10 V trigger (1 ms) on each tick.
class RandomClock : public Module {
public:
    RandomClock(uint64_t seed, float min_seconds, float max_seconds) : Module(0, 1, 2), state_(seed ? seed : 1) {
        set_param(0, min_seconds);
        set_param(1, max_seconds);
    }
    void process(const ProcessArgs &args, const PolyVoltage *const *, PolyVoltage *out) override {
        if (next_ == Transport::kNever) next_ = args.frame + draw(args.sample_rate);
        if (args.frame >= next_) {
            if (args.events) args.events->push({AudioEvent::Kind::Tick, args.handle, args.frame, static_cast<int64_t>(count_), 0});
            ++count_;
            trigger_until_ = args.frame + static_cast<uint64_t>(volts::kTriggerSeconds * args.sample_rate);
            next_ = args.frame + draw(args.sample_rate);
        }
        out[0].set_mono(args.frame < trigger_until_ ? volts::kGateHigh : 0.0f);
    }
private:
    uint64_t draw(float sample_rate) {
        // splitmix64: deterministic, no allocation.
        state_ += 0x9e3779b97f4a7c15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        const double u = static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
        const double lo = param(0), hi = param(1) > param(0) ? param(1) : param(0);
        const double seconds = lo + (hi - lo) * u;
        const uint64_t samples = static_cast<uint64_t>(seconds * sample_rate);
        return samples > 0 ? samples : 1;
    }
    uint64_t state_;
    uint64_t next_ = Transport::kNever, trigger_until_ = 0, count_ = 0;
};

}  // namespace stubby::modules
