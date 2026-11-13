#include "core/engine.h"

namespace stubby {

Engine::Engine(const EngineConfig &config)
    : config_(config),
      rack_(config.capacity),
      in_used_(static_cast<size_t>(config.capacity.inputs), false),
      out_used_(static_cast<size_t>(config.capacity.outputs), false),
      staged_(static_cast<size_t>(config.fifo_commands)) {
    commands_.reset(static_cast<size_t>(config.fifo_commands));
    returned_.reset(static_cast<size_t>(config.capacity.modules) * 2);  // removals awaiting collect()
    for (int h = config.capacity.modules - 1; h >= 0; --h) free_handles_.push_back(h);
}

Engine::~Engine() {
    // No audio thread runs during destruction: free what the rack holds, then anything
    // still in transit (removed but not collected, or added but not yet applied).
    for (int h = 0; h < config_.capacity.modules; ++h) delete rack_.remove(h);
    Module *m = nullptr;
    while (returned_.pop(m)) delete m;
    for (int i = 0; i < staged_count_; ++i)
        if (staged_[static_cast<size_t>(i)].op == Command::Op::Add) delete staged_[static_cast<size_t>(i)].module;
    Command c;
    while (commands_.pop(c))
        if (c.op == Command::Op::Add) delete c.module;
}

void Engine::reject(ModuleId id, const std::string &reason) {
    Event e;
    e.kind = Event::Kind::Rejected;
    e.sample = frame();
    e.module = id;
    e.reason = reason;
    events_.push_back(std::move(e));
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
    ModuleRecord rec{handle, in_base, out_base, module->num_inputs(), module->num_outputs()};
    Command c;
    c.op = Command::Op::Add;
    c.handle = handle;
    c.a = in_base;
    c.b = out_base;
    c.module = module.release();  // the audio thread owns it once applied
    modules_.emplace(id, std::move(rec));
    pending_.push_back(c);
    return true;
}

bool Engine::remove_module(ModuleId id) {
    auto it = modules_.find(id);
    if (it == modules_.end()) { reject(id, "remove_module: no such module"); return false; }
    const ModuleRecord &rec = it->second;
    // Unplug every cable into this module, and every cable out of it.
    for (auto c = cables_.begin(); c != cables_.end();) {
        const bool into = c->first >= rec.in_base && c->first < rec.in_base + rec.inputs;
        const bool from = c->second >= rec.out_base && c->second < rec.out_base + rec.outputs;
        if (into || from) {
            Command d;
            d.op = Command::Op::Disconnect;
            d.a = c->first;
            pending_.push_back(d);
            c = cables_.erase(c);
        } else {
            ++c;
        }
    }
    Command r;
    r.op = Command::Op::Remove;
    r.handle = rec.handle;
    pending_.push_back(r);
    // Slots and handle can be reused by later commands: the FIFO keeps order, so the
    // audio thread removes this module before any later command touches them.
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
    pending_.push_back(c);
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
    pending_.push_back(c);
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
    pending_.push_back(c);
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
    pending_.push_back(c);
    return true;
}

bool Engine::clear_stem(int stem) {
    if (stem < 0 || stem >= config_.capacity.stems) { reject(0, "clear_stem: no such stem"); return false; }
    Command c;
    c.op = Command::Op::SetStem;
    c.a = stem;
    c.b = -1;
    pending_.push_back(c);
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
    out.swap(events_);
    return out;
}

void Engine::apply(const Command &c) {
    switch (c.op) {
        case Command::Op::Add: rack_.add(c.handle, c.module, c.a, c.b); break;
        case Command::Op::Remove: {
            Module *m = rack_.remove(c.handle);
            if (m) returned_.push(m);  // sized for every module: never full
            break;
        }
        case Command::Op::Connect: rack_.connect(c.a, c.b); break;
        case Command::Op::Disconnect: rack_.disconnect(c.a); break;
        case Command::Op::SetParam: rack_.set_param(c.handle, c.a, c.value); break;
        case Command::Op::SetStem: rack_.set_stem(c.a, c.b); break;
        case Command::Op::EndBatch: break;
    }
}

void Engine::render(int frames) {
    // Apply every complete batch; hold a partial one until its end arrives.
    Command c;
    while (commands_.pop(c)) {
        if (c.op == Command::Op::EndBatch) {
            for (int i = 0; i < staged_count_; ++i) apply(staged_[static_cast<size_t>(i)]);
            staged_count_ = 0;
        } else if (staged_count_ < static_cast<int>(staged_.size())) {
            staged_[static_cast<size_t>(staged_count_++)] = c;
        }
    }
    if (frames > config_.capacity.max_block) frames = config_.capacity.max_block;
    const uint64_t first = frame_.load(std::memory_order_relaxed);
    rack_.render(frames, first, config_.sample_rate);
    frame_.store(first + static_cast<uint64_t>(frames), std::memory_order_release);
}

}  // namespace stubby
