// Cache-active 68040 JSR commit tail. The helper owns the first stack
// mutation, target I-cache access and any resulting format-$7 exception.
void emitJsr040Transaction(Asm& a, const Layout& L, const BlockIr& ir,
                           const Instr& in, uint16_t op,
                           const ControlFlowPlan& control, unsigned opCost,
                           int fault, int epilogue, bool paced, int batch,
                           uint32_t linkMask)
{
    a.strX(21, 0, L.clock);
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillQueueLive(a, L);
    spillPackedCcr(a, L);
    a.movW(4, op);
    a.ldrW(3, 1, 48);                         // Frame::saveV = target
    a.movW(2, control.returnAddress);
    a.movW(1, in.pc);
    a.movX(16, uint64_t(uintptr_t(&pom68kJitJsr040)));
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                       // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    loadQueueLive(a, L);
    a.ldrX(21, 0, L.clock);
    reloadGeneratedState(a, L);
    a.cmpWZero(14);
    a.bCond(Asm::EQ, fault);
    chargeAndRetire(a, L, opCost, paced, batch, in.pc,
                    uint32_t(in.words) + 1, ir.super);
    if (control.targetKnown)
        leaveTo(a, ir, control.target, linkMask, epilogue);
    else
        leaveToDynamic(a, L, ir, linkMask, epilogue);
}
