// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The ROM catalogue: every user-provided dump POM68K opens — machine ROMs,
// microcontroller firmware and declaration ROMs — with its identity, where
// it is installed, which profiles use it and what happens without it. The
// ROM Status panel, the downloader (RomFetch), the importer (RomImport)
// and the archive builder (tools/build_rom_archive.py) all read this table.
//
// It is not a second registry. Three owners already hold parts of it and a
// check holds each one to it:
//   * `kMachineProfiles` (MachineCatalog.h) decides which ROM a profile
//     boots: `validRomCatalog()` below fails the BUILD unless every profile
//     has exactly one required machine ROM and the ROM its menu entry
//     looks for (`romCrc32`, or `romPath` for the Plus ROMs that dispatch on
//     size alone) is catalogued.
//   * `assets.lock` is the identity list of the private asset set:
//     `rom_catalog_test` requires its firmware / machine-rom /
//     machine-rom-variant / declaration-rom rows and this table to agree
//     row for row — label, size, SHA-256, profiles, and a path this table
//     probes.
//   * `firmware::kManifest` (FirmwareManifest.h), the strict-LLE subset:
//     same test, same path, size and digest.
//
// Row fields, in declaration order (designated initialisers, one row per
// brace block — tools/build_rom_archive.py reads the table with a regex,
// so keep string fields as single literals and no braces inside a row):
//
//   id        stable short name, used by the GUI and in log lines
//   path      canonical install path: where a download or an import puts
//             the dump, and its member name in the POM68K archive. Always a
//             path the runtime probes (README § Additional firmware).
//   alsoAt    other paths something opens for the same bytes, '|'-joined:
//             a firmware fallback, a gate's MAME-layout path. A machine
//             ROM's profiles' `romPath`s are probed too and not repeated.
//   checksum  machine ROMs only: the big-endian long at offset 0, the value
//             MachineFactory dispatches on and `findRomBySignature` looks
//             for in file names
//   retroBios path of a copy in RetroBIOS at `kRetroBiosCommit`, or nullptr
//             when only POM68K's own archive serves the dump. Each mapping
//             was verified byte for byte (SHA-256) on 2026-11-20;
//             `retroBiosMember` names the zip member when the file is a
//             MAME romset.

#pragma once

#include "MachineCatalog.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace pom68k::roms {

enum class RomKind : std::uint8_t {
    MachineRom,      // the system ROM a profile boots
    Firmware,        // a microcontroller's mask ROM (Egret, Cuda, PIC, PMU…)
    DeclarationRom,  // a NuBus / built-in video card's declaration ROM
};

// What its absence costs. The GUI shows this; the downloader fetches every
// level alike.
enum class RomNeed : std::uint8_t {
    Required,    // the profile refuses to start, or stalls in its boot
    Substitute,  // a documented non-conformant substitute runs instead
                 // (LLE_VS_HLE § 2); strict LLE refuses such a session
    Optional,    // nothing breaks: a feature, or another revision of a ROM
};

struct RomDump {
    const char* id;
    const char* label;           // = the assets.lock label
    RomKind kind;
    RomNeed need;
    const char* path;            // canonical install path, "roms/…"
    const char* alsoAt;          // '|'-joined, "" when none
    std::uint32_t size;
    const char* sha256;          // lowercase hexadecimal
    std::uint32_t crc32;         // IEEE CRC-32 of the whole file (Crc32.h)
    const char* checksum;        // machine ROM header long, nullptr otherwise
    const char* profiles;        // ','-joined catalogue slugs
    const char* absent;          // what the user loses without it
    bool rootRelative;           // also probed as <firmware root>/<path minus roms/>
    const char* retroBios;       // RetroBIOS path at kRetroBiosCommit, or nullptr
    const char* retroBiosMember; // member of that zip, nullptr when loose
};

inline constexpr RomDump kRomDumps[] = {
    // ── Machine ROMs ─────────────────────────────────────────────────────
    {.id = "rom-28ba61ce", .label = "Macintosh 128K 28BA61CE", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/64KB ROMs/1984-01 - 28BA61CE - Macintosh 128.ROM", .alsoAt = "", .size = 65536,
     .sha256 = "13fe8312cf6167a2bb4351297b48cc1ee29c523b788e58270434742bfeda864c", .crc32 = 0x6D0C8A28, .checksum = "28BA61CE",
     .profiles = "mac128k", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/mac128k.rom", .retroBiosMember = nullptr},
    {.id = "rom-28ba4e50", .label = "Macintosh 512K 28BA4E50", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/64KB ROMs/1984-10 - 28BA4E50 - Macintosh 512K.ROM", .alsoAt = "", .size = 65536,
     .sha256 = "fe6a1ceff5b3eefe32f20efea967cdf8cd4cada291ede040600e7f6c9e2dfc0e", .crc32 = 0xCF759E0D, .checksum = "28BA4E50",
     .profiles = "mac512k", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/mac512k.rom", .retroBiosMember = nullptr},
    {.id = "rom-4d1f8172", .label = "Macintosh Plus v3 4D1F8172", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/macplus.rom", .alsoAt = "", .size = 131072,
     .sha256 = "dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7", .crc32 = 0xB2102E8E, .checksum = "4D1F8172",
     .profiles = "plus,mac512ke", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/macplus.rom", .retroBiosMember = nullptr},
    {.id = "rom-b2e362a8", .label = "Macintosh SE B2E362A8", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM", .alsoAt = "", .size = 262144,
     .sha256 = "0dea05180e66fddb5f5577c89418de31b97e2d9dc6affe84871b031df8245487", .crc32 = 0x0F7FF80C, .checksum = "B2E362A8",
     .profiles = "se", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/macse.zip", .retroBiosMember = "macse.rom"},
    {.id = "rom-b306e171", .label = "Macintosh SE FDHD B306E171", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM", .alsoAt = "", .size = 262144,
     .sha256 = "bb0cb4786e2e004b701dda9bec475598bc82a4f27eb7b11e6b78dfcee1434f71", .crc32 = 0xF530CB10, .checksum = "B306E171",
     .profiles = "sefdhd", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-a49f9914", .label = "Macintosh Classic A49F9914", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom", .alsoAt = "", .size = 524288,
     .sha256 = "c1c47260bacac2473e21849925fbfdf48e5ab584aaef7c6d54569d0cb6b41cce", .crc32 = 0x510D7D38, .checksum = "A49F9914",
     .profiles = "classic", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/macclasc.zip", .retroBiosMember = "341-0813__=c=1983-90_apple__japan__910d_d.27c4096_be.ue1"},
    {.id = "rom-9779d2c4", .label = "Macintosh II 9779D2C4", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM", .alsoAt = "", .size = 262144,
     .sha256 = "97f2a22bdb8972bfcc1f16aff1ebbe157887c26787a1c81747a9842fa7b97a06", .crc32 = 0x4DF6D054, .checksum = "9779D2C4",
     .profiles = "macii", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh II/MacII.ROM", .retroBiosMember = nullptr},
    {.id = "rom-97851db6", .label = "Macintosh II 97851DB6", .kind = RomKind::MachineRom, .need = RomNeed::Optional,
     .path = "roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM", .alsoAt = "", .size = 262144,
     .sha256 = "cc6d754cfa7841644971718ada1121bc5f94ff954918f502a75abb0e6fd90540", .crc32 = 0x8C8B9D03, .checksum = "97851DB6",
     .profiles = "macii", .absent = "The first Macintosh II ROM revision; 9779D2C4 boots the same profile.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-97221136", .label = "Macintosh II FDHD 97221136", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM", .alsoAt = "", .size = 262144,
     .sha256 = "79fae48e2d5cfde68520e46616503963f8c16430903f410514b62c1379af20cb", .crc32 = 0xCE3B966F, .checksum = "97221136",
     .profiles = "maciifdhd,iix,iicx,se30", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh II/MacIIx.ROM", .retroBiosMember = nullptr},
    {.id = "rom-4147dd77", .label = "Macintosh IIfx 4147DD77", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "f197eb70528f105e74059092a1a820f48ab0f90f39c5b3ec02041d85e6d5a5c5", .crc32 = 0xEF441BBD, .checksum = "4147DD77",
     .profiles = "iifx", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-368cadfe", .label = "Macintosh IIci 368CADFE", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "0419966cfd7445680c0dd6ca78e8b1f4bc212e75caf56f4a5f523afa576ad3f2", .crc32 = 0x46ADBF74, .checksum = "368CADFE",
     .profiles = "iici", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-36b7fb6c", .label = "Macintosh IIsi 36B7FB6C", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1990-10 - 36B7FB6C - Mac IIsi.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "b8b04d1478a609de89dd62e51165cc7ca5757f6f881e9770621d09af82890feb", .crc32 = 0xF304D973, .checksum = "36B7FB6C",
     .profiles = "iisi", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-350eacf0", .label = "Macintosh LC 350EACF0", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1990-10 - 350EACF0 - Mac LC.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "129391cc72f84c2b321709cad8281e30a45e50b3cf6e7afe7434c4d32c7b9d5b", .crc32 = 0x71681726, .checksum = "350EACF0",
     .profiles = "lc", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-35c28f5f", .label = "Macintosh LC II 35C28F5F", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "18c3de07ca15cf66dfbaa5984aca34b48a2d7b4d0d8c7656f79533899ccec0a0", .crc32 = 0xA92145B3, .checksum = "35C28F5F",
     .profiles = "lcii", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-3193670e", .label = "Macintosh Classic II 3193670E", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/512KB ROMs/1991-10 - 3193670E - Classic II.ROM", .alsoAt = "", .size = 524288,
     .sha256 = "5e27acb45173a87e9dfd2c2a411f7cf521ff3cc8bfab083963cf4757d6ca289c", .crc32 = 0x96D2E1FD, .checksum = "3193670E",
     .profiles = "classic2", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-ecd99dc0", .label = "Macintosh Color Classic ECD99DC0", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-02 - ECD99DC0 - Color Classic.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "645a193a7817988f54ef0ec90b6ad882b698457600a41ec8d280bee94288c534", .crc32 = 0xC84C3AA5, .checksum = "ECD99DC0",
     .profiles = "cclassic", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-eaf1678d", .label = "Macintosh TV EAF1678D", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1994 - EAF1678D - Macintosh TV.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "2b4fbc3dae98cfd6cd58cd5edf6d7e629c93c46ca233e14ee31dcb9a9f7698e5", .crc32 = 0x0644F05B, .checksum = "EAF1678D",
     .profiles = "mactv", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-ecbbc41c", .label = "Macintosh LC III ECBBC41C", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "b2a411c1b24c92af8f6b58f745e52e0545cc0d4dcdc1e15a90e0a78937cde61e", .crc32 = 0xE578F5F3, .checksum = "ECBBC41C",
     .profiles = "lc3,lc3plus", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/maclc3.zip", .retroBiosMember = "ecbbc41c.rom"},
    {.id = "rom-ec904829", .label = "Macintosh LC III EC904829", .kind = RomKind::MachineRom, .need = RomNeed::Optional,
     .path = "roms/1MB ROMs/1993-02 - EC904829 - Mac LC III (older).ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "00e3b10758b01ba998bb3aba9366c29b15f52c5b0667658ec82ee674dde9b4e2", .crc32 = 0xD326EB69, .checksum = "EC904829",
     .profiles = "lc3,lc3plus", .absent = "An earlier LC III ROM revision; ECBBC41C boots the same profiles.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-ede66cbd", .label = "Macintosh AIO EDE66CBD", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-10 - EDE66CBD - Color Classic II & LC 550 & Performa 275,550,560 & Macintosh TV.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "054cb8b0352c32ddb9bf786338fafb32e5b488556b3e2efcb84bf4252c9692a2", .crc32 = 0xA893CB0F, .checksum = "EDE66CBD",
     .profiles = "lc520,lc550,cclassic2", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-4957eb49", .label = "Macintosh IIvx and IIvi 4957EB49", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1992-10 - 4957EB49 - Mac IIvx & IIvi or Performa 600.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "4ca9c4a7c782ae50ae09ca95b6af1e7c08ce3f8ddfea1a006d3c4ccb45ac51af", .crc32 = 0x61BE06E5, .checksum = "4957EB49",
     .profiles = "iivx,iivi", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-ff7439ee", .label = "LC 475, LC 575 and Quadra 605 FF7439EE", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
     .alsoAt = "roms/mame/macqd605/ff7439ee.bin", .size = 1048576,
     .sha256 = "9c758473a523ffe40ed0d20d27ff37216ed2df064335d7da815a89cff5607b34", .crc32 = 0xB8514689, .checksum = "FF7439EE",
     .profiles = "lc475,lc575,q605", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-f1a6f343", .label = "Centris and Quadra F1A6F343", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "7d9cb8c3bae0e26171adf9f24bedf10bf6371ae3c9bdbcdfd3b94e5350099fb6", .crc32 = 0x3318A935, .checksum = "F1A6F343",
     .profiles = "c610,c650,q610,q650,q800", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-f1acad13", .label = "Centris and Quadra F1ACAD13", .kind = RomKind::MachineRom, .need = RomNeed::Optional,
     .path = "roms/1MB ROMs/1993-02 - F1ACAD13 - Quadra, Centris 610,650,800.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca", .crc32 = 0x4E70E3C0, .checksum = "F1ACAD13",
     .profiles = "c610,c650,q610,q650,q800", .absent = "A later revision of the same board ROM; F1A6F343 boots the same profiles.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-420dbff3", .label = "Quadra 700 and 900 420DBFF3", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "c2093476e9c9a7d76973910a91fbfba23ca71163b84eb2623adf8608a6b03ed2", .crc32 = 0x88EA2081, .checksum = "420DBFF3",
     .profiles = "q700,q900,pb140,pb170,pb145,pb145b", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-3dc27823", .label = "Quadra 950 3DC27823", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "7c11d2fce3217df8c70aaa07faea8b9420db561e6414bd4b557a2e72141374a4", .crc32 = 0x0E11206E, .checksum = "3DC27823",
     .profiles = "q950", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-06684214", .label = "Quadra 630 06684214", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM", .alsoAt = "roms/mame/macqd630/06684214.bin", .size = 1048576,
     .sha256 = "c1098a03786667c8bf888792590ce53b47f65bf44a75e11c24718983e1d25af8", .crc32 = 0x1735E7A5, .checksum = "06684214",
     .profiles = "q630", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/mac.rom", .retroBiosMember = nullptr},
    {.id = "rom-064dc91d", .label = "Macintosh LC 580 064DC91D", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1995-04 - 064DC91D - LC, Performa 580 & Performa 588.ROM", .alsoAt = "roms/mame/maclc580/064dc91d.bin", .size = 1048576,
     .sha256 = "bcf86532d70913c1d4fc487416eedbe510fb11ce3443e33c350ceeb3873019ca", .crc32 = 0x59E6960F, .checksum = "064DC91D",
     .profiles = "lc580", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-ecfa989b", .label = "PowerBook Duo ECFA989B", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "a5cbde0f1fe905f05592e181685df79df4a4fd3d4bfb7fe3df2e45ac8601b59a", .crc32 = 0xB86ED854, .checksum = "ECFA989B",
     .profiles = "duo210,duo230,duo250", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-0024d346", .label = "PowerBook Duo 270c 0024D346", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1993-10 - 0024D346 - Powerbook Duo 270C.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "5a931e02f1de0570c6c5995c66b8f9a89c7be31fd6e17883eca783df8ea5feaf", .crc32 = 0x94C4D04A, .checksum = "0024D346",
     .profiles = "duo270c", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-015621d7", .label = "PowerBook Duo 280/280c 015621D7", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1994-04 - 015621D7 - Powerbook Duo 280 & 280c.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "2f29a570bb766f2d5872fe7b39303a75a8df4df121accb6010e455cecbe18504", .crc32 = 0x568D28EB, .checksum = "015621D7",
     .profiles = "duo280,duo280c", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-fda22562", .label = "PowerBook 150 FDA22562", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1994-07 - FDA22562 - Powerbook 150.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "2a284933aa37f69e641f68c88fba484241cbaa2b847cd55e5ecf546bece86f4f", .crc32 = 0x7E283B7C, .checksum = "FDA22562",
     .profiles = "pb150", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-e33b2724", .label = "PowerBook 160 E33B2724", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/1MB ROMs/1992-10 - E33B2724 - Powerbook 160 & 165 & 165c & 180 & 180c.ROM", .alsoAt = "", .size = 1048576,
     .sha256 = "d43eb16ce4719d96c45be61737b497918c4ebe27640a7aafa058b7706971bd1c", .crc32 = 0x536C60F4, .checksum = "E33B2724",
     .profiles = "pb160,pb165,pb180,pb180c,pb165c", .absent = "These machines cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-96ca3846", .label = "Macintosh Portable 96CA3846", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1989-02 - 96CA3846 - Mac Portable.ROM", .alsoAt = "", .size = 262144,
     .sha256 = "3de29198fc61859ed6146dae0b84cec6c980045f4e3efd64a683aab4db8e04a4", .crc32 = 0x497348F8, .checksum = "96CA3846",
     .profiles = "portable", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "rom-96645f9c", .label = "PowerBook 100 96645F9C", .kind = RomKind::MachineRom, .need = RomNeed::Required,
     .path = "roms/256KB ROMs/1991-10 - 96645F9C - PowerBook 100.ROM", .alsoAt = "", .size = 262144,
     .sha256 = "62558d5faae58e1d6642a8b046f5ec432fe99073e018ac46597ec024b773b6a8", .crc32 = 0x29AC7EE9, .checksum = "96645F9C",
     .profiles = "pb100", .absent = "This machine cannot start.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},

    // ── Microcontroller firmware ─────────────────────────────────────────
    // Egret/Cuda candidate order per board: V8Memory, SonoraMemory,
    // VaspMemory, RbvMemory, Q605Memory, Q630Memory, Q700Memory.
    {.id = "cuda-341s0788", .label = "Cuda 341s0788", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/cuda/341s0788.bin", .alsoAt = "", .size = 4352,
     .sha256 = "4bad3d4f18425f96bbbdde55eb0413603dba4910d172ffed7ed4d7c1a34358cc", .crc32 = 0xDF6E1B43, .checksum = nullptr,
     .profiles = "lc475,lc575,q605", .absent = "ADB, PRAM and the clock run on the non-conformant Cuda HLE.", .rootRelative = false,
     .retroBios = "bios/Arcade/MAME/cuda.zip", .retroBiosMember = "341s0788.bin"},
    {.id = "cuda-341s0060", .label = "Cuda 341s0060", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/cuda/341s0060.bin", .alsoAt = "", .size = 4352,
     .sha256 = "607890e9ed816be6ca2210620f649ef63816e78d35b4e1e23858b9c8478a2c16", .crc32 = 0x0F5E7B4A, .checksum = nullptr,
     .profiles = "q630,lc580", .absent = "ADB, PRAM and the clock run on the non-conformant Cuda HLE.", .rootRelative = false,
     .retroBios = "bios/Arcade/MAME/cuda.zip", .retroBiosMember = "341s0060.bin"},
    {.id = "cuda-341s0417", .label = "Cuda 341s0417", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/cuda/341s0417.bin", .alsoAt = "", .size = 4352,
     .sha256 = "47040b87387aaa21eb64c36a9a2f3bd0296e4af2f96a771cf7de7ea4e057ba00", .crc32 = 0x571F24C9, .checksum = nullptr,
     .profiles = "cclassic", .absent = "The Color Classic falls back to Cuda 341s0788, then to the non-conformant HLE.", .rootRelative = false,
     .retroBios = "bios/Arcade/MAME/cuda.zip", .retroBiosMember = "341s0417.bin"},
    {.id = "cuda-341s0789", .label = "Cuda 341s0789", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/cuda/341s0789.bin", .alsoAt = "", .size = 4352,
     .sha256 = "14fb44c7f0fe03fab5abcb3faee300a9dad1909563cfbc010b1393e2d2582e98", .crc32 = 0x682D2ACE, .checksum = nullptr,
     .profiles = "mactv", .absent = "The Macintosh TV falls back to Cuda 341s0060 or 341s0788, then to the non-conformant HLE.", .rootRelative = false,
     .retroBios = "bios/Arcade/MAME/cuda.zip", .retroBiosMember = "341s0789.bin"},
    {.id = "adb-342s0440b", .label = "ADB PIC1654S 342s0440-b", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/adbmodem/342s0440-b.bin", .alsoAt = "", .size = 1024,
     .sha256 = "22466ae8e4c11509dcc862e85c65239efcc780d23eda9fd4c69bb8041cb1318e", .crc32 = 0xCFFB33EB, .checksum = nullptr,
     .profiles = "c610,c650,q610,q650,q800,q700", .absent = "ADB runs on the non-conformant transceiver byte model.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh II/adbmodem.zip", .retroBiosMember = "342s0440-b.bin"},
    {.id = "egret-341s0850", .label = "Egret 341s0850", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/egret/341s0850.bin", .alsoAt = "", .size = 4352,
     .sha256 = "3287874a6eec75a7a1dae09faaeb6ccb73e29b07e9429663a326f1a43e5390fa", .crc32 = 0x4906ECD0, .checksum = nullptr,
     .profiles = "lc,lcii,classic2", .absent = "ADB, PRAM and the clock run on the non-conformant Egret HLE.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/egret.zip", .retroBiosMember = "341s0850.bin"},
    {.id = "egret-341s0851", .label = "Egret 341s0851", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/egret/341s0851.bin", .alsoAt = "", .size = 4352,
     .sha256 = "77f5fb2baaca55a5921423b041e60257a864d79d734872991fa607fa33784d88", .crc32 = 0xEA9EA6E4, .checksum = nullptr,
     .profiles = "q900,q950", .absent = "Falls back to Egret 341s0850, then to the non-conformant Egret HLE.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/egret.zip", .retroBiosMember = "341s0851.bin"},
    {.id = "egret-344s0100", .label = "Egret 344s0100", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
     .path = "roms/egret/344s0100.bin", .alsoAt = "", .size = 4352,
     .sha256 = "e02ae917464691e62b957785f534af9cf57d8fead1dcd3106a5393241aad6463", .crc32 = 0x59E2B6B6, .checksum = nullptr,
     .profiles = "iisi", .absent = "The IIsi falls back to Egret 341s0851 or 341s0850, then to the non-conformant HLE.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh/egret.zip", .retroBiosMember = "344s0100.bin"},
    {.id = "pge-boot", .label = "PG&E boot", .kind = RomKind::Firmware, .need = RomNeed::Required,
     .path = "roms/pge/pge_boot.bin", .alsoAt = "", .size = 512,
     .sha256 = "ab6348f7f71d97162d7e17813c46ff8e1d2213635099166809df3214725eb29b", .crc32 = 0x62D4DFED, .checksum = nullptr,
     .profiles = "duo230,duo210,duo250,duo270c,duo280,duo280c,pb150", .absent = "No power manager: the ROM stalls at the PMU handshake.", .rootRelative = false,
     .retroBios = "bios/Arcade/MAME/m68hc05pge.zip", .retroBiosMember = "pge_boot.bin"},
    {.id = "pge-battery-id", .label = "PG&E Duo battery ID", .kind = RomKind::Firmware, .need = RomNeed::Optional,
     .path = "roms/pge/duobatid.bin", .alsoAt = "", .size = 8,
     .sha256 = "8c9bb6456a2c39023abea719b0a7294fb27ceb3fa72db950d6dd7819fbf23995", .crc32 = 0x7545C341, .checksum = nullptr,
     .profiles = "duo230,duo210,duo250,duo270c,duo280,duo280c,pb150", .absent = "The DS2400 battery serial number reads as absent.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "pmu-pb140", .label = "M50753 PB140/170", .kind = RomKind::Firmware, .need = RomNeed::Required,
     .path = "roms/pmu/pb140.bin", .alsoAt = "roms/archive/macroms/pmu/Powerbook-140-145-170__PMU.bin", .size = 6144,
     .sha256 = "7b91c8a2591b2644bc14c35cd00656ba722c356c79717ed0ef176e5e89ccbcc6", .crc32 = 0x1A32B5E5, .checksum = nullptr,
     .profiles = "pb140,pb170,pb145,pb145b", .absent = "These PowerBooks refuse to start.", .rootRelative = true,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "pmu-pb160", .label = "M50753 PB160", .kind = RomKind::Firmware, .need = RomNeed::Required,
     .path = "roms/pmu/pb160.bin", .alsoAt = "roms/archive/macroms/pmu/Powerbook-160-165-180__Powerbook 160 PMU.bin", .size = 6144,
     .sha256 = "125a8680ab3faa8cc2fed666463ea7741045d8a4b893d8d7b08f4f369790e465", .crc32 = 0xF2DF696C, .checksum = nullptr,
     .profiles = "pb160,pb165,pb180,pb180c,pb165c", .absent = "These PowerBooks refuse to start.", .rootRelative = true,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "pmu-portable", .label = "M50753 Portable and PB100", .kind = RomKind::Firmware, .need = RomNeed::Required,
     .path = "roms/pmu/portable.bin",
     .alsoAt = "roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin|roms/archive/macroms/pmu/Powerbook-100__PMU.bin", .size = 6144,
     .sha256 = "0f9911ab33a62a2831d683d9648059bd7ad59c455b59dcd214c610ce16eebd57", .crc32 = 0x01DAE148, .checksum = nullptr,
     .profiles = "portable,pb100", .absent = "These machines refuse to start.", .rootRelative = true,
     .retroBios = nullptr, .retroBiosMember = nullptr},

    // ── Declaration ROMs ─────────────────────────────────────────────────
    {.id = "decl-pb180c", .label = "PowerBook 180c video", .kind = RomKind::DeclarationRom, .need = RomNeed::Required,
     .path = "roms/pbvideo/pb180c.bin", .alsoAt = "roms/archive/macroms/68k/1M/Powerbook 180c video card.bin", .size = 32768,
     .sha256 = "11b9f7c549400ab7af727e8071d5fe6bd6f34c79087f29d073cc0fb6850c88c8", .crc32 = 0x810C75AD, .checksum = nullptr,
     .profiles = "pb180c,pb165c", .absent = "The colour PowerBooks refuse to start.", .rootRelative = true,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "decl-toby", .label = "Toby video 342-0008-a", .kind = RomKind::DeclarationRom, .need = RomNeed::Substitute,
     .path = "roms/342-0008-a.bin", .alsoAt = "roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin", .size = 4096,
     .sha256 = "02261d5b8739352ead945de0fdccd3a364890fb67d75eb5449e4bd19e18c06fc", .crc32 = 0xBF50850D, .checksum = nullptr,
     .profiles = "macii,iifx", .absent = "A synthetic declaration ROM runs: System 6 and 7.0 boot, System 7.5.5 stays at 'Welcome to Macintosh'.", .rootRelative = false,
     .retroBios = "bios/Arcade/Arcade/nb_m2vc.zip", .retroBiosMember = "342-0008-a.bin"},
    {.id = "decl-se30", .label = "Macintosh SE/30 video UK6", .kind = RomKind::DeclarationRom, .need = RomNeed::Required,
     .path = "roms/se30/se30vrom.uk6", .alsoAt = "", .size = 8192,
     .sha256 = "8af892fd7fff89c2151bb3027f3dc61e531f24e4adb0e3face95c90daece4409", .crc32 = 0xB74C3463, .checksum = nullptr,
     .profiles = "se30", .absent = "The SE/30 has no built-in video.", .rootRelative = false,
     .retroBios = nullptr, .retroBiosMember = nullptr},
    {.id = "decl-824", .label = "Display Card 8-24 341-0868", .kind = RomKind::DeclarationRom, .need = RomNeed::Optional,
     .path = "roms/341-0868.bin", .alsoAt = "roms/archive/macroms/Misc/Video cards/Apple Macintosh Display Card 8-24/Trident 8-24/341-0868.BIN", .size = 32768,
     .sha256 = "e2e763a6b432c9196f619a9f90107726ab1a84a1d54242fe5f5182bf3c97b238", .crc32 = 0x57F925FA, .checksum = nullptr,
     .profiles = "macii,maciifdhd,iix,iicx", .absent = "The Display Card 8-24 cannot be fitted.", .rootRelative = false,
     .retroBios = "bios/Apple/Macintosh II/nb_mdc824.zip", .retroBiosMember = "3410868.bin"},
};

inline constexpr std::size_t kRomDumpCount = std::size(kRomDumps);

// ── Lookups (no I/O) ─────────────────────────────────────────────────────

constexpr const RomDump* findRomDump(std::string_view id) {
    for (const RomDump& d : kRomDumps)
        if (id == d.id) return &d;
    return nullptr;
}

// True when `list` (a ','- or '|'-joined field) contains `item` exactly.
constexpr bool listHas(std::string_view list, std::string_view item, char sep) {
    while (!list.empty()) {
        const std::size_t cut = list.find(sep);
        const std::string_view head = list.substr(0, cut);
        if (head == item) return true;
        if (cut == std::string_view::npos) break;
        list.remove_prefix(cut + 1);
    }
    return false;
}

constexpr bool romUsedBy(const RomDump& d, std::string_view slug) {
    return listHas(d.profiles, slug, ',');
}

// Every dump `slug` uses, in catalogue order (machine ROMs first).
inline std::vector<const RomDump*> romDumpsForProfile(std::string_view slug) {
    std::vector<const RomDump*> out;
    for (const RomDump& d : kRomDumps)
        if (romUsedBy(d, slug)) out.push_back(&d);
    return out;
}

constexpr bool romHasRetroBiosCopy(const RomDump& d) { return d.retroBios != nullptr; }

constexpr const char* romKindName(RomKind k) {
    switch (k) {
    case RomKind::MachineRom: return "machine ROM";
    case RomKind::Firmware: return "firmware";
    case RomKind::DeclarationRom: return "declaration ROM";
    }
    return "?";
}

constexpr const char* romNeedName(RomNeed n) {
    switch (n) {
    case RomNeed::Required: return "required";
    case RomNeed::Substitute: return "substitute without it";
    case RomNeed::Optional: return "optional";
    }
    return "?";
}

// ── Build-time consistency ───────────────────────────────────────────────

// Written with raw character loops that stop at the first difference, and
// kept to the checks that tie the table to kMachineProfiles: it runs in
// every translation unit under each compiler's constexpr step budget (MSVC's
// default is 100 000). Pairwise uniqueness of ids, paths and digests is
// `rom_catalog_test`'s.
namespace detail {
constexpr bool same(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
constexpr bool isLowerHex64(const char* s) {
    int n = 0;
    for (; s && *s; ++s, ++n)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return false;
    return n == 64;
}
constexpr bool startsWithRoms(const char* s) {
    const char* p = "roms/";
    while (*p && *s == *p) { ++s; ++p; }
    return *p == 0;
}
// Index of the profile whose slug is s[0..n), or -1.
constexpr int profileIndex(const char* s, std::size_t n) {
    for (std::size_t i = 0; i < kMachineProfileCount; ++i) {
        const char* slug = kMachineProfiles[i].slug;
        std::size_t k = 0;
        while (k < n && slug[k] && slug[k] == s[k]) ++k;
        if (k == n && slug[k] == 0) return int(i);
    }
    return -1;
}
} // namespace detail

consteval bool validRomCatalog() {
    int required[kMachineProfileCount] = {};
    for (std::size_t i = 0; i < kRomDumpCount; ++i) {
        const RomDump& a = kRomDumps[i];
        if (!a.id || !a.label || !a.path || !a.alsoAt || !a.profiles || !a.absent)
            return false;
        if (!detail::startsWithRoms(a.path) || !detail::isLowerHex64(a.sha256) || a.size == 0)
            return false;
        if ((a.kind == RomKind::MachineRom) != (a.checksum != nullptr)) return false;
        if ((a.retroBios == nullptr) && (a.retroBiosMember != nullptr)) return false;
        // Every slug is a real profile; count required machine ROMs per profile.
        const char* s = a.profiles;
        if (!*s) return false;
        while (*s) {
            std::size_t n = 0;
            while (s[n] && s[n] != ',') ++n;
            const int at = detail::profileIndex(s, n);
            if (at < 0) return false;
            if (a.kind == RomKind::MachineRom && a.need == RomNeed::Required) ++required[at];
            s += n;
            if (*s == ',') ++s;
        }
    }
    // Every profile: exactly one REQUIRED machine ROM lists it, and the ROM
    // its menu entry looks for (`romCrc32` by file-name signature, or
    // `romPath` for the Plus ROMs) is a catalogued machine ROM. The two may
    // differ: the LC 580 boots its own 064DC91D by default, while its menu
    // entry looks for the Quadra 630's 06684214, which MachineFactory also
    // dispatches to it.
    for (std::size_t i = 0; i < kMachineProfileCount; ++i) {
        const MachineProfile& p = kMachineProfiles[i];
        if (required[i] != 1) return false;
        bool menuRomCatalogued = false;
        for (const RomDump& d : kRomDumps) {
            if (d.kind != RomKind::MachineRom || menuRomCatalogued) continue;
            menuRomCatalogued = p.romCrc32 ? detail::same(p.romCrc32, d.checksum)
                                           : detail::same(p.romPath, d.path);
        }
        if (!menuRomCatalogued) return false;
    }
    return true;
}

static_assert(validRomCatalog(),
              "ROM catalogue disagrees with itself or with kMachineProfiles");

} // namespace pom68k::roms
