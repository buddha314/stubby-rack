#include "core/engine.h"

#include <cmath>

namespace stubby {

Engine::Engine(const EngineConfig &config)
    : config_(config),
      rack_(config.capacity),
      sample_rate_(config.sample_rate),
      handle_ids_(static_cast<size_t>(config.capacity.modules), -1),
      in_used_(static_cast<size_t>(config.capacity.inputs), false),
      out_used_(static_cast<size_t>(config.capacity.outputs), false),
      events_(static_cast<size_t>(config.fifo_events)),
      staged_(static_cast<size_t>(config.fifo_commands)),
      scheduled_(static_cast<size_t>(config.scheduled_capacity)) {
    commands_.reset(static_cast<size_t>(config.fifo_commands));
    returned_.reset(static_cast<size_t>(config.capacity.modules) * 2);  // removals awaiting collect()
    for (int h = config.capacity.modules - 1; h >= 0; --h) free_handles_.push_back(h);
    transport_.set_sample_rate(0, config.sample_rate);
    transport_.set_tempo(0, config.tempo_bpm);
    transport_.set_meter(config.beats_per_bar);
    publish_position();
}

Engine::~Engine() {
    // No audio thread runs during destruction: free what the rack holds, then anything
    // still in transit (removed but not collected, staged, scheduled or queued adds).
    for (int h = 0; h < config_.capacity.modules; ++h) delete rack_.remove(h);
    Module *m = nullptr;
    while (returned_.pop(m)) delete m;
    for (int i = 0; i < staged_count_; ++i)
        if (staged_[static_cast<size_t>(i)].op == Command::Op::Add) delete staged_[static_cast<size_t>(i)].module;
    for (int i = 0; i < scheduled_count_; ++i)
        if (scheduled_[static_cast<size_t>(i)].op == Command::Op::Add) delete scheduled_[static_cast<size_t>(i)].module;
    Command c;
    while (commands_.pop(c))
        if (c.op == Command::Op::Add) delete c.module;
}

// ---------------------------------------------------------------- main thread

void Engine::reject(ModuleId id, const std::string &reason) {
    Event e;
    e.kind = Event::Kind::Rejected;
    e.sample = frame();
    e.module = id;
    e.reason = reason;
    events_main_.push_back(std::move(e));
}

void Engine::queue(Command c) {
    c.when = when_;
    c.action = action_;
    pending_.push_back(c);
}

ActionId Engine::at(When when) {
    when_ = when;
    action_ = when.kind == When::Kind::Now ? 0 : next_action_++;
    return action_;
}

int Engine::alloc_range(std::vector<bool> &used, int count) {
    if (count == 0) return 0;
    int run = 0;
    for (int i = 0; i < static_cast<int>(used.size()); ++i) {
        run = used[static_cast<size_t>(i)] ? 0 : run + 1;
        if (run == count) {
            const int base = i - count + 1;
            for (int k = base; k <= i; ++k) used[static_cast<size_t>(k)] = true;
            return base;
        }
    }
    return -1;
}

void Engine::free_range(std::vector<bool> &used, int base, int count) {
    for (int k = base; k < base + count; ++k) used[static_cast<size_t>(k)] = false;
}

bool Engine::add_module(ModuleId id, std::unique_ptr<Module> module) {
    if (!module) { reject(id, "add_module: null module"); return false; }
    if (modules_.count(id)) { reject(id, "add_module: id already in use"); return false; }
    if (free_handles_.empty()) { reject(id, "add_module: rack is full"); return false; }
    if (module->num_inputs() > config_.capacity.max_module_inputs) { reject(id, "add_module: too many inputs"); return false; }
    const int in_base = alloc_range(in_used_, module->num_inputs());
    if (in_base < 0) { reject(id, "add_module: out of input slots"); return false; }
    const int out_base = alloc_range(out_used_, module->num_outputs());
    if (out_base < 0) {
        free_range(in_used_, in_base, module->num_inputs());
        reject(id, "add_module: out of output slots");
        return false;
    }
    const int handle = free_handles_.back();
    free_handles_.pop_back();
    modules_.emplace(id, ModuleRecord{handle, in_base, out_base, module->num_inputs(), module->num_outputs()});
    handle_ids_[static_cast<size_t>(handle)] = id;
    Command c;
    c.op = Command::Op::Add;
    c.handle = handle;
    c.a = in_base;
    c.b = out_base;
    c.module = module.release();  // the audio thread owns it once applied
    queue(c);
    return true;
}

bool Engine::remove_module(ModuleId id) {
    auto it = modules_.find(id);
    if (it == modules_.end()) { reject(id, "remove_module: no such module"); return false; }
    const ModuleRecord rec = it->second;
    // Unplug every cable into this module, and every cable out of it.
    for (auto c = cables_.begin(); c != cables_.end();) {
        const bool into = c->first >= rec.in_base && c->first < rec.in_base + rec.inputs;
        const bool from = c->second >= rec.out_base && c->second < rec.out_base + rec.outputs;
        if (into || from) {
            Command d;
            d.op = Command::Op::Disconnect;
            d.a = c->first;
            queue(d);
            c = cables_.erase(c);
        } else {
            ++c;
        }
    }
    Command r;
    r.op = Command::Op::Remove;
    r.handle = rec.handle;
    queue(r);
    // Slots and handle can be reused by later commands: the FIFO keeps order, so the
    // audio thread removes this module before any later command touches them.
    // (A scheduled removal keeps its handle reserved only until it is queued; schedule
    // removals and re-adds of the same slots in order.)
    free_range(in_used_, rec.in_base, rec.inputs);
    free_range(out_used_, rec.out_base, rec.outputs);
    free_handles_.push_back(rec.handle);
    modules_.erase(it);
    return true;
}

bool Engine::connect(ModuleId from, int output, ModuleId to, int input) {
    auto f = modules_.find(from);
    auto t = modules_.find(to);
    if (f == modules_.end()) { reject(from, "connect: no such source module"); return false; }
    if (t == modules_.end()) { reject(to, "connect: no such destination module"); return false; }
    if (output < 0 || output >= f->second.outputs) { reject(from, "connect: no such output"); return false; }
    if (input < 0 || input >= t->second.inputs) { reject(to, "connect: no such input"); return false; }
    const int in_slot = t->second.in_base + input;
    const int out_slot = f->second.out_base + output;
    cables_[in_slot] = out_slot;  // an input takes one cable: a new one replaces the old
    Command c;
    c.op = Command::Op::Connect;
    c.a = in_slot;
    c.b = out_slot;
    queue(c);
    return true;
}

bool Engine::disconnect(ModuleId to, int input) {
    auto t = modules_.find(to);
    if (t == modules_.end()) { reject(to, "disconnect: no such module"); return false; }
    if (input < 0 || input >= t->second.inputs) { reject(to, "disconnect: no such input"); return false; }
    const int in_slot = t->second.in_base + input;
    cables_.erase(in_slot);
    Command c;
    c.op = Command::Op::Disconnect;
    c.a = in_slot;
    queue(c);
    return true;
}

bool Engine::set_param(ModuleId id, int param, float value) {
    auto it = modules_.find(id);
    if (it == modules_.end()) { reject(id, "set_param: no such module"); return false; }
    Command c;
    c.op = Command::Op::SetParam;
    c.handle = it->second.handle;
    c.a = param;
    c.value = value;
    queue(c);
    return true;
}

bool Engine::set_stem(int stem, ModuleId id, int output) {
    if (stem < 0 || stem >= config_.capacity.stems) { reject(id, "set_stem: no such stem"); return false; }
    auto it = modules_.find(id);
    if (it == modules_.end()) { reject(id, "set_stem: no such module"); return false; }
    if (output < 0 || output >= it->second.outputs) { reject(id, "set_stem: no such output"); return false; }
    Command c;
    c.op = Command::Op::SetStem;
    c.a = stem;
    c.b = it->second.out_base + output;
    queue(c);
    return true;
}

bool Engine::clear_stem(int stem) {
    if (stem < 0 || stem >= config_.capacity.stems) { reject(0, "clear_stem: no such stem"); return false; }
    Command c;
    c.op = Command::Op::SetStem;
    c.a = stem;
    c.b = -1;
    queue(c);
    return true;
}

bool Engine::set_tempo(double bpm) {
    if (!(bpm > 0.0)) { reject(0, "set_tempo: tempo must be positive"); return false; }
    Command c;
    c.op = Command::Op::SetTempo;
    c.value = bpm;
    queue(c);
    return true;
}

bool Engine::set_meter(int beats_per_bar) {
    if (beats_per_bar < 1) { reject(0, "set_meter: at least one beat per bar"); return false; }
    Command c;
    c.op = Command::Op::SetMeter;
    c.a = beats_per_bar;
    queue(c);
    return true;
}

bool Engine::start_transport() { Command c; c.op = Command::Op::Start; queue(c); return true; }
bool Engine::stop_transport() { Command c; c.op = Command::Op::Stop; queue(c); return true; }

bool Engine::set_sample_rate(double sample_rate) {
    if (!(sample_rate > 0.0)) { reject(0, "set_sample_rate: rate must be positive"); return false; }
    Command c;
    c.op = Command::Op::SetSampleRate;
    c.value = sample_rate;
    queue(c);
    return true;
}

bool Engine::set_paused(bool paused) {
    Command c;
    c.op = Command::Op::SetPaused;
    c.a = paused ? 1 : 0;
    queue(c);
    return true;
}

bool Engine::commit() {
    if (pending_.empty()) return true;
    if (commands_.getFreeSlots() < pending_.size() + 1) return false;
    for (const Command &c : pending_) commands_.push(c);
    commands_.push(Command{});  // EndBatch
    pending_.clear();
    return true;
}

void Engine::collect() {
    Module *m = nullptr;
    while (returned_.pop(m)) delete m;
}

std::vector<Event> Engine::take_events() {
    std::vector<Event> out;
    out.swap(events_main_);
    AudioEvent a;
    while (events_.pop(a)) {
        Event e;
        e.sample = a.sample;
        e.value = a.value;
        e.action = a.action;
        if (a.kind == AudioEvent::Kind::Tick) {
            e.kind = Event::Kind::Tick;
            const int64_t id = a.handle >= 0 ? handle_ids_[static_cast<size_t>(a.handle)] : -1;
            e.module = id >= 0 ? static_cast<ModuleId>(id) : 0;
        } else {
            e.kind = Event::Kind::Late;
        }
        out.push_back(std::move(e));
    }
    return out;
}

Position Engine::position() const {
    Position p;
    for (;;) {
        const uint32_t s0 = pos_seq_.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        p.frame = pos_frame_.load(std::memory_order_relaxed);
        p.beats = pos_beats_.load(std::memory_order_relaxed);
        p.bpm = pos_bpm_.load(std::memory_order_relaxed);
        p.beats_per_bar = pos_bpb_.load(std::memory_order_relaxed);
        p.running = pos_running_.load(std::memory_order_relaxed);
        if (pos_seq_.load(std::memory_order_acquire) == s0) break;
    }
    const double whole = std::floor(p.beats + 1e-9);
    p.phase = p.beats - whole;
    if (p.phase < 0.0) p.phase = 0.0;
    const int64_t beat_index = static_cast<int64_t>(whole);
    p.bar = beat_index / p.beats_per_bar + 1;
    p.beat = static_cast<int>(beat_index % p.beats_per_bar) + 1;
    return p;
}

// ---------------------------------------------------------------- audio thread

void Engine::publish_position() {
    const uint64_t f = frame_.load(std::memory_order_relaxed);
    pos_seq_.fetch_add(1, std::memory_order_acq_rel);
    pos_frame_.store(f, std::memory_order_relaxed);
    pos_beats_.store(transport_.beats_at(f), std::memory_order_relaxed);
    pos_bpm_.store(transport_.bpm(), std::memory_order_relaxed);
    pos_bpb_.store(transport_.beats_per_bar(), std::memory_order_relaxed);
    pos_running_.store(transport_.running(), std::memory_order_relaxed);
    pos_seq_.fetch_add(1, std::memory_order_release);
}

void Engine::apply(const Command &c, uint64_t at_frame) {
    switch (c.op) {
        case Command::Op::Add: rack_.add(c.handle, c.module, c.a, c.b); break;
        case Command::Op::Remove: {
            Module *m = rack_.remove(c.handle);
            if (m) returned_.push(m);  // sized for twice the rack: never full in practice
            break;
        }
        case Command::Op::Connect: rack_.connect(c.a, c.b); break;
        case Command::Op::Disconnect: rack_.disconnect(c.a); break;
        case Command::Op::SetParam: rack_.set_param(c.handle, c.a, static_cast<float>(c.value)); break;
        case Command::Op::SetStem: rack_.set_stem(c.a, c.b); break;
        case Command::Op::SetTempo: transport_.set_tempo(at_frame, c.value); break;
        case Command::Op::SetMeter: transport_.set_meter(c.a); break;
        case Command::Op::Start: transport_.start(at_frame); break;
        case Command::Op::Stop: transport_.stop(at_frame); break;
        case Command::Op::SetSampleRate:
            transport_.set_sample_rate(at_frame, c.value);
            sample_rate_.store(c.value, std::memory_order_release);
            break;
        case Command::Op::SetPaused: paused_ = c.a != 0; break;
        case Command::Op::EndBatch: break;
    }
}

void Engine::receive(const Command &c, uint64_t at_frame) {
    if (c.when.kind == When::Kind::Now) {
        apply(c, at_frame);
        return;
    }
    if (scheduled_count_ >= static_cast<int>(scheduled_.size())) {
        apply(c, at_frame);  // schedule full: apply now and report it late
        events_.push({AudioEvent::Kind::Late, -1, at_frame, 0, c.action});
        return;
    }
    Command s = c;
    // A quantized time is resolved to musical beats now (from the position when the
    // batch arrives), and to a frame each block, so tempo and sample-rate changes keep
    // it on its beat.
    const double beats = transport_.beats_at(at_frame);
    const double bpb = transport_.beats_per_bar();
    switch (c.when.kind) {
        case When::Kind::NextBeat: s.target_beats = std::floor(beats + 1e-9) + 1.0; break;
        case When::Kind::NextBar: s.target_beats = (std::floor(beats / bpb + 1e-9) + 1.0) * bpb; break;
        case When::Kind::NextDivision: {
            const double d = c.when.division;
            s.target_beats = (std::floor(beats * d + 1e-9) + 1.0) / d;
            break;
        }
        default: break;
    }
    scheduled_[static_cast<size_t>(scheduled_count_++)] = s;
}

uint64_t Engine::due_frame(const Command &c) const {
    if (c.when.kind == When::Kind::Sample) return c.when.sample;
    return transport_.frame_of(c.target_beats);
}

void Engine::render(int frames, int64_t now_ns) {
    const uint64_t start = frame_.load(std::memory_order_relaxed);
    // Apply every complete batch at the block start; hold a partial one.
    Command c;
    while (commands_.pop(c)) {
        if (c.op == Command::Op::EndBatch) {
            for (int i = 0; i < staged_count_; ++i) receive(staged_[static_cast<size_t>(i)], start);
            staged_count_ = 0;
        } else if (staged_count_ < static_cast<int>(staged_.size())) {
            staged_[static_cast<size_t>(staged_count_++)] = c;
        }
    }
    if (frames > config_.capacity.max_block) frames = config_.capacity.max_block;

    ProcessArgs args;
    int pos = 0;
    while (pos < frames) {
        const uint64_t here = start + static_cast<uint64_t>(pos);
        // Apply everything due by now (late if its time was already rendered), then
        // render up to the next due action.
        uint64_t next_due = Transport::kNever;
        for (int i = 0; i < scheduled_count_;) {
            Command &s = scheduled_[static_cast<size_t>(i)];
            const uint64_t due = due_frame(s);
            if (due <= here) {
                apply(s, here);
                if (due < start) events_.push({AudioEvent::Kind::Late, -1, here, static_cast<int64_t>(start - due), s.action});
                s = scheduled_[static_cast<size_t>(--scheduled_count_)];  // swap-remove
                // A tempo, meter or rate change moves every other due frame: rescan.
                i = 0;
                next_due = Transport::kNever;
                continue;
            }
            if (due < next_due) next_due = due;
            ++i;
        }
        int len = frames - pos;
        if (next_due != Transport::kNever && next_due - here < static_cast<uint64_t>(len))
            len = static_cast<int>(next_due - here);
        const double sr = transport_.sample_rate();
        args.sample_rate = static_cast<float>(sr);
        args.sample_time = static_cast<float>(1.0 / sr);
        args.transport = &transport_;
        args.events = &events_;
        rack_.render(len, here, args, pos);
        const float step = 1.0f / (config_.pause_fade_seconds * static_cast<float>(sr));
        gain_ = rack_.apply_gain(ui_stem(), pos, len, gain_, paused_ ? 0.0f : 1.0f, step);
        pos += len;
    }
    frame_.store(start + static_cast<uint64_t>(frames), std::memory_order_release);
    if (now_ns >= 0) sync_.update(start, now_ns, transport_.sample_rate());
    publish_position();
}

}  // namespace stubby
