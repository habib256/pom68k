// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Replay a RECORDED GUI session on the LC II at full speed and time it.
//
// This is the workload instrument TODO.md § 3 asks for after the D1F0
// lesson (a fallback histogram is not a temporal profile): the unit of
// measurement is a real session — the user's own mouse, keys and opened
// applications, captured by POM68K_INPUT_RECORD (`src/InputJournal.h`) —
// replayed from its snapshot with every event at its recorded machine
// clock. The work is fixed by construction (same guest cycles, same
// inputs), so the wall clock IS the measurement and the × real-time ratio
// is the quotable number, exactly the form `docs/MEASURING.md` § R3 wants
// and the form a boot etalon can never give.
//
// A MEASUREMENT, not a gate: EXCLUDE_FROM_ALL, no CTest entry, no
// threshold. Engine selection is the usual pair: POM68K_CPU_ENGINE /
// POM68K_JIT_BACKEND (testjit::resolveFromEnvironment). The architectural
// fingerprint is printed so an A/B between engines can refuse itself when
// the two arms did not do the same work.
//
// What it refuses, each for a reason it states: a journal from another
// profile (this binary builds the LC II construction the GUI uses — 10 MB,
// Model::LcII); a session recorded with the AppleTalk hub or LToUDP cable
// active (outside traffic does not replay); a snapshot whose hash is not
// the one the journal recorded (the fixture drifted — the drVolAtrb
// lesson); a journal crossing a mid-session state restore.
//
// Protocol note: record against a THROWAWAY COPY of the boot image. A GUI
// session flushes guest writes back to the .vhd, and a replay against an
// image the session itself mutated is a replay of a different disk — the
// 2026-08-11 GUI measurements used four throwaway copies of one volume for
// exactly this reason. This bench attaches the image read-only.

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "Cpu030.h"
#include "InputJournal.h"
#include "InputReplay.h"
#include "JitTestConfig.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string jpath = argc > 1 ? argv[1] : "session.journal";
    pom68k::InputJournal j;
    std::string err;
    if (!pom68k::loadInputJournal(jpath, j, err)) {
        std::fprintf(stderr, "FAIL: %s\n", err.c_str());
        std::fprintf(stderr, "usage: replay_bench_lcii <session.journal>\n"
                     "(recorded with POM68K_INPUT_RECORD=<path> in the GUI)\n");
        return 1;
    }
    if (!j.complete)
        std::fprintf(stderr, "note: journal has no `end` (session did not "
                     "stop cleanly); replaying to its last event\n");
    if (j.note("profile") != "lcii") {
        std::fprintf(stderr, "FAIL: journal profile is '%s'; this bench "
                     "builds the LC II only\n", j.note("profile").c_str());
        return 1;
    }
    if (j.note("network") == "1") {
        std::fprintf(stderr, "FAIL: session recorded with an external wire "
                     "(AppleTalk hub / LToUDP) — outside traffic does not "
                     "replay\n");
        return 1;
    }

    // The machine, constructed the way the GUI runner constructs it: the
    // snapshot refuses anything else (RAM size, ROM identity).
    const std::string romPath = j.note("rom");
    std::ifstream romIn(romPath, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(romIn)),
                                 std::istreambuf_iterator<char>());
    if (romData.empty()) {
        std::fprintf(stderr, "FAIL: cannot read the session's ROM (%s)\n",
                     romPath.c_str());
        return 1;
    }
    V8Memory mem(pom68k::defaultCoreConfig(), 0xA00000);
    if (!mem.loadRom(romData)) {
        std::fprintf(stderr, "FAIL: bad ROM (%s)\n", romPath.c_str());
        return 1;
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    testasset::report("journal", jpath);
    testasset::report("rom", romPath);
    std::string bootImage;
    for (const auto& kv : j.notes)
        if (kv.first == "media" && bootImage.empty()) bootImage = kv.second;
    if (!bootImage.empty()) {
        if (!mem.attachScsi(bootImage)) {   // read-only: replays never write
            std::fprintf(stderr, "FAIL: cannot attach the session's boot "
                         "image (%s)\n", bootImage.c_str());
            return 1;
        }
        testasset::report("disk", bootImage);
    }

    // Restore the snapshot the recorder took — identity first.
    const std::string spath = j.note("snapshot");
    std::ifstream snapIn(spath, std::ios::binary);
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(snapIn)),
                              std::istreambuf_iterator<char>());
    if (blob.empty()) {
        std::fprintf(stderr, "FAIL: cannot read the snapshot (%s)\n",
                     spath.c_str());
        return 1;
    }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx",
                  (unsigned long long) sav::hash(blob));
    if (!j.note("statehash").empty() && j.note("statehash") != hex) {
        std::fprintf(stderr, "FAIL: snapshot hash %s != journal statehash "
                     "%s — the fixture drifted since the recording\n",
                     hex, j.note("statehash").c_str());
        return 1;
    }
    if (!pom68k::load(mem, cpu, pom68k::SnapMachine::LcII,
                      blob.data(), blob.size(), err)) {
        std::fprintf(stderr, "FAIL: snapshot refused: %s\n", err.c_str());
        return 1;
    }

    const long long fc = [&] {
        const std::string s = j.note("framecycles");
        return s.empty() ? (long long) 640 * 407 : std::atoll(s.c_str());
    }();
    const long long cpuHz = [&] {
        const std::string s = j.note("cpuhz");
        return s.empty() ? (long long) V8Memory::kCpuHz : std::atoll(s.c_str());
    }();

    const long long clk0 = cpu.machineClock();
    const long scsi0 = mem.scsi().commands;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = replayJournal(mem, cpu, j, [&] {
        if (mem.cpuHeld()) mem.tick(fc);
        else cpu.runCycles(fc);
        return cpu.machineClock();
    });
    const auto t1 = std::chrono::steady_clock::now();

    const double wall = std::chrono::duration<double>(t1 - t0).count();
    const double guest = double(cpu.machineClock() - clk0) / double(cpuHz);
    std::printf("replay %s engine=%-6s events=%zu"
                "  wall=%.2fs for %.2fs of guest time  \xC3\x97%.2f real time"
                "  scsi=%ld  fp=%016llx\n",
                jpath.c_str(), cpu.engine() ? "jit" : "interp",
                j.events.size(), wall, guest,
                wall > 0 ? guest / wall : 0.0,
                mem.scsi().commands - scsi0,
                (unsigned long long) bench::fingerprint(cpu));
    if (!ok) {
        std::fprintf(stderr, "FAIL: replay did not reach the journal's end "
                     "(halted machine, stalled clock, or a mid-session "
                     "state restore)\n");
        return 1;
    }
    return 0;
}
