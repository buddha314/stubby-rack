#include "core/spike_rack.h"

#include <cmath>

#include "Synthesis/oscillator.h"

namespace stubby::spike {

namespace {

/// Oscillator. Inputs: V/oct, FM (linear, scaled by fm_index). Out: ±5 V.
struct Vco : Module {
    daisysp::Oscillator osc;
    float base_hz, fm_index;
    Vco(float sr, uint8_t waveform, float base, float fm) : base_hz(base), fm_index(fm) {
        osc.Init(sr);
        osc.SetWaveform(waveform);
        osc.SetAmp(1.0f);
    }
    int num_inputs() const override { return 2; }
    int num_outputs() const override { return 1; }
    void process(const float *in, float *out) override {
        const float hz = base_hz * std::exp2(in[0]) * (1.0f + fm_index * in[1]);
        osc.SetFreq(hz > 0.0f ? hz : 0.0f);
        const float v = osc.Process();
        out[0] = std::isfinite(v) ? v * 5.0f : 0.0f;
    }
};

struct Clock : Module {
    double spb, pos = 0.0;
    explicit Clock(double samples_per_beat) : spb(samples_per_beat) {}
    int num_inputs() const override { return 0; }
    int num_outputs() const override { return 1; }
    void process(const float *, float *out) override {
        out[0] = pos < spb * 0.5 ? 10.0f : 0.0f;
        pos += 1.0;
        if (pos >= spb) pos -= spb;
    }
};

/// Attack/decay envelope, retriggered on a rising gate (Schmitt 0.1 / 1.0 V).
struct Env : Module {
    float level = 0.0f, attack, decay;
    bool rising = false, gate_high = false;
    Env(float a, float d) : attack(a), decay(d) {}
    int num_inputs() const override { return 1; }
    int num_outputs() const override { return 1; }
    void process(const float *in, float *out) override {
        const float g = in[0];
        if (!gate_high && g >= 1.0f) { gate_high = true; rising = true; }
        else if (gate_high && g <= 0.1f) gate_high = false;
        if (rising) {
            level += attack;
            if (level >= 1.0f) { level = 1.0f; rising = false; }
        } else {
            level = level - decay > 0.0f ? level - decay : 0.0f;
        }
        out[0] = level * 10.0f;
    }
};

/// out = in0 * in1 / 10 V.
struct Vca : Module {
    int num_inputs() const override { return 2; }
    int num_outputs() const override { return 1; }
    void process(const float *in, float *out) override { out[0] = in[0] * in[1] * 0.1f; }
};

struct Mix4 : Module {
    int num_inputs() const override { return 4; }
    int num_outputs() const override { return 1; }
    void process(const float *in, float *out) override { out[0] = (in[0] + in[1] + in[2] + in[3]) * 0.25f; }
};

}  // namespace

int Rack::add(std::unique_ptr<Module> m) {
    in_base_.push_back(static_cast<int>(in_src_.size()));
    out_base_.push_back(static_cast<int>(slots_[0].size()));
    n_in_.push_back(m->num_inputs());
    n_out_.push_back(m->num_outputs());
    in_src_.insert(in_src_.end(), m->num_inputs(), kNone);
    for (auto &s : slots_) s.insert(s.end(), m->num_outputs(), 0.0f);
    modules_.push_back(std::move(m));
    return static_cast<int>(modules_.size()) - 1;
}

void Rack::cable(int from_module, int from_port, int to_module, int to_port) {
    in_src_[in_base_[to_module] + to_port] = static_cast<uint32_t>(out_base_[from_module] + from_port);
}

int Rack::cable_count() const {
    int n = 0;
    for (uint32_t s : in_src_) n += s != kNone;
    return n;
}

void Rack::render(int frames, float *stems_out) {
    float in[kMaxInputs];
    const int count = static_cast<int>(modules_.size());
    for (int i = 0; i < frames; ++i) {
        const float *prev = slots_[parity_ ^ 1].data();
        float *cur = slots_[parity_].data();
        for (int m = 0; m < count; ++m) {
            const int n_in = n_in_[m], base = in_base_[m];
            for (int k = 0; k < n_in; ++k) {
                const uint32_t src = in_src_[base + k];
                in[k] = src == kNone ? 0.0f : prev[src];
            }
            modules_[m]->process(in, cur + out_base_[m]);
        }
        for (size_t s = 0; s < stems_.size(); ++s) stems_out[s * frames + i] = cur[stems_[s]];
        parity_ ^= 1;
    }
}

std::unique_ptr<Rack> build_patch(int n, float sr) {
    auto rack = std::make_unique<Rack>();
    std::vector<int> vcas;
    int voice = 0;
    while (rack->module_count() + 6 <= n) {
        const float base = 110.0f * (1.0f + static_cast<float>(voice % 12) / 12.0f);
        const int clock = rack->add(std::make_unique<Clock>(sr * 60.0 / (90.0 + voice)));
        const int env = rack->add(std::make_unique<Env>(0.01f, 0.0002f));
        const int lfo = rack->add(std::make_unique<Vco>(sr, daisysp::Oscillator::WAVE_SIN, 0.3f, 0.0f));
        const uint8_t wave = voice % 2 == 0 ? daisysp::Oscillator::WAVE_POLYBLEP_SQUARE : daisysp::Oscillator::WAVE_SIN;
        const int vco = rack->add(std::make_unique<Vco>(sr, wave, base, 0.03f));
        const int vca = rack->add(std::make_unique<Vca>());
        rack->cable(clock, 0, env, 0);
        rack->cable(lfo, 0, vco, 0);
        rack->cable(vco, 0, vca, 0);
        rack->cable(env, 0, vca, 1);
        rack->cable(vca, 0, vco, 1);  // feedback: legal thanks to the 1-sample cable
        vcas.push_back(vca);
        ++voice;
    }
    for (size_t c = 0; c < vcas.size(); c += 4) {
        if (rack->module_count() >= n) break;
        const int mix = rack->add(std::make_unique<Mix4>());
        for (size_t k = 0; k < 4 && c + k < vcas.size(); ++k) rack->cable(vcas[c + k], 0, mix, static_cast<int>(k));
    }
    for (size_t v = 0; v < vcas.size() && v < 32; ++v) rack->tap_stem(vcas[v]);
    return rack;
}

}  // namespace stubby::spike
