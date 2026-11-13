#include "rack_streams.h"

#include <godot_cpp/core/class_db.hpp>

#include <time.h>

#include "core/stem_renderer.h"
#include "core/volts.h"
#include "rack_server.h"

namespace godot {

namespace {
int64_t monotonic_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}
int64_t thread_cpu_ns() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}
constexpr int kMaxFrames = 4096;
}  // namespace

Ref<AudioStreamPlayback> RackDriverStream::_instantiate_playback() const {
    Ref<RackDriverPlayback> playback;
    playback.instantiate();
    return playback;
}

void RackDriverPlayback::_start(double) {
    active = true;
    step = stubby::StemRenderer::kUnset;
}

int32_t RackDriverPlayback::_mix(AudioFrame *p_buffer, float, int32_t p_frames) {
    RackServer *server = RackServer::get_singleton();
    if (!server || p_frames > kMaxFrames) return 0;
    const int64_t now = monotonic_ns();
    server->measure_step(now, thread_cpu_ns(), p_frames);
    static float scratch[kMaxFrames];
    server->renderer().read(-1, step, scratch, p_frames);
    for (int i = 0; i < p_frames; ++i) p_buffer[i] = AudioFrame{0.0f, 0.0f};  // silent
    return p_frames;
}

void RackStemStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_stem", "stem"), &RackStemStream::set_stem);
    ClassDB::bind_method(D_METHOD("get_stem"), &RackStemStream::get_stem);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "stem"), "set_stem", "get_stem");
}

Ref<AudioStreamPlayback> RackStemStream::_instantiate_playback() const {
    Ref<RackStemPlayback> playback;
    playback.instantiate();
    playback->stem = stem;
    return playback;
}

void RackStemPlayback::_start(double) {
    active = true;
    step = stubby::StemRenderer::kUnset;
}

int32_t RackStemPlayback::_mix(AudioFrame *p_buffer, float, int32_t p_frames) {
    RackServer *server = RackServer::get_singleton();
    if (!server || p_frames > kMaxFrames) return 0;
    const int64_t t0 = monotonic_ns();
    static thread_local float mono[kMaxFrames];
    server->renderer().read(stem, step, mono, p_frames);
    // Volts to the emission stage: ±10 V is 0 dBFS (volts.h kFullScale).
    const float scale = 1.0f / stubby::volts::kFullScale;
    for (int i = 0; i < p_frames; ++i) p_buffer[i] = AudioFrame{mono[i] * scale, mono[i] * scale};
    server->add_stem_ns(monotonic_ns() - t0);
    return p_frames;
}

}  // namespace godot
