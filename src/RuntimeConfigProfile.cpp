// POM68K — typed shared-ROM profile application.
#include "RuntimeConfigParsers.h"
#include "BoardIdentity.h"

namespace pom68k::app::detail {
void applyMachineProfile(MachineSelectionConfig& selection, CpuConfig& cpu,
                         pom68k::CoreConfig& core, SnapMachine profile) {
    switch (profile) {
    case SnapMachine::Plus:
    case SnapMachine::Mac512Ke:
        selection.plusRom = profile; break;
    case SnapMachine::MacIIFDHD:
    case SnapMachine::MacII:
    case SnapMachine::IIx:
    case SnapMachine::IIcx:
    case SnapMachine::SE30:
        selection.macIi = profile;
        break;
    case SnapMachine::Lc3:
    case SnapMachine::Lc3Plus:
        selection.sonora = profile;
        break;
    case SnapMachine::Lc520:
    case SnapMachine::Lc550:
    case SnapMachine::CClassic2:
        selection.aio = profile;
        break;
    case SnapMachine::IIvx:
    case SnapMachine::IIvi:
        selection.vasp = profile;
        break;
    // MEMCjr/F108 identity is the board-ID register alone ($5FFFFFFC, MAME
    // macquadra605/630.cpp), shared with the gates through BoardIdentity.h;
    // docs_test, CHANGELOG 2026-10-02 (eighth).
    case SnapMachine::Lc475:
    case SnapMachine::Lc575:
    case SnapMachine::Q605:
        selection.memcJr = profile;
        pom68k::applyBoardIdentity(core, profile);
        cpu.q605Fpu = core.cpu.q605Fpu == pom68k::Q605FpuMode::Integrated;
        break;
    case SnapMachine::Centris610:
    case SnapMachine::Centris650:
    case SnapMachine::Quadra610:
    case SnapMachine::Quadra650:
    case SnapMachine::Quadra800:
        selection.djMemc = profile;
        break;
    case SnapMachine::Pb160: case SnapMachine::Pb165: case SnapMachine::Pb180:
        selection.niagra = profile; break;
    case SnapMachine::Q700:
    case SnapMachine::Quadra900: case SnapMachine::Pb140: case SnapMachine::Pb170: case SnapMachine::Pb145: case SnapMachine::Pb145B:
        selection.spike = profile;
        break;
    case SnapMachine::Q630:
    case SnapMachine::Lc580:
        selection.f108 = profile;
        pom68k::applyBoardIdentity(core, profile);
        break;
    case SnapMachine::Duo210:
    case SnapMachine::Duo230:
    case SnapMachine::Duo250:
    case SnapMachine::Duo270c:
    case SnapMachine::Duo280: case SnapMachine::Duo280c: case SnapMachine::Pb150:
        selection.msc = profile;
        break;
    default:
        break;
    }
}

} // namespace pom68k::app::detail
