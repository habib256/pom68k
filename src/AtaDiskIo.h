// POM68K — HFS host operations on ATA, through its journalled sector path.
#pragma once
#include "AtaDisk.h"
#include "HfsInject.h"
namespace hfsinject {
struct AtaDiskIo final : BlockIo {
    explicit AtaDiskIo(AtaDisk& disk) : disk_(disk) {}
    bool read(uint32_t lba, uint8_t* out) override { return disk_.hostRead(lba, out); }
    bool write(uint32_t lba, const uint8_t* in) override { return disk_.hostWrite(lba, in); }
    uint32_t blocks() const override { return disk_.sectorCount(); }
private:
    AtaDisk& disk_;
};
} // namespace hfsinject
