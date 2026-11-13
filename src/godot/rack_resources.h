#pragma once
// The rack's data as Godot Resources (design D6, task 5.2), saved as .tres and edited
// in the inspector.
//
// RackModuleDefinition: one module kind as the game sees it: ports, parameters
// (name, min, max, default, unit), lights, panel bindings and the DSP source. Its
// `kind` names the core registry entry; check_against_core() lists every way the two
// disagree (task 6.2).
//
// RackPatch: a whole patch as data (spec rack-control "Patches are data"): modules
// ({place, kind or definition, params {name: value}, seed}), cables ({from, output,
// to, input}; ports by index or name) and emitters ({module, output, position,
// max_distance}). A Rack node instantiates it; Rack.to_patch() captures one.

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

class RackModuleDefinition : public Resource {
    GDCLASS(RackModuleDefinition, Resource)
public:
    String get_id() const { return id; }
    void set_id(const String &v) { id = v; }
    String get_display_name() const { return display_name; }
    void set_display_name(const String &v) { display_name = v; }
    String get_kind() const { return kind; }
    void set_kind(const String &v) { kind = v; }
    PackedStringArray get_inputs() const { return inputs; }
    void set_inputs(const PackedStringArray &v) { inputs = v; }
    PackedStringArray get_outputs() const { return outputs; }
    void set_outputs(const PackedStringArray &v) { outputs = v; }
    Array get_params() const { return params; }
    void set_params(const Array &v) { params = v; }
    PackedStringArray get_lights() const { return lights; }
    void set_lights(const PackedStringArray &v) { lights = v; }
    int get_hp() const { return hp; }
    void set_hp(int v) { hp = v; }
    Dictionary get_panel() const { return panel; }
    void set_panel(const Dictionary &v) { panel = v; }
    String get_dsp_source() const { return dsp_source; }
    void set_dsp_source(const String &v) { dsp_source = v; }

    int input_index(const String &name) const { return inputs.find(name); }
    int output_index(const String &name) const { return outputs.find(name); }
    int param_index(const String &name) const;
    /// Every disagreement with the core registry's entry for `kind` (empty = agree).
    PackedStringArray check_against_core() const;
    /// A definition filled from the core registry's entry for `kind` (null if unknown).
    static Ref<RackModuleDefinition> from_kind(const String &kind);

protected:
    static void _bind_methods();

private:
    String id, display_name, kind, dsp_source;
    PackedStringArray inputs, outputs, lights;
    Array params;
    int hp = 4;
    Dictionary panel;
};

class RackPatch : public Resource {
    GDCLASS(RackPatch, Resource)
public:
    Array get_modules() const { return modules; }
    void set_modules(const Array &v) { modules = v; }
    Array get_cables() const { return cables; }
    void set_cables(const Array &v) { cables = v; }
    Array get_emitters() const { return emitters; }
    void set_emitters(const Array &v) { emitters = v; }
    /// The patch as plain data (for JSON save games and fixtures), and back.
    Dictionary to_dictionary() const;
    static Ref<RackPatch> from_dictionary(const Dictionary &d);

protected:
    static void _bind_methods();

private:
    Array modules, cables, emitters;
};

}  // namespace godot
