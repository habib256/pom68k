// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The debugger's history export ──
// One text file, written by the machine thread (DebugSession) from the
// CPU adapter's rings. The format, version 1, line by line:
//
//   # POM68K debugger history v1
//   # <key> <value>                  the session identity, as the runner
//                                    gave it (profile, rom, media, …)
//   # model <68000…68040>
//   # instructions <recorded> recorded, <kept> kept, <dropped> dropped
//   I <clock> <pc> <opcode> <sr> <d0>…<d7> <a0>…<a7>
//   # exceptions <recorded> recorded, <kept> kept, <dropped> dropped
//   E <clock> <vector> <stacked pc> <trap word>
//
// Numbers after the tag letter are hexadecimal without prefix, except the
// clocks and the counts, which are decimal. Entries are oldest first. An
// `I` line is the machine at an instruction boundary — the instruction
// about to execute, the registers before it (DebugTypes.h HistoryEntry).
// `dropped` is never omitted: a ring that overflowed says so.
//
// Gate: tests/debug_session_test.cpp (export, then parsed back).

#pragma once
#include "DebugTypes.h"

#include <string>
#include <utility>
#include <vector>

namespace pom68k::dbg {

using IdentityNotes = std::vector<std::pair<std::string, std::string>>;

// Writes the file; false + `why` (in the GUI's language) on failure.
bool exportHistory(const std::string& path, const IdentityNotes& identity,
                   const std::string& model, const Target& target,
                   std::string& why);

} // namespace pom68k::dbg
