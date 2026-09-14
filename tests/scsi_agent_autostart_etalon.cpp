// POM68K — « POM68K Disques » without a gesture (docs/SCSI_HOTPLUG.md § 8)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The agent's MacBinary is put into the 8.1 boot volume's Startup Items by
// the host (src/HfsInject.h, over the attached ScsiDisk, in memory — the
// asset file is not written). The Quadra 605 boots; the Finder launches
// the agent with the desktop; the mailbox sees its polls before any input
// reaches the guest. Then a blank disk attached live on SCSI 2 is mounted
// on request, by name — the whole product path, from a cold launch to
// « Monter », with no click in the Mac. The File Manager reading the
// catalog this gate rewrote is the oracle of that surgery.
//
// Soft-skips without the ROM or a bootable image; the agent's .bin is
// the one the repository ships (share/), or a fresh Retro68 build.

#include "AssetFingerprint.h"
#include "FinderSignature.h"
#include "GuestScsiView.h"
#include "HfsBlankVolume.h"
#include "HfsInject.h"
#include "JitTestConfig.h"
#include "PortableEnv.h"
#include "Q605ApplicationHarness.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace q605app;

namespace {

constexpr int kNewId = 2;
constexpr uint64_t kNewBytes = 20ull << 20;

pom68k::GuestScsiView view(const pom68k::GuestScsiDriveHints* hints) {
    return pom68k::readGuestScsiView([](uint32_t a, uint8_t& out) {
        out = gMem->peek8(a);
        return true;
    }, hints);
}

bool ask(pom68k::ScsiAgentMailbox::Kind kind, int id, pom68k::ScsiAgentSnapshot& out) {
    gMem->scsi().agent().post(kind, id);
    for (int frame = 0; frame < 1800 && !gCpu->isHalted(); frame++) {
        runFrames(1);
        out = gMem->scsi().agent().snapshot();
        if (!out.pending) return true;
    }
    out = gMem->scsi().agent().snapshot();
    return false;
}

} // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    // A fresh Retro68 build first, else the copy the repository ships —
    // so this gate runs on a host without the toolchain.
    std::string agent = find("dev/scsiagent/build/POM68KDisques.bin");
    if (agent.empty()) agent = find("share/POM68KDisques.bin");
    if (rom.empty() || img.empty() || agent.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd + "
                    "share/POM68KDisques.bin\n");
        return 0;
    }
    testasset::report({rom, img, agent});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    if (romData.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }
    std::ifstream ab(agent, std::ios::binary);
    const std::vector<uint8_t> agentRaw((std::istreambuf_iterator<char>(ab)),
                                        std::istreambuf_iterator<char>());
    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(agentRaw, app, err)) {
        std::fprintf(stderr, "FAIL: %s: %s\n", agent.c_str(), err.c_str());
        return 1;
    }
    const std::string newPath = pom68kTempPath("scsi_agent_autostart_new.img");
    std::vector<uint8_t> blank = hfsblank::build(kNewBytes, "Branche");
    blank[1024 + 10] |= 0x01;                     // unmounted cleanly (see scsi_agent_etalon)
    if (!hfsblank::writeFile(newPath, blank)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", newPath.c_str());
        return 1;
    }

    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.attachScsi(img, false, 0)) {
        std::fprintf(stderr, "FAIL: could not attach %s\n", img.c_str());
        return 1;
    }

    // ── The host puts the agent into Startup Items, in memory ────────────
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    const hfsinject::Outcome o = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("install: %s\n", o.message.c_str());
    if (o.kind != hfsinject::Outcome::Installed) {
        std::fprintf(stderr, "FAIL: the agent was not installed into Startup Items\n");
        return 1;
    }
    std::printf("install: %zu dirty blocks on the boot disk, write-back off\n",
                mem.scsiDisk().dirtyBlocks());

    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    gAzertyGuest = false;

    // ── Boot: the Finder, then the agent, with no input at all ───────────
    if (!bootToFinder(14000)) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d)\n", cpu.isHalted());
        return 1;
    }
    uint32_t polls = 0;
    int frame = 0;
    for (; frame < 3600 && !polls && !cpu.isHalted(); frame++) {
        runFrames(1);
        polls = mem.scsi().agent().snapshot().polls;
    }
    if (!polls) {
        dump("scsi_agent_autostart_nopoll.ppm");
        std::printf("front: %s\n", describe(runningProcesses(120)).c_str());
        std::fprintf(stderr, "FAIL: the agent never polled within 60 s of the Finder\n");
        return 1;
    }
    std::printf("agent: first poll %d frames after the Finder, no gesture\n", frame);
    const std::vector<ProcessSample> seen = runningProcesses(120);
    if (!processRuns(seen, "POM68KDisques")) {
        std::fprintf(stderr, "FAIL: POM68KDisques is not among the running processes (%s)\n",
                     describe(seen).c_str());
        return 1;
    }

    // ── A disk attached live is mounted on request ───────────────────────
    if (!mem.attachScsi(newPath, false, kNewId)) {
        std::fprintf(stderr, "FAIL: live attach refused\n");
        return 1;
    }
    std::remove(newPath.c_str());
    pom68k::ScsiAgentSnapshot rep;
    if (!ask(pom68k::ScsiAgentMailbox::Mount, kNewId, rep) || rep.lastErr != 0 ||
        rep.lastText != "Branche" || rep.lastDrive <= 0) {
        std::fprintf(stderr, "FAIL: mount not reported as « Branche » (err %d « %s »)\n",
                     rep.lastErr, rep.lastText.c_str());
        return 1;
    }
    pom68k::GuestScsiDriveHints hints{};
    hints[kNewId] = rep.lastDrive;
    runFrames(120);
    const pom68k::GuestScsiView v = view(&hints);
    if (!v.valid || !v.bays[kNewId].mounted || v.bays[kNewId].volume != "Branche") {
        std::fprintf(stderr, "FAIL: the guest's queues do not show « Branche » on SCSI %d\n", kNewId);
        return 1;
    }
    std::printf("PASS: the agent installed by the host into Startup Items polled %d frames after "
                "the Finder without a gesture and mounted « Branche » on SCSI %d\n", frame, kNewId);
    return 0;
}
