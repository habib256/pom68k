// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// prober_oracle — the Retro68 differential oracle (TODO § Preuve,
// CHANGELOG 2026-10-02 (night), (seventh)).
//
// The guest-side POM68K Prober (dev/prober) writes its findings — Gestalt,
// low memory, the bus-error topology, AppleTalk, devices — as a TSV next to
// itself the moment it launches. Put it in Startup Items of a copy of a
// machine's locked reference volume, boot, and the report is in the image.
// Booting the SAME prepared image under MAME gives the other half
// (tools/prober_oracle.sh).
//
//   prober_oracle <machine> <out-dir> [frames]      the rig
//     <out-dir>/prepared.hd  the image after injection, before any boot
//     <out-dir>/pom68k.hd    the same image after the POM68K run
//     <out-dir>/pom68k.tsv   the Prober's report read back from it
//     <out-dir>/companion.hd the blank "Infinite HD", when the volume wants one
//   prober_oracle --extract <image> <out.tsv>
//     the report out of any image the Prober ran on (MAME's)
//   prober_oracle <machine> --check <mame.tsv>       the gate
//     runs the POM68K half and compares its report with MAME's, recorded
//     in tools/prober_oracle_<mame-system>.tsv, on every field both models
//     can judge.
//
// <machine> is `lcii` (MAME maclc2) or `q605` (MAME macqd605). Each runs on
// its profile's locked volume, POM68K_BEYOND_IMG overriding it for
// exploration. The gate soft-skips without the ROM, that volume or the
// built Prober.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu030.h"
#include "Cpu040.h"
#include "HfsInject.h"
#include "InfiniteHdCompanion.h"
#include "JitTestConfig.h"
#include "ProberOracle.h"
#include "Q605Memory.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace proberoracle;

namespace {

// Fields the two models cannot both judge, each with the reason it is not
// evidence (CHANGELOG 2026-10-02 (night), (late night), (seventh)).
const char* const kRtc = "each side seeds its RTC differently";

const Unjudged kLciiUnjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.fpu", "MAME's 030 FPU produces 68881 FSAVE frames; the LC II socket takes a 68882" },
    { "ident.memTop", "follows the system-heap allocation order, a CPU-throughput symptom "
                      "(TODO § Fidélité, cacheBoost)" },
    { "probe.VIA1@Plus", "unmapped on the LC II; maclc raises no bus error there" },
    { "probe.VIA2@II", "unmapped on the LC II; maclc raises no bus error there" },
};

const Unjudged kQ605Unjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.memTop", "follows the system-heap allocation order, as on the LC II; "
                      "not attributed on this board" },
    { "volume.vol0.kbFree", "CalendarMenu rewrites its Memo when the RTC says a new day: "
                            "MAME's clock is the host's, POM68K's starts in 1904" },
};

struct Options {
    bool check = false;
    std::string outDir, golden;
    long frames = 20000;
};

// One machine's run, whatever its memory map and CPU: inject, boot until
// the report is whole (gate) or for the full budget (rig), then judge or
// save. `screen` dumps the frame when POM68K_DUMP is set and no report
// appeared.
template <class Mem, class Cpu>
int run(Mem& mem, Cpu& cpu, const Options& o, const std::string& img,
        const std::string& bin, int64_t frameCycles, const Unjudged& unjudged,
        const std::function<void()>& screen) {
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    // The Infinite Mac volumes' Startup Items alias stops the Finder on an
    // alert before it reaches the Prober; the blank companion resolves it.
    // The rig keeps a copy, inside its partition-map façade, for MAME.
    if (!infinitehd::attach(mem, img)) return 1;
    if (!o.check && infinitehd::wants(img)) {
        const std::vector<uint8_t>& c = mem.scsiDiskAt(1).image();
        if (!writeFile(o.outDir + "/companion.hd", c.data(), c.size())) return 1;
    }

    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(readAll(bin), app, err)) {
        std::fprintf(stderr, "FAIL: %s: %s\n", bin.c_str(), err.c_str());
        return 1;
    }
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    // A fixed install date, so the prepared image is the same bytes every run.
    const hfsinject::Outcome out = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("prober: %s\n", out.message.c_str());
    if (out.kind != hfsinject::Outcome::Installed &&
        out.kind != hfsinject::Outcome::AlreadyPresent) return 1;
    if (!o.check) {
        const std::vector<uint8_t>& d = mem.scsiDisk().image();
        if (!writeFile(o.outDir + "/prepared.hd", d.data(), d.size())) {
            std::fprintf(stderr, "FAIL: cannot write %s/prepared.hd\n", o.outDir.c_str());
            return 1;
        }
    }

    while (mem.cpuHeld()) mem.tick(1000);
    // The gate stops as soon as the whole report is on disk (looked for
    // every 600 frames); the rig runs its full budget, so its low memory and
    // image compare with MAME's after the same span.
    std::string ours;
    bool have = false;
    for (long f = 0; f < o.frames && !cpu.isHalted(); f++) {
        cpu.runCycles(frameCycles);
        if (o.check && f % 600 == 599 && extractReport(io, ours, err) && parse(ours).complete()) {
            std::printf("report complete after %ld frames\n", f + 1);
            have = true;
            break;
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }
    if (!have && !extractReport(io, ours, err)) {
        screen();
        std::fprintf(stderr, "FAIL: no Prober report in the image after %ld frames: %s\n",
                     o.frames, err.c_str());
        return 1;
    }

    if (o.check) {
        const std::vector<uint8_t> g = readAll(o.golden);
        if (compare(std::string(g.begin(), g.end()), ours, unjudged)) {
            std::printf("FAIL: POM68K disagrees with MAME\n");
            return 1;
        }
        std::printf("PASSED — the Prober reads the same machine under POM68K and MAME\n");
        return 0;
    }
    const std::vector<uint8_t>& d = mem.scsiDisk().image();
    if (!writeFile(o.outDir + "/pom68k.hd", d.data(), d.size())) return 1;
    if (!writeFile(o.outDir + "/pom68k.tsv", ours.data(), ours.size())) return 1;
    // Low memory at the end of the run, for a byte-level comparison with
    // MAME's (CHANGELOG 2026-10-02 (late night) walked the system heap).
    std::vector<uint8_t> low(0x40000);
    for (uint32_t a = 0; a < low.size(); a++) low[a] = mem.peek8(a);
    writeFile(o.outDir + "/pom68k.lowmem", low.data(), low.size());
    std::printf("wrote %s/prepared.hd, pom68k.hd and pom68k.tsv after %ld frames\n",
                o.outDir.c_str(), o.frames);
    return 0;
}

// MAME's Egret/Cuda starts from a cold PRAM that the ROM initialises with
// AppleTalk active (SPConfig port B = 1); POM68K's factory XPRAM seeds it
// inactive ($22) unless asked. Seed it active so both open .MPP.
pom68k::CoreConfig oracleConfig() {
    pom68k::CoreConfig config = pom68k::defaultCoreConfig();
    config.peripherals.appleTalkPram = true;
    return config;
}

std::string image(const char* locked) {
    const std::string img = testasset::overrideImage();
    return img.empty() ? testasset::find(locked) : img;
}

int lcii(const Options& o, const std::string& bin) {
    const std::string rom = testasset::find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    const std::string img = image("hdv/System 7.1 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM and hdv/ref/System 7.1 HD.dsk\n");
        return 0;
    }
    testasset::report({ rom, img });
    const pom68k::CoreConfig config = oracleConfig();
    V8Memory mem(config);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // MAME's maclc2 is run with its FPU socket filled (tools/prober_oracle.sh):
    // POM68K's default 68882 compares like for like. POM68K_NOFPU=1 boots the
    // bare machine, which does not reach the Finder yet (TODO § Fidélité).
    Cpu030 cpu(mem, testjit::resolveFromEnvironment(), config.cpu,
               std::getenv("POM68K_NOFPU") == nullptr, false);
    const auto screen = [&mem] {
        std::vector<uint32_t> fb;
        V8Video video(mem);
        video.decode(fb);
        int w = 0, h = 0;
        video.size(w, h);
        beyondboot::dumpPpm("prober_oracle.ppm", fb, w, h);   // POM68K_DUMP=1
    };
    return run(mem, cpu, o, img, bin, 640 * 407 /* 60.15 Hz @ 15.6672 MHz */,
               kLciiUnjudged, screen);
}

int q605(const Options& o, const std::string& bin) {
    const std::string rom = testasset::find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    const std::string img = image("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM and hdv/ref/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ rom, img });
    pom68k::CoreConfig config = oracleConfig();
    // The board's default ID is the LC 475's; MAME's macqd605 is $A55A2225
    // (macquadra605.cpp), with the 68040's FPU on die.
    config.bus.q605MachineId = 0xA55A2225u;
    config.cpu.q605Fpu = pom68k::Q605FpuMode::Integrated;
    Q605Memory mem(config, 32u << 20);         // MAME is run with -ramsize 32M
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), config.cpu, config.diagnostics);
    return run(mem, cpu, o, img, bin, 416667 /* 25 MHz / ~60 Hz */, kQ605Unjudged, [] {});
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--extract") == 0) {
        std::vector<uint8_t> img = readAll(argv[2]);
        hfsinject::MemoryIo io(img);
        std::string text, err;
        if (!extractReport(io, text, err)) {
            std::fprintf(stderr, "FAIL: %s: %s\n", argv[2], err.c_str());
            return 1;
        }
        return writeFile(argv[3], text.data(), text.size()) ? 0 : 1;
    }
    Options o;
    const bool known = argc >= 3 && (std::strcmp(argv[1], "lcii") == 0 ||
                                     std::strcmp(argv[1], "q605") == 0);
    o.check = known && argc == 4 && std::strcmp(argv[2], "--check") == 0;
    if (!known || (!o.check && argv[2][0] == '-')) {
        std::fprintf(stderr, "usage: prober_oracle <lcii|q605> <out-dir> [frames]\n"
                             "       prober_oracle <lcii|q605> --check <mame.tsv>\n"
                             "       prober_oracle --extract <image> <out.tsv>\n");
        return 2;
    }
    if (o.check) o.golden = argv[3];
    else {
        o.outDir = argv[2];
        if (argc > 3) o.frames = std::atol(argv[3]);
    }
    if (o.check && readAll(o.golden).empty()) {
        std::fprintf(stderr, "FAIL: cannot read %s\n", o.golden.c_str());
        return 1;
    }
    const std::string bin = testasset::find("dev/prober/build/POM68KProber.bin");
    if (bin.empty()) {
        std::printf("SKIP: needs dev/prober/build/POM68KProber.bin (dev/README.md)\n");
        return 0;
    }
    return std::strcmp(argv[1], "lcii") == 0 ? lcii(o, bin) : q605(o, bin);
}
