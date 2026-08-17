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
#include "MachineCatalog.h"
#include "SaveState.h"
#include <cstdint>
#include <string>
#include <vector>

class V8Memory;
class Cpu030;
class SonoraMemory;
class SonoraCpu;
class VaspMemory;
class VaspCpu;
class RbvMemory;
class RbvCpu;
class Q605Memory;
class Cpu040;
class CentrisMemory;
class CentrisCpu;
class Q700Memory;
class Q700Cpu;
class Q630Memory;
class Q630Cpu;
class MacIIMemory;
class Cpu020;
class MacMemory;
class Cpu68k;
class IIfxMemory;
class IIfxCpu;
class MscMemory;
class MscCpu;

namespace pom68k {

// One save/load pair per machine family; `kind` pins the profile inside
// the family. save() writes the container (header + CPU + machine chunks);
// load() returns false and leaves the machine untouched when the snapshot
// does not match — `err` then explains why. Chunks the build does not know
// are skipped and counted in `err` as a warning, not a failure.
void save(V8Memory& mem, Cpu030& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(V8Memory& mem, Cpu030& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(SonoraMemory& mem, SonoraCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(SonoraMemory& mem, SonoraCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(VaspMemory& mem, VaspCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(VaspMemory& mem, VaspCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(RbvMemory& mem, RbvCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(RbvMemory& mem, RbvCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(Q605Memory& mem, Cpu040& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(Q605Memory& mem, Cpu040& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(CentrisMemory& mem, CentrisCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(CentrisMemory& mem, CentrisCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(Q700Memory& mem, Q700Cpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(Q700Memory& mem, Q700Cpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(Q630Memory& mem, Q630Cpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(Q630Memory& mem, Q630Cpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(MacIIMemory& mem, Cpu020& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(MacIIMemory& mem, Cpu020& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(IIfxMemory& mem, IIfxCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(IIfxMemory& mem, IIfxCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(MscMemory& mem, MscCpu& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(MscMemory& mem, MscCpu& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

void save(MacMemory& mem, Cpu68k& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(MacMemory& mem, Cpu68k& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

}  // namespace pom68k
