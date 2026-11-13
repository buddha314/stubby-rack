#pragma once
// Reference patches built through the Engine API (tests and benchmarks).

#include <vector>

#include "core/engine.h"
#include "core/modules/basic.h"

namespace stubby::patches {

/// stubby-music's spike_rack patch of about `n` modules: voices of clock → envelope,
/// LFO → VCO V/oct, VCO → VCA ← envelope, VCA → VCO FM (feedback), every four voices
/// summed by a mixer. The first `stems` VCAs are taps on stems 0.. . IDs start at
/// `first_id`; `reverse` adds modules in the opposite order (same patch).
inline int build_benchmark(Engine &engine, int n, int stems = 32, ModuleId first_id = 1, bool reverse = false) {
    struct Voice { ModuleId clock, env, lfo, vco, vca; float base; int wave; double bpm; };
    std::vector<Voice> voices;
    int count = 0;
    ModuleId id = first_id;
    for (int v = 0; count + 6 <= n; ++v, count += 5) {
        voices.push_back({id, id + 1, id + 2, id + 3, id + 4, 110.0f * (1.0f + static_cast<float>(v % 12) / 12.0f),
                          v % 2 == 0 ? daisysp::Oscillator::WAVE_POLYBLEP_SQUARE : daisysp::Oscillator::WAVE_SIN,
                          90.0 + v});
        id += 5;
    }
    std::vector<ModuleId> mixers;
    for (size_t c = 0; c < voices.size() && count < n; c += 4, ++count) mixers.push_back(id++);

    const float sr = engine.sample_rate();
    auto add_voice = [&](const Voice &v) {
        engine.add_module(v.clock, std::make_unique<modules::Clock>(static_cast<float>(v.bpm)));
        engine.add_module(v.env, std::make_unique<modules::Envelope>(0.01f, 0.0002f));
        engine.add_module(v.lfo, std::make_unique<modules::Vco>(sr, daisysp::Oscillator::WAVE_SIN, 0.3f, 0.0f));
        engine.add_module(v.vco, std::make_unique<modules::Vco>(sr, static_cast<uint8_t>(v.wave), v.base, 0.03f));
        engine.add_module(v.vca, std::make_unique<modules::Vca>());
    };
    if (reverse) {
        for (auto it = mixers.rbegin(); it != mixers.rend(); ++it) engine.add_module(*it, std::make_unique<modules::Mix4>());
        for (auto it = voices.rbegin(); it != voices.rend(); ++it) add_voice(*it);
    } else {
        for (const Voice &v : voices) add_voice(v);
        for (ModuleId m : mixers) engine.add_module(m, std::make_unique<modules::Mix4>());
    }
    for (const Voice &v : voices) {
        engine.connect(v.clock, 0, v.env, 0);
        engine.connect(v.lfo, 0, v.vco, 0);
        engine.connect(v.vco, 0, v.vca, 0);
        engine.connect(v.env, 0, v.vca, 1);
        engine.connect(v.vca, 0, v.vco, 1);  // feedback: legal with 1-sample cables
    }
    for (size_t m = 0; m < mixers.size(); ++m)
        for (size_t k = 0; k < 4 && m * 4 + k < voices.size(); ++k)
            engine.connect(voices[m * 4 + k].vca, 0, mixers[m], static_cast<int>(k));
    for (int s = 0; s < stems && s < static_cast<int>(voices.size()); ++s) engine.set_stem(s, voices[static_cast<size_t>(s)].vca, 0);
    return static_cast<int>(voices.size() * 5 + mixers.size());
}

}  // namespace stubby::patches
