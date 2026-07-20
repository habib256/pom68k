// POM68K — nubus_test gate: slot $9 map + decl ROM + VIA2 NuBus IRQ lines.

#include "NuBus.h"
#include "DeclRom.h"
#include "Via6522.h"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* msg) {
    std::printf("  %-55s %s\n", msg, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

class DummyDev : public NuBusDevice {
public:
    uint8_t read8(uint32_t) override { return 0xA5; }
    uint16_t read16(uint32_t o) override { return read8(o); }
    uint32_t read32(uint32_t o) override { return read8(o); }
    void write8(uint32_t, uint8_t) override {}
    void write16(uint32_t, uint16_t) override {}
    void write32(uint32_t, uint32_t) override {}
};

int main() {
    std::printf("nubus_test — NuBus slot 9 + IRQ\n");

    NuBus bus;
    DummyDev dev;
    Via6522 via2;
    uint8_t nubusIrq = 0x3F;
    int lastSlot = -1;
    bool lastActive = false;

    bus.setIrqCallback([&](int slot, bool active) {
        lastSlot = slot;
        lastActive = active;
        static const uint8_t masks[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20};
        int idx = slot - 9;
        if (active) nubusIrq &= uint8_t(~masks[idx]);
        else nubusIrq |= masks[idx];
        via2.setInA(nubusIrq);
    });

    auto decl = DeclRom::buildSynthetic(bus.slotBase(9));
    bus.installCard(9, &dev, decl);

    uint32_t slotBase = bus.slotBase(9);
    check(slotBase == 0xF9000000u, "slot 9 base = $F9000000");
    check(bus.read8(slotBase) == 0xA5, "card VRAM region delegated");

    uint32_t declAddr = slotBase + 0x01000000u - uint32_t(decl.size());
    uint8_t b0 = bus.read8(declAddr);
    check(b0 == decl[0], "decl ROM visible at slot top");

    uint32_t superBase = bus.superSlotBase(9);
    check(bus.read8(superBase) == 0xA5, "super-slot mirrors card");

    bus.setSlotIrq(9, true);
    check(lastSlot == 9 && lastActive, "IRQ callback on assert");
    check((nubusIrq & 0x01) == 0, "VIA2 PA0 active-low on slot 9 IRQ");

    bus.setSlotIrq(9, false);
    check((nubusIrq & 0x01) != 0, "VIA2 PA0 released");

    std::printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
