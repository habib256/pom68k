// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M7 gate: a real compact ROM boots System 6 from a SCSI hard disk through the
// full NCR 5380 chain (arbitration, selection, command/data/status phases,
// pseudo-DMA). No floppy inserted, so the boot MUST come from SCSI. Checks
// the Finder desktop signature.  POM68K_COMPACT_MODEL selects Plus (default),
// SE, SE FDHD or Classic so every compact profile has an explicit SCSI proof.

#include "AgentBootProbe.h"
#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "DaynaBootProbe.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    const char* which = std::getenv("POM68K_COMPACT_MODEL");
    MacMemory::Model model = MacMemory::Model::Plus;
    const char* romRel = "roms/macplus.rom";
    const char* name = "Macintosh Plus";
    if (which && !std::strcmp(which, "se")) {
        model = MacMemory::Model::SE;
        romRel = "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM";
        name = "Macintosh SE";
    } else if (which && !std::strcmp(which, "sefdhd")) {
        model = MacMemory::Model::SEFDHD;
        romRel = "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM";
        name = "Macintosh SE FDHD";
    } else if (which && !std::strcmp(which, "classic")) {
        model = MacMemory::Model::Classic;
        romRel = "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom";
        name = "Macintosh Classic";
    }
    std::string rom = find(romRel), hd = testasset::overrideImage();   // POM68K_BEYOND_IMG: the agent variant's System 7
    if (hd.empty()) hd = find("hdv/HD20SC.vhd");
    if (rom.empty() || hd.empty()) {
        std::printf("SKIP: needs %s + hdv/HD20SC.vhd\n", romRel);
        return 0;
    }
    testasset::report({ rom, hd });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem(daynaboot::config(), model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu68k cpu(mem, jit::defaultResolvedConfig());
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(hd)) { std::fprintf(stderr, "FAIL: bad SCSI image\n"); return 1; }
    if (!agentboot::install(mem)) return 1;

    MacFrameClock fc;
    fc.resync(cpu);
    // RAM test (~45 s) + SCSI probe + driver load + System launch.
    // System 7 on the agent variant's image needs longer: POM68K_FRAMES.
    const long frames = std::getenv("POM68K_FRAMES") ? std::atol(std::getenv("POM68K_FRAMES"))
                        : model == MacMemory::Model::Plus ? 5400 : 6000;
    // On the agent variant's System 7 volume the boot stops at the
    // AppleTalk CautionAlerts a Sys7 System raises with no EtherTalk card
    // — macii_sys7_boot_etalon's dismissal, verbatim: a modal (CurActivate
    // bit 31) on a stalled SCSI count is answered with a real Return, at
    // most six times. System 6 never trips it.
    const bool sys7 = !testasset::overrideImage().empty();
    auto p32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    long stall = 0, lastCmds = -1;
    int posts = 0, cool = 0, keyUpIn = 0;
    for (long f = 0; f < frames; f++) {
        fc.runFrame(cpu, mem);
        if (!sys7) continue;
        if (keyUpIn && !--keyUpIn) mem.keyEvent(0x24, false);   // Return up
        if (cool > 0) { cool--; continue; }
        const long cmds = mem.scsi().commands;
        stall = (cmds == lastCmds && cmds > 200) ? stall + 1 : 0;
        lastCmds = cmds;
        const bool modal = (p32(0xA64) & 0x80000000u) != 0;     // CurActivate
        if (modal && stall >= 45 && posts < 6) {
            mem.keyEvent(0x24, true);                           // Return down
            keyUpIn = 6;                                        // ~100 ms hold
            posts++; cool = 90; stall = 0;
        }
    }
    if (sys7) std::printf("sys7: %d alert(s) dismissed during the boot\n", posts);

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    double menuBar = blackRatio(2, 16);
    double desktop = blackRatio(120, 240);
    std::printf("menu bar black %.2f (want <0.30), desktop %.2f (want ~0.50)\n",
                menuBar, desktop);
    // With the agent installed (POM68K_TEST_AGENT=1) the Finder has already
    // launched it by now on these fast boots, and its status window sits in
    // the sampled band: the 50 % weave reads ~0.44 through it (measured
    // 2026-09-15, all four compacts). The bare-desktop band stays for the
    // gate this is otherwise.
    const double desktopFloor = agentboot::enabled() ? 0.30 : 0.45;
    if (menuBar > 0.30 || desktop < desktopFloor || desktop > 0.55) {
        std::fprintf(stderr, "FAIL: not the Finder desktop — SCSI boot failed\n");
        return 1;
    }
    if (!daynaboot::check(mem, true)) return 1;
    if (!agentboot::checkWith(mem, cpu, [&] { fc.runFrame(cpu, mem); }, true)) return 1;
    std::printf("scsi_boot_etalon: %s booted from SCSI to the Finder, gate passed\n",
                name);
    return 0;
}
