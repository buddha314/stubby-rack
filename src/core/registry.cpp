#include "core/registry.h"

#include "core/modules/basic.h"
#include "core/modules/clocks.h"
#include "core/modules/sampler.h"

namespace stubby {

namespace {

std::vector<KindInfo> build_kinds() {
    const ParamInfo freq{"freq", 1.0f, 20000.0f, volts::kOscillatorF0, "Hz"};
    const ParamInfo fm{"fm_index", 0.0f, 1.0f, 0.0f, ""};
    std::vector<KindInfo> k;
    k.push_back({"constant", {}, {"out"}, {{"volts", -10.0f, 10.0f, 0.0f, "V"}}});
    k.push_back({"thru", {"in"}, {"out"}, {}});
    k.push_back({"tempo_clock", {}, {"gate"}, {{"multiple", 1.0f, 960.0f, 1.0f, "x"}}});
    k.push_back({"random_clock", {}, {"trigger"},
                 {{"min_seconds", 0.001f, 600.0f, 2.0f, "s"}, {"max_seconds", 0.001f, 600.0f, 8.0f, "s"}}});
    k.push_back({"free_clock", {}, {"gate"}, {{"bpm", 1.0f, 960.0f, 120.0f, "BPM"}}});
    k.push_back({"envelope", {"gate"}, {"out"},
                 {{"attack", 0.0f, 1.0f, 0.01f, "/sample"}, {"decay", 0.0f, 1.0f, 0.0002f, "/sample"}}});
    for (const char *wave : {"vco_sine", "vco_triangle", "vco_saw", "vco_square"})
        k.push_back({wave, {"voct", "fm"}, {"out"}, {freq, fm}});
    k.push_back({"vca", {"in", "cv"}, {"out"}, {}});
    k.push_back({"mix4", {"in1", "in2", "in3", "in4"}, {"out"}, {}});
    KindInfo sampler{"sampler", {}, {"out"}, {}};
    for (int v = 0; v < modules::SamplerPool::kVoices; ++v) {
        const std::string n = std::to_string(v + 1);
        sampler.params.push_back({"sample_" + n, -1.0f, 65535.0f, -1.0f, ""});
        sampler.params.push_back({"trigger_" + n, -1e9f, 1e9f, 0.0f, ""});
        sampler.params.push_back({"gain_" + n, 0.0f, 4.0f, 1.0f, ""});
    }
    k.push_back(sampler);
    return k;
}

std::unique_ptr<Module> build(const std::string &kind, const BuildContext &c) {
    using namespace modules;
    using daisysp::Oscillator;
    if (kind == "constant") return std::make_unique<Constant>();
    if (kind == "thru") return std::make_unique<Thru>();
    if (kind == "tempo_clock") return std::make_unique<TempoClock>(1);
    if (kind == "random_clock") return std::make_unique<RandomClock>(c.seed, 2.0f, 8.0f);
    if (kind == "free_clock") return std::make_unique<Clock>();
    if (kind == "envelope") return std::make_unique<Envelope>();
    if (kind == "vco_sine") return std::make_unique<Vco>(c.sample_rate, Oscillator::WAVE_SIN);
    if (kind == "vco_triangle") return std::make_unique<Vco>(c.sample_rate, Oscillator::WAVE_POLYBLEP_TRI);
    if (kind == "vco_saw") return std::make_unique<Vco>(c.sample_rate, Oscillator::WAVE_POLYBLEP_SAW);
    if (kind == "vco_square") return std::make_unique<Vco>(c.sample_rate, Oscillator::WAVE_POLYBLEP_SQUARE);
    if (kind == "vca") return std::make_unique<Vca>();
    if (kind == "mix4") return std::make_unique<Mix4>();
    if (kind == "sampler") return std::make_unique<SamplerPool>(c.bank);
    return nullptr;
}

}  // namespace

const std::vector<KindInfo> &module_kinds() {
    static const std::vector<KindInfo> kinds = build_kinds();
    return kinds;
}

const KindInfo *find_kind(const std::string &kind) {
    for (const KindInfo &k : module_kinds())
        if (k.kind == kind) return &k;
    return nullptr;
}

std::unique_ptr<Module> make_module(const std::string &kind, const BuildContext &ctx) {
    const KindInfo *info = find_kind(kind);
    if (!info) return nullptr;
    std::unique_ptr<Module> m = build(kind, ctx);
    if (!m) return nullptr;
    for (size_t p = 0; p < info->params.size(); ++p) m->set_param(static_cast<int>(p), info->params[p].def);
    return m;
}

}  // namespace stubby
