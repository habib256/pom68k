// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for the debugger's inspection contract (src/DebugCpuTarget.h):
//
//   - a LOGICAL address is translated without disturbing the MMU — through
//     a 68030 PMMU table tree (Mmu030Peek.h) and a 68040 one (Mmu040Peek.h),
//     with the descriptors' U/M bits left exactly as they were;
//   - an untranslated address is reported, not guessed;
//   - device registers are never read: inspecting the compact Mac's VIA,
//     SCC and IWM windows, and the Quadra's I/O space through a DTT window,
//     leaves the whole machine's save-state bytes identical — the strongest
//     "no side effect" claim the tree can make;
//   - disassembly reads through the same logical, side-effect-free path.
//
// Single-threaded on purpose: the adapter is the machine thread's object,
// and this test plays the machine thread. No ROM, no image.

#include "Cpu030.h"
#include "Cpu040.h"
#include "Cpu68k.h"
#include "DebugCpuTarget.h"
#include "DemoRom.h"
#include "MacMemory.h"
#include "PortableEnv.h"
#include "Q605Memory.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

namespace {

using pom68k::dbg::ByteState;
using pom68k::dbg::Space;

template <class Mem>
uint8_t* ram(Mem& mem, uint32_t phys) {
    uint32_t len = 0;
    return mem.dataSpan(phys, len, true);
}

void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

template <class Target>
bool readAll(Target& t, Space space, uint32_t addr, std::vector<uint8_t>& out,
             ByteState want) {
    std::vector<ByteState> st(out.size());
    t.readMemory(space, addr, out.data(), st.data(), out.size());
    for (ByteState s : st)
        if (s != want) return false;
    return true;
}

constexpr uint32_t kLogical = 0x10000000;
constexpr uint32_t kPhysPage = 0x00300000;
constexpr uint32_t kTables = 0x00200000;
// NOP / MOVEQ #5,D3 / RTS at the mapped page.
constexpr uint8_t kCode[] = {0x4E, 0x71, 0x76, 0x05, 0x4E, 0x75};

template <class Target>
void checkMappedPage(const char* fam, Target& t, const uint8_t* tables,
                     size_t tableBytes, uint32_t notMemory) {
    const std::vector<uint8_t> before(tables, tables + tableBytes);
    std::vector<uint8_t> got(sizeof kCode);
    bool ok = readAll(t, Space::Logical, kLogical, got, ByteState::Ok) &&
              std::memcmp(got.data(), kCode, sizeof kCode) == 0;
    char what[160];
    std::snprintf(what, sizeof what,
                  "%s: a logical address reads its physical page", fam);
    check(ok, what);
    std::vector<uint8_t> phys(sizeof kCode);
    ok = readAll(t, Space::Physical, kPhysPage, phys, ByteState::Ok) && phys == got;
    std::snprintf(what, sizeof what, "%s: the physical view agrees", fam);
    check(ok, what);
    std::vector<uint8_t> none(4);
    std::snprintf(what, sizeof what,
                  "%s: an address without a descriptor is Untranslated", fam);
    check(readAll(t, Space::Logical, 0x20000000, none, ByteState::Untranslated), what);
    std::snprintf(what, sizeof what,
                  "%s: a physical I/O or unmapped address is NotMemory", fam);
    check(readAll(t, Space::Physical, notMemory, none, ByteState::NotMemory), what);
    auto line = t.disassemble(kLogical + 2);
    std::snprintf(what, sizeof what,
                  "%s: disassembly reads the translated page (%s)", fam,
                  line.text.c_str());
    check(line.readable && line.length == 2 &&
          line.text.find("moveq") != std::string::npos, what);
    std::snprintf(what, sizeof what,
                  "%s: the walk leaves every descriptor (U/M bits) untouched", fam);
    check(std::memcmp(before.data(), tables, tableBytes) == 0, what);
}

void mmu030() {
    const auto& cfg = pom68k::defaultCoreConfig();
    static V8Memory mem(cfg);
    static Cpu030 cpu(mem, jit::defaultResolvedConfig(), cfg.cpu);
    mem.setCpu(&cpu);
    (void)mem.read8(0xA00000);                   // drop the boot overlay
    pom68k::dbg::Session session;
    pom68k::dbg::CpuTarget<Cpu030, V8Memory> t(cpu, mem, session);

    // TC: E, PS = 4 KB, IS = 0, TIA = 4, TIB = 8, TIC = 8 (sum 32).
    // CRP: short-format table A. A[1] → B, B[0] → C, C[0] → page.
    uint8_t* tab = ram(mem, kTables);
    check(tab != nullptr, "68030: page tables live in plain RAM");
    if (!tab) return;
    std::memset(tab, 0, 0x3000);
    put32(tab + 1 * 4, (kTables + 0x1000) | 2);
    put32(tab + 0x1000, (kTables + 0x2000) | 2);
    put32(tab + 0x2000, kPhysPage | 1);
    std::memcpy(ram(mem, kPhysPage), kCode, sizeof kCode);
    cpu.setSR(0x2700);
    cpu.setCRP((2ull << 32) | kTables);
    cpu.setTC(0x80C04880);
    // The V8 decodes 24 address lines (plus A31), so $10000000 aliases RAM
    // on the real bus; $F00000 is its VIA1.
    checkMappedPage("68030", t, tab, 0x3000, 0x00F00000);

    // TT0 maps $50xxxxxx transparently for supervisor data: the V8's I/O
    // decode is refused, not read.
    std::vector<uint8_t> before, after;
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, before);
    cpu.setTT0(0x50008107);                     // base $50, E, RWM, FC any
    std::vector<uint8_t> io(0x100);
    check(readAll(t, Space::Logical, 0x50F00000, io, ByteState::NotMemory) &&
          readAll(t, Space::Physical, 0x50F00000, io, ByteState::NotMemory),
          "68030: TT-mapped VIA space is NotMemory in both views");
    cpu.setTT0(0);
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, after);
    check(before == after, "68030: inspecting I/O leaves the machine state identical");
    cpu.setTC(0);
}

void mmu040() {
    const auto& cfg = pom68k::defaultCoreConfig();
    static Q605Memory mem(cfg, 8u << 20);
    static Cpu040 cpu(mem, jit::defaultResolvedConfig(), cfg.cpu, cfg.diagnostics);
    mem.setCpu(&cpu);
    (void)mem.read8(0x40000000);                 // drop the boot overlay
    pom68k::dbg::Session session;
    pom68k::dbg::CpuTarget<Cpu040, Q605Memory> t(cpu, mem, session);

    // Root (512-aligned) → pointer table → 4 KB page table → page.
    uint8_t* tab = ram(mem, kTables);
    check(tab != nullptr, "68040: page tables live in plain RAM");
    if (!tab) return;
    std::memset(tab, 0, 0x3000);
    put32(tab + (kLogical >> 25) * 4, (kTables + 0x1000) | 2);
    put32(tab + 0x1000, (kTables + 0x2000) | 2);
    put32(tab + 0x2000, kPhysPage | 1);
    std::memcpy(ram(mem, kPhysPage), kCode, sizeof kCode);
    cpu.setSR(0x2700);
    cpu.setSRP040(kTables);
    cpu.setURP040(kTables);
    cpu.setTC040(0x8000);
    checkMappedPage("68040", t, tab, 0x3000, kLogical);

    std::vector<uint8_t> before, after;
    pom68k::save(mem, cpu, pom68k::SnapMachine::Q605, before);
    cpu.setDTT0(0x5000C000);                    // $50xxxxxx, E, both modes
    std::vector<uint8_t> io(0x200);
    check(readAll(t, Space::Logical, 0x50F00000, io, ByteState::NotMemory),
          "68040: DTT-mapped VIA/SCC space is NotMemory, never read");
    check(t.disassemble(0x50F00000).readable == false,
          "68040: disassembling I/O space reads nothing");
    cpu.setDTT0(0);
    pom68k::save(mem, cpu, pom68k::SnapMachine::Q605, after);
    check(before == after, "68040: inspecting I/O leaves the machine state identical");
    cpu.setTC040(0);
}

// The compact map has no MMU, and every one of its device windows has a
// read side effect: VIA (IFR clear), SCC (RR0 latch), IWM (state lines).
void compact() {
    const auto& cfg = pom68k::defaultCoreConfig();
    static MacMemory mem(cfg, MacMemory::Model::Plus);
    mem.installRom(kDemoRom, kDemoRomSize);
    static Cpu68k cpu(mem, jit::defaultResolvedConfig());
    mem.setCpu(&cpu);
    cpu.hardReset();
    cpu.runCycles(300000);                      // let the demo raise VIA flags
    pom68k::dbg::Session session;
    pom68k::dbg::CpuTarget<Cpu68k, MacMemory> t(cpu, mem, session);

    std::vector<uint8_t> before, after;
    pom68k::save(mem, cpu, pom68k::SnapMachine::Plus, before);
    bool refused = true;
    for (uint32_t base : {0xEFE1FEu, 0x9FFFF8u, 0xBFFFF9u, 0xDFE1FFu}) {
        std::vector<uint8_t> io(0x40);
        refused = refused && readAll(t, Space::Physical, base, io, ByteState::NotMemory)
                          && readAll(t, Space::Logical, base, io, ByteState::NotMemory);
    }
    check(refused, "68000: VIA, SCC and IWM windows are NotMemory");
    std::vector<uint8_t> rom(16);
    check(readAll(t, Space::Logical, 0x400000, rom, ByteState::Ok) &&
          rom[0] == kDemoRom[0], "68000: the ROM window is inspectable");
    std::vector<uint8_t> high(4);
    check(readAll(t, Space::Logical, 0xFF400000, high, ByteState::Ok) &&
          high[0] == kDemoRom[0],
          "68000: a logical address keeps only the 24 address lines");
    (void)t.disassemble(0xEFE1FE);
    pom68k::save(mem, cpu, pom68k::SnapMachine::Plus, after);
    check(before == after, "68000: inspection leaves the machine state identical");
}

} // namespace

int main() {
    mmu030();
    mmu040();
    compact();
    std::printf("%s\n", gFails ? "debug_inspection_test: FAILED"
                               : "debug_inspection_test: all checks passed");
    return gFails ? 1 : 0;
}
