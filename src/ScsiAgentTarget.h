// POM68K — a media-free processor endpoint for the guest-control mailbox.
// An IDE-only machine still needs target 0 for the shipped agent's C0/C1
// commands. The controller handles those; ordinary probes see no disk.
#pragma once
#include "ScsiTarget.h"
#include <algorithm>
#include <cstring>

class ScsiAgentTarget final : public ScsiTarget {
public:
    void reset() { lun_ = kNoIdentify; sense_ = 0; }
    bool present() const override { return true; }
    void selectLun(uint8_t lun) override { lun_ = lun; }
    int writeByteCount(const uint8_t*, int) const override { return 0; }
    uint8_t command(const uint8_t* cdb, int length, std::vector<uint8_t>& out,
                    const std::vector<uint8_t>&) override {
        out.clear();
        if (!cdb || length < 6) return 2;
        const uint8_t lun = lun_ == kNoIdentify ? cdb[1] >> 5 : lun_;
        if (cdb[0] == 0x12) {
            if (cdb[1] & 3) { sense_ = 0x24; return 2; }
            out.assign(36, 0);
            out[0] = lun ? 0x7F : 3; // processor, never direct-access storage
            out[2] = out[3] = 2; out[4] = 31;
            std::memcpy(out.data() + 8, "POM68K  Guest control   0001", 28);
            out.resize(std::min<size_t>(out.size(), cdb[4]));
            return 0;
        }
        if (cdb[0] == 3) {
            out.assign(18, 0); out[0] = 0x70; out[7] = 10;
            out[2] = sense_ ? 5 : 0; out[12] = sense_;
            out.resize(std::min<size_t>(out.size(), cdb[4]));
            sense_ = 0;
            return 0;
        }
        if (!lun && cdb[0] == 0) return 0;
        sense_ = lun ? 0x25 : 0x20;
        return 2;
    }
    template<class Ar> void visit(Ar& ar) { ar(lun_, sense_); }
private:
    uint8_t lun_ = kNoIdentify, sense_ = 0;
};
