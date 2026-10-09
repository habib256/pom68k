// Capture the existing physical sector-image encoder into independent MOOF
// chunks for real-ROM consumers. Output is temporary private fixture data.
#include "SonyDrive.h"
#include <fstream>
#include <cstring>

static void put(std::vector<uint8_t>& b, size_t at, uint64_t n, int size) {
    for (int i = 0; i < size; ++i) b[at + size_t(i)] = uint8_t(n >> (i * 8));
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SonyDrive drive;
    if (!drive.insert(argv[1])) return 1;
    if (drive.hasNativeTracks()) {
        // Re-export an existing native reference through the product writer.
        // Rewrite the first flux tick identically to mark the temporary copy
        // dirty without changing a sector, transition or the source file.
        std::ifstream source(argv[1], std::ios::binary);
        std::ofstream copy(argv[2], std::ios::binary);
        copy << source.rdbuf(); copy.close();
        if (!copy || !drive.insert(argv[2]) || drive.isWriteProtected()) return 1;
        std::vector<int64_t> edge;
        if (!drive.debugFlux().empty() && drive.debugFlux().front() == 0) edge.push_back(0);
        drive.commitFlux(0, 1, edge, false);
        drive.setWriteBack(true);
        return drive.flushToFile() ? 0 : 1;
    }
    std::vector<uint8_t> b(1536, 0);
    std::memcpy(b.data(), "MOOF\xff\x0a\x0d\x0a", 8);
    std::memcpy(b.data() + 12, "INFO", 4); put(b, 16, 60, 4);
    b[20] = 1; b[21] = drive.isHd() ? 3 : drive.doubleSided() ? 2 : 1;
    b[22] = 1; b[24] = 1; // protect the reference capture, 125 ns bit grid
    std::fill(b.begin() + 25, b.begin() + 57, ' ');
    constexpr char creator[] = "POM68K test capture";
    std::memcpy(b.data() + 25, creator, sizeof(creator) - 1);
    std::memcpy(b.data() + 80, "TMAP", 4); put(b, 84, 160, 4);
    std::fill(b.begin() + 88, b.begin() + 248, 255);
    std::memcpy(b.data() + 248, "TRKS", 4);
    uint64_t largest = 0;
    auto units = [](int64_t t) { return uint64_t((t * 10000 + 10027008) / 20054016); };
    for (int track = 0; track < 80; ++track) {
        for (int side = 0; side < (drive.doubleSided() ? 2 : 1); ++side) {
            drive.fluxAngleTicks(side != 0);
            const size_t entry = size_t(track * 2 + side);
            const uint64_t count = units(drive.fluxRevTicks());
            const size_t blocks = size_t((count + 4095) / 4096), start = b.size();
            b.resize(start + blocks * 512, 0); b[88 + entry] = uint8_t(entry);
            put(b, 256 + entry * 8, start / 512, 2); put(b, 258 + entry * 8, blocks, 2);
            put(b, 260 + entry * 8, count, 4); largest = std::max(largest, uint64_t(blocks));
            for (int64_t edge : drive.debugFlux()) {
                const uint64_t bit = units(edge) % count;
                b[start + size_t(bit / 8)] |= uint8_t(0x80 >> (bit & 7));
            }
        }
        drive.commandSwim(0); drive.commandSwim(1);
    }
    put(b, 58, largest, 2); put(b, 252, b.size() - 256, 4);
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return out ? 0 : 1;
}
