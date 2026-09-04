// POM68K — helpers shared by generated-code backends.

#include "JitRuntime.h"

#include "Moira.h"

extern "C" int pom68kJitJsr040(moira::Moira* cpu, uint32_t instructionPc,
                               uint32_t returnAddress, uint32_t target,
                               uint32_t opcode) noexcept
{
    return cpu->pomJitJsr040(instructionPc, returnAddress, target,
                            uint16_t(opcode)) ? 1 : 0;
}
