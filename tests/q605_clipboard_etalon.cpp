// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Host text → guest keys → guest scrap → host text (Quadra 605, 8.1) ──
//
// The round trip order 9 of docs/SNOW_IMPLEMENTATION_PLAN.md asks for, on
// the real guest: boot `hdv/MacOS-8.1-boot.vhd` (an AZERTY System), launch
// SimpleText as `q605_simpletext_etalon` does, then
//
//   type   a text with capitals, digits, punctuation, accented letters and
//          a line break, planned by planTyping(…, FrenchAzerty) and emitted
//          by TextTyper on machine-time deadlines, polled once per frame —
//          the production path, not the harness's own typing table;
//   copy   Cmd-A, Cmd-C: ScrapCount moves, SimpleText puts TEXT;
//   read   readGuestScrapText through the debugger's logical read
//          (DebugCpuTarget::readMemory) — what « Lire le presse-papiers de
//          l'invité » does — and the UTF-8 that comes back must be the text
//          that went in, byte for byte.
//
// SKIP without the ROM and the volume.

#include "DebugCpuTarget.h"
#include "DebugSession.h"
#include "GuestScrap.h"
#include "Q605ApplicationHarness.h"
#include "TextTyping.h"

using namespace q605app;

namespace {

// Every class of character the AZERTY plan types: a capital, an accent
// on the number row, shifted digits, punctuation on moved keys, Return.
constexpr const char* kTyped = "Clé 42 : ça marche!\nàù § (ok).";

} // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (romData.size() != Q605Memory::kRomSize || !mem.loadRom(romData) ||
        !mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    pom68k::dbg::Session session;
    pom68k::dbg::CpuTarget<Cpu040, Q605Memory> target(cpu, mem, session);
    auto scrap = [&] {
        return pom68k::readGuestScrapText([&](uint32_t a, uint8_t* out, std::size_t n) {
            std::vector<pom68k::dbg::ByteState> st(n);
            target.readMemory(pom68k::dbg::Space::Logical, a, out, st.data(), n);
            for (auto b : st) if (b != pom68k::dbg::ByteState::Ok) return false;
            return true;
        });
    };

    bool ok = bootToFinder(12000) && ensureFastKeys();
    std::printf("boot: %s\n", ok ? "Finder up, short taps accepted" : "NOT READY");
    if (ok) {
        closeAllFinderWindows();
        openByClick(592, 50, 600);              // the volume "Mac-8.1-US"
        openBySelect("applic", 600);            // Applications
        openBySelect("simplet", 1500);          // SimpleText → untitled window
        ok = processRuns(runningProcesses(), "SimpleText");
        std::printf("launch: SimpleText %s\n", ok ? "runs" : "NOT RUNNING");
    }

    const pom68k::TypingPlan plan = pom68k::planTyping(kTyped, pom68k::GuestLayout::FrenchAzerty);
    bool typedAll = false, copied = false, same = false;
    std::uint16_t countBefore = 0;
    pom68k::GuestScrapText read;
    if (ok) {
        pom68k::TextTyper typer;
        typer.start(plan);
        long frames = 0;
        while (typer.active() && frames < 6000) {
            if (const auto step = typer.poll(cpu.machineClock(), mem.cpuHz()))
                mem.keyEvent(step->code, step->down);
            runFrames(1);
            ++frames;
        }
        runFrames(60);
        typedAll = plan.skipped == 0 && !typer.active() && typer.charactersLeft() == 0;
        std::printf("type: %zu characters, %zu without a key, %ld frames\n",
                    plan.characters, plan.skipped, frames);
        dump("q605_clipboard_typed.ppm");

        countBefore = scrap().count;
        command(adbFor('a'), 120);              // Select All (Cmd + the 'a' key)
        command(adbFor('c'), 300);              // Copy
        read = scrap();
        copied = read.count != countBefore;
        same = read.ok() && read.utf8 == kTyped && !read.truncated;
        std::printf("copy: ScrapCount %u -> %u, status %d %s, %u MacRoman bytes\n",
                    countBefore, read.count, int(read.status), read.reason.c_str(),
                    read.macBytes);
        std::printf("read back: \"%s\"\n", read.utf8.c_str());
    }
    const bool passed = ok && typedAll && copied && same && !cpu.isHalted();
    std::printf("%s — Quadra 605 clipboard round trip (typed %d, copied %d, identical %d)\n",
                passed ? "PASSED" : "FAILED", typedAll, copied, same);
    return passed ? 0 : 1;
}
