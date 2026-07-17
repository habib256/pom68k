// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Resource-patch-on-load (HLE, O6.11) ──
// A faithful port of Basilisk II's vCheckLoad/CheckLoad mechanism
// (macemu BasiliskII/src/rsrc_patches.cpp + rom_patches.cpp, commit
// 96e512bd; study in docs/BASILISK_ROM_NOTES.md). Basilisk hooks the
// ROM's vCheckLoad routine so that every resource the Resource Manager
// loads is inspected — and a handful are patched in place — before the
// system runs them. POM68K reaches the identical interception point
// WITHOUT patching the ROM: Moira sets a breakpoint at the LC II ROM's
// vCheckLoad ($A00000 + $1B8F4 = $A1B8F4, the fixed offset Basilisk uses
// for all $067C ROMs), and `Cpu030::didReachBreakpoint` calls
// `RsrcPatcher::checkLoad` with the register state.
//
// The only patch we need to boot GISTPERSO's AppleTalk-active System is
// the verbatim Basilisk one: stub the `ltlk` (id 0) LocalTalk ADEV to a
// no-op so `.MPP` concludes "no LocalTalk" and boot continues (Basilisk
// rsrc_patches.cpp:332-341). LocalTalk is disabled, not emulated —
// exactly as under Basilisk. The table is kept open so the other
// Basilisk resource patches can be added if a future disk needs them,
// but nothing speculative is enabled here.
//
// Gate: tests/rsrc_patch_test.cpp.

#pragma once
#include <cstdint>

class V8Memory;

class RsrcPatcher {
public:
    // ROM offset of vCheckLoad (Basilisk: fixed $1b8f4 for $067C ROMs);
    // the V8 machine maps the ROM at $A00000, so the runtime PC is this.
    static constexpr uint32_t kVCheckLoadPc = 0x00A00000 + 0x1B8F4;

    explicit RsrcPatcher(V8Memory& mem) : mem_(mem) {}

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    // Called at the vCheckLoad breakpoint. Reads the resource being
    // loaded from the guest (type, id, data pointer, size) and applies a
    // patch if one matches. `type`/`id`/`dataPtr`/`size` follow the
    // vCheckLoad convention (see .cpp; resolved empirically against the
    // ROM). Returns true if a patch was applied.
    bool checkLoad(uint32_t type, int16_t id, uint32_t dataPtr, uint32_t size);

    long patched = 0;                    // debug counter (# resources patched)

private:
    V8Memory& mem_;
    bool enabled_ = true;

    // Guest memory helpers routed through V8Memory (byte-addressed, BE)
    void poke16(uint32_t addr, uint16_t v);
};

// FourCC helper (host-endian literal, e.g. FOURCC('l','t','l','k'))
constexpr uint32_t FOURCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) << 24 | uint32_t(uint8_t(b)) << 16
         | uint32_t(uint8_t(c)) << 8 | uint32_t(uint8_t(d));
}
