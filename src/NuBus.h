// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// HLE NuBus: 32-bit slot windows ($90000000 super-slot, $F9000000 16 MB slot),
// declaration ROM at the top of each populated slot, slot IRQ → VIA2 PA0..5.
// Functional only — no arbitration or cycle-exact timeouts.

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class NuBusDevice {
public:
    virtual ~NuBusDevice() = default;
    virtual uint8_t  read8(uint32_t slotOff) = 0;
    virtual uint16_t read16(uint32_t slotOff) = 0;
    virtual uint32_t read32(uint32_t slotOff) = 0;
    virtual void write8(uint32_t slotOff, uint8_t v) = 0;
    virtual void write16(uint32_t slotOff, uint16_t v) = 0;
    virtual void write32(uint32_t slotOff, uint32_t v) = 0;
    virtual void tick(int cpuCycles) {}
};

class NuBus {
public:
    static constexpr int kFirstSlot = 9;
    static constexpr int kLastSlot  = 14;
    static constexpr uint32_t kSuperSlotSize = 0x10000000;
    static constexpr uint32_t kSlotSize      = 0x01000000;

    using IrqCallback = std::function<void(int slot, bool active)>;

    void setIrqCallback(IrqCallback cb) { irqCb_ = std::move(cb); }

    // Install a card in `slot` (9..14). declRom is the scrambled 32-bit image
    // mirrored at the top of the 16 MB slot (MAME install_declaration_rom).
    void installCard(int slot, NuBusDevice* dev,
                     const std::vector<uint8_t>& declRom);

    bool hasCard(int slot) const;
    uint32_t slotBase(int slot) const;
    uint32_t superSlotBase(int slot) const;

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);
    void write8(uint32_t addr, uint8_t v);
    void write16(uint32_t addr, uint16_t v);
    void write32(uint32_t addr, uint32_t v);

    void setSlotIrq(int slot, bool active);
    void tick(int cpuCycles);
    const std::vector<uint8_t>& declRom(int slot) const;

private:
    struct Slot {
        NuBusDevice* dev = nullptr;
        std::vector<uint8_t> declRom;
        bool irq = false;
    };
    Slot slots_[kLastSlot + 1];
    IrqCallback irqCb_;
    bool decode(uint32_t addr, int& slot, uint32_t& off, bool super) const;
    uint8_t readDecl8(int slot, uint32_t off) const;
};
