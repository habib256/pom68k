// POM68K — guest serial communication through the SCC (Quadra 605, 8.1)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Order 8 of docs/SNOW_IMPLEMENTATION_PLAN.md: "guest communication through
// SCC, including backpressure". « POM68K Série » (dev/serprobe), a Serial
// Driver client on the modem port, is installed into the 8.1 boot volume's
// Startup Items by the host (src/HfsInject.h, in memory) and launched by
// the Finder with no input. The host end is the « Ports série » terminal
// itself (SerialTerminal.h, an endpoint port) driven through the product's
// pump (pumpSerialChannel / serialGuestByte), 64 times a frame like
// GuiHostServices::runNetworkQuantum:
//
//   hello     the guest's "POM68K SERIE PRET" appears in the terminal log;
//   burst     6000 bytes queued through SerialTerminal::send — accepted only
//             up to its bound, the rest re-offered as the guest drains —
//             reach the SCC three at a time, only when its receiver can
//             take a byte; the guest's Serial Driver buffers them;
//   reply     the guest answers "RECU 6000 <sum>" with an order-sensitive
//             sum the host recomputes: every byte crossed, in order.
//
// SKIP without the ROM, the volume, or a Retro68 build of dev/serprobe.

#include "HfsInject.h"
#include "JitTestConfig.h"
#include "Q605ApplicationHarness.h"
#include "SerialHostTransport.h"
#include "SerialTerminal.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace q605app;

namespace {

constexpr int kModem = 1;              // SCC channel A: .AIn/.AOut
constexpr std::size_t kPayload = 6000;  // more than SerialTerminal::kInputBytes

std::string payload() {
    std::string bytes(kPayload, '\0');
    for (std::size_t i = 0; i < kPayload; ++i)
        bytes[i] = char((i * 37 + i / 7) & 0xFF);
    return bytes;
}

unsigned long expectedSum(const std::string& bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        sum += std::uint32_t(std::uint8_t(bytes[i])) * std::uint32_t(i + 1);
    return sum;
}

} // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    const std::string probe = find("dev/serprobe/build/POM68KSerie.bin");
    if (rom.empty() || img.empty() || probe.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd + a Retro68 "
                    "build of dev/serprobe (dev/serprobe/build/POM68KSerie.bin)\n");
        return 0;
    }
    testasset::report({rom, img, probe});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    std::ifstream pb(probe, std::ios::binary);
    const std::vector<uint8_t> probeRaw((std::istreambuf_iterator<char>(pb)),
                                        std::istreambuf_iterator<char>());
    hfsinject::MacBinary app;
    std::string err;
    if (romData.size() != Q605Memory::kRomSize || !hfsinject::decodeMacBinary(probeRaw, app, err)) {
        std::fprintf(stderr, "FAIL: bad ROM or probe (%s)\n", err.c_str());
        return 1;
    }
    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(romData) || !mem.attachScsi(img, false, 0)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    const hfsinject::Outcome o = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("install: %s\n", o.message.c_str());
    if (o.kind != hfsinject::Outcome::Installed) return 1;

    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;

    // The product's wiring of one endpoint port (GuiHostServices).
    SerialTerminal terminal(true);
    mem.scc().setByteCycles(int(mem.cpuHz() / 28800));
    long txPerChannel[2] = {0, 0};
    mem.scc().onTxByte = [&](int channel, std::uint8_t value) {
        if (channel >= 0 && channel < 2) ++txPerChannel[channel];
        if (channel == kModem)
            serialGuestByte(value, static_cast<SerialHostTransport*>(nullptr), &terminal);
    };
    const std::string bytes = payload();
    std::size_t offered = 0, refusedAtOnce = 0;
    bool sending = false;
    auto pump = [&] {
        if (sending && offered < bytes.size()) {
            const std::size_t taken = terminal.send(std::string_view(bytes).substr(offered));
            if (taken < bytes.size() - offered) ++refusedAtOnce;
            offered += taken;
        }
        pumpSerialChannel(mem.scc(), kModem, static_cast<SerialHostTransport*>(nullptr),
                          &terminal);
    };
    auto logged = [&] { return terminal.view().text; };

    if (!bootToFinder(14000)) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d)\n", cpu.isHalted());
        return 1;
    }
    std::printf("boot: Finder up, processes %s\n", describe(runningProcesses()).c_str());
    gFrameSlices = 64;
    gAfterFrame = pump;
    bool hello = false;
    for (int frame = 0; frame < 20000 && !hello && !cpu.isHalted(); frame += 30) {
        runFrames(30);
        hello = logged().find("POM68K SERIE PRET\n") != std::string::npos;
    }
    std::printf("hello: %s (guest -> host %llu bytes; SCC Tx B %ld, A %ld)\n",
                hello ? "received" : "NOT RECEIVED",
                (unsigned long long)terminal.view().fromGuest, txPerChannel[0], txPerChannel[1]);
    if (!hello) std::printf("processes: %s\n", describe(runningProcesses()).c_str());

    bool replied = false;
    long frames = 0;
    if (hello) {
        sending = true;
        for (; frames < 9000 && !replied && !cpu.isHalted(); frames += 30) {
            runFrames(30);
            replied = logged().find("FIN\n") != std::string::npos;
        }
    }
    const SerialTerminal::View v = terminal.view();
    char want[64];
    std::snprintf(want, sizeof want, "RECU %zu %lu", kPayload, expectedSum(bytes));
    const bool exact = v.text.find(std::string(want) + "\n") != std::string::npos;
    std::printf("burst: %zu bytes offered, %llu reached the guest's SCC, queue refused part "
                "of an offer %zu time(s), %ld frames\n", offered,
                (unsigned long long)v.toGuest, refusedAtOnce, frames);
    const std::size_t at = v.text.find("RECU");
    std::printf("reply: %s (want %s)\n", at == std::string::npos ? "none"
                : v.text.substr(at, v.text.find('\n', at) - at).c_str(), want);
    const bool passed = hello && replied && exact && v.toGuest == kPayload &&
                        refusedAtOnce > 0 && !cpu.isHalted();
    std::printf("%s — Quadra 605 serial etalon (hello %d, reply %d, exact %d)\n",
                passed ? "PASSED" : "FAILED", hello, replied, exact);
    return passed ? 0 : 1;
}
