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
    // Bt453 through MAME's .umask32(0xff000000): the register index is the
    // 32-bit WORD index & 3, so the address register is $9001C (word 7) and
    // the palette data port $90018 (word 6) — exactly what the shipped
    // Apple TFB driver writes. The old $90002 writes hit an unused byte lane.
    bus.write8(base + 0x9001C, uint8_t(~0x05));   // CLUT addr 5 (bus inverts)
    bus.write8(base + 0x90018, uint8_t(~0x12));   // R
    bus.write8(base + 0x90018, uint8_t(~0x34));   // G
    bus.write8(base + 0x90018, uint8_t(~0x56));   // B
    bus.write8(base + 0xA0000, 0x00);      // enable VBL

    bus.write32(base + 0x8003C, 0x00000030u); // MISC2 → mode nibble

    for (int i = 0; i < 900000 && !vblSeen; i++)
        toby.tick(1000);

    check(vblSeen, "VBL IRQ pulse after frame tick");

    std::vector<uint32_t> fb;
    toby.decode(fb);
    check(int(fb.size()) == toby.hres() * toby.vres(), "decode size matches mode");
    // The CLUT write must have landed: read the address register back and the
    // palette entry through the same (inverted) bus the driver uses.
    check(bus.read8(base + 0x9001C) == uint8_t(~0x08),
          "DAC address auto-incremented past the 3 palette bytes");
    check(toby.mode() <= 3, "valid depth mode");

    // ── CRTC-derived frame clock (MAME nubus_m2video calc_screen_params:
    // frame = htotal×vtotal ticks of the 30.24 MHz pixel crystal) ──
    toby.reset();
    check(toby.frameCycles() == 15667200 / 60,
          "pre-programming fallback frame is 60 Hz");
    auto tfb = [&](int reg, uint8_t v) {         // TFB writes are inverted
        bus.write32(base + 0x80000 + uint32_t(reg) * 4, ~uint32_t(v));
    };
    // A 640×480 mode with htotal 896 / vtotal 525 (the MAME power-on
    // geometry): 40+16 halfline units → 896 px, 480 visible of 525 lines.
    tfb(9,  2);                                  // HSYNCSTART   (+2 = 4)
    tfb(10, 2);                                  // HSYNCFINISH  (+2 = 4)
    tfb(11, 2);                                  // HALFLINE_EARLY low7 (+2 = 4)
    tfb(12, 16);                                 // HALFLINE     (+2 = 18)
    tfb(13, 2);                                  // HPIXELS_HLATE low6 (+2 = 4)
    tfb(14, 5);                                  // HPIXELS ×4   (+2 = 22)
    tfb(5, 0xE3);                                // VLINES_VPP: vpp 7, vfp 4
    tfb(6, 119);                                 // VLINES → vlines 960
    tfb(7, 6);                                   // VBACKPORCH   (+8 = 14)
    tfb(8, 8);                                   // SYNCINTERVAL8 (+1 = 9)
    tfb(15, 0);                                  // MISC2: mode 0, commit
    check(toby.hres() == 640 && toby.vres() == 480, "CRTC programs 640x480");
    check(toby.htotal() == 896 && toby.vtotal() == 525,
          "CRTC totals derive 896x525");
    check(toby.frameCycles() == int64_t(15667200) * 896 * 525 / 30240000,
          "frame clock = htotal*vtotal / 30.24 MHz in CPU cycles");

    std::printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
