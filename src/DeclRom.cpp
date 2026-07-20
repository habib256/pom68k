// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "DeclRom.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {

constexpr size_t kMaxRom = 8192;

// MAME BYTE4_XOR_BE — swap byte lane within each 32-bit word.
inline size_t byte4XorBe(size_t i) { return i ^ 3; }

class Builder {
public:
    explicit Builder(size_t cap = kMaxRom) : buf_(cap, 0xFF) {}

    uint32_t pos() const { return uint32_t(p_); }

    void offs(uint8_t type, uint32_t target) {
        uint32_t rel = target - pos();
        emit(type);
        emit(uint8_t(rel >> 16));
        emit(uint8_t(rel >> 8));
        emit(uint8_t(rel));
    }
    void rsrc(uint8_t type, uint32_t data) {
        emit(type);
        emit(uint8_t(data >> 16));
        emit(uint8_t(data >> 8));
        emit(uint8_t(data));
    }
    void endOfList() { emit(0xFF); emit(0); emit(0); emit(0); }
    void word(uint16_t v) { emit(uint8_t(v >> 8)); emit(uint8_t(v)); }
    void long_(uint32_t v) {
        emit(uint8_t(v >> 24)); emit(uint8_t(v >> 16));
        emit(uint8_t(v >> 8)); emit(uint8_t(v));
    }
    void string(const char* s) {
        while (*s) emit(*s++);
        emit(0);
        if (p_ & 1) emit(0);
    }

    uint32_t vModeParms(uint16_t w, uint16_t h, uint16_t rowBytes, uint8_t pixelSize) {
        uint32_t ret = pos();
        long_(50);
        long_(0);
        word(rowBytes);
        word(0); word(0); word(h); word(w); word(0); word(0);
        long_(0);
        long_(0x00480000);
        long_(0x00480000);
        word(0); word(pixelSize); word(1); word(pixelSize);
        long_(0); long_(0);
        return ret;
    }

    std::vector<uint8_t> finish() {
        buf_.resize(p_);
        DeclRom::finalize(buf_);
        return buf_;
    }

private:
    void emit(uint8_t b) {
        if (p_ < buf_.size()) buf_[p_++] = b;
    }
    std::vector<uint8_t> buf_;
    size_t p_ = 0;
};

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

} // namespace

uint32_t DeclRom::computeCrc(const uint8_t* data, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc = (crc << 1) | (crc >> 31);
        crc += data[i];
    }
    return crc;
}

void DeclRom::finalize(std::vector<uint8_t>& rom) {
    if (rom.size() < 20) return;
    size_t crcOff = rom.size() - 12;
    rom[crcOff] = rom[crcOff + 1] = rom[crcOff + 2] = rom[crcOff + 3] = 0;
    uint32_t crc = computeCrc(rom.data(), rom.size());
    rom[crcOff]     = uint8_t(crc >> 24);
    rom[crcOff + 1] = uint8_t(crc >> 16);
    rom[crcOff + 2] = uint8_t(crc >> 8);
    rom[crcOff + 3] = uint8_t(crc);
}

uint32_t DeclRom::dirOffset(const uint8_t* data, size_t n) {
    if (n < 20) return 0;
    uint32_t rel = uint32_t(data[n - 19]) << 16 | uint32_t(data[n - 18]) << 8 | data[n - 17];
    int32_t signedRel = int32_t(rel << 8) >> 8;
    return uint32_t(int32_t(n - 20) + signedRel);
}

bool DeclRom::validateFormatBlock(const uint8_t* data, size_t n) {
    if (n < 20) return false;
    auto check = [&](const uint8_t* p) {
        uint32_t tst = uint32_t(p[n - 6]) << 24 | uint32_t(p[n - 5]) << 16
                     | uint32_t(p[n - 4]) << 8 | p[n - 3];
        uint16_t fmt = uint16_t(p[n - 8]) << 8 | p[n - 7];
        uint8_t lanes = p[n - 1];
        return tst == kTestPattern && fmt == kFormatRev && (lanes == kByteLanes || lanes == 0xE1);
    };
    if (check(data)) return true;
    // MAME inverted declaration ROMs (342-0008-a.bin): reverse + invert first.
    std::vector<uint8_t> tmp(data, data + n);
    for (size_t i = 0, j = n - 1; i < j; i++, j--) std::swap(tmp[i], tmp[j]);
    uint8_t lanes = tmp[n - 1];
    if (tmp[n - 2] == 0xFF) lanes = uint8_t(lanes ^ 0xFF);
    if (lanes != kByteLanes && lanes != 0xE1) return false;
    for (auto& b : tmp) b ^= 0xFF;
    return check(tmp.data());
}

std::vector<uint8_t> DeclRom::buildSynthetic(uint32_t fbBase) {
    Builder b;

    uint32_t boardType = b.pos();
    b.word(1); b.word(0); b.word(0); b.word(0);
    uint32_t boardName = b.pos();
    b.string("Toby Frame Buffer");
    uint32_t vendorInfo = b.pos();
    b.offs(0x01, b.pos() + 16); b.string("Apple Computer");
    b.endOfList();

    uint32_t sRsrcBoard = b.pos();
    b.offs(0x01, boardType);
    b.offs(0x02, boardName);
    b.rsrc(0x20, 0x0001);
    b.offs(0x24, vendorInfo);
    b.endOfList();

    uint32_t videoType = b.pos();
    b.word(3); b.word(1); b.word(1); b.word(1);   // Display, DrHW=1 (TFB)
    uint32_t videoName = b.pos();
    b.string("Display_Video_Apple_TFB");

    uint32_t videoDrvr = b.pos();
    b.long_(0x20);
    b.word(0x4C00); b.word(0); b.word(0); b.word(0);
    b.word(0x1C); b.word(0x1E); b.word(0x20); b.word(0x22); b.word(0x24);
    b.word(0x4E75);   // Open: rts
    b.word(0x4E75);   // Prime: rts
    b.word(0x4E75);   // Control: rts
    b.word(0x4E75);   // Status: rts
    b.word(0x4E75);   // Close: rts

    uint32_t vidDrvrDir = b.pos();
    b.offs(0x02, videoDrvr);
    b.endOfList();

    uint32_t minorBase = b.pos();
    b.long_(fbBase);
    uint32_t minorLen = b.pos();
    b.long_(0x80000);

    uint32_t vidParms1 = b.vModeParms(640, 480, 80, 1);
    uint32_t vidMode1 = b.pos();
    b.offs(0x01, vidParms1);
    b.rsrc(0x03, 1);
    b.rsrc(0x04, 0);
    b.endOfList();

    uint32_t sRsrcVideo = b.pos();
    b.offs(0x01, videoType);
    b.offs(0x02, videoName);
    b.offs(0x04, vidDrvrDir);
    b.rsrc(0x08, 0x0001);
    b.offs(0x0A, minorBase);
    b.offs(0x0B, minorLen);
    b.rsrc(0x7D, 6);
    b.offs(0x80, vidMode1);
    b.endOfList();

    uint32_t sRsrcDir = b.pos();
    b.offs(0x01, sRsrcBoard);
    b.offs(0x80, sRsrcVideo);
    b.endOfList();

    b.offs(0, sRsrcDir);
    b.long_(uint32_t(b.pos() + 16));
    b.long_(0);
    b.word(kFormatRev);
    b.long_(kTestPattern);
    b.word(0x000F);

    return b.finish();
}

std::vector<uint8_t> DeclRom::installRaw(const uint8_t* raw, size_t n) {
    if (!raw || n == 0) return {};
    std::vector<uint8_t> rom(raw, raw + n);

    for (size_t i = 0, j = n - 1; i < j; i++, j--)
        std::swap(rom[i], rom[j]);

    uint8_t byteLanes = rom[n - 1];
    bool inverted = rom[n - 2] == 0xFF;
    if (inverted) byteLanes = uint8_t(byteLanes ^ 0xFF);

    std::vector<uint8_t> out;
    size_t outLen = n;

    auto scramble = [&](size_t mul) {
        out.resize(n * mul, 0);
        outLen = n * mul;
    };

    switch (byteLanes) {
    case 0x0F:
        out.resize(n);
        for (size_t i = 0; i < n; i++) out[byte4XorBe(i)] = rom[i];
        break;
    case 0xE1:
        scramble(4);
        for (size_t i = 0; i < n; i++) out[byte4XorBe(i * 4)] = rom[i];
        break;
    default:
        return {};
    }

    if (inverted)
        for (auto& b : out) b ^= 0xFF;
    return out;
}

std::vector<uint8_t> DeclRom::loadTobyRaw(const std::string& path) {
    auto raw = readFile(path);
    if (raw.size() != 4096) {
        std::fprintf(stderr, "DeclRom: %s is %zu bytes, want 4096\n",
                     path.c_str(), raw.size());
        return {};
    }
    return installRaw(raw.data(), raw.size());
}
