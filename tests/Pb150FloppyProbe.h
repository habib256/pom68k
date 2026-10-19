// POM68K — the guest's File Manager must mount a real SWIM floppy.
#pragma once
#include "HfsBlankVolume.h"
#include "PixelPin.h"

namespace pb150floppy {
template<class Frame, class Logical>
bool check(MscMemory& mem, Frame&& frame, Logical&& logical) {
    if (!mem.isPb150() || !std::getenv("POM68K_PB150_FLOPPY")) return true;
    auto image = hfsblank::build(SonyDrive::kSize1440K, "PB150 floppy");
    image[1024 + 10] |= 1;
    if (!mem.internalDrive().insertImage(std::move(image))) return false;
    bool diagnostic = false;
    auto mounted = [&] {
        uint32_t vcb = 0;
        if (!logical(0x358, 4, vcb)) return false;
        for (int n = 0; vcb && n < 32; n++) {
            uint32_t length = 0, ref = 0;
            if (!logical(vcb + 44, 1, length) || length > 27 ||
                !logical(vcb + 74, 2, ref)) return false;
            std::string name;
            for (uint32_t i = 0; i < length; i++) {
                uint32_t c = 0;
                if (!logical(vcb + 45 + i, 1, c)) return false;
                name += char(c);
            }
            if (diagnostic) std::printf("VCB: %s driver=%d\n", name.c_str(), int16_t(ref));
            if (name == "PB150 floppy" && ref == 0xFFFB) return true;
            if (!logical(vcb, 4, vcb)) return false;
        }
        return false;
    };
    bool found = false;
    for (int f = 0; f < 1200 && !found; f++) { frame(); found = mounted(); }
    std::printf("JeDI SWIM: %s by the guest's .Sony driver\n", found ? "mounted" : "NOT mounted");
    if (!found) {
        diagnostic = true; mounted();
        std::vector<uint32_t> pixels;
        mem.decodeScreen(pixels);
        pixelpin::dumpIfAsked(pixels, 640, mem.screenHeight());
        const auto& stats = mem.swim().ismStats();
        std::printf("SWIM: ISM=%d mode=$%02X setup=$%02X motor=%d pops=%ld empty=%ld errors=%ld marks=%ld syncs=%ld bytes=%ld\n",
            mem.swim().ism(), mem.swim().ismModeReg(), mem.swim().ismSetupReg(),
            mem.internalDrive().motorOn(), stats.dataPops, stats.emptyPops,
            stats.errorReads, stats.marks, stats.syncs, stats.bytes);
    }
    return found;
}
} // namespace pb150floppy
