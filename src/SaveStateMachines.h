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

namespace pom68k {

// Machine profile tag stored in the header. Values are part of the file
// format — append, never renumber. One tag per PROFILE, not per machine
// class: identity twins share a ROM (LC III / LC III+, Q605 / LC 475), so
// the header's ROM checksum cannot tell them apart and the tag must.
enum class SnapMachine : std::uint32_t {
    LcII = 1,
    // V8 siblings (same device tree as the LC II)
    Lc = 2, ClassicII = 3, ColorClassic = 4, MacTv = 5,
    // Sonora family
    Lc3 = 6, Lc3Plus = 7, Lc520 = 8, Lc550 = 9, CClassic2 = 10,
    // VASP
    IIvx = 11, IIvi = 12,
    // RBV
    IIsi = 13, IIci = 14,
    // Q605 (MEMCjr) family
    Q605 = 15, Lc475 = 16, Lc575 = 17,
    // Centris/Quadra (djMEMC + IOSB) family
    Centris610 = 18, Centris650 = 19,
    Quadra610 = 20, Quadra650 = 21, Quadra800 = 22,
    // Discrete 040 + last 68k desktop
    Q700 = 23, Q630 = 24, Lc580 = 25,
    // Mac II family (GLUE board)
    MacII = 26, IIx = 27, IIcx = 28,
    // Compact 68000 family
    Plus = 29, SE = 30, SEFDHD = 31, Classic = 32,
    // Appended 2026-07-31 (values are file format): the compact IIx
    SE30 = 33,
    // Appended 2026-08-01: platform #12 (OSS + dual Apple PIC IOPs)
    IIfx = 34,
};

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

void save(MacMemory& mem, Cpu68k& cpu, SnapMachine kind,
          std::vector<std::uint8_t>& out);
bool load(MacMemory& mem, Cpu68k& cpu, SnapMachine kind,
          const std::uint8_t* data, std::size_t len, std::string& err);

}  // namespace pom68k
