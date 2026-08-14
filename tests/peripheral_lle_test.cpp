// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The peripheral LLE/HLE registry (src/LleSession.h) — the model the
// "Périphériques" window renders.
//
// Why this gate exists at all: the WINDOW cannot be gated (no test opens an
// ImGui window in this tree — CLAUDE.md § GUI ↔ machine-thread contract), so
// everything that could be wrong on its own is pushed BELOW it, into pure
// functions, and gated here. What is left above the line is layout.
//
// The three properties worth the gate, in the order they would bite:
//   1. reporting HLE must still poison product qualification exactly as the
//      bare activateHle() call it replaced did — the `--lle-aarch64` promise
//      rests on it;
//   2. the staged selection must emit an explicit value for EVERY device,
//      including one whose choice matches what is running, because the
//      relaunch inherits this process's environment and a stale knob would
//      otherwise survive and win;
//   3. "can this device run LLE?" must be answered on the very paths the
//      device searched, never on a second copy of that list.

#include "LleSession.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

using namespace pom68k::lle;

Device makeDevice(Module m, const char* name, const char* knob, bool lle,
                  bool wanted, std::string fw,
                  std::vector<std::string> cands) {
    Device d;
    d.module = m;
    d.name = name;
    d.knob = knob;
    d.mode = lle ? Mode::Lle : Mode::Hle;
    d.why = lle ? Why::LleFirmware
                : wanted ? Why::HleNoDump : Why::HleForced;
    d.firmware = std::move(fw);
    d.candidates = std::move(cands);
    return d;
}
} // namespace

int main() {
    std::printf("peripheral_lle_test — LLE/HLE peripheral registry\n");

    // ── Registration, and the product-mode contract it must preserve ─────
    {
        beginSession();
        check(devices().empty(), "beginSession clears the registry");
        check(activeHleModules() == 0, "beginSession clears the HLE mask");

        reportFirmwareDevice(HleEgretCuda, "Cuda", "POM68K_CUDA_LLE",
                             /*lle=*/true, /*wanted=*/true,
                             "roms/cuda/341s0788.bin", {"roms/cuda/341s0788.bin"});
        check(devices().size() == 1, "an LLE report registers the device");
        check(activeHleModules() == 0,
              "an LLE report does NOT poison the HLE mask");
        check(devices()[0].mode == Mode::Lle &&
                  devices()[0].why == Why::LleFirmware,
              "LLE report carries mode + reason");
        check(devices()[0].firmware == "roms/cuda/341s0788.bin",
              "LLE report names the dump it loaded");

        reportFirmwareDevice(HleAdbModem, "PIC1654S", "POM68K_ADB_LLE",
                             /*lle=*/false, /*wanted=*/true, "",
                             {"roms/adbmodem/342s0440-b.bin"});
        check(devices().size() == 2, "a second device appends");
        check((activeHleModules() & HleAdbModem) != 0,
              "an HLE report poisons the mask (activateHle preserved)");
        check(!qualified(), "an HLE module refuses qualification");

        // A device that reports twice (a rebuilt machine) must not double up.
        reportFirmwareDevice(HleAdbModem, "PIC1654S", "POM68K_ADB_LLE",
                             /*lle=*/true, /*wanted=*/true, "x.bin", {"x.bin"});
        check(devices().size() == 2, "re-reporting replaces, never duplicates");
        check(devices()[1].mode == Mode::Lle, "the replacement is the new state");
        // The mask is deliberately NOT cleared by a later LLE report: within
        // one session a fallback that HAPPENED is a fact about that session,
        // and product mode must not be able to un-see it.
        check((activeHleModules() & HleAdbModem) != 0,
              "a later LLE report does not un-poison the session");
    }

    // ── The reason a report carries, on all three paths ──────────────────
    {
        beginSession();
        reportFirmwareDevice(HleEgretCuda, "Egret", "POM68K_EGRET_LLE",
                             false, /*wanted=*/true, "", {"nope.bin"});
        check(devices()[0].why == Why::HleNoDump,
              "wanted + failed = HleNoDump (the dump is missing)");
        beginSession();
        reportFirmwareDevice(HleEgretCuda, "Egret", "POM68K_EGRET_LLE",
                             false, /*wanted=*/false, "", {"nope.bin"});
        check(devices()[0].why == Why::HleForced,
              "not wanted = HleForced (the knob said 0)");
    }

    // ── dumpAvailable answers on the device's own candidate list ─────────
    {
        beginSession();
        const std::string tmp = "peripheral_lle_test.tmpdump";
        std::remove(tmp.c_str());

        Device absent = makeDevice(HleEgretCuda, "Egret", "POM68K_EGRET_LLE",
                                   false, true, "", {tmp, "also-absent.bin"});
        check(!dumpAvailable(absent), "no candidate present -> LLE unavailable");

        { std::ofstream(tmp, std::ios::binary) << "dump"; }
        check(dumpAvailable(absent),
              "dropping a file on a searched path makes LLE available");
        std::remove(tmp.c_str());

        // A device already running firmware needs no probe — its candidate
        // list may well be relative to a working directory since changed.
        Device live = makeDevice(HleEgretCuda, "Egret", "POM68K_EGRET_LLE",
                                 true, true, "loaded.bin", {"absent.bin"});
        check(dumpAvailable(live), "a device already on LLE is always available");
    }

    // ── The staged selection -> environment mapping ──────────────────────
    {
        beginSession();
        const std::vector<Device> live = {
            makeDevice(HleEgretCuda, "Cuda", "POM68K_CUDA_LLE", true, true,
                       "c.bin", {"c.bin"}),
            makeDevice(HleAdbModem, "PIC1654S", "POM68K_ADB_LLE", false, false,
                       "", {"a.bin"}),
        };

        // Nothing staged: nothing to apply, and nothing to emit.
        check(pendingCount(live, {}) == 0, "no staging -> nothing pending");
        check(envForSelection(live, {}).empty(),
              "no staging -> no environment written");

        // Flip the ADB back to LLE, leave the Cuda as it runs.
        const std::vector<Choice> want = {
            {HleEgretCuda, Mode::Lle},          // same as live, on purpose
            {HleAdbModem, Mode::Lle},           // a real change
        };
        check(pendingCount(live, want) == 1,
              "only the device whose choice differs counts as pending");

        const auto env = envForSelection(live, want);
        check(env.size() == 2,
              "EVERY selected device is emitted, not only the changed one");
        bool cudaOne = false, adbOne = false;
        for (const auto& [knob, value] : env) {
            if (knob == "POM68K_CUDA_LLE" && value == "1") cudaOne = true;
            if (knob == "POM68K_ADB_LLE" && value == "1") adbOne = true;
        }
        check(adbOne, "the changed device emits its knob=1");
        // This is the one that would rot silently: the user forced HLE in an
        // earlier session, so POM68K_ADB_LLE=0 is in this process's
        // environment and the relaunch inherits it. Re-asserting "1" for the
        // UNCHANGED device is what makes an undo actually undo.
        check(cudaOne,
              "an unchanged device re-asserts its knob (the relaunch inherits "
              "the env)");

        // And the HLE direction writes 0, not an unset.
        const auto off = envForSelection(live, {{HleEgretCuda, Mode::Hle}});
        check(off.size() == 1 && off[0].first == "POM68K_CUDA_LLE" &&
                  off[0].second == "0",
              "choosing HLE writes knob=0");
    }

    // ── A machine with no LLE-capable device is a real answer ────────────
    {
        beginSession();
        check(devices().empty() && pendingCount(devices(), {}) == 0,
              "a machine with no such device yields an empty list, not a bug");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
