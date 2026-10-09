// Read-amplifier output, never magnetic surface data.
#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace floppy {
// MAME 0.285 floppy.cpp get_next_transition and the floppy technical
// reference: AGC sees noise after 16 us without transitions. Its 50%/4 us
// distribution is an approximation, not a measured Sony amplifier response.
// Stateless hashing gives one stream independent of polling granularity and
// identical after restore. Absolute time makes later revolutions differ.
inline int64_t readNoise(const std::vector<int64_t>& edges, int64_t revolution,
                         int64_t tick, uint64_t identity, int64_t physical) {
    constexpr int64_t delay = 16000LL * 20054016 / 1250000;
    constexpr int64_t interval = 4000LL * 20054016 / 1250000;
    int64_t previous = 0;
    if (!edges.empty()) {
        const int64_t base = tick / revolution * revolution;
        auto next = std::upper_bound(edges.begin(), edges.end(), tick - base);
        previous = next == edges.begin() ? base - revolution + edges.back()
                                        : base + *std::prev(next);
    }
    const int64_t start = previous + delay;
    int64_t slot = tick < start ? 0 : (tick - start) / interval;
    for (;;) {
        const int64_t candidate = start + slot * interval + interval / 2;
        if (candidate >= physical) return physical;
        if (candidate >= tick) {
            uint64_t x = uint64_t(previous) ^ identity ^
                         (uint64_t(slot) * 0x9e3779b97f4a7c15ULL);
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            if ((x ^ (x >> 31)) & 1) return candidate;
        }
        ++slot;
    }
}
} // namespace floppy
