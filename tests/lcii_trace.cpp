// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate) — trace the real LC II ROM ($35C28F5F) booting
// on the O6 machine skeleton: hot PCs, overlay/config transitions,
// device-space access counters, bus errors taken, first Egret handshake
// attempt. The LC II sibling of tests/boot_trace.cpp.
//
// Usage: lcii_trace [rom_path] [--cycles N] [--pc-log N] [--scsi image]
//                   [--hot-from N] [--ring-from N]
// --hot-from  : hot-PC histogram counts only clocks ≥ N (tail analysis)
// --ring-from : arm the instruction ring at clock N; dumped with
//               registers + MMU table walk when a bus error fires

#include "V8Memory.h"
#include "V8Video.h"
#include "Cpu030.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

const char* kDefaultRom = "docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM";

std::vector<uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

struct TraceCpu : Cpu030 {
    using Cpu030::Cpu030;
    std::map<uint32_t, long> pcHits;
    long berrs = 0;
    uint32_t lastBerrPc = 0;

    uint32_t mmuFaultAddrDbg() const { return mmuFaultAddr; }
    std::function<void()> onBerr;

    long irqLog = 0;
    long vecHist[64] = {};                      // exception vector histogram
    long intHist[8] = {};                       // interrupt level histogram
    long intLog = 0;
    void willInterrupt(moira::u8 level) override {
        if (level < 8) intHist[level]++;
        if (level == 4 && intLog++ < 20)
            std::printf("[%10lld] SCC IRQ (level 4) taken at PC=$%08X\n",
                        (long long)getClock(), getPC0());
    }
    std::function<void(int)> onOddException;   // vectors 3/4/10/11…
    void willExecute(moira::M68kException exc, moira::u16 vector) override {
        if (vector < 64) vecHist[vector]++;
        if (vector >= 24 && vector <= 31 && irqLog++ < 30) {
            std::printf("[%10lld] IRQ vector %d taken at PC=$%08X SR=%04X\n",
                        (long long)getClock(), vector, getPC0(), getSR());
        }
        // Line-A (10) is the Toolbox trap dispatcher — normal traffic
        if ((vector == 3 || vector == 4 || vector == 11)
            && onOddException) onOddException(vector);
        if (vector == 2) {
            berrs++;
            lastBerrPc = getPC0();
            std::printf("[%10lld] BERR #%ld at PC=$%08X fault=$%08X acc=$%08X %s "
                        "TC=%08X CRP=%016llX SSW-flags=%04X\n",
                        (long long)getClock(), berrs, getPC0(), mmuFaultAddr,
                        mmuAccAddr, mmuAccWrite ? "write" : "read", getTC(),
                        (unsigned long long)getCRP(), mmuSsw);
            std::printf("             TT0=%08X TT1=%08X opcode=%08X pc=%08X\n",
                        getTT0(), getTT1(), mmuOpcodeV, getPC());
            if (onBerr) onBerr();
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    std::string romPath = kDefaultRom;
    long long cycles = 50'000'000;           // ~3.2 s of machine time
    int pcTop = 25;
    std::string scsiPath;
    long long hotFrom = 0;                   // hot-PC histogram start
    long long ringFrom = -1;                 // instruction-ring arm point
    uint32_t probePc = 0;                    // dump registers at this pc0
    uint32_t dasmFrom = 0, dasmTo = 0;       // disassemble range at end
    uint32_t traceAtPc = 0;                  // --trace-at: full-detail trace
    int traceCount = 0;                      // of N instructions from a pc
                                             // (armed only past --hot-from)
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--cycles") && i + 1 < argc) cycles = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--pc-log") && i + 1 < argc) pcTop = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scsi") && i + 1 < argc) scsiPath = argv[++i];
        else if (!std::strcmp(argv[i], "--hot-from") && i + 1 < argc) hotFrom = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--ring-from") && i + 1 < argc) ringFrom = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--probe") && i + 1 < argc)
            probePc = uint32_t(std::strtoul(argv[++i], nullptr, 16));
        else if (!std::strcmp(argv[i], "--dasm") && i + 2 < argc) {
            dasmFrom = uint32_t(std::strtoul(argv[++i], nullptr, 16));
            dasmTo = uint32_t(std::strtoul(argv[++i], nullptr, 16));
        }
        else if (!std::strcmp(argv[i], "--trace-at") && i + 2 < argc) {
            traceAtPc = uint32_t(std::strtoul(argv[++i], nullptr, 16));
            traceCount = std::atoi(argv[++i]);
        }
        else romPath = argv[i];
    }

    auto rom = slurp(romPath);
    if (rom.size() != V8Memory::kRomSize) {
        std::printf("lcii_trace: no 512 KB ROM at '%s' — nothing to do\n", romPath.c_str());
        return 0;
    }

    V8Memory mem;                            // 10 MB
    mem.loadRom(rom);
    if (!scsiPath.empty())
        std::printf("SCSI disk: %s %s\n", scsiPath.c_str(),
                    mem.attachScsi(scsiPath) ? "attached" : "FAILED");
    // Persisted PRAM skips the cold-boot full-RAM burn-in
    if (mem.egret().loadPram("lcii_trace.pram"))
        std::printf("PRAM: lcii_trace.pram loaded\n");
    // Same baseline as the GUI: Basilisk II factory XPRAM defaults when
    // no valid battery file ('NuMc' signature, LocalTalk off, DynWait)
    mem.egret().factoryDefaults();
    TraceCpu cpu(mem, getenv("LCII_FPU") != nullptr);
    mem.setCpu(&cpu);
    cpu.hardReset();

    // O6.11: log XPRAM reads (esp. the $E0-$E3 network-config region)
    // to see what .MPP consults for its LocalTalk decision. Opt-in.
    long xpramLog = 0;
    if (getenv("WATCH_XPRAM"))
        mem.egret().onXPramRead = [&](int addr, int count) {
            if (xpramLog++ > 400) return;
            std::printf("[%10lld] XPRAM read off=$%02X count=%d  [",
                        (long long)cpu.getClock(), addr, count);
            for (int i = 0; i < count && i < 16; i++)
                std::printf("%02X ", mem.egret().pram((addr + i) & 0xFF));
            std::printf("] PC=%08X\n", cpu.getPC0());
        };

    long egretBytes = 0;
    mem.egret().onByte = [&](bool toEgret, uint8_t b) {
        // host→Egret bytes always (commands are the interesting part);
        // Egret→host only the first ones (block replies flood the log)
        if (toEgret || egretBytes++ < 60)
            std::printf("[%10lld] egret %s $%02X\n", (long long)cpu.getClock(),
                        toEgret ? "<-" : "->", b);
    };
    // Per-edge Egret handshake logging is available via onEdge when a
    // protocol question needs it (see the O6.3 debugging sessions).

    // Register-level SCSI trace in a clock window (transfer post-mortem):
    // opt-in with SCSI_REGS="<from> <to>" (clocks), quiet by default.
    long long scsiFrom = 0, scsiTo = 0;
    if (const char* w = getenv("SCSI_REGS")) sscanf(w, "%lld %lld", &scsiFrom, &scsiTo);

    // SCC access log in a clock window (SCC_LOG="<from> <to>"): every
    // $F04000-$F05FFF touch with the decoded channel/reg (O6.10 debug)
    long long sccFrom = 0, sccTo = 0;
    if (const char* w = getenv("SCC_LOG")) sscanf(w, "%lld %lld", &sccFrom, &sccTo);
    long sccLog = 0;
    int sccPtr[2] = {0, 0};                  // track WR0 pointer per channel
    mem.onSccAccess = [&](uint32_t a, bool wr, uint8_t v) {
        long long c = cpu.getClock();
        int ch = (a >> 1) & 1;
        bool data = (a >> 2) & 1;
        // Decode which WR a control write targets, to spot int programming
        int reg = -1;
        if (wr && !data) {
            if (sccPtr[ch] == 0) { reg = 0; sccPtr[ch] = (v & 7) | ((v & 0x38) == 8 ? 8 : 0); }
            else { reg = sccPtr[ch]; sccPtr[ch] = 0; }
        }
        if (c < sccFrom || c > sccTo || !wr || sccLog++ > 400) return;
        if (reg >= 0)
            std::printf("[%10lld] SCC ch%d WR%d <- $%02X PC=%08X\n",
                        c, ch, reg, v, cpu.getPC0());
        else
            std::printf("[%10lld] SCC ch%d data <- $%02X PC=%08X\n",
                        c, ch, v, cpu.getPC0());
    };
    long regLog = 0;
    mem.scsi().onAccess = [&](int reg, bool w, uint8_t v) {
        long long c = cpu.getClock();
        if (c < scsiFrom || c > scsiTo || regLog++ > 400) return;
        static const char* rn[8] = {"DATA","ICR","MODE","TCR","CSR","BSR","IDR","RPI"};
        std::printf("[%10lld] 5380 %s %s%s%02X PC=%08X\n", c,
                    rn[reg & 7], w ? "<- " : "-> ", w ? "" : "$", v, cpu.getPC0());
    };

    long cdbLog = 0;
    static long lastDma = 0;
    mem.scsi().onCommand = [&](const std::vector<uint8_t>& cdb) {
        if (cdbLog++ >= 60) return;
        std::printf("[%10lld] SCSI CDB:", (long long)cpu.getClock());
        for (uint8_t b : cdb) std::printf(" %02X", b);
        std::printf("  (data since prev: %ld)\n", mem.scsi().dmaBytes - lastDma);
        lastDma = mem.scsi().dmaBytes;
    };

    // Egret holds the 68030 at power-on (v8.cpp:204-205)
    while (mem.cpuHeld()) mem.tick(1000);

    std::printf("lcii_trace: ROM %s, PC0=$%08X\n", romPath.c_str(), cpu.getPC());

    bool overlayWas = mem.overlay();
    uint8_t configWas = mem.ramConfig();
    long steps = 0;

    // The disassembler reads through the LIVE bus — an operand fetch
    // from unmapped I/O raises the machine's bus error. Contain it.
    auto safeDasm = [&](char* out, uint32_t a) -> int {
        try {
            return cpu.disassemble(out, a);
        } catch (moira::MmuBusError&) {
            std::snprintf(out, 16, "<bus error>");
            return 2;
        }
    };

    // Manual MMU table walk for the first unexpected fault (debug aid)
    auto peek32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    auto walkDump = [&](uint32_t logical) {
        uint64_t crp = cpu.getCRP();
        uint32_t tc = cpu.getTC();
        uint32_t table = uint32_t(crp) & 0xFFFFFFF0;
        int shifts[4] = { int(tc >> 12 & 0xF), int(tc >> 8 & 0xF),
                          int(tc >> 4 & 0xF), int(tc & 0xF) };
        int bit = 32 - int(tc >> 16 & 0xF);      // after IS
        int dt = int(crp >> 32) & 3;             // DT in the limit long
        std::printf("  walk $%08X: TC=%08X CRP=%016llX\n", logical, tc,
                    (unsigned long long)crp);
        for (int lvl = 0; lvl < 4 && shifts[lvl]; lvl++) {
            int idx = (logical >> (bit - shifts[lvl])) & ((1 << shifts[lvl]) - 1);
            bit -= shifts[lvl];
            uint32_t entAddr = table + uint32_t(idx) * (dt == 3 ? 8 : 4);
            uint32_t d0 = peek32(entAddr);
            uint32_t d1 = dt == 3 ? peek32(entAddr + 4) : 0;
            std::printf("  L%d idx=%d ent@$%08X = %08X %08X\n", lvl, idx,
                        entAddr, d0, d1);
            int ndt = int(d0 & 3);
            if (ndt < 2) break;                  // invalid / page
            table = (dt == 3 ? d1 : d0) & 0xFFFFFFF0;
            dt = ndt;
        }
    };
    auto dumpBTable = [&](const char* tag, uint32_t aIdx) {
        uint64_t crp = cpu.getCRP();
        uint32_t root = uint32_t(crp) & 0xFFFFFFF0;
        uint32_t d0 = peek32(root + aIdx * 8), d1 = peek32(root + aIdx * 8 + 4);
        std::printf("  A[%u]%s = %08X %08X — B table:\n", aIdx, tag, d0, d1);
        uint32_t bt = d1 & 0xFFFFFFF0;
        for (int j = 0; j < 16; j++)
            std::printf("    B[%2d]@$%08X = %08X\n", j, bt + j * 4, peek32(bt + j * 4));
    };
    bool walked = false;
    // Instruction ring: dumped when the unexpected fault hits
    std::vector<std::string> iring(220);
    size_t iringIdx = 0;
    cpu.onOddException = [&](int vec) {
        if (walked || ringFrom < 0 || cpu.getClock() <= ringFrom) return;
        walked = true;
        uint32_t vbr = cpu.getVBR();
        uint32_t target = peek32(vbr + uint32_t(vec) * 4);
        std::printf("[%10lld] first odd exception: vector %d at PC=$%08X "
                    "VBR=%08X handler=%08X\n",
                    (long long)cpu.getClock(), vec, cpu.getPC0(), vbr, target);
        char da[96];
        uint32_t a = target;
        for (int i = 0; i < 10; i++) {
            int n = safeDasm(da, a);
            std::printf("    $%08X  %s\n", a, da);
            a += uint32_t(n);
        }
        std::printf("  --- ring before it:\n");
        for (size_t i = 0; i < iring.size(); i++) {
            auto& s = iring[(iringIdx + i) % iring.size()];
            if (!s.empty()) std::printf("  %s\n", s.c_str());
        }
    };
    cpu.onBerr = [&] {
        if (!walked && (cpu.getTC() & 0x80000000)) {
            walked = true;
            std::printf("  MMU block @$9FFFAA: TC=%08X TT0=%08X TT1=%08X CRP=%08X %08X\n",
                        peek32(0x9FFFAA), peek32(0x9FFFAE), peek32(0x9FFFB2),
                        peek32(0x9FFFA2), peek32(0x9FFFA6));
            walkDump(cpu.mmuFaultAddrDbg());
            {
                uint32_t root = uint32_t(cpu.getCRP()) & 0xFFFFFFF0;
                std::printf("  root table (32 long entries) @$%08X:\n", root);
                for (int i = 0; i < 32; i++)
                    std::printf("    A[%2d] = %08X %08X\n", i,
                                peek32(root + i * 8), peek32(root + i * 8 + 4));
            }
            dumpBTable(" (bank $81)", 16);
            std::printf("  --- last instructions:\n");
            for (size_t i = 0; i < iring.size(); i++) {
                auto& s = iring[(iringIdx + i) % iring.size()];
                if (!s.empty()) std::printf("  %s\n", s.c_str());
            }
        }
    };

    // Jump-trajectory ring: how did we get to the POST debug console?
    // TRAJ_AT=<hex pc> retargets the dump (O6.11: the .MPP/LAP open
    // chain — who decided to bring LocalTalk up), default $A498F8.
    std::vector<uint32_t> traj(256, 0);
    size_t trajIdx = 0;
    uint32_t lastPc = 0;
    bool trajDumped = false;
    uint32_t trajAt = 0xA498F8;
    if (const char* t = getenv("TRAJ_AT"))
        trajAt = uint32_t(std::strtoul(t, nullptr, 16));

    while (cpu.getClock() < cycles && !cpu.isHalted()) {
        uint32_t pc = cpu.getPC0();
        static int postFault = 0;
        if (ringFrom >= 0 && cpu.getClock() > ringFrom && (!walked || postFault < 40)) {
            if (walked) postFault++;
            char line[160], da[96];
            safeDasm(da, pc);
            std::snprintf(line, sizeof line,
                          "$%08X  %-44s A2=%08X A4=%08X A6=%08X A7=%08X",
                          pc, da, cpu.getA(2), cpu.getA(4), cpu.getA(6), cpu.getA(7));
            iring[iringIdx++ % iring.size()] = line;
            if (walked && postFault == 40) {
                std::printf("  --- instructions around the fault:\n");
                for (size_t i = 0; i < iring.size(); i++) {
                    auto& s = iring[(iringIdx + i) % iring.size()];
                    if (!s.empty()) std::printf("  %s\n", s.c_str());
                }
            }
            // Wild-jump trap: first execution at the null page
            if (!walked && (pc & 0xFFFFFF) < 0x8) {
                walked = true;
                std::printf("  OS trap table around $A02D: [$4B0]=%08X [$4B4]=%08X "
                            "[$4B8]=%08X  [$400]=%08X [$404]=%08X\n",
                            peek32(0x4B0), peek32(0x4B4), peek32(0x4B8),
                            peek32(0x400), peek32(0x404));
                std::printf("  --- first execution at $%08X, ring:\n", pc);
                for (size_t i = 0; i < iring.size(); i++) {
                    auto& s = iring[(iringIdx + i) % iring.size()];
                    if (!s.empty()) std::printf("  %s\n", s.c_str());
                }
            }
        }
        cpu.execute();
        steps++;
        if (cpu.getClock() >= hotFrom) cpu.pcHits[pc]++;

        // O6.11: log every _ReadXPRam/_WriteXPRam A-trap with its caller
        // PC + D0 (count<<16|offset) — the SysParam copy path (opt-in).
        if (getenv("WATCH_XPTRAP")) {
            uint16_t op = uint16_t(mem.peek8(pc) << 8 | mem.peek8(pc + 1));
            static long xt = 0;
            if ((op == 0xA051 || op == 0xA052) && xt++ < 60)
                std::printf("[%10lld] %s D0=%08X at PC=$%08X\n",
                            (long long)cpu.getClock(),
                            op == 0xA051 ? "_ReadXPRam" : "_WriteXPRam",
                            cpu.getD(0), pc);
        }

        // O6.11: watch the SysParam AppleTalk bytes ($1F9-$1FB incl.
        // SPConfig) + PortBUse ($291) — who writes them, from what?
        // The AppleTalk-active decision flows through these (opt-in).
        if (getenv("WATCH_SP")) {
            static uint64_t sp = ~0ull; static uint8_t pb = 0xEE;
            static int spLogs = 0;
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) v = v << 8 | mem.peek8(0x1F8 + i);
            uint8_t b = mem.peek8(0x291);
            if ((v != sp || b != pb) && spLogs++ < 40) {
                sp = v; pb = b;
                std::printf("[%10lld] SysParam $1F8=%016llX PortBUse=%02X "
                            "after PC=$%08X\n", (long long)cpu.getClock(),
                            (unsigned long long)v, b, pc);
            }
        }

        // Watch the boot-mode flag byte $1EFC (bit 4 selects the
        // zone-at-zero InitZone path) and the Line-A vector — opt-in
        // (WATCH_BOOT), it was the thread that led to the ReadXPram fix.
        if (getenv("WATCH_BOOT")) {
            static uint32_t vec10 = 0xDEADBEEF;
            static int vec10Logs = 0;
            static int flagByte = -1;
            if (vec10Logs < 24) {
                uint32_t v = peek32(0x28);
                int f = mem.peek8(0x1EFC);
                if (v != vec10 || f != flagByte) {
                    vec10 = v; flagByte = f;
                    vec10Logs++;
                    std::printf("[%10lld] vec10=%08X $1EFC=%02X at PC=$%08X\n",
                                (long long)cpu.getClock(), v, f, pc);
                }
            }
        }

        // Boot-phase milestone: WarmStart = 'WLSC' at $CFC is the ROM's
        // own "low memory is now valid" marker — Basilisk II gates every
        // host->Mac callback on it (HasMacStarted(), macos_util.h:278-281;
        // docs/BASILISK_ROM_NOTES.md §4.3). Brackets a fault: before or
        // after low-mem validity.
        static bool warmSeen = false;
        if (!warmSeen && (steps & 0x3FF) == 0 && peek32(0xCFC) == 0x574C5343) {
            warmSeen = true;
            std::printf("[%10lld] WarmStart 'WLSC' set at $CFC — low memory "
                        "valid (PC=$%08X)\n", (long long)cpu.getClock(), pc);
        }
        // O6.11: track the vCheckLoad chain vector $7F0 (Basilisk's
        // resource-load hook) — find the RUNTIME vCheckLoad the System's
        // RAM Resource Manager installs (the ROM routine at $A1B8F4 is
        // dead in System 7). Report each change of *($7F0).
        if (getenv("WATCH_VCL")) {
            static uint32_t vcl = 0xDEADBEEF;
            static int vclLogs = 0;
            if ((steps & 0xFF) == 0 && vclLogs < 30) {
                uint32_t v = peek32(0x7F0);
                if (v != vcl) {
                    vcl = v; vclLogs++;
                    std::printf("[%10lld] $7F0 (vCheckLoad) = $%08X at PC=$%08X\n",
                                (long long)cpu.getClock(), v, pc);
                }
            }
        }

        // Full-detail execution trace: N instructions from --trace-at pc
        // (2 shots max, armed only past --hot-from — the pc may also run
        // in unrelated earlier phases)
        static int traceLeft = 0, traceShots = 2;
        if (traceAtPc && pc == traceAtPc && traceShots > 0
            && cpu.getClock() >= hotFrom) { traceLeft = traceCount; traceShots--; }
        if (traceLeft > 0) {
            traceLeft--;
            char da[96];
            safeDasm(da, pc);
            std::printf("T $%08X  %-40s D0=%08X D1=%08X D3=%08X A0=%08X "
                        "A1=%08X A2=%08X A7=%08X\n", pc, da, cpu.getD(0),
                        cpu.getD(1), cpu.getD(3), cpu.getA(0), cpu.getA(1),
                        cpu.getA(2), cpu.getA(7));
        }

        // O6.11: log the CALLER of _ReadXPRam ($A0CC64) — the code that
        // consumes an XPRAM byte for its LocalTalk decision. D0 =
        // count<<16 | offset; (a7) = return address = the consumer.
        if (getenv("WATCH_XPCALL") && pc == 0xA0CC64) {
            uint32_t d0 = cpu.getD(0);
            static long xc = 0;
            if (xc++ < 60)
                std::printf("[%10lld] _ReadXPRam off=$%02X cnt=%d caller=$%08X "
                            "(a1=%08X)\n", (long long)cpu.getClock(), d0 & 0xFF,
                            (d0 >> 16) & 0xFF, peek32(cpu.getA(7)), cpu.getA(1));
        }

        // O6.11: find how the 'ltlk' ADEV loads. Log Resource-Manager
        // getter traps whose stack arguments carry a FOURCC of interest
        // (esp. 'ltlk' = $6C746C6B) — reveals the runtime load path so we
        // can place the resource-patch hook. Opt-in (WATCH_RSRC).
        if (getenv("WATCH_RSRC")) {
            uint16_t op = uint16_t(mem.peek8(pc) << 8 | mem.peek8(pc + 1));
            // _GetResource A9A0, _Get1Resource A81F, _GetNamedResource A9A1,
            // _Get1NamedResource A820, _GetIndResource A99D, _Get1IndResource A80E
            bool getter = op == 0xA9A0 || op == 0xA81F || op == 0xA9A1
                       || op == 0xA820 || op == 0xA99D || op == 0xA80E;
            static long rlog = 0;
            static uint32_t retPc = 0;         // print a0 at the getter return
            if (retPc && pc == retPc) {
                std::printf("             -> return a0=%08X d0=%08X\n",
                            cpu.getA(0), cpu.getD(0));
                retPc = 0;
            }
            if (getter && rlog < 120) {
                uint32_t sp = cpu.getA(7);
                for (int off = 0; off <= 12; off += 2) {
                    uint32_t v = peek32(sp + off);
                    if (v == 0x6C746C6B) {       // 'ltlk'
                        rlog++;
                        std::printf("[%10lld] GETRSRC op=%04X 'ltlk' id=%04X "
                                    "(a7+%d) PC=%08X\n",
                                    (long long)cpu.getClock(), op,
                                    peek32(sp + off + 4) >> 16, off, pc);
                        retPc = pc + 2;
                        break;
                    }
                }
            }
        }

        // O6.11 SDLC: dump the frozen transaction state at the $A6540
        // mutex spin — $8ce (abort/busy flags), $63e (mutex), $63f
        // (retry state), $630/$634 (pending-op / resume ptrs), and
        // whether $A65BC (the transaction) is re-entered at all.
        if (getenv("WATCH_TX2")) {
            static long s1 = 0, s2 = 0;
            uint32_t a2 = cpu.getA(2);
            if (pc == 0x000A6540 && s1++ % 4000000 == 0)
                std::printf("[%10lld] SPIN $A6540 $8ce=%02X $63e=%02X $63f=%02X "
                            "SCC WR9=%02X WR15(A)=%02X WR15(B)=%02X irq=%d "
                            "sccIrqLine=%d\n",
                            (long long)cpu.getClock(), mem.peek8(a2+0x8ce),
                            mem.peek8(a2+0x63e), mem.peek8(a2+0x63f),
                            mem.scc().wr(1, 9), mem.scc().wr(1, 15),
                            mem.scc().wr(0, 15), mem.scc().irqAsserted(),
                            mem.iplLevel() == 4);
            if (pc == 0x000A65BC && s2++ < 10)
                std::printf("[%10lld] ENTER $A65BC (transaction re-invoked) "
                            "$8ce=%02X\n", (long long)cpu.getClock(),
                            mem.peek8(a2+0x8ce));
        }

        // O6.11 SDLC: dump the LocalTalk transaction decision at $A6772
        // (blt $a6796 re-arm vs decrement retry $644 → complete). Shows
        // D3/D0 (the frame-length test) and the counters $640/$644/$63f.
        if (getenv("WATCH_TX") && pc == 0x000A6772) {
            static long txlog = 0;
            if (txlog++ < 20) {
                uint32_t a2 = cpu.getA(2);
                int16_t d3 = int16_t(cpu.getD(3)), d0 = int16_t(cpu.getD(0));
                std::printf("[%10lld] TX $A6772 D3=%d D0=%d -> D3-D0-4=%d %s | "
                            "$640=%02X $644=%02X $63f=%02X $645=%02X\n",
                            (long long)cpu.getClock(), d3, d0, d3 - d0 - 4,
                            (d3 - d0 - 4) < 0 ? "REARM" : "count-down",
                            mem.peek8(a2 + 0x640), mem.peek8(a2 + 0x644),
                            mem.peek8(a2 + 0x63f), mem.peek8(a2 + 0x645));
            }
        }

        static int probeHits = 0;
        if (probePc && pc == probePc && probeHits++ < 5) {
            std::printf("[%10lld] PROBE $%08X SR=%04X D0=%08X D1=%08X D2=%08X "
                        "D3=%08X D4=%08X\n    A0=%08X A1=%08X A2=%08X A3=%08X "
                        "A4=%08X A6=%08X A7=%08X\n",
                        (long long)cpu.getClock(), pc, cpu.getSR(), cpu.getD(0),
                        cpu.getD(1), cpu.getD(2), cpu.getD(3), cpu.getD(4),
                        cpu.getA(0), cpu.getA(1), cpu.getA(2), cpu.getA(3),
                        cpu.getA(4), cpu.getA(6), cpu.getA(7));
            uint32_t sp = cpu.getA(7);
            std::printf("    (A7)@%08X: %08X %08X %08X %08X | +$10: %08X +$12: %08X\n",
                        sp, peek32(sp), peek32(sp+4), peek32(sp+8), peek32(sp+12),
                        peek32(sp+0x10), peek32(sp+0x12));
            // vCheckLoad convention probe: decode d3/d1 as FOURCC (type),
            // (a2)/(a2+4)/(a2+8) and *a0 (handle deref) — see RsrcPatcher
            auto fourcc = [](uint32_t v){
                static char b[5]; for (int i=0;i<4;i++){ char c=char(v>>(24-8*i));
                b[i]=(c>=32&&c<127)?c:'.'; } b[4]=0; return b; };
            uint32_t a0=cpu.getA(0), a2=cpu.getA(2);
            std::printf("    d3='%s' d1='%s'  (a2)=%08X (a2+4)=%08X (a2+8)=%08X "
                        "*a0=%08X\n", fourcc(cpu.getD(3)), fourcc(cpu.getD(1)),
                        peek32(a2), peek32(a2+4), peek32(a2+8), peek32(a0));
            // Handle-hunt: treat each stack slot as a Handle, deref twice,
            // print the master ptr + first 2 words (O6.11 resource-patch)
            for (int off = 0; off <= 0x14; off += 4) {
                uint32_t h = peek32(sp + off);
                if (h < 0x1000 || h >= 0xA00000) continue;
                uint32_t mp = peek32(h);
                if (mp < 0x1000 || mp >= 0xA00000) continue;
                std::printf("      [a7+%02X]=%08X -> *=%08X  data[0..3]=%04X %04X "
                            "size@-8=%08X\n", off, h, mp,
                            uint16_t(mem.peek8(mp)<<8|mem.peek8(mp+1)),
                            uint16_t(mem.peek8(mp+2)<<8|mem.peek8(mp+3)),
                            peek32(mp-8));
            }
        }

        uint32_t d = pc > lastPc ? pc - lastPc : lastPc - pc;
        uint32_t p24 = pc & 0xFFFFFF;
        // skip the sound/PRNG loops that flood the ring
        bool noisy = (p24 >= 0xA45C00 && p24 < 0xA46100)
                  || (p24 >= 0xA46800 && p24 < 0xA47300)
                  || (p24 >= 0xA14C00 && p24 < 0xA15500)
                  || (p24 >= 0xA4A000 && p24 < 0xA4A480);
        if (d > 0x40 && !noisy) { traj[trajIdx++ % traj.size()] = pc; }
        lastPc = pc;
        if (!trajDumped && (pc & 0xFFFFFF) == trajAt) {
            trajDumped = true;
            // O6.11: the AppleTalk boot gates — HWCfgFlags ($B22, lmgr's
            // btst #6 on the high byte guards its XPRAM $E0 read),
            // SPConfig ($1FB) and PortBUse ($291)
            std::printf("--- globals at $%08X: HWCfgFlags=%02X%02X SPConfig=%02X "
                        "PortBUse=%02X  XPRAM E0-E3=%02X %02X %02X %02X\n",
                        trajAt, mem.peek8(0xB22), mem.peek8(0xB23),
                        mem.peek8(0x1FB), mem.peek8(0x291),
                        mem.egret().pram(0xE0), mem.egret().pram(0xE1),
                        mem.egret().pram(0xE2), mem.egret().pram(0xE3));
            std::printf("--- trajectory into $%08X (oldest first):\n", trajAt);
            for (size_t i = 0; i < traj.size(); i++) {
                uint32_t p = traj[(trajIdx + i) % traj.size()];
                if (p) std::printf("  $%08X\n", p);
            }
            std::printf("--- instruction ring into $%08X:\n", trajAt);
            for (size_t i = 0; i < iring.size(); i++) {
                auto& s = iring[(iringIdx + i) % iring.size()];
                if (!s.empty()) std::printf("  %s\n", s.c_str());
            }
        }

        if (mem.overlay() != overlayWas) {
            overlayWas = mem.overlay();
            std::printf("[%10lld] overlay -> %d at PC $%06X\n",
                        (long long)cpu.getClock(), overlayWas ? 1 : 0, pc);
        }
        if (mem.ramConfig() != configWas) {
            configWas = mem.ramConfig();
            std::printf("[%10lld] RAM config -> $%02X at PC $%06X\n",
                        (long long)cpu.getClock(), configWas, pc);
        }
    }

    std::printf("\nstopped: clock=%lld steps=%ld halted=%d stopped=%d pc=$%08X\n",
                (long long)cpu.getClock(), steps, cpu.isHalted(),
                cpu.isStopped(), cpu.getPC0());
    std::printf("bus errors taken: %ld (last at PC $%08X)\n", cpu.berrs, cpu.lastBerrPc);
    std::printf("VIA1 portA=$%02X portB=$%02X  videoCfg=$%02X  Ticks=%u\n",
                mem.via1().portA(), mem.via1().portB(), mem.videoConfig(),
                peek32(0x16A));

    mem.egret().savePram("lcii_trace.pram");
    std::printf("exception vector histogram (vector: count):\n ");
    for (int v = 0; v < 64; v++)
        if (cpu.vecHist[v]) std::printf(" [%d]=%ld", v, cpu.vecHist[v]);
    std::printf("\n");
    std::printf("SCSI: reads=%ld writes=%ld selects=%ld commands=%ld lastCmd=$%02X\n",
                mem.scsi().reads, mem.scsi().writes, mem.scsi().selects,
                mem.scsi().commands, mem.scsi().lastCmd);

    if (dasmTo > dasmFrom) {                 // post-mortem RAM disassembly
        std::printf("dasm $%08X-$%08X (vec2 handler = $%08X):\n",
                    dasmFrom, dasmTo, peek32(cpu.getVBR() + 8));
        char da[96];
        for (uint32_t a = dasmFrom; a < dasmTo; ) {
            int n = safeDasm(da, a);
            std::printf("  $%08X  [%04X] %s\n", a,
                        uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)), da);
            a += uint32_t(n);
        }
    }

    {   // where did we stop?
        char da[96];
        uint32_t a = cpu.getPC0() - 16;
        std::printf("code at the stop point:\n");
        for (int i = 0; i < 12; i++) {
            int n = safeDasm(da, a);
            std::printf("  %c$%08X  %s\n", a == cpu.getPC0() ? '>' : ' ', a, da);
            a += uint32_t(n);
        }
    }

    // Video state + screenshot (lcii_boot.ppm in the CWD)
    long vramUsed = 0;
    for (uint32_t i = 0; i < V8Memory::kVramSize; i++)
        if (mem.vram()[i]) vramUsed++;
    std::printf("VRAM non-zero bytes: %ld  depth=%d  sense=%d  pens[0]=%06X pens[FF]=%06X\n",
                vramUsed, mem.videoConfig() & 7, mem.monitorSense(),
                mem.ariel().pen(0), mem.ariel().pen(0xFF));
    {
        V8Video video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        int hres, vres;
        V8Video::resolution(mem.monitorSense(), hres, vres);
        std::ofstream ppm("lcii_boot.ppm", std::ios::binary);
        ppm << "P6\n" << hres << " " << vres << "\n255\n";
        for (uint32_t px : fb) {
            char rgb[3] = { char(px >> 16), char(px >> 8), char(px) };
            ppm.write(rgb, 3);
        }
        std::printf("screenshot: lcii_boot.ppm (%dx%d)\n", hres, vres);
    }

    std::vector<std::pair<uint32_t, long>> hot(cpu.pcHits.begin(), cpu.pcHits.end());
    std::sort(hot.begin(), hot.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("\nhot PCs:\n");
    for (int i = 0; i < pcTop && i < int(hot.size()); i++)
        std::printf("  $%08X  %ld\n", hot[i].first, hot[i].second);

    return 0;
}
