// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "V8Memory.h"
#include "Cpu030.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

V8Memory::V8Memory(uint32_t totalRam, Model model, int64_t cpuHz)
    : ram_(totalRam, 0), rom_(kRomSize, 0), vram_(kVramSize, 0),
      egret_(via_, model == Model::ColorClassic || model == Model::MacTv,
             int(cpuHz)),
      egretLle_(via_, cpuHz, model == Model::ColorClassic
                                     || model == Model::MacTv
                                 ? CudaLle::Flavor::Cuda
                                 : CudaLle::Flavor::Egret),
      totalRam_(totalRam), model_(model), cpuHz_(cpuHz),
      viaDiv_(int(cpuHz / kViaHz)),
      mbRam_(model == Model::Lc ? 0x200000 : kMbRamSize) {
    // Pseudo-VIA machine hooks (v8.cpp:328-352): reg 1 = RAM config
    // (reads back config | 0x04), reg $10 read = monitor sense on bits
    // 3-5, port B bit 3 = HMMU enable (68020 LC only, see addrMask_).
    pvia_.onConfigRead = [this] { return uint8_t(config_ | 0x04); };
    pvia_.onConfigWrite = [this](uint8_t v) { config_ = v; applyRamConfig(v); };
    if (model_ == Model::Lc)                 // PB3 LOW = HMMU 24-bit mode
        pvia_.onPortB = [this](uint8_t v) {
            addrMask_ = (v & 0x08) ? 0x80FFFFFF : 0x00FFFFFF;
        };
    // Eagle has no monitor sense — the Classic II display is built in
    // (v8.cpp:662-665 via2_video_config_r returns 0). The Spice's
    // Trinitron is built in too but reports the fixed 512×384 sense 2
    // (v8.cpp:760-763 returns 0x02 << 3).
    if (model_ == Model::ClassicII)
        pvia_.onVideoRead = [] { return uint8_t(0); };
    else if (model_ == Model::ColorClassic)
        pvia_.onVideoRead = [] { return uint8_t(0x02 << 3); };
    else if (model_ == Model::MacTv)         // Tinker Bell: fixed 13" 640×480
        pvia_.onVideoRead = [] { return uint8_t(0x06 << 3); };
    else
        pvia_.onVideoRead = [this] { return uint8_t((montype_ << 3) & 0x38); };
    if (model_ == Model::MacTv) montype_ = 6;   // built-in 640×480 CRT
    pvia_.onVideoWrite = [this](uint8_t v) { videoConfig_ = v; };
    egret_.setAdbBus(&adb_);
    // ASC IRQ is LEVEL-triggered into pseudo-VIA IFR bit 4 (v8.cpp:119-122).
    // The Spice carries the Sonora-class EASC ($BC) instead of the V8 ASC
    // (v8.cpp:717-720 ASC_SONORA replace) — only one of the two is wired.
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    ascSonora_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    // Egret firmware LLE (real 341S0850, MAME egret.cpp:74) — the DEFAULT
    // since 2026-07-24 (TODO step 6 closed): the instruction-slaved ADB
    // wire (CudaLle::mcu_.onCycles) fixed the batch-frozen receive that
    // starved the mouse to ~1.5% delivery; lcii_mouse_trace now saturates
    // the screen exactly like the HLE. POM68K_EGRET_LLE=0 forces the HLE,
    // a missing dump falls back silently (the Cuda rollout pattern).
    {
        // Color Classic: Cuda MCU. The factory part is 341S0417 (Cuda
        // 2.35, maclc.cpp:480) but that image wedges on our M68hc05 —
        // it releases the host reset then never answers the VIA
        // transport (bring-up 2026-07-24, TODO § Color Classic) — so the
        // LLE runs the Q605-proven 341S0788 (Cuda 2.37); the CC ROM
        // drives it fine. Same env-gated rollout as the LC II Egret.
        const bool cudaMcu = hasCudaMcu();
        const char* e = std::getenv(cudaMcu ? "POM68K_CUDA_LLE"
                                            : "POM68K_EGRET_LLE");
        const bool want = !e || std::atoi(e) != 0;
        static constexpr const char* kEgretFw[] = {
            "roms/egret/341s0850.bin", "../roms/egret/341s0850.bin", nullptr };
        static constexpr const char* kCudaFw[] = {
            "roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin", nullptr };
        // Mac TV: the factory Cuda is 341s0789 (Cuda 2.38, cuda.cpp:48);
        // fall back to the AIO-proven 2.40 then the CC's 2.37.
        static constexpr const char* kTvFw[] = {
            "roms/cuda/341s0789.bin", "../roms/cuda/341s0789.bin",
            "roms/cuda/341s0060.bin", "../roms/cuda/341s0060.bin",
            "roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin", nullptr };
        if (want) {
            for (const char* const* p = model_ == Model::MacTv ? kTvFw
                                        : cudaMcu ? kCudaFw : kEgretFw; *p; p++) {
                std::ifstream in(*p, std::ios::binary);
                if (!in) continue;
                std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
                if (egretLle_.loadFirmware(fw)) { egretLleOn_ = true; break; }
            }
            if (!egretLleOn_ && e)
                std::fprintf(stderr, "V8: %s set but no MCU firmware dump — "
                             "HLE fallback\n",
                             cudaMcu ? "POM68K_CUDA_LLE" : "POM68K_EGRET_LLE");
        }
    }
    reset();
}

// 512 KB flat image; the stored big-endian word checksum (bytes 4…end)
// is verified against the header — warn-only, so patched/homebrew ROMs
// still load (the real LC II ROM is $35C28F5F, docs/LCII_HARDWARE.md).
bool V8Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize && data.size() != 0x100000) return false;
    rom_ = data;
    romSize_ = uint32_t(data.size());        // Spice ROMs (Color Classic
    romMask_ = romSize_ - 1;                 // $ECD99DC0) are 1 MB

    // Classic II ROM ($3193670E): genuine ROM bug — a JMP through a
    // boxflag table indexes PAST the table and lands mid-instruction in
    // 32-bit mode. MAME patches the bad JMP to an RTS (the IIvx/IIvi ROM
    // shows the intended entry is a no-op) and fixes the checksum to
    // match (maclc.cpp:614-630 ROM_FILL). Same patch here.
    if (rom_[0] == 0x31 && rom_[1] == 0x93 && rom_[2] == 0x67 && rom_[3] == 0x0E) {
        rom_[0x43B6E] = 0x4E; rom_[0x43B6F] = 0x75;  // JMP (table) → RTS
        rom_[0x00002] = 0x66; rom_[0x00003] = 0x88;  // checksum compensation
    }

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
    scc_.setClocks(cpuHz_, 7833600);         // SCC async-baud LLE: PCLK =
                                             // C7M (LCII_HARDWARE.md:44)
    scc_.setAbortIdle(std::getenv("POM68K_SCC_CLEANLINE") == nullptr);
                                             // no hardwired LocalTalk peer
                                             // (O6.10); a real LToUDP peer
                                             // drops the standing abort —
                                             // Scc8530::openLine (LLE step 8)
    viaPhase_ = 0;
    tickAcc_ = 0;
    addrMask_ = 0x80FFFFFF;                  // HMMU disabled at reset (MAME
                                             // m68kcpu.cpp:1147; PB3 rewrites)
    simmMapped_ = mbMapped_ = false;         // no RAM until the overlay drops
    via_.reset();
    pvia_.reset();
    ariel_.reset();
    egret_.reset();
    egret_.factoryDefaults();                // SPConfig XPRAM $13 = $22
    egretLle_.reset();
    if (egretLleOn_)                         // stage the same battery
        for (int i = 0; i < 256; i++)
            egretLle_.setPram(i, egret_.pram(i));
    adb_.reset();
    asc_.reset();
    ascSonora_.reset();
    scsi_.reset();
    if (spiceClass()) {                      // Spice/Tinker Bell: SWIM2
        swim2_.reset();
        swim2_.attachDrive(&drive_, nullptr);
    } else {
        swim_.reset();
        swim_.attachDrive(&drive_, nullptr);
    }
    drive_.setSpinClockHz(15667200);         // devices tick in the C15M domain
    framePos_ = 0;
    c15Acc_ = 0;
    // Frame/VBL geometry in CPU cycles: V8-class scans 640×407 dots at
    // C15M (VBL = lines 384-406); Tinker Bell scans 800×525 @ 25.175 MHz
    // with 480 active lines (v8.cpp:936 set_raw).
    if (model_ == Model::MacTv) {
        frameCycles_ = int64_t(800) * 525 * cpuHz_ / 25175000;
        vblStart_ = int64_t(800) * 480 * cpuHz_ / 25175000;
    } else {
        frameCycles_ = int64_t(640) * 407 * cpuHz_ / kCpuHz;
        vblStart_ = int64_t(640) * 384 * cpuHz_ / kCpuHz;
    }
    vblState_ = false;
    // VIA1 port A input = V8-family machine ID | diag bit: $D4 for the
    // V8 proper (LC/LC II, v8.cpp:249-252), $92 for the Classic II's
    // Eagle (v8.cpp:657-660), $82 for the Color Classic's Spice
    // (v8.cpp:755-758); PB3 = Egret XCVR_SESSION, idle high. PB0-PB2
    // (legacy RTC lines) and PB6-PB7 keep the 6522 pull-up default 1
    // (review 2026-07-16: they read 0 before, incl. to the ROM's
    // old-clock probe). PB4/PB5 (VIA_FULL/SYS_SESSION) are HOST-driven
    // handshake lines and must idle LOW here: portB() is fed into
    // Egret::portBChanged, whose HLE is edge-triggered — pulled-up 1s
    // while DDRB is still 0 at reset read as a phantom session rise and
    // wedge the transport (validated: pull-ups on PB4/PB5 black-screen
    // the boot etalon).
    // Tinker Bell reads a plain $84 (v8.cpp:946-949, no diag bit OR).
    via_.setInA(model_ == Model::ClassicII     ? 0x93
                : model_ == Model::ColorClassic ? 0x83
                : model_ == Model::MacTv        ? 0x84
                                                : 0xD5);
    via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
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

    simmPhys_ = totalRam_ > mbRam_ ? totalRam_ - mbRam_ : 0;
    simmOff_ = mbRam_;
    if (totalRam_ == 0xA00000) { simmPhys_ = 0x800000; simmOff_ = 0x200000; }

    simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;

    static constexpr uint32_t kSimmCfg[4] = { 0, 0x200000, 0x400000, 0x800000 };
    mbLoc_ = simmMapped_ ? kSimmCfg[(config >> 6) & 3] : 0;

    mbMapped_ = (config & 0xC0) != 0xC0;     // 8 MB SIMM ⇒ only the $800000 alias
    mbSize_ = (config & 0x20) ? 0x200000 : mbRam_;
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
// With cpuClk/viaClk = viaDiv_ (20 at C15M, 40 on the Mac TV's C32M):
// main = (via_cycle*2 + 3) * viaDiv_/2 + 1.
// Machine cycles, not core-clock cycles — aligning to a boost-fast phantom
// E-clock shrinks every VIA-paced pulse by cacheBoost_ (CHANGELOG 2026-07-25).
void V8Memory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c / viaDiv_;
    int64_t target = (viaCycle * 2 + 3) * (viaDiv_ / 2) + 1;
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
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (egretLleOn_) egretLle_.portBChanged(via_.portB());
            else             egret_.portBChanged(via_.portB());
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)                 // PB3 = XCVR_SESSION, live
        via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t V8Memory::read8(uint32_t addr) {
    addr &= addrMask_;                       // A31 + A23-A0 (maclc.cpp:181)
    if (addr & 0x80000000) {                 // PDS slot $E: no card
        if (model_ == Model::ClassicII) return 0xFF;  // no PDS, open bus
        busError();
    }

    if (addr < 0xA00000) {                   // RAM space (ROM under overlay)
        if (overlay_) {
            if (addr < romSize_) return rom_[addr];
            return 0xFF;                     // unmapped while booting
        }
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }

    if (addr < 0xB00000) {                   // ROM, 512K mirrored ×2 or 1 MB
        if (overlay_) {                      // rom_switch_r (v8.cpp:225-235):
            overlay_ = false;                // any read clears the overlay,
            applyRamConfig(0xC0);            // default "full SIMM + full MB"
        }
        return rom_[addr & romMask_];
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
    if (addr >= 0xF14000 && addr < 0xF16000)
        return spiceClass() ? ascSonora_.read(addr - 0xF14000)
                            : asc_.read(addr - 0xF14000);
    if (addr >= 0xF16000 && addr < 0xF18000) {
        // SWIM1 in its IWM-compatible GCR mode (O6.7): reg = A9-A12,
        // +5 CPU cycles per access (maclc.cpp:268-287); HDSEL = VIA1 PA5.
        // Spice: the integrated SWIM2 instead (v8.cpp:699,874-882 — same
        // reg decode + wait states, HDSEL from the SWIM2 mode register).
        if (cpu_) cpu_->stall(5);
        if (spiceClass())
            return swim2_.read((addr >> 9) & 0xF);
        swim_.setSel((via_.portA() & 0x20) != 0);
        return swim_.read((addr >> 9) & 0xF);
    }
    if (addr >= 0xF24000 && addr < 0xF26000) return ariel_.read(addr & 3);
    if (addr >= 0xF26000 && addr < 0xF28000) {
        uint8_t d = pvia_.read(addr - 0xF26000);
        updateIrq();
        return d;
    }

    // Unmapped I/O: BERR on the V8 proper — the LC/LC II ROM map probes
    // rely on it (AddrMapFlags $773F, LCII_HARDWARE.md:78). The Classic
    // II's Eagle bus is FORGIVING instead (MAME macclas2: only the SCSI
    // helper timeout raises /BERR; every map hole is open bus): its ROM
    // dereferences $50F18038 and pokes through wild pointers with no
    // catcher installed — any BERR there lands on a zero vector → DS 1.
    if (model_ == Model::ClassicII) return 0xFF;
    busError();                              // unmapped I/O: ROM map probe
}

void V8Memory::write8(uint32_t addr, uint8_t v) {
    addr &= addrMask_;
    if (addr & 0x80000000) {                 // PDS slot $E: no card
        if (model_ == Model::ClassicII) return;       // no PDS, open bus
        busError();
    }

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
    if (addr >= 0xF14000 && addr < 0xF16000) {
        if (spiceClass()) ascSonora_.write(addr - 0xF14000, v);
        else              asc_.write(addr - 0xF14000, v);
        return;
    }
    if (addr >= 0xF16000 && addr < 0xF18000) {               // SWIM1 (O6.7)
        if (cpu_) cpu_->stall(5);
        if (spiceClass()) {                                  // Spice SWIM2
            swim2_.write((addr >> 9) & 0xF, v);
            return;
        }
        swim_.setSel((via_.portA() & 0x20) != 0);
        swim_.write((addr >> 9) & 0xF, v);
        return;
    }
    // Spice brightness/contrast DAC ($F18000/1, v8.cpp:700,925-929) —
    // accepted and dropped (the emulated CRT has no analog stage).
    if (spiceClass() && addr >= 0xF18000 && addr < 0xF18002)
        return;
    if (addr >= 0xF24000 && addr < 0xF26000) { ariel_.write(addr & 3, v); return; }
    if (addr >= 0xF26000 && addr < 0xF28000) {
        pvia_.write(addr - 0xF26000, v);
        updateIrq();
        return;
    }

    if (model_ == Model::ClassicII) return;  // Eagle: forgiving bus (read8)
    busError();
}

uint16_t V8Memory::read16(uint32_t addr) {
    addr &= addrMask_;
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
        uint32_t o = addr & romMask_;                    // ROM, mirrored
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & romMask_]);
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
    addr &= addrMask_;
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
    addr &= addrMask_;
    if (addr & 0x80000000) return 0xFF;
    if (addr < 0xA00000) {
        if (overlay_) return addr < romSize_ ? rom_[addr] : 0xFF;
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }
    if (addr < 0xB00000) return rom_[addr & romMask_];
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
    int viaCycles = viaPhase_ / viaDiv_;
    viaPhase_ %= viaDiv_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // Real video VBL → pseudo-VIA slot bit $40 (v8.cpp:106-108); geometry
    // reduced to CPU cycles at reset (Tinker Bell scans 800×525).
    framePos_ += cpuCycles;
    framePos_ %= frameCycles_;
    bool vbl = framePos_ >= vblStart_;
    if (vbl != vblState_) {
        vblState_ = vbl;
        pvia_.slotIrq(PseudoVia::VBL, vbl);
    }

    if (egretLleOn_) egretLle_.tick(cpuCycles);   // firmware + AdbLine
    else             egret_.tick(cpuCycles);      // may load the SR (SHIFT IRQ)
    // ASC drain, SWIM cells and the floppy spindle live in the C15M domain
    // (1:1 on every V8 machine but the Mac TV's C32M — the VASP pattern).
    c15Acc_ += int64_t(cpuCycles) * kCpuHz;
    int c15 = int(c15Acc_ / cpuHz_);
    c15Acc_ -= int64_t(c15) * cpuHz_;
    if (c15) {
        if (spiceClass()) {
            ascSonora_.tick(c15);            // Sonora EASC drain (Spice)
            swim2_.tick(c15);                // Spice/Tinker Bell SWIM2
        } else {
            asc_.tick(c15);                  // FIFO drain at 22 257 Hz
            swim_.tick(c15);                 // IWM nibbles / ISM cell engine
        }
        drive_.tick(c15);                    // spindle/tach time (was frozen)
    }
    scc_.tick(cpuCycles);                    // open-line Break/Abort stream (O6.11)
    sccIrq_ = scc_.irqAsserted();            // bidirectional — a de-asserted SCC
                                             // must lower the line too (updateIrq
                                             // below applies it); was latch-high only
    updateIrq();
}

