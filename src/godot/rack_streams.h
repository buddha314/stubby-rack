#pragma once
// The driver and stem streams (design D4, task 5.4). RackDriverStream plays on the
// always-on, silent player inside RackDriver, so the rack and its clock run when
// nothing is audible. RackStemStream plays one stem on a RackEmitter. Neither is
// resampled: _mix ignores p_rate_scale, so pitch_scale and doppler don't apply to
// synth emitters. _mix touches only the core's EngineDriver (captured when the
// playback is made), never a Godot object, and takes no lock.

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

#include <cstdint>

#include "core/driver.h"

namespace godot {

class RackDriverStream : public AudioStream {
    GDCLASS(RackDriverStream, AudioStream)
public:
    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    String _get_stream_name() const override { return "RackDriver"; }
    double _get_length() const override { return 0.0; }
protected:
    static void _bind_methods() {}
};

class RackStemStream : public AudioStream {
    GDCLASS(RackStemStream, AudioStream)
public:
    void set_stem(int p_stem) { stem = p_stem; }
    int get_stem() const { return stem; }
    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    String _get_stream_name() const override { return "RackStem"; }
    double _get_length() const override { return 0.0; }
protected:
    static void _bind_methods();
private:
    int stem = -1;
};

/// One playback class for both: the driver reads stem -1 (silence).
class RackStemPlayback : public AudioStreamPlayback {
    GDCLASS(RackStemPlayback, AudioStreamPlayback)
public:
    int stem = -1;
    stubby::EngineDriver *driver = nullptr;
    void _start(double p_from_pos) override;
    void _stop() override { active = false; }
    bool _is_playing() const override { return active; }
    int32_t _mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
protected:
    static void _bind_methods() {}
private:
    bool active = false;
    uint64_t step = stubby::EngineDriver::kUnset;
};

}  // namespace godot
