// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The guest agent as a beyond-boot proof for any boot etalon ──
// POM68K_TEST_AGENT=1 makes a boot etalon (a) put « POM68K Disques » into
// its boot volume's Startup Items in memory, right after the disk is
// attached (src/HfsInject.h, the product path of GuiAgentAutostart.h), and
// (b) on top of its own Finder verdict, require that the Finder launched
// the agent — its first poll of the mailbox — and that a blank disk
// attached live is mounted on request, by name. What that proves past the
// boot signature: the Process Manager launched an application, the SCSI
// Manager carried its vendor commands, the File Manager mounted a volume
// the agent's own driver serves. One header, twelve platforms — the
// DaynaBootProbe.h pattern. Unset, the etalon is exactly the gate it was.
//
// A System without a Startup Items folder (System 6) cannot carry this
// proof; the variant is not registered for such images, and the probe
// says so rather than passing.

#pragma once
#include "FinderSignature.h"
#include "HfsBlankVolume.h"
#include "HfsInject.h"
#include "Mmu030Peek.h"
#include "PortableEnv.h"
#include "ScsiAgentMailbox.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace agentboot {

inline bool enabled() {
    const char* v = std::getenv("POM68K_TEST_AGENT");
    return v && *v == '1';
}

inline std::string binaryPath() {
    for (const char* p : { "dev/scsiagent/build/POM68KDisques.bin", "share/POM68KDisques.bin" })
        if (std::ifstream(p, std::ios::binary)) return p;
    return {};
}

// A boot etalon that names the front application in its verdict: with the
// probe on, the agent may already be in front — it launches with the
// desktop and the fast boards reach the verdict after it — and it stays
// there: it is background-capable, so the Finder runs behind it without
// ever becoming CurApName again (System 7.5.5, Color Classic II, measured
// 2026-09-15 over 300 frames). Either name is the Finder having finished
// its startup. FinderSignature.h's finderRuns is for a front application
// that is NOT background-capable (Stickies), which does yield the name.
inline bool finderOrAgent(const std::string& app) {
    return app == "Finder" || (enabled() && app == "POM68KDisques");
}

// After the boot disk is attached. False = the etalon must fail: the
// variant was registered for an image that carries Startup Items.
template <class M>
bool install(M& mem) {
    if (!enabled()) return true;
    const std::string bin = binaryPath();
    if (bin.empty()) { std::fprintf(stderr, "FAIL: agent probe: no POM68KDisques.bin\n"); return false; }
    std::ifstream in(bin, std::ios::binary);
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(raw, app, err)) {
        std::fprintf(stderr, "FAIL: agent probe: %s: %s\n", bin.c_str(), err.c_str());
        return false;
    }
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    const hfsinject::Outcome o = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("agent probe: %s\n", o.message.c_str());
    return o.kind == hfsinject::Outcome::Installed || o.kind == hfsinject::Outcome::AlreadyPresent;
}

// The verdict on top of the etalon's own, once its Finder signature holds.
// Runs frames of `frameCycles` on `cpu`: up to 60 s of guest time for the
// first poll, then a blank volume on the first free ID and 30 s for the
// mount report.
// The front application's name (CurApName), through the PMMU on a 68030
// whose translation is on, straight from RAM elsewhere — the readers the
// etalons already use.
template <class M, class C>
std::string frontApplication(M& mem, C& cpu) {
    if constexpr (requires { cpu.getTC(); cpu.getCRP(); cpu.getSRP(); }) {
        auto peek = [&](uint32_t a) {
            uint32_t phys = 0;
            if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(), a, 5,
                                       [&](uint32_t x) { return mem.peek8(x); }, &phys))
                return -1;
            return int(mem.peek8(phys));
        };
        return findersig::curApNameAt(peek);
    } else {
        return findersig::curApName(mem);
    }
}

// `runFrame` advances the machine by one 60 Hz frame the way the etalon's
// own loop does — a compact's MacFrameClock, everyone else's runCycles.
template <class M, class C, class F>
bool checkWith(M& mem, C& cpu, F runFrame, bool ok) {
    if (!enabled() || !ok) return ok;
    auto polls = [&] { return mem.scsi().agent().snapshot().polls; };
    const long scsiBefore = mem.scsi().commands;
    int frame = 0;
    bool pressed = false;
    for (; frame < 7200 && !polls() && !cpu.isHalted(); frame++) {
        runFrame();
        if (frame % 600 == 599)
            std::printf("agent probe: %d s — front « %s », SCSI +%ld\n", (frame + 1) / 60,
                        frontApplication(mem, cpu).c_str(), mem.scsi().commands - scsiBefore);
        // Ten seconds without a poll AND without a disk read: the Finder
        // is not launching anything — a boot-time alert is up (the 8.1
        // volume's startup-alias alert, a System 7.5 volume's own), and a
        // modal Finder runs no Startup Items until it is dismissed. Return
        // takes the default button, once; a real desktop ignores it.
        if (!pressed && frame == 599 && mem.scsi().commands == scsiBefore) {
            std::printf("agent probe: no disk activity for 10 s — pressing Return "
                        "for a boot-time alert\n");
            mem.keyEvent(0x24, true);
            for (int f = 0; f < 8; f++) runFrame();
            mem.keyEvent(0x24, false);
            pressed = true;
        }
    }
    if (!polls()) {
        std::fprintf(stderr, "FAIL: agent probe: no poll within 120 s of the Finder verdict "
                             "(front « %s », halted %d, SCSI +%ld)\n",
                     frontApplication(mem, cpu).c_str(), cpu.isHalted() ? 1 : 0,
                     mem.scsi().commands - scsiBefore);
        return false;
    }
    std::printf("agent probe: first poll %d frames after the Finder verdict, front « %s »\n",
                frame, frontApplication(mem, cpu).c_str());
    int id = -1;
    for (int i = 1; i <= 6 && id < 0; i++)
        if (!mem.scsi().target(i)) id = i;
    if (id < 0) { std::fprintf(stderr, "FAIL: agent probe: no free SCSI ID\n"); return false; }
    const std::string path = pom68kTempPath("agent_probe_blank.img");
    std::vector<uint8_t> blank = hfsblank::build(20ull << 20, "Branche");
    blank[1024 + 10] |= 0x01;                       // unmounted cleanly
    if (!hfsblank::writeFile(path, blank) || !mem.attachScsi(path, false, id)) {
        std::fprintf(stderr, "FAIL: agent probe: cannot attach the blank volume\n");
        return false;
    }
    std::remove(path.c_str());
    mem.scsi().agent().post(pom68k::ScsiAgentMailbox::Mount, id);
    pom68k::ScsiAgentSnapshot rep;
    for (int f = 0; f < 1800 && !cpu.isHalted(); f++) {
        runFrame();
        rep = mem.scsi().agent().snapshot();
        if (!rep.pending) break;
    }
    const bool mounted = !rep.pending && rep.lastErr == 0 && rep.lastText == "Branche" && rep.lastDrive > 0;
    std::printf("agent probe: mount on SCSI %d %s (err %d « %s », drive %d, polls %u)\n", id,
                mounted ? "reported" : "NOT reported", rep.lastErr, rep.lastText.c_str(),
                rep.lastDrive, rep.polls);
    if (!mounted) std::fprintf(stderr, "FAIL: agent probe: the agent did not mount the blank volume\n");
    return mounted;
}

template <class M, class C>
bool check(M& mem, C& cpu, long long frameCycles, bool ok) {
    return checkWith(mem, cpu, [&] { cpu.runCycles(frameCycles); }, ok);
}

} // namespace agentboot
