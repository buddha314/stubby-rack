#include "rack_resources.h"

#include <godot_cpp/core/class_db.hpp>

#include <cmath>

#include "core/registry.h"

namespace godot {

int RackModuleDefinition::param_index(const String &name) const {
    for (int64_t i = 0; i < params.size(); ++i) {
        const Dictionary p = params[i];
        if (String(p.get("name", "")) == name) return static_cast<int>(i);
    }
    return -1;
}

PackedStringArray RackModuleDefinition::check_against_core() const {
    PackedStringArray problems;
    const stubby::KindInfo *k = stubby::find_kind(kind.utf8().get_data());
    if (!k) {
        problems.append(String("unknown kind '") + kind + "'");
        return problems;
    }
    auto ports = [&](const char *what, const PackedStringArray &mine, const std::vector<std::string> &core) {
        if (mine.size() != static_cast<int64_t>(core.size()))
            problems.append(String(what) + ": " + String::num_int64(mine.size()) + " declared, core has " +
                            String::num_int64(static_cast<int64_t>(core.size())));
        for (int64_t i = 0; i < mine.size() && i < static_cast<int64_t>(core.size()); ++i)
            if (mine[i] != String(core[static_cast<size_t>(i)].c_str()))
                problems.append(String(what) + " " + String::num_int64(i) + ": '" + mine[i] + "', core has '" +
                                String(core[static_cast<size_t>(i)].c_str()) + "'");
    };
    ports("inputs", inputs, k->inputs);
    ports("outputs", outputs, k->outputs);
    if (params.size() != static_cast<int64_t>(k->params.size()))
        problems.append(String("params: ") + String::num_int64(params.size()) + " declared, core has " +
                        String::num_int64(static_cast<int64_t>(k->params.size())));
    for (int64_t i = 0; i < params.size() && i < static_cast<int64_t>(k->params.size()); ++i) {
        const Dictionary p = params[i];
        const stubby::ParamInfo &c = k->params[static_cast<size_t>(i)];
        if (String(p.get("name", "")) != String(c.name.c_str()))
            problems.append(String("param ") + String::num_int64(i) + ": '" + String(p.get("name", "")) +
                            "', core has '" + String(c.name.c_str()) + "'");
        const double def = p.get("default", 0.0);
        if (std::fabs(def - c.def) > 1e-4 * (1.0 + std::fabs(c.def)))
            problems.append(String("param ") + String(c.name.c_str()) + ": default " + String::num(def) +
                            ", core has " + String::num(c.def));
    }
    return problems;
}

Ref<RackModuleDefinition> RackModuleDefinition::from_kind(const String &kind) {
    const stubby::KindInfo *k = stubby::find_kind(kind.utf8().get_data());
    if (!k) return Ref<RackModuleDefinition>();
    Ref<RackModuleDefinition> d;
    d.instantiate();
    d->id = kind;
    d->display_name = kind;
    d->kind = kind;
    for (const auto &s : k->inputs) d->inputs.append(String(s.c_str()));
    for (const auto &s : k->outputs) d->outputs.append(String(s.c_str()));
    for (const stubby::ParamInfo &p : k->params) {
        Dictionary pd;
        pd["name"] = String(p.name.c_str());
        pd["min"] = p.min;
        pd["max"] = p.max;
        pd["default"] = p.def;
        pd["unit"] = String(p.unit.c_str());
        d->params.append(pd);
    }
    return d;
}

void RackModuleDefinition::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &RackModuleDefinition::get_id);
    ClassDB::bind_method(D_METHOD("set_id", "id"), &RackModuleDefinition::set_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &RackModuleDefinition::get_display_name);
    ClassDB::bind_method(D_METHOD("set_display_name", "name"), &RackModuleDefinition::set_display_name);
    ClassDB::bind_method(D_METHOD("get_kind"), &RackModuleDefinition::get_kind);
    ClassDB::bind_method(D_METHOD("set_kind", "kind"), &RackModuleDefinition::set_kind);
    ClassDB::bind_method(D_METHOD("get_inputs"), &RackModuleDefinition::get_inputs);
    ClassDB::bind_method(D_METHOD("set_inputs", "inputs"), &RackModuleDefinition::set_inputs);
    ClassDB::bind_method(D_METHOD("get_outputs"), &RackModuleDefinition::get_outputs);
    ClassDB::bind_method(D_METHOD("set_outputs", "outputs"), &RackModuleDefinition::set_outputs);
    ClassDB::bind_method(D_METHOD("get_params"), &RackModuleDefinition::get_params);
    ClassDB::bind_method(D_METHOD("set_params", "params"), &RackModuleDefinition::set_params);
    ClassDB::bind_method(D_METHOD("get_lights"), &RackModuleDefinition::get_lights);
    ClassDB::bind_method(D_METHOD("set_lights", "lights"), &RackModuleDefinition::set_lights);
    ClassDB::bind_method(D_METHOD("get_hp"), &RackModuleDefinition::get_hp);
    ClassDB::bind_method(D_METHOD("set_hp", "hp"), &RackModuleDefinition::set_hp);
    ClassDB::bind_method(D_METHOD("get_panel"), &RackModuleDefinition::get_panel);
    ClassDB::bind_method(D_METHOD("set_panel", "panel"), &RackModuleDefinition::set_panel);
    ClassDB::bind_method(D_METHOD("get_dsp_source"), &RackModuleDefinition::get_dsp_source);
    ClassDB::bind_method(D_METHOD("set_dsp_source", "source"), &RackModuleDefinition::set_dsp_source);
    ClassDB::bind_method(D_METHOD("input_index", "name"), &RackModuleDefinition::input_index);
    ClassDB::bind_method(D_METHOD("output_index", "name"), &RackModuleDefinition::output_index);
    ClassDB::bind_method(D_METHOD("param_index", "name"), &RackModuleDefinition::param_index);
    ClassDB::bind_method(D_METHOD("check_against_core"), &RackModuleDefinition::check_against_core);
    ClassDB::bind_static_method("RackModuleDefinition", D_METHOD("from_kind", "kind"), &RackModuleDefinition::from_kind);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "id"), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "kind"), "set_kind", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "inputs"), "set_inputs", "get_inputs");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "outputs"), "set_outputs", "get_outputs");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "params", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_params", "get_params");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "lights"), "set_lights", "get_lights");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "hp", PROPERTY_HINT_RANGE, "1,84"), "set_hp", "get_hp");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "panel"), "set_panel", "get_panel");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "dsp_source", PROPERTY_HINT_MULTILINE_TEXT), "set_dsp_source", "get_dsp_source");
}

Dictionary RackPatch::to_dictionary() const {
    Dictionary d;
    Array mods;
    for (int64_t i = 0; i < modules.size(); ++i) {
        Dictionary m = Dictionary(modules[i]).duplicate(true);
        // A definition resource is stored by path (save games and fixtures are data).
        Ref<RackModuleDefinition> def = m.get("definition", Variant());
        if (def.is_valid()) {
            m["definition"] = def->get_path();
            if (!m.has("kind")) m["kind"] = def->get_kind();
        }
        mods.append(m);
    }
    d["modules"] = mods;
    d["cables"] = cables.duplicate(true);
    d["emitters"] = emitters.duplicate(true);
    return d;
}

Ref<RackPatch> RackPatch::from_dictionary(const Dictionary &d) {
    Ref<RackPatch> p;
    p.instantiate();
    Array mods = Array(d.get("modules", Array())).duplicate(true);
    for (int64_t i = 0; i < mods.size(); ++i) {
        Dictionary m = mods[i];
        if (m.get("definition", Variant()).get_type() == Variant::STRING) m.erase("definition");  // kind suffices
    }
    p->modules = mods;
    p->cables = Array(d.get("cables", Array())).duplicate(true);
    p->emitters = Array(d.get("emitters", Array())).duplicate(true);
    return p;
}

void RackPatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_modules"), &RackPatch::get_modules);
    ClassDB::bind_method(D_METHOD("set_modules", "modules"), &RackPatch::set_modules);
    ClassDB::bind_method(D_METHOD("get_cables"), &RackPatch::get_cables);
    ClassDB::bind_method(D_METHOD("set_cables", "cables"), &RackPatch::set_cables);
    ClassDB::bind_method(D_METHOD("get_emitters"), &RackPatch::get_emitters);
    ClassDB::bind_method(D_METHOD("set_emitters", "emitters"), &RackPatch::set_emitters);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &RackPatch::to_dictionary);
    ClassDB::bind_static_method("RackPatch", D_METHOD("from_dictionary", "data"), &RackPatch::from_dictionary);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "modules", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_modules", "get_modules");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "cables", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_cables", "get_cables");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "emitters", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_emitters", "get_emitters");
}

}  // namespace godot
