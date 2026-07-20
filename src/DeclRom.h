// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// NuBus declaration ROM builder + Toby card image loader.
// Format block and sResource grammar: docs/BASILISK_ROM_NOTES.md §9;
// CRC/scramble rules from MAME nubus install_declaration_rom.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class DeclRom {
public:
    static constexpr uint32_t kTestPattern = 0x5A932BC7;
    static constexpr uint16_t kFormatRev   = 0x0101;
    static constexpr uint8_t  kByteLanes   = 0x0F;

    // Build a minimal Display sResource (640×480×1) for unit tests.
    static std::vector<uint8_t> buildSynthetic(uint32_t fbBase = 0xF9000000);

    // Load raw 342-0008-a.bin (4096 B), reverse + BYTE4_XOR_BE scramble
    // for the 32-bit big-endian NuBus data path (MAME nubus_m2video).
    static std::vector<uint8_t> loadTobyRaw(const std::string& path);
    static std::vector<uint8_t> installRaw(const uint8_t* raw, size_t n);

    // Basilisk CRC over the whole image (CRC field zeroed first).
    static uint32_t computeCrc(const uint8_t* data, size_t n);
    static void     finalize(std::vector<uint8_t>& rom);

    // Parse helpers for tests.
    static bool validateFormatBlock(const uint8_t* data, size_t n);
    static uint32_t dirOffset(const uint8_t* data, size_t n);
};
