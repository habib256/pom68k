// POM68K — physical tracks survive seeks, mode changes and machine resets.
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace floppy {
// Generous one-turn resource bound (60 RPM); supported Sony mechanisms
// rotate much faster. It also bounds products in the spindle clock math.
inline constexpr int64_t kMaxRevolution = 15667200LL * 1024;
struct Track {
    bool present = false;
    int64_t revolution = 0, decodeClock = 0;
    std::vector<int64_t> edges;
    template<class Ar> void visit(Ar& ar) {
        ar(present, revolution, decodeClock, edges);
        if constexpr (Ar::loading) {
            if (revolution < 0 || revolution > kMaxRevolution || decodeClock < 0 ||
                decodeClock > kMaxRevolution || (!present && (revolution || decodeClock || !edges.empty())) ||
                (present && !revolution) || !std::is_sorted(edges.begin(), edges.end()) ||
                (!edges.empty() && (edges.front() < 0 || edges.back() >= revolution)) ||
                std::adjacent_find(edges.begin(), edges.end()) != edges.end()) ar.fail();
        }
    }
};
class TrackMedium {
public:
    bool native = false;
    std::array<Track, 160> tracks{};
    void clear() { native = false; for (auto& track : tracks) track = {}; }
    void retain(int index, const std::vector<int64_t>& edges, int64_t rev, int64_t clock) {
        if (index >= 0 && index < 160 && rev > 0) tracks[size_t(index)] = {true, rev, clock, edges};
    }
    void invalidate(int index) { if (index >= 0 && index < 160) tracks[size_t(index)] = {}; }
    template<class Ar> void visit(Ar& ar) { ar(native, tracks); }
};
} // namespace floppy
