// Cache-active 68040 JSR commit tail. Unlike ordinary access thunks, a zero
// result has already processed the post-push fault and takes Exit::Fault.
void Emitter::emitJsr040Transaction(size_t i, uint16_t op, unsigned opCost,
                                    bool constant,
                                    const ControlFlowPlan& control)
{
    const Instr& in = ir_.instrs[i];
    spillClock();
    a_.movRR(Sz::Q, RDI, kCpu);
    a_.movRI(RSI, in.pc);
    a_.movRI(RDX, control.returnAddress);
    a_.movRM(Sz::L, RCX, F(kFValue));
    a_.movRI(R8, op);
    call(reinterpret_cast<void*>(&pom68kJitJsr040));
    fillClock();
    a_.testRR(Sz::L, RAX, RAX);
    a_.jcc(Cc::E, *exitFault_);
    chargeCycles(int(opCost));
    retire();
    if (constant) leaveTo(control.target);
    else          leaveToDynamic();
}
