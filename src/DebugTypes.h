// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Debugger vocabulary: commands, snapshots and the target interface ──
// Plain data shared by the GUI thread and the machine thread. The GUI never
// holds a CPU or memory pointer: it posts a Command and reads an immutable
// Snapshot that the machine thread built (DebugSession.h). The Target is the
// machine thread's side — one adapter per CPU/memory pair
// (DebugCpuTarget.h).

#pragma once
#include "DeviceSnapshot.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pom68k::dbg {

// Logical: what the CPU's program sees, translated by the CPU's own MMU or
// transparent-translation registers. Physical: the CPU bus address, decoded
// by the board exactly as a read would be. The board's own remaps (the
// GLUE's 24-bit mode, a boot overlay) belong to Physical.
enum class Space : std::uint8_t { Logical, Physical };

// Per-byte verdict of an inspection. Only bytes backed by plain RAM, ROM or
// a framebuffer are ever read; device registers are refused, never read
// through the bus.
enum class ByteState : std::uint8_t {
    Ok = 0,
    Untranslated,    // the logical address has no valid descriptor
    NotMemory        // I/O, unmapped, or a window a read would disturb
};

enum class StopReason : std::uint8_t {
    None, Pause, Step, Breakpoint,
    Watchpoint,      // after the instruction that made the access retired
    Exception        // at the handler's first instruction, frame stacked
};

// A data-access stop. `addr` is LOGICAL — the address the program used,
// after the CPU's own 24-bit masking, before any MMU translation — so it
// follows the program, not the page. Only DATA-space accesses (function
// codes 1 and 5) match. Program-space reads never do: opcode and extension
// words, and also PC-relative operands, which the 68k reads in program
// space too; an execution stop is a breakpoint. Device DMA is not a CPU
// access and never matches. Exception processing's own stack and vector
// accesses are data accesses and do match.
enum class Access : std::uint8_t { Read = 1, Write = 2, ReadWrite = 3 };
struct Watchpoint {
    std::uint32_t addr = 0;
    std::uint8_t length = 1;         // 1..kMaxWatchLength bytes
    Access access = Access::Write;
    bool operator==(const Watchpoint&) const = default;
};

// An exception stop: the CPU accepted `vector` (2 bus error … 10 A-line,
// 11 F-line, 24-31 spurious/autovector interrupts, 32-47 TRAP #n …) and
// stacked its frame. For vector 10 a non-zero `trap` narrows the stop to
// one Toolbox/OS trap: the word at the stacked PC is compared with the
// flag bits ignored — bit 10 (auto-pop) of a Toolbox trap ($A800-$AFFF),
// bits 8-10 (the two flag bits and "don't preserve A0") of an OS trap
// ($A000-$A7FF), per the trap word layout of Inside Macintosh II (The
// Operating System Utilities, "The Trap Dispatcher").
struct Catch {
    std::uint8_t vector = 0;
    std::uint16_t trap = 0;          // vector 10 only; 0 = every A-line
    bool operator==(const Catch&) const = default;
};

// Opt-in histories (bounded rings, allocated when enabled). An instruction
// entry is the machine at an instruction boundary: the instruction about
// to execute and the registers before it — the next entry shows its
// effect. An exception entry is an accepted vector with its stacked PC
// (and the A-line word for vector 10). The rings keep the newest entries;
// `recorded` counts every entry ever made, so recorded > capacity says how
// many of the oldest were dropped — never silently.
struct HistoryEntry {
    std::int64_t clock = 0;          // Moira core clock at the boundary
    std::uint32_t pc = 0;
    std::uint16_t opcode = 0;        // 0 when the PC is not readable memory
    std::uint16_t sr = 0;
    std::array<std::uint32_t, 8> d{}, a{};
};
struct TrapEntry {
    std::int64_t clock = 0;
    std::uint32_t stackedPc = 0;
    std::uint16_t trapWord = 0;
    std::uint8_t vector = 0;
};
inline constexpr std::size_t kHistoryCapacity = 16384;
inline constexpr std::size_t kTrapHistoryCapacity = 1024;
inline constexpr std::size_t kHistoryTail = 32;      // entries per snapshot

// What the last stop was about, beyond its PC.
struct StopDetail {
    // Watchpoint: the access and the instruction that made it.
    std::uint32_t accessAddr = 0;
    std::uint8_t accessSize = 0;
    bool accessWrite = false;
    std::uint32_t instructionPc = 0;
    // Exception: the vector, the stacked PC, and the A-line word if any.
    std::uint8_t vector = 0;
    std::uint32_t stackedPc = 0;
    std::uint16_t trapWord = 0;
};

// The registers an edit can name. D0-D7/A0-A7 in order, so `Reg(D0 + n)`
// works. A7 is the ACTIVE stack pointer, as the CPU sees it; USP/ISP/MSP
// name a bank whichever one is active. ISP is the 68000's SSP. VBR, SFC
// and DFC exist from the 68010, MSP and CACR from the 68020. The MMU
// registers exist on the MMU-bearing model only: TC/CRP/SRP/TT0/TT1 on the
// 68030 (CRP and SRP are 64-bit), TC040/URP040/SRP040/DTT/ITT on the
// 68040 and 68LC040. Their edit semantics are in Target::setRegister.
enum class Reg : std::uint8_t {
    D0 = 0, A0 = 8, PC = 16, SR, USP, ISP, MSP, VBR, SFC, DFC,
    CACR, TC, CRP, SRP, TT0, TT1,
    TC040, URP040, SRP040, DTT0, DTT1, ITT0, ITT1,
    Count
};

// Bounds on what one snapshot can carry. A request beyond them is clamped
// and the snapshot says so.
inline constexpr std::uint32_t kMaxMemoryBytes = 4096;
inline constexpr int kMaxDisasmLines = 64;
inline constexpr std::size_t kMaxBreakpoints = 256;
inline constexpr std::size_t kMaxEditBytes = 256;
inline constexpr std::size_t kMaxWatchpoints = 32;
inline constexpr std::uint8_t kMaxWatchLength = 16;
inline constexpr std::size_t kMaxCatches = 64;

struct Registers {
    std::array<std::uint32_t, 8> d{}, a{};
    std::uint32_t pc = 0, usp = 0, isp = 0, msp = 0, vbr = 0;
    std::uint32_t sfc = 0, dfc = 0, cacr = 0;
    std::uint16_t sr = 0;
    // 68030 PMMU
    std::uint32_t tc = 0, tt0 = 0, tt1 = 0;
    std::uint64_t crp = 0, srp = 0;
    // 68040 MMU
    std::uint32_t tc040 = 0, urp040 = 0, srp040 = 0, dtt0 = 0, dtt1 = 0;
    std::uint32_t itt0 = 0, itt1 = 0;
};

struct DisasmLine {
    std::uint32_t addr = 0;
    std::uint8_t length = 2;         // bytes; 2 when unreadable
    bool readable = false;
    bool breakpoint = false;
    std::uint16_t opcode = 0;        // first word, when readable
    std::string text;
    // Annotations (DebugSession): the ROM symbol at this address
    // (DebugSymbols.h), and what the instruction names — a trap, or a
    // low-memory global it addresses absolutely (MacSymbols.h).
    std::string label;
    std::string comment;
};

struct MemoryView {
    Space space = Space::Logical;
    std::uint32_t addr = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<ByteState> state;    // same length as bytes
};

struct Command {
    enum class Kind : std::uint8_t {
        Pause, Continue, Step,
        StepOver,                    // a call (BSR/JSR/TRAP/A-line/F-line) as one step
        StepOut,                     // run until the current routine returns
        AddBreakpoint, RemoveBreakpoint, ClearBreakpoints,
        ViewMemory,                  // addr, length, space
        ViewDisasm,                  // addr; followPc = true follows the PC
        // Edits: only while stopped (refused otherwise, with a message).
        SetRegister,                 // reg, value
        WriteMemory,                 // addr, space, data — all bytes or none
        AddWatchpoint,               // watch
        RemoveWatchpoint,            // addr (every watchpoint starting there)
        ClearWatchpoints,
        AddCatch,                    // catch
        RemoveCatch,                 // catch
        ClearCatches,
        LoadSymbols,                 // path: a ROM symbol file (DebugSymbols.h)
        ClearSymbols,
        SetDeviceView,               // value: 0 off, 1 publish device snapshots
        SetHistory,                  // value: 0 off, 1 on
        ClearHistory,
        ExportHistory,               // path (written by the machine thread)
    };
    Kind kind = Kind::Pause;
    std::uint64_t id = 0;            // assigned by Session::post
    std::uint32_t addr = 0;
    std::uint32_t length = 0;
    Space space = Space::Logical;
    bool followPc = false;
    Reg reg = Reg::D0;
    std::uint64_t value = 0;         // 64 bits for the 68030's CRP/SRP
    std::vector<std::uint8_t> data;  // WriteMemory, at most kMaxEditBytes
    Watchpoint watch;
    Catch catchpoint;
    std::string path;                // ExportHistory, LoadSymbols
};

struct Snapshot {
    std::uint64_t generation = 0;    // bumps on every publish
    std::uint64_t acked = 0;         // highest command id applied
    bool available = true;           // false: this build cannot stop the CPU
    bool stopped = false;
    // A stop reached inside a quantum (breakpoint, step) holds the machine
    // thread at that instruction; reset, save-state and engine requests
    // wait for the next quantum boundary. A Pause stop IS that boundary.
    bool inQuantum = false;
    StopReason reason = StopReason::None;
    StopDetail detail;               // meaningful for Watchpoint/Exception
    std::string model;               // "68000" … "68040"
    std::int64_t machineClock = 0;
    std::int64_t coreClock = 0;
    Registers regs;
    bool mmuEnabled = false;
    bool supervisor = false;
    std::vector<DisasmLine> disasm;
    MemoryView memory;
    std::vector<std::uint32_t> breakpoints;   // logical PCs
    std::vector<Watchpoint> watchpoints;
    std::vector<Catch> catches;
    // Histories: on/off, totals ever recorded, and the newest entries
    // (oldest first) with the disassembly of each instruction entry.
    bool historyOn = false;
    std::uint64_t historyRecorded = 0, trapsRecorded = 0;
    std::vector<HistoryEntry> historyTail;
    std::vector<std::string> historyText;    // parallel to historyTail
    std::vector<TrapEntry> trapTail;
    // Typed device snapshots (DeviceSnapshot.h), published only while the
    // window asks for them: VIA, SCC, floppy, SCSI, ADB and video.
    bool devicesOn = false;
    std::vector<pom68k::dev::Snapshot> devices;
    // The running ROM's own checksum and the ROM symbols accepted for it.
    std::uint32_t romChecksum = 0;
    std::size_t symbolCount = 0;
    std::string symbolSource;
    // Engine the user asked for (0 = interpreter, 1 = accelerated) and the
    // one that actually executes: with a stop armed every instruction goes
    // through Moira's interpreter (JitEngine.cpp: !pomJitIdle()).
    int requestedEngine = 0;
    int effectiveEngine = 0;
    std::string message;             // last refusal, in the GUI's language
};

// The machine thread's view of one CPU + memory pair. Every method is
// called on the machine thread at an instruction boundary.
class Target {
public:
    virtual ~Target() = default;
    // Registers, clocks, model, MMU state and engines.
    virtual void capture(Snapshot& s) const = 0;
    // The CPU's position: Moira's core clock and the next instruction.
    virtual std::int64_t clock() const = 0;
    virtual std::uint32_t pc() const = 0;
    // Side-effect-free: device registers are never read (ByteState).
    virtual void readMemory(Space space, std::uint32_t addr, std::uint8_t* out,
                            ByteState* state, std::size_t n) = 0;
    virtual DisasmLine disassemble(std::uint32_t logicalAddr) = 0;
    // The ROM's first longword (its checksum); 0 if the map has no ROM.
    virtual std::uint32_t romChecksum() const = 0;
    // The board's devices, read from their members (DeviceSnapshot.h).
    virtual void devices(std::vector<pom68k::dev::Snapshot>& out) const = 0;
    virtual bool addBreakpoint(std::uint32_t pc) = 0;
    virtual void removeBreakpoint(std::uint32_t pc) = 0;
    virtual void clearBreakpoints() = 0;
    virtual std::vector<std::uint32_t> breakpoints() const = 0;
    // Edits, at an instruction boundary while stopped. False + `why` (in the
    // GUI's language) refuses the whole edit and changes nothing.
    //   setRegister: a PC edit reloads the prefetch queue from the new
    //     address through the same side-effect-free logical read as the
    //     memory view; an address that read refuses is refused. An SR edit
    //     swaps the active stack like the CPU would, but is not an
    //     instruction: it arms no trace and no IRQ-recognition delay.
    //   writeMemory: every byte must be plain RAM or framebuffer (the
    //     map's writable data span); ROM and device registers are refused,
    //     never written through the bus. Translated code is invalidated.
    //   MMU registers: the edit is a PMOVE/MOVEC with flush — both ATCs
    //     are emptied, the JIT's map generation moves, the 68030's carried
    //     prefetch pipe and every translated block are dropped — so the
    //     next access translates through the new map.
    //   CACR: a MOVEC to CACR, write-only strobes (clear/clear-entry)
    //     included; translated code is dropped.
    //   Both are refused while the architectural 68040 data cache is on,
    //     whose dirty lines such an edit would orphan.
    virtual bool setRegister(Reg reg, std::uint64_t value, std::string& why) = 0;
    virtual bool writeMemory(Space space, std::uint32_t addr,
                             const std::uint8_t* data, std::size_t n,
                             std::string& why) = 0;
    // Access and exception stops. add* refuses (false + why) a malformed
    // or excess entry; a duplicate is accepted and changes nothing.
    virtual bool addWatchpoint(const Watchpoint& w, std::string& why) = 0;
    virtual void removeWatchpoint(std::uint32_t addr) = 0;
    virtual void clearWatchpoints() = 0;
    virtual std::vector<Watchpoint> watchpoints() const = 0;
    virtual bool addCatch(const Catch& c, std::string& why) = 0;
    virtual void removeCatch(const Catch& c) = 0;
    virtual void clearCatches() = 0;
    virtual std::vector<Catch> catches() const = 0;
    // Stop after the next instruction retires (Moira's soft stop).
    virtual void armStep() = 0;
    // Run-until steps, both judged at every instruction boundary on the
    // stack that was active when they were armed (USP, ISP or MSP), so a
    // deeper invocation of the same code never satisfies them:
    //   step over: if the instruction at PC is a call — BSR, JSR, TRAP #n,
    //     an A-line or an F-line word — stop when that stack is back at
    //     the start depth with the PC on the next instruction, or has
    //     risen above it (an auto-pop trap returns to its caller's
    //     caller); any other instruction is a plain step.
    //   step out: stop after the first RTS/RTD/RTR/RTE that starts with
    //     that stack at or above the start depth.
    // A breakpoint, watchpoint or exception stop met on the way stops
    // there and ends the run; so does cancelRun() (a Pause).
    virtual void armStepOver() = 0;
    virtual void armStepOut() = 0;
    virtual void cancelRun() = 0;
    // Called after every command batch and at every quantum boundary:
    // re-assert whatever per-instruction soft stop a run or a history
    // needs. A breakpoint list edit recomputes CHECK_BP from the list
    // alone, and a reset clears it; either would silently stop them.
    virtual void maintain() = 0;
    // Histories. `tail` = 0 copies whole rings; oldest first.
    virtual void setHistory(bool on) = 0;
    virtual bool historyOn() const = 0;
    virtual void clearHistory() = 0;
    virtual std::uint64_t historyRecorded() const = 0;
    virtual std::uint64_t trapsRecorded() const = 0;
    virtual void history(std::vector<HistoryEntry>& out, std::size_t tail) const = 0;
    virtual void traps(std::vector<TrapEntry>& out, std::size_t tail) const = 0;
    virtual bool stopsArmed() const = 0;
};

} // namespace pom68k::dbg
