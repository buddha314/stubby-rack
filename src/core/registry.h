#pragma once
// The core's module registry (design D7, task 6.2): every module kind by name, with
// its ports and parameters in index order. The Godot side builds modules by kind, and
// a RackModuleDefinition resource must declare the same ports and parameters (a test
// checks that they agree).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/module.h"
#include "core/sample_bank.h"

namespace stubby {

struct ParamInfo {
    std::string name;
    float min, max, def;
    std::string unit;
};

struct KindInfo {
    std::string kind;
    std::vector<std::string> inputs, outputs;
    std::vector<ParamInfo> params;
};

/// Everything a module needs at construction (built on the main thread).
struct BuildContext {
    float sample_rate = 48000.0f;
    uint64_t seed = 1;                  // for seeded modules (random clocks)
    const SampleBank *bank = nullptr;   // for the sampler
};

/// Every registered kind, in a stable order.
const std::vector<KindInfo> &module_kinds();
/// The kind's info, or nullptr.
const KindInfo *find_kind(const std::string &kind);
/// A new module of `kind` with its default parameters, or nullptr for an unknown kind.
std::unique_ptr<Module> make_module(const std::string &kind, const BuildContext &ctx);

}  // namespace stubby
