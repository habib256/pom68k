// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "V8Memory.h"
#include "Cpu030.h"
#include <cstdio>
#include <cstdlib>

V8Memory::V8Memory(uint32_t totalRam)
    : ram_(totalRam, 0), rom_(kRomSize, 0), vram_(kVramSize, 0),
      totalRam_(totalRam) {
    // Pseudo-VIA machine hooks (v8.cpp:328-352): reg 1 = RAM config
    // (reads back config | 0x04), reg $10 read = monitor sense on bits
    // 3-5, port B bit 3 = HMMU enable (68020 LC only — ignored here).
    pvia_.onConfigRead = [this] { return uint8_t(config_ | 0x04); };
    pvia_.onConfigWrite = [this](uint8_t v) { config_ = v; applyRamConfig(v); };
    pvia_.onVideoRead = [this] { return uint8_t((montype_ << 3) & 0x38); };
    pvia_.onVideoWrite = [this](uint8_t v) { videoConfig_ = v; };
    egret_.setAdbBus(&adb_);
    // ASC IRQ is LEVEL-triggered into pseudo-VIA IFR bit 4 (v8.cpp:119-122)
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    reset();
}

// 512 KB flat image; the stored big-endian word checksum (bytes 4…end)
// is verified against the header — warn-only, so patched/homebrew ROMs
// still load (the real LC II ROM is $35C28F5F, docs/LCII_HARDWARE.md).
bool V8Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;

    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "V8Memory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

void V8Memory::reset() {
    overlay_ = true;
    config_ = 0;
    videoConfig_ = 0;
    sccIrq_ = false;
    scc_.reset();
    scc_.setAbortIdle(true);                 // no LocalTalk peer (O6.10)
    lapHeldCycles_ = 0;
    lapWatchdog_ = getenv("POM68K_NO_LTALK_WD") == nullptr;
    viaPhase_ = 0;
    tickAcc_ = 0;
    tickAcc60_ = 0;
    simmMapped_ = mbMapped_ = false;         // no RAM until the overlay drops
    via_.reset();
    pvia_.reset();
    ariel_.reset();
    egret_.reset();
    egret_.factoryDefaults();                // SPConfig XPRAM $13 = $22
    adb_.reset();
    asc_.reset();
    scsi_.reset();
    iwm_.reset();
    iwm_.attachDrive(&drive_, nullptr);
    framePos_ = 0;
    vblState_ = false;
    scsiStallFrames_ = 0;
    lastScsiCmds_ = -1;
    alertDismissPosts_ = 0;
    alertDismissCool_ = 0;
    alertEvSlot_ = 0;
    // VIA1 port A input = V8-family machine ID $D4 | diag bit
    // (v8.cpp:249-252); PB3 = Egret XCVR_SESSION, idle high. PB0-PB2
    // (legacy RTC lines) and PB6-PB7 keep the 6522 pull-up default 1
    // (review 2026-07-16: they read 0 before, incl. to the ROM's
    // old-clock probe). PB4/PB5 (VIA_FULL/SYS_SESSION) are HOST-driven
    // handshake lines and must idle LOW here: portB() is fed into
    // Egret::portBChanged, whose HLE is edge-triggered — pulled-up 1s
    // while DDRB is still 0 at reset read as a phantom session rise and
    // wedge the transport (validated: pull-ups on PB4/PB5 black-screen
    // the boot etalon).
    via_.setInA(0xD5);
    via_.setInB(uint8_t(0xC7 | (egret_.xcvrSession() << 3)));
}

// MAME v8.cpp:354-422 ram_size(). SIMM bank (bank A) always maps at 0,
// motherboard (bank B) after the CONFIGURED SIMM size; the first 2 MB of
// motherboard RAM are always aliased at $800000 (handled in ramIndex).
// ram_ layout mirrors MAME's contiguous RAM device: motherboard at
// offset 0, SIMM at +4 MB — except the 10 MB config, where the SIMM is
// 8 MB at +2 MB and the soldered bank's upper 2 MB are the wasted ones
// (v8.cpp:363-369).
void V8Memory::applyRamConfig(uint8_t config) {
    if (overlay_) return;

    simmPhys_ = totalRam_ > kMbRamSize ? totalRam_ - kMbRamSize : 0;
    simmOff_ = kMbRamSize;
    if (totalRam_ == 0xA00000) { simmPhys_ = 0x800000; simmOff_ = 0x200000; }

    simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;

    static constexpr uint32_t kSimmCfg[4] = { 0, 0x200000, 0x400000, 0x800000 };
    mbLoc_ = simmMapped_ ? kSimmCfg[(config >> 6) & 3] : 0;

    mbMapped_ = (config & 0xC0) != 0xC0;     // 8 MB SIMM ⇒ only the $800000 alias
    mbSize_ = (config & 0x20) ? 0x200000 : kMbRamSize;
}


void V8Memory::busError() const {
    // Machine-less access (unit tests poking the map): open bus would be
    // silent — throw via the CPU when wired, else report loudly.
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int V8Memory::iplLevel() const {
    if (sccIrq_) return 4;                   // v8.cpp:287-315
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void V8Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// v8.cpp:462-483 — the CPU is stalled to the 783.36 kHz VIA E-clock on
// every VIA1 access: start at via_cycle+1, end half a VIA cycle later.
// With cpuClk/viaClk = 20: main = (via_cycle*2 + 3)*10 + 1.
void V8Memory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->getClock();
    int64_t viaCycle = c / 20;
    int64_t target = (viaCycle * 2 + 3) * 10 + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t V8Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();            // word path skips read8's flush
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride (v8.cpp:434-460)
    if (write) {
        via_.write(reg, v);
        // Port B outputs carry the Egret handshake (PB4 VIA_FULL,
        // PB5 SYS_SESSION, maclc.cpp:425-433)
        if (reg == Via6522::ORB || reg == Via6522::DDRB)
            egret_.portBChanged(via_.portB());
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)                 // PB3 = XCVR_SESSION, live
        via_.setInB(uint8_t(0xC7 | (egret_.xcvrSession() << 3)));
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t V8Memory::read8(uint32_t addr) {
    addr &= 0x80FFFFFF;                      // A31 + A23-A0 (maclc.cpp:181)
    if (addr & 0x80000000) busError();       // PDS slot $E: no card

    if (addr < 0xA00000) {                   // RAM space (ROM under overlay)
        if (overlay_) {
            if (addr < kRomSize) return rom_[addr];
            return 0xFF;                     // unmapped while booting
        }
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }

    if (addr < 0xB00000) {                   // ROM, mirrored ×2 (v8.cpp:87-89)
        if (overlay_) {                      // rom_switch_r (v8.cpp:225-235):
            overlay_ = false;                // any read clears the overlay,
            applyRamConfig(0xC0);            // default "full SIMM + full MB"
        }
        return rom_[addr & (kRomSize - 1)];
    }

    if (addr < 0xF00000) return 0xFF;        // $B00000-$EFFFFF: open bus

    // ── I/O space $F00000-$FFFFFF (docs/LCII_HARDWARE.md § Address map) ──
    if (addr >= 0xF40000 && addr < 0xFC0000) return vram_[addr - 0xF40000];
    if (cpu_) cpu_->flushTicks();            // registers see current time
    if (addr < 0xF02000) return viaAccess8(addr, false, 0);
    if (addr >= 0xF04000 && addr < 0xF06000) {
        // Z85C30 (O6.10: real Scc8530, dc_ab decode — maclc.cpp:114:
        // A1 = channel, A2 = data/ctl). Ext/status interrupts drive
        // IPL 4 (the LAP manager's carrier sense, Scc8530.h).
        int ch = (addr >> 1) & 1;
        uint8_t d = ((addr >> 2) & 1) ? scc_.readData(ch)
                                      : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, false, d);
        return d;
    }
    // SCSI pseudo-DMA windows: DRQ-handshaked, a hung DRQ bus-errors
    // (maclc.cpp:222-266; macscsi.cpp:5-52 — the SCSI Manager's blind
    // transfers catch the BERR to end the loop)
    if ((addr >= 0xF06000 && addr < 0xF08000) ||
        (addr >= 0xF12000 && addr < 0xF14000)) return scsiDma_();
    if (addr >= 0xF10000 && addr < 0xF12000) {
        // 53C80 registers, stride $10; pseudo-DMA read = reg 6 at +$260
        // (maclc.cpp:206-212)
        int reg = (addr >> 4) & 7;
        if (reg == 6 && (addr & 0xFFF) == 0x260) return scsiDma_();
        uint8_t d = scsi_.read(reg);
        scsiDrq(scsi_.drqActive());
        return d;
    }
    if (addr >= 0xF14000 && addr < 0xF16000) return asc_.read(addr - 0xF14000);
    if (addr >= 0xF16000 && addr < 0xF18000) {
        // SWIM1 in its IWM-compatible GCR mode (O6.7): reg = A9-A12,
        // +5 CPU cycles per access (maclc.cpp:268-287); HDSEL = VIA1 PA5
        if (cpu_) cpu_->stall(5);
        iwm_.setSel((via_.portA() & 0x20) != 0);
        return iwm_.read((addr >> 9) & 0xF);
    }
    if (addr >= 0xF24000 && addr < 0xF26000) return ariel_.read(addr & 3);
    if (addr >= 0xF26000 && addr < 0xF28000) {
        uint8_t d = pvia_.read(addr - 0xF26000);
        updateIrq();
        return d;
    }

    busError();                              // unmapped I/O: ROM map probe
}

void V8Memory::write8(uint32_t addr, uint8_t v) {
    addr &= 0x80FFFFFF;
    if (addr & 0x80000000) busError();

    if (addr < 0xA00000) {
        if (overlay_) return;                // ROM overlay: writes unmapped
        uint32_t i = ramIndex(addr);
        if (i != 0xFFFFFFFF) ram_[i] = v;
        return;
    }

    if (addr < 0xF00000) return;             // ROM + open bus: ignore

    if (addr >= 0xF40000 && addr < 0xFC0000) { vram_[addr - 0xF40000] = v; return; }
    if (cpu_) cpu_->flushTicks();            // registers see current time
    if (addr < 0xF02000) { viaAccess8(addr, true, v); return; }
    if (addr >= 0xF04000 && addr < 0xF06000) {              // SCC (O6.10)
        int ch = (addr >> 1) & 1;
        if (onSccAccess) onSccAccess(addr, true, v);
        if ((addr >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());
        return;
    }
    if ((addr >= 0xF06000 && addr < 0xF08000) ||
        (addr >= 0xF12000 && addr < 0xF14000)) { scsiDmaW_(v); return; }
    if (addr >= 0xF10000 && addr < 0xF12000) {
        // pseudo-DMA write = reg 0 at +$200 (maclc.cpp:214-220)
        int reg = (addr >> 4) & 7;
        if (reg == 0 && (addr & 0xFFF) == 0x200) { scsiDmaW_(v); return; }
        scsi_.write(reg, v);
        scsiDrq(scsi_.drqActive());
        return;
    }
    if (addr >= 0xF14000 && addr < 0xF16000) { asc_.write(addr - 0xF14000, v); return; }
    if (addr >= 0xF16000 && addr < 0xF18000) {               // SWIM1 (O6.7)
        if (cpu_) cpu_->stall(5);
        iwm_.setSel((via_.portA() & 0x20) != 0);
        iwm_.write((addr >> 9) & 0xF, v);
        return;
    }
    if (addr >= 0xF24000 && addr < 0xF26000) { ariel_.write(addr & 3, v); return; }
    if (addr >= 0xF26000 && addr < 0xF28000) {
        pvia_.write(addr - 0xF26000, v);
        updateIrq();
        return;
    }

    busError();
}

uint16_t V8Memory::read16(uint32_t addr) {
    addr &= 0x80FFFFFF;
    // POM68K perf (2026-07-17): word fast paths for RAM / ROM / VRAM —
    // the profile showed every 16-bit access splitting into two full
    // read8 decode cascades (1.8G calls at the Finder). Side-effect-free
    // regions only; the overlay case falls through to read8 (whose ROM
    // read clears the overlay).
    if (addr < 0xA00000) [[likely]] {                    // RAM space
        if (!overlay_) {
            uint32_t i0 = ramIndex(addr), i1 = ramIndex(addr + 1);
            return uint16_t(((i0 != 0xFFFFFFFF ? ram_[i0] : 0xFF) << 8)
                          |  (i1 != 0xFFFFFFFF ? ram_[i1] : 0xFF));
        }
    } else if (addr >= 0xF40000 && addr < 0xFBFFFF) {    // VRAM window
        return uint16_t(vram_[addr - 0xF40000] << 8 | vram_[addr - 0xF3FFFF]);
    } else if (addr >= 0xA00000 && addr < 0xB00000 && !overlay_) {
        uint32_t o = addr & (kRomSize - 1);              // ROM, mirrored
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    // VIA1 reads mirror the byte on both lanes (v8.cpp:434-447)
    if (addr >= 0xF00000 && addr < 0xF02000 && !(addr & 0x80000000)) {
        uint16_t d = viaAccess8(addr, false, 0);
        return uint16_t(d | (d << 8));
    }
    // SCC word fast path: one ctl/data side-effect, byte mirrored on both
    // lanes. Falling through to two read8() would double-advance ptr_.
    if (addr >= 0xF04000 && addr < 0xF06000 && !(addr & 0x80000000)) {
        if (cpu_) cpu_->flushTicks();
        int ch = (addr >> 1) & 1;
        uint8_t d = ((addr >> 2) & 1) ? scc_.readData(ch)
                                      : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, false, d);
        return uint16_t(d | (d << 8));
    }
    // Two sequenced statements: the operands of `|` are unsequenced in
    // C++ and read8 has side effects on device space (the SCSI
    // pseudo-DMA windows pop one FIFO byte per access) — a right-first
    // compiler would byte-swap every 16-bit blind transfer.
    const uint16_t hi = read8(addr);
    return uint16_t(hi << 8) | read8(addr + 1);
}

void V8Memory::write16(uint32_t addr, uint16_t v) {
    addr &= 0x80FFFFFF;
    // POM68K perf (2026-07-17): word fast paths for RAM / VRAM (see
    // read16) — side-effect-free regions only.
    if (addr < 0xA00000) [[likely]] {                    // RAM space
        if (!overlay_) {
            uint32_t i0 = ramIndex(addr), i1 = ramIndex(addr + 1);
            if (i0 != 0xFFFFFFFF) ram_[i0] = uint8_t(v >> 8);
            if (i1 != 0xFFFFFFFF) ram_[i1] = uint8_t(v);
            return;
        }
        return;                                          // overlay: unmapped
    }
    if (addr >= 0xF40000 && addr < 0xFBFFFF) {           // VRAM window
        vram_[addr - 0xF40000] = uint8_t(v >> 8);
        vram_[addr - 0xF3FFFF] = uint8_t(v);
        return;
    }
    // VIA1 word writes hit the register once per byte lane, low lane
    // first (v8.cpp:449-460 ACCESSING_BITS order)
    if (addr >= 0xF00000 && addr < 0xF02000 && !(addr & 0x80000000)) {
        viaAccess8(addr, true, uint8_t(v));
        viaAccess8(addr, true, uint8_t(v >> 8));
        return;
    }
    // SCC: one side-effect (high byte), matching the read16 mirror rule.
    if (addr >= 0xF04000 && addr < 0xF06000 && !(addr & 0x80000000)) {
        if (cpu_) cpu_->flushTicks();
        int ch = (addr >> 1) & 1;
        uint8_t b = uint8_t(v >> 8);
        if ((addr >> 2) & 1) scc_.writeData(ch, b);
        else                 scc_.writeCtl(ch, b);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, true, b);
        return;
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t V8Memory::peek8(uint32_t addr) const {
    addr &= 0x80FFFFFF;
    if (addr & 0x80000000) return 0xFF;
    if (addr < 0xA00000) {
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }
    if (addr < 0xB00000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0xF40000 && addr < 0xFC0000) return vram_[addr - 0xF40000];
    return 0xFF;
}

// DRQ-gated pseudo-DMA byte: without DRQ the V8 withholds /DSACK until
// the ~16 µs timeout raises /BERR — functionally, no data = bus error
// (macscsi.cpp:19-23; the blind-transfer MOVE.L loops rely on it)
uint8_t V8Memory::scsiDma_() {
    if (!scsi_.drqActive()) busError();
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void V8Memory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

// VIA1 timers at φ2 = CPU/20, plus the free-running 60.15 Hz tick timer
// into CA1 (v8.cpp:198-199,243-247 — NOT the video VBL; that one lands
// on pseudo-VIA slot bit $40 in O6.4). 60.15 Hz = 1203/20 Hz exactly,
// Bresenham on 20 × kCpuHz / 1203.
void V8Memory::tick(int cpuCycles) {
    viaPhase_ += cpuCycles;
    int viaCycles = viaPhase_ / 20;
    viaPhase_ %= 20;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= kCpuHz * 20) {
        tickAcc_ -= kCpuHz * 20;
        via_.raiseCa1();
    }

    // Real video VBL → pseudo-VIA slot bit $40 (v8.cpp:106-108): 512×384
    // frame = 640×407 dots at the CPU clock; blank = lines 384-406
    framePos_ += cpuCycles;
    framePos_ %= 640 * 407;
    bool vbl = framePos_ >= 640 * 384;
    if (vbl != vblState_) {
        vblState_ = vbl;
        pvia_.slotIrq(PseudoVia::VBL, vbl);
    }

    egret_.tick(cpuCycles);                  // may load the SR (SHIFT IRQ)
    asc_.tick(cpuCycles);                    // FIFO drain at 22 257 Hz
    iwm_.tick(cpuCycles);                    // GCR nibble stream
    scc_.tick(cpuCycles);                    // open-line Break/Abort stream (O6.11)
    sccIrq_ = scc_.irqAsserted();            // bidirectional — a de-asserted SCC
                                             // must lower the line too (updateIrq
                                             // below applies it); was latch-high only
    localTalkWatchdog(cpuCycles);            // O6.11 LAP unwedge

    // Keep AppleTalk inactive: XPRAM $13 and SysParam SPConfig $1FB = $22
    // (same policy as Mac II / Rtc). Infinite Mac Sys7 still surfaces
    // EtherTalk CautionAlerts — cleared by maybeDismissBootAlerts.
    if (egret_.pram(0x13) != 0x22)
        egret_.setPram(0x13, 0x22);
    if (peek8(0x1F8) == 0xA8 && peek8(0x1FB) != 0x22)
        write8(0x1FB, 0x22);

    tickAcc60_ += cpuCycles;
    if (tickAcc60_ >= kCpuHz / 60) {
        tickAcc60_ -= kCpuHz / 60;
        maybeDismissBootAlerts();
    }
    updateIrq();
}

void V8Memory::postKeyReturn() {
    // EvQEl below SysZone: 32-byte slots at $0F00+ (Mac II path reused).
    const uint32_t el = 0x00000F00u + uint32_t(alertEvSlot_++ & 7) * 32u;
    auto w16 = [this](uint32_t a, uint16_t v) {
        write8(a, uint8_t(v >> 8));
        write8(a + 1, uint8_t(v));
    };
    auto w32 = [this](uint32_t a, uint32_t v) {
        write8(a, uint8_t(v >> 24));
        write8(a + 1, uint8_t(v >> 16));
        write8(a + 2, uint8_t(v >> 8));
        write8(a + 3, uint8_t(v));
    };
    auto p32 = [this](uint32_t a) {
        return (uint32_t(peek8(a)) << 24) | (uint32_t(peek8(a + 1)) << 16) |
               (uint32_t(peek8(a + 2)) << 8) | peek8(a + 3);
    };
    w32(el, 0);
    w16(el + 4, 0);
    w16(el + 6, 3);                            // keyDown
    w32(el + 8, 0x240D);                       // ADB Return / CR
    w32(el + 12, p32(0x16A));                  // when = Ticks
    w16(el + 16, 256);
    w16(el + 18, 192);
    w16(el + 20, 0);
    const uint32_t tail = p32(0x150);
    if (tail && tail < 0xA00000)
        w32(tail, el);
    else
        w32(0x14C, el);
    w32(0x150, el);
}

void V8Memory::maybeDismissBootAlerts() {
    // Infinite Mac Sys7 EtherTalk CautionAlerts block ModalDialog; soft-post
    // Return. Gated on CurActivate bit31 + SCSI stall so Finder (CurActivate=0)
    // is untouched — same heuristic as MacIIMemory.
    if (alertDismissCool_ > 0) {
        alertDismissCool_--;
        return;
    }
    const long cmds = scsi_.commands;
    if (cmds == lastScsiCmds_ && cmds > 200)
        scsiStallFrames_++;
    else
        scsiStallFrames_ = 0;
    lastScsiCmds_ = cmds;

    const uint32_t curAct =
        (uint32_t(peek8(0xA64)) << 24) | (uint32_t(peek8(0xA65)) << 16) |
        (uint32_t(peek8(0xA66)) << 8) | peek8(0xA67);
    const bool modal = (curAct & 0x80000000u) != 0;
    if (!modal || scsiStallFrames_ < 45 || alertDismissPosts_ >= 6)
        return;

    postKeyReturn();
    alertDismissPosts_++;
    alertDismissCool_ = 90;
    scsiStallFrames_ = 0;
}

// O6.11 — HLE LocalTalk-LAP unwedge. When AppleTalk is active on a
// machine with no LocalTalk peer, the System's built-in .MPP LAP arms an
// SDLC transaction on the SCC and its caller busy-waits on a driver mutex
// (RAM byte $63e of the AppleTalk globals) for a completion that, on real
// hardware, arrives as the LAP send/receive times out ("no node
// responded") — a path woven through the SCC SDLC engine, the level-4 SCC
// ISR (which only resets the channel here, $A6C8E) and the Time Manager.
// POM68K reproduces the SCC side faithfully (Scc8530 streams the standing
// Break/Abort, delivered as a level-4 interrupt) but not the full LAP
// timeout state machine. Rather than emulate all three subsystems, this
// watchdog recognises the wedged transaction — the LAP mutex held for far
// longer than any real transaction — and performs the completion the
// driver's own path would (clear the mutex + the pending-op/resume
// pointers), so the caller falls through, its retry loop runs down, and
// .MPP reports "network unavailable" and boot continues. Gated so the
// only machines affected are those actually stuck in this wait.
//   AppleTalk globals base: a2 = *(*(ExpandMem $2B6) + $70)  (per the ROM
//   SCC ISR at $A67C0); the mutex/ptrs are +$63e / +$630 / +$634.
void V8Memory::localTalkWatchdog(int cpuCycles) {
    // Only while the LAP has LocalTalk live: Break/Abort IE armed on the
    // SCC channel it drives (WR15 bit 7) with master interrupts enabled.
    if (!lapWatchdog_ || !(scc_.wr(0, 15) & 0x80) || !(scc_.wr(1, 9) & 0x08)) {
        lapHeldCycles_ = 0;
        return;
    }
    auto rd32 = [&](uint32_t a) {
        return uint32_t(peek8(a)) << 24 | uint32_t(peek8(a + 1)) << 16
             | uint32_t(peek8(a + 2)) << 8 | peek8(a + 3);
    };
    uint32_t xm = rd32(0x2B6);
    if (xm < 0x1000 || xm >= 0xA00000) { lapHeldCycles_ = 0; return; }
    uint32_t a2 = rd32(xm + 0x70);
    if (a2 < 0x1000 || a2 >= 0xA00000) { lapHeldCycles_ = 0; return; }

    if (peek8(a2 + 0x63E) == 0) { lapHeldCycles_ = 0; return; }   // free

    // Mutex held — a real transaction clears it in well under a
    // millisecond; ~0.5 s (8M cycles at 15.67 MHz) with the abort stream
    // running means the LAP is wedged. Release only the mutex byte the
    // caller busy-waits on ($63e); the caller's own retry loop then
    // re-arms the transaction (rewriting the pending-op $630 / resume
    // $634 pointers) and, after its retry budget, reports failure.
    // NB: $634 is NOT touched — a stale `jmp ($634)` in the driver must
    // still land on its real resume ($A6562), not on a zeroed vector.
    lapHeldCycles_ += cpuCycles;
    if (lapHeldCycles_ < 8'000'000) return;
    lapHeldCycles_ = 0;
    // Loud on purpose: this pokes guest RAM on a fingerprint pinned to
    // ONE AppleTalk version's globals layout — if it ever fires against
    // a different System, the trace must say so (review 2026-07-16).
    std::fprintf(stderr, "[V8] LocalTalk watchdog: releasing LAP mutex "
                 "at $%06X (POM68K_NO_LTALK_WD disables)\n", a2 + 0x63E);
    write8(a2 + 0x63E, 0);                   // release the mutex
}
