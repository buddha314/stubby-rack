#include "rack_server.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <algorithm>
#include <cmath>

#include "core/modules/sampler.h"
#include "core/registry.h"
#include "core/version.h"
#include "core/volts.h"
#include "rack_driver.h"

namespace godot {

RackServer *RackServer::singleton = nullptr;

namespace {
constexpr int64_t kUiSamplerId = (int64_t(1) << 31) - 1;  // reserved; game ids are below it

stubby::When to_when(int when, int64_t value) {
    switch (when) {
        case RackServer::WHEN_SAMPLE: return stubby::When::at_sample(static_cast<uint64_t>(value < 0 ? 0 : value));
        case RackServer::WHEN_NEXT_BEAT: return stubby::When::next_beat();
        case RackServer::WHEN_NEXT_BAR: return stubby::When::next_bar();
        case RackServer::WHEN_NEXT_DIVISION: return stubby::When::next_division(static_cast<int>(value));
        default: return stubby::When::now();
    }
}

double setting(const char *name, double fallback) {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (!ps || !ps->has_setting(name)) return fallback;
    return static_cast<double>(ps->get_setting(name, fallback));
}
}  // namespace

RackServer::RackServer() {
    singleton = this;
    // Defaults from project settings (spec master-clock "configured default"; task 4.5).
    stubby::EngineConfig config;
    config.sample_rate = static_cast<float>(setting("audio/driver/mix_rate", 48000.0));
    config.tempo_bpm = setting("audio/master_clock/tempo_bpm", 1.0);
    config.beats_per_bar = static_cast<int>(setting("audio/master_clock/beats_per_bar", 4.0));
    bank_ = std::make_unique<stubby::SampleBank>();
    engine_ = std::make_unique<stubby::Engine>(config);
    driver_ = std::make_unique<stubby::EngineDriver>(*engine_);
    stem_used_.assign(static_cast<size_t>(engine_->ui_stem()), false);
    // The UI stem's sampler: UI sounds share the clock but not the pause mute.
    stubby::BuildContext ctx;
    ctx.sample_rate = config.sample_rate;
    ctx.bank = bank_.get();
    ui_sampler_ = kUiSamplerId;
    engine_->add_module(static_cast<stubby::ModuleId>(ui_sampler_), stubby::make_module("sampler", ctx));
    engine_->set_stem(engine_->ui_stem(), static_cast<stubby::ModuleId>(ui_sampler_), 0);
    engine_->commit();
}

RackServer::~RackServer() {
    if (singleton == this) singleton = nullptr;
}

String RackServer::get_version() const { return String(stubby::kVersion); }

double RackServer::voct_to_hz(double volts) const { return stubby::volts::voct_to_hz(static_cast<float>(volts)); }

// ---------------------------------------------------------------- modules

int64_t RackServer::new_module_id() { return next_id_++; }

bool RackServer::add_module(int64_t id, const String &kind, int64_t seed) {
    if (id <= 0 || id >= kUiSamplerId) {
        emit_signal("command_rejected", id, String("add_module: ids are 1 .. 2^31-2"));
        return false;
    }
    stubby::BuildContext ctx;
    ctx.sample_rate = engine_->sample_rate();
    ctx.seed = static_cast<uint64_t>(seed);
    ctx.bank = bank_.get();
    auto module = stubby::make_module(kind.utf8().get_data(), ctx);
    if (!module) {
        emit_signal("command_rejected", id, String("add_module: unknown kind ") + kind);
        return false;
    }
    if (id >= next_id_) next_id_ = id + 1;
    return engine_->add_module(static_cast<stubby::ModuleId>(id), std::move(module));
}

bool RackServer::remove_module(int64_t id) {
    sampler_voice_.erase(id);
    return engine_->remove_module(static_cast<stubby::ModuleId>(id));
}
bool RackServer::connect_ports(int64_t from, int output, int64_t to, int input) {
    return engine_->connect(static_cast<stubby::ModuleId>(from), output, static_cast<stubby::ModuleId>(to), input);
}
bool RackServer::disconnect_port(int64_t to, int input) {
    return engine_->disconnect(static_cast<stubby::ModuleId>(to), input);
}
bool RackServer::set_param(int64_t id, int param, double value) {
    return engine_->set_param(static_cast<stubby::ModuleId>(id), param, static_cast<float>(value));
}
bool RackServer::set_stem(int stem, int64_t id, int output) {
    return engine_->set_stem(stem, static_cast<stubby::ModuleId>(id), output);
}
bool RackServer::clear_stem(int stem) { return engine_->clear_stem(stem); }

int RackServer::allocate_stem() {
    for (size_t s = 0; s < stem_used_.size(); ++s)
        if (!stem_used_[s]) {
            stem_used_[s] = true;
            return static_cast<int>(s);
        }
    return -1;
}

void RackServer::release_stem(int stem) {
    if (stem < 0 || stem >= static_cast<int>(stem_used_.size())) return;
    stem_used_[static_cast<size_t>(stem)] = false;
    engine_->clear_stem(stem);
}

Array RackServer::get_module_kinds() const {
    Array out;
    for (const stubby::KindInfo &k : stubby::module_kinds()) {
        Dictionary d;
        d["kind"] = String(k.kind.c_str());
        PackedStringArray ins, outs;
        for (const auto &s : k.inputs) ins.append(String(s.c_str()));
        for (const auto &s : k.outputs) outs.append(String(s.c_str()));
        d["inputs"] = ins;
        d["outputs"] = outs;
        Array params;
        for (const stubby::ParamInfo &p : k.params) {
            Dictionary pd;
            pd["name"] = String(p.name.c_str());
            pd["min"] = p.min;
            pd["max"] = p.max;
            pd["default"] = p.def;
            pd["unit"] = String(p.unit.c_str());
            params.append(pd);
        }
        d["params"] = params;
        out.append(d);
    }
    return out;
}

// ---------------------------------------------------------------- time

int64_t RackServer::schedule(int when, int64_t value) {
    return static_cast<int64_t>(engine_->at(to_when(when, value)));
}
bool RackServer::set_tempo(double bpm) { return engine_->set_tempo(bpm); }
bool RackServer::set_meter(int beats_per_bar) { return engine_->set_meter(beats_per_bar); }
bool RackServer::start_transport() { return engine_->start_transport(); }
bool RackServer::stop_transport() { return engine_->stop_transport(); }
bool RackServer::set_paused(bool paused) { return engine_->set_paused(paused); }
int64_t RackServer::now_sample() const { return static_cast<int64_t>(engine_->frame()); }
double RackServer::get_sample_rate() const { return engine_->sample_rate(); }

Dictionary RackServer::get_position() const {
    const stubby::Position p = engine_->position();
    Dictionary d;
    d["sample"] = static_cast<int64_t>(p.frame);
    d["beats"] = p.beats;
    d["bar"] = p.bar;
    d["beat"] = p.beat;
    d["phase"] = p.phase;
    d["bpm"] = p.bpm;
    d["beats_per_bar"] = p.beats_per_bar;
    d["running"] = p.running;
    return d;
}

int64_t RackServer::sample_to_ticks_usec(int64_t sample) const {
    const int64_t latency_ns = static_cast<int64_t>(AudioServer::get_singleton()->get_output_latency() * 1e9);
    int64_t heard_ns = 0;
    if (!engine_->predict_ns(static_cast<uint64_t>(sample < 0 ? 0 : sample), latency_ns, heard_ns)) return -1;
    const int64_t now_ticks = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec());
    return now_ticks + (heard_ns - stubby::EngineDriver::steady_ns()) / 1000;
}

// ---------------------------------------------------------------- samples

int RackServer::load_sample(const Ref<AudioStream> &stream) {
    if (stream.is_null()) return -1;
    const uint64_t key = stream->get_instance_id();
    auto hit = sample_cache_.find(key);
    if (hit != sample_cache_.end()) return hit->second;
    // Decode offline through Godot's own playback (any format: WAV/QOA, Ogg, MP3),
    // which mixes at the server's mix rate.
    Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
    if (playback.is_null()) return -1;
    const double rate = AudioServer::get_singleton()->get_mix_rate();
    const double length = stream->get_length();
    const int64_t total = length > 0.0 ? static_cast<int64_t>(std::ceil(length * rate)) : static_cast<int64_t>(rate * 10);
    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(total));
    playback->start(0.0);
    while (static_cast<int64_t>(mono.size()) < total && playback->is_playing()) {
        const int chunk = static_cast<int>(std::min<int64_t>(4096, total - static_cast<int64_t>(mono.size())));
        PackedVector2Array frames = playback->mix_audio(1.0f, chunk);
        if (frames.is_empty()) break;
        for (int64_t i = 0; i < frames.size(); ++i) mono.push_back(0.5f * (frames[i].x + frames[i].y));
    }
    playback->stop();
    const int id = bank_->add(std::move(mono), rate);
    if (id >= 0) sample_cache_[key] = id;
    return id;
}

int64_t RackServer::play_sample(int sample_id, int when, int64_t value, double gain, int64_t sampler) {
    const int64_t target = sampler < 0 ? ui_sampler_ : sampler;
    if (!bank_->get(sample_id)) {
        emit_signal("command_rejected", target, String("play_sample: no such sample"));
        return -1;
    }
    if (!engine_->has_module(static_cast<stubby::ModuleId>(target))) {
        emit_signal("command_rejected", target, String("play_sample: no such sampler"));
        return -1;
    }
    int &voice = sampler_voice_[target];
    const int v = voice;
    voice = (voice + 1) % stubby::modules::SamplerPool::kVoices;
    trigger_count_ += 1.0f;  // a new trigger value starts the voice
    const stubby::ActionId action = engine_->at(to_when(when, value));
    const auto id = static_cast<stubby::ModuleId>(target);
    engine_->set_param(id, 3 * v, static_cast<float>(sample_id));
    engine_->set_param(id, 3 * v + 2, static_cast<float>(gain));
    engine_->set_param(id, 3 * v + 1, trigger_count_);
    engine_->at(stubby::When::now());
    return static_cast<int64_t>(action);
}

// ---------------------------------------------------------------- frame loop

bool RackServer::commit() {
    const bool ok = engine_->commit();
    engine_->at(stubby::When::now());  // a schedule lasts at most one frame
    return ok;
}

void RackServer::frame_update() {
    // A device change: keep the engine at the mix rate (musical position is kept).
    const double rate = AudioServer::get_singleton()->get_mix_rate();
    if (rate > 0.0 && std::fabs(rate - engine_->sample_rate()) > 0.5) engine_->set_sample_rate(rate);
    commit();
    engine_->collect();
    for (const stubby::Event &e : engine_->take_events()) {
        switch (e.kind) {
            case stubby::Event::Kind::Tick:
                emit_signal("clock_ticked", static_cast<int64_t>(e.module), static_cast<int64_t>(e.sample), e.value);
                break;
            case stubby::Event::Kind::Late:
                emit_signal("action_late", static_cast<int64_t>(e.action), e.value, static_cast<int64_t>(e.sample));
                break;
            case stubby::Event::Kind::Rejected:
                emit_signal("command_rejected", static_cast<int64_t>(e.module), String(e.reason.c_str()));
                break;
        }
    }
}

void RackServer::ensure_driver() {
    if (driver_node_ != 0 && ObjectDB::get_instance(driver_node_)) return;
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (!tree || !tree->get_root()) return;
    RackDriver *node = memnew(RackDriver);
    node->set_name("RackDriver");
    driver_node_ = node->get_instance_id();
    tree->get_root()->call_deferred("add_child", node);
}

Dictionary RackServer::get_driver_stats() const {
    Dictionary d;
    const auto &s = driver_->stats;
    d["renders"] = static_cast<int64_t>(s.renders.load());
    d["reads"] = static_cast<int64_t>(s.reads.load());
    d["skips"] = static_cast<int64_t>(s.skips.load());
    d["repeat_steps"] = static_cast<int64_t>(s.repeat_steps.load());
    d["joins"] = static_cast<int64_t>(s.joins.load());
    d["dropped_events"] = static_cast<int64_t>(engine_->dropped_events());
    return d;
}

void RackServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_version"), &RackServer::get_version);
    ClassDB::bind_method(D_METHOD("voct_to_hz", "volts"), &RackServer::voct_to_hz);
    ClassDB::bind_method(D_METHOD("new_module_id"), &RackServer::new_module_id);
    ClassDB::bind_method(D_METHOD("add_module", "id", "kind", "seed"), &RackServer::add_module, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("remove_module", "id"), &RackServer::remove_module);
    ClassDB::bind_method(D_METHOD("connect_ports", "from", "output", "to", "input"), &RackServer::connect_ports);
    ClassDB::bind_method(D_METHOD("disconnect_port", "to", "input"), &RackServer::disconnect_port);
    ClassDB::bind_method(D_METHOD("set_param", "id", "param", "value"), &RackServer::set_param);
    ClassDB::bind_method(D_METHOD("set_stem", "stem", "id", "output"), &RackServer::set_stem);
    ClassDB::bind_method(D_METHOD("clear_stem", "stem"), &RackServer::clear_stem);
    ClassDB::bind_method(D_METHOD("allocate_stem"), &RackServer::allocate_stem);
    ClassDB::bind_method(D_METHOD("release_stem", "stem"), &RackServer::release_stem);
    ClassDB::bind_method(D_METHOD("get_ui_stem"), &RackServer::get_ui_stem);
    ClassDB::bind_method(D_METHOD("get_stem_count"), &RackServer::get_stem_count);
    ClassDB::bind_method(D_METHOD("get_module_kinds"), &RackServer::get_module_kinds);
    ClassDB::bind_method(D_METHOD("schedule", "when", "value"), &RackServer::schedule, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("set_tempo", "bpm"), &RackServer::set_tempo);
    ClassDB::bind_method(D_METHOD("set_meter", "beats_per_bar"), &RackServer::set_meter);
    ClassDB::bind_method(D_METHOD("start_transport"), &RackServer::start_transport);
    ClassDB::bind_method(D_METHOD("stop_transport"), &RackServer::stop_transport);
    ClassDB::bind_method(D_METHOD("set_paused", "paused"), &RackServer::set_paused);
    ClassDB::bind_method(D_METHOD("now_sample"), &RackServer::now_sample);
    ClassDB::bind_method(D_METHOD("get_position"), &RackServer::get_position);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &RackServer::get_sample_rate);
    ClassDB::bind_method(D_METHOD("sample_to_ticks_usec", "sample"), &RackServer::sample_to_ticks_usec);
    ClassDB::bind_method(D_METHOD("load_sample", "stream"), &RackServer::load_sample);
    ClassDB::bind_method(D_METHOD("play_sample", "sample_id", "when", "value", "gain", "sampler"),
                         &RackServer::play_sample, DEFVAL(WHEN_NOW), DEFVAL(0), DEFVAL(1.0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("commit"), &RackServer::commit);
    ClassDB::bind_method(D_METHOD("ensure_driver"), &RackServer::ensure_driver);
    ClassDB::bind_method(D_METHOD("get_driver_stats"), &RackServer::get_driver_stats);

    ADD_SIGNAL(MethodInfo("clock_ticked", PropertyInfo(Variant::INT, "module_id"),
                          PropertyInfo(Variant::INT, "sample"), PropertyInfo(Variant::INT, "index")));
    ADD_SIGNAL(MethodInfo("action_late", PropertyInfo(Variant::INT, "action"),
                          PropertyInfo(Variant::INT, "samples_missed"), PropertyInfo(Variant::INT, "sample")));
    ADD_SIGNAL(MethodInfo("command_rejected", PropertyInfo(Variant::INT, "module_id"),
                          PropertyInfo(Variant::STRING, "reason")));

    BIND_ENUM_CONSTANT(WHEN_NOW);
    BIND_ENUM_CONSTANT(WHEN_SAMPLE);
    BIND_ENUM_CONSTANT(WHEN_NEXT_BEAT);
    BIND_ENUM_CONSTANT(WHEN_NEXT_BAR);
    BIND_ENUM_CONSTANT(WHEN_NEXT_DIVISION);
}

}  // namespace godot
