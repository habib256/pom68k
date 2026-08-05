// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate) — the LC II sibling of tests/sony_trace.cpp, built
// for the TODO §1 hunt "no 800K GCR mount on a boosted 030": trace what the
// ROM's .Sony driver GIVES UP ON, instead of stacking black-box counters.
//
// Boots the LC II to the Finder, resolves the .Sony driver through the unit
// table (UTableBase $11C, unit 4 = refnum -5), inserts an 800K GCR image,
// then single-steps the 030 and logs:
//   - every driver Prime/Control call: param block (offset, count, csCode),
//     the caller, and the ioResult it completes with — the driver's own
//     verdict, error code by error code;
//   - the IWM data-register poll sites (PC histogram, hits vs polls);
//   - on the FIRST failing Prime (and on each NEW error code): the last N
//     instructions before the driver decided, with registers — the
//     sony_trace microscope, aimed at the give-up point;
//   - _MountVol and the diskEvt _PostEvent at trap level, so the error the
//     SYSTEM acts on is tied to the driver call that produced it.
//
// POM68K_CACHE_BOOST drives the reproducer: boost 1-2 mounts, 3-4 refuses
// (CHANGELOG 2026-08-05 seventh). Run once at each side of the cliff and
// diff the Prime/error journal — the divergence IS the mechanism.
//
// Usage: lcii_sony_trace [--frames N] [--ring N] [--trace N] [--img path]
//   --frames N  step budget after the insert, in 60.15 Hz frames (dflt 1500)
//   --ring N    instructions kept before a failing Prime (default 240)
//   --trace N   after the first failure, live-print N instructions from the
//               start of the next Prime (the retry, seen live; default 0)
//   --img path  floppy image (default disks35/Disk605.dsk, the etalon's)

#include "V8Memory.h"
#include "V8Video.h"
#include "SonyDrive.h"
#include "Cpu030.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

// Everything on this machine is 24-bit; PCs and driver pointers compare
// masked so a high-byte tag can never split one address into two.
uint32_t m24(uint32_t a) { return a & 0xFFFFFF; }

struct TraceCpu : Cpu030 {
    using Cpu030::Cpu030;
    // A-trap tap: every OS/Toolbox trap raises vector 10; the opcode at
    // PC0 says which. Decoded in the hook, consumed by the main loop.
    std::function<void(uint32_t pc0, uint16_t op)> onATrap;
    void willExecute(moira::M68kException, moira::u16 vector) override {
        if (vector == 10 && onATrap) onATrap(getPC0(), 0);
    }
};

const char* csName(int code) {
    switch (code) {
        case 1:  return "killIO";
        case 5:  return "verify";
        case 6:  return "format";
        case 7:  return "eject";
        case 8:  return "setTagBuffer";
        case 9:  return "trackCache";
        case 21: return "icon";
        case 22: return "mediaIcon";
        case 23: return "driveInfo";
        default: return "?";
    }
}

const char* errName(int e) {
    switch (e) {
        case 0:   return "noErr";
        case -36: return "ioErr";
        case -50: return "paramErr";
        case -53: return "volOffLinErr";
        case -56: return "nsDrvErr";
        case -57: return "noMacDskErr";
        case -60: return "badMDBErr";
        case -64: return "noDriveErr";
        case -65: return "offLinErr";
        case -66: return "noNybErr";
        case -67: return "noAdrMkErr";
        case -68: return "dataVerErr";
        case -69: return "badCksmErr";
        case -70: return "badBtSlpErr";
        case -71: return "noDtaMkErr";
        case -72: return "badDCksum";
        case -73: return "badDBtSlp";
        case -74: return "wrUnderrun";
        case -75: return "cantStepErr";
        case -76: return "tk0BadErr";
        case -77: return "initIWMErr";
        case -78: return "twoSideErr";
        case -79: return "spdAdjErr";
        case -80: return "seekErr";
        case -81: return "sectNFErr";
        default:  return "?";
    }
}

} // namespace

int main(int argc, char** argv) {
    long frames = 1500, ringN = 600, traceN = 0;
    std::string imgArg;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--ring") && i + 1 < argc) ringN = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) traceN = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--img") && i + 1 < argc) imgArg = argv[++i];
    }

    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string boot = find("hdv/lcii-boot.vhd");
    if (boot.empty()) boot = find("hdv/boot.vhd");
    if (boot.empty()) boot = find("hdv/System 7.5 HD.dsk");
    std::string floppySrc = imgArg.empty() ? find("disks35/Disk605.dsk") : find(imgArg.c_str());
    if (rom.empty() || boot.empty() || floppySrc.empty()) {
        std::printf("SKIP: needs the LC II ROM, a bootable hdv/ image and an 800K image\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    V8Memory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    TraceCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(boot)) { std::fprintf(stderr, "FAIL: bad boot image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    const char* boostEnv = getenv("POM68K_CACHE_BOOST");
    std::printf("lcii_sony_trace: boost=%s, boot=%s, floppy=%s\n",
                boostEnv ? boostEnv : "4 (default)", boot.c_str(), floppySrc.c_str());

    // Same private-copy discipline as lcii_beyond_etalon: the guest may
    // write, the asset must not change; MDB drAtrb bit 8 set = cleanly
    // unmounted, so a read-write mount has a deterministic write to do.
    std::vector<uint8_t> floppyOrig;
    {
        std::ifstream fin(floppySrc, std::ios::binary);
        floppyOrig.assign(std::istreambuf_iterator<char>(fin),
                          std::istreambuf_iterator<char>());
        if (floppyOrig.size() >= 0x40C)
            floppyOrig[0x40A] = uint8_t(floppyOrig[0x40A] | 0x01);
    }
    const char* floppyCopy = "lcii_sony_trace.dsk";
    {
        std::ofstream fout(floppyCopy, std::ios::binary | std::ios::trunc);
        fout.write(reinterpret_cast<const char*>(floppyOrig.data()),
                   std::streamsize(floppyOrig.size()));
    }

    auto peek16 = [&](uint32_t a) -> uint16_t {
        return uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1));
    };
    auto peek32 = [&](uint32_t a) -> uint32_t {
        return uint32_t(peek16(a)) << 16 | peek16(a + 2);
    };
    auto safeDasm = [&](char* out, uint32_t a) -> int {
        try { return cpu.disassemble(out, a); }
        catch (moira::MmuBusError&) { std::snprintf(out, 16, "<bus error>"); return 2; }
    };

    const int64_t kFrame = 640 * 407;        // 60.15 Hz @ 15.6672 MHz
    auto runFrames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };

    while (mem.cpuHeld()) mem.tick(1000);
    runFrames(16000);                        // boot to a settled Finder
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted during boot\n"); return 1; }

    // ---- Resolve the .Sony driver through the unit table ----------------
    // 24-bit machine: handles and master pointers carry Memory Manager tag
    // bits in the high byte — mask EVERY dereference. Sweep the whole table
    // and pick the unit NAMED ".Sony" rather than trusting "unit 4": the
    // sweep is also the debug view when the layout surprises us.
    uint32_t utable = m24(peek32(0x11C));
    int unitCnt = int16_t(peek16(0x1D2));               // UnitNtryCnt
    if (unitCnt <= 0 || unitCnt > 128) unitCnt = 48;
    std::printf("UTableBase=$%06X UnitNtryCnt=%d\n", utable, unitCnt);
    uint32_t drvr = 0, primeEntry = 0, ctlEntry = 0;
    for (int u = 0; u < unitCnt; u++) {
        uint32_t dceH = m24(peek32(utable + uint32_t(u) * 4));
        if (!dceH) continue;
        uint32_t dce = m24(peek32(dceH));
        if (!dce) continue;
        // dCtlDriver read the self-validating way: the flags word turned
        // out to lie on this machine (unit -5 has $4000 set yet holds a
        // plain ROM POINTER) — so try pointer first and fall back to
        // handle if the pointer's Pascal name is not printable.
        uint32_t raw = peek32(dce);
        uint16_t dFlags = peek16(dce + 4);
        int16_t refNum = int16_t(peek16(dce + 24));
        uint32_t dp = m24(raw);
        {
            int n = dp ? mem.peek8(dp + 18) : 0;
            char c0 = n >= 1 ? char(mem.peek8(dp + 19)) : 0;
            if (n < 1 || n > 31 || c0 < 32 || c0 >= 127)
                dp = m24(peek32(m24(raw)));             // handle after all
        }
        auto pname = [&](uint32_t base, char* out) {
            int n = base ? mem.peek8(base + 18) : 0;
            if (n > 31) n = 31;
            for (int i = 0; i < n; i++) {
                char c = char(mem.peek8(base + 19 + uint32_t(i)));
                out[i] = (c >= 32 && c < 127) ? c : '.';
            }
            out[n < 0 ? 0 : n] = 0;
        };
        char nm[32] = {};
        pname(dp, nm);
        std::printf("  unit %2d: dce=$%06X flags=$%04X refNum=%d drvr=$%06X "
                    "'%s'\n", u, dce, dFlags, refNum, dp, nm);
        if (refNum == -5) {                  // raw chain, both readings
            uint32_t raw = peek32(dce);
            char asPtr[32] = {}, asHdl[32] = {};
            pname(m24(raw), asPtr);
            pname(m24(peek32(m24(raw))), asHdl);
            std::printf("    refNum -5 raw dCtlDriver=$%08X  as ptr -> "
                        "$%06X '%s'  as handle -> $%06X '%s'\n", raw,
                        m24(raw), asPtr, m24(peek32(m24(raw))), asHdl);
        }
        if (!drvr && std::strncmp(nm, ".Sony", 5) == 0) {
            drvr = dp;
            primeEntry = m24(drvr + peek16(drvr + 10));
            ctlEntry   = m24(drvr + peek16(drvr + 12));
        }
    }
    if (!drvr) {
        // Fallback that owes the System nothing: the poll sites sit in ROM,
        // so find the ROM's own DRVR header by its Pascal name and journal
        // that entry. (romData is the 512 KB image, logical base $A00000.)
        for (size_t i = 0; i + 24 < romData.size(); i++) {
            if (romData[i + 18] == 5 &&
                !std::memcmp(&romData[i + 19], ".Sony", 5)) {
                uint16_t po = uint16_t(romData[i + 10] << 8 | romData[i + 11]);
                uint16_t co = uint16_t(romData[i + 12] << 8 | romData[i + 13]);
                std::printf("  ROM scan: DRVR '.Sony' header at ROM+$%05zX "
                            "prime=+$%04X ctl=+$%04X\n", i, po, co);
                if (!drvr && po < 0x8000 && co < 0x8000) {
                    drvr = 0xA00000 + uint32_t(i);
                    primeEntry = m24(drvr + po);
                    ctlEntry   = m24(drvr + co);
                }
            }
        }
    }
    if (!drvr) {
        std::printf("WARNING: no .Sony in the unit table — Prime/Control "
                    "journal disabled, trap+IWM capture still runs\n");
        primeEntry = ctlEntry = 0xFFFFFFFF;
    } else {
        std::printf("driver .Sony at $%06X, prime=$%06X ctl=$%06X\n",
                    drvr, primeEntry, ctlEntry);
    }

    // ---- Insert and step -------------------------------------------------
    SonyDrive& drv = mem.internalDrive();
    drv.setWriteBack(true);                  // the reproducer's environment
    std::vector<uint32_t> beforeIns;
    { V8Video v(mem); v.decode(beforeIns); }
    if (!mem.insertDisk(floppyCopy)) {
        std::fprintf(stderr, "FAIL: could not insert %s\n", floppyCopy);
        return 1;
    }
    std::printf("inserted at machine clock %lld\n\n", (long long)cpu.machineClock());

    // Per-call journal state. The .Sony driver is synchronous: one call in
    // flight at a time, completion = its ioResult leaving ioInProgress (>0).
    struct Ring { uint32_t pc; uint32_t d0, d1, d2, a0, a1; long nib; };
    std::vector<Ring> ring{size_t(ringN)};
    size_t ringIdx = 0;
    bool callActive = false, callIsCtl = false;
    uint32_t callPb = 0, callFrom = 0;
    int64_t callClock = 0;
    long callNib = 0, callPolls = 0, callHits = 0;
    int callTrack = 0;
    long primeNo = 0, ctlNo = 0, failures = 0;
    std::map<int, long> resultHist;           // error code -> count
    std::map<int, long> dumpedErr;            // codes already ring-dumped
    std::map<uint32_t, std::pair<long, long>> pollSites; // pc -> polls,hits
    // The starvation microscope: WHERE was the CPU when the drive replaced
    // a nibble nobody had read? The top of this histogram is the code that
    // starves the driver — the whole point of the hunt.
    std::map<uint32_t, long> lossSites;                  // pc -> nibbles lost
    // Duplicate deliveries: a poll landing back inside the IWM's 14-tick
    // post-read hold re-reads the SAME latched nibble MSB-set (MAME
    // iwm.cpp:284 re-arms the window on every access — silicon holds too).
    // Every entry here is a nibble duplicated into the driver's stream.
    std::map<uint32_t, long> dupSites;                   // pc -> re-reads
    long callLost = 0;                                   // per-call overwrite
    uint32_t callBuf = 0, callOff = 0, callReq = 0;      // for the truth diff
    long callMidFieldLost = 0;                           // lost INSIDE the
                                                         // data-field loops
    long liveTrace = 0;                       // instructions left to print
    bool armLiveTrace = false;                // arm at next Prime entry

    const Iwm& iwm = mem.iwm();

    // Trap tap: _MountVol ($A00F) result via return-address watch, and the
    // diskEvt _PostEvent (A0=7, D0=message). Decoded here, resolved below.
    uint32_t mountRet = 0, mountPb = 0;
    int mountErr = 1;                         // 1 = not seen yet
    int64_t mountClock = -1;
    cpu.onATrap = [&](uint32_t pc0, uint16_t) {
        uint16_t op = peek16(pc0);
        if ((op & 0xF0FF) == 0xA00F) {        // _MountVol (+async/immed bits)
            mountRet = m24(pc0 + 2);
            mountPb = m24(cpu.getA(0));
            std::printf("[%10lld] _MountVol pb=$%06X ioVRefNum=%d caller=$%06X\n",
                        (long long)cpu.machineClock(), mountPb,
                        int16_t(peek16(mountPb + 22)), m24(pc0));
        } else if (op == 0xA02F && uint16_t(cpu.getA(0)) == 7) {
            std::printf("[%10lld] _PostEvent diskEvt msg=$%08X (drive %d, "
                        "hi-word err %d %s)\n",
                        (long long)cpu.machineClock(), cpu.getD(0),
                        int16_t(cpu.getD(0)), int16_t(cpu.getD(0) >> 16),
                        errName(int16_t(cpu.getD(0) >> 16)));
        }
    };

    auto dumpRing = [&](const char* why) {
        std::printf("  --- last %ld instructions before %s:\n", ringN, why);
        char da[96];
        for (size_t i = 0; i < ring.size(); i++) {
            const Ring& r = ring[(ringIdx + i) % ring.size()];
            if (!r.pc) continue;
            safeDasm(da, r.pc);
            std::printf("  $%06X  %-44s D0=%08X D1=%08X D2=%08X A0=%08X "
                        "A1=%08X nib=%ld\n", r.pc, da, r.d0, r.d1, r.d2,
                        r.a0, r.a1, r.nib);
        }
    };

    auto closeCall = [&](int result) {
        resultHist[result]++;
        std::printf("[%10lld] %s #%ld -> %d %s  (%lld machine cycles, "
                    "nib +%ld, polls +%ld, hits +%ld, lost +%ld, "
                    "track %d -> %d)\n",
                    (long long)cpu.machineClock(), callIsCtl ? "CTL" : "PRIME",
                    callIsCtl ? ctlNo : primeNo, result, errName(result),
                    (long long)(cpu.machineClock() - callClock),
                    drv.nibblesRead - callNib, iwm.dataReads - callPolls,
                    iwm.dataHits - callHits, iwm.overwritten - callLost,
                    callTrack, drv.currentTrack());
        if (!callIsCtl)
            std::printf("             mid-data-field losses: %ld\n",
                        callMidFieldLost);
        if (result < 0) {
            failures++;
            // What actually landed in the caller's buffer vs the image's
            // ground truth: a 2-byte SHIFT from some offset = a dropped
            // nibble group, one wrong byte = corruption, garbage = lost
            // framing. This is the shape of the failure, not just its name.
            if (!callIsCtl && callReq >= 16 &&
                size_t(callOff) + 16 <= floppyOrig.size()) {
                uint32_t n = std::min<uint32_t>(callReq, 512);
                uint32_t firstBad = n;
                for (uint32_t i = 0; i < n; i++)
                    if (mem.peek8(callBuf + i) != floppyOrig[callOff + i]) {
                        firstBad = i;
                        break;
                    }
                if (firstBad == n) {
                    std::printf("             buffer MATCHES the image "
                                "(%u bytes) — checksum-side defect\n", n);
                } else {
                    std::printf("             buffer diverges at +$%X:\n", firstBad);
                    uint32_t w0 = firstBad >= 8 ? firstBad - 8 : 0;
                    std::printf("               image :");
                    for (uint32_t i = w0; i < w0 + 24 && i < n; i++)
                        std::printf(" %02X", floppyOrig[callOff + i]);
                    std::printf("\n               buffer:");
                    for (uint32_t i = w0; i < w0 + 24 && i < n; i++)
                        std::printf(" %02X", mem.peek8(callBuf + i));
                    std::printf("\n");
                }
            }
            if (dumpedErr[result]++ == 0) dumpRing(errName(result));
            if (traceN > 0 && !armLiveTrace && liveTrace == 0) {
                armLiveTrace = true;
                std::printf("  (live trace armed for the next Prime, %ld "
                            "instructions)\n", traceN);
            }
        }
        callActive = false;
    };

    const int64_t stepEnd = cpu.machineClock() + frames * kFrame;
    char da[96];
    while (cpu.machineClock() < stepEnd && !cpu.isHalted()) {
        uint32_t pc = m24(cpu.getPC0());

        if (pc == primeEntry || pc == ctlEntry) {
            if (callActive) closeCall(int16_t(peek16(callPb + 16)));
            callActive = true;
            callIsCtl = (pc == ctlEntry);
            callPb = m24(cpu.getA(0));
            callFrom = m24(peek32(cpu.getA(7)));
            callClock = cpu.machineClock();
            callNib = drv.nibblesRead;
            callPolls = iwm.dataReads;
            callHits = iwm.dataHits;
            callLost = iwm.overwritten;
            callMidFieldLost = 0;
            callTrack = drv.currentTrack();
            callBuf = m24(peek32(callPb + 32));
            callOff = peek32(callPb + 46);
            callReq = peek32(callPb + 36);
            for (Ring& r : ring) r = {};
            ringIdx = 0;
            if (callIsCtl) {
                ctlNo++;
                std::printf("[%10lld] CTL   #%ld csCode=%d (%s) pb=$%06X "
                            "caller=$%06X csParam=%04X %04X\n",
                            (long long)callClock, ctlNo, int16_t(peek16(callPb + 26)),
                            csName(int16_t(peek16(callPb + 26))), callPb, callFrom,
                            peek16(callPb + 28), peek16(callPb + 30));
            } else {
                primeNo++;
                uint16_t ioTrap = peek16(callPb + 6);
                std::printf("[%10lld] PRIME #%ld %s off=$%X (sector %u) "
                            "req=$%X buf=$%06X posMode=%u pb=$%06X caller=$%06X "
                            "track=%d\n",
                            (long long)callClock, primeNo,
                            (ioTrap & 0xFF) == 3 ? "WRITE" : "READ",
                            peek32(callPb + 46), peek32(callPb + 46) / 512,
                            peek32(callPb + 36), m24(peek32(callPb + 32)),
                            peek16(callPb + 44), callPb, callFrom, callTrack);
                if (armLiveTrace) { armLiveTrace = false; liveTrace = traceN; }
            }
        }

        if (callActive) {
            Ring& r = ring[ringIdx++ % ring.size()];
            r = { pc, cpu.getD(0), cpu.getD(1), cpu.getD(2),
                  cpu.getA(0), cpu.getA(1), drv.nibblesRead };
        }
        if (liveTrace > 0) {
            liveTrace--;
            safeDasm(da, pc);
            std::printf("T $%06X  %-44s D0=%08X D1=%08X D2=%08X A0=%08X "
                        "A1=%08X nib=%ld\n", pc, da, cpu.getD(0), cpu.getD(1),
                        cpu.getD(2), cpu.getA(0), cpu.getA(1), drv.nibblesRead);
        }

        long polls0 = iwm.dataReads, hits0 = iwm.dataHits;
        long lost0 = iwm.overwritten, dup0 = iwm.reReads;
        cpu.execute();
        if (iwm.reReads != dup0) dupSites[pc] += iwm.reReads - dup0;
        if (iwm.dataReads != polls0) {
            auto& s = pollSites[pc];
            s.first += iwm.dataReads - polls0;
            s.second += iwm.dataHits - hits0;
        }
        if (iwm.overwritten != lost0) {
            lossSites[pc] += iwm.overwritten - lost0;
            // The data-field read loops of the ROM driver ($A6E31E-$A6E37A
            // on the LC II $35C28F5F ROM): a loss while the PC is HERE is
            // a byte dropped mid-sector — the kind that kills a checksum.
            if (callActive && pc >= 0xA6E240 && pc <= 0xA6E400)
                callMidFieldLost += iwm.overwritten - lost0;
        }

        if (callActive) {
            int16_t res = int16_t(peek16(callPb + 16));
            if (res <= 0) closeCall(res);
        }
        if (mountRet && m24(cpu.getPC0()) == mountRet) {
            mountErr = int16_t(cpu.getD(0));
            mountClock = cpu.machineClock();
            std::printf("[%10lld] _MountVol RETURNED %d %s\n",
                        (long long)mountClock, mountErr, errName(mountErr));
            mountRet = 0;
        }
        // The story is over shortly after the mount verdict — but leave
        // the dialog (or the desktop icon) time to DRAW, the screen judge
        // below reads it.
        if (mountClock >= 0 && cpu.machineClock() > mountClock + 120 * kFrame)
            break;
        if (!drv.hasDisk()) { std::printf("(disk ejected)\n"); break; }
    }
    if (callActive) closeCall(int16_t(peek16(callPb + 16)));

    // ---- Summary --------------------------------------------------------
    std::printf("\n== summary: %ld primes, %ld controls, %ld failures\n",
                primeNo, ctlNo, failures);
    for (auto& [code, n] : resultHist)
        std::printf("   result %6d %-12s x%ld\n", code, errName(code), n);
    std::printf("== MountVol: %s (%d %s)\n",
                mountErr == 1 ? "never returned" : "returned",
                mountErr, mountErr == 1 ? "" : errName(mountErr));
    std::printf("== IWM: polls %ld hits %ld (%.1f%%) overwritten %ld reReads %ld\n",
                iwm.dataReads, iwm.dataHits,
                iwm.dataReads ? 100.0 * double(iwm.dataHits) / double(iwm.dataReads) : 0.0,
                iwm.overwritten, iwm.reReads);
    std::printf("== drive: track %d, motor %s, CSTIN=%d, nibbles %ld, "
                "personality %s\n", drv.currentTrack(),
                drv.motorOn() ? "on" : "off", drv.sense(0x1), drv.nibblesRead,
                mem.swim().ism() ? "ISM" : "IWM");
    {
        std::vector<std::pair<uint32_t, std::pair<long, long>>> top(
            pollSites.begin(), pollSites.end());
        std::sort(top.begin(), top.end(), [](auto& a, auto& b) {
            return a.second.first > b.second.first;
        });
        std::printf("== top IWM poll sites (pc: polls, hits):\n");
        for (size_t i = 0; i < top.size() && i < 8; i++) {
            safeDasm(da, top[i].first);
            std::printf("   $%06X  %-40s %ld polls, %ld hits\n",
                        top[i].first, da, top[i].second.first,
                        top[i].second.second);
        }
    }
    {
        std::vector<std::pair<uint32_t, long>> top(dupSites.begin(),
                                                   dupSites.end());
        std::sort(top.begin(), top.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        long total = 0;
        for (auto& [pc2, n] : top) total += n;
        std::printf("== DUPLICATE nibbles (re-read inside the 14-tick hold; "
                    "%ld total):\n", total);
        for (size_t i = 0; i < top.size() && i < 8; i++) {
            safeDasm(da, top[i].first);
            std::printf("   $%06X  %-40s %ld duplicates\n",
                        top[i].first, da, top[i].second);
        }
    }
    {
        std::vector<std::pair<uint32_t, long>> top(lossSites.begin(),
                                                   lossSites.end());
        std::sort(top.begin(), top.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        long total = 0;
        for (auto& [pc2, n] : top) total += n;
        std::printf("== WHERE nibbles were lost (pc the CPU was at, %ld "
                    "total, %zu sites):\n", total, top.size());
        for (size_t i = 0; i < top.size() && i < 16; i++) {
            safeDasm(da, top[i].first);
            std::printf("   $%06X  %-40s %ld lost (%.1f%%)\n",
                        top[i].first, da, top[i].second,
                        total ? 100.0 * double(top[i].second) / double(total) : 0.0);
        }
    }
    // The etalon's screen judge: icon strip = mount, centre white = dialog.
    {
        std::vector<uint32_t> after;
        V8Video v(mem);
        v.decode(after);
        long stripDelta = 0, centreDark = 0;
        for (int y = 50; y < 115; y++)
            for (int x = 445; x < 510; x++)
                if (beforeIns[size_t(y) * 512 + x] != after[size_t(y) * 512 + x])
                    stripDelta++;
        for (int y = 90; y < 200; y++)
            for (int x = 120; x < 380; x++)
                if ((after[size_t(y) * 512 + x] & 0xFF) < 0x80) centreDark++;
        double centreWhite = 1.0 - double(centreDark) / (260.0 * 110.0);
        std::printf("== screen: icon strip delta %ld px, centre white %.2f -> %s\n",
                    stripDelta, centreWhite,
                    stripDelta >= 50 ? "MOUNTED"
                    : centreWhite > 0.80 ? "init dialog" : "no verdict");
        if (getenv("POM68K_DUMP")) {
            FILE* fp = fopen("lcii_sony_trace.ppm", "wb");
            std::fprintf(fp, "P6\n512 384\n255\n");
            for (uint32_t p : after) {
                uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
                fwrite(rgb, 1, 3, fp);
            }
            fclose(fp);
        }
    }
    std::remove(floppyCopy);
    return 0;
}
