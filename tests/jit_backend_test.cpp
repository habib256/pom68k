// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: the portability seam. No external assets; native hosts also run
// one hand-built block against the Q605 CPU wrapper. This checks the things
// that decide whether the JIT exists at all on a given host
// (src/jit/POM68K_JIT.md invariants 6 and 7):
//
//   * backend selection, including that `auto` chooses a usable native
//     generator when eligible and an unknown name lands on `threaded`;
//   * the W^X executable-memory allocator, or a clean refusal on a platform
//     that has none (Emscripten, iOS, a hardened kernel);
//   * the block classifier's safety rules — the ones that keep MMU, cache
//     and supervisor-state instructions out of a block.

#include "jit/JitBackend.h"
#include "jit/JitCodeBuffer.h"
#include "jit/JitConfig.h"
#include "jit/JitCost.h"
#include "jit/JitIr.h"
#include "jit/backends/X64Asm.h"
#include "JitTestConfig.h"

#if defined(POM68K_JIT_BACKEND_A64) || defined(POM68K_JIT_BACKEND_X64)
#include "Cpu040.h"
#include "Q605Memory.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL: %s\n", what); failures++; }
}

void checkUnsafe(uint16_t op, const char* what) {
    check(jit::classify(op) == jit::Kind::Unsafe, what);
}

void checkSafe(uint16_t op, const char* what) {
    check(jit::classify(op) != jit::Kind::Unsafe, what);
}

void setCpuEngineEnv(const char* value) {
#ifdef _WIN32
    _putenv_s("POM68K_CPU_ENGINE", value ? value : "");
#else
    if (value) setenv("POM68K_CPU_ENGINE", value, 1);
    else unsetenv("POM68K_CPU_ENGINE");
#endif
}

void setEnv(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1);
    else unsetenv(key);
#endif
}

struct SavedEnv {
    const char* key;
    bool had;
    std::string value;
    explicit SavedEnv(const char* k)
        : key(k), had(std::getenv(k) != nullptr),
          value(std::getenv(k) ? std::getenv(k) : "") {}
    void clear() const { setEnv(key, nullptr); }
    void restore() const { setEnv(key, had ? value.c_str() : nullptr); }
};

}  // namespace

int main() {
    // The x64 translation unit compiles on every host, but an A64 machine
    // cannot execute its generated code. Keep the host opcodes introduced by
    // recent Speedometer lowerings pinned at the byte level here; x64 CI
    // exercises the complete generated paths.
    {
        uint8_t code[16] = {};
        jit::x64::Asm a(code, sizeof(code));
        a.imulRR(jit::x64::RDI, jit::x64::RCX);
        a.imulRR(jit::x64::R9, jit::x64::R10);
        a.aluRR(jit::x64::Asm::Op::ADC, jit::x64::Sz::B,
                jit::x64::RDI, jit::x64::RDX);
        a.aluRR(jit::x64::Asm::Op::SBB, jit::x64::Sz::W,
                jit::x64::R9, jit::x64::R10);
        check(a.size() == 14 &&
              !std::memcmp(code,
                  "\x0F\xAF\xF9\x45\x0F\xAF\xCA"
                  "\x40\x10\xD7\x66\x45\x19\xD1", 14),
              "x64 IMUL plus byte ADC/extended-register word SBB encodings");
    }

    // ── One IR memory contract, one proof planner, every backend ─────────
    // These are encoding-level and asset-free. The copyback gates exercise
    // the generated A64/x64 implementations of the same plans below.
    {
        std::printf("[jit_backend] IR semantic and memory protocols\n");
        check(sizeof(jit::MemoryContract) <= 56,
              "memory contract stays compact enough for every traced instruction");
        check(sizeof(jit::InstructionSemantics) <= 16,
              "instruction-semantics contract stays compact in every trace");

        const auto pair = jit::describeMemory(0x2F38, false); // MOVE.L abs.W,-(A7)
        check(pair.described && pair.count == 2,
              "memory-to-memory MOVE records two accesses");
        check(pair.order == jit::MemoryOrder::SourceThenDestination,
              "MOVE records source-before-destination order");
        check(pair.access[0].direction == jit::MemoryDirection::Read &&
              pair.access[1].direction == jit::MemoryDirection::Write,
              "MOVE records R then W directions");
        check(pair.access[1].eaCommit == jit::EaCommit::BeforeAccess,
              "MOVE -(A7) records its pre-access EA commit");
        check(pair.lastWrite == 1 && pair.cachePairProved,
              "MOVE identifies its last write and proved cache pair");

        jit::MemoryProofOptions cache;
        cache.cacheReads = cache.cacheWrites = cache.cachePairs = true;
        const auto pairProof = jit::memoryProofPlan(pair, cache);
        check(pairProof.protocol == jit::MemoryProofProtocol::AtomicCachePair &&
              pairProof.preflightMask == 3 && pairProof.cacheMask == 3,
              "pair plan proves R and W before either access is visible");
        auto pairUse = jit::instructionMemoryPlan(pair, cache);
        const auto pairRead = pairUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Source,
            4, 7, 0);
        const auto pairWrite = pairUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Destination,
            4, 4, 7);
        check(pairRead.valid() && pairWrite.valid() && pairUse.complete() &&
              pairRead.cache && pairWrite.cache &&
              pairWrite.eaCommit == jit::EaCommit::BeforeAccess,
              "backend access tokens carry order, cache and EA commit from IR");
        check(!pairUse.access(jit::MemoryDirection::Read,
                              jit::MemoryOperand::Source, 4, 7, 0).valid(),
              "an IR access slot cannot be consumed twice");

        jit::MemoryProofOptions exact040;
        exact040.exactReads = true;
        exact040.preflightedExactSource = true;
        const auto exactMove040 = jit::describeMemory(0x12D0, false);
        const auto exactMove040Proof =
            jit::memoryProofPlan(exactMove040, exact040);
        check(exactMove040Proof.protocol ==
                  jit::MemoryProofProtocol::PreflightAll &&
              exactMove040Proof.preflightMask == 3 &&
              exactMove040Proof.exactThunkMask == 1,
              "040 MOVE may read an exact source after proving its destination");
        auto exact030 = exact040;
        exact030.preflightedExactSource = false;
        const auto exactMove030 = jit::describeMemory(0x12D0, true);
        check(jit::memoryProofPlan(exactMove030, exact030).exactThunkMask == 0,
              "030 two-EA MOVE keeps the retained-cache direct-preflight proof");

        const auto rmw = jit::describeMemory(0xB592, false); // EOR.L D2,(A2)
        const auto rmwProof = jit::memoryProofPlan(rmw, cache);
        check(rmw.count == 2 && rmw.order == jit::MemoryOrder::ReadModifyWrite,
              "RMW records read then last-write on one operand");
        check(rmwProof.protocol == jit::MemoryProofProtocol::ReadModifyWrite &&
              rmwProof.preflightMask == 3 && rmwProof.cacheMask == 0,
              "unproved RMW stays on one whole-instruction proof");
        auto rmwUse = jit::instructionMemoryPlan(rmw, cache);
        const auto rmwRead = rmwUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Destination,
            4, 2, 2);
        const auto rmwWrite = rmwUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Destination,
            4, 2, 2);
        check(jit::memoryRmwAccessPair(rmwRead, rmwWrite) &&
              rmwUse.complete(),
              "RMW tokens require both ordered accesses and one preflight");

        const auto clr = jit::describeMemory(0x4292, false); // CLR.L (A2)
        auto clrUse = jit::instructionMemoryPlan(clr, cache);
        const auto clrWrite = clrUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Destination,
            4, 2, 2);
        check(clr.count == 1 && clrWrite.valid() && clrUse.complete() &&
              clrWrite.protocol == jit::MemoryProofProtocol::SingleWrite,
              "CLR memory is one write, never a backend-invented read");

        const auto timedRead = jit::describeMemory(0x4A11, true);
        jit::MemoryProofOptions noOptionalThunks;
        noOptionalThunks.exactReads = false;
        noOptionalThunks.exactWrites = false;
        noOptionalThunks.cacheReads = true;
        const auto timedProof = jit::memoryProofPlan(timedRead,
                                                     noOptionalThunks);
        check(jit::memoryRequiresExactAccess(timedRead) &&
              timedProof.exactThunkMask == 1 && timedProof.cacheMask == 0,
              "model-required exact access is IR policy, not an A64 opcode exception");

        const auto speedometerBtst = jit::describeMemory(0x0829, true);
        const auto speedometerMove0 = jit::describeMemory(0x1029, true);
        const auto speedometerMove2 = jit::describeMemory(0x1429, true);
        const auto speedometerBtstProof = jit::memoryProofPlan(
            speedometerBtst, noOptionalThunks);
        const auto speedometerMove0Proof = jit::memoryProofPlan(
            speedometerMove0, noOptionalThunks);
        const auto speedometerMove2Proof = jit::memoryProofPlan(
            speedometerMove2, noOptionalThunks);
        check(jit::memoryRequiresExactAccess(speedometerBtst) &&
              jit::memoryRequiresExactAccess(speedometerMove0) &&
              jit::memoryRequiresExactAccess(speedometerMove2) &&
              speedometerBtstProof.exactThunkMask == 1 &&
              speedometerMove0Proof.exactThunkMask == 1 &&
              speedometerMove2Proof.exactThunkMask == 1 &&
              !jit::memoryRequiresExactAccess(
                  jit::describeMemory(0x0829, false)) &&
              !jit::memoryRequiresExactAccess(
                  jit::describeMemory(0x1029, false)) &&
              !jit::memoryRequiresExactAccess(
                  jit::describeMemory(0x1429, false)),
              "Speedometer device polls require exact reads only on the 68030");

        jit::Instr bfextuMemory;
        bfextuMemory.opcode = 0xE9D0;       // BFEXTU (A0){0:D0},D2
        bfextuMemory.words = 2;
        bfextuMemory.semantics = jit::describeInstruction(bfextuMemory.opcode);
        bfextuMemory.memory = jit::describeMemory(bfextuMemory.opcode, false);
        bfextuMemory.extensionCount = 1;
        bfextuMemory.extensions[0] = 0x2020;
        jit::describeEffectiveAddresses(bfextuMemory);
        jit::refineMemoryFromExtensions(bfextuMemory, false);
        auto bfextuUse = jit::instructionMemoryPlan(bfextuMemory.memory,
                                                    exact040);
        const auto bfextuRead = bfextuUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            4, 2, 0);
        check(bfextuRead.valid() && bfextuUse.complete() &&
              bfextuMemory.memory.count == 1,
              "zero-offset memory BFEXTU refines to one exact longword read");
        bfextuMemory.extensions[0] = 0x5848; // D1 offset, immediate width 8
        bfextuMemory.memory = jit::describeMemory(bfextuMemory.opcode, false);
        jit::refineMemoryFromExtensions(bfextuMemory, false);
        check(bfextuMemory.memory.count == 1,
              "dynamic-offset width-8 BFEXTU stays within one longword");
        bfextuMemory.extensions[0] = 0x2060; // immediate bit offset 1
        bfextuMemory.memory = jit::describeMemory(bfextuMemory.opcode, false);
        jit::refineMemoryFromExtensions(bfextuMemory, false);
        auto crossingUse = jit::instructionMemoryPlan(bfextuMemory.memory,
                                                      exact040);
        const auto crossingLong = crossingUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            4, 2, 0);
        const auto crossingTail = crossingUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            1, 2, 0);
        check(bfextuMemory.memory.count == 2 && crossingLong.valid() &&
              crossingTail.valid() && crossingUse.complete() &&
              crossingLong.protocol == jit::MemoryProofProtocol::PreflightAll,
              "possible five-byte BFEXTU publishes ordered long+tail reads");

        jit::Instr bfinsMemory;
        bfinsMemory.opcode = 0xEFD1;       // BFINS D0,(A1){7:9}
        bfinsMemory.words = 2;
        bfinsMemory.semantics = jit::describeInstruction(bfinsMemory.opcode);
        bfinsMemory.memory = jit::describeMemory(bfinsMemory.opcode, true);
        bfinsMemory.extensionCount = 1;
        bfinsMemory.extensions[0] = 0x01C9;
        jit::describeEffectiveAddresses(bfinsMemory);
        jit::refineMemoryFromExtensions(bfinsMemory, true);
        auto bfinsUse = jit::instructionMemoryPlan(bfinsMemory.memory, cache);
        const auto bfinsRead = bfinsUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            4, 2, 1);
        const auto bfinsWrite = bfinsUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Operand,
            4, 2, 1);
        check(bfinsMemory.memory.count == 2 &&
              bfinsMemory.memory.lastWrite == 1 &&
              jit::memoryRmwAccessPair(bfinsRead, bfinsWrite) &&
              bfinsUse.complete(),
              "tailless BFINS publishes one proved read4/write4 RMW pair");
        bfinsMemory.extensions[0] = 0x01C0; // offset 7, width 32: fifth byte
        bfinsMemory.memory = jit::describeMemory(bfinsMemory.opcode, true);
        jit::refineMemoryFromExtensions(bfinsMemory, true);
        auto bfinsTailUse = jit::instructionMemoryPlan(bfinsMemory.memory,
                                                       cache);
        const auto bfinsLongRead = bfinsTailUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            4, 2, 1);
        const auto bfinsLongWrite = bfinsTailUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Operand,
            4, 2, 1);
        const auto bfinsTailRead = bfinsTailUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            1, 2, 1);
        const auto bfinsTailWrite = bfinsTailUse.access(
            jit::MemoryDirection::Write, jit::MemoryOperand::Operand,
            1, 2, 1);
        check(bfinsMemory.memory.described &&
              bfinsMemory.memory.count == 4 &&
              bfinsMemory.memory.lastWrite == 3 &&
              jit::memoryRmwTailAccessSequence(
                  bfinsLongRead, bfinsLongWrite,
                  bfinsTailRead, bfinsTailWrite) &&
              bfinsTailUse.complete(),
              "five-byte BFINS publishes proved read4/write4/read1/write1");

        const auto write030 = jit::describeMemory(0x2140, true); // MOVE.L D0,d16(A0)
        jit::MemoryProofOptions proof030;
        proof030.exactWrites = true;
        proof030.restartableWriteRequired = true;
        const auto lastWrite = jit::memoryProofPlan(write030, proof030);
        check(lastWrite.protocol == jit::MemoryProofProtocol::SingleWrite &&
              lastWrite.restartableLastWrite() && lastWrite.exactThunkMask == 1,
              "68030 proved LASTWRITE produces a restartable exact-write plan");

        jit::Instr speedometerWrite;
        speedometerWrite.opcode = 0x137C;
        speedometerWrite.words = 3;
        speedometerWrite.semantics =
            jit::describeInstruction(speedometerWrite.opcode);
        speedometerWrite.memory =
            jit::describeMemory(speedometerWrite.opcode, true);
        speedometerWrite.extensionCount = 2;
        speedometerWrite.extensions[0] = 0x0002;
        speedometerWrite.extensions[1] = 0x1A00;
        jit::describeEffectiveAddresses(speedometerWrite);
        jit::refineMemoryFromExtensions(speedometerWrite, true);
        const auto speedometerWriteProof =
            jit::memoryProofPlan(speedometerWrite.memory, proof030);
        check(speedometerWrite.memory.count == 1 &&
              speedometerWrite.memory.access[0].exactRequired &&
              speedometerWriteProof.protocol ==
                  jit::MemoryProofProtocol::SingleWrite &&
              speedometerWriteProof.exactThunkMask == 1,
              "Speedometer 137C/$1A00 publishes a model-required exact write");
        speedometerWrite.extensions[1] = 0x1A02;
        speedometerWrite.memory =
            jit::describeMemory(speedometerWrite.opcode, true);
        jit::refineMemoryFromExtensions(speedometerWrite, true);
        check(!speedometerWrite.memory.access[0].exactRequired,
              "an unrelated 137C displacement keeps the ordinary RAM path");

        const auto bsr = jit::describeMemory(0x6106, false);
        const auto rts = jit::describeMemory(0x4E75, false);
        check(jit::memoryProofPlan(bsr, cache).protocol ==
                  jit::MemoryProofProtocol::SingleWrite &&
              bsr.access[0].operand == jit::MemoryOperand::Stack,
              "BSR describes its sole stack write");
        check(jit::memoryProofPlan(rts, cache).protocol ==
                  jit::MemoryProofProtocol::SingleRead &&
              rts.access[0].eaCommit == jit::EaCommit::AfterAccess,
              "RTS describes its stack read and postincrement commit");

        const auto movem = jit::describeMemory(0x48E7, false);
        check(movem.count == jit::MemoryContract::VariableCount &&
              movem.order == jit::MemoryOrder::RegisterDescending &&
              jit::memoryProofPlan(movem, cache).protocol ==
                  jit::MemoryProofProtocol::OrderedSpan,
              "MOVEM predecrement records a variable descending access span");

        const auto adda = jit::describeInstruction(0xD3C1); // ADDA.L D1,A1
        check(adda.operation == jit::SemanticOp::AddressAlu &&
              adda.alu == jit::AluOperation::Add && adda.sizeIndex == 2 &&
              adda.eaMode == 0 && adda.eaReg == 1 &&
              adda.registerIndex == 1,
              "IR describes ADDA operation, width and operands once");
        const auto addx = jit::describeInstruction(0xD981); // ADDX.L D1,D4
        check(addx.operation == jit::SemanticOp::AddSubExtend &&
              addx.alu == jit::AluOperation::Add && addx.sizeIndex == 2 &&
              addx.eaReg == 1 && addx.registerIndex == 4,
              "IR distinguishes register ADDX and its X/sticky-Z contract");
        const auto subx = jit::describeInstruction(0x9381); // SUBX.L D1,D1
        check(subx.operation == jit::SemanticOp::AddSubExtend &&
              subx.alu == jit::AluOperation::Sub && subx.sizeIndex == 2 &&
              subx.eaReg == 1 && subx.registerIndex == 1,
              "IR distinguishes Speedometer register SUBX from SUB overlap");
        check(jit::describeInstruction(0xD109).operation ==
                  jit::SemanticOp::Unknown,
              "predecrement-memory ADDX remains outside register semantics");
        const auto extb = jit::describeInstruction(0x49C7); // EXTB.L D7
        check(extb.operation == jit::SemanticOp::Extend &&
              extb.sizeIndex == 2 && extb.eaReg == 7 && extb.action == 1,
              "IR distinguishes EXTB.L's byte source from ordinary EXT.L");
        const auto extl = jit::describeInstruction(0x48C7); // EXT.L D7
        check(extl.operation == jit::SemanticOp::Extend &&
              extl.sizeIndex == 2 && extl.eaReg == 7 && extl.action == 0,
              "ordinary EXT.L keeps its word-source Extend action");
        const auto eor = jit::describeInstruction(0xB592); // EOR.L D2,(A2)
        check(eor.operation == jit::SemanticOp::AluRegToEa &&
              eor.alu == jit::AluOperation::Eor && eor.bytes() == 4,
              "IR distinguishes register-to-EA EOR from CMP encoding overlap");
        const auto divs = jit::describeInstruction(0x8DFC); // DIVS.W #imm,D6
        check(divs.operation == jit::SemanticOp::DivideWord &&
              divs.action == 1 && divs.bytes() == 2 &&
              divs.eaMode == 7 && divs.eaReg == 4 &&
              divs.registerIndex == 6,
              "IR describes signed word division without treating it as OR");
        const auto muls = jit::describeInstruction(0xC7FC); // MULS.W #imm,D3
        check(muls.operation == jit::SemanticOp::MultiplyWord &&
              muls.action == 1 && muls.bytes() == 2 &&
              muls.eaMode == 7 && muls.eaReg == 4 &&
              muls.registerIndex == 3,
              "IR describes Speedometer signed word multiplication once");
        const auto mulu = jit::describeInstruction(0xC2FC); // MULU.W #imm,D1
        check(mulu.operation == jit::SemanticOp::MultiplyWord &&
              mulu.action == 0 && mulu.registerIndex == 1,
              "IR distinguishes unsigned word multiplication");
        const auto mulsMemory = jit::describeMemory(0xC1D0, true);
        check(mulsMemory.count == 1 &&
              mulsMemory.access[0].direction == jit::MemoryDirection::Read &&
              mulsMemory.access[0].operand == jit::MemoryOperand::Source &&
              mulsMemory.access[0].bytes == 2,
              "word multiplication publishes its sole source-memory read");
        const auto divuMemory = jit::describeMemory(0x80D0, false);
        check(divuMemory.count == 1 &&
              divuMemory.access[0].direction == jit::MemoryDirection::Read &&
              divuMemory.access[0].operand == jit::MemoryOperand::Source &&
              divuMemory.access[0].bytes == 2,
              "word division publishes its sole source-memory read");
        const auto divuProof = jit::memoryProofPlan(divuMemory, cache);
        auto divuRead = jit::memoryAccessPlan(
            divuMemory, divuProof, jit::MemoryDirection::Read,
            jit::MemoryOperand::Source, 2, 2, 0);
        check(jit::replayableSpeculativeRead(divuRead),
              "sole division source mints a replayable speculative read");
        divuRead.exactRequired = true;
        check(!jit::replayableSpeculativeRead(divuRead),
              "an exact-required source cannot be speculatively replayed");
        const auto mull = jit::describeInstruction(0x4C00); // MULL D0,Dh:Dl
        check(mull.operation == jit::SemanticOp::MultiplyLong &&
              mull.bytes() == 4 && mull.eaMode == 0 && mull.eaReg == 0,
              "IR describes 68020 long multiplication as its own operation");
        const auto mullMemory = jit::describeMemory(0x4C10, true);
        check(mullMemory.count == 1 &&
              mullMemory.access[0].direction == jit::MemoryDirection::Read &&
              mullMemory.access[0].operand == jit::MemoryOperand::Source &&
              mullMemory.access[0].bytes == 4,
              "long multiplication publishes its sole long source-memory read");
        const auto divl = jit::describeInstruction(0x4C40); // DIVL D0,Dr:Dq
        check(divl.operation == jit::SemanticOp::DivideLong &&
              divl.bytes() == 4 && divl.eaMode == 0 && divl.eaReg == 0,
              "IR describes 68020 long division as its own operation");
        const auto divlMemory = jit::describeMemory(0x4C50, true);
        check(divlMemory.count == 1 &&
              divlMemory.access[0].direction == jit::MemoryDirection::Read &&
              divlMemory.access[0].operand == jit::MemoryOperand::Source &&
              divlMemory.access[0].bytes == 4,
              "long division publishes its sole long source-memory read");
        jit::Instr divlExt;
        divlExt.pc = 0x2000;
        divlExt.opcode = 0x4C68; // DIVL d16(A0),Dr:Dq
        divlExt.words = 3;
        divlExt.extensions[0] = 0x1C02;
        divlExt.extensions[1] = 0x0010;
        divlExt.extensionCount = 2;
        divlExt.semantics = jit::describeInstruction(divlExt.opcode);
        jit::describeEffectiveAddresses(divlExt);
        check(divlExt.effectiveAddressCount == 1 &&
              divlExt.effectiveAddresses[0].extensionOffset == 1 &&
              divlExt.effectiveAddresses[0].value == 0x10,
              "DIVL source EA begins after its mandatory selector word");
        const auto exg = jit::describeInstruction(0xCD4F); // EXG A6,A7
        check(exg.operation == jit::SemanticOp::Exchange && exg.action == 1 &&
              exg.registerIndex == 6 && exg.eaReg == 7,
              "IR distinguishes EXG address registers from AND/MUL encodings");
        const auto cmpm = jit::describeInstruction(0xB308); // CMPM.B (A0)+,(A1)+
        check(cmpm.operation == jit::SemanticOp::CompareMemory &&
              cmpm.sizeIndex == 0 && cmpm.eaReg == 0 &&
              cmpm.destinationReg == 1,
              "IR distinguishes CMPM's two postincrement memory operands");
        const auto cmpmMemory = jit::describeMemory(0xB308, false);
        check(cmpmMemory.count == 2 &&
              cmpmMemory.order == jit::MemoryOrder::SourceThenDestination &&
              cmpmMemory.access[0].direction == jit::MemoryDirection::Read &&
              cmpmMemory.access[0].operand == jit::MemoryOperand::Source &&
              cmpmMemory.access[1].direction == jit::MemoryDirection::Read &&
              cmpmMemory.access[1].operand == jit::MemoryOperand::Destination &&
              jit::memoryProofPlan(cmpmMemory, cache).protocol ==
                  jit::MemoryProofProtocol::PreflightAll,
              "CMPM requires both reads to be preflighted before access zero");
        const auto move = jit::describeInstruction(0x2ADC);
        check(move.operation == jit::SemanticOp::Move && move.bytes() == 4 &&
              move.eaMode == 3 && move.eaReg == 4 &&
              move.destinationMode == 3 && move.destinationReg == 5,
              "IR records both MOVE operands in architectural order");
        jit::Instr moveExt;
        moveExt.pc = 0x1000;
        moveExt.opcode = 0x23FC; // MOVE.L #$12345678,$00ABCDEF.L
        moveExt.words = 5;
        moveExt.semantics = jit::describeInstruction(moveExt.opcode);
        moveExt.extensionCount = 4;
        moveExt.extensions[0] = 0x1234;
        moveExt.extensions[1] = 0x5678;
        moveExt.extensions[2] = 0x00AB;
        moveExt.extensions[3] = 0xCDEF;
        jit::describeEffectiveAddresses(moveExt);
        const auto* moveSource = jit::findEffectiveAddress(moveExt, 7, 4, 2, 0);
        const auto* moveDestination = jit::findEffectiveAddress(moveExt, 7, 1, 2, 2);
        check(moveSource && moveSource->valid && !moveSource->memory() &&
              uint32_t(moveSource->value) == 0x12345678u &&
              moveDestination && moveDestination->valid &&
              moveDestination->memory() &&
              uint32_t(moveDestination->value) == 0x00ABCDEFu,
              "IR owns extension decoding and both concrete MOVE effective addresses");

        jit::Instr fullDirect;
        fullDirect.pc = 0x2000;
        fullDirect.opcode = 0x2030; // MOVE.L ([full extension],A0),D0
        fullDirect.words = 4;
        fullDirect.semantics = jit::describeInstruction(fullDirect.opcode);
        fullDirect.extensionCount = 3;
        fullDirect.extensions[0] = 0x1D30; // D1.L*4, full, long BD, direct
        fullDirect.extensions[1] = 0xFFFE;
        fullDirect.extensions[2] = 0xDCBA;
        jit::describeEffectiveAddresses(fullDirect);
        const auto* directEa = jit::findEffectiveAddress(fullDirect, 6, 0, 2, 0);
        check(directEa && directEa->valid && directEa->fullFormat &&
              directEa->kind == jit::EffectiveAddressKind::FullIndex &&
              directEa->extensionWords == 3 &&
              directEa->baseDisplacementWords == 2 &&
              uint32_t(directEa->baseDisplacement) == 0xFFFEDCBAu &&
              directEa->indirect == jit::IndexIndirect::None &&
              directEa->indexRegister == 1 && directEa->indexLong &&
              directEa->indexShift == 2,
              "IR decodes the complete direct 68020 full-index format");

        jit::Instr fullIndirect;
        fullIndirect.pc = 0x3000;
        fullIndirect.opcode = 0x203B; // MOVE.L full-index(PC),D0
        fullIndirect.words = 5;
        fullIndirect.semantics = jit::describeInstruction(fullIndirect.opcode);
        fullIndirect.extensionCount = 4;
        fullIndirect.extensions[0] = 0xB1A3; // A3.W, BS, word BD, pre, long OD
        fullIndirect.extensions[1] = 0xFF80;
        fullIndirect.extensions[2] = 0x0001;
        fullIndirect.extensions[3] = 0x2345;
        jit::describeEffectiveAddresses(fullIndirect);
        const auto* indirectEa =
            jit::findEffectiveAddress(fullIndirect, 7, 3, 2, 0);
        check(indirectEa && indirectEa->valid && indirectEa->fullFormat &&
              indirectEa->kind == jit::EffectiveAddressKind::PcFullIndex &&
              indirectEa->baseSuppressed && !indirectEa->indexSuppressed &&
              indirectEa->baseDisplacement == -128 &&
              indirectEa->outerDisplacement == 0x00012345 &&
              indirectEa->indirect == jit::IndexIndirect::Preindexed &&
              indirectEa->extensionWords == 4,
              "IR separates base/outer displacement and preindexed indirection");

        jit::Instr speedometerTst;
        speedometerTst.pc = 0x3200;
        speedometerTst.opcode = 0x4A76; // TST.W ([40,A6],4)
        speedometerTst.words = 4;
        speedometerTst.semantics =
            jit::describeInstruction(speedometerTst.opcode);
        speedometerTst.memory =
            jit::describeMemory(speedometerTst.opcode, true);
        speedometerTst.extensionCount = 3;
        speedometerTst.extensions[0] = 0x8162;
        speedometerTst.extensions[1] = 0x0028;
        speedometerTst.extensions[2] = 0x0004;
        jit::describeEffectiveAddresses(speedometerTst);
        jit::refineMemoryFromExtensions(speedometerTst, true);
        const auto* speedometerTstEa = jit::findEffectiveAddress(
            speedometerTst, 6, 6, 1, 0);
        auto speedometerTstUse = jit::instructionMemoryPlan(
            speedometerTst.memory, cache);
        const auto speedometerTstPointer = speedometerTstUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Control,
            4, 6, 6);
        const auto speedometerTstOperand = speedometerTstUse.access(
            jit::MemoryDirection::Read, jit::MemoryOperand::Operand,
            2, 6, 6);
        check(speedometerTstEa && speedometerTstEa->valid &&
              speedometerTstEa->fullFormat &&
              speedometerTstEa->indexSuppressed &&
              speedometerTstEa->baseDisplacement == 0x28 &&
              speedometerTstEa->outerDisplacement == 4 &&
              speedometerTstEa->indirect ==
                  jit::IndexIndirect::Preindexed &&
              jit::fullIndexPenalty(
                  speedometerTstEa->baseDisplacementWords,
                  speedometerTstEa->indirect,
                  speedometerTstEa->outerDisplacementWords) == 9,
              "IR decodes Speedometer's exact 4A76 full-indirect operand");
        check(speedometerTst.memory.count == 2 &&
              speedometerTst.memory.order == jit::MemoryOrder::Sequential &&
              speedometerTstPointer.valid() &&
              speedometerTstOperand.valid() &&
              speedometerTstPointer.preflight &&
              speedometerTstOperand.preflight &&
              speedometerTstUse.complete(),
              "full-indirect TST publishes pointer then operand reads");

        jit::Instr fullPost;
        fullPost.pc = 0x4000;
        fullPost.extensionCount = 2;
        fullPost.extensions[0] = 0x01D6; // BS+IS, null BD, post, word OD
        fullPost.extensions[1] = 0xFFFC;
        const auto postEa = jit::decodeEffectiveAddress(
            fullPost, jit::OperandRole::Operand, 6, 2, 2, 0);
        check(postEa.valid && postEa.baseSuppressed && postEa.indexSuppressed &&
              postEa.baseDisplacementWords == 0 &&
              postEa.outerDisplacementWords == 1 &&
              postEa.outerDisplacement == -4 &&
              postEa.indirect == jit::IndexIndirect::Postindexed,
              "IR decodes suppressed-base/index postindexed full extensions");

        fullPost.extensions[0] = 0x0100; // reserved BD=00
        check(!jit::decodeEffectiveAddress(
                  fullPost, jit::OperandRole::Operand, 6, 2, 2, 0).valid,
              "IR rejects reserved 68020 full-extension encodings");

        jit::Instr bsrInstr;
        bsrInstr.pc = 0x5000; bsrInstr.opcode = 0x6100; bsrInstr.words = 2;
        bsrInstr.semantics = jit::describeInstruction(bsrInstr.opcode);
        bsrInstr.extensionCount = 1; bsrInstr.extensions[0] = 0xFFFE;
        jit::describeEffectiveAddresses(bsrInstr);
        jit::describeControlFlow(bsrInstr);
        check(bsrInstr.control.valid && bsrInstr.control.targetKnown &&
              bsrInstr.control.kind == jit::ControlFlowKind::DirectCall &&
              bsrInstr.control.target == 0x5000 &&
              bsrInstr.control.fallthrough == 0x5004 &&
              bsrInstr.control.returnAddress == 0x5004 &&
              bsrInstr.control.pushesReturnAddress,
              "IR owns BSR target, fallthrough and pushed return address");
        jit::Instr bsrLong;
        bsrLong.pc = 0x5000; bsrLong.opcode = 0x61FF; bsrLong.words = 3;
        bsrLong.fetchWords = 3;
        bsrLong.semantics = jit::describeInstruction(bsrLong.opcode);
        bsrLong.extensionCount = 2;
        bsrLong.extensions[0] = 0xFFFF;
        bsrLong.extensions[1] = 0xFFFE;
        jit::describeControlFlow(bsrLong);
        check(bsrLong.control.valid && bsrLong.control.target == 0x5000 &&
              bsrLong.control.fallthrough == 0x5006 &&
              bsrLong.control.returnAddress == 0x5006 &&
              jit::provedLinearControlFetch030(bsrLong),
              "IR proves BSR.L target, PC+6 return and three linear fetches");
        bsrLong.fetchWords = 2;
        check(!jit::provedLinearControlFetch030(bsrLong),
              "IR rejects BSR.L without all three traced fetches");

        jit::Instr jsr;
        jsr.pc = 0x6000; jsr.opcode = 0x4EB9; jsr.words = 3;
        jsr.semantics = jit::describeInstruction(jsr.opcode);
        jsr.extensionCount = 2;
        jsr.extensions[0] = 0x0012; jsr.extensions[1] = 0x3456;
        jit::describeEffectiveAddresses(jsr);
        jit::describeControlFlow(jsr);
        check(jsr.control.valid && jsr.control.targetKnown &&
              jsr.control.kind == jit::ControlFlowKind::DirectCall &&
              jsr.control.target == 0x00123456 &&
              jsr.control.returnAddress == 0x6006,
              "IR resolves constant JSR control data from its EA plan");
        jsr.fetchWords = 3;
        check(jit::provedLinearControlFetch030(jsr),
              "IR proves the three linear fetches of JSR abs.l");
        jit::Instr indexedJsr;
        indexedJsr.opcode = 0x4EB0; indexedJsr.words = 3;
        indexedJsr.fetchWords = 4;
        indexedJsr.semantics = jit::describeInstruction(indexedJsr.opcode);
        check(jit::provedLinearControlFetch030(indexedJsr),
              "IR proves indexed JSR's one post-extension refill");
        indexedJsr.fetchWords = 3;
        check(!jit::provedLinearControlFetch030(indexedJsr),
              "IR rejects an indexed JSR with an unproved fetch count");
        jit::Instr briefJsr;
        briefJsr.opcode = 0x4EB2; briefJsr.words = 2;
        briefJsr.fetchWords = 2; briefJsr.extensionCount = 1;
        briefJsr.extensions[0] = 0x1000;
        briefJsr.semantics = jit::describeInstruction(briefJsr.opcode);
        check(jit::provedLinearControlFetch030(briefJsr),
              "IR proves Speedometer brief indexed JSR's two fetches");
        briefJsr.fetchWords = 3;
        check(!jit::provedLinearControlFetch030(briefJsr),
              "IR rejects a spurious computeEA refill on brief indexed JSR");

        jit::Instr braWord;
        braWord.opcode = 0x6000; braWord.words = 2; braWord.fetchWords = 2;
        braWord.semantics = jit::describeInstruction(braWord.opcode);
        check(jit::provedLinearControlFetch030(braWord),
              "IR proves BRA.W's two linear 68030 fetches");
        jit::Instr braLong;
        braLong.opcode = 0x60FF; braLong.words = 3; braLong.fetchWords = 3;
        braLong.semantics = jit::describeInstruction(braLong.opcode);
        check(jit::provedLinearControlFetch030(braLong),
              "IR proves BRA.L's three linear 68030 fetches");
        jit::Instr bccWord = braWord;
        bccWord.opcode = 0x6600;
        bccWord.semantics = jit::describeInstruction(bccWord.opcode);
        check(!jit::provedLinearControlFetch030(bccWord),
              "IR keeps conditional Bcc.W out of the single-path proof");
        jit::Instr jmpLong;
        jmpLong.opcode = 0x4EF9; jmpLong.words = 3; jmpLong.fetchWords = 3;
        jmpLong.semantics = jit::describeInstruction(jmpLong.opcode);
        check(jit::provedLinearControlFetch030(jmpLong),
              "IR proves JMP abs.l's three linear 68030 fetches");
        indexedJsr.pc = 0x6100; indexedJsr.words = 2;
        indexedJsr.fetchWords = 3; indexedJsr.extensionCount = 1;
        indexedJsr.extensions[0] = 0x2591;
        indexedJsr.memory = jit::describeMemory(indexedJsr.opcode, true);
        jit::describeEffectiveAddresses(indexedJsr);
        jit::refineMemoryFromExtensions(indexedJsr, true);
        const auto indexedJsrProof = jit::memoryProofPlan(
            indexedJsr.memory, jit::MemoryProofOptions{});
        check(indexedJsr.memory.count == 2 &&
              indexedJsr.memory.order ==
                  jit::MemoryOrder::SourceThenDestination &&
              indexedJsrProof.protocol ==
                  jit::MemoryProofProtocol::PreflightAll &&
              indexedJsrProof.preflightMask == 3,
              "IR requires pointer and stack preflight for indirect JSR");
        const auto movemLoad = jit::describeInstruction(0x4CDF);
        check(movemLoad.operation == jit::SemanticOp::Movem &&
              movemLoad.toRegisters && movemLoad.bytes() == 4,
              "IR records MOVEM span direction and width");
        check(!jit::describeInstruction(0x0108).valid(),
              "shared semantics never misdecode MOVEP as a bit operation");
        check(!jit::describeInstruction(0x0AC0).valid(),
              "shared semantics leave CAS to the interpreter");
    }

    // Profiles are bundles, while a leaf variable remains an explicit A/B
    // override. No static env caching: one process can prove all three.
    {
        std::printf("[jit_backend] operating profiles\n");
        const SavedEnv saved[] = {
            SavedEnv("POM68K_JIT_PROFILE"),
            SavedEnv("POM68K_JIT_ACCESS_THUNK"),
            SavedEnv("POM68K_JIT_LINKS"),
            SavedEnv("POM68K_JIT_PARANOID"),
            SavedEnv("POM68K_JIT_HISTO"),
            SavedEnv("POM68K_JIT_040_LINE_PAIR"),
            SavedEnv("POM68K_JIT_040_LINE_STATS"),
            SavedEnv("POM68K_JIT_PROFIT_SCORE"),
            SavedEnv("POM68K_JIT_PACKED_CCR"),
            SavedEnv("POM68K_JIT_REG_CACHE"),
            SavedEnv("POM68K_JIT_EDGE_CELLS"),
            SavedEnv("POM68K_JIT_DYNAMIC_BITFIELD"),
            SavedEnv("POM68K_JIT_030_MEMBF"),
        };
        for (const auto& e : saved) e.clear();

        check(!jit::packedCcrEnabled() && !jit::registerCacheEnabled() &&
              !jit::edgeLinkCellsEnabled(),
              "measured codegen experiments stay off by default");
        check(jit::dynamicRegisterBitfieldEnabled(),
              "measured dynamic register bitfields stay on by default");
        check(jit::memBitfield030Admission(),
              "proved 68030 memory bitfields stay on by default");
        setEnv("POM68K_JIT_030_MEMBF", "0");
        {
            const auto config = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&config);
            check(!jit::memBitfield030Admission(),
                  "an explicit memory-bitfield veto overrides the proved default");
        }
        setEnv("POM68K_JIT_030_MEMBF", nullptr);

        setEnv("POM68K_JIT_PROFILE", "production");
        {
            const auto config = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&config);
            check(jit::accessThunkMode() == 2 && jit::linksEnabled() &&
                  !jit::paranoidEnabled() && !jit::histogramEnabled(),
                  "production profile is the current fast conformant policy");
        }
        setEnv("POM68K_JIT_PROFILE", "conservative");
        {
            const auto config = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&config);
            check(jit::accessThunkMode() == 0 && !jit::linksEnabled() &&
                  jit::paranoidEnabled() && !jit::cache040LinePairsEnabled(),
                  "conservative profile keeps replay and revalidation boundaries");
        }
        setEnv("POM68K_JIT_PROFILE", "instrumented");
        {
            const auto config = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&config);
            check(jit::accessThunkMode() == 2 && !jit::linksEnabled() &&
                  jit::paranoidEnabled() && jit::histogramEnabled() &&
                  jit::cache040LineReadStatsEnabled(),
                  "instrumented profile enables census and proof counters");
        }
        setEnv("POM68K_JIT_ACCESS_THUNK", "1");
        {
            const auto config = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&config);
            check(jit::accessThunkMode() == 1,
                  "a leaf knob explicitly overrides its selected profile");
        }

        // An Engine publishes this resolved snapshot while compiling. Later
        // process-wide changes are inputs for a future Engine only.
        setEnv("POM68K_JIT_PROFILE", "production");
        setEnv("POM68K_JIT_ACCESS_THUNK", nullptr);
        setEnv("POM68K_JIT_PROFIT_SCORE", "64");
        setEnv("POM68K_JIT_PACKED_CCR", "1");
        setEnv("POM68K_JIT_REG_CACHE", "1");
        setEnv("POM68K_JIT_EDGE_CELLS", "1");
        setEnv("POM68K_JIT_DYNAMIC_BITFIELD", "1");
        jit::ResolvedConfig resolved = testjit::resolveFromEnvironment();
        resolved.applyBackendDefaults(true, /*accessClockBias=*/true);
        setEnv("POM68K_JIT_PROFILE", "conservative");
        setEnv("POM68K_JIT_PACKED_CCR", "0");
        setEnv("POM68K_JIT_REG_CACHE", "0");
        setEnv("POM68K_JIT_EDGE_CELLS", "0");
        setEnv("POM68K_JIT_DYNAMIC_BITFIELD", "0");
        {
            jit::ScopedResolvedConfig active(&resolved);
            check(jit::accessThunkMode() == 2 && jit::linksEnabled() &&
                  jit::blockCacheEnabled(false) && jit::hotThreshold(false) == 1,
                  "resolved configuration is stable and carries backend defaults");
            check(jit::packedCcrEnabled() && jit::registerCacheEnabled() &&
                  jit::edgeLinkCellsEnabled(),
                  "resolved configuration captures all codegen experiments");
            check(jit::dynamicRegisterBitfieldEnabled(),
                  "resolved configuration captures dynamic-bitfield admission");
            check(resolved.profitScore == 64,
                  "resolved configuration captures the profitability score");
            check(resolved.profitScoreExplicit,
                  "an explicit profitability score is distinguished from a backend default");
        }
        {
            const auto future = testjit::resolveFromEnvironment();
            jit::ScopedResolvedConfig active(&future);
            check(jit::accessThunkMode() == 0,
                  "a later injected snapshot observes the profile change");
            check(!jit::packedCcrEnabled() && !jit::registerCacheEnabled() &&
                  !jit::edgeLinkCellsEnabled(),
                  "a later injected snapshot observes experiment overrides");
            check(!jit::dynamicRegisterBitfieldEnabled(),
                  "a later injected snapshot observes the bitfield override");
        }

        // The § C.4nonies admissions follow the backend's accessClockBias
        // declaration — ON where the access thunks carry the peripheral-
        // phase clock bias (x64), OFF where the alignment has not landed
        // (a64, threaded) — and an explicit env wins in either direction.
        // Pinned here so the coupling cannot rot into "the admission
        // default outran the alignment", which is the exact failure the
        // declaration exists to prevent.
        setEnv("POM68K_JIT_RESTART_BASE", nullptr);
        setEnv("POM68K_JIT_BSRW", nullptr);
        jit::ResolvedConfig aligned = testjit::resolveFromEnvironment();
        aligned.applyBackendDefaults(true, /*accessClockBias=*/true);
        check(aligned.restartBaseAdmission && aligned.bsrWideAdmission,
              "a bias-declaring backend turns both admissions on by default");
        jit::ResolvedConfig unaligned = testjit::resolveFromEnvironment();
        unaligned.applyBackendDefaults(true, /*accessClockBias=*/false);
        check(!unaligned.restartBaseAdmission && !unaligned.bsrWideAdmission,
              "a backend without the alignment keeps both admissions off");
        setEnv("POM68K_JIT_RESTART_BASE", "0");
        setEnv("POM68K_JIT_BSRW", "0");
        jit::ResolvedConfig vetoed = testjit::resolveFromEnvironment();
        vetoed.applyBackendDefaults(true, /*accessClockBias=*/true);
        check(!vetoed.restartBaseAdmission && !vetoed.bsrWideAdmission,
              "an explicit 0 beats the bias-declaring backend's admission default");
        setEnv("POM68K_JIT_RESTART_BASE", "1");
        setEnv("POM68K_JIT_BSRW", "1");
        jit::ResolvedConfig forced = testjit::resolveFromEnvironment();
        forced.applyBackendDefaults(true, /*accessClockBias=*/false);
        check(forced.restartBaseAdmission && forced.bsrWideAdmission,
              "an explicit 1 beats the unaligned backend's admission default");
        setEnv("POM68K_JIT_RESTART_BASE", nullptr);
        setEnv("POM68K_JIT_BSRW", nullptr);

        for (const auto& e : saved) e.restore();
    }

    // The product default is per guest family, while the environment remains
    // an unconditional user/test override. Keep this asset-free so a default
    // regression fails before any boot gate is involved.
    const char* oldEngine = std::getenv("POM68K_CPU_ENGINE");
    const bool hadOldEngine = oldEngine != nullptr;
    const std::string savedEngine = oldEngine ? oldEngine : "";
    setCpuEngineEnv(nullptr);
    const auto defaults = testjit::resolveFromEnvironment();
    check(defaults.engineForGuest(false) == jit::EngineKind::Interp,
          "non-68040 guests default to the interpreter");
    check(defaults.engineForGuest(true) == jit::EngineKind::Jit,
          "validated 68040 guests default to the accelerated engine");
    setCpuEngineEnv("interp");
    check(testjit::resolveFromEnvironment().engineForGuest(true) ==
              jit::EngineKind::Interp,
          "POM68K_CPU_ENGINE=interp explicitly restores the reference");
    setCpuEngineEnv("jit");
    check(testjit::resolveFromEnvironment().engineForGuest(false) ==
              jit::EngineKind::Jit,
          "POM68K_CPU_ENGINE=jit explicitly enables the engine on another family");
    setCpuEngineEnv(hadOldEngine ? savedEngine.c_str() : nullptr);

    std::printf("[jit_backend] backend registry\n");

    int count = 0;
    const char* const* names = jit::backendNames(count);
    check(count >= 1, "at least one backend is compiled in");
    bool hasThreaded = false;
    bool hasX64 = false;
    for (int i = 0; i < count; i++) {
        std::printf("  compiled: %s\n", names[i]);
        if (!std::strcmp(names[i], "threaded")) hasThreaded = true;
        if (!std::strcmp(names[i], "x86-64")) hasX64 = true;
    }
    check(hasThreaded, "the portable 'threaded' backend is always compiled in");

    // Selection is per GUEST family as well as per host: a code generator
    // written against one 68k family is wrong on another, not merely slower
    // (JitBackend.h § GuestFamily). These calls therefore name the guest.
    jit::Backend* autoPick = jit::selectBackend("auto", jit::kGuest68040);
    check(autoPick != nullptr, "auto selection never returns null");
    check(autoPick->usable(), "the selected backend reports itself usable");
    if (std::getenv("POM68K_JIT_REQUIRE_NATIVE"))
        check(autoPick->caps().nativeCode,
              "this CI tier requires the host A64/x64 generator to execute");
    std::printf("  auto (68040) -> %s (%s)\n",
                autoPick->name(), autoPick->description());
    if (autoPick->caps().nativeCode) {
        const jit::CompileResult refused =
            autoPick->compile(jit::BlockIr{}, jit::Context{});
        check(!refused.code && refused.reject == jit::CompileReject::Context,
              "native compile refusal carries a structured context reason");
    }

    jit::Backend* threaded = jit::selectBackend("threaded", jit::kGuest68040);
    check(threaded != nullptr && !std::strcmp(threaded->name(), "threaded"),
          "explicit 'threaded' resolves");
    check(threaded->usable(), "'threaded' is usable everywhere — it is the floor");
    check(!threaded->caps().nativeCode, "'threaded' generates no host code");

    jit::Backend* bogus = jit::selectBackend("no-such-backend", jit::kGuest68040);
    check(bogus != nullptr, "an unknown name still yields a backend");
    check(bogus->usable(), "the fallback is usable");

    // ── Guest-family scope (the jit_lcii_boot_etalon timeout, 2026-07-30) ──
    // The x86-64 generator is written against the 68040's instruction-boundary
    // contract; handed the 68030 LC II it wedged the guest in the ROM's Egret
    // handshake loop for a full hour. Selection must refuse that combination
    // instead of shipping it, and `threaded` must remain valid for every
    // family so `auto` always has a correct floor.
    std::printf("[jit_backend] guest-family scope\n");
    check((threaded->caps().guestFamilies & jit::kGuestAny) == jit::kGuestAny,
          "'threaded' declares every guest family — it replays Moira's handlers");
    for (uint32_t fam : { jit::kGuest68000, jit::kGuest68020,
                          jit::kGuest68030, jit::kGuest68040 }) {
        jit::Backend* b = jit::selectBackend("auto", fam);
        check(b != nullptr && (b->caps().guestFamilies & fam) != 0,
              "auto never returns a backend invalid for the guest it was asked about");
        // …and never a native generator outside its own speed declaration:
        // autoFamilies is earned per (family, backend) pair on D.1
        // evidence, so `auto` handing out undeclared native code would be
        // the C.5 discipline silently broken.
        check(!b->caps().nativeCode || (b->caps().autoFamilies & fam) != 0,
              "auto only serves native code where autoFamilies declares it");
    }
    // The history of this check IS the safety story. Before 2026-07-30 an
    // explicit x64 on a 68030 was honoured with no 030 semantics behind it,
    // and jit_lcii_boot_etalon spent an hour wedged proving it; the
    // declaration then refused it for a year of afternoons. Since
    // 2026-08-18 the 030 work is lockstep-proved
    // (jit_lockstep_030_x64_experimental_test, 120k identical), the
    // declaration says so, and the pin flips: an EXPLICIT request is
    // honoured. 2026-08-19 made the flip a one-line declaration
    // (caps().autoFamilies) and then did NOT fire it: the bench win was
    // measured (JIT_BRINGUP § C.4sexies, −12 %) but the first m030 run
    // under the flip found the IIsi in SIGSEGV under the generator
    // (§ C.4septies). 2026-08-21 cleared it: the same host boots the IIsi
    // generator green under the hardened native gate, and the C.5 flip
    // fired — `auto` on an 030 served the generator on x86-64 too.
    //
    // WITHDRAWN 2026-08-29, and the withdrawal is the same story a third
    // time. Every 030 promotion above was decided on a bench, a lockstep
    // and ONE machine's boot; the first `ctest -L m030` ever run on this
    // host — possible only once Moira patch 31 let generated code into the
    // pre-MMU boot — hung EVERY 68030 gate that reaches the generator,
    // while the six `interp_*` references and the two 68020 Mac LC gates
    // passed. So SPEED scope is the 68040 again and `auto` on an 030
    // resolves to `threaded`. CORRECTNESS scope is untouched: the explicit
    // request below is still honoured, which is what keeps the pinned
    // `jit_*_boot_etalon` gates pointed at the defect.
    if (hasX64) {
        jit::Backend* on030 = jit::selectBackend("x64", jit::kGuest68030);
        check(!std::strcmp(on030->name(), "x86-64"),
              "x64 requested for a 68030 guest is honoured since the "
              "declaration (2026-08-18)");
        jit::Backend* on040 = jit::selectBackend("x64", jit::kGuest68040);
        check(!std::strcmp(on040->name(), "x86-64"),
              "…and still served for the 68040 it was written for");
        jit::Backend* auto030 = jit::selectBackend("auto", jit::kGuest68030);
        check(!std::strcmp(auto030->name(), "threaded"),
              "auto on a 68030 resolves to 'threaded' — the C.5 flip was "
              "withdrawn on 2026-08-29 by the m030 tier");
        jit::Backend* auto040 = jit::selectBackend("auto", jit::kGuest68040);
        check(!std::strcmp(auto040->name(), "x86-64"),
              "auto on a 68040 still picks the native generator");
        check(on030->caps().accessClockBias,
              "x64 declares the access-clock bias its thunks carry "
              "(JIT_BRINGUP § C.4nonies) — the admission defaults ride it");
    }

    // Every backend compiled in must DECLARE a scope: the caps field defaults
    // to 0 = undeclared so a new backend cannot inherit a silent "works
    // everywhere", and an undeclared one resolves to `threaded` on every
    // family. NOTE the keys, not the display names — selectBackend() matches
    // on "x64", never on "x86-64", and using the wrong list here made this
    // very check a no-op on its first outing.
    int keyCount = 0;
    const char* const* keys = jit::backendKeys(keyCount);
    check(keyCount == count, "one registry key per compiled backend");
    for (int i = 0; i < keyCount; i++) {
        bool anyFamily = false;
        for (uint32_t fam : { jit::kGuest68000, jit::kGuest68020,
                              jit::kGuest68030, jit::kGuest68040 }) {
            jit::Backend* b = jit::selectBackend(keys[i], fam);
            if (!std::strcmp(b->name(), names[i])) anyFamily = true;
        }
        char msg[128];
        std::snprintf(msg, sizeof msg,
                      "backend '%s' declares at least one guest family", keys[i]);
        check(anyFamily, msg);
    }

    std::printf("[jit_backend] executable memory\n");
    if (!jit::CodeBuffer::supported()) {
        // A legitimate outcome, not an error: this host cannot give us
        // executable pages, so `auto` must have chosen a non-generating
        // backend — which the checks above already proved usable.
        std::printf("  no executable memory on this host — code generators "
                    "are unavailable, portable backend stands in\n");
        check(!autoPick->caps().nativeCode,
              "a code-generating backend was NOT selected without W^X memory");
    } else {
        jit::CodeBuffer buf;
        check(buf.reserve(4096), "reserve");
        check(buf.valid() && buf.capacity() >= 4096, "capacity");
        check(buf.writable(), "starts writable");
        uint8_t* p = buf.alloc(32, 16);
        check(p != nullptr, "alloc");
        check((reinterpret_cast<uintptr_t>(p) & 15) == 0, "alignment honoured");
        if (p) std::memset(p, 0x90, 32);
        check(buf.alloc(1u << 30) == nullptr, "over-allocation is refused");
        check(buf.makeExecutable(), "W -> X");
        if (buf.unified()) {
            // The kernel granted one RWX mapping, so the transitions are
            // no-ops by design: a code generator that flipped protection per
            // block would pay an mprotect PAIR per block, which measured as
            // the largest single cost in the whole backend.
            std::printf("  (unified RWX mapping — W/X transitions are no-ops)\n");
            check(buf.writable(), "stays writable");
            check(buf.alloc(16) != nullptr, "alloc still served");
        } else {
            check(!buf.writable(), "no longer writable while executable");
            check(buf.alloc(16) == nullptr, "alloc refused while executable");
        }
        check(buf.makeWritable(), "X -> W");
        check(buf.writable(), "writable again");
        check(buf.alloc(16) != nullptr, "append after first W/X cycle");
        check(buf.makeExecutable(), "second W -> X on the same mapping");
        check(buf.makeWritable(), "second X -> W on the same mapping");
        buf.release();
        check(!buf.valid(), "release");
    }

#if defined(POM68K_JIT_BACKEND_A64) || defined(POM68K_JIT_BACKEND_X64)
    // Execute a hand-built all-native block. This catches emitter encodings,
    // the native ABI frame, W^X transitions, I-cache invalidation,
    // MOVEQ flags and cycle charging without needing a user ROM.
    {
        std::printf("[jit_backend] native codegen smoke\n");
#if defined(POM68K_JIT_BACKEND_A64)
        constexpr const char* backendKey = "a64";
        constexpr const char* backendName = "aarch64";
#else
        constexpr const char* backendKey = "x64";
        constexpr const char* backendName = "x86-64";
#endif
        check(!std::strcmp(autoPick->name(), backendName),
              "auto selects the validated native backend on this host");
#if defined(POM68K_JIT_BACKEND_A64)
        // AArch64 earns the 030 automatic path independently of x64: exact
        // i-cache/state lockstep plus a fixed-budget win over threaded.
        {
            jit::Backend* a64auto030 =
                jit::selectBackend("auto", jit::kGuest68030);
            check(!std::strcmp(a64auto030->name(), "aarch64"),
                  "auto on a 68030 selects the measured AArch64 generator");
            check(a64auto030->caps().profitScore68030 == 64,
                  "AArch64 68030 publishes its measured cold-code score");
            check(a64auto030->caps().accessClockBias,
                  "AArch64 declares the access-clock bias its thunks carry "
                  "since 2026-08-22 (pom68kA64Read/Write, JIT_BRINGUP "
                  "§ C.4nonies) — the admission defaults ride it");
        }
#endif
        static Q605Memory mem(pom68k::defaultCoreConfig());
        static Cpu040 cpu(mem, jit::defaultResolvedConfig(),
                          pom68k::defaultCoreConfig().cpu,
                          pom68k::defaultCoreConfig().diagnostics);
        mem.setCpu(&cpu);

        jit::BlockIr ir;
        ir.entryPc = ir.codeBase = 0x1000;
        ir.super = true;
        ir.code = {0x70FF, 0x40C1, 0x4E71, 0, 0}; // MOVEQ; MOVE SR,D1; NOP
        // Named jit::Instr{...} on purpose: g++ 13 rejects binding a
        // const Instr& parameter from a bare braced list because
        // MemoryContract carries `MemoryAccess access[MaxAccesses] = {}` (a C array
        // with a default member initializer); direct-list-initialization
        // of the temporary is accepted.
        ir.instrs.push_back(jit::Instr{0x1000, 0x70FF, 1, jit::Kind::Move,
                                       jit::FlagSetsCcr, 2});
        ir.instrs.push_back(jit::Instr{0x1002, 0x40C1, 1, jit::Kind::Alu,
                                       jit::FlagNone, 8, 8});
        ir.instrs.push_back(jit::Instr{0x1004, 0x4E71, 1, jit::Kind::AddrCalc,
                                       jit::FlagNone, 2});
        for (jit::Instr& in : ir.instrs) {
            in.memory = jit::describeMemory(in.opcode, false);
            in.semantics = jit::describeInstruction(in.opcode);
        }

        jit::Context ctx;
        ctx.cpu = &cpu;
        ctx.clockTarget = 100;
        jit::Backend* native =
            jit::selectBackend(backendKey, jit::kGuest68040);
        check(!std::strcmp(native->name(), backendName),
              "explicit native backend resolves");
        for (const bool packedCcr : {false, true}) {
            cpu.setClock(0);
            cpu.setPC(0x1000);
            cpu.setSR(0x2710);              // supervisor + IPL 7 + X
            cpu.setD(0, 0);
            cpu.setD(1, 0xA5A50000);
            const auto layout = cpu.pomJitLayout();
            auto* raw = reinterpret_cast<uint8_t*>(&cpu);
            raw[layout.srT1] = 1;
            raw[layout.srT0] = 1;
            raw[layout.srM] = 1;
            *reinterpret_cast<uint32_t*>(raw + layout.flags) = 0;

            jit::ResolvedConfig smokeConfig = jit::defaultResolvedConfig();
            smokeConfig.packedCcr = packedCcr;
            ctx.config = &smokeConfig;
            jit::ScopedResolvedConfig active(&smokeConfig);
            jit::Compiled* code = native->compile(ir, ctx).code;
            check(code != nullptr,
                  "MOVEQ/MOVE SR/NOP block compiles with either CCR layout");
            if (code) {
                jit::RunResult rr = native->run(code, ctx);
                std::printf("  packed=%d result: retired=%u exit=%s "
                            "D0=$%08X SR=$%04X PC=$%08X clock=%lld\n",
                            int(packedCcr), rr.instrs, jit::exitName(rr.exit),
                            cpu.getD(0), cpu.getSR(), cpu.getPC(),
                            (long long)cpu.getClock());
                check(rr.instrs == 3 && rr.slowInstrs == 0,
                      "three instructions retire natively");
                check(rr.exit == jit::Exit::BlockEnd,
                      "native block exits cleanly");
                check(cpu.getD(0) == 0xFFFFFFFFu,
                      "MOVEQ sign-extends into D0");
                check((cpu.getSR() & 0x1F) == 0x18,
                      "MOVEQ sets N, clears Z/V/C and preserves X");
                check(cpu.getD(1) == 0xA5A5F718u,
                      "MOVE SR,D1 reconstructs T/S/M, IPL and CCR without "
                      "clobbering high word");
                check(cpu.getPC() == 0x1006,
                      "native block commits its exit PC");
                check(cpu.getClock() == 12,
                      "native block charges exact cycles");
                native->release(code);
            }
            native->flushAll();
        }
    }
#endif

    // What the ACTIVE backend claims it can turn into host code. On a host
    // with no code generator every answer is false and this section simply
    // records that — the point is that the census and the block builder
    // agree with the backend actually selected.
    {
        // `auto` is the validated native generator on x86-64/AArch64 and the
        // portable floor elsewhere, so this remains a host-independent gate.
        jit::Backend* b = jit::selectBackend("auto", jit::kGuest68040);
        std::printf("[jit_backend] native coverage (%s)\n", b->name());
        const bool gen = b->caps().nativeCode;
        bool allNativeDescribed = true;
        for (unsigned opcode = 0; opcode <= 0xFFFF; opcode++) {
            if (b->canEmit(uint16_t(opcode)) &&
                !jit::describeInstruction(uint16_t(opcode)).valid()) {
                allNativeDescribed = false;
                break;
            }
        }
        check(allNativeDescribed,
              "every opcode claimed by the active backend has shared IR semantics");
        // The two opcodes a Mac ROM's hardware poll loops are built from.
        check(b->canEmit(0x082B) == gen, "BTST #imm,d16(A3)");
        check(b->canEmit(0x0344) == gen, "BCHG D1,D4");
        check(b->canEmit(0x08A1) == gen, "BCLR #imm,-(A1)");
        check(b->canEmit(0x08EA) == gen, "BSET #imm,d16(A2)");
        check(b->canEmit(0x033C) == gen,
              "BTST D1,#imm is the legal dynamic-immediate form");
        check(b->canEmit(0x083A) == gen,
              "BTST #imm,d16(PC) is the legal static PC-relative form");
        check(!b->canEmit(0x0808), "BTST #imm,An is illegal");
        check(!b->canEmit(0x083C), "BTST #imm,#imm is illegal");
        check(!b->canEmit(0x08FA), "BSET #imm,d16(PC) is illegal");
        check(!b->canEmit(0x08FC), "BSET #imm,#imm is illegal");
        check(b->canEmit(0x4A3C) == gen, "TST.B #imm is legal on 020+");
        check(b->canEmit(0x4A7C) == gen, "TST.W #imm is legal on 020+");
        check(b->canEmit(0x4ABC) == gen, "TST.L #imm is legal on 020+");
        check(b->canEmit(0x0C3A) == gen,
              "CMPI.B #imm,d16(PC) is legal on 020+");
        check(b->canEmit(0x0C7B) == gen,
              "CMPI.W #imm,d8(PC,Dn) is legal on 020+");
        check(!b->canEmit(0x0008), "ORI.B #imm,An is illegal");
        check(!b->canEmit(0x003A), "ORI.B #imm,d16(PC) is illegal");
        check(!b->canEmit(0x063C), "ADDI.B #imm,#imm is illegal");
        check(!b->canEmit(0x5008), "ADDQ.B #imm,An is illegal");
        check(b->canEmit(0x5048) == gen, "ADDQ.W #imm,An is legal");
        check(!b->canEmit(0x503A), "ADDQ.B #imm,d16(PC) is illegal");
        check(!b->canEmit(0x813C), "OR.B D0,#imm is illegal");
        check(!b->canEmit(0x423C), "CLR.B #imm is illegal");
        check(!b->canEmit(0x4208), "CLR.B An is illegal");
        check(!b->canEmit(0x4A08), "TST.B An is illegal");
        check(b->canEmit(0x4A48) == gen, "TST.W An is legal on 020+");
        check(!b->canEmit(0x4858), "PEA (An)+ is illegal");
        check(!b->canEmit(0x4860), "PEA -(An) is illegal");
        check(!b->canEmit(0x487C), "PEA #imm is illegal");
        check(b->canEmit(0x487A) == gen, "PEA d16(PC) is legal");
        check(!b->canEmit(0x1008), "MOVE.B An,Dn is illegal");
        check(!b->canEmit(0x1040), "MOVEA.B Dn,An is illegal");
        check(b->canEmit(0x3040) == gen, "MOVEA.W Dn,An is legal");
        check(b->canEmit(0x1180) == gen,
              "MOVE.B D0,d8(A0,Dn) has a shared indexed-destination lowering");
        check(b->canEmit(0x31BC) == gen,
              "MOVE.W #imm,d8(A0,Dn) has a shared indexed-destination lowering");
        check(b->canEmit(0x21B2) == gen,
              "MOVE.L d8(A2,Dn),d8(A0,Dn) has a shared indexed pair lowering");
        check(b->canEmit(0x66F8) == gen, "BNE.S -8");
        check(b->canEmit(0x2ADC) == gen, "MOVE.L (A4)+,(A5)+");
        check(b->canEmit(0x7000) == gen, "MOVEQ #0,D0");
        check(b->canEmit(0x49C7) == gen, "Speedometer EXTB.L D7");
        check(b->canEmit(0xD3C1) == gen, "ADDA.L D1,A1");
        // …and forms no backend may claim because they are Unsafe or opcode
        // overlaps. Both native generators implement brief indexed EAs;
        // full-format extensions remain an IR-time capability refusal.
        // LINK/UNLK/NOP are the $4Exx carve-out: no control transfer, no
        // SR/MMU/cache state, and 3.6 % of a real workload sitting at every
        // function entry and exit. They are compiled, and they no longer
        // end a block.
        check(b->canEmit(0x4E71) == gen, "NOP");
        check(b->canEmit(0x4E56) == gen, "LINK A6,#d16");
        check(b->canEmit(0x4E5E) == gen, "UNLK A6");
        check(jit::classify(0x4E71) != jit::Kind::Unsafe, "NOP does not end a block");
        check(jit::classify(0x4E56) != jit::Kind::Unsafe, "LINK does not end a block");
        check(jit::classify(0x4E5E) != jit::Kind::Unsafe, "UNLK does not end a block");
        // JSR/BSR/RTS are compiled as block TERMINATORS: 7 % of a real
        // workload, and each one used to be both an interpreter round trip
        // and a block boundary the linker could not cross.
        check(b->canEmit(0x4E75) == gen, "RTS");
        check(b->canEmit(0x4EB9) == gen, "JSR abs.l");
        check(b->canEmit(0x4EB0) == gen, "JSR indexed");
        check(b->canEmit(0x6100) == gen, "BSR");
        check(jit::endsBlockAfter(jit::classify(0x4E75)), "RTS terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x4EB9)), "JSR terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x6100)), "BSR terminates a block");
        check(jit::branchWords(0x4E75) == 1, "RTS is one word");
        check(jit::branchWords(0x61FF) == 3, "BSR.L is three words");
        check(jit::branchWords(0x4EB9) == 3, "JSR abs.l is three words");
        check(jit::branchWords(0x4EAE) == 2, "JSR d16(A6) is two words");
        check(jit::branchWords(0x4EB0, 0x81E1) == 3,
              "JSR full-index with word base displacement is three words");
        check(jit::branchWords(0x4EB0, 0x25A3) == 5,
              "JSR full-index with long base/outer displacements is five words");
        // …while the rest of the $4Exx group still ends a block BEFORE it.
        check(!b->canEmit(0x4E73), "RTE is never native");
        check(b->canEmit(0x4ED0) == gen,
              "JMP (A0) follows the active generator's coverage");
        check(jit::branchWords(0x4EF9) == 3, "JMP abs.l is three words");
        check(b->canEmit(0x48E7) == gen,
              "MOVEM.L regs,-(SP) follows the active generator's coverage");
        check(b->canEmit(0x4CDF) == gen,
              "MOVEM.L (SP)+,regs follows the active generator's coverage");
        check(b->canEmit(0x51C8) == gen,
              "DBRA follows the active generator's coverage");
        check(b->canEmit(0x40C0) == gen,
              "MOVE SR,D0 follows both native generators' coverage");
        checkSafe(0x40C0, "MOVE SR,D0 does not end a block");
        checkUnsafe(0x40D0, "MOVE SR,(A0) remains a block boundary");
        check(!b->canEmit(0xF200), "F-line is never native");
        check(b->canEmit(0x0130) == gen,
              "BTST Dn,d8(A0,Xn) follows active generator coverage");
        check(b->canEmit(0x4870) == gen,
              "PEA d8(A0,Xn) follows active generator coverage");
        check(b->canEmit(0x41F0) == gen,
              "LEA d8(A0,Xn),A0 follows active generator coverage");
        check(b->canEmit(0xCD4F),
              "EXG A6,A7 is native on both generators (x64 port 2026-08-21)");
        check(!b->canEmit(0x0108), "MOVEP is not BTST");
        check(b->canEmit(0x80C0) == gen, "DIVU.W D0,D0");
        check(b->canEmit(0x81FC) == gen, "DIVS.W #imm,D0");
        check(!b->canEmit(0x80C8), "DIVU.W An source is illegal");
        check(b->canEmit(0x81D0) == gen,
              "DIVS.W (A0),D0 follows active generator coverage");
        check(b->canEmit(0x4C40) == gen,
              "DIVL D0,Dr:Dq follows active generator coverage");
        check(b->canEmit(0x4C50) == gen,
              "DIVL (A0),Dr:Dq follows active generator coverage");
        check(b->canEmit(0x4C7C) == gen,
              "DIVL #imm,Dr:Dq follows active generator coverage");
        check(b->canEmit(0xC1C0) == gen,
              "MULS.W D0,D0 follows active generator coverage");
        check(b->canEmit(0xC2FC) == gen,
              "MULU.W #imm,D1 follows active generator coverage");
        check(b->canEmit(0xC1D0) == gen,
              "MULS.W (A0),D0 follows active generator coverage");
        check(!b->canEmit(0xC1C8), "MULS.W An source is illegal");
        check(!b->canEmit(0xC101), "ABCD is not OR-to-ea");
        check(b->canEmit(0xB308),
              "distinct-register CMPM is native on both generators "
              "(x64 port 2026-08-21), never EOR-to-ea");
        check(!b->canEmit(0xB108),
              "same-register CMPM keeps its dependent second EA in Moira");
    }

    std::printf("[jit_backend] block classifier\n");
    // These MUST end a block: they can change the MMU, a cache or the
    // supervisor bit, which would silently stale the code window.
    checkUnsafe(0x4E73, "RTE");
    checkUnsafe(0x4E76, "TRAPV");
    checkUnsafe(0x4E72, "STOP");
    checkUnsafe(0x4E70, "RESET");
    checkUnsafe(0x4E7A, "MOVEC from control register");
    checkUnsafe(0x4E7B, "MOVEC to control register");
    // JMP graduated from Unsafe to a Branch TERMINATOR (census 2026-07-30:
    // 0.7 % of the idle Finder) — for the plain EA modes only. Shared brief
    // data-EA lowering does not prove the dynamic target/queue contract, so
    // indexed control flow stays Unsafe.
    check(jit::endsBlockAfter(jit::classify(0x4ED0)), "JMP (A0) terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x4EF9)), "JMP (xxx).L terminates a block");
    checkUnsafe(0x4EF0, "JMP indexed stays Unsafe");
    check(!jit::selectBackend("auto", jit::kGuest68040)->canEmit(0x4EF0),
          "JMP indexed is not advertised as native");
    // DBcc was already a terminator; the census pass made it EMITTABLE.
    check(jit::endsBlockAfter(jit::classify(0x51C8)), "DBRA terminates a block");
    checkUnsafe(0x46C0, "MOVE to SR");
    checkSafe(0x40C0, "MOVE SR,D0 is a read-only block member");
    checkUnsafe(0x40D0, "MOVE SR,(A0)");
    checkUnsafe(0x44C0, "MOVE to CCR");
    checkUnsafe(0x007C, "ORI to SR");
    checkUnsafe(0x027C, "ANDI to SR");
    checkUnsafe(0x0A3C, "EORI to CCR");
    checkUnsafe(0x0E00, "MOVES");
    checkUnsafe(0x4AC0, "TAS (locked RMW)");
    checkUnsafe(0x484A, "BKPT");
    checkUnsafe(0x50FA, "TRAPcc");
    checkUnsafe(0xA000, "A-line");
    checkUnsafe(0xF000, "F-line (MMU / CINV / MOVE16 / FPU)");
    checkUnsafe(0xF518, "CINV");

    // These are ordinary straight-line code and must NOT end a block.
    checkSafe(0x2000, "MOVE.L D0,D0");
    checkSafe(0x3040, "MOVEA.W D0,A0");
    checkSafe(0x7000, "MOVEQ");
    checkSafe(0xD080, "ADD.L D0,D0");
    checkSafe(0x9080, "SUB.L");
    checkSafe(0xB080, "CMP.L");
    checkSafe(0xC080, "AND.L");
    checkSafe(0x0680, "ADDI.L");
    checkSafe(0x0C80, "CMPI.L");
    checkSafe(0x4280, "CLR.L D0");
    checkSafe(0x4A80, "TST.L D0");
    checkSafe(0x41F0, "LEA");
    checkSafe(0x48E7, "MOVEM");
    checkSafe(0xE188, "LSL.L");
    checkSafe(0x5280, "ADDQ.L");
    checkSafe(0x57C0, "Scc");

    // Branches are neither: they END a block, and are part of it. A
    // backend that can evaluate the condition and compute the target keeps
    // a loop inside generated code instead of returning to the engine at
    // every iteration — which is the whole reason J2 exists.
    check(jit::endsBlockAfter(jit::classify(0x6000)), "BRA terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x67FE)), "BEQ terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x51C8)), "DBcc terminates a block");
    check(!jit::endsBlock(jit::classify(0x6000)), "BRA is not Unsafe");
    check(jit::branchWords(0x67FE) == 1, "BEQ.B is one word");
    check(jit::branchWords(0x6700) == 2, "BEQ.W is two words");
    check(jit::branchWords(0x67FF) == 3, "BEQ.L is three words");
    check(jit::branchWords(0x51C8) == 2, "DBcc is two words");

    // Trap-capable but straight-line: allowed in a block, flagged for a
    // future code generator, and caught at replay by the pc check.
    check(jit::classify(0x80C0) == jit::Kind::Muldiv, "DIVU is Muldiv");
    check(jit::classify(0xC1C0) == jit::Kind::Muldiv, "MULS is Muldiv");
    check(jit::classify(0x4C40) == jit::Kind::Muldiv, "DIVL (020+) is Muldiv");
    check(jit::classify(0x4180) == jit::Kind::Muldiv, "CHK is trap-capable");
    check(jit::instrFlags(0x80C0, jit::Kind::Muldiv) & jit::FlagMayTrap,
          "word division carries FlagMayTrap");
    check(!(jit::instrFlags(0xC1C0, jit::Kind::Muldiv) & jit::FlagMayTrap),
          "word multiplication has no internal trap continuation");

    if (failures) {
        std::printf("[jit_backend] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[jit_backend] OK\n");
    return 0;
}
