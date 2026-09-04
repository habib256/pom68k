// Exhaustive equivalence check for V8Memory::ramIndexNext vs ramIndex(addr+1).
#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>

enum class Model { LcII, Lc, ClassicII, ColorClassic, MacTv };

struct Geo {
    bool overlay_ = false;
    Model model_ = Model::LcII;
    uint32_t mbLoc_ = 0, mbSize_ = 0, simmLoc_ = 0, simmPhys_ = 0, simmOff_ = 0;
    bool mbMapped_ = false, simmMapped_ = false;
    uint32_t totalRam_ = 0xA00000, mbRam_ = 0x400000;

    uint32_t ramIndex(uint32_t addr) const {
        if (overlay_) return 0xFFFFFFFF;
        if (addr >= 0x800000)
            return model_ == Model::MacTv ? 0xFFFFFFFF : (addr & 0x1FFFFF);
        if (mbMapped_ && addr >= mbLoc_ && addr < mbLoc_ + mbSize_)
            return addr - mbLoc_;
        if (simmMapped_ && addr - simmLoc_ < simmPhys_)
            return simmOff_ + (addr - simmLoc_);
        return 0xFFFFFFFF;
    }
    uint32_t ramIndexNext(uint32_t addr, uint32_t i0) const {
        if ((addr | mbLoc_ | mbSize_ | simmLoc_ | simmPhys_) & 1)
            return ramIndex(addr + 1);
        return i0 == 0xFFFFFFFF ? 0xFFFFFFFF : i0 + 1;
    }
    void applyRamConfig(uint8_t config) {
        if (overlay_) return;
        if (model_ == Model::MacTv) {
            simmPhys_ = totalRam_ > mbRam_ ? totalRam_ - mbRam_ : 0;
            simmOff_ = mbRam_;
            simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;
            mbLoc_ = 0; mbSize_ = mbRam_; mbMapped_ = true;
            return;
        }
        simmPhys_ = totalRam_ > mbRam_ ? totalRam_ - mbRam_ : 0;
        simmOff_ = mbRam_;
        if (totalRam_ == 0xA00000) { simmPhys_ = 0x800000; simmOff_ = 0x200000; }
        simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;
        static constexpr uint32_t kSimmCfg[4] = { 0, 0x200000, 0x400000, 0x800000 };
        mbLoc_ = simmMapped_ ? kSimmCfg[(config >> 6) & 3] : 0;
        mbMapped_ = (config & 0xC0) != 0xC0;
        mbSize_ = (config & 0x20) ? 0x200000 : mbRam_;
    }
};

static long long bad = 0, checked = 0;

static void check(const Geo& g, uint32_t addr) {
    if (addr >= 0xA00000) return;              // read16/write16 RAM window only
    const uint32_t i0 = g.ramIndex(addr);
    const uint32_t got = g.ramIndexNext(addr, i0);
    const uint32_t want = g.ramIndex(addr + 1);
    ++checked;
    if (got != want) {
        if (++bad < 20)
            std::printf("MISMATCH model=%d mbLoc=%08X mbSize=%08X mbM=%d "
                        "simmLoc=%08X simmPhys=%08X simmOff=%08X simmM=%d "
                        "addr=%08X i0=%08X got=%08X want=%08X\n",
                        int(g.model_), g.mbLoc_, g.mbSize_, g.mbMapped_,
                        g.simmLoc_, g.simmPhys_, g.simmOff_, g.simmMapped_,
                        addr, i0, got, want);
    }
}

int main() {
    const uint32_t rams[] = { 0xA00000, 0x400000, 0x200000, 0x800000, 0x100000,
                              4u << 20, 6u << 20, 0xC00000 };
    std::mt19937 rng(12345);
    for (int m = 0; m < 5; m++) {
        Model model = Model(m);
        for (uint32_t total : rams) {
            for (int cfg = 0; cfg < 256; cfg++) {
                Geo g;
                g.model_ = model;
                g.totalRam_ = total;
                g.mbRam_ = model == Model::Lc ? 0x200000 : 0x400000;
                if (model == Model::MacTv) g.simmLoc_ = 0x400000;
                g.applyRamConfig(uint8_t(cfg));
                std::vector<uint32_t> seams = {
                    0, 1, 2, 0x800000, 0x7FFFFF, 0x9FFFFF, 0x1FFFFF, 0x200000,
                    g.mbLoc_, g.mbLoc_ + g.mbSize_, g.simmLoc_,
                    g.simmLoc_ + g.simmPhys_, 0x400000, 0x600000, 0xA00000 };
                for (uint32_t s : seams)
                    for (int d = -4; d <= 4; d++) {
                        long long a = (long long)s + d;
                        if (a < 0 || a >= 0xA00000) continue;
                        check(g, (uint32_t)a);
                    }
                for (uint32_t a = 0; a < 0xA00000; a += 0x3FF) check(g, a);
                for (uint32_t a = 1; a < 0xA00000; a += 0x3FF) check(g, a);
                for (int k = 0; k < 200; k++) check(g, rng() % 0xA00000);
                for (int f = 0; f < 4; f++) {          // odd geometry: fallback
                    Geo h = g;
                    (f == 0 ? h.mbLoc_ : f == 1 ? h.mbSize_
                     : f == 2 ? h.simmLoc_ : h.simmPhys_) |= 1u;
                    h.mbMapped_ = h.simmMapped_ = true;
                    for (uint32_t a = 0; a < 0xA00000; a += 0x2FFF) {
                        check(h, a); check(h, a + 1);
                    }
                }
            }
        }
    }
    std::printf("checked=%lld mismatches=%lld\n", checked, bad);
    return bad ? 1 : 0;
}
