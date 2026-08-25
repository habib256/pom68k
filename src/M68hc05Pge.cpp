// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// PG&E interpreter — see M68hc05Pge.h for the oracle map. The M6805
// opcode core is the M68hc05 (E1) one with 16-bit addressing and the
// PGE stack window; the peripheral bus is entirely PGE. Deliberate
// clone, not a subclass — rationale in the header.

#include "M68hc05Pge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

// MAME m6805.cpp s_hc_cycles — 0 marks an undefined opcode.
const uint8_t M68hc05Pge::kCycles[256] = {
    /*0*/ 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    /*1*/ 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    /*2*/ 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    /*3*/ 5,0,0,5,5,0,5,5,5,5,5,0,5,4,0,5,
    /*4*/ 3,0,11,3,3,0,3,3,3,3,3,0,3,3,0,3,
    /*5*/ 3,0,0,3,3,0,3,3,3,3,3,0,3,3,0,3,
    /*6*/ 6,0,0,6,6,0,6,6,6,6,6,0,6,5,0,6,
    /*7*/ 5,0,0,5,5,0,5,5,5,5,5,0,5,4,0,5,
    /*8*/ 9,6,0,10,0,0,0,0,0,0,0,0,0,0,2,2,
    /*9*/ 0,0,0,0,0,0,0,2,2,2,2,2,2,2,0,2,
    /*A*/ 2,2,2,2,2,2,2,0,2,2,2,2,0,6,2,0,
    /*B*/ 3,3,3,3,3,3,3,4,3,3,3,3,2,5,3,4,
    /*C*/ 4,4,4,4,4,4,4,5,4,4,4,4,3,6,4,5,
    /*D*/ 5,5,5,5,5,5,5,6,5,5,5,5,4,7,5,6,
    /*E*/ 4,4,4,4,4,4,4,5,4,4,4,4,3,6,4,5,
    /*F*/ 3,3,3,3,3,3,3,4,3,3,3,3,2,5,3,4,
};

bool M68hc05Pge::loadBootRom(const std::vector<uint8_t>& data) {
    if (data.size() != sizeof(bootRom_)) return false;   // 512 B
    std::memcpy(bootRom_, data.data(), sizeof(bootRom_));
    romLoaded_ = true;
    return true;
}

void M68hc05Pge::reset() {
    a_ = x_ = 0;
    sp_ = 0xFF;
    cc_ = CC_I;
    std::memset(ports_, 0, sizeof(ports_));
    std::memset(ddrs_, 0, sizeof(ddrs_));
    pllCtrl_ = 0;
    option_ = 0x80;                                  // internal ROM banked in
    cscr_ = 0x01;
    cpicsr_ = kcsr_ = adcsr_ = tbcs_ = 0;
    spcr_ = spsr_ = 0; spiIn_ = spiOut_ = 0; spiBit_ = 0;
    spiClk_ = spiMiso_ = false; spiEdgeAcc_ = 0;
    adbcr_ = 0; adbsr_ = 0x80; adbdr_ = 0;           // transmitter empty
    adbTimerAcc_ = 0; adbTimerMode_ = -1;
    pwmacr_ = pwma0_ = pwma1_ = pwmbcr_ = pwmb0_ = pwmb1_ = 0;
    plmcr_ = plmt1_ = plmt2_ = 0;
    pending_ = 0; waiting_ = false; spin_ = 0;
    illegal_ = false; illegalPc_ = 0; illegalOp_ = 0;
    cycles_ = 0; secAcc_ = cpiAcc_ = 0;
    keyscanAcc_ = 0; keyscanPeriod_ = 0;
    pc_ = read16(0xFFFE);                            // reset vector
}

// MAME-parity audit §2.12 (cosmetic, DOCUMENT-SKIP 2026-08-06 — and the
// claim shrank on re-check). The audit flagged "external /IRQ latch not
// cancelled on deassert". Verified against master: MAME does exactly the
// same. m6805_base_device::execute_set_input (m6805.cpp:576-587) only ever
// ORs the pending bit in on a transition to non-CLEAR and never clears it
// on the way down — the latch is released when the vector is TAKEN, in
// both models. So there is no divergence left to align, only an
// unmodelled release path shared with MAME. It is moot on this platform
// regardless: nothing in src/ calls setIrqLine() on the PG&E — the Duo's
// /IRQ pin is not wired to anything yet (the BORG causes we do model
// arrive as INT_ADB / INT_RTI / INT_CPI / INT_SPI).
// Reopening condition: whoever wires the /IRQ pin (sleep/wake, milestone
// per docs/DUO_BRINGUP.md) owns deciding whether the shipped firmware
// needs a level-sensitive release; the answer is not MAME's, since MAME
// leaves the pin unwired too (macpwrbkmsc.cpp binds no IRQ source).
void M68hc05Pge::setIrqLine(bool asserted) {
    if (asserted && !irqLine_) pending_ |= INT_IRQ;
    irqLine_ = asserted;
    if (asserted) waiting_ = false;
}

uint8_t M68hc05Pge::portsIn(int p) {
    uint8_t in = readPort ? readPort(p) : 0xFF;
    in &= uint8_t(~ddrs_[p]);
    in |= ports_[p] & ddrs_[p];
    return in;
}

void M68hc05Pge::sendPort(int p, uint8_t data) {
    if (writePort) writePort(p, data);
}

void M68hc05Pge::updateAdbIrq() {
    const uint8_t irqs = 0xE8;                       // TDRE|TC|SRQ|RDRF
    if (adbsr_ & adbcr_ & irqs) { pending_ |= INT_ADB; waiting_ = false; }
    else pending_ &= uint8_t(~INT_ADB);
}

// One SPI clock edge (m68hc05pge.cpp spi_tick, master mode). 16 edges per
// byte; the pin callbacks are the MSC VIA1 shifter on the other end.
void M68hc05Pge::spiEdge() {
    const bool phase = (spcr_ >> 2) & 1;
    if (!(spiBit_ & 1)) {                            // first edge of the bit
        if (!phase) {
            if (spiMosi) spiMosi((spiOut_ >> 7) & 1);
            spiOut_ = uint8_t(spiOut_ << 1);
        }
        spiClk_ = !spiClk_;
        if (spiClock) spiClock(spiClk_);
        if (!phase) spiIn_ = uint8_t((spiIn_ << 1) | (spiMiso_ ? 1 : 0));
    } else {                                         // second edge
        if (phase) {
            if (spiMosi) spiMosi((spiOut_ >> 7) & 1);
            spiOut_ = uint8_t(spiOut_ << 1);
        }
        spiClk_ = (spcr_ >> 3) & 1;                  // back to idle polarity
        if (spiClock) spiClock(spiClk_);
        if (phase) spiIn_ = uint8_t((spiIn_ << 1) | (spiMiso_ ? 1 : 0));
    }
    spiBit_--;
    if (spiBit_ <= 0) {
        spiRing_[spiRingPos_] = uint16_t((spiOutLatch_ << 8) | spiIn_);
        spiRingPos_ = (spiRingPos_ + 1) & 63;
        // POM68K_PGE_TRAP=<hexbyte>: when the PMU RECEIVES that byte over
        // SPI, dump what the firmware was doing at that instant — its PC
        // ring, the interrupt mask and the pending set. The question this
        // exists for: does a host command land while the firmware has
        // interrupts masked or is busy inside another handler, and get
        // dropped?
        static const char* trapEnv = std::getenv("POM68K_PGE_TRAP");
        if (trapEnv) {
            static const unsigned trapByte =
                unsigned(strtoul(trapEnv, nullptr, 16)) & 0xFF;
            if (spiIn_ == trapByte) {
                static long tn = 0;
                if (tn++ < 4000) {
                    std::fprintf(stderr,
                                 "pge TRAP $%02X #%ld cyc=%lld pc=$%04X cc=$%02X"
                                 " pending=$%02X adbcr=$%02X adbsr=$%02X"
                                 " spcr=$%02X waiting=%d\n",
                                 spiIn_, tn, (long long)cycles_, pc_, cc_,
                                 pending_, adbcr_, adbsr_, spcr_, waiting_);
                    std::fprintf(stderr, "  mcu PCs:");
                    for (int i = 0; i < 24; i++)
                        std::fprintf(stderr, " %04X", pcHistory(i));
                    std::fprintf(stderr, "\n");
                }
            }
        }
        // POM68K_PGE_SPIBYTES=1: every completed exchange. The question it
        // exists to answer is whether a host PmgrOp actually reaches the
        // PMU over the wire, or dies in the transport.
        static const bool sb = std::getenv("POM68K_PGE_SPIBYTES") != nullptr;
        if (sb) {
            static long n = 0;
            if (n++ < 400000)
                std::fprintf(stderr, "spi: out=$%02X in=$%02X cyc=%lld\n",
                             spiOutLatch_, spiIn_, (long long)cycles_);
        }
        spsr_ |= 0x80;                               // SPSR_IRQ_FLAG
        if (spcr_ & 0x80) { pending_ |= INT_SPI; waiting_ = false; }
    }
}

// ── PGE on-chip bus (m68hc05pge.cpp:339-364 m68hc05pge_map) ────────────
uint8_t M68hc05Pge::read8(uint16_t addr) {
    if (addr >= 0x8000) {                            // SRAM / banked boot ROM
        if ((option_ & 0x80) && addr >= 0xFE00)
            return bootRom_[addr - 0xFE00];
        if ((cscr_ & 0x20) && !(option_ & 0x40))
            return sram_[addr - 0x8000];
        return 0xFF;
    }
    if (addr >= 0x0040 && addr <= 0x03FF) return ram_[addr - 0x40];
    switch (addr) {
    case 0x00: case 0x01: case 0x02: case 0x03:
        return portsIn(addr);                        // ports A-D
    case 0x04: case 0x05: case 0x06:
        return ddrs_[addr - 4];                      // DDR A-C
    case 0x07: return pllCtrl_;
    case 0x0A: return spcr_;
    case 0x0B: return spsr_;
    case 0x0C: {                                     // SPDR
        static const bool spiTrace = std::getenv("POM68K_PGE_TRACE") != nullptr;
        if (spiTrace) {
            static long n = 0;
            if (n++ < 2000)
                std::fprintf(stderr, "pge: SPI recv $%02X pc=$%04X\n",
                             spiIn_, pc_);
        }
        if (spsr_ & 0x80) pending_ &= uint8_t(~INT_SPI);
        return spiIn_;
    }
    case 0x0D: return cpicsr_;
    case 0x0E: return cscr_;
    case 0x0F: {                                     // KCSR: read acks IRQs
        const uint8_t r = kcsr_;
        if (kcsr_ & 0xC0) pending_ &= uint8_t(~INT_KEY);
        kcsr_ &= 0x3F;
        return r;
    }
    case 0x14: {                                     // TBCS: bit 7 = button
        // (active low: 1 = not pressed — trackball_r "button not pressed")
        const uint8_t v = uint8_t(((tbButton && tbButton()) ? 0x00 : 0x80) | tbcs_);
        if (tbTrace_) {
            static long n = 0;
            if ((n++ % 1000) == 0)
                std::fprintf(stderr, "[tb] TBCS -> $%02X (read #%ld)\n", v, n);
        }
        return v;
    }
    case 0x15: {
        const uint8_t v = tbX ? tbX() : 0;
        static uint8_t last = 0;
        if (tbTrace_ && v != last)
            std::fprintf(stderr, "[tb] X -> %d\n", int(int8_t(v)));
        last = v;
        return v;
    }
    case 0x16: {
        const uint8_t v = tbY ? tbY() : 0;
        static uint8_t last = 0;
        if (tbTrace_ && v != last)
            std::fprintf(stderr, "[tb] Y -> %d\n", int(int8_t(v)));
        last = v;
        return v;
    }
    case 0x18: return adbcr_;
    case 0x19: return adbsr_;
    case 0x1A:                                       // ADBDR
        // Reading the data register consumes a received byte: RDRF clears
        // (and with it the ADB interrupt it was raising).
        if (adbsr_ & 0x08) {
            adbsr_ &= uint8_t(~0x08);
            updateAdbIrq();
        }
        return adbdr_;
    case 0x1C: return option_;
    case 0x1D: return adcIn ? adcIn(adcsr_ & 0x0F) : 0xFF;
    case 0x1E: return adcsr_;
    case 0x2D: return pwmacr_;
    case 0x2E: return pwma0_;
    case 0x2F: return pwma1_;
    case 0x30: return pwmbcr_;
    case 0x31: return pwmb0_;
    case 0x32: return pwmb1_;
    case 0x34: return plmcr_;
    case 0x35: return plmt1_;
    case 0x36: return plmt2_;
    case 0x38: return uint8_t(rtc_ >> 24);
    case 0x39: return uint8_t(rtc_ >> 16);
    case 0x3A: return uint8_t(rtc_ >> 8);
    case 0x3B: return uint8_t(rtc_);
    default: break;
    }
    // Ports E-L + DDRs, $20-$2C (ports_high_r).
    if (addr >= 0x20 && addr <= 0x2C) {
        const int off = addr - 0x20;
        switch (off) {
        case 0x0: case 0x2: case 0x4: case 0x6: case 0x8:
            return portsIn((off >> 1) + E);
        case 0xA: return portsIn(L);
        case 0xC: return portsIn(K);
        case 0x1: case 0x3: case 0x5: case 0x7: case 0x9:
            return ddrs_[(off >> 1) + E];
        case 0xB: return ddrs_[L];
        default: break;
        }
    }
    return 0;
}

void M68hc05Pge::write8(uint16_t addr, uint8_t v) {
    if (addr >= 0x8000) {
        // The boot ROM overlays READS at $FE00-$FFFF only: MAME's view[1]
        // installs .rom() over the sram .rw() entry, which replaces the
        // read handler and leaves writes flowing to sram_w — so the
        // firmware upload CAN fill the vector page before banking the
        // boot ROM out. Blocking those writes left $FFFx zero and every
        // post-bank-out interrupt vectored to $0000 (the first PGE crash).
        if ((cscr_ & 0x20) && !(option_ & 0x40))
            sram_[addr - 0x8000] = v;
        return;
    }
    if (addr >= 0x0040 && addr <= 0x03FF) { ram_[addr - 0x40] = v; return; }
    if (addr == 0x0FF0) return;                      // watchdog reset
    switch (addr) {
    case 0x00: case 0x01: case 0x02: case 0x03: {    // ports A-D
        const int p = addr;
        sendPort(p, uint8_t((v & ddrs_[p]) | (pullups_[p] & ~ddrs_[p])));
        ports_[p] = v;
        return;
    }
    case 0x04: case 0x05: case 0x06: {               // DDR A-C
        const int p = addr - 4;
        sendPort(p, uint8_t((ports_[p] & v) | (pullups_[p] & ~v)));
        ddrs_[p] = v;
        return;
    }
    case 0x07: pllCtrl_ = v; return;
    case 0x0A:                                       // SPCR
        spcr_ = v;
        spiClk_ = (v >> 3) & 1;                      // idle polarity
        if (spiClock) spiClock(spiClk_);
        return;
    case 0x0C: {                                     // SPDR: start transfer
        static const bool spiTrace = std::getenv("POM68K_PGE_TRACE") != nullptr;
        if (spiTrace) {
            static long n = 0;
            if (n++ < 2000)
                std::fprintf(stderr, "pge: SPI send $%02X (spcr $%02X) pc=$%04X\n",
                             v, spcr_, pc_);
        }
        if (!(spcr_ & 0x10))
            std::fprintf(stderr, "pge: SPI slave mode not implemented\n");
        spiTransfers++;
        spsr_ &= 0x7F;
        pending_ &= uint8_t(~INT_SPI);
        spiClk_ = (spcr_ >> 3) & 1;
        if (spiClock) spiClock(spiClk_);
        spiOut_ = v;
        spiOutLatch_ = v;
        spiIn_ = 0;
        spiBit_ = 16;
        spiEdgeAcc_ = 0;
        return;
    }
    case 0x0D:                                       // CPICSR
        cpicsr_ = v;
        if (!(v & 0x40)) pending_ &= uint8_t(~INT_CPI);
        if (!(v & 0x80)) pending_ &= uint8_t(~INT_RTI);
        return;
    case 0x0E: cscr_ = v; return;
    case 0x0F:                                       // KCSR
        if ((v & 0x08) && !(kcsr_ & 0x08))           // KSCAN on
            keyscanPeriod_ = (int64_t(1) << (v & 7)) * kHz / 1000000 + 1;
        else if (!(v & 0x08) && (kcsr_ & 0x08))
            keyscanPeriod_ = 0;
        kcsr_ = v;
        return;
    case 0x14: tbcs_ = v & 0x7F; return;
    case 0x18:                                       // ADBCR
        // Clearing TC sets TDRE; clearing TDRE arms the TC timer (50 µs).
        if ((adbcr_ & 0x40) && !(v & 0x40)) adbsr_ |= 0x80;
        if ((adbcr_ & 0x80) && !(v & 0x80)) {
            adbTimerMode_ = 1;
            adbTimerAcc_ = 50 * kHz / 1000000;
        }
        adbcr_ = v;
        updateAdbIrq();
        return;
    case 0x19: adbsr_ = v; updateAdbIrq(); return;
    case 0x1A: {                                     // ADBDR: send byte
        static const bool t = std::getenv("POM68K_PGE_ADBTRACE") != nullptr;
        if (t) {
            static long n = 0;
            if (n++ < 20000)
                std::fprintf(stderr, "adbcell: TX $%02X (cr=$%02X sr=$%02X) "
                             "pc=$%04X cyc=%lld\n", v, adbcr_, adbsr_, pc_,
                             (long long)cycles_);
        }
        adbdr_ = v;
        adbsr_ &= uint8_t(~0xC0);                    // clear TDRE+TC
        adbTimerMode_ = 0;
        adbTimerAcc_ = 1200 * kHz / 1000000;         // byte time 1.2 ms
        return;
    }
    case 0x1C: {                                     // OPTION (banks boot ROM)
        static const bool t = std::getenv("POM68K_PGE_TRACE") != nullptr;
        if (t && ((v ^ option_) & 0x80))
            std::fprintf(stderr, "pge: OPTION $%02X -> $%02X (boot ROM %s) "
                         "pc=$%04X sp=$%02X\n", option_, v,
                         (v & 0x80) ? "IN" : "OUT", pc_, sp_);
        option_ = v;
        return;
    }
    case 0x1E:                                       // ADCSR
        adcsr_ = v;
        if (v & 0x20) adcsr_ |= 0x80;                // conversion "done"
        return;
    case 0x2D: pwmacr_ = v; return;
    case 0x2E: pwma0_ = v; return;
    case 0x2F: pwma1_ = v; return;
    case 0x30: pwmbcr_ = v; return;
    case 0x31: pwmb0_ = v; return;
    case 0x32: pwmb1_ = v; return;
    case 0x34: plmcr_ = v; return;
    case 0x35: plmt1_ = v; return;
    case 0x36: plmt2_ = v; return;
    case 0x38: rtc_ = (rtc_ & 0x00FFFFFF) | (uint32_t(v) << 24); return;
    case 0x39: rtc_ = (rtc_ & 0xFF00FFFF) | (uint32_t(v) << 16); return;
    case 0x3A: rtc_ = (rtc_ & 0xFFFF00FF) | (uint32_t(v) << 8); return;
    case 0x3B: rtc_ = (rtc_ & 0xFFFFFF00) | v; return;
    default: break;
    }
    if (addr >= 0x20 && addr <= 0x2C) {              // ports E-L (ports_high_w)
        const int off = addr - 0x20;
        int p = -1;
        bool isDdr = false;
        switch (off) {
        case 0x0: case 0x2: case 0x4: case 0x6: case 0x8:
            p = (off >> 1) + E; break;
        case 0xA: p = L; break;
        case 0xC: p = K; break;
        case 0x1: case 0x3: case 0x5: case 0x7: case 0x9:
            p = (off >> 1) + E; isDdr = true; break;
        case 0xB: p = L; isDdr = true; break;
        default: return;
        }
        if (isDdr) {
            sendPort(p, uint8_t((ports_[p] & v) | (pullups_[p] & ~v)));
            ddrs_[p] = v;
        } else {
            sendPort(p, uint8_t((v & ddrs_[p]) | (pullups_[p] & ~ddrs_[p])));
            ports_[p] = v;
        }
    }
}

void M68hc05Pge::pushState() {
    push8(uint8_t(pc_));
    push8(uint8_t(pc_ >> 8));
    push8(x_);
    push8(a_);
    push8(uint8_t(cc_ | 0xE0));
}

// Priority walk IRQ > ADB > RTI > CPI > SPI > KEY, vectors $FFFA-(n×2)
// (m68hc05pge.cpp interrupt_vector). Charged 0 cycles like the E1 core —
// same deliberate, gated inaccuracy (see M68hc05.cpp:139-167); revisit
// only with the PGE's own transport gates green.
int M68hc05Pge::serviceInterrupts() {
    if (!pending_) return 0;
    if (cc_ & CC_I) return 0;
    static const uint8_t order[6] = { INT_IRQ, INT_ADB, INT_RTI,
                                      INT_CPI, INT_SPI, INT_KEY };
    for (int i = 0; i < 6; i++) {
        if (pending_ & order[i]) {
            pushState();
            cc_ |= CC_I;
            pending_ &= uint8_t(~order[i]);
            pc_ = read16(uint16_t(0xFFFA - (i << 1)));
            waiting_ = false;
            return 0;
        }
    }
    return 0;
}

uint8_t M68hc05Pge::aluRmw(int op, uint8_t v) {
    uint8_t r = v;
    switch (op) {
        case 0x0:
            r = uint8_t(-v);
            cc_ = uint8_t((cc_ & ~CC_C) | (v ? CC_C : 0));
            setNZ(r);
            break;
        case 0x3: r = uint8_t(~v); cc_ |= CC_C; setNZ(r); break;
        case 0x4:
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = v >> 1; setNZ(r);
            break;
        case 0x6: {
            uint8_t cin = uint8_t((cc_ & CC_C) << 7);
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = uint8_t((v >> 1) | cin); setNZ(r);
            break;
        }
        case 0x7:
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = uint8_t((v >> 1) | (v & 0x80)); setNZ(r);
            break;
        case 0x8:
            cc_ = uint8_t((cc_ & ~CC_C) | (v >> 7));
            r = uint8_t(v << 1); setNZ(r);
            break;
        case 0x9: {
            uint8_t cin = cc_ & CC_C;
            cc_ = uint8_t((cc_ & ~CC_C) | (v >> 7));
            r = uint8_t((v << 1) | cin); setNZ(r);
            break;
        }
        case 0xA: r = uint8_t(v - 1); setNZ(r); break;
        case 0xC: r = uint8_t(v + 1); setNZ(r); break;
        case 0xD: setNZ(v); break;
        case 0xF: r = 0; cc_ = uint8_t((cc_ & ~CC_N) | CC_Z); break;
        default: break;
    }
    return r;
}

void M68hc05Pge::aluOp(int op, uint16_t ea, bool imm) {
    switch (op) {
        case 0x7: write8(ea, a_); setNZ(a_); return;             // STA
        case 0xF: write8(ea, x_); setNZ(x_); return;             // STX
        case 0xC: pc_ = ea; return;                              // JMP
        case 0xD:                                                // JSR
            push8(uint8_t(pc_));
            push8(uint8_t(pc_ >> 8));
            pc_ = ea;
            return;
        default: break;
    }
    uint8_t m = imm ? uint8_t(ea) : read8(ea);
    switch (op) {
        case 0x0: {
            int r = a_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0x1: {
            int r = a_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            setNZ(uint8_t(r));
            break;
        }
        case 0x2: {
            int r = a_ - m - (cc_ & CC_C);
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0x3: {
            int r = x_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            setNZ(uint8_t(r));
            break;
        }
        case 0x4: a_ &= m; setNZ(a_); break;
        case 0x5: setNZ(uint8_t(a_ & m)); break;
        case 0x6: a_ = m; setNZ(a_); break;
        case 0x8: a_ ^= m; setNZ(a_); break;
        case 0x9: {
            int c = cc_ & CC_C;
            int r = a_ + m + c;
            cc_ = uint8_t((cc_ & ~(CC_H | CC_C))
                          | (((a_ & 0xF) + (m & 0xF) + c) > 0xF ? CC_H : 0)
                          | (r > 0xFF ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0xA: a_ |= m; setNZ(a_); break;
        case 0xB: {
            int r = a_ + m;
            cc_ = uint8_t((cc_ & ~(CC_H | CC_C))
                          | (((a_ & 0xF) + (m & 0xF)) > 0xF ? CC_H : 0)
                          | (r > 0xFF ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0xE: x_ = m; setNZ(x_); break;
        default: break;
    }
}

int M68hc05Pge::execOne() {
    pcRing_[pcRingPos_] = pc_;
    pcRingPos_ = (pcRingPos_ + 1) & 63;
    // POM68K_PGE_PCCOUNT="hex,hex,…": execution counts + first hits for
    // firmware PCs — the DUO_PCCOUNT idea, MCU side. Diagnostic only.
    {
        static std::vector<std::pair<uint16_t, long>>* watch = [] {
            auto* v = new std::vector<std::pair<uint16_t, long>>;
            if (const char* e = std::getenv("POM68K_PGE_PCCOUNT"))
                while (*e) {
                    v->push_back({uint16_t(strtoul(e, nullptr, 16)), 0});
                    const char* c = strchr(e, ',');
                    if (!c) break;
                    e = c + 1;
                }
            return v;
        }();
        // POM68K_PGE_PCWIN="lo,hi[;lo,hi…]": inside those MCU-cycle
        // windows, print EVERY hit — the interleave is the point there,
        // not the count.
        static const auto wins = [] {
            auto* v = new std::vector<std::pair<long long, long long>>;
            if (const char* e = std::getenv("POM68K_PGE_PCWIN"))
                while (*e) {
                    long long lo = atoll(e), hi = -1;
                    if (const char* c = strchr(e, ',')) hi = atoll(c + 1);
                    v->push_back({lo, hi});
                    if (!(e = strchr(e, ';'))) break;
                    e++;
                }
            return v;
        }();
        bool inWin = false;
        for (auto& [lo, hi] : *wins)
            if (cycles_ >= lo && cycles_ <= hi) { inWin = true; break; }
        // POM68K_PGE_PCHIST="lo,hi": 256-byte-bucket histogram of executed
        // PCs inside that MCU-cycle window, dumped at destruction — where
        // does a starved stretch actually spend its cycles?
        static const auto hist = [] {
            struct H {
                long long lo = -1, hi = -1;
                std::vector<long> buckets = std::vector<long>(256, 0);
                ~H() {
                    if (lo < 0) return;
                    std::fprintf(stderr, "-- PGE pc histogram [%lld,%lld] --\n",
                                 lo, hi);
                    for (int i = 0; i < 256; i++)
                        if (buckets[size_t(i)])
                            std::fprintf(stderr, "  $%02XXX: %ld\n", i,
                                         buckets[size_t(i)]);
                }
            };
            static H h;
            if (const char* e = std::getenv("POM68K_PGE_PCHIST")) {
                h.lo = atoll(e);
                if (const char* c = strchr(e, ',')) h.hi = atoll(c + 1);
            }
            return &h;
        }();
        if (hist->lo >= 0 && cycles_ >= hist->lo && cycles_ <= hist->hi)
            hist->buckets[pc_ >> 8]++;
        for (auto& [wpc, n] : *watch)
            if (pc_ == wpc) {
                n++;
                if (inWin || n <= 12 || (n & (n - 1)) == 0)
                    std::fprintf(stderr, "pgepc $%04X #%ld cyc=%lld cc=$%02X"
                                 " a=$%02X x=$%02X\n",
                                 wpc, n, (long long)cycles_, cc_, a_, x_);
            }
    }
    uint8_t op = fetch8();
    int cyc = kCycles[op];
    if (cyc == 0) {
        illegal_ = true;
        illegalPc_ = uint16_t(pc_ - 1);
        illegalOp_ = op;
        return 2;
    }
    instructions++;
    const int hi = op >> 4, lo = op & 0xF;

    switch (hi) {
        case 0x0: {                                  // BRSET/BRCLR
            int bit = lo >> 1;
            uint8_t m = read8(fetch8());
            int8_t rel = int8_t(fetch8());
            bool set = (m >> bit) & 1;
            cc_ = uint8_t((cc_ & ~CC_C) | (set ? CC_C : 0));
            bool take = (lo & 1) ? !set : set;
            if (take) pc_ = uint16_t(pc_ + rel);
            break;
        }
        case 0x1: {                                  // BSET/BCLR
            int bit = lo >> 1;
            uint16_t ea = fetch8();
            uint8_t m = read8(ea);
            if (lo & 1) m &= uint8_t(~(1u << bit));
            else        m |= uint8_t(1u << bit);
            write8(ea, m);
            break;
        }
        case 0x2: {                                  // Bcc
            int8_t rel = int8_t(fetch8());
            const bool Cf = cc_ & CC_C, Z = cc_ & CC_Z, N = cc_ & CC_N;
            const bool Hf = cc_ & CC_H, If = cc_ & CC_I;
            bool take = false;
            switch (lo) {
                case 0x0: take = true; break;
                case 0x1: take = false; break;
                case 0x2: take = !(Cf || Z); break;
                case 0x3: take = Cf || Z; break;
                case 0x4: take = !Cf; break;
                case 0x5: take = Cf; break;
                case 0x6: take = !Z; break;
                case 0x7: take = Z; break;
                case 0x8: take = !Hf; break;
                case 0x9: take = Hf; break;
                case 0xA: take = !N; break;
                case 0xB: take = N; break;
                case 0xC: take = !If; break;
                case 0xD: take = If; break;
                case 0xE: take = irqLine_; break;
                case 0xF: take = !irqLine_; break;
            }
            if (take) pc_ = uint16_t(pc_ + rel);
            break;
        }
        case 0x3: {                                  // RMW dir
            uint16_t ea = fetch8();
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);
            break;
        }
        case 0x4:
            if (op == 0x42) {                        // MUL
                uint16_t r = uint16_t(x_) * a_;
                x_ = uint8_t(r >> 8);
                a_ = uint8_t(r);
                cc_ &= uint8_t(~(CC_H | CC_C));
            } else {
                a_ = aluRmw(lo, a_);
            }
            break;
        case 0x5: x_ = aluRmw(lo, x_); break;
        case 0x6: {                                  // RMW ix1
            uint16_t ea = uint16_t(x_ + fetch8());
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);
            break;
        }
        case 0x7: {                                  // RMW ix
            uint16_t ea = x_;
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);
            break;
        }
        case 0x8:
            switch (op) {
                case 0x80:                           // RTI
                    cc_ = pop8();
                    a_ = pop8();
                    x_ = pop8();
                    pc_ = uint16_t(pop8() << 8);
                    pc_ = uint16_t(pc_ | pop8());
                    break;
                case 0x81:                           // RTS
                    pc_ = uint16_t(pop8() << 8);
                    pc_ = uint16_t(pc_ | pop8());
                    break;
                case 0x83:                           // SWI
                    pushState();
                    cc_ |= CC_I;
                    pc_ = read16(0xFFFC);
                    break;
                // MAME-parity audit §2.12 (cosmetic, DOCUMENT-SKIP
                // 2026-08-06): STOP is approximated as WAIT — I cleared,
                // core idle, on-chip timers (RTI, CPI, SPI, the ADB cell)
                // still counting; a true STOP gates the oscillator and
                // freezes them. There is nothing to align to: MAME's
                // handlers for both opcodes are
                // `fatalerror("unimplemented STOP/WAIT")`
                // (6805ops.hxx:527-539), so any Duo firmware reaching $8E
                // kills MAME outright. WAIT is the conservative
                // approximation — a STOPped PMU still wakes on its own
                // timer instead of hanging the machine. Reopen with the
                // sleep/wake milestone (docs/DUO_BRINGUP.md), where "the
                // timers must stop" becomes an actual requirement.
                case 0x8E:                           // STOP
                case 0x8F:                           // WAIT
                    cc_ &= uint8_t(~CC_I);
                    waiting_ = true;
                    break;
            }
            break;
        case 0x9:
            switch (op) {
                case 0x97: x_ = a_; break;
                case 0x98: cc_ &= uint8_t(~CC_C); break;
                case 0x99: cc_ |= CC_C; break;
                case 0x9A: cc_ &= uint8_t(~CC_I); break;
                case 0x9B: cc_ |= CC_I; break;
                case 0x9C: sp_ = 0xFF; break;
                case 0x9D: break;
                case 0x9F: a_ = x_; break;
            }
            break;
        case 0xA:
            if (op == 0xAD) {                        // BSR
                int8_t rel = int8_t(fetch8());
                push8(uint8_t(pc_));
                push8(uint8_t(pc_ >> 8));
                pc_ = uint16_t(pc_ + rel);
            } else {
                aluOp(lo, fetch8(), true);
            }
            break;
        case 0xB: aluOp(lo, fetch8(), false); break;
        case 0xC: aluOp(lo, fetch16(), false); break;
        case 0xD: aluOp(lo, uint16_t(x_ + fetch16()), false); break;
        case 0xE: aluOp(lo, uint16_t(x_ + fetch8()), false); break;
        case 0xF: aluOp(lo, x_, false); break;
    }
    return cyc;
}

int M68hc05Pge::run(int budget) {
    if (!romLoaded_ || illegal_) return budget;
    int used = 0;
    while (used < budget) {
        int cyc = serviceInterrupts();
        if (spin_ > 0) {                             // MAME spin_until_time
            cyc += 4;
            spin_ -= 4;
        } else if (waiting_) {
            cyc += 4;
        } else {
            const int icyc = execOne();
            if (illegal_) { used += cyc + icyc; break; }
            cyc += icyc;
        }
        used += cyc;
        cycles_ += cyc;
        if (onCycles) onCycles(cyc);

        // SPI master shift: one edge per div/2 cycles ({2,4,16,32}/2)
        // — m68hc05pge.cpp spi_tick at clock/divisor Hz.
        if (spiBit_ > 0) {
            static const int kEdge[4] = { 1, 2, 8, 16 };
            spiEdgeAcc_ += cyc;
            const int per = kEdge[spcr_ & 3];
            while (spiEdgeAcc_ >= per && spiBit_ > 0) {
                spiEdgeAcc_ -= per;
                spiEdge();
            }
        }

        // ADB cell timer (TDRE after a byte, TC after clearing TDRE).
        // No external devices are attached, matching MAME's cell: it never
        // raises RDRF. The Duo's built-in keyboard/trackball are separate.
        if (adbTimerMode_ >= 0) {
            adbTimerAcc_ -= cyc;
            if (adbTimerAcc_ <= 0) {
                const int mode = adbTimerMode_;
                adbTimerMode_ = -1;
                adbsr_ |= mode == 0 ? 0x80 : 0x40;   // TDRE / TC
                updateAdbIrq();
            }
        }

        // Fixed timers FREE-RUN from reset (m68hc05pge device_reset —
        // unlike the E1, whose timers arm on first register write).
        secAcc_ += cyc;                              // 1 s → RTC + CPI flag
        if (secAcc_ >= kHz) {
            secAcc_ -= kHz;
            rtc_++;
            cpicsr_ |= 0x40;
            if (cpicsr_ & 0x10) { pending_ |= INT_CPI; waiting_ = false; }
        }
        cpiAcc_ += cyc;                              // 5.86 ms → RTI flag
        if (cpiAcc_ >= kHz * 5860 / 1000000) {
            cpiAcc_ -= kHz * 5860 / 1000000;
            cpicsr_ |= 0x80;
            if (cpicsr_ & 0x20) { pending_ |= INT_RTI; waiting_ = false; }
        }

        // Hardware keyboard scanner (keyscan_tick): sweep the matrix rows
        // through port C, look for any active-low column on ports A/B.
        if (keyscanPeriod_ > 0) {
            keyscanAcc_ += cyc;
            if (keyscanAcc_ >= keyscanPeriod_) {
                keyscanAcc_ = 0;
                kcsr_ |= 0x40;                       // SIF
                for (int row = 0; row < 8; row++) {
                    const uint8_t sel = uint8_t((1u << row) ^ 0xFF);
                    sendPort(C, uint8_t((sel & ddrs_[C]) | (pullups_[C] & ~ddrs_[C])));
                    ports_[C] = sel;
                    if (portsIn(A) != 0xFF || portsIn(B) != 0xFF)
                        kcsr_ |= 0x80;               // KIF
                }
                const uint8_t keyirq = 0xA0, scanirq = 0x50;
                if (((kcsr_ & keyirq) == keyirq) || ((kcsr_ & scanirq) == scanirq)) {
                    pending_ |= INT_KEY;
                    waiting_ = false;
                }
            }
        }
    }
    return used;
}
