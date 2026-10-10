// POM68K — gate `cd_pressed_toc_etalon`: the table of contents of the two
// pressed discs POM68K has played (`cd/`, private inputs).
//
// Both mounted on 2026-09-19 and both stopped opening on 2026-10-09, when a
// stricter sheet reader refused their PREGAP and FLAGS lines; no gate held
// them, so nothing went red (CHANGELOG 2026-10-10 (second)). This one reads
// each disc as the drive does — ScsiDisk::openCdrom, then READ TOC — and
// pins its data blocks, track count, selected starts, the Q control of an
// audio track and the lead-out. A disc absent from this host is skipped
// loudly; with neither present the gate is a SKIP.

#include "ScsiDisk.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Pinned {
    const char* cue;
    const char* name;
    std::uint32_t dataBlocks;
    std::size_t tracks;
    std::uint32_t track2, track3, lastTrack, leadOut;
    std::uint8_t track2Control;
};

std::uint32_t be32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
}

} // namespace

int main() {
    // Measured on 2026-10-10. Each track-2 start is the data track's end
    // plus the sheet's PREGAP 00:02:00 (150 sectors); each lead-out is the
    // BIN's 2352-byte sector count plus those same 150.
    const Pinned discs[] = {
        {"cd/BattleChessEnhanced/BattleChessEnhanced.cue", "BattleChess Enhanced",
         20634, 22, 20784, 90222, 190832, 311542, 0x10},
        {"cd/AppleCDExplorer.cue", "AppleCD Explorer",
         128080, 61, 128230, 129375, 223565, 226420, 0x11},
    };
    int present = 0;
    for (const Pinned& disc : discs) {
        if (!std::filesystem::is_regular_file(disc.cue)) {
            std::printf("  (%s absent: %s not read on this host)\n", disc.cue, disc.name);
            continue;
        }
        ++present;
        std::printf("%s (%s)\n", disc.name, disc.cue);
        ScsiDisk drive;
        const bool opened = drive.openCdrom(disc.cue);
        check(opened, "the sheet opens");
        if (!opened) continue;
        check(drive.blocks() == disc.dataBlocks, "data track blocks");
        check(drive.trackCount() == disc.tracks && !drive.trackIsAudio(0) &&
                  drive.trackIsAudio(1),
              "track count: one data track, then audio");
        check(drive.trackStartLba(1) == disc.track2 && drive.trackStartLba(2) == disc.track3 &&
                  drive.trackStartLba(disc.tracks - 1) == disc.lastTrack,
              "track 2, track 3 and the last track start where the sheet's gaps put them");
        std::vector<std::uint8_t> out;
        const std::vector<std::uint8_t> in;
        const std::uint8_t toc[10] = {0x43, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0};
        const bool answered = drive.command(toc, 10, out, in) == 0 &&
            out.size() == 4 + 8 * (disc.tracks + 1);
        const std::size_t leadOut = 4 + 8 * disc.tracks;
        std::printf("      lead-out %u\n", answered ? be32(&out[leadOut + 4]) : 0);
        if (disc.leadOut)
            check(answered && out[leadOut + 2] == 0xAA &&
                      be32(&out[leadOut + 4]) == disc.leadOut,
                  "READ TOC: the lead-out");
        check(answered && out[4 + 8 + 1] == disc.track2Control,
              "READ TOC: track 2's ADR/CTRL (0x10 audio, 0x11 with pre-emphasis)");
    }
    if (!present) {
        std::printf("SKIP: no pressed disc under cd/ on this host\n");
        return 0;
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
