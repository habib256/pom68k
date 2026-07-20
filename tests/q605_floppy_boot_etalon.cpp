// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 SWIM2 floppy gate: synthetic 1.44 MB image through Swim2+SonyDrive
// under CPU-less unit control (drive detect + motor + track0/sector0
// read-back). Optional ROM floppy-boot path soft-skips without
// roms/quadra605.rom — full ROM boot remains fragile vs SCSI default.

#include "Q605Memory.h"
#include "Cpu040.h"
#include "Swim2.h"
#include "SonyDrive.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

std::vector<uint8_t> makeHdImage() {
    std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
    // Recognisable boot-block signature at LBA 0 (track0/side0/sector0)
    img[0] = 0x4C; img[1] = 0x4B;                // 'LK'
    img[2] = 0x60; img[3] = 0x00;
    img[4] = 0x00; img[5] = 0x8E;
    img[6] = 0x44; img[7] = 0x18;
    // bbEntry lands at +$92. The payload writes "POM6" to low memory and
    // loops; observing it proves the ROM found SWIM2, read the boot blocks,
    // validated 'LK', and transferred control to floppy code.
    static const uint8_t boot[] = {
        0x23, 0xFC, 0x50, 0x4F, 0x4D, 0x36, // move.l #$504F4D36,$D00.l
        0x00, 0x00, 0x0D, 0x00,
        0x60, 0xF4,                         // bra.s boot
    };
    static const char kSig[] = "POM68K-Q605-FLOPPY";
    std::memcpy(&img[0x20], kSig, sizeof kSig - 1);
    for (int i = 0; i < 512; i++)
        if (i >= 0x40) img[size_t(i)] = uint8_t(0x10 + (i & 0x0F));
    std::memcpy(&img[0x92], boot, sizeof boot);
    return img;
}

bool readSector0ViaSwim(Swim2& swim, SonyDrive& drive, uint8_t out[512]) {
    drive.command(0b0100);                       // motor on
    swim.write(6, 0xFF);                         // clear mode
    swim.write(7, 0x8A);                         // motor + A + ACTION
    swim.write(5, 0x00);                         // MFM

    std::vector<uint8_t> stream;
    stream.reserve(10000);
    for (int i = 0; i < 12000 && int(stream.size()) < 9000; i++) {
        swim.tick(128);
        while (swim.fifoCount()) stream.push_back(swim.read(1));
    }

    for (size_t i = 0; i + 8 < stream.size(); i++) {
        if (!(stream[i] == 0xA1 && stream[i + 1] == 0xA1 && stream[i + 2] == 0xA1 &&
              stream[i + 3] == 0xFE && stream[i + 4] == 0x00 && stream[i + 5] == 0x00 &&
              stream[i + 6] == 0x01))
            continue;
        for (size_t j = i + 8; j + 4 + 512 < stream.size(); j++) {
            if (!(stream[j] == 0xA1 && stream[j + 1] == 0xA1 && stream[j + 2] == 0xA1 &&
                  stream[j + 3] == 0xFB))
                continue;
            std::memcpy(out, &stream[j + 4], 512);
            return true;
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    std::printf("q605_floppy_boot_etalon — SWIM2 SuperDrive media gate\n");

    Q605Memory mem(1u << 20);
    mem.reset();
    SonyDrive& drive = mem.internalDrive();
    check(drive.isSuperDrive(), "Q605 internal drive is SuperDrive");
    check(drive.sense(0xE) == false, "drive INSTALLED sense");

    auto img = makeHdImage();
    check(drive.insertImage(img), "insert synthetic 1.44MB");
    check(!drive.sense(0x1) && drive.sense(0xA) && drive.mfmMode(),
          "disk present + SuperDrive + MFM");

    // No SCSI attached — floppy-only stack under unit control.
    uint8_t sector[512];
    check(readSector0ViaSwim(mem.swim(), drive, sector),
          "Swim2 reads track0 sector1 payload");
    check(sector[0] == 0x4C && sector[1] == 0x4B, "boot-block 'LK' signature");
    check(std::memcmp(sector + 0x20, "POM68K-Q605-FLOPPY", 17) == 0,
          "embedded floppy signature");

    // Direct API still matches (encoder/image coherence).
    uint8_t direct[512];
    check(drive.readSector(0, 0, 0, direct), "readSector(0,0,0)");
    check(std::memcmp(sector, direct, 512) == 0,
          "Swim2 stream matches raw sector 0");

    drive.eject();
    check(!drive.hasDisk(), "ejectDisk path clears media");

    // Whole-machine ROM path. It soft-skips only when the user ROM is absent.
    std::string romPath = (argc > 1) ? argv[1] : "";
    if (romPath.empty()) {
        for (const char* p : {
            "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
            "roms/mame/macqd605/ff7439ee.bin",
            "roms/quadra605.rom",
            "../roms/mame/macqd605/ff7439ee.bin"
        }) {
            std::ifstream f(p, std::ios::binary);
            if (f) { romPath = p; break; }
        }
    }
    if (romPath.empty()) {
        std::printf("  (optional ROM floppy-boot soft-skipped — no quadra605.rom)\n");
    } else {
        std::ifstream in(romPath, std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        if (rom.size() == Q605Memory::kRomSize) {
            // Full ROM→floppy boot remains open (SCSI is the default Quadra
            // path). Soft-report only: never fail the gate on it.
            Q605Memory bootMem(32u << 20);
            if (!bootMem.loadRom(rom)) {
                std::printf("  (optional ROM path soft-skipped — loadRom failed)\n");
            } else if (!bootMem.internalDrive().insertImage(makeHdImage())) {
                std::printf("  (optional ROM path soft-skipped — insert failed)\n");
            } else {
                Cpu040 cpu(bootMem);
                bootMem.setCpu(&cpu);
                cpu.hardReset();
                while (bootMem.cpuHeld()) bootMem.tick(1000);
                constexpr int kSlice = 416667;
                constexpr int kMaxFrames = 1200;     // 0.5 G — probe only
                bool jumped = false;
                for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
                    cpu.runCycles(kSlice);
                    if (bootMem.peek8(0x0D00) == 0x50 &&
                        bootMem.peek8(0x0D01) == 0x4F &&
                        bootMem.peek8(0x0D02) == 0x4D &&
                        bootMem.peek8(0x0D03) == 0x36) {
                        jumped = true;
                        break;
                    }
                }
                std::printf("  optional ROM floppy probe: %s (pc=$%08X clk=%lld "
                            "SWIM mode=$%02X track=%d bytes=%ld)%s\n",
                            jumped ? "boot block ran" : "no transfer yet",
                            cpu.getPC(), (long long)cpu.getClock(),
                            bootMem.swim().mode(),
                            bootMem.internalDrive().currentTrack(),
                            bootMem.internalDrive().nibblesRead,
                            cpu.isHalted() ? " HALTED" : "");
            }
        } else {
            std::printf("  (optional ROM path soft-skipped — bad ROM size)\n");
        }
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
