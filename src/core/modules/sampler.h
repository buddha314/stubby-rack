#pragma once
// The sampler (task 5.5): one-shot sounds played through the rack, so every timed
// sound shares the master clock. A pool of kVoices voices; voice v has params
// [3v] sample id, [3v+1] trigger, [3v+2] gain. Changing a voice's trigger value starts
// its sample from the beginning on that exact sample: schedule the set_param with
// Engine::at() to land a sound on a beat. The controller picks voices round-robin.
// Out 0: the sum of the voices, a full-scale sample at gain 1 being ±5 V (audio level).

#include <cstdint>

#include "core/module.h"
#include "core/sample_bank.h"
#include "core/volts.h"

namespace stubby::modules {

class SamplerPool : public Module {
public:
    static constexpr int kVoices = 8;

    explicit SamplerPool(const SampleBank *bank) : Module(0, 1, 3 * kVoices), bank_(bank) {
        for (int v = 0; v < kVoices; ++v) {
            set_param(3 * v, -1.0f);
            set_param(3 * v + 2, 1.0f);
        }
    }

    void process(const ProcessArgs &args, const PolyVoltage *const *, PolyVoltage *out) override {
        float sum = 0.0f;
        for (int v = 0; v < kVoices; ++v) {
            Voice &voice = voices_[v];
            const float trigger = param(3 * v + 1);
            if (trigger != voice.trigger) {
                voice.trigger = trigger;
                voice.entry = bank_ ? bank_->get(static_cast<int>(param(3 * v))) : nullptr;
                voice.pos = 0.0;
                voice.step = voice.entry ? voice.entry->sample_rate / args.sample_rate : 0.0;
            }
            if (!voice.entry) continue;
            const uint32_t i = static_cast<uint32_t>(voice.pos);
            if (i + 1 >= voice.entry->length) {
                if (i < voice.entry->length) sum += voice.entry->data[i] * param(3 * v + 2);
                voice.entry = nullptr;  // finished
                continue;
            }
            const float frac = static_cast<float>(voice.pos - i);
            const float s = voice.entry->data[i] + (voice.entry->data[i + 1] - voice.entry->data[i]) * frac;
            sum += s * param(3 * v + 2);
            voice.pos += voice.step;
        }
        out[0].set_mono(volts::sanitize(sum * volts::kAudioPeak));
    }

private:
    struct Voice {
        const SampleBank::Entry *entry = nullptr;
        double pos = 0.0, step = 0.0;
        float trigger = 0.0f;
    };
    const SampleBank *bank_;
    Voice voices_[kVoices];
};

}  // namespace stubby::modules
