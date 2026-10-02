// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// lcii_prober_oracle — the POM68K half of the Retro68 differential oracle
// (TODO § Preuve, "Introduire Retro68 comme oracle invité différentiel").
//
// The guest-side POM68K Prober (dev/prober) writes its findings — Gestalt,
// low memory, the bus-error topology, devices — as a TSV next to itself the
// moment it launches. Put it in Startup Items of a copy of the LC II's
// reference volume, boot, and the report is in the image. Booting the SAME
// prepared image under MAME `maclc2` gives the other half; the two TSVs are
// compared on the host (tools/prober_oracle.sh).
//
//   lcii_prober_oracle <out-dir> [frames]
//     <out-dir>/prepared.hd  the image after injection, before any boot
//     <out-dir>/pom68k.hd    the same image after the POM68K run
//
// Not a gate yet: its first use is to find out what the two disagree on.

#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "HfsInject.h"
#include "JitTestConfig.h"
#include "V8Memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lcii_prober_oracle <out-dir> [frames]\n");
        return 2;
    }
    const std::string outDir = argv[1];
    const long frames = argc > 2 ? std::atol(argv[2]) : 20000;

    const std::string rom = testasset::find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    const std::string bin = testasset::find("dev/prober/build/POM68KProber.bin");
    if (rom.empty() || img.empty() || bin.empty()) {
        std::printf("SKIP: needs the LC II ROM, hdv/boot.vhd and dev/prober/build/POM68KProber.bin\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream romIn(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(romIn)),
                                       std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // MAME's maclc2 has no 68882: POM68K_NOFPU=1 boots the bare machine the
    // way lcii_boot_etalon does, so the two halves compare like for like.
    const bool withFpu = std::getenv("POM68K_NOFPU") == nullptr;
    Cpu030 cpu(mem, testjit::resolveFromEnvironment(), pom68k::defaultCoreConfig().cpu,
               withFpu, false);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }

    std::ifstream binIn(bin, std::ios::binary);
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(binIn)),
                                   std::istreambuf_iterator<char>());
    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(raw, app, err)) {
        std::fprintf(stderr, "FAIL: %s: %s\n", bin.c_str(), err.c_str());
        return 1;
    }
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    const hfsinject::Outcome o = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("prober: %s\n", o.message.c_str());
    if (o.kind != hfsinject::Outcome::Installed &&
        o.kind != hfsinject::Outcome::AlreadyPresent) return 1;
    if (!writeFile(outDir + "/prepared.hd", mem.scsiDisk().image())) {
        std::fprintf(stderr, "FAIL: cannot write %s/prepared.hd\n", outDir.c_str());
        return 1;
    }

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;        // 60.15 Hz @ 15.6672 MHz
    for (long f = 0; f < frames && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    if (!writeFile(outDir + "/pom68k.hd", mem.scsiDisk().image())) return 1;
    std::printf("wrote %s/prepared.hd and %s/pom68k.hd after %ld frames\n",
                outDir.c_str(), outDir.c_str(), frames);
    return 0;
}
