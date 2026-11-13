#include "rack_server.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/version.h"
#include "core/volts.h"

namespace godot {

RackServer *RackServer::singleton = nullptr;

RackServer::RackServer() : renderer_(std::make_unique<stubby::StemRenderer>(128)) {
    singleton = this;
    renderer_->configure(48000.0f, 440.0f, 2.0f);
}
RackServer::~RackServer() {
    if (singleton == this) singleton = nullptr;
}

String RackServer::get_version() const { return String(stubby::kVersion); }

double RackServer::voct_to_hz(double volts) const {
    return stubby::volts::voct_to_hz(static_cast<float>(volts));
}

void RackServer::spike_configure(double tone_hz, double tone_volts, int stems) {
    renderer_->configure(48000.0f, static_cast<float>(tone_hz), static_cast<float>(tone_volts), stems);
}

int RackServer::spike_use_rack(int modules) {
    renderer_->set_rack(nullptr);
    rack_.reset();
    if (modules <= 0) return 0;
    rack_ = stubby::spike::build_patch(modules, 48000.0f);
    renderer_->set_rack(rack_.get());
    return rack_->module_count();
}

int64_t RackServer::get_frontier() const { return static_cast<int64_t>(renderer_->frontier()); }

void RackServer::measure_step(int64_t now_ns, int64_t cpu_ns, int frames) {
    block_frames_.store(frames, std::memory_order_relaxed);
    if (last_step_ns_ != 0) {
        const int64_t interval = now_ns - last_step_ns_;
        const int64_t cpu = cpu_ns - last_cpu_ns_;
        const int64_t block_ns = int64_t(frames) * 1'000'000'000 / 48000;
        steps_.fetch_add(1, std::memory_order_relaxed);
        cpu_ns_total_.fetch_add(cpu, std::memory_order_relaxed);
        // PulseAudio mixes two 512-frame steps per ~21 ms callback, so 2 blocks is normal.
        if (interval > 3 * block_ns) late_steps_.fetch_add(1, std::memory_order_relaxed);
        if (interval > max_interval_ns_.load(std::memory_order_relaxed)) max_interval_ns_.store(interval, std::memory_order_relaxed);
        if (cpu > max_cpu_ns_.load(std::memory_order_relaxed)) max_cpu_ns_.store(cpu, std::memory_order_relaxed);
    }
    last_step_ns_ = now_ns;
    last_cpu_ns_ = cpu_ns;
}

Dictionary RackServer::get_spike_stats() const {
    Dictionary d;
    auto &s = renderer_->stats;
    d["frontier"] = static_cast<int64_t>(renderer_->frontier());
    d["renders"] = static_cast<int64_t>(s.renders.load());
    d["reads"] = static_cast<int64_t>(s.reads.load());
    d["skips"] = static_cast<int64_t>(s.skips.load());
    d["repeat_steps"] = static_cast<int64_t>(s.repeat_steps.load());
    d["joins"] = static_cast<int64_t>(s.joins.load());
    d["steps"] = steps_.load();
    d["late_steps"] = late_steps_.load();
    d["max_interval_ms"] = max_interval_ns_.load() / 1e6;
    d["block_frames"] = block_frames_.load();
    const int64_t steps = steps_.load();
    d["audio_cpu_ms_per_step"] = steps ? cpu_ns_total_.load() / 1e6 / steps : 0.0;
    d["audio_cpu_max_ms"] = max_cpu_ns_.load() / 1e6;
    d["stem_mix_ms_per_step"] = steps ? stem_ns_.load() / 1e6 / steps : 0.0;
    return d;
}

Array RackServer::get_trace() const {
    Array out;
    const auto &r = *renderer_;
    const uint64_t n = r.trace_n.load();
    const uint64_t start = n > stubby::StemRenderer::kTrace ? n - stubby::StemRenderer::kTrace : 0;
    for (uint64_t i = start; i < n; ++i) {
        const auto &e = r.trace[i % stubby::StemRenderer::kTrace];
        Array row;
        row.append(e.stem); row.append(static_cast<int64_t>(e.pos == stubby::StemRenderer::kUnset ? -1 : (int64_t)e.pos));
        row.append(static_cast<int64_t>(e.front)); row.append(e.frames); row.append(e.ns);
        out.append(row);
    }
    return out;
}

int64_t RackServer::now_ns() const { return stubby::StemRenderer::steady_ns(); }

PackedInt64Array RackServer::get_sync_log() const {
    PackedInt64Array out;
    const uint64_t n = renderer_->sync_n.load(std::memory_order_acquire);
    out.resize(static_cast<int64_t>(n) * 2);
    for (uint64_t i = 0; i < n; ++i) {
        out[static_cast<int64_t>(2 * i)] = static_cast<int64_t>(renderer_->sync_sample[i]);
        out[static_cast<int64_t>(2 * i + 1)] = renderer_->sync_ns[i];
    }
    return out;
}

void RackServer::reset_spike_stats() {
    auto &s = renderer_->stats;
    renderer_->sync_n = 0;
    s.renders = 0; s.reads = 0; s.skips = 0; s.repeat_steps = 0; s.joins = 0;
    steps_ = 0; late_steps_ = 0; max_interval_ns_ = 0; cpu_ns_total_ = 0; max_cpu_ns_ = 0; stem_ns_ = 0;
}

void RackServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("spike_configure", "tone_hz", "tone_volts", "stems"), &RackServer::spike_configure);
    ClassDB::bind_method(D_METHOD("get_frontier"), &RackServer::get_frontier);
    ClassDB::bind_method(D_METHOD("spike_use_rack", "modules"), &RackServer::spike_use_rack);
    ClassDB::bind_method(D_METHOD("get_spike_stats"), &RackServer::get_spike_stats);
    ClassDB::bind_method(D_METHOD("reset_spike_stats"), &RackServer::reset_spike_stats);
    ClassDB::bind_method(D_METHOD("get_trace"), &RackServer::get_trace);
    ClassDB::bind_method(D_METHOD("get_sync_log"), &RackServer::get_sync_log);
    ClassDB::bind_method(D_METHOD("now_ns"), &RackServer::now_ns);
    ClassDB::bind_method(D_METHOD("get_version"), &RackServer::get_version);
    ClassDB::bind_method(D_METHOD("voct_to_hz", "volts"), &RackServer::voct_to_hz);
}

}  // namespace godot
