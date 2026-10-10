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
// <machine> is `lcii` (MAME maclc2); `q605`, `lc475`, `lc575` (macqd605,
// maclc475, maclc575); `q800`, `q650`, `q610`, `c650`, `c610` (macqd800,
// macqd650, macqd610, macct650, macct610); `q630`, `lc580` (macqd630,
// maclc580); `q700`, `q900` (macqd700, macqd900). Each runs on
// its profile's locked volume, POM68K_BEYOND_IMG overriding it for
// exploration. The gate soft-skips without the ROM, that volume or the
// built Prober.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu030.h"
#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include "Cpu040.h"
#include "HfsInject.h"
#include "InfiniteHdCompanion.h"
#include "JitTestConfig.h"
#include "ProberOracle.h"
#include "Q605Memory.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
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

// The LC III on System 7.5.3, an Apple CD-ROM at ID 3 on both sides.
const Unjudged kLc3Unjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.fpu", "MAME's 030 FPU produces 68881 FSAVE frames; the socket takes a 68882" },
    { "ident.memTop", "follows the system-heap allocation order, a CPU-throughput symptom "
                      "(TODO § Fidélité, cacheBoost)" },
};

// The 040 boards on Mac OS 8.1 (CHANGELOG 2026-10-02 (eighth), (ninth)).
// `memTop` moves with the system-heap allocation order on every board MAME
// and POM68K both run, in either direction; it is a timing symptom, not an
// identity, until the CPU throughput is calibrated (TODO § Fidélité).
const char* const kHeap = "follows the system-heap allocation order, a CPU-throughput "
                          "symptom (TODO § Fidélité, cacheBoost)";
const char* const kCalendar = "CalendarMenu rewrites its Memo when the RTC says a new day: "
                              "MAME's clock is the host's, POM68K's starts in 1904";
const Unjudged k040Unjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.memTop", kHeap },
    { "volume.vol0.kbFree", kCalendar },
};

// The Quadra 650: its mouse (ADB address 3) gets no service routine under
// POM68K, deterministically and under both engines, where MAME's macqd650
// and POM68K's own Quadra 800 — same board, clock and FPU — install one
// (CHANGELOG 2026-10-10 (seventh)). Consistent with the ROM's ADB race on
// this family that a too-fast 040 wins (TODO § Fidélité, cacheBoost), not
// judged here until that is calibrated.
const Unjudged kQ650Unjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.memTop", kHeap },
    { "volume.vol0.kbFree", kCalendar },
    { "adb.dev2.service", "the djMEMC ADB race (TODO § Fidélité, cacheBoost): POM68K's "
                          "Quadra 650 mouse misses its service routine" },
};

// The Quadra 700 runs System 7.1, which has no CalendarMenu. So do the
// Quadra 900 and 950 once MAME's Egret boots from POM68K's seeded PRAM: a
// cold one brings System 7.1 up 24-bit, and ten probes below 16 MB then
// reach I/O and NuBus space (CHANGELOG 2026-10-10 (seventh), (ninth)).
const Unjudged kQ700Unjudged = {
    { "clock.macSeconds", kRtc },
    { "clock.dateTime", kRtc },
    { "ident.memTop", kHeap },
};

struct Options {
    bool check = false;
    std::string outDir, golden;
    long frames = 20000;
};

// The blank "Infinite HD" as POM68K's SCSI disk presents it — inside the
// partition-map façade a ROM needs — for MAME's SCSI ID 1.
bool saveCompanion(const std::string& out) {
    const std::string tmp = pom68kProcessTempPath("prober_companion", ".img");
    std::vector<uint8_t> blank = hfsblank::build(5ull << 20, "Infinite HD");
    blank[1024 + 10] |= 0x01;                       // unmounted cleanly
    ScsiDisk disk;
    const bool ok = hfsblank::writeFile(tmp, blank) && disk.open(tmp);
    std::remove(tmp.c_str());
    return ok && writeFile(out, disk.image().data(), disk.image().size());
}

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
    // Every MAME Mac has an empty Apple CD-ROM at SCSI ID 3, in a slot
    // `-scsi:3 ""` cannot empty; the Apple CD-ROM extension writes into
    // itself when it finds a drive, so both sides carry one (CHANGELOG
    // 2026-10-10 (eleventh)).
    if (!mem.attachCdromEmpty(3)) { std::fprintf(stderr, "FAIL: no CD-ROM bay\n"); return 1; }
    // The Infinite Mac volumes' Startup Items alias stops the Finder on an
    // alert before it reaches the Prober; the blank companion resolves it.
    // The rig keeps a copy, inside its partition-map façade, for MAME.
    if (!infinitehd::attach(mem, img)) return 1;
    if (!o.check && infinitehd::wants(img) && !saveCompanion(o.outDir + "/companion.hd"))
        return 1;

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

// The Macintosh LC: the V8 board in its 68020 contract (V8Memory::Model::Lc,
// Cpu030 in LC mode), 10 MB, the FPU socket filled as for the LC II.
int lc(const Options& o, const std::string& bin) {
    const std::string rom = testasset::find("roms/512KB ROMs/1990-10 - 350EACF0 - Mac LC.ROM");
    const std::string img = image("hdv/System 7.1 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC ROM and hdv/ref/System 7.1 HD.dsk\n");
        return 0;
    }
    testasset::report({ rom, img });
    const pom68k::CoreConfig config = oracleConfig();
    V8Memory mem(config, 0xA00000, V8Memory::Model::Lc);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, testjit::resolveFromEnvironment(), config.cpu, true, true);
    return run(mem, cpu, o, img, bin, 640 * 407, kLciiUnjudged, [] {});
}

// The LC III: Sonora + Egret at 25 MHz (SonoraMemory::kIdLc3),
// 8 MB, 68882, the factory XPRAM the product seeds (PlatformSonora.cpp).
// On System 7.5.3, `lc3_boot_etalon`'s volume: System 7.1 has no enabler
// for the LC III and never reaches the Prober (CHANGELOG 2026-10-10 (tenth)).
int lc3(const Options& o, const std::string& bin) {
    const std::string rom = testasset::find("roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM");
    const std::string img = image("hdv/System 7.5.3 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC III ROM and hdv/ref/System 7.5.3 HD.dsk\n");
        return 0;
    }
    testasset::report({ rom, img });
    const pom68k::CoreConfig config = oracleConfig();
    SonoraMemory mem(config, 0x800000, SonoraMemory::kCpuHz, SonoraMemory::kIdLc3, false);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.egret().factoryDefaults();
    SonoraCpu cpu(mem, testjit::resolveFromEnvironment(), config.cpu, true);
    return run(mem, cpu, o, img, bin, SonoraMemory::kCpuHz / 60, kLc3Unjudged, [] {});
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

// MEMCjr + PrimeTime, MAME macquadra605.cpp: one FF7439EE ROM, three
// identities by the board-ID register — as the product's profiles set them
// (RuntimeConfigMachine.cpp applyMachineProfile).
struct MemcJr { std::uint32_t id; pom68k::Q605FpuMode fpu; };
constexpr MemcJr kQ605{0xA55A2225u, pom68k::Q605FpuMode::Integrated};
constexpr MemcJr kLc475{0xA55A2221u, pom68k::Q605FpuMode::Soft68882};
constexpr MemcJr kLc575{0xA55A222Eu, pom68k::Q605FpuMode::Soft68882};

int memcjr(const Options& o, const std::string& bin, const MemcJr& model) {
    const std::string rom = testasset::find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    const std::string img = image("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM and hdv/ref/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ rom, img });
    pom68k::CoreConfig config = oracleConfig();
    config.bus.q605MachineId = model.id;
    config.cpu.q605Fpu = model.fpu;
    Q605Memory mem(config, 32u << 20);         // MAME is run with -ramsize 32M
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), config.cpu, config.diagnostics);
    return run(mem, cpu, o, img, bin, 416667 /* 25 MHz / ~60 Hz */, k040Unjudged, [] {});
}

// djMEMC + IOSB, MAME macquadra800.cpp: five identities on the F1A6F343
// ROM MAME calls bios "original", with the product's clocks, model pins and
// FPU (PlatformDafb.cpp runCentris).
struct DjMemc { std::int64_t hz; std::uint8_t pins; bool fpu; const Unjudged* unjudged; };
const DjMemc kQ800{CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra800, true, &k040Unjudged};
const DjMemc kQ650{CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra650, true, &kQ650Unjudged};
const DjMemc kQ610{CentrisMemory::kCpuHzQ610, CentrisMemory::kIdQuadra610, true, &k040Unjudged};
const DjMemc kC650{CentrisMemory::kCpuHz650, CentrisMemory::kIdCentris650, false, &k040Unjudged};
const DjMemc kC610{CentrisMemory::kCpuHz610, CentrisMemory::kIdCentris610, false, &k040Unjudged};

int djmemc(const Options& o, const std::string& bin, const DjMemc& model) {
    const std::string rom = testasset::find("roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM");
    const std::string img = image("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the F1A6F343 ROM and hdv/ref/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ rom, img });
    pom68k::CoreConfig config = oracleConfig();
    config.cpu.centrisFull040 = model.fpu;
    CentrisMemory mem(config, 32u << 20, model.hz, model.pins);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    CentrisCpu cpu(mem, testjit::resolveFromEnvironment(), config.cpu);
    return run(mem, cpu, o, img, bin, model.hz / 60, *model.unjudged, [] {});
}

// F108 + PrimeTime II, MAME macquadra630.cpp: the Quadra 630 ($A55A2252).
// The LC/Performa 580 ($A55A225A) is the same board with a 68LC040.
int q630(const Options& o, const std::string& bin, bool lc580 = false) {
    const std::string rom = testasset::find("roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM");
    const std::string img = image("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 06684214 ROM and hdv/ref/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ rom, img });
    pom68k::CoreConfig config = oracleConfig();
    config.bus.q630MachineId = lc580 ? 0xA55A225Au : 0xA55A2252u;
    config.cpu.q630Lc040 = lc580;
    Q630Memory mem(config, 32u << 20);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Q630Cpu cpu(mem, testjit::resolveFromEnvironment(), config.cpu);
    return run(mem, cpu, o, img, bin, Q630Memory::kCpuHz / 60, k040Unjudged, [] {});
}

// Spike, MAME macquadra700.cpp: the Quadra 700, on System 7.1 in 8 MB.
// MAME 0.287's macqd700 stays black with 20 or 36 MB, and Mac OS 8.1 stops
// on « not enough memory » in 8 (CHANGELOG 2026-10-02 (ninth)).
// The Quadra 900 is the same discrete board with Apple PIC IOPs and Egret
// (Q700Memory::Model::Q900, MAME macquadra700.cpp's macqd900).
int q700(const Options& o, const std::string& bin, bool q900 = false, bool q950 = false) {
    const std::string rom = testasset::find(q950
        ? "roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM"
        : "roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM");
    const std::string img = image("hdv/System 7.1 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 420DBFF3 ROM and hdv/ref/System 7.1 HD.dsk\n");
        return 0;
    }
    testasset::report({ rom, img });
    const pom68k::CoreConfig config = oracleConfig();
    const int64_t hz = q950 ? Q700Memory::kCpuHzQ950 : Q700Memory::kCpuHz;
    Q700Memory mem(config, 8u << 20, hz,
                   q950 ? Q700Memory::Model::Q950
                   : q900 ? Q700Memory::Model::Q900 : Q700Memory::Model::Spike);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // The Eclipse's battery store as POM68K seeds it, for the rig to hand
    // MAME's Egret (its NVRAM is these 256 bytes): both then boot from the
    // same PRAM (CHANGELOG 2026-10-10 (ninth)).
    if (!o.check && (q900 || q950)) mem.savePram(o.outDir + "/pom68k.pram");
    Q700Cpu cpu(mem, testjit::resolveFromEnvironment(), config.cpu);
    return run(mem, cpu, o, img, bin, hz / 60, kQ700Unjudged, [] {});
}

int dispatch(const std::string& machine, const Options& o, const std::string& bin) {
    if (machine == "lcii") return lcii(o, bin);
    if (machine == "q605") return memcjr(o, bin, kQ605);
    if (machine == "lc475") return memcjr(o, bin, kLc475);
    if (machine == "lc575") return memcjr(o, bin, kLc575);
    if (machine == "q800") return djmemc(o, bin, kQ800);
    if (machine == "q650") return djmemc(o, bin, kQ650);
    if (machine == "q610") return djmemc(o, bin, kQ610);
    if (machine == "c650") return djmemc(o, bin, kC650);
    if (machine == "c610") return djmemc(o, bin, kC610);
    if (machine == "q630") return q630(o, bin);
    if (machine == "lc580") return q630(o, bin, true);
    if (machine == "q900") return q700(o, bin, true);
    if (machine == "q950") return q700(o, bin, false, true);
    if (machine == "lc") return lc(o, bin);
    if (machine == "lc3") return lc3(o, bin);
    return q700(o, bin);
}

const char* const kMachines[] = { "lcii", "q605", "lc475", "lc575", "q800", "q650", "q610",
                                  "c650", "c610", "q630", "lc580", "q700", "q900",
                                  "q950", "lc", "lc3" };

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
    bool known = false;
    for (const char* m : kMachines) known = known || (argc >= 3 && std::strcmp(argv[1], m) == 0);
    o.check = known && argc == 4 && std::strcmp(argv[2], "--check") == 0;
    if (!known || (!o.check && argv[2][0] == '-')) {
        std::fprintf(stderr, "usage: prober_oracle <machine> <out-dir> [frames]\n"
                             "       prober_oracle <machine> --check <mame.tsv>\n"
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
    return dispatch(argv[1], o, bin);
}
