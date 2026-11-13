#pragma once
// Spike 1.2-1.4 (design D4): the driver and stem streams.
// RackDriverStream plays on an always-on (silent) player so the rack and its clock
// keep running when nothing is audible. RackStemStream plays one emitter's stem on an
// AudioStreamPlayer3D. Neither is resampled: _mix ignores p_rate_scale, so pitch_scale
// and doppler don't apply to synth emitters. _mix touches only the core's lock-free
// state, never a Godot object.

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

#include <atomic>
#include <cstdint>

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

class RackDriverPlayback : public AudioStreamPlayback {
    GDCLASS(RackDriverPlayback, AudioStreamPlayback)
public:
    void _start(double p_from_pos) override;
    void _stop() override { active = false; }
    bool _is_playing() const override { return active; }
    int32_t _mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
protected:
    static void _bind_methods() {}
private:
    bool active = false;
    uint64_t step = 0;
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
    int stem = 0;
};

class RackStemPlayback : public AudioStreamPlayback {
    GDCLASS(RackStemPlayback, AudioStreamPlayback)
public:
    int stem = 0;
    void _start(double p_from_pos) override;
    void _stop() override { active = false; }
    bool _is_playing() const override { return active; }
    int32_t _mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
protected:
    static void _bind_methods() {}
private:
    bool active = false;
    uint64_t step = 0;
};

}  // namespace godot
