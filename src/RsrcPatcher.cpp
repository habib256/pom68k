// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RsrcPatcher.h"
#include "V8Memory.h"

// Basilisk II opcodes (rsrc_patches.cpp)
namespace {
constexpr uint16_t M68K_JMP_A0 = 0x4ED0;     // jmp (a0)
constexpr uint16_t M68K_MOVEQ0 = 0x7000;     // moveq #0,d0
constexpr uint16_t M68K_RTS    = 0x4E75;     // rts
} // namespace

void RsrcPatcher::poke16(uint32_t addr, uint16_t v) {
    mem_.write8(addr, uint8_t(v >> 8));
    mem_.write8(addr + 1, uint8_t(v));
}

// Verbatim port of Basilisk II CheckLoad (rsrc_patches.cpp). Only the
// LocalTalk-disable patch is enabled — the one GISTPERSO's AppleTalk-active
// System needs — the rest of Basilisk's table is intentionally omitted
// (the V8 machine provides those services for real). LocalTalk is
// DISABLED, not emulated, exactly as under Basilisk.
bool RsrcPatcher::checkLoad(uint32_t type, int16_t id, uint32_t dataPtr,
                            uint32_t size) {
    if (!enabled_ || dataPtr == 0) return false;

    // Disable LocalTalk (Basilisk rsrc_patches.cpp:332-341): overwrite the
    // start of the 'ltlk' ADEV (id 0) with a no-op stub so .MPP finds no
    // LocalTalk and boot continues.
    //   4E D0  jmp (a0)      ← return to caller via a0
    //   70 00  moveq #0,d0   ← noErr
    //   4E 75  rts
    if (type == FOURCC('l', 't', 'l', 'k') && id == 0 && size >= 6) {
        poke16(dataPtr + 0, M68K_JMP_A0);
        poke16(dataPtr + 2, M68K_MOVEQ0);
        poke16(dataPtr + 4, M68K_RTS);
        patched++;
        return true;
    }

    return false;
}
