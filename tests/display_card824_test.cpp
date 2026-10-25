// POM68K — Display Card 8•24 contracts without Apple assets: declaration-ROM
// lanes and window, the JMFB/CRTC/RAMDAC/clock registers as the 68020 bus
// delivers them, scan-out, VBL, save states and the Mac II slot-9 wiring.
// The CRTC values are the ones the card's own driver leaves for the Hi-Res
// monitor (read back from macii_824_etalon), whose 30.24 MHz dot clock and
// 66.67 Hz frame are Apple's published timing.

#include "DisplayCard824.h"
#include "MacIIMemory.h"
#include "NuBus.h"
#include "SaveState.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    failures += !ok;
}
std::vector<uint8_t> save(auto& object) {
    std::vector<uint8_t> out;
    sav::Writer ar(out);
    ar(object);
    return out;
}
bool load(auto& object, const std::vector<uint8_t>& bytes) {
    sav::Reader ar(bytes.data(), bytes.size());
    ar(object);
    return ar.ok() && !ar.remaining();
}

// A 32 KB image shaped like the 341-0868: payload bytes, then the format
// block's byte-lane byte ($78 = lane 3) last.
std::vector<uint8_t> fakeDeclRom() {
    std::vector<uint8_t> raw(DisplayCard824::kDeclRomSize);
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = uint8_t(i * 7 + 1);
    raw[raw.size() - 2] = 0x00;
    raw.back() = 0x78;
    return raw;
}

constexpr uint32_t kRegs = 0x200000;

// Hi-Res as the driver programs it: CRTC words, the pixel-counter preload
// (256 − 32 at one bit, the counter running on longwords of pixels), then
// the clock synthesizer nibble by nibble (20 MHz / 125 × 189 = 30.24 MHz)
// and the RAMDAC.
void programHiRes(NuBus& bus, uint32_t slot, uint8_t preload, uint8_t ramdac) {
    const uint32_t crtc[][2] = {
        {0x108, 314}, {0x10C, 638}, {0x110, 52}, {0x114, 62}, {0x118, 104},
        {0x124, 960}, {0x128, 78}, {0x12C, 6}, {0x130, 6},
    };
    for (const auto& r : crtc) bus.write32(slot + kRegs + r[0], r[1]);
    bus.write32(slot + kRegs + 0x04, preload);
    const uint16_t multiplier = 189, modulus = 125;
    for (uint32_t n = 0; n < 4; ++n)
        bus.write32(slot + kRegs + 0x300 + 4 * n, (multiplier >> (4 * n)) & 15);
    for (uint32_t n = 0; n < 3; ++n)
        bus.write32(slot + kRegs + 0x310 + 4 * n, (modulus >> (4 * n)) & 15);
    bus.write32(slot + kRegs + 0x324, 0);                    // post-divider ÷1
    bus.write32(slot + kRegs + 0x208, ramdac);
}

}  // namespace

int main() {
    std::printf("display_card824_test — Display Card 8•24 (JMFB)\n");

    // ── Declaration ROM: lane 3, top of the slot only ───────────────────
    const std::vector<uint8_t> raw = fakeDeclRom();
    const std::vector<uint8_t> image = DisplayCard824::installDeclRom(raw);
    check(image.size() == raw.size() * 4 && image[3] == raw[0] &&
              image[4 * 100 + 3] == raw[100] && image[0] == 0 && image[2] == 0,
          "a lane-3 dump spreads to every fourth byte of the slot image");
    check(DisplayCard824::installDeclRom({raw.begin(), raw.end() - 1}).empty(),
          "a file of another size is not the 341-0868");
    std::vector<uint8_t> otherLanes = raw;
    otherLanes.back() = 0xE1;
    check(DisplayCard824::installDeclRom(otherLanes).empty(),
          "an unknown byte-lane byte is refused, not guessed");

    uint8_t monitor = 0;
    check(DisplayCard824::monitorFromWidth(1152, monitor) && monitor == DisplayCard824::kRgb21 &&
              DisplayCard824::monitorFromWidth(512, monitor) && monitor == DisplayCard824::kRgb12 &&
              !DisplayCard824::monitorFromWidth(1024, monitor) &&
              !DisplayCard824::monitorFromName("portrait", monitor),
          "POM68K_MONITOR widths name the four colour displays, nothing else");

    NuBus bus;
    DisplayCard824 card(bus, 9, MacIIMemory::kCpuHz, DisplayCard824::kHiRes);
    bus.installCard(9, &card, image, NuBus::DeclWindow::TopOfSlot);
    const uint32_t slot = bus.slotBase(9);
    check(bus.read8(slot + 0xFFFFFF) == raw.back() &&
              bus.read8(slot + 0xFFFFFF - 4) == raw[raw.size() - 2],
          "the format block answers at the top of the 16 MB slot");
    bus.write8(slot + 0x0E0000, 0x5A);
    check(bus.read8(slot + 0x0E0000) == 0x5A && card.vram()[0x0E0000] == 0x5A,
          "VRAM where a per-megabyte ROM mirror would sit stays VRAM");

    // ── Sense lines and the JMFB control word ───────────────────────────
    check(((bus.read32(slot + kRegs) >> 9) & 7) == 6,
          "the Hi-Res monitor's undriven sense code is 6");
    bus.write32(slot + kRegs, 0x0800);                       // drive sense2
    check(((bus.read32(slot + kRegs) >> 9) & 7) == 2,
          "driving a sense line reads back the monitor's wiring");
    bus.write16(slot + kRegs, 0x00CA);
    check((bus.read32(slot + kRegs) & 0xF1FF) == 0x00CA && card.control() == 0x00CA,
          "a word at the register's base reaches it smeared onto the low lanes");

    // ── The RAMDAC through the driver's word-then-long sequence ─────────
    bus.write32(slot + kRegs + 0x200, 5);                    // CLUT address 5
    bus.write16(slot + kRegs + 0x204, 0x0011);               // red   (word, lanes 0-1)
    bus.write32(slot + kRegs + 0x204, 0x00000022);           // green (long)
    bus.write16(slot + kRegs + 0x206, 0x0033);               // blue  (word, lanes 2-3)
    check(card.pen(5) == 0x112233,
          "a long after a word on the same port is a second bus cycle");
    bus.write32(slot + kRegs + 0x200, 0);
    for (uint8_t c : {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00})
        bus.write8(slot + kRegs + 0x207, c);
    check(card.pen(0) == 0xFFFFFF && card.pen(1) == 0 && card.pen(5) == 0x112233,
          "three components fill one entry and step the address");

    // ── CRTC, clock and frame: Apple's Hi-Res timing ────────────────────
    programHiRes(bus, slot, 0xE0, 0x00);                     // 1 bpp, ÷32
    check(card.configured() && card.width() == 640 && card.height() == 480 &&
              card.bitsPerPixel() == 1,
          "the driver's Hi-Res CRTC words describe 640×480 at one bit");
    programHiRes(bus, slot, 0xFC, 0x0C << 1);                // 8 bpp, ÷4
    check(card.width() == 640 && card.height() == 480 && card.bitsPerPixel() == 8,
          "eight bits keep the raster with the preload at four");
    const int64_t hiResFrame = MacIIMemory::kCpuHz * 864 * 525 / 30240000;
    check(card.frameCycles() == hiResFrame,
          "864 × 525 dots at 30.24 MHz: the 66.67 Hz Hi-Res frame");

    // ── Scan-out ────────────────────────────────────────────────────────
    bus.write32(slot + kRegs + 0x08, 0);                     // base
    bus.write32(slot + kRegs + 0x0C, 640 / 4);               // stride, longwords
    bus.write32(slot + kRegs, 0x0040);                       // transfer on
    bus.write8(slot + 0, 5);
    bus.write8(slot + 1, 1);
    bus.write8(slot + 640, 0);
    std::vector<uint32_t> fb;
    card.decode(fb);
    check(fb.size() == 640u * 480u && (fb[0] & 0xFFFFFF) == 0x112233 &&
              (fb[1] & 0xFFFFFF) == 0 && (fb[640] & 0xFFFFFF) == 0xFFFFFF,
          "eight-bit pixels index the CLUT, one row per stride");
    bus.write32(slot + kRegs + 0x04, 0xFF);                  // preload: ÷1,
    bus.write32(slot + kRegs + 0x208, 0x0D << 1);            // one pixel a longword
    check(card.width() == 640 && card.height() == 480 && card.bitsPerPixel() == 24 &&
              card.frameCycles() == hiResFrame,
          "24 bpp keeps the Hi-Res raster with the divider at one");
    bus.write32(slot + kRegs + 0x0C, 640 * 3 / 8);
    bus.write32(slot + kRegs, 0x0044);                       // + packed view
    bus.write32(slot + 0, 0x00ABCDEF);
    check(card.vram()[0] == 0xAB && card.vram()[1] == 0xCD && card.vram()[2] == 0xEF,
          "the packed view stores a 00RRGGBB longword as three bytes");
    card.decode(fb);
    check((fb[0] & 0xFFFFFF) == 0xABCDEF && bus.read32(slot + 0) == 0x00ABCDEF,
          "24-bit pixels are direct colour, read back through the same view");
    bus.write32(slot + kRegs, 0x0000);
    card.decode(fb);
    check(fb.size() == 640u * 480u && fb[0] == 0xFF000000u,
          "with transfer off the card scans out black");

    // ── VBL: CRTC enable, slot line, acknowledge ────────────────────────
    int lastIrqSlot = 0;
    bool lastIrq = false;
    bus.setIrqCallback([&](int s, bool on) { lastIrqSlot = s; lastIrq = on; });
    bus.write32(slot + kRegs + 0x13C, 0);                    // VBL enabled
    card.tick(int(hiResFrame));
    check(card.irqLevel() && lastIrq && lastIrqSlot == 9 && card.frameCount() == 1,
          "a whole frame raises the slot-9 VBL line once");
    bus.write32(slot + kRegs + 0x148, 0);
    check(!card.irqLevel() && !lastIrq, "the CRTC acknowledge drops the line");
    bus.write32(slot + kRegs + 0x13C, 2);                    // VBL disabled
    card.tick(int(hiResFrame));
    check(!card.irqLevel(), "a disabled VBL stays low across the frame");
    bus.write32(slot + kRegs + 0x13C, 0);

    // ── Save states ─────────────────────────────────────────────────────
    const auto state = save(card);
    card.tick(123457);
    const auto later = save(card);
    NuBus otherBus;
    DisplayCard824 fresh(otherBus, 9, MacIIMemory::kCpuHz, DisplayCard824::kHiRes);
    check(load(fresh, state), "a fresh card accepts the snapshot");
    fresh.tick(123457);
    check(save(fresh) == later, "the restored frame clock continues exactly");
    DisplayCard824 split(otherBus, 9, MacIIMemory::kCpuHz, DisplayCard824::kHiRes);
    load(split, state);
    for (int i = 0; i < 123457; i += 1000) split.tick(std::min(1000, 123457 - i));
    check(save(split) == later, "frame timing does not depend on tick partitioning");

    // ── Mac II slot 9: one card, recorded in the machine chunk ──────────
    const char* declPath = "display_card824_test.decl";
    std::ofstream(declPath, std::ios::binary)
        .write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
    const auto config = pom68k::defaultCoreConfig();
    MacIIMemory with824(config);
    check(with824.installDisplayCard824(declPath, DisplayCard824::kRgb21) &&
              with824.card824() && !with824.toby() &&
              !with824.installTobyVideo(),
          "slot 9 holds the 8•24 and then refuses Toby");
    check(with824.installDisplayCard824("no-such-341-0868.bin", DisplayCard824::kHiRes) &&
              with824.card824()->monitor() == DisplayCard824::kRgb21,
          "a second install keeps the fitted card and its monitor");
    MacIIMemory missing(config);
    check(!missing.installDisplayCard824("no-such-341-0868.bin", DisplayCard824::kHiRes) &&
              !missing.card824(),
          "a named dump that does not load leaves the slot empty");
    MacIIMemory se30(config, 0x800000, MacIIMemory::Model::SE30);
    check(!se30.installDisplayCard824(declPath, DisplayCard824::kHiRes),
          "the SE/30 has no NuBus slot 9");
    MacIIMemory withToby(config);
    withToby.installTobyVideo();
    const auto tobyState = save(withToby);
    MacIIMemory toby2(config);
    toby2.installTobyVideo();
    check(load(toby2, tobyState), "a Toby machine still restores a Toby snapshot");
    const auto cardState = save(with824);
    check(!load(withToby, cardState),
          "an 8•24 snapshot does not load into a Toby machine");
    MacIIMemory with824b(config);
    with824b.installDisplayCard824(declPath, DisplayCard824::kRgb21);
    check(load(with824b, cardState), "an 8•24 machine restores its own snapshot");
    std::remove(declPath);

    return failures ? 1 : 0;
}
