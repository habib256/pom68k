// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Save states: per-machine save/load entry points ──
// One pair per machine family. Each writes the container (magic + header +
// chunks) and, on the way back, refuses a snapshot that does not belong to
// the running machine before touching a single byte of its state.
//
// Refusing early matters: a half-applied snapshot is worse than none. The
// loader validates the magic, the format version, the machine profile, the
// ROM checksum and the RAM size, and only then hands each chunk to its
// device.

#pragma once
#include "SaveState.h"
#include <cstdint>
#include <string>
#include <vector>

class V8Memory;
class Cpu030;

namespace pom68k {

// Machine profile tag stored in the header. Values are part of the file
// format — append, never renumber.
enum class SnapMachine : std::uint32_t {
    LcII = 1,
};

// Serializes the whole V8-family machine (LC / LC II / Classic II / Color
// Classic / Mac TV share the tree; `kind` distinguishes the profile).
void save(V8Memory& mem, Cpu030& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);

// Returns false and leaves the machine untouched when the snapshot does not
// match; `err` then explains why. Chunks the build does not know are
// skipped and counted in `err` as a warning, not a failure.
bool load(V8Memory& mem, Cpu030& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

}  // namespace pom68k
