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

enum class StopReason : std::uint8_t { None, Pause, Step, Breakpoint };

// The registers an edit can name. D0-D7/A0-A7 in order, so `Reg(D0 + n)`
// works. A7 is the ACTIVE stack pointer, as the CPU sees it; USP/ISP/MSP
// name a bank whichever one is active. ISP is the 68000's SSP. VBR, SFC
// and DFC exist from the 68010, MSP from the 68020. The MMU and cache control
// registers are deliberately absent: an edit there moves the address map
// or the cache, which needs its own definition, not a register poke.
enum class Reg : std::uint8_t {
    D0 = 0, A0 = 8, PC = 16, SR, USP, ISP, MSP, VBR, SFC, DFC, Count
};

// Bounds on what one snapshot can carry. A request beyond them is clamped
// and the snapshot says so.
inline constexpr std::uint32_t kMaxMemoryBytes = 4096;
inline constexpr int kMaxDisasmLines = 64;
inline constexpr std::size_t kMaxBreakpoints = 256;
inline constexpr std::size_t kMaxEditBytes = 256;

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
};

struct DisasmLine {
    std::uint32_t addr = 0;
    std::uint8_t length = 2;         // bytes; 2 when unreadable
    bool readable = false;
    bool breakpoint = false;
    std::string text;
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
        AddBreakpoint, RemoveBreakpoint, ClearBreakpoints,
        ViewMemory,                  // addr, length, space
        ViewDisasm,                  // addr; followPc = true follows the PC
        // Edits: only while stopped (refused otherwise, with a message).
        SetRegister,                 // reg, value
        WriteMemory,                 // addr, space, data — all bytes or none
    };
    Kind kind = Kind::Pause;
    std::uint64_t id = 0;            // assigned by Session::post
    std::uint32_t addr = 0;
    std::uint32_t length = 0;
    Space space = Space::Logical;
    bool followPc = false;
    Reg reg = Reg::D0;
    std::uint32_t value = 0;
    std::vector<std::uint8_t> data;  // WriteMemory, at most kMaxEditBytes
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
    std::string model;               // "68000" … "68040"
    std::int64_t machineClock = 0;
    std::int64_t coreClock = 0;
    Registers regs;
    bool mmuEnabled = false;
    bool supervisor = false;
    std::vector<DisasmLine> disasm;
    MemoryView memory;
    std::vector<std::uint32_t> breakpoints;   // logical PCs
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
    virtual bool setRegister(Reg reg, std::uint32_t value, std::string& why) = 0;
    virtual bool writeMemory(Space space, std::uint32_t addr,
                             const std::uint8_t* data, std::size_t n,
                             std::string& why) = 0;
    // Stop after the next instruction retires (Moira's soft stop).
    virtual void armStep() = 0;
    virtual bool stopsArmed() const = 0;
};

} // namespace pom68k::dbg
