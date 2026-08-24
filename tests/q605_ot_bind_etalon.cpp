// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LLE step 7 gate (LLE_VS_HLE §1.10): Open Transport's LLAP driver must
// bind .MPP against the in-process AppleTalk stack — the configuration
// where the wedge was captured (SCCDBG 2026-07-24: spin at $D1F04
// waiting for RR0 Break/Abort to clear, unblocked only by the
// POM68K_SCC_CLEANLINE escape hatch; with it, Netscape reached
// www.apple.com through MacIP/NAT). Unlike System 7's LAP, OT waits for
// the standing abort to CLEAR before binding — so a virgin,
// never-driven line must read clean (no FM0 transitions → no recovered
// clock → no abort condition); the no-peer abort may exist only once
// the line has actually carried a frame (an LLAP trailer ends in a
// real abort).
// The wiring mirrors main.cpp wireLocalTalk: hub attached (router-lite
// with its periodic RTMP beacon), express lapCTS synthesis, boosted
// lossless virtual wire, sliced quanta with the hub ticked in machine
// cycles. Proof of bind = the guest's DDP conversation with the stack
// (stats.ddpIn) after its lapENQ acquisition — a wedged OT probes its
// node ID and then never speaks DDP.
// Boots the FF7439EE Quadra 605 ROM + Mac OS 8.1 (AppleTalk active in
// the on-disk prefs). Soft-skips when the assets are absent.

#include "AssetFingerprint.h"
#include "AtalkHub.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    return testasset::findAny(names);
}
} // namespace

int main() {
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    std::string diskPath = findAsset({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });
    std::fflush(stdout);

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);

    // ── main.cpp wireLocalTalk, hub-without-cable flavor ──
    AtalkHub hub;
    hub.setService("afp", false);      // router/node only: no share dir,
    hub.setService("pap", false);      // no spool, no NAT — the bind gate
    hub.setService("macip", false);    // needs just the RTMP/NBP stack
    const int byteCycles = int(mem.cpuHz() / 28800);
    const int64_t hubHz = int64_t(byteCycles) * 28800;
    mem.scc().setByteCycles(byteCycles);
    mem.scc().setWirePace(std::max(byteCycles / 8, 64));
    mem.scc().setLosslessRx(true);
    hub.attach(mem, hubHz, nullptr);

    long txFrames = 0;
    long typeHist[256] = {};
    mem.scc().onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
        if (ch != 0) return;
        txFrames++;
        if (n >= 3) typeHist[d[2]]++;
        if (n == 3 && d[2] == 0x84) {              // lapRTS → express lapCTS
            if (d[0] != 0xFF) {
                const uint8_t cts[3] = { d[1], d[0], 0x85 };
                mem.scc().injectRxFrame(0, cts, 3, true);
            }
            return;
        }
        if (n == 3 && d[2] == 0x85) return;
        hub.onGuestFrame(d, n);
    };

    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 416667;      // 25 MHz / ~60 Hz
    constexpr int kMaxFrames = 12000;         // same budget as the boot etalon
    constexpr int kSlices = 64;               // main.cpp runQuantumWithWire
    constexpr long kDdpTarget = 3;
    AtalkStack::Stats st;
    for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
        for (int s = 0; s < kSlices; s++) {
            cpu.runCycles(kFrameCycles / kSlices);
            hub.tick(cpu.machineClock());
        }
        if (!(frame % 30)) {
            st = hub.snapshot().net;
            if (st.ddpIn >= kDdpTarget && st.enqSeen > 0) break;
        }
    }
    st = hub.snapshot().net;

    std::printf("Tx frames=%ld; ENQ seen=%ld DDP in=%ld frames in=%ld "
                "NBP lookups=%ld guest node=%u; types:", txFrames, st.enqSeen,
                st.ddpIn, st.framesIn, st.nbpLookups, st.guestNode);
    for (int t = 0; t < 256; t++)
        if (typeHist[t]) std::printf(" $%02X=%ld", t, typeHist[t]);
    std::printf("\n");
    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted (double fault)\n");
        return 1;
    }
    // Acquisition alone is not a bind: the $D1F04 spin happens around the
    // ENQ dance, waiting on RR0 bit 7 — a wedged OT never speaks DDP.
    bool ok = st.enqSeen > 0 && st.ddpIn >= kDdpTarget;
    std::printf("%s\n", ok
        ? "PASSED — OT bound .MPP: DDP reached the stack after the ENQ dance"
        : "FAILED — OT never bound .MPP (no DDP after the ENQ dance)");
    return ok ? 0 : 1;
}
