// POM68K — the guest agent « POM68K Disques » end to end (docs/SCSI_HOTPLUG.md § 3)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The Quadra 605 boots Mac OS 8.1; the agent's floppy (dev/scsiagent, built
// with Retro68) is inserted live, opened and the application launched by
// Finder gestures. From then on the agent polls the mailbox
// (src/ScsiAgentMailbox.h) through the vendor SCSI commands, and this gate
// drives it as the Disques window would:
//
//   1. a blank HFS disk attached live on SCSI 2 → « Monter »: the agent
//      reads its partition map, serves it with its own driver and
//      PBMountVol's it — reported by name, and the guest's own queues show
//      the volume on the drive the report named;
//   2. « Démonter »: gone from the VCB queue — then « Retirer »: with no
//      VCB left on the bay the target leaves the bus (detachScsi, § 7),
//      the guest's queues do not move and the agent keeps polling; the
//      same image is attached again;
//   3. « Monter » again: the drive the agent already registered is reused,
//      on the re-attached target;
//   4. « Démonter » the BOOT volume: refused (fBsyErr −47, the File
//      Manager's word), and the host is told the code unchanged.
//
// Soft-skips without the ROM, a bootable image, or the agent's .dsk.

#include "AssetFingerprint.h"
#include "FinderSignature.h"
#include "GuestScsiView.h"
#include "HfsBlankVolume.h"
#include "JitTestConfig.h"
#include "PortableEnv.h"
#include "Q605ApplicationHarness.h"

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
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

// Post a request and run until the agent answers it (or 30 s of guest time).
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

void show(const char* step, const pom68k::ScsiAgentSnapshot& s) {
    std::printf("[%s] kind %d id %d err %d drive %d « %s » (polls %u)\n", step,
                s.lastKind, s.lastId, s.lastErr, s.lastDrive, s.lastText.c_str(), s.polls);
}

} // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    const std::string agent = find("dev/scsiagent/build/POM68KDisques.dsk");
    if (rom.empty() || img.empty() || agent.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd + "
                    "dev/scsiagent/build/POM68KDisques.dsk (Retro68)\n");
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
    const std::string newPath = pom68kTempPath("scsi_agent_new.img");
    // The volume must carry the « unmounted cleanly » bit (drAtrb bit 8,
    // MDB at byte 1024 + 10): a dirty HFS volume mounted after the boot
    // goes through Mac OS 8's mount check, whose verdict on a blank image
    // is « There is a problem with the disk » (2026-09-14).
    std::vector<uint8_t> blank = hfsblank::build(kNewBytes, "Branche");
    blank[1024 + 10] |= 0x01;
    if (!hfsblank::writeFile(newPath, blank)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", newPath.c_str());
        return 1;
    }
    // A second blank volume present at BOOT on SCSI 3, served by the ROM's
    // driver: the Finder's « put away » exercised on a volume that is not
    // the agent's — the control for the agent-served case.
    const std::string bootPath = pom68kTempPath("scsi_agent_boot.img");
    if (!hfsblank::writeFile(bootPath, hfsblank::build(kNewBytes, "Racine"))) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", bootPath.c_str());
        return 1;
    }

    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.attachScsi(img, false, 0)) {
        std::fprintf(stderr, "FAIL: could not attach %s\n", img.c_str());
        return 1;
    }
    if (!mem.attachScsi(bootPath, false, 3)) {
        std::fprintf(stderr, "FAIL: could not attach %s\n", bootPath.c_str());
        return 1;
    }
    std::remove(bootPath.c_str());
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    gAzertyGuest = false;                        // the 8.1 US image

    if (!bootToFinder(14000)) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d)\n", cpu.isHalted());
        return 1;
    }
    ensureFastKeys();
    closeAllFinderWindows();

    // ── Launch the agent from its floppy ─────────────────────────────────
    if (!mem.insertDisk(agent)) {
        std::fprintf(stderr, "FAIL: could not insert %s\n", agent.c_str());
        return 1;
    }
    runFrames(900);                              // the Finder mounts and draws it
    openBySelect("pom", 600);                    // the floppy, on the desktop (adbFor: lowercase)
    openBySelect("pom", 900);                    // the application, in its window
    const std::vector<ProcessSample> seen = runningProcesses(120);
    std::printf("front: %s\n", describe(seen).c_str());
    if (!processRuns(seen, "POM68KDisques")) {
        dump("scsi_agent_launch.ppm");
        std::fprintf(stderr, "FAIL: the agent did not launch\n");
        return 1;
    }
    // Every CDB the controller sees from here on: whether the agent's SCSI
    // Manager calls reach the bus at all, and with what.
    // The last CDBs the controller saw, for the failure report.
    std::deque<std::vector<uint8_t>> lastCdbs;
    mem.scsi().onCommand = [&lastCdbs](const std::vector<uint8_t>& cdb) {
        lastCdbs.push_back(cdb);
        if (lastCdbs.size() > 16) lastCdbs.pop_front();
    };
    uint32_t polls = 0;
    for (int frame = 0; frame < 1200 && !polls; frame++) {
        runFrames(1);
        polls = mem.scsi().agent().snapshot().polls;
    }
    if (!polls) {
        dump("scsi_agent_nopoll.ppm");
        std::fprintf(stderr, "FAIL: the agent never polled the mailbox\n");
        return 1;
    }
    std::printf("agent: polling (first poll seen)\n");

    // ── 0. control: the Finder puts away a ROM-served volume on request ──
    {
        pom68k::GuestScsiDriveHints none{};
        pom68k::GuestScsiView v0 = view(&none);
        std::printf("[control] SCSI3 mounted=%d « %s »\n", v0.bays[3].mounted ? 1 : 0,
                    v0.bays[3].volume.c_str());
        pom68k::ScsiAgentSnapshot r0;
        const bool answered = ask(pom68k::ScsiAgentMailbox::Unmount, 3, r0);
        show("control-unmount", r0);
        runFrames(120);
        v0 = view(&none);
        std::printf("[control] answered=%d SCSI3 mounted=%d after the put away\n",
                    answered ? 1 : 0, v0.bays[3].mounted ? 1 : 0);
        if (!answered) { keyHold(0x24, 12); runFrames(240); show("control-after-ok", mem.scsi().agent().snapshot()); }
    }

    // ── 1. attach live, mount by request ─────────────────────────────────
    if (!mem.attachScsi(newPath, false, kNewId)) {
        std::fprintf(stderr, "FAIL: live attach refused\n");
        return 1;
    }
    pom68k::ScsiAgentSnapshot rep;
    pom68k::GuestScsiDriveHints hints{};
    if (!ask(pom68k::ScsiAgentMailbox::Mount, kNewId, rep)) {
        std::fprintf(stderr, "FAIL: mount request never answered\n");
        return 1;
    }
    show("mount", rep);
    if (rep.lastErr != 0 || rep.lastText != "Branche" || rep.lastDrive <= 0) {
        std::fprintf(stderr, "FAIL: mount not reported as « Branche » on a drive\n");
        return 1;
    }
    hints[kNewId] = rep.lastDrive;
    runFrames(120);
    pom68k::GuestScsiView v = view(&hints);
    if (!v.valid || !v.bays[kNewId].mounted || v.bays[kNewId].volume != "Branche") {
        std::fprintf(stderr, "FAIL: the guest's queues do not show « Branche » on SCSI %d\n",
                     kNewId);
        return 1;
    }

    // ── 2. unmount by request ────────────────────────────────────────────
    // The Finder opens its desktop database on every new volume; give it
    // the idle time to do so before the volume is asked to leave.
    runFrames(600);
    if (!ask(pom68k::ScsiAgentMailbox::Unmount, kNewId, rep) || rep.lastErr != 0) {
        show("unmount", rep);
        std::printf("front: %s\n", describe(runningProcesses(120)).c_str());
        const ScsiDisk& nd = mem.scsiDiskAt(kNewId);
        std::printf("target %d: reads %ld cmds/%ld blocks, writes %ld cmds/%ld blocks\n", kNewId,
                    nd.readCommands, nd.readBlocks, nd.writeCommands, nd.writeBlocks);
        // The driver's own record of its last Prime ("POMD" in the app heap).
        for (uint32_t a = 0x01000000; a < (32u << 20) - 32; a += 2) {
            if (mem.peek8(a) != 'P' || mem.peek8(a + 1) != 'O' || mem.peek8(a + 2) != 'M' ||
                mem.peek8(a + 3) != 'D') continue;
            std::printf("driver state @%08X: refNum %d openErr %d ringPos %d\n", a,
                        int16_t(peek32(a + 4) >> 16), int16_t(peek32(a + 6) >> 16),
                        int16_t(peek32(a + 12) >> 16));
            for (int k = 0; k < 8; k++) {
                const uint32_t r = a + 14 + uint32_t(k) * 14;
                std::printf("  ring[%d]: kind %d code %d err %d stat %d msg %d lba %u\n", k,
                            int16_t(peek32(r) >> 16), int16_t(peek32(r + 2) >> 16),
                            int16_t(peek32(r + 4) >> 16), int16_t(peek32(r + 6) >> 16),
                            int16_t(peek32(r + 8) >> 16), peek32(r + 10));
            }
        }
        std::printf("last CDBs:\n");
        for (const auto& c : lastCdbs) {
            std::printf("  ");
            for (uint8_t b : c) std::printf(" %02X", b);
            std::printf("\n");
        }
        dump("scsi_agent_unmount.ppm");
        std::fprintf(stderr, "FAIL: unmount not answered with noErr\n");
        return 1;
    }
    show("unmount", rep);
    runFrames(120);
    hints[kNewId] = 0;
    v = view(&hints);
    if (!v.valid || v.bays[kNewId].mounted) {
        std::fprintf(stderr, "FAIL: « Branche » still in the VCB queue after the unmount\n");
        return 1;
    }

    // ── 2b. the cable comes out, then goes back in ───────────────────────
    // With no VCB on the bay (docs/SCSI_HOTPLUG.md § 7) the target may
    // leave the bus: the controller's slot empties and the guest's queues
    // do not move — the agent keeps polling target 0 throughout. The
    // detach may land inside one of those polls, on another target: the
    // board refuses only a session on the detached ID.
    if (!mem.detachScsi(kNewId)) {
        std::fprintf(stderr, "FAIL: live detach refused (session on the target? %d)\n",
                     mem.scsi().sessionOn(kNewId) ? 1 : 0);
        return 1;
    }
    if (mem.scsi().target(kNewId) != nullptr || mem.scsiDiskAt(kNewId).present()) {
        std::fprintf(stderr, "FAIL: SCSI %d still on the bus after the detach\n", kNewId);
        return 1;
    }
    runFrames(180);                              // three seconds of polls without it
    v = view(&hints);
    if (!v.valid || v.bays[kNewId].mounted || !v.bays[0].mounted) {
        std::fprintf(stderr, "FAIL: the guest's queues moved across the detach\n");
        return 1;
    }
    const uint32_t pollsBefore = mem.scsi().agent().snapshot().polls;
    runFrames(120);
    if (mem.scsi().agent().snapshot().polls == pollsBefore) {
        std::fprintf(stderr, "FAIL: the agent stopped polling after the detach\n");
        return 1;
    }
    std::printf("detached: SCSI %d off the bus, agent still polling\n", kNewId);
    if (!mem.attachScsi(newPath, false, kNewId)) {
        std::fprintf(stderr, "FAIL: re-attach after the detach refused\n");
        return 1;
    }
    std::remove(newPath.c_str());

    // ── 3. mount again: the registered drive is reused, on the re-attached
    //       target — the same image, read afresh from the bus ─────────────
    if (!ask(pom68k::ScsiAgentMailbox::Mount, kNewId, rep) || rep.lastErr != 0 ||
        rep.lastText != "Branche") {
        show("remount", rep);
        std::fprintf(stderr, "FAIL: second mount failed\n");
        return 1;
    }
    show("remount", rep);
    hints[kNewId] = rep.lastDrive;
    runFrames(120);
    v = view(&hints);
    if (!v.valid || !v.bays[kNewId].mounted) {
        std::fprintf(stderr, "FAIL: « Branche » not back after the second mount\n");
        return 1;
    }

    // ── 4. the boot volume cannot be unmounted: fBsyErr, verbatim ────────
    if (!ask(pom68k::ScsiAgentMailbox::Unmount, 0, rep)) {
        std::fprintf(stderr, "FAIL: boot-volume unmount never answered\n");
        return 1;
    }
    show("unmount-boot", rep);
    if (rep.lastErr == 0) {
        std::fprintf(stderr, "FAIL: the boot volume's unmount was reported as a success\n");
        return 1;
    }
    v = view(&hints);
    if (!v.valid || !v.bays[0].mounted) {
        std::fprintf(stderr, "FAIL: the boot volume left the VCB queue\n");
        return 1;
    }
    std::printf("PASS: agent mounted, unmounted « Branche » on SCSI %d, the target left and "
                "rejoined the bus, remounted; boot volume refused with %d\n", kNewId,
                rep.lastErr);
    return 0;
}
