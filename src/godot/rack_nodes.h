#pragma once
// The rack's nodes (design D6, task 5.3).
//
// Rack (Node3D): one machine's rack in the world. Entering the tree, it instantiates
// its RackPatch into RackServer (module ids from new_module_id(), places by name);
// leaving, it removes them. Edits go through it by place name: connect_cable,
// disconnect_cable, set_param (ports and params by index or name). to_patch()
// captures the live state as a RackPatch (save games, undo, fixtures).
//
// RackEmitter (AudioStreamPlayer3D): plays one module output's stem from where it
// stands. It binds to its nearest Rack ancestor on entering the tree: takes a free
// stem, points it at `module`/`output`, and plays a RackStemStream. Doppler is off
// (the stem isn't resampled) and process_mode is ALWAYS (pause mutes in the core).
// A patch's `emitters` entries become RackEmitter children at runtime.

#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <map>

#include "rack_resources.h"

namespace godot {

class RackEmitter;

class Rack : public Node3D {
    GDCLASS(Rack, Node3D)
public:
    Ref<RackPatch> get_patch() const { return patch; }
    void set_patch(const Ref<RackPatch> &p) { patch = p; }

    void _enter_tree() override;
    void _ready() override;
    void _exit_tree() override;

    bool is_instantiated() const { return instantiated; }
    int64_t get_module_id(const String &place) const;
    bool connect_cable(const String &from, const Variant &output, const String &to, const Variant &input);
    bool disconnect_cable(const String &to, const Variant &input);
    bool set_param(const String &place, const Variant &param, double value);
    double get_param(const String &place, const Variant &param) const;
    /// Forwards to RackServer.schedule (commands after it, this frame, land at `when`).
    int64_t schedule(int when, int64_t value = 0);
    Ref<RackPatch> to_patch() const;
    /// For RackEmitter: resolve an output port of `place` (-1 if none).
    int output_index(const String &place, const Variant &output) const;

protected:
    static void _bind_methods();

private:
    struct Place {
        int64_t id = 0;
        String kind;
        Ref<RackModuleDefinition> definition;
        Dictionary source;  // the patch entry (kept for to_patch)
        PackedStringArray inputs, outputs, param_names;
        std::map<int, double> params;  // set values by index
    };
    int port(const PackedStringArray &names, const Variant &v) const;
    int param_of(const Place &p, const Variant &v) const;
    void instantiate();
    void release();

    Ref<RackPatch> patch;
    bool instantiated = false;
    bool emitters_made = false;
    std::map<String, Place> places;
    Array cables;  // live cables, as patch entries with port indices
};

class RackEmitter : public AudioStreamPlayer3D {
    GDCLASS(RackEmitter, AudioStreamPlayer3D)
public:
    RackEmitter();
    String get_module() const { return module; }
    void set_module(const String &v) { module = v; }
    Variant get_output() const { return output; }
    void set_output(const Variant &v) { output = v; }
    int get_stem() const { return stem; }

    void _enter_tree() override;
    void _exit_tree() override;
    /// Binds to `rack` (called on entering the tree, after the rack has instantiated).
    bool bind(Rack *rack);
    void unbind();

protected:
    static void _bind_methods();

private:
    String module;
    Variant output = 0;
    int stem = -1;
};

}  // namespace godot
