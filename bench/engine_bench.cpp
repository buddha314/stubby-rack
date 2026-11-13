// The engine's budget benchmark (spec modular-rack "Realtime safety and budget"):
// stubby-music's spike_rack patch built through the Engine API, 1,000 blocks of 512
// frames per size. Run: scons bench && ./build/bench/engine_bench
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "core/engine.h"
#include "core/patches.h"

int main() {
    std::printf("| Modules | Mean / block | p99 | Max | Mean %% of block | ns per module-sample |\n");
    std::printf("| --- | --- | --- | --- | --- | --- |\n");
    for (int n : {50, 100, 200, 400}) {
        stubby::Engine e;
        const int modules = stubby::patches::build_benchmark(e, n);
        e.commit();
        for (int i = 0; i < 200; ++i) e.render(512);
        std::vector<double> t;
        for (int i = 0; i < 1000; ++i) {
            const auto a = std::chrono::steady_clock::now();
            e.render(512);
            t.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count());
        }
        std::sort(t.begin(), t.end());
        double mean = 0;
        for (double x : t) mean += x;
        mean /= t.size();
        std::printf("| %d | %.1f µs | %.1f µs | %.1f µs | %.1f%% | %.2f |\n", modules, mean / 1e3,
                    t[t.size() * 99 / 100] / 1e3, t.back() / 1e3, 100.0 * mean / (512 / 48000.0 * 1e9),
                    mean / (modules * 512.0));
    }
}
