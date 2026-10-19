// PowerBook profile differences. MAME macpwrbk030.cpp macpb140/145/145b/160/165/170/180;
// Apple InternalOnlyEqu.a boxPowerBook145 = 48. No guest identity patches.
#pragma once
#include "MachineCatalog.h"

namespace pom68k {
struct Pb030Profile {
    int64_t cpuHz;
    bool fpu;
    uint8_t boxFlag;
    uint32_t baseRamBytes;
    const char* cpuLine;
};
constexpr Pb030Profile pb030Profile(SnapMachine profile) {
    switch(profile) {
    case SnapMachine::Pb165: return {33000000,false,78,4u<<20,"68030 @ 33 MHz, GSC"};
    case SnapMachine::Pb180c: return {33000000,true,65,4u<<20,"68030 + 68882 @ 33 MHz, color LCD"};
    case SnapMachine::Pb180: return {33000000,true,27,4u<<20,"68030 + 68882 @ 33 MHz, GSC"};
    case SnapMachine::Pb160: return {25000000,false,28,4u<<20,"68030 @ 25 MHz, GSC"};
    case SnapMachine::Pb145: return {25000000,false,48,2u<<20,"68030 @ 25 MHz"};
    case SnapMachine::Pb145B:return {25000000,false,48,4u<<20,"68030 @ 25 MHz"};
    case SnapMachine::Pb170: return {25000000,true,15,4u<<20,"68030 + 68882 @ 25 MHz"};
    default: return {15667200,false,19,2u<<20,"68030 @ 15.6672 MHz"};
    }
}
} // namespace pom68k
