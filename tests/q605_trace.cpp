// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// q605_trace — LC 475 / Quadra 605 ROM boot trace (Q5 dev tool, the
// lcii_trace pattern): runs the real FF7439EE ROM on the Q605 machine
// and reports PC coverage, the exception-vector histogram, bus-error
// sites and the first I/O accesses. Not a CTest gate.
//
// Usage: q605_trace <rom> [--cycles N] [--io N] [--dafb-io N] [--swim-io N]
//                   [--berr N] [--pcring N]
//                   [--stop-at HEXPC [--stop-skip N]] [--watch HEXPC]
// --stop-skip N ignores the first N hits of --stop-at, to catch a
// specific call of a routine that runs many times.

#include "Cpu040.h"
#include "Q605Memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

namespace {

class TraceCpu : public Cpu040 {
public:
    using Cpu040::Cpu040;
    std::map<int, long> vecHist;
    long irqCount[8] = {};
    void willInterrupt(moira::u8 level) override { irqCount[level & 7]++; }
    void willExecute(moira::M68kException, moira::u16 vector) override {
        vecHist[vector]++;
        if (vector == 2) {
            std::printf("  VEC2 #%ld pc=$%08X sr=$%04X A0=$%08X A1=$%08X A3=$%08X A7=$%08X clk=%lld\n",
                        vecHist[2], getPC0(), getSR(), getA(0), getA(1), getA(3), getSP(),
                        (long long)getClock());
            justFaulted = true;
            std::printf("       D1=$%08X D2=$%08X D3=$%08X\n", getD(1), getD(2), getD(3));
        }
        // Q6.5b: the System Error "illegal instruction" crash dialog is raised
        // by vector 4 (illegal), 10 (line-A), or 11 (line-F). Log the faulting
        // PC + regs so the offending opcode can be disassembled.
        if (vector == 4 || vector == 11) {
            static long en = 0;
            if (en++ < 20)
                std::printf("  EXC vec=%u pc=$%08X sr=$%04X D0=$%08X D1=$%08X A0=$%08X A1=$%08X A6=$%08X SP=$%08X clk=%lld\n",
                            vector, getPC0(), getSR(), getD(0), getD(1), getA(0), getA(1),
                            getA(6), getSP(), (long long)getClock());
        }
    }
    bool justFaulted = false;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <rom> [--cycles N] [--io N] [--dafb-io N] [--swim-io N] [--berr N] [--pcring N]\n",
            argv[0]);
        return 2;
    }
    long long cycles = 200000000;   // 8 machine-seconds at 25 MHz
    int ioMax = 60, dafbIoMax = 0, swimIoMax = 0, berrMax = 40; size_t pcRing = 32;
    uint32_t stopAt = 0, watch = 0, wwatch = 0, firstpc = 0, complog = 0, dumpAt = 0;
    long stopSkip = 0;                 // ignore the first N hits of --stop-at
    std::string diskPath;             // --disk / --scsi: boot image at ID 0
    long long scsiFrom = 0, scsiTo = 0;   // SCSI register-trace clock window
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = atoll(argv[++i]);
        else if (a == "--io" && i + 1 < argc) ioMax = atoi(argv[++i]);
        else if (a == "--dafb-io" && i + 1 < argc) dafbIoMax = atoi(argv[++i]);
        else if (a == "--swim-io" && i + 1 < argc) swimIoMax = atoi(argv[++i]);
        else if (a == "--berr" && i + 1 < argc) berrMax = atoi(argv[++i]);
        else if (a == "--pcring" && i + 1 < argc) pcRing = size_t(atoll(argv[++i]));
        else if (a == "--stop-at" && i + 1 < argc) stopAt = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--stop-skip" && i + 1 < argc) stopSkip = atol(argv[++i]);
        else if (a == "--watch" && i + 1 < argc) watch = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if ((a == "--disk" || a == "--scsi") && i + 1 < argc) diskPath = argv[++i];
        else if (a == "--scsi-log" && i + 2 < argc) { scsiFrom = atoll(argv[++i]); scsiTo = atoll(argv[++i]); }
        else if (a == "--wwatch" && i + 1 < argc) wwatch = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--firstpc" && i + 1 < argc) firstpc = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--complog" && i + 1 < argc) complog = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--dumpat" && i + 1 < argc) dumpAt = uint32_t(strtoul(argv[++i], nullptr, 16));
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    uint32_t ramMb = 32;
    for (int i = 2; i < argc - 1; i++)
        if (std::string(argv[i]) == "--ram") ramMb = uint32_t(atoi(argv[i+1]));
    Q605Memory mem(ramMb << 20);
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "ROM must be 1 MB (got %zu)\n", rom.size()); return 1; }
    TraceCpu cpu(mem);
    mem.setCpu(&cpu);

    // SCSI boot disk at ID 0 (the ROM's boot scan needs the $6A DDM entry;
    // wrap bare images with tools/wrap_hfs.py). Seed factory XPRAM + a
    // persisted PRAM the same way the LC II tracer does so the cold-boot
    // full-RAM burn-in is skipped on re-runs.
    if (!diskPath.empty())
        std::printf("SCSI disk: %s %s\n", diskPath.c_str(),
                    mem.attachScsi(diskPath) ? "attached" : "FAILED");
    // PRAM persistence makes back-to-back runs DIVERGE: a boot mutates XPRAM,
    // the next run loads the mutated state and takes a different path (SCSI
    // counts differ, --stop-at points shift by ~15M cycles). For reproducible
    // debugging set Q605_NOPRAM to force the deterministic factory cold boot
    // (identical every run; the Q6.5d dsBadPatch repro lands at exactly clk
    // 1235015000). Without it the on-disk q605_trace.pram is loaded/saved.
    bool noPram = getenv("Q605_NOPRAM") != nullptr;
    if (!noPram && mem.cuda().loadPram("q605_trace.pram"))
        std::printf("PRAM: q605_trace.pram loaded\n");
    mem.cuda().factoryDefaults();

    if (wwatch) {
        mem.ramWatch_ = wwatch;
        mem.onRamWrite = [&](uint32_t a, int sz, uint32_t v) {
            static long n = 0;
            // Q605_WWFROM: only log write-watch hits at/after this clock, so a
            // hot address can be observed near a late event without burning the
            // 200-line budget on early-boot traffic.
            static long long wwFrom = getenv("Q605_WWFROM") ? atoll(getenv("Q605_WWFROM")) : 0;
            if (cpu.getClock() < wwFrom) return;
            if (n++ > 200) return;
            std::printf("  WWATCH $%08X sz%d = $%0*X  pc=$%08X A0=$%08X A1=$%08X A2=$%08X A3=$%08X A4=$%08X clk=%lld\n",
                        a, sz, sz * 2, v, cpu.getPC0(), cpu.getA(0), cpu.getA(1),
                        cpu.getA(2), cpu.getA(3), cpu.getA(4), (long long)cpu.getClock());
        };
    }

    // IOSB ASC feed counters: distinguish left/right FIFO traffic.
    long ascFifoWrites[2] = {};
    mem.asc().onWrite = [&](uint32_t off, uint8_t) {
        if (off < 0x800) ascFifoWrites[(off >> 10) & 1]++;
    };

    int ioSeen = 0, dafbIoSeen = 0, swimIoSeen = 0, berrSeen = 0;
    long scsiRegLog = 0, lastDma = 0;
    mem.scsi().onCommand = [&](const std::vector<uint8_t>& cdb) {
        std::printf("  SCSI CDB [");
        for (uint8_t b : cdb) std::printf("%02X ", b);
        std::printf("] (data since prev: %ld) clk=%lld\n",
                    mem.scsi().dmaBytes - lastDma, (long long)cpu.getClock());
        lastDma = mem.scsi().dmaBytes;
    };
    if (scsiTo)
        mem.scsi().onAccess = [&](int reg, bool w, uint8_t v) {
            long long c = cpu.getClock();
            if (c < scsiFrom || c > scsiTo) return;
            if (!getenv("Q605_SCSIALL")) {            // Q605_SCSIALL: no filter
                if (!w && reg == 4) return;      // skip the phase-poll spam
                if (w && reg == 3 && v == 0x90) return; // skip DMA XFER spam
                if (!w && (reg == 7 || reg == 5)) return; // chunk-loop reads
            }
            if (scsiRegLog++ > 400) return;
            std::printf("  SCSI %c reg%X = $%02X  (pc=$%08X clk=%lld)\n",
                        w ? 'W' : 'R', reg, v, cpu.getPC0(), c);
        };
    mem.onIoAccess = [&](uint32_t a, bool w, uint32_t v) {
        if (ioSeen++ < ioMax)
            std::printf("  IO  %c $%08X = $%02X  (pc=$%08X clk=%lld)\n", w ? 'W' : 'R', a, v, cpu.getPC0(),
                        (long long)cpu.getClock());
        bool dafb = a >= 0xF9800000 && a < 0xF9800400;
        bool holding = (a & 0xFFFF) >= 0xE07C && (a & 0xFFFF) <= 0xE07F;
        if ((dafb || holding) && dafbIoSeen++ < dafbIoMax)
            std::printf("  DAFB %c $%08X = $%02X  (pc=$%08X clk=%lld)\n",
                        w ? 'W' : 'R', a, v, cpu.getPC0(),
                        (long long)cpu.getClock());
        uint32_t io = (a & 0x0FFFFFFF) & ~0xF00000u;
        if (io >= 0x1E000 && io < 0x20000 && swimIoSeen++ < swimIoMax)
            std::printf("  SWIM %c reg%X $%08X = $%02X  (pc=$%08X clk=%lld)\n",
                        w ? 'W' : 'R', (io >> 9) & 0x0F, a, v,
                        cpu.getPC0(), (long long)cpu.getClock());
    };
    long long cudaFrom = 0;
    { const char* e = getenv("Q605_CUDA_FROM"); if (e) cudaFrom = atoll(e); }
    mem.cuda().onByte = [&](bool toCuda, uint8_t b) {
        if (cudaFrom && cpu.getClock() < cudaFrom) return;
        std::printf("  CUDA %s $%02X  (clk=%lld pc=$%08X)\n", toCuda ? "<-" : "->", b,
                    (long long)cpu.getClock(), cpu.getPC0());
    };
    mem.cuda().onEdge = [&](uint8_t pb, int phase, bool xcvr) {
        static int n = 0;
        if (n++ < 40)
            std::printf("  EDGE pb=$%02X phase=%d xcvr=%d (clk=%lld)\n", pb, phase, xcvr,
                        (long long)cpu.getClock());
    };
    mem.onBusError = [&](uint32_t a, bool w) {
        if (berrSeen++ < berrMax)
            std::printf("  BERR %c $%08X  (pc=$%08X)\n", w ? 'W' : 'R', a, cpu.getPC0());
    };

    cpu.hardReset();
    std::printf("[q605_trace] reset: cpuHeld=%d overlay=%d pc=$%08X sp=$%08X\n",
                mem.cpuHeld(), mem.overlay(), cpu.getPC0(), cpu.getSP());

    // --dumpat with no --stop-at: static ROM/RAM disassembly right after
    // reset (overlay on → ROM mapped), then exit. Handy for reading ROM
    // routines without having to hit a runtime breakpoint.
    if (dumpAt && !stopAt) {
        std::printf("[q605_trace] static disasm at $%08X:\n", dumpAt);
        uint32_t dpc = dumpAt;
        char dbuf[128];
        for (int i = 0; i < 96; i++) {
            int len = 2;
            try { len = cpu.disassemble(dbuf, dpc); }
            catch (...) { std::snprintf(dbuf, sizeof dbuf, "<fault>"); }
            std::printf("   $%08X  %s\n", dpc, dbuf);
            dpc += len;
        }
        return 0;
    }

    std::vector<uint32_t> ring(pcRing, 0);
    size_t rp = 0;
    // Q6.4: trace who clears the ROvr boot flag XPRAM $8A. Print the CPU PC
    // and the recent PC ring when a Cuda XPRAM write targets $8A.
    mem.cuda().onXPramWrite = [&](int addr, uint8_t v) {
        if (!getenv("Q605_XPRAM8A") || addr != 0x8A) return;
        static long n = 0;
        if (n++ > 20) return;
        std::printf("  XPWRITE $8A = $%02X  pc=$%08X  clk=%lld\n", v, cpu.getPC0(),
                    (long long)cpu.getClock());
        // Walk the stack for ROM return addresses ($408xxxxx) to find the
        // semantic caller above the Cuda transport driver.
        uint32_t sp = cpu.getSP();
        std::printf("    stack ROM callers:");
        for (int i = 0; i < 200; i += 2) {
            uint32_t w = uint32_t(mem.peek8(sp+i))<<24 | uint32_t(mem.peek8(sp+i+1))<<16 |
                         uint32_t(mem.peek8(sp+i+2))<<8 | mem.peek8(sp+i+3);
            if ((w & 0xFFF00000u) == 0x40800000u) std::printf(" [sp+%d]=$%08X", i, w);
        }
        std::printf("\n");
    };
    std::map<uint32_t, long> pcCov;               // 64 KB granularity
    long long slice = 5000;
    long long done = 0;
    // --ckpt N: every N cycles print a progress line (SCSI cmds, current PC,
    // count of instructions executed in loaded-System RAM $0080_0000..$008F_FFFF
    // since the last checkpoint, and restart-vector $4080000A hits) so a single
    // run reveals the boot trajectory (progressing vs looping).
    long long ckpt = 0, nextCkpt = 0; long sysHits = 0, restartHits = 0;
    { const char* e = getenv("Q605_CKPT"); if (e) { ckpt = atoll(e); nextCkpt = ckpt; } }
    while (done < cycles) {
        if (ckpt && cpu.getClock() >= nextCkpt) {
            std::printf("  CKPT clk=%lld pc=$%08X scsiCmds=%ld sysRAMhits+=%ld restart=%ld\n",
                        (long long)cpu.getClock(), cpu.getPC0(),
                        mem.scsi().commands, sysHits, restartHits);
            sysHits = 0; nextCkpt += ckpt;
        }
        if (mem.cpuHeld()) { mem.tick(int(slice)); done += slice; continue; }
        moira::i64 t = cpu.getClock() + slice;
        bool stop = false;
        while (cpu.getClock() < t && !cpu.isHalted()) {
            uint32_t pc = cpu.getPC0();
            if (ckpt) {
                if ((pc >> 20) == 0x008) sysHits++;
                else if (pc == 0x4080000A) restartHits++;
            }
            static uint32_t prevpc = 0;
            if (firstpc && pc == firstpc && prevpc != firstpc &&
                (prevpc < pc || prevpc > pc + 8)) {
                static int fn = 0;
                if (fn++ < 30)
                    std::printf("  FIRSTPC $%08X from $%08X  D0=$%08X D1=$%08X D2=$%08X D7=$%08X A0=$%08X A1=$%08X A6=$%08X SP=$%08X clk=%lld\n",
                                pc, prevpc, cpu.getD(0), cpu.getD(1), cpu.getD(2), cpu.getD(7),
                                cpu.getA(0), cpu.getA(1), cpu.getA(6), cpu.getSP(),
                                (long long)cpu.getClock());
            }
            prevpc = pc;
            // Q6.5d: Q605_PCLOG="<from> <to>" — log the instruction path inside a
            // tight clock window (e.g. the ~8848-cyc gap between a successful
            // $51F08 read completing and the SIM/FM re-issuing it) to see the
            // decision that re-reads. Collapses runs of the same PC; caps output.
            if (const char* pl = getenv("Q605_PCLOG")) {
                static long long plFrom = 0, plTo = 0; static bool plInit = false;
                if (!plInit) { std::sscanf(pl, "%lld %lld", &plFrom, &plTo); plInit = true; }
                static long pn = 0; static uint32_t lastLogged = 0;
                long long c = cpu.getClock();
                if (c >= plFrom && c <= plTo && pc != lastLogged && pn++ < 1500) {
                    lastLogged = pc;
                    char db[96];
                    try { cpu.disassemble(db, pc); } catch (...) { db[0] = 0; }
                    std::printf("  PCLOG clk=%lld $%08X  %s\n", c, pc, db);
                }
            }
            // Q6.5d: at the SCSI Manager 4.3 completion decision $00121B60
            // (cmpi.w #$84,($8,A3) → if equal, re-issue the read), log the nexus
            // A3 and its condition fields so the retry trigger can be traced.
            // ($8,A3)==$84 → re-read; a normal completion differs. Env Q605_NEXUS.
            if (getenv("Q605_NEXUS") && pc == 0x00121B5A) {
                static long nn = 0;
                if (nn++ < 40) {
                    uint32_t a3 = cpu.getA(3), a4 = cpu.getA(4);
                    std::printf("  NEXUS clk=%lld A3=$%08X A4=$%08X (8,A3)=$%02X (e,A3)=$%02X (bc,A3)=$%04X (88,A4)state=$%04X (96,A4)=$%04X\n",
                                (long long)cpu.getClock(), a3, a4,
                                mem.peek8(a3+8), mem.peek8(a3+0xE),
                                (mem.peek8(a3+0xBC)<<8)|mem.peek8(a3+0xBD),
                                (mem.peek8(a4+0x88)<<8)|mem.peek8(a4+0x89),
                                (mem.peek8(a4+0x96)<<8)|mem.peek8(a4+0x97));
                }
            }
            // Q6.5b: log SCSI 53C96 IRQ-line 0->1 transitions with clk, to find
            // the spurious interrupt that dispatches the crash event ($0011E996
            // is only reached on our machine, never on MAME). Env Q605_SCSIIRQ.
            if (getenv("Q605_SCSIIRQ")) {
                static bool prevIrq = false; static long sq = 0;
                static long long from = atoll(getenv("Q605_SCSIIRQ"));
                bool iq = mem.scsi().irq();
                if (iq && !prevIrq && cpu.getClock() >= from && sq++ < 200)
                    std::printf("  SCSIIRQ^ clk=%lld pc=$%08X lastCmd=$%02X\n",
                                (long long)cpu.getClock(), pc, mem.scsi().lastCmd);
                prevIrq = iq;
            }
            ring[rp++ % ring.size()] = pc;
            pcCov[pc >> 16]++;
            // --complog: dump the Cuda completion-ISR reply framing at
            // $408A9CFC (move.w ($10,A2),D0). A2 = receive-state block:
            // ($10,A2) = received byte count, ($0C,A2)/A1 = reply buffer,
            // ($08,A2) = data destination. The count-4 drives the dbra copy.
            // Selector-dispatch tap at $4080ED7E: the dispatcher does
            // move.l (A7)+,D2; move.w (A7)+,D0 — so [SP]=D2(4), [SP+4]=selector.
            if (pc == 0x4080ED7E) {
                static long sn = 0;
                if (sn++ < 60) {
                    uint32_t sp = cpu.getSP();
                    uint16_t sel = uint16_t(mem.peek8(sp+4)<<8 | mem.peek8(sp+5));
                    std::printf("  SELDISP #%ld clk=%lld sel=$%04X D1=$%08X D2=$%08X A0=$%08X A1=$%08X\n",
                                sn, (long long)cpu.getClock(), sel, cpu.getD(1), cpu.getD(2),
                                cpu.getA(0), cpu.getA(1));
                    if (sel == 2) {   // restart (ShutDwnStart): dump raw stack
                        std::printf("    STACK[sp..sp+96]:");
                        for (int i = 0; i < 96; i += 2)
                            std::printf(" %02X%02X", mem.peek8(sp+i), mem.peek8(sp+i+1));
                        std::printf("\n");
                    }
                }
            }
            // Q6.5d: SCSI Manager DMA-vs-PIO decision ($0011B3A0
            // cmpi.b #1,($1A6,A5)). ($1A6,A5)==1 → the pathological non-DMA
            // single-byte Transfer-Info path (used for the scod/-16470 resource
            // read of LBA $51F08 that stalls → ioErr → dsBadPatch). Log A5 +
            // the mode byte so the flag's origin can be found. Env Q605_SCSIMODE.
            if (getenv("Q605_SCSIMODE") && pc == 0x0011B3A0) {
                static long mn = 0;
                if (mn++ < 30) {
                    uint32_t a5 = cpu.getA(5);
                    std::printf("  SCSIMODE clk=%lld A5=$%08X [A5+1A6]=$%02X [A5+3E]=$%02X [A5+39]=$%02X\n",
                                (long long)cpu.getClock(), a5, mem.peek8(a5+0x1A6),
                                mem.peek8(a5+0x3E), mem.peek8(a5+0x39));
                }
            }
            // Q6.5d: _SysError trap patch stub ($0004C966) — every _SysError
            // ($A9C9) invocation is intercepted here. Log the error code (D0.w)
            // and the caller = (A7) so the site that raises dsBadPatch(99) can
            // be identified vs MAME. Env Q605_SYSERR=<startClk>.
            if (getenv("Q605_SYSERR") && pc == 0x0004C966) {
                static long long seFrom = atoll(getenv("Q605_SYSERR"));
                static long sen = 0;
                if (cpu.getClock() >= seFrom && sen++ < 200) {
                    uint32_t sp = cpu.getSP();
                    uint32_t ret = uint32_t(mem.peek8(sp))<<24 | uint32_t(mem.peek8(sp+1))<<16 |
                                   uint32_t(mem.peek8(sp+2))<<8 | mem.peek8(sp+3);
                    std::printf("  SYSERR clk=%lld D0=%d($%04X) caller=$%08X D4=$%08X D5=$%08X\n",
                                (long long)cpu.getClock(), cpu.getD(0) & 0xFFFF,
                                cpu.getD(0) & 0xFFFF, ret, cpu.getD(4), cpu.getD(5));
                }
            }
            // Dialog/Alert text tap ($4082919C): the ROM string-draw helper
            // reads a Pascal string ptr from ($4,A7). Dump printable strings so
            // a startup-error alert can be read (Q6.5b). Env Q605_STRTAP.
            if (getenv("Q605_STRTAP") && pc == 0x4082919C) {
                static long strn = 0;
                uint32_t sp = cpu.getSP();
                uint32_t p = uint32_t(mem.peek8(sp+4))<<24 | uint32_t(mem.peek8(sp+5))<<16 |
                             uint32_t(mem.peek8(sp+6))<<8 | mem.peek8(sp+7);
                uint8_t len = mem.peek8(p);
                if (len && len < 64 && strn++ < 400) {
                    std::printf("  STR clk=%lld \"", (long long)cpu.getClock());
                    for (int i = 1; i <= len; i++) {
                        uint8_t ch = mem.peek8(p + i);
                        std::putchar(ch >= 0x20 && ch < 0x7F ? ch : '.');
                    }
                    std::printf("\"\n");
                }
            }
            // Q6.5b: event/deferred-task dispatcher tap ($0011A6E6, right after
            // handler=table[D0] and before jsr): log event code D0, task record
            // A4, context A0=*(A4+4), the chosen handler A1, and the context's
            // +$F0 completion field — to see the event stream and catch the
            // fatal record's provenance. Env Q605_EVTAP=<startClk>.
            if (getenv("Q605_EVTAP") && pc == 0x0011A6E6) {
                static long long evFrom = atoll(getenv("Q605_EVTAP"));
                static long evn = 0;
                if (cpu.getClock() >= evFrom && evn++ < 300) {
                    uint32_t a4 = cpu.getA(4);
                    uint32_t ctx = uint32_t(mem.peek8(a4+4))<<24 | uint32_t(mem.peek8(a4+5))<<16 |
                                   uint32_t(mem.peek8(a4+6))<<8 | mem.peek8(a4+7);
                    std::printf("  EVT clk=%lld code=$%04X rec=$%08X ctx=$%08X hdlr(A1)=$%08X ctx+F0=$%02X%02X%02X%02X\n",
                                (long long)cpu.getClock(), cpu.getD(0) & 0xFFFF, a4, ctx, cpu.getA(1),
                                mem.peek8(ctx+0xF0), mem.peek8(ctx+0xF1), mem.peek8(ctx+0xF2), mem.peek8(ctx+0xF3));
                }
            }
            // Q6.5b: async-decision probe — at the bne after $11E5A6 ($0011A9D2)
            // log the SIM's interrupt-check inputs: flag A5+$38, the polled
            // hardware byte ptr *(A5+$C) and its live value, the bit number
            // A5+$4D, and Z (bne taken = async/trampoline path when Z=0).
            if (getenv("Q605_ASYNCHK") && pc == 0x0011A9D2) {
                static long an = 0;
                if (an++ < 12) {
                    uint32_t a5 = cpu.getA(5);
                    uint32_t p = uint32_t(mem.peek8(a5+0xC))<<24 | uint32_t(mem.peek8(a5+0xD))<<16 |
                                 uint32_t(mem.peek8(a5+0xE))<<8 | mem.peek8(a5+0xF);
                    std::printf("  ASYNCHK clk=%lld flag38=$%02X ptr(A5+C)=$%08X [ptr]=$%02X bit4D=%u Z=%d scsiIRQ=%d via2IFR=$%02X\n",
                                (long long)cpu.getClock(), mem.peek8(a5+0x38), p, mem.peek8(p),
                                mem.peek8(a5+0x4D), (cpu.getSR() >> 2) & 1,
                                mem.scsi().irq(), mem.via2Ifr());
                }
            }
            // Q6.5b: at the SCSI interrupt handler $0011E996, log the VIA2
            // IFR/IER + SCSI irq/context so the spurious-IRQ crash can be
            // characterized (is the SCSI int enabled? is irq_ stale?).
            if (pc == 0x0011E996) {
                // Q605_ISRFROM=<clk>: gate ISR logging to a late window (e.g. the
                // Q6.5d dsBadPatch completion ~1220592000) instead of the first
                // 30 early-boot hits. Shows whether the SCSI ISR fires at the
                // transaction's completion and which continuation (*(A0+F0)) it
                // jumps to — the async-completion path.
                static long long isrFrom = getenv("Q605_ISRFROM") ? atoll(getenv("Q605_ISRFROM")) : -1;
                static long hn = 0;
                bool show = (isrFrom < 0) ? (hn < 30) : (cpu.getClock() >= isrFrom && hn < 60);
                if (show) {
                    hn++;
                    std::printf("  SCSIISR clk=%lld A0=$%08X hdlr=*(A0+F0)=$%08X via2IFR=$%02X via2IER=$%02X scsiIRQ=%d sr=$%04X\n",
                                (long long)cpu.getClock(), cpu.getA(0),
                                uint32_t(mem.peek8(cpu.getA(0)+0xF0))<<24 | uint32_t(mem.peek8(cpu.getA(0)+0xF1))<<16 |
                                uint32_t(mem.peek8(cpu.getA(0)+0xF2))<<8 | mem.peek8(cpu.getA(0)+0xF3),
                                mem.via2Ifr(), mem.via2Ier(), mem.scsi().irq(), cpu.getSR());
                }
            }
            // Task-walk handler tap: at $4080EEB2 (jsr (A0)) log the task
            // element (A2), its flags word, and the handler address A0.
            if (pc == 0x4080EEB2) {
                static long tn = 0;
                if (tn++ < 200)
                    std::printf("  TASK #%ld clk=%lld A2=$%08X flags=$%04X D5=%u handler(A0)=$%08X\n",
                                tn, (long long)cpu.getClock(), cpu.getA(2),
                                uint16_t(mem.peek8(cpu.getA(2)+4)<<8 | mem.peek8(cpu.getA(2)+5)),
                                cpu.getD(5), cpu.getA(0));
            }
            // Cuda receive-ISR header-done tap ($408A9BE2): the ISR has
            // just consumed ($e,A2) header bytes into the header buffer at
            // ($18,A2); ($1,A0)=header[1] gates the +1 dynamic extension
            // (cmpi.b #$2). Log header bytes + the header/data counts + dest
            // to see how each XPRAM read is framed (OSDefault vs SysParam).
            if (pc == 0x408A9BE2) {
                static long hn = 0;
                if (hn++ < 400) {
                    uint32_t a2 = cpu.getA(2);
                    uint16_t hcnt = uint16_t(mem.peek8(a2+0x0e)<<8 | mem.peek8(a2+0x0f));
                    uint16_t dcnt = uint16_t(mem.peek8(a2+0x12)<<8 | mem.peek8(a2+0x13));
                    uint32_t dest = uint32_t(mem.peek8(a2+0x14))<<24 | uint32_t(mem.peek8(a2+0x15))<<16
                                  | uint32_t(mem.peek8(a2+0x16))<<8 | mem.peek8(a2+0x17);
                    std::printf("  HDRDONE #%ld clk=%lld A2=$%08X hcnt=%u dcnt=%u dest=$%08X hdrbuf:",
                                hn, (long long)cpu.getClock(), a2, hcnt, dcnt, dest);
                    for (int i = 0; i < 8; i++) std::printf(" %02X", mem.peek8(a2+0x18+i));
                    std::printf("\n");
                }
            }
            if (complog && pc == complog) {
                static long cn = 0;
                if (cn++ < 120) {
                    uint32_t a2 = cpu.getA(2), a1 = cpu.getA(1);
                    uint32_t cnt = uint32_t(mem.peek8(a2+0x10))<<8 | mem.peek8(a2+0x11);
                    // DCE = A2's $34 field (movea.l ($34,A2),A0 earlier);
                    // its ioCompletion is at DCE+$10 (($10,A0)).
                    uint32_t a0 = uint32_t(mem.peek8(a2+0x34))<<24 | uint32_t(mem.peek8(a2+0x35))<<16
                                | uint32_t(mem.peek8(a2+0x36))<<8 | mem.peek8(a2+0x37);
                    uint32_t comp = uint32_t(mem.peek8(a0+0x10))<<24 | uint32_t(mem.peek8(a0+0x11))<<16
                                  | uint32_t(mem.peek8(a0+0x12))<<8 | mem.peek8(a0+0x13);
                    std::printf("  COMPLOG #%ld clk=%lld A2=$%08X len=%u DCE(A0)=$%08X ioComp=$%08X reply:",
                                cn, (long long)cpu.getClock(), a2, cnt, a0, comp);
                    for (int i = 0; i < 16; i++) std::printf(" %02X", mem.peek8(a1 + i));
                    std::printf("\n");
                }
            }
            if (stopAt && pc == stopAt) {
                if (stopSkip > 0) stopSkip--;
                else { stop = true; break; }
            }
            if (watch && pc == watch) {
                static int wn = 0;
                uint32_t a0 = cpu.getA(0);
                uint32_t spSize = uint32_t(mem.peek8(a0+8))<<24 | uint32_t(mem.peek8(a0+9))<<16 |
                                  uint32_t(mem.peek8(a0+10))<<8 | mem.peek8(a0+11);
                if (wn++ < 400)
                    std::printf("  WATCH $%08X #%d: A0=$%08X spSize=$%08X A1=$%08X A3=$%08X A6=$%08X clk=%lld\n",
                                watch, wn, a0, spSize, cpu.getA(1), cpu.getA(3), cpu.getA(6),
                                (long long)cpu.getClock());
            }
            cpu.execute();
            if (cpu.justFaulted) {
                cpu.justFaulted = false;
                uint32_t fsp = cpu.getSP();
                std::printf("  VEC2-FRAME sp=$%08X:", fsp);
                for (int i = 0; i < 60; i++) {
                    if (i % 20 == 0) std::printf("\n   ");
                    std::printf("%02X ", mem.peek8(fsp + i));
                }
                std::printf("\n");
                auto peek32 = [&](uint32_t a) {
                    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a+1)) << 16 |
                           uint32_t(mem.peek8(a+2)) << 8 | mem.peek8(a+3);
                };
                uint32_t tc = cpu.getTC040(), urp = cpu.getURP040();
                std::printf("  MMU: TC040=$%08X URP=$%08X SRP=$%08X ITT0=$%08X ITT1=$%08X DTT0=$%08X DTT1=$%08X\n",
                            tc, urp, cpu.getSRP040(),
                            cpu.getITT0(), cpu.getITT1(), cpu.getDTT0(), cpu.getDTT1());
                uint32_t a0 = cpu.getA(0);
                std::printf("  [A0=spBlock]:");
                for (int i = 0; i < 64; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", a0 + i);
                    std::printf(" %02X", mem.peek8(a0 + i));
                }
                std::printf("\n");
                bool p8k = (tc >> 14) & 1;
                uint32_t root = (cpu.getSR() & 0x2000) ? cpu.getSRP040() : urp;
                for (uint32_t va : {0x40900000u, 0x408FE000u, 0x40800000u}) {
                    uint32_t ri = va >> 25, pi = (va >> 18) & 0x7F;
                    uint32_t pgi = p8k ? (va >> 13) & 0x1F : (va >> 12) & 0x3F;
                    uint32_t rd = peek32((root & ~0x1FFu) + ri * 4);
                    std::printf("  walk $%08X: root[$%02X]=$%08X", va, ri, rd);
                    if (rd & 2) {
                        uint32_t pd = peek32((rd & ~0x1FFu) + pi * 4);
                        std::printf(" ptr[$%02X]=$%08X", pi, pd);
                        if (pd & 2) {
                            uint32_t pg = peek32((pd & (p8k ? ~0x7Fu : ~0xFFu)) + pgi * 4);
                            std::printf(" page[$%02X]=$%08X", pgi, pg);
                        }
                    }
                    std::printf("\n");
                }
            }
        }
        if (stop) {
            std::printf("[q605_trace] STOP at $%08X\n", stopAt);
            std::printf("  RING (oldest->newest):");
            for (size_t i = 0; i < ring.size(); i++) {
                uint32_t rpc = ring[(rp + i) % ring.size()];
                if (rpc) std::printf(" $%08X", rpc);
            }
            std::printf("\n");
            for (int r = 0; r < 8; r++)
                std::printf("  D%d=$%08X  A%d=$%08X\n", r, cpu.getD(r), r,
                            r == 7 ? cpu.getSP() : cpu.getA(r));
            std::printf("  IRQ: scsi.irq=%d via2IFR=$%02X via2IER=$%02X sr=$%04X\n",
                        mem.scsi().irq(), mem.via2Ifr(), mem.via2Ier(), cpu.getSR());
            auto peekL = [&](uint32_t a) {
                return uint32_t(mem.peek8(a))<<24 | uint32_t(mem.peek8(a+1))<<16 |
                       uint32_t(mem.peek8(a+2))<<8 | mem.peek8(a+3);
            };
            std::printf("  LOWMEM MemTop($108)=$%08X BufPtr($10C)=$%08X "
                        "MemSize($1EF8?)=%08X\n",
                        peekL(0x108), peekL(0x10C), peekL(0x1EF8));
            {
                uint32_t buf = cpu.getA(2);      // SCSI DATA IN destination
                std::printf("  [A2 buffer $%08X]:", buf);
                for (int i = -16; i < 48; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", buf + i);
                    std::printf(" %02X", mem.peek8(buf + i));
                }
                std::printf("\n");
            }
            uint32_t a1 = cpu.getA(1);
            std::printf("  [A1-32..A1+64]:");
            for (int i = -32; i < 64; i++) {
                if (i % 16 == 0) std::printf("\n   $%08X:", a1 + i);
                std::printf(" %02X", mem.peek8(a1 + i));
            }
            // Q6.6: the async-wait control blocks. A3 = deferred-task element
            // ($48 = its ready flag longword), A4 = the IOPB-ish block whose
            // $a0 word is the ioResult-style busy flag polled at $00123BB0.
            // $0C0C = jSCSIInt device record (($be)/($c0,A0) gate at $0011CD44).
            for (int rr : {3, 4}) {
                uint32_t base = cpu.getA(rr);
                std::printf("\n  [A%d $%08X]:", rr, base);
                for (int i = 0; i < 0xB0; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", base + i);
                    std::printf(" %02X", mem.peek8(base + i));
                }
            }
            {
                uint32_t dr = peekL(0x0C0C);
                std::printf("\n  [$0C0C]->$%08X devrec:", dr);
                for (int i = 0; i < 0xD0; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", dr + i);
                    std::printf(" %02X", mem.peek8(dr + i));
                }
            }
            std::printf("\n  disasm from stop:");
            {
                char da2[128];
                uint32_t dpc = stopAt;
                for (int i = 0; i < 40; i++) {
                    int len = 2;
                    try { len = cpu.disassemble(da2, dpc); }
                    catch (...) { std::snprintf(da2, sizeof da2, "<fault>"); }
                    std::printf("\n   $%08X  %s", dpc, da2);
                    dpc += len;
                }
            }
            std::printf("\n  [SP..SP+96]:");
            uint32_t sp = cpu.getSP();
            for (int i = 0; i < 96; i++) {
                if (i % 16 == 0) std::printf("\n   $%08X:", sp + i);
                std::printf(" %02X", mem.peek8(sp + i));
            }
            std::printf("\n");
            // Q605_RAMDUMP="<hexFrom> <hexLen> <file>": at the stop point, dump
            // a raw RAM window to a binary file so guest code can be searched
            // offline (objdump/grep for instruction patterns).
            if (const char* rd = getenv("Q605_RAMDUMP")) {
                unsigned rdFrom = 0, rdLen = 0; char rdFile[256] = {};
                if (std::sscanf(rd, "%x %x %255s", &rdFrom, &rdLen, rdFile) == 3) {
                    FILE* rf = std::fopen(rdFile, "wb");
                    if (rf) {
                        for (unsigned i = 0; i < rdLen; i++) std::fputc(mem.peek8(rdFrom + i), rf);
                        std::fclose(rf);
                        std::printf("  [RAMDUMP $%08X +$%X -> %s]\n", rdFrom, rdLen, rdFile);
                    }
                }
            }
            if (dumpAt) {
                std::printf("  [DUMPAT $%08X] bytes:", dumpAt);
                for (int i = 0; i < 128; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", dumpAt + i);
                    std::printf(" %02X", mem.peek8(dumpAt + i));
                }
                // Disassemble from a byte buffer via a scratch RAM window:
                // Moira reads code through the CPU, but here peek8 gives us
                // the physical bytes; disassemble them directly with Moira's
                // static disassembler by temporarily executing from dumpAt.
                std::printf("\n  [DUMPAT disasm]:");
                uint32_t dpc = dumpAt;
                char dbuf[128];
                for (int i = 0; i < 48; i++) {
                    int len = 2;
                    try { len = cpu.disassemble(dbuf, dpc); }
                    catch (...) { std::snprintf(dbuf, sizeof dbuf, "<fault>"); }
                    std::printf("\n   $%08X  %s", dpc, dbuf);
                    dpc += len;
                }
                std::printf("\n");
            }
            break;
        }
        if (cpu.isHalted()) { std::printf("[q605_trace] CPU HALTED (double fault)\n"); break; }
        done += slice;
    }

    std::printf("\n[q605_trace] after %lld cycles: pc=$%08X sp=$%08X sr=$%04X halted=%d stopped=%d\n",
                done, cpu.getPC0(), cpu.getSP(), cpu.getSR(), cpu.isHalted(), cpu.isStopped());

    std::printf("-- vector histogram --\n");
    for (auto& [v, n] : cpu.vecHist)
        std::printf("  vec %3d : %ld\n", v, n);
    std::printf("-- IRQ (willInterrupt) by level --\n");
    for (int l = 1; l < 8; l++)
        if (cpu.irqCount[l]) std::printf("  IPL %d : %ld\n", l, cpu.irqCount[l]);

    std::printf("-- PC coverage (64 KB regions, top 24) --\n");
    std::vector<std::pair<long, uint32_t>> cov;
    for (auto& [r, n] : pcCov) cov.push_back({n, r});
    std::sort(cov.rbegin(), cov.rend());
    int shown = 0;
    for (auto& [n, r] : cov) {
        if (shown++ >= 24) break;
        std::printf("  $%08X: %ld\n", r << 16, n);
    }

    {   // VRAM snapshot: nonzero span + a coarse 64×24 ASCII view (1bpp
        // assumption is fine for "did the ROM paint anything" checks)
        const uint8_t* vr = mem.vram();
        uint32_t lo = 0xFFFFFFFF, hi = 0;
        for (uint32_t i = 0; i < Q605Memory::kVramSize; i++)
            if (vr[i]) { if (i < lo) lo = i; hi = i; }
        if (hi) {
            std::printf("-- VRAM nonzero span $%05X-$%05X --\n", lo, hi);
            std::map<uint8_t, long> hist;
            for (uint32_t i = 0; i < Q605Memory::kVramSize; i++) hist[vr[i]]++;
            std::vector<std::pair<long, int>> hv;
            for (auto& [b, n] : hist) hv.push_back({n, b});
            std::sort(hv.rbegin(), hv.rend());
            std::printf("  top bytes:");
            for (size_t i = 0; i < hv.size() && i < 8; i++)
                std::printf(" $%02X:%ld", hv[i].second, hv[i].first);
            std::printf("\n  first row: ");
            for (int i = 0; i < 32; i++) std::printf("%02X", vr[i]);
            std::printf("\n  row 100:   ");
            for (int i = 0; i < 32; i++) std::printf("%02X", vr[100*640+i]);
            std::printf("\n");
            for (int row = 0; row < 24; row++) {
                char line[65] = {};
                for (int col = 0; col < 64; col++) {
                    // sample 640x480 @8bpp stride 640: cell = 10x20 px
                    uint32_t px = uint32_t(row * 20) * 640 + col * 10;
                    int n = 0;
                    for (int dy = 0; dy < 20; dy += 4)
                        for (int dx = 0; dx < 10; dx += 2)
                            if (px + dy * 640 + dx < Q605Memory::kVramSize &&
                                vr[px + dy * 640 + dx]) n++;
                    line[col] = n == 0 ? '.' : (n < 8 ? '+' : '#');
                }
                std::printf("  %s\n", line);
            }
        } else std::printf("-- VRAM all zero --\n");
    }

    {   // Full-res 640x480 PPM through the CLUT, so a drawn dialog/screen can
        // be read directly (Read the image). The framebuffer base is taken from
        // ScrnBase (low-mem $0824), which the video driver points into the VRAM
        // window ($F9000000); depth from ScreenRow / bounds heuristics.
        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        auto pk32 = [&](uint32_t a){ return uint32_t(mem.peek8(a))<<24 | uint32_t(mem.peek8(a+1))<<16 |
                                            uint32_t(mem.peek8(a+2))<<8 | mem.peek8(a+3); };
        uint32_t scrnBase = pk32(0x0824);
        // Authoritative geometry from the main GDevice (low-mem MainDevice
        // $08A4 → GDevice → +$16 gdPMap handle → PixMap): baseAddr $00,
        // rowBytes+flags $04, bounds $06, pixelSize $1C.
        uint32_t mainDevH = pk32(0x08A4);
        uint32_t mainDev  = mainDevH ? pk32(mainDevH) : 0;
        uint32_t pmapH    = mainDev ? pk32(mainDev + 0x16) : 0;
        uint32_t pmap     = pmapH ? pk32(pmapH) : 0;
        uint32_t pmBase = 0, pmRow = 0, pmDepth = 0, pmT=0,pmL=0,pmB=0,pmR=0;
        if (pmap) {
            pmBase = pk32(pmap + 0x00);
            pmRow  = (pk32(pmap + 0x04) >> 16) & 0x3FFF;
            pmT = (pk32(pmap+0x06)>>16)&0xFFFF; pmL = pk32(pmap+0x06)&0xFFFF;
            pmB = (pk32(pmap+0x0A)>>16)&0xFFFF; pmR = pk32(pmap+0x0A)&0xFFFF;
            pmDepth = (pk32(pmap+0x1C)>>16)&0xFFFF;
        }
        std::printf("-- ScrnBase($824)=$%08X  MainDevice=$%08X PixMap=$%08X\n",
                    scrnBase, mainDev, pmap);
        std::printf("--   PixMap baseAddr=$%08X rowBytes=%u depth=%u bounds=(%u,%u,%u,%u)\n",
                    pmBase, pmRow, pmDepth, pmT, pmL, pmB, pmR);
        // The framebuffer base is either the physical VRAM window
        // ($F9000000 + off) or a logical MMU alias of it ($5190xxxx, the base
        // Mac OS 8.1 hands QuickDraw in 32-bit mode). The aperture is VRAM-
        // size aligned, so the low log2(kVramSize) bits are the VRAM byte
        // offset either way — masking skips the leading offscreen scratch
        // ("same"/"diff" at VRAM 0) that otherwise shows as a white band atop
        // the screen.
        uint32_t fbSrc = pmBase ? pmBase : scrnBase;
        uint32_t fbOff = fbSrc & (Q605Memory::kVramSize - 1);
        uint32_t fbBase = 0xF9000000 + fbOff;
        if (uint64_t(fbOff) + uint64_t((pmB > pmT ? pmB - pmT : 480)) *
                (pmRow ? pmRow : 1024) > Q605Memory::kVramSize) { fbOff = 0; fbBase = 0xF9000000; }
        // Screen bounds/base come from the PixMap, but depth and stride are
        // hardware state. During SetDepth the PixMap can lag the DAFB writes.
        uint32_t w = (pmR > pmL && pmR - pmL <= 1600) ? pmR - pmL : 640;
        uint32_t h = (pmB > pmT && pmB - pmT <= 1200) ? pmB - pmT : 480;
        uint32_t depth = mem.dafbDepth();
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8)
            depth = (pmDepth == 1 || pmDepth == 2 || pmDepth == 4 || pmDepth == 8)
                  ? pmDepth : 1;
        uint32_t minStride = (w * depth + 7) / 8;
        uint32_t stride = mem.dafbStride();
        if (stride < minStride || stride > Q605Memory::kVramSize)
            stride = pmRow >= minStride ? pmRow : minStride;
        if (uint64_t(fbOff) + uint64_t(h) * stride > Q605Memory::kVramSize) {
            fbOff = 0;
            fbBase = 0xF9000000;
        }
        std::printf("-- DAFB base=$%05X mode=%u depth=%u stride=%u\n",
                    mem.dafbBase(), mem.dafbMode(), mem.dafbDepth(), mem.dafbStride());
        std::printf("-- render fbBase=$%08X off=$%05X %ux%u depth=%u stride=%u bytes:",
                    fbBase, fbOff, w, h, depth, stride);
        for (int i = 0; i < 16; i++) std::printf(" %02X", vr[fbOff + i]);
        std::printf("\n");
        if (depth == 1) {
            FILE* mf = std::fopen("q605_boot_1bpp.pbm", "wb");
            if (mf) {
                std::fprintf(mf, "P4\n%u %u\n", w, h);
                for (uint32_t y = 0; y < h; y++)
                    for (uint32_t xb = 0; xb < (w + 7) / 8; xb++)
                        std::fputc(vr[fbOff + y * stride + xb], mf);
                std::fclose(mf);
                std::printf("-- wrote q605_boot_1bpp.pbm (%ux%u stride %u) --\n",
                            w, h, stride);
            }
        }
        // P6 screenshot at the active indexed depth, through the Antelope CLUT.
        FILE* pf = std::fopen("q605_boot.ppm", "wb");
        if (pf) {
            std::fprintf(pf, "P6\n%u %u\n255\n", w, h);
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++) {
                    uint8_t b = vr[fbOff + y * stride + x * depth / 8];
                    uint8_t pen;
                    if (depth == 1) pen = (b >> (7 - (x & 7))) & 1;
                    else if (depth == 2) pen = (b >> (6 - 2 * (x & 3))) & 3;
                    else if (depth == 4) pen = (x & 1) ? (b & 0x0F) : (b >> 4);
                    else pen = b;
                    const uint8_t* c = cl[pen];
                    std::fputc(c[0], pf); std::fputc(c[1], pf); std::fputc(c[2], pf);
                }
            std::fclose(pf);
            std::printf("-- wrote q605_boot.ppm (%ux%u %ubpp stride %u via CLUT) --\n",
                        w, h, depth, stride);
        }
        FILE* rf = std::fopen("q605_vram.raw", "wb");
        if (rf) { std::fwrite(vr, 1, Q605Memory::kVramSize, rf); std::fclose(rf);
                  std::printf("-- wrote q605_vram.raw (1MB) --\n"); }
    }

    std::printf("-- last %zu PCs --\n", ring.size());
    char da[128];
    for (size_t i = 0; i < ring.size(); i++) {
        uint32_t pc = ring[(rp + i) % ring.size()];
        if (!pc) continue;
        try { cpu.disassemble(da, pc); }
        catch (...) { std::snprintf(da, sizeof da, "<dasm fault>"); }
        std::printf("  $%08X  %s\n", pc, da);
    }

    std::printf("-- SCSI: reads=%ld writes=%ld selects=%ld commands=%ld dmaBytes=%ld lastCmd=$%02X --\n",
                mem.scsi().reads, mem.scsi().writes, mem.scsi().selects,
                mem.scsi().commands, mem.scsi().dmaBytes, mem.scsi().lastCmd);
    std::printf("-- ASC-IOSB: version=$%02X FIFO bytes fed A=%ld B=%ld --\n",
                mem.asc().read(0x800), ascFifoWrites[0], ascFifoWrites[1]);
    if (!noPram) mem.cuda().savePram("q605_trace.pram");
    return 0;
}
