// POM68K — dev tool (not a gate): the table of contents of a CD image,
// exactly as ScsiDisk reads it.
//
// The CDDA work of 2026-09-17 was built and gated against SYNTHESIZED
// discs — a cue sheet this tree writes itself, with MODE1/2048 data and a
// handful of audio tracks. A pressed disc is a different animal: MODE1/2352
// sectors, PREGAP and FLAGS lines, dozens of tracks, and index times
// measured from the start of the file rather than the track. Before a guest
// is involved at all, this says whether our own reader agrees with the
// sheet: how many tracks, which are audio, where each starts, and how long
// the disc runs.
//
//     make -C build cd_toc_probe
//     build/cd_toc_probe cd/<image>.cue
//
// Prints one line per track and a total. Exit 1 when the image will not
// open, 0 otherwise — a reading tool, not a verdict.

#include "ScsiDisk.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {
// CD time is 75 frames a second, from the disc's start.
std::string msf(std::uint32_t lba) {
    char out[16];
    std::snprintf(out, sizeof out, "%02u:%02u:%02u", lba / (75 * 60),
                  (lba / 75) % 60, lba % 75);
    return out;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: cd_toc_probe <image.cue|.iso|.toast|.cdr>\n");
        return 1;
    }
    const std::string path = argv[1];
    ScsiDisk disc;
    if (!disc.openCdrom(path)) {
        std::fprintf(stderr, "cannot open %s as a CD image\n", path.c_str());
        return 1;
    }
    std::printf("%s\n  data blocks %u of %u bytes (%.1f MB)\n", path.c_str(),
                disc.blocks(), disc.blockSize(),
                double(disc.blocks()) * disc.blockSize() / 1048576.0);

    const std::size_t n = disc.trackCount();
    std::printf("  %zu track(s)\n", n);
    std::size_t audio = 0;
    std::uint32_t firstAudio = 0, lastStart = 0;
    for (std::size_t i = 0; i < n; i++) {
        const bool isAudio = disc.trackIsAudio(i);
        const std::uint32_t start = disc.trackStartLba(i);
        if (isAudio && !audio) firstAudio = start;
        if (isAudio) audio++;
        lastStart = start;
        // Every track of a 60-track disc is noise; the shape is what reads.
        if (n <= 12 || i < 4 || i + 2 >= n)
            std::printf("   %2zu %-5s start LBA %8u  (%s)\n", i + 1,
                        isAudio ? "AUDIO" : "DATA", start, msf(start).c_str());
        else if (i == 4)
            std::printf("   …  %zu more\n", n - 6);
    }
    std::printf("  %zu audio track(s)", audio);
    if (audio) std::printf(", first at %s, last starts %s",
                           msf(firstAudio).c_str(), msf(lastStart).c_str());
    std::printf("\n");
    return 0;
}
