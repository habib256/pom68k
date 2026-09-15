// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool: list a NuBus declaration ROM's sResources the way the Slot
// Manager reads them — from the format block at the top of the slot space,
// through the byte lanes it declares, offsets relative to the field that
// holds them (Designing Cards and Drivers for the Macintosh Family, ch. 8).
//   declrom_dump synthetic            the sResource DeclRom::buildSynthetic emits
//   declrom_dump <342-0008-a.bin>     a card ROM file, lane-installed as the card presents it
//   declrom_dump ... --code <out.bin> also write each driver's code block dense, for dasm
// Not a gate: it reads, it does not judge. Its first use (2026-09-15) was
// to see what the real Toby sResource carries that the synthetic one does
// not, once a System 7 volume drew nothing on the synthetic driver.

#include "DeclRom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Lanes {
    const std::vector<uint8_t>& img;
    uint8_t mask = 0x0F;               // bit i: byte lane i carries data
    bool valid(uint32_t a) const { return (mask >> (a & 3)) & 1; }
    // The n data bytes at or above address `a`, skipping invalid lanes.
    std::vector<uint8_t> read(uint32_t a, size_t n) const {
        std::vector<uint8_t> out;
        for (uint32_t p = a; p < img.size() && out.size() < n; p++)
            if (valid(p)) out.push_back(img[p]);
        return out;
    }
    // The address n data bytes above `a` (the Slot Manager's sPointer).
    uint32_t advance(uint32_t a, size_t n) const {
        uint32_t p = a;
        while (n && p < img.size()) { if (valid(p)) n--; p++; }
        while (p < img.size() && !valid(p)) p++;
        return p;
    }
    uint32_t be32(uint32_t a) const { auto b = read(a, 4); return b.size() == 4 ? (uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]) : 0; }
    uint16_t be16(uint32_t a) const { auto b = read(a, 2); return b.size() == 2 ? uint16_t(b[0] << 8 | b[1]) : 0; }
    // sOffset: an entry's 24-bit signed offset, relative to the entry.
    int32_t s24(uint32_t a) const { auto b = read(a, 4); if (b.size() < 4) return 0; int32_t v = int32_t(uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]); return (v << 8) >> 8; }
    // `n` data bytes further up in address space: entry targets are byte
    // offsets in lane-adjusted address space — n data bytes, not n addresses.
    uint32_t target(uint32_t entry) const {
        const int32_t off = s24(entry);
        if (off >= 0) return advance(entry, size_t(off));
        // Backwards: step down `-off` data bytes.
        uint32_t p = entry; size_t n = size_t(-off);
        while (n && p > 0) { p--; if (valid(p)) n--; }
        return p;
    }
    std::string cstr(uint32_t a) const {
        std::string s;
        for (uint32_t p = a; p < img.size() && s.size() < 64; p++) {
            if (!valid(p)) continue;
            if (!img[p]) break;
            s += char(img[p]);
        }
        return s;
    }
};

const char* entryName(uint8_t id) {
    switch (id) {
        case 0x01: return "sRsrcType";      case 0x02: return "sRsrcName";
        case 0x03: return "sRsrcIcon";      case 0x04: return "sRsrcDrvrDir";
        case 0x05: return "sRsrcLoadRec";   case 0x06: return "sRsrcBootRec";
        case 0x07: return "sRsrcFlags";     case 0x08: return "sRsrcHWDevId";
        case 0x0A: return "MinorBaseOS";    case 0x0B: return "MinorLength";
        case 0x0C: return "MajorBaseOS";    case 0x0D: return "MajorLength";
        case 0x0E: return "sRsrcCicn";      case 0x0F: return "sRsrcIcl8";
        case 0x10: return "sRsrcIcl4";      case 0x20: return "BoardId/VidNames";
        case 0x21: return "PRAMInitData";   case 0x22: return "PrimaryInit";
        case 0x23: return "TimeOutConst";   case 0x24: return "VendorInfo";
        case 0x25: return "BoardFlags";     case 0x26: return "SecondaryInit";
        case 0x40: return "sGammaDir";      case 0x41: return "sVidAttributes";
        case 0x7D: return "sVidPixelPack";  case 0x7E: return "sVidDefMode?";
        case 0x7F: return "sVidDepth?";
        default: return id >= 0x80 ? "mode" : "?";
    }
}

void dumpDriverDir(const Lanes& L, uint32_t dir, const char* codeOut) {
    for (uint32_t e = dir; e < L.img.size(); e = L.advance(e, 4)) {
        const auto b = L.read(e, 4);
        if (b.size() < 4 || b[0] == 0xFF) break;
        const uint32_t blk = L.target(e);
        const uint32_t len = L.be32(blk);
        const uint32_t drvr = L.advance(blk, 4);
        std::printf("      driver id $%02X (%s): block %u bytes, DRVR flags $%04X delay %u evtMask $%04X menu %d "
                    "open %u prime %u ctl %u status %u close %u, name \"",
                    b[0], b[0] == 1 ? "68000" : b[0] == 2 ? "68020" : b[0] == 3 ? "68030" : "?",
                    len, L.be16(drvr), L.be16(L.advance(drvr, 2)), L.be16(L.advance(drvr, 4)),
                    int16_t(L.be16(L.advance(drvr, 6))), L.be16(L.advance(drvr, 8)),
                    L.be16(L.advance(drvr, 10)), L.be16(L.advance(drvr, 12)),
                    L.be16(L.advance(drvr, 14)), L.be16(L.advance(drvr, 16)));
        const auto name = L.read(L.advance(drvr, 18), 32);
        for (size_t i = 1; i < name.size() && i <= name[0]; i++) std::putchar(name[i]);
        std::printf("\"\n");
        if (codeOut) {
            const auto code = L.read(drvr, len - 4);
            std::string path = std::string(codeOut) + "." + std::to_string(b[0]) + ".bin";
            if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(code.data(), 1, code.size(), f);
                std::fclose(f);
                std::printf("      code written: %s (%zu bytes)\n", path.c_str(), code.size());
            }
        }
    }
}

void dumpVidParams(const Lanes& L, uint32_t blk) {
    const uint32_t len = L.be32(blk);
    const uint32_t p = L.advance(blk, 4);
    auto at = [&](size_t off) { return L.advance(p, off); };
    std::printf("len %u: baseOffset $%08X rowBytes %u bounds (%d,%d,%d,%d) version %u packType %u packSize %u "
                "hRes $%08X vRes $%08X pixelType %u pixelSize %u cmpCount %u cmpSize %u planeBytes %u",
                len, L.be32(at(0)), L.be16(at(4)), int16_t(L.be16(at(6))), int16_t(L.be16(at(8))),
                int16_t(L.be16(at(10))), int16_t(L.be16(at(12))), L.be16(at(14)), L.be16(at(16)),
                L.be32(at(18)), L.be32(at(22)), L.be32(at(26)), L.be16(at(30)), L.be16(at(32)),
                L.be16(at(34)), L.be16(at(36)), L.be32(at(38)));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: declrom_dump synthetic|<file> [--code out]\n"); return 2; }
    const char* codeOut = nullptr;
    for (int i = 2; i + 1 < argc; i++) if (!std::strcmp(argv[i], "--code")) codeOut = argv[i + 1];
    std::vector<uint8_t> img = !std::strcmp(argv[1], "synthetic")
        ? DeclRom::buildSynthetic(0xF9000000) : DeclRom::loadTobyRaw(argv[1]);
    if (img.empty()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    // The format block is the top 20 data bytes; its byteLanes byte is the
    // last data byte, and it is on a lane the mask says is valid — find it
    // by trying each mask the byte could be.
    Lanes L{img};
    uint32_t top = uint32_t(img.size());
    bool found = false;
    for (uint32_t phase = 0; phase < 4 && !found; phase++) {
        const uint32_t a = top - 1 - phase;
        const uint8_t lanes = img[a];
        const uint8_t mask = (lanes & 0xF0) == uint8_t((~lanes & 0x0F) << 4) ? (lanes & 0x0F) : 0;
        if (!mask) continue;
        L.mask = mask;
        if (!L.valid(a)) continue;
        // Walk 20 data bytes down to the block's start.
        uint32_t p = a + 1; size_t n = 20;
        while (n && p > 0) { p--; if (L.valid(p)) n--; }
        const uint32_t fb = p;
        if (L.be32(L.advance(fb, 14)) != DeclRom::kTestPattern) continue;
        found = true;
        std::printf("format block at $%X, byteLanes $%02X (mask %d%d%d%d), length %u, CRC $%08X, rev $%04X\n",
                    fb, lanes, (mask >> 3) & 1, (mask >> 2) & 1, (mask >> 1) & 1, mask & 1,
                    L.be32(L.advance(fb, 4)), L.be32(L.advance(fb, 8)), L.be16(L.advance(fb, 12)));
        const uint32_t dir = L.target(fb);
        std::printf("sResource directory at $%X\n", dir);
        for (uint32_t e = dir; e < img.size(); e = L.advance(e, 4)) {
            const auto b = L.read(e, 4);
            if (b.size() < 4 || b[0] == 0xFF) break;
            const uint32_t rs = L.target(e);
            std::printf("  sRsrc $%02X at $%X\n", b[0], rs);
            for (uint32_t f = rs; f < img.size(); f = L.advance(f, 4)) {
                const auto eb = L.read(f, 4);
                if (eb.size() < 4 || eb[0] == 0xFF) break;
                const uint8_t id = eb[0];
                std::printf("    $%02X %-16s ", id, entryName(id));
                const uint32_t t = L.target(f);
                if (id == 0x01) { auto ty = L.read(t, 8); std::printf("category %u cType %u drSW %u drHW %u", ty[0] << 8 | ty[1], ty[2] << 8 | ty[3], ty[4] << 8 | ty[5], ty[6] << 8 | ty[7]); }
                else if (id == 0x02 || id == 0x20) std::printf("\"%s\"", L.cstr(t).c_str());
                else if (id == 0x04) { std::printf("\n"); dumpDriverDir(L, t, codeOut); continue; }
                else if (id == 0x07 || id == 0x08 || id == 0x7D || id == 0x7E || id == 0x7F) std::printf("value %d", L.s24(f));
                else if (id == 0x0A || id == 0x0B || id == 0x0C || id == 0x0D) std::printf("$%08X (block len %u)", L.be32(L.advance(t, 4)), L.be32(t));
                else if (id >= 0x80) {
                    // A mode entry points at a LIST: mVidParams (1) → the
                    // block, mTable (2), mPageCnt (3), mDevType (4).
                    std::printf("mode $%02X:", id);
                    for (uint32_t m = t; m < img.size(); m = L.advance(m, 4)) {
                        const auto mb = L.read(m, 4);
                        if (mb.size() < 4 || mb[0] == 0xFF) break;
                        if (mb[0] == 1) { std::printf(" mVidParams "); dumpVidParams(L, L.target(m)); }
                        else if (mb[0] == 3) std::printf(" mPageCnt %d", L.s24(m));
                        else if (mb[0] == 4) std::printf(" mDevType %d", L.s24(m));
                        else if (mb[0] == 2) std::printf(" mTable → $%X", L.target(m));
                        else std::printf(" m$%02X", mb[0]);
                    }
                }
                else if (id == 0x22 || id == 0x26) std::printf("code block at $%X, %u bytes", t, L.be32(t));
                else if (id == 0x40) std::printf("gamma dir at $%X", t);
                else if (id == 0x41) std::printf("attributes %d", L.s24(f));
                else std::printf("→ $%X", t);
                std::printf("\n");
            }
        }
    }
    if (!found) { std::fprintf(stderr, "no format block found\n"); return 1; }
    return 0;
}
