// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// rominfo — offline Mac ROM introspection, algorithms lifted from the
// Basilisk II study (docs/BASILISK_ROM_NOTES.md; source references are
// to ~/src/macemu BasiliskII/src at commit 96e512bd):
//   header       rom_patches.cpp print_rom_info() (offsets 0/4/8/18/26/34)
//   resources    rom_patches.cpp find_rom_resource()/list_rom_resources()
//   traps        rom_patches.cpp find_rom_trap() — the compressed ROM trap
//                address table at header +$22 (Toolbox $A800.., then OS
//                $A000..; $80 = unimplemented, $FF = absolute, else
//                cumulative offsets)
//   universal    rom_patches.cpp list_universal_infos() + the UniversalInfo
//                field map (BASILISK_ROM_NOTES.md §2.2)
//   decoder      rom_patches.cpp:1216-1227 — the (decoderInfo index,
//                low-mem global) pair table at fixed ROM offset $94A of
//                every $067C ROM: every hardware base the ROM installs
//                into low memory, self-described.
//
// Standalone: reads the ROM file directly, no emulator core. Build target
// `rominfo` (CMakeLists.txt). Typical uses: resolve an A-trap to a ROM
// offset for breakpointing in lcii_trace (--trap A053), check whether the
// ROM carries a no-FPU SANE (two PACK 4 resources), dump the hardware
// bases the ROM will demand from the V8 address map.
//
// Usage: rominfo <rom> [--trap XXXX]... [--traps] [--resources]
//                      [--universal] [--all]
//        (no section flag = header + resource summary + universal + decoder)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> rom;

uint32_t rd32(uint32_t o) {
    if (o + 4 > rom.size()) return 0;
    return uint32_t(rom[o]) << 24 | uint32_t(rom[o + 1]) << 16
         | uint32_t(rom[o + 2]) << 8 | rom[o + 3];
}
uint16_t rd16(uint32_t o) {
    if (o + 2 > rom.size()) return 0;
    return uint16_t(rom[o] << 8 | rom[o + 1]);
}

const char* familyName(uint16_t v) {
    switch (v) {                         // rom_patches.h:25-31
    case 0x0000: return "64K (original Macintosh)";
    case 0x0075: return "Plus (128K)";
    case 0x0276: return "SE/Classic (256/512K)";
    case 0x0178: return "Mac II, not 32-bit clean (256K)";
    case 0x067C: return "32-bit clean ($067C family)";
    default:     return "unknown";
    }
}

// Gestalt-ID <-> name (MacDesc[], rom_patches.cpp:196-257, abridged to the
// IDs plausible in $067C ROMs)
const char* modelName(int gestaltId) {
    switch (gestaltId) {
    case 6:  return "Mac II";        case 7:  return "Mac IIx";
    case 8:  return "Mac IIcx";      case 9:  return "Mac SE/030";
    case 11: return "Mac IIci";      case 13: return "Mac IIfx";
    case 18: return "Mac IIsi";      case 19: return "Mac LC";
    case 20: return "Quadra 900";    case 22: return "Quadra 700";
    case 23: return "Classic II";    case 26: return "Quadra 950";
    case 37: return "Mac LC II";     case 44: return "Mac IIvi";
    case 45: return "Mac IIvm/Performa 600";
    case 48: return "Mac IIvx";      case 49: return "Color Classic";
    default: return "unknown";
    }
}

// Low-memory globals cited in BASILISK_ROM_NOTES.md §4.3 (only the
// well-attested ones are named; the rest print raw)
const char* lmgName(uint16_t a) {
    switch (a) {
    case 0x1D4: return "VIA";        case 0x1D8: return "SCCRd";
    case 0x1DC: return "SCCWr";      case 0xCC0: return "ASCBase";
    case 0xCEC: return "VIA2/RBV";   case 0xCF8: return "ADBBase";
    case 0xDD8: return "VidMem(GetDevBase)";
    default:    return "";
    }
}

void printHeader() {
    // Mac ROM checksum = 32-bit sum of the big-endian words after the
    // checksum long itself (offset 4..end); Basilisk prints the stored
    // value without verifying (rom_patches.cpp:301) — we verify.
    uint32_t sum = 0;
    for (uint32_t o = 4; o + 1 < rom.size(); o += 2) sum += rd16(o);
    uint32_t stored = rd32(0);
    uint16_t version = rd16(8);

    std::printf("ROM: %zu KB\n", rom.size() / 1024);
    std::printf("Checksum    : $%08X (computed $%08X — %s)\n", stored, sum,
                stored == sum ? "OK" : "MISMATCH");
    std::printf("Reset SP/PC : $%08X / $%08X%s\n", rd32(0), rd32(4),
                version == 0x067C && (rd32(4) & 0xFFFF) == 0x2A
                    ? "  (base+$2A, canonical $067C entry)" : "");
    std::printf("Version     : $%04X — %s\n", version, familyName(version));
    std::printf("Sub version : $%04X\n", rd16(18));
    std::printf("Resource map: $%08X\n", rd32(26));
    std::printf("Trap table  : $%08X\n", rd32(34));
}

// Walk the ROM resource map (header +$1A). Entry: +8 next, +12 data,
// +16 type, +20 id, +23/+24 name. Size = long at data-8 & $FFFFFF.
struct RomRsrc { uint32_t data, type, size; int16_t id; std::string name; };

std::vector<RomRsrc> listResources() {
    std::vector<RomRsrc> out;
    uint32_t map = rd32(26);
    if (!map || map >= rom.size()) return out;
    for (uint32_t p = rd32(map); p && p < rom.size(); p = rd32(p + 8)) {
        RomRsrc r;
        r.data = rd32(p + 12);
        r.type = rd32(p + 16);
        r.id = int16_t(rd16(p + 20));
        r.size = r.data >= 8 ? rd32(r.data - 8) & 0xFFFFFF : 0;
        int len = p + 23 < rom.size() ? rom[p + 23] : 0;
        for (int i = 0; i < len && p + 24 + i < rom.size(); i++)
            r.name += char(rom[p + 24 + i]);
        out.push_back(std::move(r));
    }
    return out;
}

void printResources(bool full) {
    auto rs = listResources();
    if (full) {
        std::printf("\nROM resources (offset, type, id, size, name):\n");
        for (auto& r : rs)
            std::printf("  $%06X  %c%c%c%c %6d %7u  %s\n", r.data,
                        char(r.type >> 24), char(r.type >> 16),
                        char(r.type >> 8), char(r.type),
                        r.id, r.size, r.name.c_str());
    }
    int pack4 = 0;
    for (auto& r : rs) if (r.type == 0x5041434B && r.id == 4) pack4++;
    // rom_patches.cpp:1812-1815: two PACK 4 = the ROM carries a no-FPU
    // SANE; only one = the ROM's SANE requires an FPU
    std::printf("\n%zu ROM resources; PACK 4 x%d — %s\n", rs.size(), pack4,
                pack4 >= 2 ? "no-FPU SANE present in ROM"
                           : "ROM SANE requires an FPU (no-FPU path must "
                             "come from the System file)");
}

// Compressed trap table (header +$22): Toolbox $A800-$ABFF then OS
// $A000-$A3FF, 1024 entries each (find_rom_trap, rom_patches.cpp:123-155)
void walkTraps(const std::function<void(uint16_t, uint32_t, bool)>& fn) {
    uint32_t bp = rd32(34);
    if (!bp || bp >= rom.size()) return;
    uint32_t ofs = 0;
    for (int block = 0; block < 2; block++) {
        uint16_t trap = block == 0 ? 0xA800 : 0xA000;
        for (int i = 0; i < 0x400; i++, trap++) {
            bool unimpl = false;
            uint8_t b = rom[bp++];
            if (b == 0x80) {
                unimpl = true;
            } else if (b == 0xFF) {
                ofs = rd32(bp);
                bp += 4;
            } else if (b & 0x80) {
                ofs += uint32_t((b & 0x7F) << 1);
            } else {
                int add = ((b << 8) | rom[bp++]) << 1;
                if (!add) return;        // end marker
                ofs += uint32_t(add);
            }
            fn(trap, ofs, unimpl);
        }
    }
}

uint32_t findTrap(uint16_t want) {
    uint32_t found = 0;
    walkTraps([&](uint16_t t, uint32_t o, bool u) {
        if (t == want && !u) found = o;
    });
    return found;
}

void printTraps() {
    std::printf("\nROM trap table (implemented entries, trap -> ROM offset):\n");
    walkTraps([](uint16_t t, uint32_t o, bool u) {
        if (!u) std::printf("  $%04X -> $%06X\n", t, o);
    });
}

// UniversalInfo (BASILISK_ROM_NOTES.md §2): scan from $3000 for the long
// $DC000505, back up 16 to the info record, walk backwards to the
// Universal table (null-terminated self-relative long offsets).
void printUniversal() {
    uint16_t version = rd16(8);
    if (version != 0x067C) {
        std::printf("\n(no UniversalInfo — pre-$067C ROM)\n");
        return;
    }
    uint32_t sig = 0;
    for (uint32_t o = 0x3000; o + 4 < rom.size() && o < 0x5000; o += 2)
        if (rd32(o) == 0xDC000505) { sig = o; break; }
    if (!sig) { std::printf("\nUniversalInfo signature not found\n"); return; }
    uint32_t rec0 = sig - 16;
    uint32_t q = rec0;
    while (q > 0 && rd32(q) != rec0 - q) q -= 4;
    if (!q) { std::printf("\nUniversal table not found\n"); return; }

    // The decoder->low-mem copy table at fixed ROM offset $94A
    // (rom_patches.cpp:1216-1227): (decoderInfo index, LMG addr) word
    // pairs, $FFFF-terminated
    struct Pair { uint16_t idx, lmg; };
    std::vector<Pair> pairs;
    for (uint32_t p = 0x94A; rd16(p) != 0xFFFF && p < 0xA00; p += 4)
        pairs.push_back({ rd16(p), rd16(p + 2) });

    std::printf("\nUniversal table at $%06X (info, kind, hwCfg, FPU, rom85,"
                " defRSRC, addrMap, univROM, decoder, model):\n", q);
    std::vector<uint32_t> decoders;
    for (; rd32(q); q += 4) {
        uint32_t info = rd32(q) + q;     // self-relative
        uint8_t productKind = rom[info + 0x12];
        uint32_t decoder = info + rd32(info);   // self-relative +0
        char model[40];
        if (productKind < 250)           // $FD = resolved at runtime (the
            std::snprintf(model, sizeof model, "Gestalt %u = %s",   // V8
                          productKind + 6, modelName(productKind + 6));
        else                             // machines read $5FFFFFFC, §3.3)
            std::snprintf(model, sizeof model, "unset ($%02X)", productKind);
        std::printf("  $%06X %3u  $%04X %s $%04X  %u  $%08X $%08X $%06X  %s\n",
                    info, productKind, rd16(info + 0x10),
                    rd32(info + 0x10) & (1u << 28) ? "FPU" : " - ",
                    rd16(info + 0x14), rom[info + 0x16],
                    rd32(info + 0x18), rd32(info + 0x1C), decoder, model);
        if (std::find(decoders.begin(), decoders.end(), decoder)
            == decoders.end()) decoders.push_back(decoder);
    }
    for (uint32_t decoder : decoders) {
        std::printf("\n  DecoderInfo $%06X — hardware bases the ROM copies"
                    " to low memory\n  (via the pair table at ROM+$94A):\n",
                    decoder);
        for (auto& pr : pairs) {
            const char* n = lmgName(pr.lmg);
            std::printf("    decoder[%2u] = $%08X -> LMG $%03X%s%s\n",
                        pr.idx, rd32(decoder + pr.idx * 4u), pr.lmg,
                        *n ? "  " : "", n);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    std::vector<uint16_t> traps;
    bool allTraps = false, resources = false, all = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--trap") && i + 1 < argc)
            traps.push_back(uint16_t(std::strtoul(argv[++i], nullptr, 16)));
        else if (!std::strcmp(argv[i], "--traps")) allTraps = true;
        else if (!std::strcmp(argv[i], "--resources")) resources = true;
        else if (!std::strcmp(argv[i], "--all")) all = true;
        else path = argv[i];
    }
    if (path.empty()) {
        std::printf("usage: rominfo <rom> [--trap XXXX]... [--traps] "
                    "[--resources] [--all]\n");
        return 1;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("rominfo: cannot open %s\n", path.c_str()); return 1; }
    rom.assign(std::istreambuf_iterator<char>(f), {});
    if (rom.size() < 0x100) { std::printf("rominfo: not a ROM\n"); return 1; }

    printHeader();
    printResources(resources || all);
    printUniversal();
    for (uint16_t t : traps) {
        uint32_t o = findTrap(t);
        if (o) std::printf("\ntrap $%04X -> ROM offset $%06X\n", t, o);
        else   std::printf("\ntrap $%04X: unimplemented in ROM\n", t);
    }
    if (allTraps || all) printTraps();
    return 0;
}
