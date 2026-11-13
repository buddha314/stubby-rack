#include "rack_streams.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/volts.h"
#include "rack_server.h"

namespace godot {

namespace {
constexpr int kMaxFrames = 4096;

Ref<AudioStreamPlayback> make_playback(int stem) {
    Ref<RackStemPlayback> playback;
    playback.instantiate();
    playback->stem = stem;
    RackServer *server = RackServer::get_singleton();
    playback->driver = server ? server->driver() : nullptr;
    return playback;
}
}  // namespace

Ref<AudioStreamPlayback> RackDriverStream::_instantiate_playback() const { return make_playback(-1); }

void RackStemStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_stem", "stem"), &RackStemStream::set_stem);
    ClassDB::bind_method(D_METHOD("get_stem"), &RackStemStream::get_stem);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "stem"), "set_stem", "get_stem");
}

Ref<AudioStreamPlayback> RackStemStream::_instantiate_playback() const { return make_playback(stem); }

void RackStemPlayback::_start(double) {
    active = true;
    step = stubby::EngineDriver::kUnset;
}

int32_t RackStemPlayback::_mix(AudioFrame *p_buffer, float, int32_t p_frames) {
    if (!driver || p_frames > kMaxFrames) return 0;
    float mono[kMaxFrames];
    // Volts to the emission stage: ±10 V is 0 dBFS (volts.h kFullScale).
    driver->read(stem, step, mono, p_frames, 1.0f / stubby::volts::kFullScale);
    for (int i = 0; i < p_frames; ++i) p_buffer[i] = AudioFrame{mono[i], mono[i]};
    return p_frames;
}

}  // namespace godot
