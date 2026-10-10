// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: a Quadra 630 boots from its IDE disk, with no SCSI disk on the bus.
//
// The F108 machines are the only 68k Macs whose internal disk is ATA, and
// this is the whole path end to end, done the way a person would:
//
//   1. a blank IDE image and the reference SCSI volume;
//   2. the guest's own **Drive Setup** partitions the ATA disk — Apple's
//      tool writing Apple's driver, so nothing here is synthesized: a
//      `$0701` driver descriptor, two `Apple_Driver_ATA` partitions, a
//      patch partition and an HFS one;
//   3. the SCSI volume is cloned into that HFS partition, host-side, which
//      is what cloning a disk is;
//   4. the machine is rebuilt with the IDE disk ALONE and must reach the
//      Finder;
//   5. an application rewind on that disk: the Finder writes a folder, the
//      machine is saved and writes a second one, and a new machine on the
//      same file loads the state, finds the file back at the saved bytes and
//      writes, from the same input, the same disk and RAM as the
//      uninterrupted run (SNOW_IMPLEMENTATION_PLAN.md, ATA continuation).
//
// Judged on the ROM's own traffic: the boot volume's blocks arrive over ATA
// and the SCSI controller sees nothing at all, which is the only way to
// tell an IDE boot from a SCSI one behind the same desktop.
//
// The clicks are Drive Setup's own buttons at the coordinates Mac OS 8.1
// puts them on a 640×480 screen. Runtime is minutes: it is a full format
// and a 300 MB clone.

#include "AssetFingerprint.h"
#include "HfsInject.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "SaveState.h"
#include "SaveStateMachines.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

uint32_t peek32(const Q630Memory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16 |
           uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

// The Apple partition map the guest just wrote, read back host-side.
struct Partition { uint32_t start = 0, blocks = 0; std::string type; };
std::vector<Partition> partitions(const std::string& path) {
    std::vector<Partition> out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    std::vector<uint8_t> head(512 * 64);
    f.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    for (int i = 1; i < 63; i++) {
        const uint8_t* p = head.data() + size_t(i) * 512;
        if (p[0] != 'P' || p[1] != 'M') break;
        Partition e;
        e.start  = uint32_t(p[8]) << 24 | uint32_t(p[9]) << 16 |
                   uint32_t(p[10]) << 8 | p[11];
        e.blocks = uint32_t(p[12]) << 24 | uint32_t(p[13]) << 16 |
                   uint32_t(p[14]) << 8 | p[15];
        e.type.assign(reinterpret_cast<const char*>(p + 48),
                      strnlen(reinterpret_cast<const char*>(p + 48), 32));
        out.push_back(e);
    }
    return out;
}
constexpr int kFrameCycles = 550000;              // 33 MHz / ~60 Hz

uint64_t fileHash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    return sav::hash(d);
}

// A folder of that name at the volume's root or on its desktop, read
// host-side from the IDE file through the catalog.
bool folderOnDisk(const std::string& path, const std::string& name) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    hfsinject::MemoryIo io(img);
    uint32_t start = 0, blocks = 0;
    std::string err;
    if (!hfsinject::findHfsVolume(io, start, blocks, err)) return false;
    hfsinject::Volume vol(io, start, blocks);
    if (!vol.open(err)) return false;
    const uint32_t desk = vol.findFolder(2, "Desktop Folder");
    return vol.findFolder(2, name) || (desk && vol.findFolder(desk, name));
}

// The Finder's second untitled folder, whichever numbering it uses.
bool secondFolderOnDisk(const std::string& path) {
    return folderOnDisk(path, "untitled folder 1") || folderOnDisk(path, "untitled folder 2");
}

// The Quadra 630 with its IDE disk alone, write-back.
struct Machine {
    Q630Memory mem;
    Q630Cpu cpu;
    bool ok = false;
    Machine(const pom68k::CoreConfig& core, const std::vector<uint8_t>& rom,
            const std::string& ide)
        : mem(core, 32u << 20), cpu(mem, jit::defaultResolvedConfig(), core.cpu) {
        ok = mem.loadRom(rom) && mem.attachIde(ide, true);
        if (!ok) return;
        mem.setCpu(&cpu);
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
    }
    void frames(long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
    }
    void clickAt(int tx, int ty) {
        for (int it = 0; it < 900; it++) {
            const int px = int16_t(mem.peek8(0x832) << 8 | mem.peek8(0x833));
            const int py = int16_t(mem.peek8(0x830) << 8 | mem.peek8(0x831));
            const int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            auto step = [](int d) {
                int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            frames(2);
        }
        frames(10);
        mem.mouseButton(true);  frames(5);
        mem.mouseButton(false); frames(5);
    }
    // Cmd-N ($37, $2D); a click on the empty desktop ends the name edit the
    // Finder opens on the new folder; then frames until the disk has been
    // quiet for three seconds: the File Manager's own flush, not a host one.
    void newFolder() {
        const long before = mem.ide().sectorsWritten;
        mem.keyEvent(0x37, true);  frames(4);
        mem.keyEvent(0x2D, true);  frames(4);
        mem.keyEvent(0x2D, false); frames(4);
        mem.keyEvent(0x37, false); frames(60);
        clickAt(320, 440);
        long last = -1;
        for (int quiet = 0, f = 0; quiet < 180 && f < 6000; f++) {
            frames(1);
            quiet = mem.ide().sectorsWritten == last ? quiet + 1 : 0;
            last = mem.ide().sectorsWritten;
        }
        std::printf("  new folder: %ld sectors written\n", mem.ide().sectorsWritten - before);
    }
    uint64_t ramHash() const {
        std::vector<uint8_t> ram(32u << 20);
        for (uint32_t a = 0; a < ram.size(); a++) ram[a] = mem.peek8(a);
        return sav::hash(ram);
    }
};
} // namespace

int main() {
    std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM",
        "roms/mame/macqd630/06684214.bin", "roms/quadra630.rom" });
    // POM68K_Q630_ROM=lc580 runs the other F108 profile — the LC/Performa
    // 580's own dump, a later ROM on the same board. The IDE path should
    // not care which of the two is in the socket, and this is how that is
    // checked rather than assumed.
    if (std::getenv("POM68K_Q630_ROM")) {
        const std::string alt = testasset::findAny({
            "roms/1MB ROMs/1995-04 - 064DC91D - LC, Performa 580 & Performa 588.ROM",
            "roms/mame/maclc580/064dc91d.bin" });
        if (!alt.empty()) romPath = alt;
    }
    std::string diskPath = testasset::findAny({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the 06684214 ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    // Room for the reference volume (300 MB) plus the driver and patch
    // partitions Drive Setup puts in front of it.
    const std::string idePath = std::getenv("POM68K_Q630_ROM")
                              ? "lc580_ide_boot.img" : "q630_ide_boot.img";
    {
        std::ofstream f(idePath, std::ios::binary);
        f.seekp(std::streamoff(330) * 1024 * 1024 - 1);
        f.put('\0');
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    // ── Phase 1: the guest partitions its own IDE disk ──────────────────
    {
        pom68k::CoreConfig core;
        Q630Memory mem(core, 32u << 20);
        if (!mem.loadRom(rom) || !mem.attachScsi(diskPath) ||
            !mem.attachIde(idePath, true)) {
            std::fprintf(stderr, "FAIL: could not compose the machine\n");
            return 1;
        }
        Q630Cpu cpu(mem, jit::defaultResolvedConfig(), core.cpu);
        mem.setCpu(&cpu);
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
        auto runFrames = [&](long n) {
            for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
        };
        auto clickAt = [&](int tx, int ty) {
            for (int it = 0; it < 900; it++) {
                const int px = int16_t(mem.peek8(0x832) << 8 | mem.peek8(0x833));
                const int py = int16_t(mem.peek8(0x830) << 8 | mem.peek8(0x831));
                const int dx = tx - px, dy = ty - py;
                if (!dx && !dy) break;
                auto step = [](int d) {
                    int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                    return std::max(-8, std::min(8, s));
                };
                mem.mouseMove(step(dx), step(dy));
                runFrames(2);
            }
            runFrames(10);
            mem.mouseButton(true);  runFrames(5);
            mem.mouseButton(false); runFrames(5);
        };
        // A double-click never takes in a headless run — the Finder selects
        // the icon and stops — so opening is click-then-Command-O ($37, $1F).
        auto open = [&] {
            runFrames(30);
            mem.keyEvent(0x37, true);  runFrames(4);
            mem.keyEvent(0x1F, true);  runFrames(4);
            mem.keyEvent(0x1F, false); runFrames(4);
            mem.keyEvent(0x37, false); runFrames(4);
        };
        runFrames(16000);                          // desktop, with the
        clickAt(364, 230); runFrames(600);         // "unreadable disk" dialog
        clickAt(590, 50);  clickAt(590, 50);       // the boot volume's icon
        runFrames(900);
        clickAt(63, 305);  open(); runFrames(900); // Utilities
        clickAt(383, 233); open(); runFrames(3000);// Drive Setup
        clickAt(72, 107);  runFrames(300);         // the ATA drive's row
        clickAt(256, 251); runFrames(900);         // « Initialize… »
        clickAt(278, 249); runFrames(12000);       // « Initialize »
        std::printf("phase 1: %ld ATA commands, %ld sectors written\n",
                    mem.ide().commands, mem.ide().sectorsWritten);
        check(mem.ide().sectorsWritten > 1000,
              "Drive Setup writes the disk: a map, a driver and a volume");
    }

    const std::vector<Partition> map = partitions(idePath);
    bool hasDriver = false;
    uint32_t hfsStart = 0, hfsBlocks = 0;
    for (const Partition& p : map) {
        if (p.type == "Apple_Driver_ATA" && p.blocks) hasDriver = true;
        if (p.type == "Apple_HFS" && p.blocks > hfsBlocks) {
            hfsStart = p.start; hfsBlocks = p.blocks;
        }
    }
    check(hasDriver, "and the driver is Apple's own Apple_Driver_ATA");
    check(hfsStart && hfsBlocks >= 614400,
          "with an HFS partition big enough for the reference volume");
    if (!hfsStart) {
        std::remove(idePath.c_str());
        std::remove((idePath + ".pomundo").c_str());
        return 1;
    }

    // ── Cloning the volume, which is what cloning a disk is ─────────────
    {
        std::ifstream s(diskPath, std::ios::binary);
        std::fstream d(idePath, std::ios::binary | std::ios::in | std::ios::out);
        s.seekg(std::streamoff(96) * 512);         // the reference volume's
        d.seekp(std::streamoff(hfsStart) * 512);   // Apple_HFS partition
        std::vector<char> buf(8u << 20);
        std::streamoff left = std::streamoff(614400) * 512;
        while (left > 0 && s && d) {
            const std::streamsize n = std::streamsize(std::min<std::streamoff>(
                left, std::streamoff(buf.size())));
            s.read(buf.data(), n);
            d.write(buf.data(), s.gcount());
            left -= s.gcount();
            if (!s.gcount()) break;
        }
        check(left == 0, "the reference volume is cloned into that partition");
    }

    // ── Phase 2: the IDE disk ALONE ─────────────────────────────────────
    pom68k::CoreConfig core;
    std::vector<uint8_t> state;
    uint64_t savedDisk = 0, onceDisk = 0, onceRam = 0;
    bool onceHasSecond = false;
    {
        Machine m(core, rom, idePath);
        if (!m.ok) {
            std::fprintf(stderr, "FAIL: could not compose the IDE-only machine\n");
            return 1;
        }
        m.frames(20000);
        if (m.cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

        std::printf("phase 2: %ld ATA commands, %ld sectors read; SCSI %ld\n",
                    m.mem.ide().commands, m.mem.ide().sectorsRead, m.mem.scsi().commands);
        check(m.mem.scsi().commands == 0, "nothing is on the SCSI bus: this is an IDE boot");
        check(m.mem.ide().sectorsRead > 5000,
              "and the System comes off the ATA disk, megabytes of it");
        // The Finder's own signature: a 640×480×8 screen with a drawn menu bar.
        check(peek32(m.mem, 0x0824) != 0 && m.mem.videoDepth() == 8,
              "the machine reaches a drawn 8-bit desktop");

        // ── Phase 3: an application rewind on the IDE disk ──────────────
        // The Finder writes a folder, the machine is saved, it writes a
        // second one; the uninterrupted run's disk and RAM are what the
        // restored machine must reproduce (DEV.md § 1.4, DiskTimeline.h).
        m.newFolder();
        check(folderOnDisk(idePath, "untitled folder"),
              "phase 3: the Finder's new folder reaches the IDE disk");
        pom68k::save(m.mem, m.cpu, pom68k::SnapMachine::Q630, state);
        savedDisk = fileHash(idePath);
        m.newFolder();
        onceDisk = fileHash(idePath);
        onceRam = m.ramHash();
        onceHasSecond = secondFolderOnDisk(idePath);
        check(onceHasSecond, "after the save, a second folder reaches it too");
        if (!onceHasSecond) {       // the names the catalog holds, to say why
            std::ifstream f(idePath, std::ios::binary);
            const std::string img((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            for (size_t at = img.find("untitled folder"); at != std::string::npos;
                 at = img.find("untitled folder", at + 1))
                std::printf("  catalog name at %zu: \"%s\"\n", at,
                            img.substr(at, std::min<size_t>(20, img.find('\0', at) - at)).c_str());
        }
    }
    {
        // A new machine on the same file, as a later process would open it.
        Machine m(core, rom, idePath);
        std::string err;
        const bool loaded = m.ok && pom68k::load(m.mem, m.cpu, pom68k::SnapMachine::Q630,
                                                 state.data(), state.size(), err);
        if (!err.empty()) std::printf("  load: %s\n", err.c_str());
        check(loaded, "a fresh machine on the same file loads the state");
        check(loaded && fileHash(idePath) == savedDisk,
              "the IDE file is back to the saved bytes");
        check(loaded && folderOnDisk(idePath, "untitled folder") &&
              !secondFolderOnDisk(idePath),
              "the first folder is there and the second is not");
        if (loaded) m.newFolder();
        check(loaded && fileHash(idePath) == onceDisk && m.ramHash() == onceRam,
              "the same input writes the same disk and RAM as the uninterrupted run");
    }

    std::remove(idePath.c_str());
    std::remove((idePath + ".pomundo").c_str());   // DiskTimeline's write-back journal
    std::printf(failures ? "FAILED\n"
                         : "PASSED — an F108 machine booted from its IDE disk and rewound it\n");
    return failures ? 1 : 0;
}
