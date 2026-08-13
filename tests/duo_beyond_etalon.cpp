// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the PowerBook Duo 230 (MSC + PG&E, 68030 @ 33 MHz, LCD
// 640×400) — shared-engine gate (BeyondBoot.h). Rig, boot loop and Finder
// signature from duo230_boot_etalon; keyboard through the PG&E's ADB cell
// (the matrix keyboard is still milestone 4); Time through the Mmu030Peek
// walk. POM68K_BEYOND=soak|persist. Soft-skips without the Duo ROM +
// pge_boot.bin + a bootable image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Mmu030Peek.h"
#include "MscCpu.h"
#include "MscMemory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/duo230.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM");
    std::string img = testasset::find("hdv/System 7.5.5 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the Duo ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != MscMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }
    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.pgeActive()) {
        std::printf("SKIP: no roms/pge/pge_boot.bin — the PMU cannot boot\n");
        return 0;
    }
    MscCpu cpu(mem);
    mem.setCpu(&cpu);
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    cpu.hardReset();
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = 33000000 / 60;
    const int W = MscMemory::kScreenW, H = MscMemory::kScreenH;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    auto finderUp = [&]() {
        std::vector<uint32_t> fb;
        mem.decodeScreen(fb);
        const double menu = beyondboot::darkRatio(fb, W, 0, W, 2, 18);
        const double desk = beyondboot::darkRatio(fb, W, W / 2, W, 40, H - 40);
        // duo230_boot_etalon's signature.
        return menu >= 0.0 && menu < 0.35 && desk > 0.20 && desk < 0.80;
    };
    auto boot = [&]() {
        long held = 0;
        while (mem.cpuHeld() && held < 400000) { mem.tick(1000); held++; }
        if (mem.cpuHeld()) return false;
        frames(12000);
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d, TC=$%08X, SCSI %ld\n", W, H, cpu.getTC(),
                mem.scsi().commands);

    // The persist leg is BLOCKED on F6 (SIMPLIFICATIONS_REVIEW.md): the
    // System reads the Duo's built-in keyboard through the PMU matrix,
    // which is still DUO_BRINGUP milestone 4 — mem.keyEvent feeds the
    // PG&E's ADB cell and the Finder never sees the keystrokes (verified
    // 2026-08-13: Cmd-N leaves the image byte-identical). A SKIP, not a
    // pass; delete this block the day the matrix lands.
    // The SOAK stays registered and RED on today's tree: it found that the
    // System clock is frozen (0 s over 180 s — no one-second source is
    // wired from the PG&E to the host), which is a real guest-visible
    // defect, recorded in TODO § 1.
    if (getenv("POM68K_BEYOND") && std::string(getenv("POM68K_BEYOND")) == "persist") {
        std::printf("SKIP: Duo persist is blocked on the F6 matrix keyboard "
                    "(DUO_BRINGUP milestone 4)\n");
        return 0;
    }

    beyondboot::Hooks h;
    h.name = "PowerBook Duo 230";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t phys = 0;
            if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(),
                                       0x20C + uint32_t(i), 5,
                                       [&](uint32_t a) { return mem.peek8(a); },
                                       &phys))
                return false;
            v = v << 8 | mem.peek8(phys);
        }
        *out = v;
        return true;
    };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        std::vector<uint32_t> fb;
        mem.decodeScreen(fb);
        beyondboot::dumpPpm((std::string("duo_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    return beyondboot::run(h);
}
