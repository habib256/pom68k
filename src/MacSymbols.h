// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac OS names the debugger annotates with ──
// A-line trap names and low-memory global names, the same for every 68k
// Macintosh ROM and System (they are the OS's published interface, not a
// ROM's internals — ROM-internal symbols are DebugSymbols.h, tied to a ROM
// checksum). Provenance: cxmon, Basilisk II's monitor, via the generated
// MacSymbolTables.inc (tools/gen_mac_symbols.py names the commit).
//
// Gate: tests/debug_session_test.cpp (symbol checks).

#pragma once
#include <cstdint>
#include <string>

namespace pom68k::mac {

struct MacTrapName { std::uint16_t word; const char* name; };
struct MacLowMemName { std::uint16_t addr; const char* name; };

// The trap's name without its leading underscore, or nullptr. Matched
// exactly first — the table names common flag variants itself ($A31E is
// NewPtrClear) — then with the flag bits a trap word may carry ignored:
// bit 10 (auto-pop) of a Toolbox trap, bits 8-10 of an OS trap.
const char* trapName(std::uint16_t word);

// "Ticks", "SysZone+$2"… for an address inside a low-memory global; empty
// when none covers it. A global is taken to extend to the next one, but
// never more than kLowMemSpan bytes: past that a label would be a guess.
inline constexpr std::uint32_t kLowMemSpan = 0x40;
std::string lowMemLabel(std::uint32_t addr);

} // namespace pom68k::mac
