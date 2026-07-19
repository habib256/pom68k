// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// q605_trace — LC 475 / Quadra 605 ROM boot trace (Q5 dev tool, the
// lcii_trace pattern): runs the real FF7439EE ROM on the Q605 machine
// and reports PC coverage, the exception-vector histogram, bus-error
// sites and the first I/O accesses. Not a CTest gate.
//
// Usage: q605_trace <rom> [--cycles N] [--io N] [--berr N] [--pcring N]
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
    if (argc < 2) { std::fprintf(stderr, "usage: %s <rom> [--cycles N] [--io N] [--berr N] [--pcring N]\n", argv[0]); return 2; }
    long long cycles = 200000000;   // 8 machine-seconds at 25 MHz
    int ioMax = 60, berrMax = 40; size_t pcRing = 32;
    uint32_t stopAt = 0, watch = 0, wwatch = 0, firstpc = 0, complog = 0, dumpAt = 0;
    long stopSkip = 0;                 // ignore the first N hits of --stop-at
    std::string diskPath;             // --disk / --scsi: boot image at ID 0
    long long scsiFrom = 0, scsiTo = 0;   // SCSI register-trace clock window
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = atoll(argv[++i]);
        else if (a == "--io" && i + 1 < argc) ioMax = atoi(argv[++i]);
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
    if (mem.cuda().loadPram("q605_trace.pram"))
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

    int ioSeen = 0, berrSeen = 0;
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
                static long hn = 0;
                if (hn++ < 30)
                    std::printf("  SCSIISR clk=%lld A0=$%08X hdlr=*(A0+F0)=$%08X via2IFR=$%02X via2IER=$%02X scsiIRQ=%d sr=$%04X\n",
                                (long long)cpu.getClock(), cpu.getA(0),
                                uint32_t(mem.peek8(cpu.getA(0)+0xF0))<<24 | uint32_t(mem.peek8(cpu.getA(0)+0xF1))<<16 |
                                uint32_t(mem.peek8(cpu.getA(0)+0xF2))<<8 | mem.peek8(cpu.getA(0)+0xF3),
                                mem.via2Ifr(), mem.via2Ier(), mem.scsi().irq(), cpu.getSR());
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
        // be read directly (Read the image). 8bpp, stride 640.
        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        FILE* pf = std::fopen("q605_boot.ppm", "wb");
        if (pf) {
            std::fprintf(pf, "P6\n640 480\n255\n");
            for (uint32_t i = 0; i < 640u * 480u; i++) {
                const uint8_t* c = cl[vr[i]];
                std::fputc(c[0], pf); std::fputc(c[1], pf); std::fputc(c[2], pf);
            }
            std::fclose(pf);
            std::printf("-- wrote q605_boot.ppm (640x480 via CLUT) --\n");
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
    mem.cuda().savePram("q605_trace.pram");
    return 0;
}
