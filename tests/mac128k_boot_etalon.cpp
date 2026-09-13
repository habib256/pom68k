// POM68K — Macintosh 128K / 512K boot gate: the two machines below the Plus
// boot a real 400K System floppy to the Finder desktop.
//
// These are `MacMemory` with less of it: a 64 KB ROM, 128 KB / 512 KB of
// soldered RAM (so the framebuffer sits at ramTop-$5900 = $1A700 on the
// 128K, not at $3FA700), the M0110 keyboard and quadrature mouse the Plus
// uses rather than ADB, and NO SCSI bus at all — the 5380 arrived with the
// Plus. The medium is the other half: a 400K disk is SINGLE-SIDED, which
// `SonyDrive` derives from the 409 600-byte image itself (`SonyDrive.cpp`
// :188 `doubleSided_`, :218 the half-size cylinder stride, :227 the SEL
// line the IWM drives unconditionally, :401 the $02 address-field format
// byte a double-sided disk signs as $22).
//
// Finder signature, the same one the Plus and the SE family are held to:
// white menu bar with black glyphs on top, 50 % gray dithered desktop
// below, and the disk still in the drive at the end.
// Soft-skips unless the model's 64 KB ROM and a 400K system disk are both
// present. POM68K_MAC128K_MODEL picks the sibling: mac128k (default) /
// mac512k.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    const char* which = getenv("POM68K_MAC128K_MODEL");
    MacMemory::Model model = MacMemory::Model::Mac128;
    // The archive spelling, which is the one the ROM actually ships under —
    // DEV.md § 2.12 names a gate that invents a filename as a trap paid for
    // twice: it SKIPs, exits 0 and counts green.
    const char* romRel = "roms/64KB ROMs/1984-01 - 28BA61CE - Macintosh 128.ROM";
    const char* name = "Macintosh 128K";
    std::uint32_t wantRam = 0x20000;
    if (which && !std::strcmp(which, "mac512k")) {
        model = MacMemory::Model::Mac512;
        romRel = "roms/64KB ROMs/1984-10 - 28BA4E50 - Macintosh 512K.ROM";
        name = "Macintosh 512K";
        wantRam = 0x80000;
    }

    // System 1.1 is 400K/MFS and boots on a 128K; 2.0 is the 512K-era
    // sibling. Either satisfies both machines, so take whichever is here.
    std::string dsk = find("disks35/System 1.1.dsk");
    if (dsk.empty()) dsk = find("disks35/System 2.0.dsk");
    const std::string rom = find(romRel);
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs %s + disks35/System 1.1.dsk (or 2.0)\n", romRel);
        return 0;
    }
    testasset::report({ rom, dsk });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), {});

    MacMemory mem(pom68k::defaultCoreConfig(), model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }

    // The board facts this profile exists for, asserted before the boot so a
    // failure downstream cannot be blamed on them.
    if (mem.ramSize() != wantRam || mem.romSize() != 0x10000 ||
        mem.hasScsi() || mem.isAdb()) {
        std::fprintf(stderr,
                     "FAIL: %s profile wrong (ram=%u rom=%u scsi=%d adb=%d)\n",
                     name, mem.ramSize(), mem.romSize(),
                     mem.hasScsi() ? 1 : 0, mem.isAdb() ? 1 : 0);
        return 1;
    }

    // Memory-model probe: the Sad Mac 0F0004 is the ROM's mod3 RAM test, so
    // the first question is whether OUR RAM is self-consistent through the
    // window that test uses ($600000, overlay up). Unique value per longword,
    // written through the alias and read back through both names.
    if (getenv("POM68K_MAC128K_MEMPROBE")) {
        const uint32_t top = mem.ramSize();
        for (uint32_t o = 0; o < top; o += 4) {
            const uint32_t v = 0xA5000000u ^ o;
            mem.write16(0x600000 + o, uint16_t(v >> 16));
            mem.write16(0x600000 + o + 2, uint16_t(v));
        }
        long bad = 0;
        for (uint32_t o = 0; o < top && bad < 8; o += 4) {
            const uint32_t want = 0xA5000000u ^ o;
            const uint32_t got = (uint32_t(mem.read16(0x600000 + o)) << 16) |
                                 mem.read16(0x600000 + o + 2);
            if (got != want) {
                std::printf("  MEMPROBE alias mismatch @+%06X want %08X got %08X\n",
                            o, want, got);
                bad++;
            }
        }
        std::printf("  MEMPROBE $600000 window over %u bytes: %ld mismatch(es)\n",
                    top, bad);
        std::printf("  MEMPROBE overlay=%d ramSize=%u romSize=%u\n",
                    mem.overlay() ? 1 : 0, mem.ramSize(), mem.romSize());
        return 0;
    }

    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(dsk)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    if (mem.internalDrive().doubleSided()) {
        std::fprintf(stderr, "FAIL: 400K medium came up double-sided\n");
        return 1;
    }

    // 128 KB of RAM tests in a blink next to the Plus's 4 MB, so the budget
    // here is the disk, not the memory check.
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES")) : 4000;
    const bool trace = getenv("POM68K_MAC128K_TRACE") != nullptr;
    // Side-effect-free (peek8): the ROM's own low-memory globals, which say
    // what it CONCLUDED rather than what we configured. MemTop $0108,
    // ScrnBase $0824 (Inside Macintosh III).
    auto peek32 = [&mem](uint32_t a) {
        return (unsigned(mem.peek8(a)) << 24) | (unsigned(mem.peek8(a + 1)) << 16) |
               (unsigned(mem.peek8(a + 2)) << 8) | unsigned(mem.peek8(a + 3));
    };
    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < kFrames; f++) {
        fc.runFrame(cpu, mem);
        if (trace && (f < 400 || f % 250 == 0)) {
            const uint8_t* ram = mem.ram();
            const uint32_t base = mem.screenBase();
            long ones = 0;
            for (uint32_t i = 0; i < 21888; i++)
                for (int b = 0; b < 8; b++) ones += (ram[base + i] >> b) & 1;
            std::printf("  f=%5ld pc=%06X overlay=%d track=%2d screen=$%X "
                        "black=%.3f D7=%08X A2=%08X MemTop=%08X ScrnBase=%08X\n",
                        f, unsigned(cpu.getPC()),
                        mem.overlay() ? 1 : 0, mem.internalDrive().currentTrack(),
                        base, double(ones) / (21888.0 * 8),
                        unsigned(cpu.getD(7)), unsigned(cpu.getA(2)),
                        peek32(0x0108), peek32(0x0824));
            std::fflush(stdout);
        }
    }

    if (mem.overlay()) { std::fprintf(stderr, "FAIL: overlay still on\n"); return 1; }
    if (!mem.internalDrive().hasDisk()) { std::fprintf(stderr, "FAIL: ejected\n"); return 1; }

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    if (const char* ppm = getenv("POM68K_MAC128K_PPM")) {
        std::ofstream out(ppm, std::ios::binary);
        out << "P5\n512 342\n255\n";
        for (int i = 0; i < 512 * 342; i++)
            out.put(char((fb[i] & 0xFF) ? 255 : 0));
    }
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    const double menuBar = blackRatio(2, 16);      // mostly white + glyphs
    const double desktop = blackRatio(240, 270);   // 50 % gray dither
    std::printf("%s: menu bar black %.2f (want <0.30), desktop %.2f "
                "(want ~0.50), track %d, screen $%X\n",
                name, menuBar, desktop, mem.internalDrive().currentTrack(),
                mem.screenBase());
    if (menuBar > 0.30 || menuBar < 0.01) {
        std::fprintf(stderr, "FAIL: no menu bar\n");
        return 1;
    }
    if (desktop < 0.40 || desktop > 0.60) {
        std::fprintf(stderr, "FAIL: no gray desktop\n");
        return 1;
    }
    std::printf("PASS: %s reaches the Finder\n", name);
    return 0;
}
