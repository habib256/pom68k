// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: the portability seam. No emulated machine, no assets — this
// checks the things that decide whether the JIT exists at all on a given
// host (src/jit/POM68K_JIT.md invariants 6 and 7):
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
#include "jit/JitIr.h"

#if defined(POM68K_JIT_BACKEND_A64)
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
    // ── One IR memory contract, one proof planner, every backend ─────────
    // These are encoding-level and asset-free. The copyback gates exercise
    // the generated A64/x64 implementations of the same plans below.
    {
        std::printf("[jit_backend] IR semantic and memory protocols\n");
        check(sizeof(jit::MemoryContract) <= 40,
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

        const auto write030 = jit::describeMemory(0x2140, true); // MOVE.L D0,d16(A0)
        jit::MemoryProofOptions proof030;
        proof030.exactWrites = true;
        proof030.restartableWriteRequired = true;
        const auto lastWrite = jit::memoryProofPlan(write030, proof030);
        check(lastWrite.protocol == jit::MemoryProofProtocol::SingleWrite &&
              lastWrite.restartableLastWrite() && lastWrite.exactThunkMask == 1,
              "68030 proved LASTWRITE produces a restartable exact-write plan");

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
        const auto eor = jit::describeInstruction(0xB592); // EOR.L D2,(A2)
        check(eor.operation == jit::SemanticOp::AluRegToEa &&
              eor.alu == jit::AluOperation::Eor && eor.bytes() == 4,
              "IR distinguishes register-to-EA EOR from CMP encoding overlap");
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
        };
        for (const auto& e : saved) e.clear();

        check(!jit::packedCcrEnabled() && !jit::registerCacheEnabled() &&
              !jit::edgeLinkCellsEnabled(),
              "measured codegen experiments stay off by default");
        check(jit::dynamicRegisterBitfieldEnabled(),
              "measured dynamic register bitfields stay on by default");

        setEnv("POM68K_JIT_PROFILE", "production");
        check(jit::accessThunkMode() == 2 && jit::linksEnabled() &&
              !jit::paranoidEnabled() && !jit::histogramEnabled(),
              "production profile is the current fast conformant policy");
        setEnv("POM68K_JIT_PROFILE", "conservative");
        check(jit::accessThunkMode() == 0 && !jit::linksEnabled() &&
              jit::paranoidEnabled() && !jit::cache040LinePairsEnabled(),
              "conservative profile keeps replay and revalidation boundaries");
        setEnv("POM68K_JIT_PROFILE", "instrumented");
        check(jit::accessThunkMode() == 2 && !jit::linksEnabled() &&
              jit::paranoidEnabled() && jit::histogramEnabled() &&
              jit::cache040LineReadStatsEnabled(),
              "instrumented profile enables census and proof counters");
        setEnv("POM68K_JIT_ACCESS_THUNK", "1");
        check(jit::accessThunkMode() == 1,
              "a leaf knob explicitly overrides its selected profile");

        // An Engine publishes this resolved snapshot while compiling. Later
        // process-wide changes are inputs for a future Engine only.
        setEnv("POM68K_JIT_PROFILE", "production");
        setEnv("POM68K_JIT_ACCESS_THUNK", nullptr);
        setEnv("POM68K_JIT_PROFIT_SCORE", "64");
        setEnv("POM68K_JIT_PACKED_CCR", "1");
        setEnv("POM68K_JIT_REG_CACHE", "1");
        setEnv("POM68K_JIT_EDGE_CELLS", "1");
        setEnv("POM68K_JIT_DYNAMIC_BITFIELD", "1");
        jit::ResolvedConfig resolved = jit::resolveConfig();
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
        check(jit::accessThunkMode() == 0,
              "legacy accessors remain live outside an Engine compile scope");
        check(!jit::packedCcrEnabled() && !jit::registerCacheEnabled() &&
              !jit::edgeLinkCellsEnabled(),
              "live experiment accessors observe later process overrides");
        check(!jit::dynamicRegisterBitfieldEnabled(),
              "live dynamic-bitfield accessor observes a later override");

        // The § C.4nonies admissions follow the backend's accessClockBias
        // declaration — ON where the access thunks carry the peripheral-
        // phase clock bias (x64), OFF where the alignment has not landed
        // (a64, threaded) — and an explicit env wins in either direction.
        // Pinned here so the coupling cannot rot into "the admission
        // default outran the alignment", which is the exact failure the
        // declaration exists to prevent.
        setEnv("POM68K_JIT_RESTART_BASE", nullptr);
        setEnv("POM68K_JIT_BSRW", nullptr);
        jit::ResolvedConfig aligned = jit::resolveConfig();
        aligned.applyBackendDefaults(true, /*accessClockBias=*/true);
        check(aligned.restartBaseAdmission && aligned.bsrWideAdmission,
              "a bias-declaring backend turns both admissions on by default");
        jit::ResolvedConfig unaligned = jit::resolveConfig();
        unaligned.applyBackendDefaults(true, /*accessClockBias=*/false);
        check(!unaligned.restartBaseAdmission && !unaligned.bsrWideAdmission,
              "a backend without the alignment keeps both admissions off");
        setEnv("POM68K_JIT_RESTART_BASE", "0");
        setEnv("POM68K_JIT_BSRW", "0");
        jit::ResolvedConfig vetoed = jit::resolveConfig();
        vetoed.applyBackendDefaults(true, /*accessClockBias=*/true);
        check(!vetoed.restartBaseAdmission && !vetoed.bsrWideAdmission,
              "an explicit 0 beats the bias-declaring backend's admission default");
        setEnv("POM68K_JIT_RESTART_BASE", "1");
        setEnv("POM68K_JIT_BSRW", "1");
        jit::ResolvedConfig forced = jit::resolveConfig();
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
    check(jit::defaultEngine(false) == jit::EngineKind::Interp,
          "non-68040 guests default to the interpreter");
    check(jit::defaultEngine(true) == jit::EngineKind::Jit,
          "validated 68040 guests default to the accelerated engine");
    setCpuEngineEnv("interp");
    check(jit::defaultEngine(true) == jit::EngineKind::Interp,
          "POM68K_CPU_ENGINE=interp explicitly restores the reference");
    setCpuEngineEnv("jit");
    check(jit::defaultEngine(false) == jit::EngineKind::Jit,
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
    // fired — `auto` on an 030 now serves the generator on x86-64 too.
    if (hasX64) {
        jit::Backend* on030 = jit::selectBackend("x64", jit::kGuest68030);
        check(!std::strcmp(on030->name(), "x86-64"),
              "x64 requested for a 68030 guest is honoured since the "
              "declaration (2026-08-18)");
        jit::Backend* on040 = jit::selectBackend("x64", jit::kGuest68040);
        check(!std::strcmp(on040->name(), "x86-64"),
              "…and still served for the 68040 it was written for");
        jit::Backend* auto030 = jit::selectBackend("auto", jit::kGuest68030);
        check(!std::strcmp(auto030->name(), "x86-64"),
              "auto on a 68030 serves the generator — the C.5 flip "
              "(2026-08-21, JIT_BRINGUP § C.5)");
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

#if defined(POM68K_JIT_BACKEND_A64)
    // Execute a hand-built all-native block. This catches emitter encodings,
    // the Darwin AAPCS frame, MAP_JIT W^X transitions, I-cache invalidation,
    // MOVEQ flags and cycle charging without needing a user ROM.
    {
        std::printf("[jit_backend] aarch64 native smoke\n");
        check(!std::strcmp(autoPick->name(), "aarch64"),
              "auto selects the validated AArch64 backend on arm64");
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
        static Q605Memory mem;
        static Cpu040 cpu(mem);
        mem.setCpu(&cpu);
        cpu.setClock(0);
        cpu.setPC(0x1000);
        cpu.setSR(0x2710);                  // supervisor + IPL 7 + X
        cpu.setD(0, 0);
        cpu.setD(1, 0xA5A50000);
        const auto layout = cpu.pomJitLayout();
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&cpu) +
                                     layout.flags) = 0;

        jit::BlockIr ir;
        ir.entryPc = ir.codeBase = 0x1000;
        ir.super = true;
        ir.code = {0x70FF, 0x40C1, 0x4E71, 0, 0}; // MOVEQ; MOVE SR,D1; NOP
        ir.instrs.push_back({0x1000, 0x70FF, 1, jit::Kind::Move,
                             jit::FlagSetsCcr, 2});
        ir.instrs.push_back({0x1002, 0x40C1, 1, jit::Kind::Alu,
                             jit::FlagNone, 8, 8});
        ir.instrs.push_back({0x1004, 0x4E71, 1, jit::Kind::AddrCalc,
                             jit::FlagNone, 2});
        for (jit::Instr& in : ir.instrs) {
            in.memory = jit::describeMemory(in.opcode, false);
            in.semantics = jit::describeInstruction(in.opcode);
        }

        jit::Context ctx;
        ctx.cpu = &cpu;
        ctx.clockTarget = 100;
        jit::Backend* a64 = jit::selectBackend("a64", jit::kGuest68040);
        check(!std::strcmp(a64->name(), "aarch64"), "explicit a64 resolves");
        jit::Compiled* code = a64->compile(ir, ctx).code;
        check(code != nullptr, "MOVEQ/NOP block compiles to AArch64");
        if (code) {
            jit::RunResult rr = a64->run(code, ctx);
            std::printf("  result: retired=%u exit=%s D0=$%08X SR=$%04X "
                        "PC=$%08X clock=%lld\n",
                        rr.instrs, jit::exitName(rr.exit), cpu.getD(0), cpu.getSR(),
                        cpu.getPC(), (long long)cpu.getClock());
            check(rr.instrs == 3 && rr.slowInstrs == 0,
                  "three instructions retire natively");
            check(rr.exit == jit::Exit::BlockEnd, "native block exits cleanly");
            check(cpu.getD(0) == 0xFFFFFFFFu, "MOVEQ sign-extends into D0");
            check((cpu.getSR() & 0x1F) == 0x18,
                  "MOVEQ sets N, clears Z/V/C and preserves X");
            check(cpu.getD(1) == 0xA5A52718u,
                  "MOVE SR,D1 reconstructs IPL and CCR without clobbering high word");
            check(cpu.getPC() == 0x1006, "native block commits its exit PC");
            check(cpu.getClock() == 12, "native block charges exact cycles");
            a64->release(code);
        }
        a64->flushAll();
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
        check(b->canEmit(0x66F8) == gen, "BNE.S -8");
        check(b->canEmit(0x2ADC) == gen, "MOVE.L (A4)+,(A5)+");
        check(b->canEmit(0x7000) == gen, "MOVEQ #0,D0");
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
        check(b->canEmit(0x6100) == gen, "BSR");
        check(jit::endsBlockAfter(jit::classify(0x4E75)), "RTS terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x4EB9)), "JSR terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x6100)), "BSR terminates a block");
        check(jit::branchWords(0x4E75) == 1, "RTS is one word");
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
        const bool a64 = !std::strcmp(b->name(), "aarch64");
        check(b->canEmit(0x51C8) == gen,
              "DBRA follows the active generator's coverage");
        check(b->canEmit(0x40C0) == a64,
              "MOVE SR,D0 follows AArch64 native coverage");
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
        check(!b->canEmit(0x81C0), "DIVU is not an ALU direction");
        check(!b->canEmit(0xC1C0), "MULS is not an ALU direction");
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
    check(jit::classify(0x4C40) == jit::Kind::Muldiv, "DIVL (020+) is Muldiv");
    check(jit::classify(0x4180) == jit::Kind::Muldiv, "CHK is trap-capable");
    check(jit::instrFlags(0x80C0, jit::Kind::Muldiv) & jit::FlagMayTrap,
          "Muldiv carries FlagMayTrap");

    if (failures) {
        std::printf("[jit_backend] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[jit_backend] OK\n");
    return 0;
}
