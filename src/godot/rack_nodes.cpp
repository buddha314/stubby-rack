#include "rack_nodes.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/registry.h"
#include "rack_server.h"
#include "rack_streams.h"

namespace godot {

namespace {
PackedStringArray strings(const std::vector<std::string> &v) {
    PackedStringArray out;
    for (const auto &s : v) out.append(String(s.c_str()));
    return out;
}
}  // namespace

// ---------------------------------------------------------------- Rack

void Rack::_enter_tree() {
    if (RackServer *s = RackServer::get_singleton()) s->ensure_driver();
    instantiate();
}

void Rack::_ready() {
    // A patch's emitters become children (not owned, so never saved into the scene).
    if (emitters_made || patch.is_null()) return;
    emitters_made = true;
    const Array list = patch->get_emitters();
    for (int64_t i = 0; i < list.size(); ++i) {
        const Dictionary e = list[i];
        RackEmitter *em = memnew(RackEmitter);
        em->set_name(String("Emitter") + String::num_int64(i));
        em->set_module(e.get("module", ""));
        em->set_output(e.get("output", 0));
        em->set_position(e.get("position", Vector3()));
        if (e.has("max_distance")) em->set_max_distance(e["max_distance"]);
        add_child(em);
    }
}

void Rack::_exit_tree() { release(); }

void Rack::instantiate() {
    RackServer *server = RackServer::get_singleton();
    if (instantiated || !server || patch.is_null()) return;
    instantiated = true;
    const Array mods = patch->get_modules();
    for (int64_t i = 0; i < mods.size(); ++i) {
        const Dictionary m = mods[i];
        Place p;
        p.source = m;
        p.definition = m.get("definition", Variant());
        p.kind = p.definition.is_valid() ? p.definition->get_kind() : String(m.get("kind", ""));
        const stubby::KindInfo *k = stubby::find_kind(p.kind.utf8().get_data());
        const String place = m.get("place", String("module") + String::num_int64(i));
        if (!k) {
            UtilityFunctions::push_error("Rack: unknown module kind '", p.kind, "' at ", place);
            continue;
        }
        p.inputs = strings(k->inputs);
        p.outputs = strings(k->outputs);
        for (const auto &pi : k->params) p.param_names.append(String(pi.name.c_str()));
        p.id = server->new_module_id();
        if (!server->add_module(p.id, p.kind, m.get("seed", 1))) continue;
        // Definition defaults first (they may differ from the core's), then the patch's values.
        if (p.definition.is_valid()) {
            const Array dp = p.definition->get_params();
            for (int64_t j = 0; j < dp.size() && j < p.param_names.size(); ++j) {
                const Dictionary d = dp[j];
                if (d.has("default")) p.params[static_cast<int>(j)] = d["default"];
            }
        }
        const Dictionary values = m.get("params", Dictionary());
        const Array keys = values.keys();
        for (int64_t j = 0; j < keys.size(); ++j) {
            const int idx = param_of(p, keys[j]);
            if (idx >= 0) p.params[idx] = values[keys[j]];
            else UtilityFunctions::push_error("Rack: no param '", keys[j], "' on ", place);
        }
        for (const auto &kv : p.params) server->set_param(p.id, kv.first, kv.second);
        places[place] = p;
    }
    const Array list = patch->get_cables();
    for (int64_t i = 0; i < list.size(); ++i) {
        const Dictionary c = list[i];
        connect_cable(c.get("from", ""), c.get("output", 0), c.get("to", ""), c.get("input", 0));
    }
}

void Rack::release() {
    RackServer *server = RackServer::get_singleton();
    if (!instantiated || !server) return;
    for (const auto &kv : places) server->remove_module(kv.second.id);
    places.clear();
    cables.clear();
    instantiated = false;
}

int Rack::port(const PackedStringArray &names, const Variant &v) const {
    if (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) {
        const int i = static_cast<int>(v);
        return i >= 0 && i < names.size() ? i : -1;
    }
    return static_cast<int>(names.find(String(v)));
}

int Rack::param_of(const Place &p, const Variant &v) const {
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT && p.definition.is_valid()) {
        const int i = p.definition->param_index(String(v));
        if (i >= 0) return i;
    }
    return port(p.param_names, v);
}

int64_t Rack::get_module_id(const String &place) const {
    auto it = places.find(place);
    return it == places.end() ? 0 : it->second.id;
}

int Rack::output_index(const String &place, const Variant &output) const {
    auto it = places.find(place);
    return it == places.end() ? -1 : port(it->second.outputs, output);
}

bool Rack::connect_cable(const String &from, const Variant &output, const String &to, const Variant &input) {
    RackServer *server = RackServer::get_singleton();
    auto f = places.find(from);
    auto t = places.find(to);
    if (!server || f == places.end() || t == places.end()) {
        UtilityFunctions::push_error("Rack: no module '", f == places.end() ? from : to, "'");
        return false;
    }
    const int o = port(f->second.outputs, output);
    const int in = port(t->second.inputs, input);
    if (o < 0 || in < 0) {
        UtilityFunctions::push_error("Rack: no port ", o < 0 ? Variant(output) : Variant(input));
        return false;
    }
    if (!server->connect_ports(f->second.id, o, t->second.id, in)) return false;
    for (int64_t i = cables.size() - 1; i >= 0; --i) {  // an input takes one cable
        const Dictionary c = cables[i];
        if (String(c["to"]) == to && int(c["input"]) == in) cables.remove_at(i);
    }
    Dictionary c;
    c["from"] = from;
    c["output"] = o;
    c["to"] = to;
    c["input"] = in;
    cables.append(c);
    return true;
}

bool Rack::disconnect_cable(const String &to, const Variant &input) {
    RackServer *server = RackServer::get_singleton();
    auto t = places.find(to);
    if (!server || t == places.end()) return false;
    const int in = port(t->second.inputs, input);
    if (in < 0 || !server->disconnect_port(t->second.id, in)) return false;
    for (int64_t i = cables.size() - 1; i >= 0; --i) {
        const Dictionary c = cables[i];
        if (String(c["to"]) == to && int(c["input"]) == in) cables.remove_at(i);
    }
    return true;
}

bool Rack::set_param(const String &place, const Variant &param, double value) {
    RackServer *server = RackServer::get_singleton();
    auto it = places.find(place);
    if (!server || it == places.end()) return false;
    const int idx = param_of(it->second, param);
    if (idx < 0 || !server->set_param(it->second.id, idx, value)) return false;
    it->second.params[idx] = value;
    return true;
}

double Rack::get_param(const String &place, const Variant &param) const {
    auto it = places.find(place);
    if (it == places.end()) return 0.0;
    const int idx = param_of(it->second, param);
    auto v = it->second.params.find(idx);
    if (v != it->second.params.end()) return v->second;
    const stubby::KindInfo *k = stubby::find_kind(it->second.kind.utf8().get_data());
    return k && idx >= 0 && idx < static_cast<int>(k->params.size()) ? k->params[static_cast<size_t>(idx)].def : 0.0;
}

int64_t Rack::schedule(int when, int64_t value) {
    RackServer *server = RackServer::get_singleton();
    return server ? server->schedule(when, value) : 0;
}

Ref<RackPatch> Rack::to_patch() const {
    Ref<RackPatch> out;
    out.instantiate();
    Array mods;
    for (const auto &kv : places) {
        Dictionary m = kv.second.source.duplicate();
        m["place"] = kv.first;
        if (kv.second.definition.is_null()) m["kind"] = kv.second.kind;
        Dictionary values;
        for (const auto &pv : kv.second.params)
            values[pv.first < kv.second.param_names.size() ? Variant(kv.second.param_names[pv.first]) : Variant(pv.first)] = pv.second;
        m["params"] = values;
        mods.append(m);
    }
    out->set_modules(mods);
    out->set_cables(cables.duplicate(true));
    Array ems;
    for (int i = 0; i < get_child_count(); ++i) {
        RackEmitter *e = Object::cast_to<RackEmitter>(get_child(i));
        if (!e) continue;
        Dictionary d;
        d["module"] = e->get_module();
        d["output"] = e->get_output();
        d["position"] = e->get_position();
        d["max_distance"] = e->get_max_distance();
        ems.append(d);
    }
    out->set_emitters(ems);
    return out;
}

void Rack::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_patch"), &Rack::get_patch);
    ClassDB::bind_method(D_METHOD("set_patch", "patch"), &Rack::set_patch);
    ClassDB::bind_method(D_METHOD("is_instantiated"), &Rack::is_instantiated);
    ClassDB::bind_method(D_METHOD("get_module_id", "place"), &Rack::get_module_id);
    ClassDB::bind_method(D_METHOD("connect_cable", "from", "output", "to", "input"), &Rack::connect_cable);
    ClassDB::bind_method(D_METHOD("disconnect_cable", "to", "input"), &Rack::disconnect_cable);
    ClassDB::bind_method(D_METHOD("set_param", "place", "param", "value"), &Rack::set_param);
    ClassDB::bind_method(D_METHOD("get_param", "place", "param"), &Rack::get_param);
    ClassDB::bind_method(D_METHOD("schedule", "when", "value"), &Rack::schedule, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("to_patch"), &Rack::to_patch);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "patch", PROPERTY_HINT_RESOURCE_TYPE, "RackPatch"), "set_patch", "get_patch");
}

// ---------------------------------------------------------------- RackEmitter

RackEmitter::RackEmitter() {
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_doppler_tracking(DOPPLER_TRACKING_DISABLED);
}

void RackEmitter::_enter_tree() {
    for (Node *n = get_parent(); n; n = n->get_parent())
        if (Rack *rack = Object::cast_to<Rack>(n)) {
            bind(rack);
            return;
        }
}

void RackEmitter::_exit_tree() { unbind(); }

bool RackEmitter::bind(Rack *rack) {
    RackServer *server = RackServer::get_singleton();
    if (!server || !rack || stem >= 0) return false;
    const int64_t id = rack->get_module_id(module);
    const int out = rack->output_index(module, output);
    if (id == 0 || out < 0) {
        UtilityFunctions::push_error("RackEmitter: no output ", output, " on '", module, "'");
        return false;
    }
    stem = server->allocate_stem();
    if (stem < 0) {
        UtilityFunctions::push_error("RackEmitter: no free stem");
        return false;
    }
    server->set_stem(stem, id, out);
    Ref<RackStemStream> s;
    s.instantiate();
    s->set_stem(stem);
    set_stream(s);
    call_deferred("play");
    return true;
}

void RackEmitter::unbind() {
    if (stem < 0) return;
    stop();
    if (RackServer *server = RackServer::get_singleton()) server->release_stem(stem);
    stem = -1;
}

void RackEmitter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_module"), &RackEmitter::get_module);
    ClassDB::bind_method(D_METHOD("set_module", "place"), &RackEmitter::set_module);
    ClassDB::bind_method(D_METHOD("get_output"), &RackEmitter::get_output);
    ClassDB::bind_method(D_METHOD("set_output", "output"), &RackEmitter::set_output);
    ClassDB::bind_method(D_METHOD("get_stem"), &RackEmitter::get_stem);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "module"), "set_module", "get_module");
    ADD_PROPERTY(PropertyInfo(Variant::NIL, "output", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_output", "get_output");
}

}  // namespace godot
