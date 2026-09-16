// POM68K — Macintosh 128K / 512K beyond-boot gate: the Finder writes a file
// to the 400 K MFS floppy and the host reads it back.
//
// The boot gate (mac128k_boot_etalon) proves the desktop; this one proves
// the File Manager below it, on the medium the machine actually has: after
// the Finder is up, the mouse selects the "Welcome!" document on the
// desktop and Cmd-D asks for a duplicate. Finder 1.1 copies the file to
// "Copy of Welcome!" — a new MFS directory entry with its own data fork —
// and the host reads the in-memory floppy image with MfsVolume.h: the new
// entry exists, its data fork is byte-identical to the original's, the
// volume's file count and free-block count moved by the right amounts,
// and a reboot on the modified floppy still reaches the Finder with the
// copy still there (the write survived the Finder's own directory flush).
//
// No folder is involved: MFS has none (folders live only in the Finder's
// DeskTop file), which is why the observable is a duplicated file rather
// than the `folderprobe` count the HFS gates use.
//
// Soft-skips like the boot gate: the model's 64 KB ROM and the System 1.1
// floppy. POM68K_MAC128K_MODEL=mac512k picks the sibling;
// POM68K_MAC128K_PPM=<path> dumps the screen at each step (suffixes
// -finder, -selected, -copied, -rebooted).

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "MfsVolume.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

MacMemory* gMem = nullptr;
Cpu68k* gCpu = nullptr;
MacFrameClock gClock;

void runFrames(long n) {
    for (long i = 0; i < n; i++) gClock.runFrame(*gCpu, *gMem);
}

// Low-memory Mouse ($830 v, $832 h): where the guest believes the pointer
// is. The quadrature mouse is relative, so the move is a feedback loop.
void pointer(int& x, int& y) {
    x = int16_t(uint16_t(gMem->peek8(0x832)) << 8 | gMem->peek8(0x833));
    y = int16_t(uint16_t(gMem->peek8(0x830)) << 8 | gMem->peek8(0x831));
}

bool moveTo(int tx, int ty) {
    const bool trace = getenv("POM68K_MFS_TRACE") != nullptr;
    for (int i = 0; i < 1500; i++) {
        int x, y;
        pointer(x, y);
        if (trace && (i < 10 || i % 20 == 0))
            std::printf("  mouse[%3d]: guest (%d,%d)\n", i, x, y);
        if (x == tx && y == ty) return true;
        // One quadrature step per axis per frame: the 1.1 mouse driver
        // scales a fast burst (8 steps a frame landed 16 pixels, measured
        // 2026-09-16) and a sub-frame remainder then never lands; one step
        // a frame is one pixel, exactly.
        auto step = [](int d) { return d > 0 ? 1 : d < 0 ? -1 : 0; };
        gMem->mouseMove(step(tx - x), step(ty - y));
        runFrames(1);
    }
    int x, y;
    pointer(x, y);
    std::printf("mouse: wanted (%d,%d), guest sees (%d,%d)\n", tx, ty, x, y);
    return false;
}

void click() {
    gMem->mouseButton(true);
    runFrames(4);
    gMem->mouseButton(false);
    runFrames(12);
}

void keyTap(uint8_t code) {
    gMem->keyEvent(code, true);
    runFrames(3);
    gMem->keyEvent(code, false);
    runFrames(3);
}

struct Screen {
    double menuBar = 0, desktop = 0;
};

Screen screen(const char* suffix) {
    MacVideo video;
    const uint32_t* fb = video.render(*gMem);
    if (const char* ppm = getenv("POM68K_MAC128K_PPM")) {
        std::ofstream out(std::string(ppm) + "-" + suffix + ".ppm", std::ios::binary);
        out << "P5\n512 342\n255\n";
        for (int i = 0; i < 512 * 342; i++) out.put(char((fb[i] & 0xFF) ? 255 : 0));
    }
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    return {blackRatio(2, 16), blackRatio(240, 270)};
}

bool finderUp(const char* suffix) {
    const Screen s = screen(suffix);
    const bool ok = s.menuBar < 0.30 && s.menuBar > 0.01 &&
                    s.desktop > 0.40 && s.desktop < 0.60;
    std::printf("screen[%s]: menu bar black %.2f, desktop %.2f — %s\n", suffix,
                s.menuBar, s.desktop, ok ? "Finder" : "not the Finder");
    return ok;
}

} // namespace

int main() {
    const char* which = getenv("POM68K_MAC128K_MODEL");
    MacMemory::Model model = MacMemory::Model::Mac128;
    const char* romRel = "roms/64KB ROMs/1984-01 - 28BA61CE - Macintosh 128.ROM";
    const char* name = "Macintosh 128K";
    if (which && !std::strcmp(which, "mac512k")) {
        model = MacMemory::Model::Mac512;
        romRel = "roms/64KB ROMs/1984-10 - 28BA4E50 - Macintosh 512K.ROM";
        name = "Macintosh 512K";
    }
    // System 1.1's Finder (1.1g) is the one this gate drives; System 2.0
    // would do as well, but the desktop layout the click relies on is the
    // 1.1 volume's, so the fallback is explicit rather than silent.
    const std::string dsk = testasset::find("disks35/System 1.1.dsk");
    const std::string rom = testasset::find(romRel);
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs %s + disks35/System 1.1.dsk\n", romRel);
        return 0;
    }
    testasset::report({ rom, dsk });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), {});

    MacMemory mem(pom68k::defaultCoreConfig(), model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(dsk)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    gMem = &mem;
    gCpu = &cpu;
    gClock.resync(cpu);

    using namespace pom68k::mfs;
    const std::vector<uint8_t>& image = mem.internalDrive().image();
    const Volume before = parse(image);
    if (!before.valid) { std::fprintf(stderr, "FAIL: floppy is not MFS: %s\n", before.error.c_str()); return 1; }
    const File* original = before.find("Welcome!");
    if (!original) { std::fprintf(stderr, "FAIL: no Welcome! on the floppy\n"); return 1; }
    const std::vector<uint8_t> originalData = dataFork(image, before, *original);
    std::printf("%s: floppy '%s', %zu files, %u free blocks; Welcome! data fork %u B\n",
                name, before.name.c_str(), before.files.size(), before.freeBlocks,
                unsigned(original->dataLength));

    // ── Boot to the Finder (the boot gate's budget and signature) ──
    const long kBootFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES")) : 4000;
    runFrames(kBootFrames);
    if (!finderUp("finder")) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }

    // ── Select Welcome! on the desktop, Cmd-D ──
    // The 1.1 desktop lays the startup disk top-right and the Welcome!
    // document under it (icon centre ~472,105).
    const int iconX = getenv("POM68K_MFS_ICON_X") ? atoi(getenv("POM68K_MFS_ICON_X")) : 472;
    const int iconY = getenv("POM68K_MFS_ICON_Y") ? atoi(getenv("POM68K_MFS_ICON_Y")) : 105;
    if (!moveTo(iconX, iconY)) { std::fprintf(stderr, "FAIL: mouse did not reach the icon\n"); return 1; }
    click();
    runFrames(30);
    screen("selected");
    mem.keyEvent(0x37, true);              // Cmd
    runFrames(6);
    keyTap(0x02);                          // 'd' — Duplicate
    mem.keyEvent(0x37, false);
    // The copy: 8906 bytes through the File Manager onto a 400 K drive,
    // plus the Finder's directory and DeskTop updates. ~12 s of guest time.
    runFrames(720);
    screen("copied");

    // ── Host verification on the in-memory medium ──
    const Volume after = parse(image);
    if (!after.valid) { std::fprintf(stderr, "FAIL: floppy no longer parses: %s\n", after.error.c_str()); return 1; }
    const File* copy = after.find("Copy of Welcome!");
    std::printf("after: %zu files (drNmFls %u), %u free blocks%s%s\n",
                after.files.size(), after.fileCount, after.freeBlocks,
                copy ? ", 'Copy of Welcome!' present" : ", NO 'Copy of Welcome!'",
                after.error.empty() ? "" : (" — " + after.error).c_str());
    for (const File& f : after.files)
        if (!before.find(f.name))
            std::printf("  new entry: '%s' #%u data %u B rsrc %u B\n", f.name.c_str(),
                        f.fileNumber, unsigned(f.dataLength), unsigned(f.rsrcLength));
    bool ok = copy != nullptr && after.error.empty();
    if (copy) {
        const std::vector<uint8_t> copyData = dataFork(image, after, *copy);
        const bool same = copyData.size() == originalData.size() && copyData == originalData;
        const long blocksUsed = long(before.freeBlocks) - long(after.freeBlocks);
        std::printf("copy: data fork %zu B %s the original, file #%u (next was %u), "
                    "%ld block(s) consumed\n", copyData.size(),
                    same ? "identical to" : "DIFFERS from", copy->fileNumber,
                    before.nextFileNumber, blocksUsed);
        ok = ok && same && after.files.size() == before.files.size() + 1 &&
             copy->fileNumber >= before.nextFileNumber && blocksUsed >= 9 &&
             copy->dataStart != original->dataStart;
    }

    // ── Reboot on the modified floppy: still the Finder, copy still there ──
    cpu.hardReset();
    gClock.resync(cpu);
    runFrames(kBootFrames);
    const bool rebooted = finderUp("rebooted");
    const Volume again = parse(image);
    const bool kept = again.valid && again.find("Copy of Welcome!") &&
                      dataFork(image, again, *again.find("Copy of Welcome!")) == originalData;
    std::printf("reboot: %s, copy %s\n", rebooted ? "Finder" : "NO Finder",
                kept ? "survived" : "did NOT survive");
    ok = ok && rebooted && kept;

    std::printf("%s: %s\n", ok ? "PASS" : "FAIL",
                ok ? "the Finder duplicated a file on the MFS floppy and the host read it back"
                   : "see above");
    return ok ? 0 : 1;
}
