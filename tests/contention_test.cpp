// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M4 gate: the RAM/video contention model. Simulates a CPU issuing
// back-to-back 4-cycle RAM accesses for one full frame and checks the
// stolen-cycle budget against the hardware numbers (DEV.md § Timing):
// 128 cycles stolen per visible line (342) + 4 per line for the sound
// fetch (370) = 45 256 of 130 240, i.e. the GttMFH 2.56 MB/s average
// RAM bandwidth (Table 5-3).

#include "Cpu68k.h"
#include "MacMemory.h"
#include <memory>
#include <vector>
#include <cstdio>
#include <cstring>

int main() {
    constexpr long kFrame = 130240;
    long accesses = 0, waited = 0;
    moira::i64 t = 0;
    while (t < kFrame) {
        int d = Cpu68k::contentionDelay(t);
        waited += d;
        t += d + 4;                    // one word access = 4 CPU cycles
        accesses++;
    }
    long stolen = 342L * 128 + 370L * 4;               // 45 256
    long expected = (kFrame - stolen) / 4;             // 21 246 accesses
    double mbps = double(accesses) * 2 * 60.1474 / 1e6; // words → bytes/s
    std::printf("accesses/frame=%ld (expected %ld), waited=%ld (stolen budget %ld)\n",
                accesses, expected, waited, stolen);
    std::printf("average RAM bandwidth: %.2f MB/s (GttMFH target 2.56)\n", mbps);
    if (accesses < expected - 2 || accesses > expected + 2) {
        std::fprintf(stderr, "FAIL: contention model off budget\n");
        return 1;
    }
    // ROM/IO accesses are never delayed — spot-check the vblank region.
    if (Cpu68k::contentionDelay(342L * 352 + 100) != 0) {
        std::fprintf(stderr, "FAIL: contention applied during vblank\n");
        return 1;
    }
    // The closed form against the stated model, on every cycle of two
    // frames (the second proves the frame wrap).
    for (moira::i64 c = 0; c < 2 * kFrame; c++)
        if (Cpu68k::contentionDelay(c) != Cpu68k::contentionDelayRef(c)) {
            std::fprintf(stderr, "FAIL: closed form %d != model %d at cycle %lld\n",
                         Cpu68k::contentionDelay(c), Cpu68k::contentionDelayRef(c),
                         (long long)c);
            return 1;
        }
    // MacMemory's inline RAM read against the full decoder, overlay up and
    // down, across RAM, the overlay, the ROM window, the RAM alias and both
    // edges of the low quarters (no device is read: those have side effects).
    {
        std::vector<uint8_t> rom(0x20000);
        for (size_t i = 0; i < rom.size(); i++) rom[i] = uint8_t(i * 7 + (i >> 9));
        uint32_t sum = 0;
        for (size_t i = 4; i + 1 < rom.size(); i += 2)
            sum += uint32_t(rom[i] << 8 | rom[i + 1]);
        for (int k = 0; k < 4; k++) rom[size_t(k)] = uint8_t(sum >> (24 - 8 * k));
        auto mem = std::make_unique<MacMemory>(pom68k::defaultCoreConfig());
        if (!mem->loadRom(rom)) {
            std::fprintf(stderr, "FAIL: synthetic Plus ROM refused\n");
            return 1;
        }
        mem->reset();
        // The twin takes every write through the decoder, both passes: the
        // overlaid pass leaves RAM behind through the $600000 alias.
        auto twin = std::make_unique<MacMemory>(pom68k::defaultCoreConfig());
        twin->loadRom(rom);
        twin->reset();
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) {
                mem->write8(0xE807FE, 0xFF);           // VIA DDRA
                mem->write8(0xE81FFE, 0x00);           // PA4 low: overlay down
                for (uint32_t a = 0; a < 0x400000; a += 0x101)
                    mem->write8(a, uint8_t(a * 13 + (a >> 11)));
            }
            if (mem->overlay() != (pass == 0)) {
                std::fprintf(stderr, "FAIL: overlay not %s\n", pass ? "down" : "up");
                return 1;
            }
            auto check = [&](uint32_t a) {
                return mem->read8(a) == mem->read8Decoded(a) &&
                       mem->read16(a) == mem->read16Decoded(a);
            };
            bool same = true;
            for (uint32_t a = 0; a < 0x500000 && same; a += 0x35) same = check(a);
            for (uint32_t a = 0x600000; a < 0x800000 && same; a += 0x3B) same = check(a);
            for (uint32_t a : { 0x3FFFFEu, 0x3FFFFFu, 0x400000u, 0x0FFFFFu, 0x100000u,
                                0x1FFFFFu, 0x01000000u, 0x013FFFFFu, 0xFF000010u })
                same = same && check(a);
            if (!same) {
                std::fprintf(stderr, "FAIL: inline RAM read differs from the "
                                     "decoder (overlay %s)\n", pass ? "down" : "up");
                return 1;
            }
            // Writes: the inline path on this board, the decoder on a twin,
            // then every RAM byte compared. RAM, ROM window and alias only.
            if (pass == 1) {
                twin->write8Decoded(0xE807FE, 0xFF);
                twin->write8Decoded(0xE81FFE, 0x00);
                for (uint32_t a = 0; a < 0x400000; a += 0x101)
                    twin->write8Decoded(a, uint8_t(a * 13 + (a >> 11)));
            }
            auto store = [&](uint32_t a, uint16_t v) {
                mem->write8(a, uint8_t(v));          twin->write8Decoded(a, uint8_t(v));
                mem->write16(a + 3, v);              twin->write16Decoded(a + 3, v);
            };
            for (uint32_t a = 0; a < 0x4FFFF0; a += 0x2B3) store(a, uint16_t(a * 31 + 7));
            for (uint32_t a = 0x600000; a < 0x7FFFF0; a += 0x4C1) store(a, uint16_t(a ^ 0x5A5A));
            // The top edge last: a word at $3FFFFF must not wrap into $000000,
            // and nothing may overwrite the evidence afterwards.
            for (uint32_t a : { 0x01000000u, 0x0FFFFCu, 0x3FFFFBu, 0x3FFFFDu, 0x3FFFFCu })
                store(a, uint16_t(0xBEEF + a));
            if (mem->overlay() != twin->overlay() ||
                std::memcmp(mem->ram(), twin->ram(), mem->ramBytes()) != 0) {
                std::fprintf(stderr, "FAIL: inline RAM write differs from the "
                                     "decoder (overlay %s)\n", pass ? "down" : "up");
                return 1;
            }
        }
    }
    std::printf("contention_test: all checks passed\n");
    return 0;
}
