// POM68K — helpers shared by generated-code backends.

#include "JitRuntime.h"

#include <cstddef>

#include "Moira.h"

extern "C" int pom68kJitJsr040(moira::Moira* cpu, uint32_t instructionPc,
                               uint32_t returnAddress, uint32_t target,
                               uint32_t opcode,
                               moira::Moira::PomJitAtcReplay040* replay) noexcept
{
    if (replay) cpu->pomJitAtcReplayBegin040(*replay, cpu->getSP() - 4, true, replay->access);
    const int ok = cpu->pomJitJsr040(instructionPc, returnAddress, target,
                                    uint16_t(opcode)) ? 1 : 0;
    if (replay) {
        replay->active = replay->fetches = replay->routes = 0;
        replay->cacheSaved = 0;     // the JSR retired: keep the committed count
    }
    return ok;
}

using AtcReplay040 = moira::Moira::PomJitAtcReplay040;

// Both emitters address these fields directly in generated code.
static_assert(sizeof(moira::Moira::PomJitDtlbEntry) == 16);
static_assert(offsetof(moira::Moira::PomJitDtlbEntry, codeMask) == 4);
static_assert(sizeof(moira::Moira::PomJitDtlbEntry::codeMask) == 2);
static_assert(offsetof(moira::Moira::PomJitDtlbEntry, atc040) == 6);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, atc040) == 13);
static_assert(offsetof(AtcReplay040, opaque) == 16);


// Runs one instruction through Moira, from a clean boundary.
//   1 = retired normally, 0 = did not retire (fault/exception processed),
//  -1 = the code window no longer covers this pc — leave, do not count.
extern "C" int pom68kJitStep(moira::Moira* cpu, AtcReplay040* replay) noexcept {
    const bool restored = replay && replay->active;
    if (replay) cpu->pomJitAtcReplayAbort040(*replay);
    if (!restored && !cpu->pomJitCovers(cpu->getPC())) return -1;
    if (!cpu->pomJitIdle()) return -1;
    return cpu->pomJitExecOne() ? 1 : 0;
}

// The slow half of the data path: a real mmu040 access, fault turned into
// a status. `bytes` is 1, 2 or 4.
//
// `pcWords` (fetchWords << 32 | pc, 0 when the block emits no i-cache
// charge) is the peripheral-phase alignment of 2026-08-21: this path is
// where I/O lands, an I/O access forces a peripheral flush at the access
// clock, and the interpreter reaches the same access with the fetch
// penalty already on the clock while the emitted charge sits after the
// body. Bias the clock by the read-only peek for the access alone —
// success or fault, the bias is gone before anything else runs, so the
// success-path charge and the fault re-run both stay exactly as they were.
extern "C" int pom68kJitRead(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                             uint32_t* out, uint64_t pcWords, AtcReplay040* replay) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    if (replay) cpu->pomJitAtcReplayBegin040(*replay, addr, true, replay->access);
    const int ok = cpu->pomJitReadData(addr, int(bytes), *out) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}

extern "C" int pom68kJitWrite(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                              uint32_t val, uint64_t pcWords, AtcReplay040* replay) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    if (replay) cpu->pomJitAtcReplayBegin040(*replay, addr, true, replay->access);
    const int ok = cpu->pomJitWriteData(addr, int(bytes), val) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}

// The 030 JSR reads the first word at its target and leaves that run-time
// value in queue.irc.  No compile-time word is exact: a trap patch may have
// changed it since the block was recorded.  A false result means the read
// would fault, so generated code replays the still-untouched instruction.
extern "C" int pom68kJitReadProg(moira::Moira* cpu, uint32_t addr,
                                 uint32_t* out, AtcReplay040* replay) noexcept {
    if (replay) cpu->pomJitAtcReplayBegin040(*replay, addr, false, replay->access);
    uint16_t word = 0;
    const int ok = cpu->pomJitReadProg(addr, word) ? 1 : 0;
    *out = word;
    return ok;
}
