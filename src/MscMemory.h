// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── PowerBook Duo memory map + MSC gate array (platform #11) ──
// MSC ("Main System Controller") is the Duo 210/230/250/270c/280/280c
// system ASIC — and the PowerBook 150's lineage. Map from MAME
// macpwrbkmsc.cpp:588-627 + msc.cpp:30-38 (R. Belmont, BSD-3-Clause),
// blueprint in docs/DUO_BRINGUP.md:
//   $00000000  RAM (ROM mirror while the boot overlay is up)
//   $40000000  1 MB ROM ×16 mirror; any read clears the overlay
//   $50F00000  VIA1 (MSC-internal; $200 stride, data on D15-D8)
//   $50F04000  SCC 8530 (dc_ab decode, byte on D15-D8)
//   $50F06000  SCSI pseudo-DMA           $50F12000  mirror
//   $50F10000  NCR 5380 ($10 stride)     $50F14000  ASC (MSC flavour)
//   $50F20000  GSC LCD controller regs   $50F26000  pseudo-VIA2
//   $50FA0000  power_cycle_w             $5FFFFFFC  box ID
//   $60000000  GSC VRAM 128 KB (mirrored across $6xxxxxxx)
// Box IDs (macpwrbkmsc.cpp:604-634): Duo 210 $A55A1004, 230 $A55A1005,
// 250 $A55A1006, 270c $A55A1002, 280 $A55A1000.
// No internal floppy (the Duo Dock carries it), no Egret/Cuda: power,
// clock, PRAM, keyboard (matrix), trackball and 1-Wire battery ID all
// live behind the PG&E power manager (68HC05 + hardware SPI), reached
// through the VIA1 shifter with /PMU_ACK//PMU_REQ on pseudo-VIA2 port B
// bits 1/2 (macpwrbkmsc.cpp:23-26).
//
// Milestone 1 (this skeleton): NO PG&E yet — the CPU is released at
// reset (on hardware the PMU holds /HALT until it has booted,
// msc.cpp:151), and the ROM is expected to stall at its first PMU
// exchange. That stall IS the milestone-1 checkpoint (the LC 520
// bring-up pattern). PG&E LLE: TODO.md, needs roms/pge/pge_boot.bin.
// Gate (once booting): tests/duo230_boot_etalon.cpp.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "PseudoVia.h"
#include "PgePmu.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Scc8530.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MscCpu;

class MscMemory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x20000;   // 128 KB GSC
    static constexpr int64_t  kCpuHz210 = 25000000;  // Duo 210 (25_MHz_XTAL)
    static constexpr int64_t  kCpuHz230 = 33000000;  // Duo 230/250 (33_MHz_XTAL)
    static constexpr int64_t  kViaHz = 783360;       // C7M / 10
    static constexpr int64_t  kC15M = 15667200;      // ASC/SCC domain
    static constexpr uint32_t kIdDuo210 = 0xA55A1004;
    static constexpr uint32_t kIdDuo230 = 0xA55A1005;
    static constexpr uint32_t kIdDuo250 = 0xA55A1006;

    explicit MscMemory(uint32_t totalRam = 0x800000,
                       int64_t cpuHz = kCpuHz230,
                       uint32_t machineId = kIdDuo230);
    int64_t cpuHz() const { return cpuHz_; }

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect-free

    void setCpu(MscCpu* cpu) { cpu_ = cpu; }

    // ── JIT hooks (VaspMemory contract) ────────────────────────────────
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return totalRam_; }
    void jitMapChanged();
    void updateIrq();
    // msc.cpp field_interrupts: SCC=4 > pseudo-VIA2=2 > VIA1/PMU=1.
    // (The PMU-with-IER-$90 special case rides VIA1's line once the PG&E
    // lands.)
    int iplLevel() const;

    void tick(int cpuCycles);        // VIA timers, 60.15 Hz CA1, VBL

    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    AscV8& asc() { return asc_; }
    int ascAvailable() const { return asc_.available(); }
    int16_t ascPop() { return asc_.pop(); }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    bool attachCdrom(const std::string& path, int id = 3) {
        if (id < 0 || id > 6 || !scsiDisks_[id].openCdrom(path)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    // The PMU boots first and releases the 68030 via its port E bit 2
    // (msc.cpp:151 + pmu_porte_w). Without roms/pge/pge_boot.bin there is
    // no PMU to do that, so the CPU runs free (the milestone-1 skeleton
    // behaviour — the ROM then stalls at the PMU handshake, loudly).
    bool cpuHeld() const { return pmu_.active() && pmu_.cpuHeld(); }
    PgePmu& pmu() { return pmu_; }
    bool pgeActive() const { return pmu_.active(); }
    // Clock/PRAM live inside the PMU on Duos — same forwarder shape as the
    // other platforms' setRtcSeconds (e.g. V8Memory → egret). The ctor
    // already seeds from host time (MAME m68hc05pge.cpp:185-187 does it in
    // device_start); this lets the GUI re-seed like every other machine.
    void setRtcSeconds(uint32_t s) { pmu_.setSeconds(s); }
    // Input goes through the PG&E. The matrix keyboard and the trackball
    // quadrature counters are milestone 4; until then host events ride the
    // ADB devices behind the PMU's modem cell, which is how an external
    // Duo keyboard/mouse would reach the guest anyway.
    void keyEvent(uint8_t code, bool down) { pmu_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { pmu_.mouseMove(dx, dy); }
    void mouseButton(bool down) { pmu_.mouseButton(down); }
    bool overlay() const { return overlay_; }
    Scc8530& scc() { return scc_; }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }

    // ── GSC video state (fixed-mode 640×400 DBLite panel on the Duo 2x0) ──
    const uint8_t* vram() const { return vram_.data(); }
    uint8_t gscReg(int i) const { return gscRegs_[i & 0x1F]; }
    static constexpr int kScreenW = 640;
    static constexpr int kScreenH = 400;
    // Host decode (milestone 3): 00RRGGBB gray, W×H. Mode = GSC reg 4 bits
    // 0-1 (MAME gsc.cpp screen_update_gsc): 0 → 1 bpp, 1 → 2 bpp, 2 → 4 bpp;
    // pen p of 16 renders as the P2/P5 gray ((15-p)<<4 | $F).
    void decodeScreen(std::vector<uint32_t>& out) const {
        out.resize(size_t(kScreenW) * size_t(kScreenH));
        const int mode = gscRegs_[4] & 3;
        for (int y = 0; y < kScreenH; y++)
            for (int x = 0; x < kScreenW; x++) {
                uint8_t pen;
                if (mode == 0) {
                    uint8_t b = vram_[size_t(y) * 80 + size_t(x) / 8];
                    pen = uint8_t(((b >> (7 - (x & 7))) & 1) ? 15 : 0);
                } else if (mode == 1) {
                    uint8_t b = vram_[size_t(y) * 160 + size_t(x) / 4];
                    pen = uint8_t(((b >> (6 - 2 * (x & 3))) & 3) << 2);
                } else {
                    uint8_t b = vram_[size_t(y) * 320 + size_t(x) / 2];
                    pen = (x & 1) ? uint8_t(b & 0xF) : uint8_t(b >> 4);
                }
                const uint8_t g = uint8_t(((15 - pen) << 4) | 0xF);
                out[size_t(y) * kScreenW + x] =
                    uint32_t(g) << 16 | uint32_t(g) << 8 | g;
            }
    }

    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // Diagnostic: first I/O accesses (duo_trace wires it)
    std::function<void(uint32_t, bool, uint32_t)> onIoAccess;

    // PMU wake (port G bit 5 falling): latched here, consumed by MscCpu at
    // a run boundary — a Moira reset from inside a memory callback would
    // re-enter the core mid-instruction.
    bool consumeWakeReset() { bool w = wakeReset_; wakeReset_ = false; return w; }

    // ── Save states (V8Memory pattern) ─────────────────────────────────
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via_, pvia_, pmu_, asc_, scsi_, scc_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_, mscConfig_, mscClockCtrl_, mscSoundCtrl_);
        ar(viaAcc_, tickAcc_, c15Acc_, framePos_, vblState_, wakeReset_);
        ar.bytes(gscRegs_, sizeof gscRegs_);
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    void viaSync();                  // E-clock stall (msc.cpp via_sync)
    uint8_t scsiDma_();
    void scsiDmaW_(uint8_t v);
    [[noreturn]] void busError() const;

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via_;
    // APPLE_MSC_PSEUDOVIA: full 256-byte decode, port-B input = the PMU
    // handshake lines, MSC block at $20-$2F (clock-ctrl 1, sound-ctrl 2
    // with SOUND_BUSY read-clear), msc_config on the video hook.
    PseudoVia pvia_{PseudoVia::Flavour::Msc};
    PgePmu pmu_;                     // PG&E on the VIA1 shifter
    AscV8 asc_;                      // TODO milestone 3+: ASC_MSC flavour
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    Scc8530 scc_;
    MscCpu* cpu_ = nullptr;

    uint32_t totalRam_;
    int64_t  cpuHz_;
    uint32_t machineId_;
    jit::CodeGuard* jitGuard_ = nullptr;
    bool overlay_ = true;
    bool sccIrq_ = false;
    uint8_t mscConfig_ = 0;          // msc_config_w (bit 1 halves CPU clock)
    uint8_t mscClockCtrl_ = 0;       // MSC block reg 1
    uint8_t mscSoundCtrl_ = 0;       // reg 2; bit 6 SOUND_BUSY read-clears
    uint8_t gscRegs_[0x20] = {};     // latched; decodeScreen() reads reg 4
    bool wakeReset_ = false;         // PMU wake → CPU reset at run boundary
    bool pmuReq_ = true;             // /PMU_REQ latch (host-written PB2)

    int64_t viaAcc_ = 0;
    int64_t tickAcc_ = 0;
    int64_t c15Acc_ = 0;
    int64_t framePos_ = 0;
    bool vblState_ = false;
};
