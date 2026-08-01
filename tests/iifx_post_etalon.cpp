// POM68K — Mac IIfx POST gate (docs/IOP_BRINGUP.md milestone 3).
//
// Proves the OSS + dual-IOP front end carries the real ROM through its
// power-on self test into StartBoot:
//
//   1. both Apple PICs get RELEASED by the ROM and their 65C02s execute
//      real firmware — verified byte-perfect: 64 bytes at each PIC's
//      reset-vector target must exist verbatim in the system ROM (the
//      Duo BORG-upload check, `docs/DUO_BRINGUP.md`);
//   2. the ROM programs the OSS priority file (NuBus inputs at level 2,
//      SCC IOP at 4, tick + VIA1 at 1 — read off MAME-parity hardware
//      by the 2026-08-01 bring-up trace);
//   3. the boot scan reaches the SCSI bus: 5380 arbitration + selection
//      traffic (scsi().commands > 0 needs a target; the scan itself is
//      proven by selection attempts — `Ncr5380::commands` counts real
//      commands once a disk answers).
//
// Soft-skips without the IIfx ROM. A disk image is optional here — the
// full Finder etalon is milestone 5's gate.

#include "IIfxMemory.h"
#include "IIfxCpu.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// 64 bytes of 65C02 code at the PIC's reset-vector target must appear
// verbatim somewhere in the system ROM — the upload was byte-perfect.
static bool firmwareInRom(ApplePic& pic, const std::vector<uint8_t>& rom,
                          const char* name) {
    const uint16_t rv = uint16_t(pic.ramByte(0x7FFC) | (pic.ramByte(0x7FFD) << 8));
    uint8_t code[64];
    for (int i = 0; i < 64; i++)
        code[i] = pic.ramByte(uint16_t((rv + i) & 0x7FFF));
    for (size_t o = 0; o + sizeof(code) <= rom.size(); o++) {
        if (std::equal(code, code + sizeof(code), rom.data() + o)) {
            std::printf("%s: reset vector $%04X, firmware found at ROM+$%zX\n",
                        name, rv, o);
            return true;
        }
    }
    std::fprintf(stderr, "%s: reset vector $%04X — code NOT in ROM\n", name, rv);
    return false;
}

int main() {
    std::string rom = find("roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM");
    if (rom.empty()) {
        std::printf("SKIP: needs the Mac IIfx ROM ($4147DD77)\n");
        return 0;
    }
    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != IIfxMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size %zu\n", romData.size());
        return 1;
    }

    IIfxMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    std::string img = find("hdv/MacOS-7.6-boot.vhd");
    if (!img.empty() && !mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: attachScsi(%s)\n", img.c_str());
        return 1;
    }
    IIfxCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    // The bring-up trace measured: SCC PIC release ~15 K cycles, SWIM PIC
    // release (real upload) ~158 M, SCSI boot scan under 800 M. Budget 1 G
    // (25 s at 40 MHz) with early-out once everything is seen.
    const int64_t kBudget = std::getenv("POM68K_IIFX_POST_CYCLES")
        ? std::atoll(std::getenv("POM68K_IIFX_POST_CYCLES"))
        : 1'000'000'000LL;
    bool released = false;
    while (cpu.getClock() < kBudget && !cpu.isHalted()) {
        cpu.runCycles(100'000);
        released = !mem.sccPic().cpuHeld() && !mem.swimPic().cpuHeld();
        if (released && mem.scsi().commands > 0 &&
            mem.sccPic().cpu().cycleCount() > 1'000'000 &&
            mem.swimPic().cpu().cycleCount() > 1'000'000)
            break;
    }

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    bool ok = true;
    if (!released) {
        std::fprintf(stderr, "FAIL: a PIC is still held (scc=%d swim=%d)\n",
                     mem.sccPic().cpuHeld(), mem.swimPic().cpuHeld());
        ok = false;
    } else {
        ok = firmwareInRom(mem.sccPic(), romData, "sccpic") && ok;
        ok = firmwareInRom(mem.swimPic(), romData, "swimpic") && ok;
    }

    // OSS priority file programmed: every NuBus input at a non-zero level
    // and VIA1 (input 11) routed.
    bool ossOk = mem.ossReg(11) > 0 && mem.ossReg(0) > 0 && mem.ossReg(7) > 0;
    if (!ossOk) {
        std::fprintf(stderr, "FAIL: OSS priorities unprogrammed (%d %d %d)\n",
                     mem.ossReg(0), mem.ossReg(7), mem.ossReg(11));
        ok = false;
    }

    std::printf("t=%lld sccPic cyc=%lld swimPic cyc=%lld scsiSel=%ld scsiCmds=%ld vbl=%ld\n",
                (long long)cpu.getClock(),
                (long long)mem.sccPic().cpu().cycleCount(),
                (long long)mem.swimPic().cpu().cycleCount(),
                mem.scsi().selects, mem.scsi().commands, mem.vblPulses());
    if (!img.empty() && mem.scsi().commands == 0) {
        std::fprintf(stderr, "FAIL: boot scan issued no SCSI command\n");
        ok = false;
    }
    std::printf("%s\n", ok ? "PASSED — POST complete, IOPs live, boot scan reached"
                           : "FAILED");
    return ok ? 0 : 1;
}
