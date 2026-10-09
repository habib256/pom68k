// POM68K — hardware differences hidden by shared Macintosh ROMs.
#include "MacMemory.h"
#include "MacIIMemory.h"
#include <cstdio>
#include <array>
#include <algorithm>
#include <vector>

// Drive the FDHD board's MMIO aperture and guest-clocked serializer, rather
// than writing directly to the host sector buffer. Parameters match the
// nominal C15M SWIM1 fixture in swim1_test; CRC bytes come from its TSS.
static bool writeFdhdSector(MacIIMemory& mem) {
    auto write = [&](int reg, uint8_t byte) { mem.write16(0x50f16000 + (reg << 9), uint16_t(0xaa00 | byte)); };
    auto read = [&](int reg) { return mem.read16(0x50f16000 + (reg << 9)); };
    read(13);
    for (uint8_t byte : {0x17, 0x17, 0x57, 0x17, 0x57, 0x57}) write(15, byte);
    if (!mem.swim().ism()) return false;
    write(6, 0xbf);
    constexpr uint8_t params[] = {42, 64, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 0, 27, 0, 59};
    for (uint8_t byte : params) write(3, byte);
    write(6, 0xbf);
    for (uint8_t byte : params) if (read(3) != uint16_t(byte * 0x101)) return false;
    mem.internalDrive().commandSwim(2);
    write(5, 0);
    auto feed = [&](int reg, uint8_t byte) {
        int budget = 64;
        while (mem.swim().fifoCount() >= 2 && budget--) mem.tick(64);
        write(reg, byte); mem.tick(128);
    };
    write(7, 0x9a);
    for (int i = 0; i < 12; ++i) feed(0, 0);
    for (int i = 0; i < 3; ++i) feed(1, 0xa1);
    feed(0, 0xfe);
    for (uint8_t byte : {0, 0, 2, 2}) feed(0, byte);
    feed(2, 0);
    for (int i = 0; i < 22; ++i) feed(0, 0x4e);
    for (int i = 0; i < 12; ++i) feed(0, 0);
    for (int i = 0; i < 3; ++i) feed(1, 0xa1);
    feed(0, 0xfb);
    std::array<uint8_t, 512> expected{}, actual{};
    for (size_t i = 0; i < expected.size(); ++i) feed(0, expected[i] = uint8_t(i ^ 0x90));
    feed(2, 0);
    for (int i = 0; i < 4; ++i) feed(0, 0x4e);
    int budget = 64;
    while (mem.swim().fifoCount() && budget--) mem.tick(256);
    mem.tick(512); write(6, 0x18);
    return mem.internalDrive().readSector(0, 0, 1, actual.data()) && actual == expected;
}

static bool readHdAcrossIndex(MacIIMemory& mem) {
    auto write = [&](int reg, uint8_t byte) { mem.write8(0x50f16000 + (reg << 9), byte); };
    auto read = [&](int reg) { return mem.read8(0x50f16000 + (reg << 9)); };
    read(13);
    for (uint8_t byte : {0x17, 0x17, 0x57, 0x17, 0x57, 0x57}) write(15, byte);
    write(6, 0xbf);
    // Parameters observed from the unmodified 97221136 .Sony ROM driver.
    for (uint8_t byte : {24, 65, 46, 46, 24, 24, 27, 27, 47, 47, 25, 25, 151, 27, 87, 59}) write(3, byte);
    mem.internalDrive().commandSwim(2);
    write(5, 0x20); write(7, 0xca);
    int bytes = 0;
    for (int tick = 0; tick < 40000 && bytes < 13000; ++tick) {
        mem.tick(128);
        while (mem.swim().fifoCount()) { read(1); ++bytes; }
        if (read(7) & 0x20) return false;
    }
    write(6, 0x08);
    return bytes >= 13000;
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
        failures += !ok;
    };
    MacMemory enhanced(pom68k::defaultCoreConfig(), MacMemory::Model::Mac512e);
    MacMemory plus(pom68k::defaultCoreConfig());
    std::vector<uint8_t> rom(128u << 10, 0);
    rom[0] = 0x4d; rom[1] = 0x1f; rom[2] = 0x81; rom[3] = 0x72;
    check(enhanced.loadRom(rom) && plus.loadRom(rom), "512Ke and Plus share a 128K ROM");
    enhanced.reset(); plus.reset();
    check(enhanced.ramSize() == 0x80000 && enhanced.romSize() == 0x20000 &&
          !enhanced.hasScsi() && !enhanced.isAdb() && !enhanced.hasSuperDrive() &&
          !enhanced.hasPwmSpindle(), "512Ke: soldered 512K, M0110, self-regulating 800K, no SCSI");
    check(enhanced.read16(0x420000) == 0x4d1f && enhanced.read16(0x440000) == 0x4d1f &&
          enhanced.peek8(0x4e0002) == 0x81 && plus.read16(0x420000) != plus.read16(0x440000),
          "512Ke repeated ROM decode makes the Plus ROM detect absent SCSI");
    uint32_t span = 0;
    const auto* bytes = enhanced.codeSpan(0x440000, span);
    check(bytes && bytes[0] == 0x4d && span == 0x20000,
          "JIT code window matches 512Ke ROM mirrors and ends at a mirror boundary");
    bytes = enhanced.dataSpan(0x4fffff, span, false);
    check(bytes && *bytes == 0 && span == 1, "JIT data window bounds the last ROM mirror");
    enhanced.write8(0xe807fe, 0xff); enhanced.write8(0xe81ffe, 0);
    enhanced.write16(0x600100, 0xa55a);
    check(!enhanced.overlay() && enhanced.read16(0x100) == 0xa55a &&
          enhanced.read16(0x80000 + 0x100) == 0xa55a,
          "512Ke RAM mirrors every 512K; its $600000 alias survives overlay removal");
    check(enhanced.read16(0x580000) == 0x5858,
          "absent 5380 leaves the SCSI aperture on address-dependent open bus");
    check(enhanced.internalDrive().insertImage(std::vector<uint8_t>(819200, 0)) &&
          enhanced.internalDrive().doubleSided() &&
          !enhanced.internalDrive().isSuperDrive() && !enhanced.internalDrive().sense(0xa),
          "512Ke mechanism reads double-sided 800K without exposing SuperDrive capability");
    MacIIMemory fdhd(pom68k::defaultCoreConfig(), 0x100000, MacIIMemory::Model::MacIIFDHD);
    MacIIMemory ii(pom68k::defaultCoreConfig(), 0x100000);
    fdhd.reset(); ii.reset();
    check(!fdhd.is030() && fdhd.hasSuperDrive() && fdhd.externalFloppyPort() &&
          !ii.is030() && !ii.hasSuperDrive(), "II FDHD: 68020 plus SWIM, independently of CPU family");
    check(fdhd.via1().portA() == ii.via1().portA() && fdhd.via2().portB() == ii.via2().portB(),
          "II FDHD retains original II VIA machine-ID pins");
    check(fdhd.internalDrive().insertImage(std::vector<uint8_t>(1474560, 0)) &&
          fdhd.internalDrive().isHd() && fdhd.externalDrive().isSuperDrive(),
          "II FDHD connects HD-capable mechanisms on both controller outputs");
    const auto& flux = fdhd.internalDrive().debugFlux();
    int64_t maximum = 0;
    for (size_t i = 1; i < flux.size(); ++i) maximum = std::max(maximum, flux[i] - flux[i - 1]);
    if (!flux.empty()) maximum = std::max(maximum,
        fdhd.internalDrive().fluxRevTicks() - flux.back() + flux.front());
    check(!flux.empty() && maximum <= 4 * fdhd.internalDrive().fluxCellTicks(),
          "formatted HD revolution, including gap4/index wrap, has no transition-free arc");
    check(readHdAcrossIndex(fdhd), "real ROM parameters read beyond one HD revolution without a separator error");
    fdhd.reset();
    check(writeFdhdSector(fdhd), "II FDHD MMIO/TSS formats and writes an MFM sector with matching read-back");
    return failures ? 1 : 0;
}
