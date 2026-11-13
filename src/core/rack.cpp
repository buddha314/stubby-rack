#include "core/rack.h"

#include "core/volts.h"

namespace stubby {

Rack::Rack(const RackCapacity &cap)
    : cap_(cap),
      slots_(static_cast<size_t>(cap.modules)),
      active_(static_cast<size_t>(cap.modules), -1),
      in_src_(static_cast<size_t>(cap.inputs), -1),
      stem_src_(static_cast<size_t>(cap.stems), -1),
      stems_(static_cast<size_t>(cap.stems) * cap.max_block, 0.0f),
      in_ptrs_(static_cast<size_t>(cap.max_module_inputs), &kUnconnected) {
    out_[0].assign(static_cast<size_t>(cap.outputs), PolyVoltage{});
    out_[1].assign(static_cast<size_t>(cap.outputs), PolyVoltage{});
}

void Rack::add(int handle, Module *module, int in_base, int out_base) {
    Slot &s = slots_[static_cast<size_t>(handle)];
    s.module = module;
    s.in_base = in_base;
    s.out_base = out_base;
    s.position = active_count_;
    active_[static_cast<size_t>(active_count_++)] = handle;
    for (int k = 0; k < module->num_inputs(); ++k) in_src_[static_cast<size_t>(in_base + k)] = -1;
    for (int k = 0; k < module->num_outputs(); ++k) {
        out_[0][static_cast<size_t>(out_base + k)] = PolyVoltage{};
        out_[1][static_cast<size_t>(out_base + k)] = PolyVoltage{};
    }
}

Module *Rack::remove(int handle) {
    Slot &s = slots_[static_cast<size_t>(handle)];
    Module *m = s.module;
    if (!m) return nullptr;
    // Swap-remove from the dense list: order never matters with 1-sample cables.
    const int last = active_[static_cast<size_t>(active_count_ - 1)];
    active_[static_cast<size_t>(s.position)] = last;
    slots_[static_cast<size_t>(last)].position = s.position;
    --active_count_;
    s = Slot{};
    return m;
}

void Rack::set_param(int handle, int param, float value) {
    Module *m = slots_[static_cast<size_t>(handle)].module;
    if (m) m->set_param(param, value);
}

void Rack::render(int frames, uint64_t first_frame, const ProcessArgs &base, int stem_offset) {
    ProcessArgs args = base;
    const int stems = stem_high_;
    for (int i = 0; i < frames; ++i) {
        args.frame = first_frame + static_cast<uint64_t>(i);
        const PolyVoltage *prev = out_[parity_ ^ 1].data();
        PolyVoltage *cur = out_[parity_].data();
        for (int a = 0; a < active_count_; ++a) {
            const int handle = active_[static_cast<size_t>(a)];
            const Slot &s = slots_[static_cast<size_t>(handle)];
            const int n_in = s.module->num_inputs();
            for (int k = 0; k < n_in; ++k) {
                const int src = in_src_[static_cast<size_t>(s.in_base + k)];
                in_ptrs_[static_cast<size_t>(k)] = src < 0 ? &kUnconnected : &prev[src];
            }
            args.handle = handle;
            s.module->process(args, in_ptrs_.data(), cur + s.out_base);
        }
        const int at = stem_offset + i;
        for (int st = 0; st < stems; ++st) {
            const int src = stem_src_[static_cast<size_t>(st)];
            stems_[static_cast<size_t>(st) * cap_.max_block + at] = src < 0 ? 0.0f : volts::sanitize(cur[src].v[0]);
        }
        parity_ ^= 1;
    }
}

float Rack::apply_gain(int emitters, int offset, int frames, float gain, float target, float step) {
    const int stems = emitters < stem_high_ ? emitters : stem_high_;
    if (gain == 1.0f && target == 1.0f) return gain;
    for (int i = 0; i < frames; ++i) {
        if (gain < target) gain = gain + step < target ? gain + step : target;
        else if (gain > target) gain = gain - step > target ? gain - step : target;
        for (int st = 0; st < stems; ++st) stems_[static_cast<size_t>(st) * cap_.max_block + offset + i] *= gain;
    }
    return gain;
}

}  // namespace stubby
