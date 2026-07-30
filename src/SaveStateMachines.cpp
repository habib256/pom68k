// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Save states: machine-level assembly ──
// Where a machine's chunks are written and read back. The per-device state
// lives in each device's own visit() (SaveState.h explains the contract);
// this file owns the order, the identity checks and the re-binding that a
// restore needs.
//
// It also carries the explicit instantiations below. A visit() body is a
// template, so it is not compiled until something instantiates it — a
// device could carry a visit() that does not build for months and every
// gate would still be green. Naming both archives here makes the compiler
// check every one of them on each build.

#include "AdbBus.h"
#include "AdbLine.h"
#include "AdbVia.h"
#include "Asc.h"
#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include "Cpu020.h"
#include "Cpu030.h"
#include "Cpu040.h"
#include "Cpu68k.h"
#include "CudaLle.h"
#include "Dafb.h"
#include "Egret.h"
#include "Iwm.h"
#include "M68hc05.h"
#include "MacIIMemory.h"
#include "MacInput.h"
#include "MacMemory.h"
#include "Ncr5380.h"
#include "Ncr53c96.h"
#include "Pic1654s.h"
#include "PseudoVia.h"
#include "Q605Memory.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "Rtc.h"
#include "SaveState.h"
#include "ScsiDisk.h"
#include "SaveStateMachines.h"
#include "Scc8530.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "SonyDrive.h"
#include "Swim1.h"
#include "Swim2.h"
#include "TobyVideo.h"
#include "V8Memory.h"
#include "Valkyrie.h"
#include "VaspCpu.h"
#include "VaspMemory.h"
#include "Via6522.h"
#include <cstring>

// ── Compile-time check of every chunk ───────────────────────────────────
// One line per archive per class. CudaLle's body nests M68hc05 and AdbLine,
// but they are named explicitly anyway: an unused device chunk that stops
// compiling should fail the build on the day it breaks, not on the day a
// machine first happens to reference it.
#define POM68K_SAV_CHECK(Klass)                                    \
    template void Klass::visit<sav::Writer>(sav::Writer&);         \
    template void Klass::visit<sav::Reader>(sav::Reader&)

POM68K_SAV_CHECK(Cpu030);
POM68K_SAV_CHECK(SonoraCpu);
POM68K_SAV_CHECK(VaspCpu);
POM68K_SAV_CHECK(RbvCpu);
POM68K_SAV_CHECK(Cpu040);
POM68K_SAV_CHECK(CentrisCpu);
POM68K_SAV_CHECK(Q700Cpu);
POM68K_SAV_CHECK(Q630Cpu);
POM68K_SAV_CHECK(Cpu020);
POM68K_SAV_CHECK(Cpu68k);
POM68K_SAV_CHECK(MacKeyboard);
POM68K_SAV_CHECK(MacMouse);
POM68K_SAV_CHECK(NuBus);
POM68K_SAV_CHECK(TobyVideo);
POM68K_SAV_CHECK(Rtc);
POM68K_SAV_CHECK(Pic1654s);
POM68K_SAV_CHECK(AdbVia);
POM68K_SAV_CHECK(Dafb);
POM68K_SAV_CHECK(Valkyrie);
POM68K_SAV_CHECK(AscIosb);
POM68K_SAV_CHECK(Ncr53c96);
POM68K_SAV_CHECK(Via6522);
POM68K_SAV_CHECK(PseudoVia);
POM68K_SAV_CHECK(AdbBus);
POM68K_SAV_CHECK(AdbLine);
POM68K_SAV_CHECK(M68hc05);
POM68K_SAV_CHECK(CudaLle);
POM68K_SAV_CHECK(Egret);
POM68K_SAV_CHECK(ScsiDisk);
POM68K_SAV_CHECK(AscV8);
POM68K_SAV_CHECK(AscSonora);
POM68K_SAV_CHECK(Ncr5380);
POM68K_SAV_CHECK(Iwm);
POM68K_SAV_CHECK(Swim1);
POM68K_SAV_CHECK(Swim2);
POM68K_SAV_CHECK(SonyDrive);
POM68K_SAV_CHECK(Ariel);
POM68K_SAV_CHECK(Scc8530);
POM68K_SAV_CHECK(V8Memory);       // the LC II machine chunk (nests all of the above)
POM68K_SAV_CHECK(SonoraMemory);   // LC III / III+ / 520 / 550 / CC II
POM68K_SAV_CHECK(VaspMemory);     // IIvx / IIvi
POM68K_SAV_CHECK(RbvMemory);      // IIsi / IIci (nests AdbVia + Rtc too)
POM68K_SAV_CHECK(Q605Memory);     // Q605 / LC 475 / LC 575
POM68K_SAV_CHECK(CentrisMemory);  // Centris 610/650, Quadra 610/650/800
POM68K_SAV_CHECK(Q700Memory);     // Quadra 700
POM68K_SAV_CHECK(Q630Memory);     // Quadra 630 / LC 580
POM68K_SAV_CHECK(MacIIMemory);    // Mac II / IIx / IIcx (nests NuBus + Toby)
POM68K_SAV_CHECK(MacMemory);      // Plus / SE / SE FDHD / Classic

// ── Container assembly ──────────────────────────────────────────────────
namespace pom68k {
namespace {

// Chunk tags. Four characters, part of the file format.
constexpr char kHead[4] = {'H','E','A','D'};
constexpr char kCpu [4] = {'C','P','U',' '};
constexpr char kMach[4] = {'M','A','C','H'};

// The container body is machine-shape-independent: any (Mem, Cpu) pair
// exposing romChecksum()/ramBytes()/machineClock() and a visit() each goes
// through the same header + chunk assembly. The public overloads below are
// one-line forwards, so adding a machine costs a declaration, not a copy of
// the validation logic.
template <class Mem, class Cpu>
void saveT(Mem& mem, Cpu& cpu, SnapMachine kind,
           std::vector<std::uint8_t>& out) {
    out.clear();
    out.insert(out.end(), sav::kMagic, sav::kMagic + sizeof sav::kMagic);

    sav::Header h;
    h.machineKind = static_cast<std::uint32_t>(kind);
    h.romChecksum = mem.romChecksum();
    h.ramSize     = mem.ramBytes();
    h.emuCycles   = static_cast<std::uint64_t>(cpu.machineClock());
    { sav::Chunk c(out, kHead); sav::Writer w(out); w(h); }
    { sav::Chunk c(out, kCpu);  sav::Writer w(out); w(cpu); }
    { sav::Chunk c(out, kMach); sav::Writer w(out); w(mem); }
}

template <class Mem, class Cpu>
bool loadT(Mem& mem, Cpu& cpu, SnapMachine kind,
           const std::uint8_t* data, std::size_t len, std::string& err) {
    err.clear();
    if (len < sizeof sav::kMagic
        || std::memcmp(data, sav::kMagic, sizeof sav::kMagic) != 0) {
        err = "not a POM68K save state";
        return false;
    }

    const std::uint8_t* cur = data + sizeof sav::kMagic;
    const std::uint8_t* end = data + len;

    // Pass 1: locate and validate the header before mutating anything. A
    // rejected snapshot must leave the running machine exactly as it was.
    sav::ChunkView cv, cpuChunk, machChunk;
    bool haveHead = false, haveCpu = false, haveMach = false;
    int unknown = 0;
    for (const std::uint8_t* p = cur; sav::nextChunk(p, end, cv); ) {
        if (cv.is(kHead)) {
            sav::Header h;
            auto r = cv.reader();
            r(h);
            if (!r.ok()) { err = "truncated header"; return false; }
            if (h.version != sav::kVersion) {
                err = "save state version " + std::to_string(h.version)
                    + ", this build writes " + std::to_string(sav::kVersion);
                return false;
            }
            if (h.machineKind != static_cast<std::uint32_t>(kind)) {
                err = "save state is for another machine profile";
                return false;
            }
            if (h.romChecksum != mem.romChecksum()) {
                err = "save state was taken with a different ROM";
                return false;
            }
            if (h.ramSize != mem.ramBytes()) {
                err = "save state has " + std::to_string(h.ramSize)
                    + " bytes of RAM, this machine has "
                    + std::to_string(mem.ramBytes());
                return false;
            }
            haveHead = true;
        } else if (cv.is(kCpu))  { cpuChunk = cv;  haveCpu  = true; }
        else if (cv.is(kMach))   { machChunk = cv; haveMach = true; }
        else                     { ++unknown; }
    }

    if (!haveHead) { err = "save state has no header chunk"; return false; }
    if (!haveCpu || !haveMach) {
        err = "save state is missing the CPU or machine chunk";
        return false;
    }

    // Pass 2: apply. The machine chunk goes first so the CPU's cache flush
    // (MoiraSnapshot, on load) happens against the restored RAM.
    { auto r = machChunk.reader();
      r(mem);
      if (!r.ok()) { err = "machine chunk is corrupt"; return false; } }
    { auto r = cpuChunk.reader();
      r(cpu);
      if (!r.ok()) { err = "CPU chunk is corrupt"; return false; } }

    if (unknown)
        err = "warning: skipped " + std::to_string(unknown)
            + " unknown chunk(s) written by a newer build";
    return true;
}

}  // namespace

// ── Public per-machine entry points (declared in SaveStateMachines.h) ───
#define POM68K_SAV_MACHINE(Mem, Cpu)                                        \
    void save(Mem& mem, Cpu& cpu, SnapMachine kind,                         \
              std::vector<std::uint8_t>& out) {                             \
        saveT(mem, cpu, kind, out);                                         \
    }                                                                       \
    bool load(Mem& mem, Cpu& cpu, SnapMachine kind,                         \
              const std::uint8_t* data, std::size_t len, std::string& err) {\
        return loadT(mem, cpu, kind, data, len, err);                       \
    }

POM68K_SAV_MACHINE(V8Memory, Cpu030)
POM68K_SAV_MACHINE(SonoraMemory, SonoraCpu)
POM68K_SAV_MACHINE(VaspMemory, VaspCpu)
POM68K_SAV_MACHINE(RbvMemory, RbvCpu)
POM68K_SAV_MACHINE(Q605Memory, Cpu040)
POM68K_SAV_MACHINE(CentrisMemory, CentrisCpu)
POM68K_SAV_MACHINE(Q700Memory, Q700Cpu)
POM68K_SAV_MACHINE(Q630Memory, Q630Cpu)
POM68K_SAV_MACHINE(MacIIMemory, Cpu020)
POM68K_SAV_MACHINE(MacMemory, Cpu68k)

}  // namespace pom68k
