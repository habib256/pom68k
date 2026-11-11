// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// MacIIMemory's inline RAM read (read8/read16) against the full map
// (read8Mapped/read16Mapped), on a synthetic ROM: overlay up and down,
// 32-bit and 24-bit GLUE modes, 8 MiB and 2 MiB boards, a 68020 and a
// 68030 board. No device is read — those have side effects — only RAM, the
// bank mirror, the ROM windows and the edges between them.

#include "MacIIMemory.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

bool sweep(MacIIMemory& mem) {
    auto same = [&](uint32_t a) {
        return mem.read8(a) == mem.read8Mapped(a) &&
               mem.read16(a) == mem.read16Mapped(a);
    };
    bool ok = true;
    for (uint32_t a = 0; a < 0x01000000u && ok; a += 0x1D) ok = same(a);          // RAM + mirror
    for (uint32_t a = 0x40000000u; a < 0x40100000u && ok; a += 0x101) ok = same(a); // ROM
    for (uint32_t a = 0x40800000u; a < 0x40900000u && ok; a += 0x101) ok = same(a);
    for (uint32_t base : { 0x001FFFF0u, 0x003FFFF0u, 0x007FFFF0u, 0x00BFFFF0u,
                           0x00FFFFF0u, 0x017FFFF0u, 0xFF7FFFF0u, 0x3FFFFFE0u })
        for (uint32_t a = base; a < base + 0x20 && ok; a++) ok = same(a);
    return ok;
}

bool runBoard(MacIIMemory::Model model, uint32_t ramSize, const char* name) {
    auto mem = std::make_unique<MacIIMemory>(pom68k::defaultCoreConfig(), ramSize, model);
    std::vector<uint8_t> rom(0x40000);
    for (size_t i = 0; i < rom.size(); i++) rom[i] = uint8_t(i * 11 + (i >> 7));
    if (!mem->loadRom(rom)) { std::printf("  %s: synthetic ROM refused\n", name); return false; }
    mem->reset();
    bool ok = mem->overlay() && !mem->hmmu24() && sweep(*mem);        // overlay up, 32-bit
    mem->write8(0x50F00600, 0xFF);               // VIA1 DDRA: outputs
    mem->write8(0x50F01E00, 0x00);               // ORA_NH: PA4 low, overlay down
    for (uint32_t a = 0; a < ramSize; a += 0x7F) mem->write8(a, uint8_t(a * 5 + (a >> 13)));
    ok = ok && !mem->overlay() && sweep(*mem);                        // overlay down, 32-bit
    mem->write8(0x50F02400, 0x08);               // VIA2 DDRB: PB3 output
    mem->write8(0x50F02000, 0x00);               // ORB: PB3 low, 24-bit GLUE map
    const bool mode24 = mem->hmmu24();
    ok = ok && sweep(*mem);
    char what[96];
    std::snprintf(what, sizeof what, "%s: inline reads equal the full map (24-bit mode %s)",
                  name, mode24 ? "reached" : "NOT reached");
    check(ok && mode24, what);
    return ok && mode24;
}
} // namespace

int main() {
    std::printf("macii_ram_read_test — inline RAM reads against the GLUE map\n");
    runBoard(MacIIMemory::Model::MacII, 0x800000, "Mac II, 8 MiB");
    runBoard(MacIIMemory::Model::MacII, 0x200000, "Mac II, 2 MiB");
    runBoard(MacIIMemory::Model::IIx, 0x800000, "Mac IIx (68030), 8 MiB");
    std::printf(gFails ? "FAILED (%d)\n" : "PASS\n", gFails);
    return gFails ? 1 : 0;
}
