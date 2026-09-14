// POM68K — « POM68K Disques » without a gesture, on a French System (LC 520)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The second cell of docs/SCSI_HOTPLUG.md § 8, and the one that names the
// folder right: GISTPERSO is a System 7.5.5 in French whose blessed
// « Dossier Système » holds « Ouverture au démarrage » (CNID 4769) — not
// the « Éléments de démarrage » first guessed. The host installs the agent
// there in memory (the asset is not written), the LC 520 boots — the rig
// of lc520_boot_etalon and aio_beyond_etalon: Sonora, NCR 5380, Cuda —
// the French Finder launches the agent with the desktop, the 5380 answers
// its polls, and a blank disk attached live on SCSI 2 is mounted on
// request. The 68030 / 5380 / System 7.5 side of the agent, which the
// Quadra gate (68040 / 53C96 / Mac OS 8.1) does not cover.
//
// `control` boots the same rig without the injection: what separates
// « the injection broke the boot » from « the signature missed the
// Finder ». Soft-skips without the ROM, GISTPERSO, or the shipped agent.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "FinderSignature.h"
#include "HfsBlankVolume.h"
#include "HfsInject.h"
#include "Mmu030Peek.h"
#include "PortableEnv.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kNewId = 2;
constexpr uint64_t kNewBytes = 20ull << 20;

std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    const bool control = argc > 1 && std::string(argv[1]) == "control";
    std::string rom = testasset::find("roms/maclc520.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-10 - EDE66CBD - Color Classic II & LC 550 & Performa 275,550,560 & Macintosh TV.ROM");
    std::string img = testasset::find("hdv/ref/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    std::string agent = testasset::find("dev/scsiagent/build/POM68KDisques.bin");
    if (agent.empty()) agent = testasset::find("share/POM68KDisques.bin");
    if (rom.empty() || img.empty() || agent.empty()) {
        std::printf("SKIP: needs the 1 MB EDE66CBD ROM + hdv/GISTPERSO-boot.vhd + "
                    "share/POM68KDisques.bin\n");
        return 0;
    }
    testasset::report({rom, img, agent});
    const std::vector<uint8_t> romData = slurp(rom);
    if (romData.size() != SonoraMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }
    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(slurp(agent), app, err)) {
        std::fprintf(stderr, "FAIL: %s: %s\n", agent.c_str(), err.c_str());
        return 1;
    }
    const std::string newPath = pom68kTempPath("lc520_agent_autostart_new.img");
    std::vector<uint8_t> blank = hfsblank::build(kNewBytes, "Branche");
    blank[1024 + 10] |= 0x01;                     // unmounted cleanly
    if (!hfsblank::writeFile(newPath, blank)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", newPath.c_str());
        return 1;
    }

    // LC 520 identity + Cuda transport, exactly lc520_boot_etalon's rig.
    SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000,
                     SonoraMemory::kCpuHz, SonoraMemory::kIdLc520,
                     /*cudaAdb=*/true);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(6);                       // built-in 640×480 RGB
    SonoraCpu cpu(mem, jit::defaultResolvedConfig(),
                  pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = SonoraMemory::kCpuHz / 60;
    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };

    // ── The host finds « Ouverture au démarrage » and installs the agent ─
    {
        hfsinject::ScsiDiskIo io(mem.scsiDisk());
        uint32_t start, length;
        if (!hfsinject::findHfsVolume(io, start, length, err)) {
            std::fprintf(stderr, "FAIL: %s\n", err.c_str());
            return 1;
        }
        hfsinject::Volume v(io, start, length);
        if (!v.open(err)) { std::fprintf(stderr, "FAIL: %s\n", err.c_str()); return 1; }
        const uint32_t blessed = v.blessedFolder();
        const uint32_t items = v.findFolder(blessed, hfsinject::startupItemsNames()[1]);
        const uint32_t wrong = v.findFolder(blessed, "\x83l\x8Ements de d\x8Emarrage");
        std::printf("volume « %s »: blessed %u, « Ouverture au démarrage » %u, "
                    "« Éléments de démarrage » %u\n", v.name().c_str(), blessed, items, wrong);
        if (!items || wrong) {
            std::fprintf(stderr, "FAIL: the French Startup Items folder is not the one expected\n");
            return 1;
        }
    }
    if (!control) {
        hfsinject::ScsiDiskIo rw(mem.scsiDisk());
        const hfsinject::Outcome o = hfsinject::installStartupItem(rw, app, 3870700000u);
        std::printf("install: %s\n", o.message.c_str());
        if (o.kind != hfsinject::Outcome::Installed) {
            std::fprintf(stderr, "FAIL: the agent was not installed\n");
            return 1;
        }
    }

    // ── Boot to the French Finder: lc520_boot_etalon's luminance signature ─
    int W = 640, H = 480;
    auto finderUp = [&]() {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        video.size(W, H);
        const double menu = beyondboot::darkRatio(fb, W, 0, W, 2, 16);
        const double desk = beyondboot::darkRatio(fb, W, W - 112, W, 40, H - 44);
        return menu < 0.30 && desk > 0.35 && desk < 0.80;
    };
    auto dumpScreen = [&](const char* name) {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        beyondboot::dumpPpm(name, fb, W, H);
    };
    auto peekLogical = [&](uint32_t a) {
        uint32_t phys = 0;
        if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(), a, 5,
                                   [&](uint32_t x) { return mem.peek8(x); }, &phys))
            return -1;
        return int(mem.peek8(phys));
    };
    auto frontApp = [&]() { return findersig::curApNameAt(peekLogical); };

    while (mem.cpuHeld()) mem.tick(1000);        // the Cuda holds the CPU through reset
    bool finder = false;
    for (int frame = 0; frame < 20000 && !cpu.isHalted() && !finder; frame++) {
        frames(1);
        if (frame >= 2400 && !(frame % 60)) finder = finderUp();
    }
    if (!finder) {
        dumpScreen(control ? "lc520_agent_autostart_control_nofinder.ppm"
                           : "lc520_agent_autostart_nofinder.ppm");
        std::fprintf(stderr, "FAIL: no Finder (halted=%d, front « %s », control=%d)\n",
                     cpu.isHalted(), frontApp().c_str(), control ? 1 : 0);
        return 1;
    }
    if (control) {
        std::printf("CONTROL: the Finder came up without the injection; front « %s »\n",
                    frontApp().c_str());
        return 0;
    }

    // ── The agent polls with no input at all ─────────────────────────────
    uint32_t polls = 0;
    int frame = 0;
    for (; frame < 3600 && !polls && !cpu.isHalted(); frame++) {
        frames(1);
        polls = mem.scsi().agent().snapshot().polls;
    }
    if (!polls) {
        dumpScreen("lc520_agent_autostart_nopoll.ppm");
        std::fprintf(stderr, "FAIL: the agent never polled within 60 s of the Finder (front « %s »)\n",
                     frontApp().c_str());
        return 1;
    }
    std::printf("agent: first poll %d frames after the Finder, no gesture; front « %s »\n",
                frame, frontApp().c_str());

    // ── A disk attached live is mounted on request ───────────────────────
    if (!mem.attachScsi(newPath, false, kNewId)) {
        std::fprintf(stderr, "FAIL: live attach refused\n");
        return 1;
    }
    std::remove(newPath.c_str());
    mem.scsi().agent().post(pom68k::ScsiAgentMailbox::Mount, kNewId);
    pom68k::ScsiAgentSnapshot rep;
    for (int f = 0; f < 1800 && !cpu.isHalted(); f++) {
        frames(1);
        rep = mem.scsi().agent().snapshot();
        if (!rep.pending) break;
    }
    if (rep.pending || rep.lastErr != 0 || rep.lastText != "Branche" || rep.lastDrive <= 0) {
        dumpScreen("lc520_agent_autostart_mount.ppm");
        std::fprintf(stderr, "FAIL: mount not reported as « Branche » (pending %d, err %d « %s »)\n",
                     rep.pending ? 1 : 0, rep.lastErr, rep.lastText.c_str());
        return 1;
    }
    std::printf("PASS: the French Finder launched the agent from « Ouverture au démarrage » "
                "%d frames after the desktop, and it mounted « Branche » on SCSI %d\n",
                frame, kNewId);
    return 0;
}
