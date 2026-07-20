// POM68K — toby_test gate: VRAM/CLUT/VBL on NuBus slot 9.

#include "NuBus.h"
#include "TobyVideo.h"
#include "DeclRom.h"
#include <cstdio>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* msg) {
    std::printf("  %-55s %s\n", msg, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main() {
    std::printf("toby_test — Toby Video HLE\n");

    NuBus bus;
    TobyVideo toby(bus, 9);
    auto decl = DeclRom::buildSynthetic(bus.slotBase(9));
    bus.installCard(9, &toby, decl);

    bool vblSeen = false;
    toby.setIrqHandler([&](bool s) { if (s) vblSeen = true; });

    uint32_t base = bus.slotBase(9);
    bus.write32(base, 0x00000000u);          // VRAM word (inverted bus)
    bus.write8(base + 0x90002, 0x00);      // CLUT addr
    bus.write8(base + 0x90002, 0x80);      // CLUT data
    bus.write8(base + 0xA0000, 0x00);      // enable VBL

    bus.write32(base + 0x8003C, 0x00000030u); // MISC2 → mode nibble

    for (int i = 0; i < 900000 && !vblSeen; i++)
        toby.tick(1000);

    check(vblSeen, "VBL IRQ pulse after frame tick");

    std::vector<uint32_t> fb;
    toby.decode(fb);
    check(int(fb.size()) == toby.hres() * toby.vres(), "decode size matches mode");
    check(toby.mode() <= 3, "valid depth mode");

    std::printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
