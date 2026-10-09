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
//   - disassembly reads through the same logical, side-effect-free path;
//   - an MMU register edit retranslates: after an instruction has run
//     through one table tree (its page now in the ATC), a CRP (030) or
//     URP/SRP (040) edit to a second tree makes the same logical PC run
//     the second tree's page; CACR edits are MOVECs (strobes not stored);
//     a model without the register refuses it;
//   - every board publishes typed device snapshots (VIA, SCC, floppy, SCSI,
//     ADB, video — minus what the hardware lacks) that leave the board's
//     whole serialized state identical, report a VIA register the device
//     holds, and do not clear a pending VIA interrupt flag.
//
// Single-threaded on purpose: the adapter is the machine thread's object,
// and this test plays the machine thread. No ROM, no image.

#include "Cpu030.h"
#include "Cpu040.h"
#include "CentrisMemory.h"
#include "Cpu68k.h"
#include "DebugCpuTarget.h"
#include "DemoRom.h"
#include "IIfxMemory.h"
#include "MacIIMemory.h"
#include "MacMemory.h"
#include "MscMemory.h"
#include "PortableEnv.h"
#include "Q605Memory.h"
#include "Q630Memory.h"
#include "Q700Memory.h"
#include "RbvMemory.h"
#include "SaveState.h"
#include "SonoraMemory.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"
#include "VaspMemory.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

namespace {

using pom68k::dbg::ByteState;
using pom68k::dbg::Reg;
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

// Root pointer upper long: L/U = 0 (upper limit), LIMIT = $7FFF, DT = 2.
constexpr uint64_t kCrpUpper = 0x7FFF0002ull << 32;
constexpr uint32_t kTables2 = 0x00240000;
constexpr uint32_t kPhysPage2 = 0x00340000;
// NOP / MOVEQ #7,D3 / RTS: the second tree's page.
constexpr uint8_t kCode2[] = {0x4E, 0x71, 0x76, 0x07, 0x4E, 0x75};

// Runs the MOVEQ at kLogical+2 once, PC set through the debugger.
template <class Cpu, class Target>
uint32_t runMoveq(Cpu& cpu, Target& t) {
    std::string why;
    cpu.setD(3, 0);
    if (!t.setRegister(Reg::PC, kLogical + 2, why)) return 0xFFFFFFFF;
    cpu.execute();
    return cpu.getD(3);
}

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
    cpu.setCRP(kCrpUpper | kTables);
    cpu.setTC(0x80C04880);
    // The V8 decodes 24 address lines (plus A31), so $10000000 aliases RAM
    // on the real bus; $F00000 is its VIA1.
    checkMappedPage("68030", t, tab, 0x3000, 0x00F00000);

    // The root's LIMIT is part of the mapping: an upper limit of 0 leaves
    // only index 0, and $10000000 (index 1) has no translation.
    cpu.setCRP((0x00000002ull << 32) | kTables);
    std::vector<uint8_t> outside(4);
    check(readAll(t, Space::Logical, kLogical, outside, ByteState::Untranslated),
          "68030: an index above the root's upper limit is Untranslated");
    cpu.setCRP(kCrpUpper | kTables);

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

    // MMU edits: tree 1 runs MOVEQ #5, a CRP edit to tree 2 runs MOVEQ #7.
    // The rig's construction reset fetched ROM-less vectors and halted the
    // CPU (a PC edit does not leave HALT, as on the real part): give it
    // reset vectors in RAM, now that the overlay is down, and reset it.
    {
        uint8_t* v = ram(mem, 0);
        put32(v, 0x8000);
        put32(v + 4, 0x2000);
        cpu.reset();
        cpu.setSR(0x2700);
    }
    check(!cpu.isHalted(), "68030: the rig CPU runs");

    uint8_t* tab2 = ram(mem, kTables2);
    std::memset(tab2, 0, 0x3000);
    put32(tab2 + 1 * 4, (kTables2 + 0x1000) | 2);
    put32(tab2 + 0x1000, (kTables2 + 0x2000) | 2);
    put32(tab2 + 0x2000, kPhysPage2 | 1);
    std::memcpy(ram(mem, kPhysPage2), kCode2, sizeof kCode2);
    // Executing needs the stack and the vectors too: both trees map the
    // first 256 MB identically with an early-termination page descriptor.
    put32(tab, 1);
    put32(tab2, 1);
    std::string why;
    check(t.setRegister(Reg::TC, 0, why) &&
              t.setRegister(Reg::CRP, kCrpUpper | kTables, why) &&
              t.setRegister(Reg::TC, 0x80C04880, why) && cpu.getTC() == 0x80C04880,
          "68030: TC and the 64-bit CRP are editable");
    check(runMoveq(cpu, t) == 5, "68030: an instruction runs through tree 1");
    check(t.setRegister(Reg::CRP, kCrpUpper | kTables2, why) &&
              cpu.getCRP() == (kCrpUpper | kTables2),
          "68030: CRP edited to tree 2");
    check(runMoveq(cpu, t) == 7,
          "68030: the same logical PC now runs tree 2's page (ATC flushed)");
    check(t.setRegister(Reg::CACR, 0x09, why) && (cpu.getCACR() & 0x08) == 0 &&
              (cpu.getCACR() & 0x01) != 0,
          "68030: a CACR edit is a MOVEC: EI kept, the CI strobe not stored");
    t.setRegister(Reg::CACR, 0, why);
    t.setRegister(Reg::TC, 0, why);
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

    uint8_t* tab2 = ram(mem, kTables2);
    std::memset(tab2, 0, 0x3000);
    put32(tab2 + (kLogical >> 25) * 4, (kTables2 + 0x1000) | 2);
    put32(tab2 + 0x1000, (kTables2 + 0x2000) | 2);
    put32(tab2 + 0x2000, kPhysPage2 | 1);
    std::memcpy(ram(mem, kPhysPage2), kCode2, sizeof kCode2);
    check(runMoveq(cpu, t) == 5, "68040: an instruction runs through tree 1");
    std::string why;
    check(t.setRegister(Reg::SRP040, kTables2, why) &&
              t.setRegister(Reg::URP040, kTables2, why) && cpu.getSRP040() == kTables2,
          "68040: SRP and URP edited to tree 2");
    check(runMoveq(cpu, t) == 7,
          "68040: the same logical PC now runs tree 2's page (ATCs flushed)");
    check(!t.setRegister(Reg::TC, 0x80C04880, why) && !why.empty(),
          "68040: the 68030's TC is refused on a 68040");
    check(t.setRegister(Reg::TC040, 0, why) && cpu.getTC040() == 0,
          "68040: TC040 is editable");
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
    std::string why;
    check(!t.setRegister(Reg::CACR, 1, why) && !t.setRegister(Reg::TC, 1, why) &&
              !why.empty(),
          "68000: CACR and MMU registers are refused");
}

// ── Device snapshots, board by board ────────────────────────────────
template <class Mem>
std::vector<uint8_t> boardState(Mem& mem) {
    std::vector<uint8_t> b;
    sav::Writer w(b);
    mem.visit(w);
    return b;
}

template <class Mem, class Via>
void boardDevices(const char* board, Mem& mem, Via& via1,
                  std::set<std::string> lacks = {}) {
    char what[200];
    // A known VIA register, and a pending flag a register read would clear.
    via1.write(2, 0x5A);                        // DDRB
    via1.write(14, 0x7F);                       // IER: disable all
    via1.write(13, 0x7F);                       // IFR: clear all
    via1.loadSR(0x33);                          // sets IFR.SHIFT
    const uint8_t ifr = via1.ifrRaw();
    const auto before = boardState(mem);
    std::vector<pom68k::dev::Snapshot> d;
    mem.debugDevices(d);
    mem.debugDevices(d);
    const auto after = boardState(mem);
    std::snprintf(what, sizeof what,
                  "%s: device snapshots leave the board's state identical", board);
    check(before == after && via1.ifrRaw() == ifr, what);

    std::set<std::string> kinds;
    for (const auto& s : d) kinds.insert(s.kind);
    bool all = true;
    for (const char* k : {"VIA", "SCC", "Floppy", "SCSI", "ADB", "Video"})
        if (!lacks.count(k) && !kinds.count(k)) {
            std::printf("  %s: no %s snapshot\n", board, k);
            all = false;
        }
    for (const auto& k : lacks)
        if (kinds.count(k)) all = false;    // a part the hardware lacks
    std::snprintf(what, sizeof what,
                  "%s: VIA, SCC, floppy, SCSI, ADB and video are published%s", board,
                  lacks.empty() ? "" : " (minus the parts it lacks)");
    check(all, what);

    bool ddrb = false, ifrSeen = false;
    for (const auto& s : d) {
        if (s.kind != "VIA" || (s.name != "VIA1" && s.name != "VIA")) continue;
        for (const auto& f : s.fields) {
            ddrb |= f.name == "ddrb" && f.value == 0x5A;
            ifrSeen |= f.name == "ifr" && f.value == ifr && (ifr & 0x04);
        }
    }
    std::snprintf(what, sizeof what,
                  "%s: VIA1 reports DDRB and the pending SHIFT flag", board);
    check(ddrb && ifrSeen, what);
}

void devices() {
    const auto& cfg = pom68k::defaultCoreConfig();
    {
        auto m = std::make_unique<MacMemory>(cfg, MacMemory::Model::Plus);
        boardDevices("Plus", *m, m->via(), {"ADB"});
    }
    {
        auto m = std::make_unique<MacMemory>(cfg, MacMemory::Model::SE);
        boardDevices("SE", *m, m->via());
    }
    {
        auto m = std::make_unique<MacIIMemory>(cfg);
        boardDevices("Mac II (no video card)", *m, m->via1(), {"Video"});
    }
    {
        auto m = std::make_unique<IIfxMemory>(cfg);
        boardDevices("IIfx (no video card)", *m, m->via1(), {"Video"});
    }
    {
        auto m = std::make_unique<V8Memory>(cfg);
        boardDevices("LC II", *m, m->via1());
    }
    {
        auto m = std::make_unique<V8Memory>(cfg, 0xA00000, V8Memory::Model::ColorClassic);
        boardDevices("Color Classic", *m, m->via1());
    }
    {
        auto m = std::make_unique<RbvMemory>(cfg);
        boardDevices("IIsi", *m, m->via1());
    }
    {
        auto m = std::make_unique<RbvMemory>(cfg, 0x800000, RbvMemory::kCpuHz, true);
        boardDevices("IIci", *m, m->via1());
    }
    {
        auto m = std::make_unique<SonoraMemory>(cfg);
        boardDevices("LC III", *m, m->via1());
    }
    {
        auto m = std::make_unique<VaspMemory>(cfg);
        boardDevices("IIvx", *m, m->via1());
    }
    {
        auto m = std::make_unique<Q605Memory>(cfg, 8u << 20);
        boardDevices("Quadra 605", *m, m->via1());
    }
    {
        auto m = std::make_unique<CentrisMemory>(cfg, 8u << 20);
        boardDevices("Centris 650", *m, m->via1());
    }
    {
        auto m = std::make_unique<Q700Memory>(cfg, 8u << 20);
        boardDevices("Quadra 700", *m, m->via1());
    }
    {
        auto m = std::make_unique<Q700Memory>(cfg, 8u << 20, Q700Memory::kCpuHz,
                                              Q700Memory::Model::Q900);
        boardDevices("Quadra 900", *m, m->via1());
    }
    {
        auto m = std::make_unique<Q630Memory>(cfg, 8u << 20);
        boardDevices("Quadra 630", *m, m->via1());
    }
    {
        auto m = std::make_unique<MscMemory>(cfg);
        boardDevices("Duo 230", *m, m->via1(), {"Floppy"});
    }
}

} // namespace

int main() {
    mmu030();
    mmu040();
    compact();
    devices();
    std::printf("%s\n", gFails ? "debug_inspection_test: FAILED"
                               : "debug_inspection_test: all checks passed");
    return gFails ? 1 : 0;
}
