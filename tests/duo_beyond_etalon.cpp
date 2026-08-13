// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the PowerBook Duo 230 (MSC + PG&E, 68030 @ 33 MHz, LCD
// 640×400) — shared-engine gate (BeyondBoot.h). Rig, boot loop and Finder
// signature from duo230_boot_etalon; keyboard through the PG&E's own
// matrix scanner (PgePmu, DUO_BRINGUP milestone 4 — landed 2026-08-13);
// Time through the Mmu030Peek walk. POM68K_BEYOND=soak|persist. Soft-skips
// without the Duo ROM + pge_boot.bin + a bootable image.

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
    // 4 MB — the Duo 230's factory size, and a rig choice with a measured
    // reason: System 7.5.5 sizes its disk cache from RAM, and on 8 MB it
    // holds the new folder's catalog blocks indefinitely (the Finder
    // creates the folder — a screen dump shows the icon — and the guest
    // issues no write command at all in the two emulated minutes that
    // follow). The Mac II showed the identical split at the identical
    // sizes on its own volume.
    MscMemory mem(4u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
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
        // duo230_boot_etalon's signature, plus "no modal dialog": the 7.5.5
        // volume opens one at every boot ("the alias 'Infinite HD' could
        // not be opened", Stop/Continue) and the two ratios are satisfied
        // with it up — 381 pixels of light run against 47-70 for a clean
        // desktop (BeyondBoot.h::lightRun).
        return menu >= 0.0 && menu < 0.35 && desk > 0.20 && desk < 0.80 &&
               beyondboot::lightRun(fb, W, H) < beyondboot::kDialogRun;
    };
    auto boot = [&]() {
        long held = 0;
        while (mem.cpuHeld() && held < 400000) { mem.tick(1000); held++; }
        if (mem.cpuHeld()) return false;
        frames(9000);
        // Poll and dismiss, the shape every other gate on the roster uses.
        // The Duo had no dismissal at all: it ran a fixed budget and
        // declared victory, so the boot alert was still up when the persist
        // gesture started and ate every key of it. 150-frame holds — the
        // engine's rule, and above a Slow Keys acceptance delay.
        for (int poll = 0; poll < 16; poll++) {
            if (cpu.isHalted()) return false;
            if (finderUp()) return true;
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(450);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d, TC=$%08X, SCSI %ld\n", W, H, cpu.getTC(),
                mem.scsi().commands);

    // ── The persist leg still SKIPs, for a DIFFERENT reason than it used
    // to, and the old one is disproven ────────────────────────────────────
    // It used to skip on "the built-in keyboard is a PMU matrix POM68K does
    // not model, so Cmd-N leaves the image byte-identical". The matrix is
    // implemented now (PgePmu, from MAME's Y0-Y7 tables) and that reading
    // was wrong twice over: the keyboard works — this machine dismisses its
    // own boot alert with Return and a screen dump at the gesture's peak
    // shows `untitled folder` on the desktop — and the byte-identical image
    // had a second cause the SKIP hid, namely that the machine was already
    // DEAD when the gesture arrived, frozen since 58 s in the power_cycle_w
    // spin.
    //
    // What actually blocks the leg is that **this guest never writes the
    // folder**. Measured 2026-08-13: zero write commands in TEN emulated
    // minutes after the folder appears, at 4 MB and at 8 MB alike, while
    // the write path plainly works (60 write commands, 177 blocks, during
    // its own boot). It is not a budget: the flush poll ran to 36000
    // frames. Nor is it the disk cache alone — the Mac II, which showed the
    // same symptom, flushes as soon as it has 4 MB and this one does not.
    // Pressing the POWER KEY (0x7F, the PG&E's port A pseudo-row) to end
    // the session through the System raises no dialog: on a Duo that key is
    // the PMU's, not the keyboard's.
    //
    // Working hypothesis, UNVERIFIED: the System is holding the writes
    // behind the power manager's hard-disk SPIN-DOWN, and `ScsiDisk` is
    // always instantly ready, so the spin-up transition the driver flushes
    // on never happens. Next instrument: watch the guest's queue rather
    // than the medium, or model spin-down on the target. Do NOT re-derive
    // the ten-minute measurement or re-try the power key.
    if (getenv("POM68K_BEYOND") && std::string(getenv("POM68K_BEYOND")) == "persist") {
        std::printf("SKIP: the Duo creates the folder but never writes it — "
                    "0 write commands in 10 emulated minutes (see above)\n");
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
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    h.probe = [&]() {
        // Has the guest written ANYTHING since it mounted? A System 7 mount
        // alone clears the volume's clean-unmount bit, so a zero here is a
        // dead write path, not a Finder that did nothing.
        std::fprintf(stderr, "[scsi] write cmds %ld blocks %ld, read cmds %ld\n",
                     mem.scsiDisk().writeCommands, mem.scsiDisk().writeBlocks,
                     mem.scsiDisk().readCommands);
    };
    // A CPU reset, not a machine reset — TRIED and reverted 2026-08-13.
    // `mem.reset()` looks more faithful and is worse here: it wipes the
    // IIfx's PRAM (`rtc_.factoryDefaults()`) and zeroes the Duo's GSC mode
    // registers, and the guest does not rewrite the latter, so the second
    // boot decoded at the wrong depth and the screen came back as noise
    // while the machine was plainly alive behind it. It also did not fix
    // what it was tried for: the IIfx's second boot fails identically
    // either way, and even on a machine rebuilt from scratch.
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        std::vector<uint32_t> fb;
        mem.decodeScreen(fb);
        beyondboot::dumpPpm((std::string("duo_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    return beyondboot::run(h);
}
