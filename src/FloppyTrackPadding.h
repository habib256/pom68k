// POM68K — formatted gap4 remains clocked across the index boundary.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pom68k::floppy {
// IBM System 34 MFM gap4 uses encoded 4E bytes (MAME flopimg.cpp
// build_pc_track_mfm); Macintosh GCR uses FF self-sync groups. A formatted
// sector image must not acquire a transition-free arc when sized to one turn.
inline void padFormattedTrack(std::vector<uint8_t>& cells, size_t target, bool mfm) {
    if (mfm) {
        bool last = !cells.empty() && cells.back();
        while (cells.size() < target) {
            for (int bit = 7; bit >= 0 && cells.size() < target; --bit) {
                const bool data = (0x4e >> bit) & 1;
                cells.push_back(uint8_t(!last && !data));
                if (cells.size() < target) cells.push_back(uint8_t(data));
                last = data;
            }
        }
    } else {
        while (cells.size() + 10 <= target) {
            for (int bit = 0; bit < 8; ++bit) cells.push_back(1);
            cells.push_back(0); cells.push_back(0);
        }
        if (cells.size() < target) cells.resize(target, 0);
    }
}
} // namespace pom68k::floppy
