// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "NuBus.h"
#include <cstring>

namespace {
inline bool inRange(uint32_t a, uint32_t base, uint32_t size) {
    return a >= base && a < base + size;
}
} // namespace

uint32_t NuBus::slotBase(int slot) const {
    return 0xF9000000u + uint32_t(slot - kFirstSlot) * kSlotSize;
}

uint32_t NuBus::superSlotBase(int slot) const {
    return 0x90000000u + uint32_t(slot - kFirstSlot) * kSuperSlotSize;
}

bool NuBus::hasCard(int slot) const {
    return slot >= kFirstSlot && slot <= kLastSlot && slots_[slot].dev;
}

void NuBus::installCard(int slot, NuBusDevice* dev,
                        const std::vector<uint8_t>& declRom) {
    if (slot < kFirstSlot || slot > kLastSlot) return;
    slots_[slot].dev = dev;
    slots_[slot].declRom = declRom;
    slots_[slot].irq = false;
}

const std::vector<uint8_t>& NuBus::declRom(int slot) const {
    static const std::vector<uint8_t> empty;
    if (slot < kFirstSlot || slot > kLastSlot) return empty;
    return slots_[slot].declRom;
}

bool NuBus::decode(uint32_t addr, int& slot, uint32_t& off, bool super) const {
    if (super) {
        if (addr < 0x90000000u || addr >= 0xF0000000u) return false;
        slot = kFirstSlot + int((addr - 0x90000000u) / kSuperSlotSize);
        off = (addr - superSlotBase(slot)) & (kSuperSlotSize - 1);
    } else if (addr >= 0xF9000000u && addr < 0xFF000000u) {
        // 32-bit slot space Fs000000 (16 MB per slot)
        slot = kFirstSlot + int((addr - 0xF9000000u) / kSlotSize);
        off = (addr - slotBase(slot)) & (kSlotSize - 1);
    } else if (addr >= 0x00900000u && addr < 0x00F00000u) {
        // 24-bit classic NuBus: $s00000-$sFFFFF for slots $9-$E (AMU/HMMU
        // presents these; Mac II ROM Primary Init often uses them).
        slot = int(addr >> 20);
        off = addr & 0xFFFFFu;
    } else {
        return false;
    }
    return slot >= kFirstSlot && slot <= kLastSlot && slots_[slot].dev;
}

uint8_t NuBus::readDecl8(int slot, uint32_t off) const {
    const auto& rom = slots_[slot].declRom;
    if (rom.empty()) return 0xFF;
    // MAME places the image so the last romlen bytes of the 16 MB slot are
    // a linear copy (format block at FsFFFFFF), then tiles that pattern.
    const uint32_t fromEnd = (0x1000000u - 1u - (off & 0xFFFFFFu)) % uint32_t(rom.size());
    return rom[rom.size() - 1 - fromEnd];
}

uint8_t NuBus::read8(uint32_t addr) {
    int slot = 0;
    uint32_t off = 0;
    if (decode(addr, slot, off, false) || decode(addr, slot, off, true)) {
        if (!slots_[slot].dev) return 0xFF;
        // Toby/card registers occupy the low 1 MB window (mirrored by
        // A20-A23). Declaration ROM tiles the rest of the 16 MB slot,
        // including the canonical format block at the top (MAME
        // install_declaration_rom mirror_all_mb).
        const uint32_t low = off & 0x0FFFFFu;
        if (low < 0xE0000u)
            return slots_[slot].dev->read8(off);
        if (!slots_[slot].declRom.empty())
            return readDecl8(slot, off);
        return slots_[slot].dev->read8(off);
    }
    return 0xFF;
}

uint16_t NuBus::read16(uint32_t addr) {
    return uint16_t(read8(addr) << 8) | read8(addr + 1);
}

uint32_t NuBus::read32(uint32_t addr) {
    return uint32_t(read8(addr) << 24) | uint32_t(read8(addr + 1) << 16)
         | uint32_t(read8(addr + 2) << 8) | read8(addr + 3);
}

void NuBus::write8(uint32_t addr, uint8_t v) {
    int slot = 0;
    uint32_t off = 0;
    if (decode(addr, slot, off, false) || decode(addr, slot, off, true)) {
        if (slots_[slot].dev && (off & 0x0FFFFFu) < 0xE0000u)
            slots_[slot].dev->write8(off, v);
    }
}

void NuBus::write16(uint32_t addr, uint16_t v) {
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

void NuBus::write32(uint32_t addr, uint32_t v) {
    write8(addr, uint8_t(v >> 24));
    write8(addr + 1, uint8_t(v >> 16));
    write8(addr + 2, uint8_t(v >> 8));
    write8(addr + 3, uint8_t(v));
}

void NuBus::setSlotIrq(int slot, bool active) {
    if (slot < kFirstSlot || slot > kLastSlot) return;
    if (slots_[slot].irq == active) return;
    slots_[slot].irq = active;
    if (irqCb_) irqCb_(slot, active);
}

void NuBus::tick(int cpuCycles) {
    for (int s = kFirstSlot; s <= kLastSlot; s++)
        if (slots_[s].dev) slots_[s].dev->tick(cpuCycles);
}
