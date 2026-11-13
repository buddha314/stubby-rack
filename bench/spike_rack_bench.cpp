// Spike 1.5: cost of the VCV-style rack per 512-frame block (Godot's block), the same
// table stubby-music's spike_rack printed for the Rust rack (8.8 ns per module-sample
// at 200 modules). Run: scons bench
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/spike_rack.h"

int main() {
    constexpr int kBlock = 512;
    constexpr float kSr = 48000.0f;
    constexpr double kBudgetNs = kBlock / 48000.0 * 1e9;
    std::printf("| Modules | Cables | Mean / block | p99 | Max | Mean %% of block | ns per module-sample |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- |\n");
    for (int n : {50, 100, 200, 400}) {
        auto rack = stubby::spike::build_patch(n, kSr);
        std::vector<float> stems(static_cast<size_t>(rack->stem_count()) * kBlock);
        for (int i = 0; i < 200; ++i) rack->render(kBlock, stems.data());  // warm up
        std::vector<double> t;
        t.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            const auto a = std::chrono::steady_clock::now();
            rack->render(kBlock, stems.data());
            t.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count());
        }
        std::sort(t.begin(), t.end());
        double mean = 0;
        for (double x : t) mean += x;
        mean /= t.size();
        std::printf("| %d | %d | %.1f µs | %.1f µs | %.1f µs | %.1f%% | %.2f |\n", rack->module_count(),
                    rack->cable_count(), mean / 1e3, t[t.size() * 99 / 100] / 1e3, t.back() / 1e3,
                    100.0 * mean / kBudgetNs, mean / (rack->module_count() * kBlock));
    }
    // Feedback sanity: finite, bounded, deterministic.
    auto render = [] {
        auto rack = stubby::spike::build_patch(200, kSr);
        std::vector<float> s(static_cast<size_t>(rack->stem_count()) * 48000);
        rack->render(48000, s.data());
        return s;
    };
    auto a = render(), b = render();
    float peak = 0;
    bool finite = true;
    for (float x : a) { finite &= std::isfinite(x); peak = std::max(peak, std::fabs(x)); }
    std::printf("\nFeedback check (200 modules, 1 s): all finite = %s, peak = %.2f V, deterministic = %s\n",
                finite ? "true" : "false", peak, a == b ? "true" : "false");
}
