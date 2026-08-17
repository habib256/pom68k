// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The product catalogue. A Macintosh profile is declared here once; the GUI,
// process dispatch, save-state identity, gates and generated documentation
// consume this table. Append profiles and snapshot ids, never renumber them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace pom68k {

enum class CpuFamily : std::uint8_t { M68000, M68020, M68030, M68040 };

// A platform is one board implementation (memory map + CPU wrapper + device
// graph), not one marketing name. This is the bounded reuse unit through which
// the catalogue grows towards every 68k Macintosh.
enum class PlatformKind : std::uint8_t {
    Compact, Glue, Oss, V8, Rbv, Sonora, Vasp, MemcJr, DjMemc, Spike, F108, Msc
};

// GUI/process runner. Several marketing profiles deliberately share one kind.
// Values are explicit so changing declaration order cannot change a relaunch.
enum class MachineKind : std::uint8_t {
    Plus = 0, Se, SeFdhd, MacClassic, MacII, IIfx, Lc, LcII, ClassicII,
    ColorClassic, MacTv, IIsi, IIci, Lc3, Aio, Vasp, Centris, Q700, Q630,
    Quadra, Duo
};

// Stored in snapshot headers: these numeric values are a file format. Append,
// never insert or renumber. There is one id per product profile, not per board.
enum class SnapMachine : std::uint32_t {
    LcII = 1,
    Lc = 2, ClassicII = 3, ColorClassic = 4, MacTv = 5,
    Lc3 = 6, Lc3Plus = 7, Lc520 = 8, Lc550 = 9, CClassic2 = 10,
    IIvx = 11, IIvi = 12,
    IIsi = 13, IIci = 14,
    Q605 = 15, Lc475 = 16, Lc575 = 17,
    Centris610 = 18, Centris650 = 19,
    Quadra610 = 20, Quadra650 = 21, Quadra800 = 22,
    Q700 = 23, Q630 = 24, Lc580 = 25,
    MacII = 26, IIx = 27, IIcx = 28,
    Plus = 29, SE = 30, SEFDHD = 31, Classic = 32,
    SE30 = 33, IIfx = 34, Quadra900 = 35, Quadra950 = 36,
    Duo230 = 37,
};

struct MachineProfile {
    const char* group;          // menu heading / board family description
    const char* label;          // user-visible marketing name
    const char* slug;           // stable host-side file/result identifier
    MachineKind kind;           // GUI/process runner
    PlatformKind platform;      // memory map + device graph
    CpuFamily cpu;
    SnapMachine snapshot;       // stable save-state identity
    const char* romPath;        // canonical convenience path
    const char* romCrc32;       // accepted ROM identity, hexadecimal text
    const char* variantKey;     // process configuration key, or nullptr
    const char* variantValue;   // exact value within the runner
    bool defaultVariant;
};

inline constexpr MachineProfile kMachineProfiles[] = {
    {"68000", "Macintosh Plus", "plus", MachineKind::Plus, PlatformKind::Compact, CpuFamily::M68000, SnapMachine::Plus, "roms/macplus.rom", nullptr, nullptr, nullptr, true},
    {"68000", "Macintosh SE", "se", MachineKind::Se, PlatformKind::Compact, CpuFamily::M68000, SnapMachine::SE, "roms/macse.rom", "B2E362A8", nullptr, nullptr, true},
    {"68000", "Macintosh SE FDHD", "sefdhd", MachineKind::SeFdhd, PlatformKind::Compact, CpuFamily::M68000, SnapMachine::SEFDHD, "roms/macsefd.rom", "B306E171", nullptr, nullptr, true},
    {"68000", "Macintosh Classic", "classic", MachineKind::MacClassic, PlatformKind::Compact, CpuFamily::M68000, SnapMachine::Classic, "roms/macclassic.rom", "A49F9914", nullptr, nullptr, true},

    {"GLUE + NuBus (Mac II)", "Macintosh II", "macii", MachineKind::MacII, PlatformKind::Glue, CpuFamily::M68020, SnapMachine::MacII, "roms/macii.rom", "9779D2C4", "POM68K_MACII_MODEL", "ii", false},
    {"GLUE + NuBus (Mac II)", "Macintosh IIx", "iix", MachineKind::MacII, PlatformKind::Glue, CpuFamily::M68030, SnapMachine::IIx, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iix", true},
    {"GLUE + NuBus (Mac II)", "Macintosh IIcx", "iicx", MachineKind::MacII, PlatformKind::Glue, CpuFamily::M68030, SnapMachine::IIcx, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iicx", false},
    {"GLUE + NuBus (Mac II)", "Macintosh SE/30", "se30", MachineKind::MacII, PlatformKind::Glue, CpuFamily::M68030, SnapMachine::SE30, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "se30", false},
    {"OSS + IOP (IIfx)", "Macintosh IIfx (40 MHz)", "iifx", MachineKind::IIfx, PlatformKind::Oss, CpuFamily::M68030, SnapMachine::IIfx, "roms/maciifx.rom", "4147DD77", nullptr, nullptr, true},

    {"RBV (video en RAM)", "Macintosh IIci", "iici", MachineKind::IIci, PlatformKind::Rbv, CpuFamily::M68030, SnapMachine::IIci, "roms/maciici.rom", "368CADFE", nullptr, nullptr, true},
    {"RBV (video en RAM)", "Macintosh IIsi", "iisi", MachineKind::IIsi, PlatformKind::Rbv, CpuFamily::M68030, SnapMachine::IIsi, "roms/maciisi.rom", "36B7FB6C", nullptr, nullptr, true},

    {"V8 / Eagle / Spice / Tinker Bell", "Macintosh LC", "lc", MachineKind::Lc, PlatformKind::V8, CpuFamily::M68020, SnapMachine::Lc, "roms/maclc.rom", "350EACF0", nullptr, nullptr, true},
    {"V8 / Eagle / Spice / Tinker Bell", "Macintosh LC II", "lcii", MachineKind::LcII, PlatformKind::V8, CpuFamily::M68030, SnapMachine::LcII, "roms/maclcii.rom", "35C28F5F", nullptr, nullptr, true},
    {"V8 / Eagle / Spice / Tinker Bell", "Macintosh Classic II", "classic2", MachineKind::ClassicII, PlatformKind::V8, CpuFamily::M68030, SnapMachine::ClassicII, "roms/classic2.rom", "3193670E", nullptr, nullptr, true},
    {"V8 / Eagle / Spice / Tinker Bell", "Macintosh Color Classic", "cclassic", MachineKind::ColorClassic, PlatformKind::V8, CpuFamily::M68030, SnapMachine::ColorClassic, "roms/cclassic.rom", "ECD99DC0", nullptr, nullptr, true},
    {"V8 / Eagle / Spice / Tinker Bell", "Macintosh TV", "mactv", MachineKind::MacTv, PlatformKind::V8, CpuFamily::M68030, SnapMachine::MacTv, "roms/mactv.rom", "EAF1678D", nullptr, nullptr, true},

    {"Sonora", "Macintosh LC III", "lc3", MachineKind::Lc3, PlatformKind::Sonora, CpuFamily::M68030, SnapMachine::Lc3, "roms/maclc3.rom", "ECBBC41C", "POM68K_LC3_PLUS", "0", true},
    {"Sonora", "Macintosh LC III+ (33 MHz)", "lc3plus", MachineKind::Lc3, PlatformKind::Sonora, CpuFamily::M68030, SnapMachine::Lc3Plus, "roms/maclc3.rom", "ECBBC41C", "POM68K_LC3_PLUS", "1", false},
    {"Sonora", "Macintosh LC 520", "lc520", MachineKind::Aio, PlatformKind::Sonora, CpuFamily::M68030, SnapMachine::Lc520, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "A55A0100", true},
    {"Sonora", "Macintosh LC 550 (33 MHz)", "lc550", MachineKind::Aio, PlatformKind::Sonora, CpuFamily::M68030, SnapMachine::Lc550, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "A55A0101", false},
    {"Sonora", "Macintosh Color Classic II", "cclassic2", MachineKind::Aio, PlatformKind::Sonora, CpuFamily::M68030, SnapMachine::CClassic2, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "CC2", false},

    {"VASP (Sonora + peripheriques V8)", "Macintosh IIvx", "iivx", MachineKind::Vasp, PlatformKind::Vasp, CpuFamily::M68030, SnapMachine::IIvx, "roms/maciivx.rom", "4957EB49", "POM68K_IIVI", "0", true},
    {"VASP (Sonora + peripheriques V8)", "Macintosh IIvi (16 MHz)", "iivi", MachineKind::Vasp, PlatformKind::Vasp, CpuFamily::M68030, SnapMachine::IIvi, "roms/maciivx.rom", "4957EB49", "POM68K_IIVI", "1", false},

    {"MEMCjr + PrimeTime", "Macintosh LC 475", "lc475", MachineKind::Quadra, PlatformKind::MemcJr, CpuFamily::M68040, SnapMachine::Lc475, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A2221", true},
    {"MEMCjr + PrimeTime", "Macintosh LC 575 (33 MHz)", "lc575", MachineKind::Quadra, PlatformKind::MemcJr, CpuFamily::M68040, SnapMachine::Lc575, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A222E", false},
    {"MEMCjr + PrimeTime", "Quadra 605", "q605", MachineKind::Quadra, PlatformKind::MemcJr, CpuFamily::M68040, SnapMachine::Q605, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A2225", false},

    {"djMEMC + IOSB", "Macintosh Centris 610 (20 MHz)", "c610", MachineKind::Centris, PlatformKind::DjMemc, CpuFamily::M68040, SnapMachine::Centris610, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "c610", false},
    {"djMEMC + IOSB", "Macintosh Centris 650", "c650", MachineKind::Centris, PlatformKind::DjMemc, CpuFamily::M68040, SnapMachine::Centris650, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "c650", true},
    {"djMEMC + IOSB", "Macintosh Quadra 610", "q610", MachineKind::Centris, PlatformKind::DjMemc, CpuFamily::M68040, SnapMachine::Quadra610, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q610", false},
    {"djMEMC + IOSB", "Macintosh Quadra 650 (33 MHz)", "q650", MachineKind::Centris, PlatformKind::DjMemc, CpuFamily::M68040, SnapMachine::Quadra650, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q650", false},
    {"djMEMC + IOSB", "Macintosh Quadra 800 (33 MHz)", "q800", MachineKind::Centris, PlatformKind::DjMemc, CpuFamily::M68040, SnapMachine::Quadra800, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q800", false},

    {"Discret 040 (Quadra 700/900/950)", "Macintosh Quadra 700", "q700", MachineKind::Q700, PlatformKind::Spike, CpuFamily::M68040, SnapMachine::Q700, "roms/quadra700.rom", "420DBFF3", "POM68K_Q700_MODEL", "q700", true},
    {"Discret 040 (Quadra 700/900/950)", "Macintosh Quadra 900 (IOP)", "q900", MachineKind::Q700, PlatformKind::Spike, CpuFamily::M68040, SnapMachine::Quadra900, "roms/quadra700.rom", "420DBFF3", "POM68K_Q700_MODEL", "q900", false},
    {"Discret 040 (Quadra 700/900/950)", "Macintosh Quadra 950 (33 MHz, IOP)", "q950", MachineKind::Q700, PlatformKind::Spike, CpuFamily::M68040, SnapMachine::Quadra950, "roms/quadra950.rom", "3DC27823", "POM68K_Q700_MODEL", "q950", false},

    {"F108 + PrimeTime II + Valkyrie", "Macintosh Quadra 630 (33 MHz)", "q630", MachineKind::Q630, PlatformKind::F108, CpuFamily::M68040, SnapMachine::Q630, "roms/quadra630.rom", "06684214", "POM68K_Q630_ID", "A55A2252", true},
    {"F108 + PrimeTime II + Valkyrie", "Macintosh LC / Performa 580", "lc580", MachineKind::Q630, PlatformKind::F108, CpuFamily::M68040, SnapMachine::Lc580, "roms/quadra630.rom", "06684214", "POM68K_Q630_ID", "A55A225A", false},

    {"MSC + PG&E (PowerBook Duo)", "PowerBook Duo 230 (33 MHz)", "duo230", MachineKind::Duo, PlatformKind::Msc, CpuFamily::M68030, SnapMachine::Duo230, "roms/macduo230.rom", "ECFA989B", nullptr, nullptr, true},
};

inline constexpr std::size_t kMachineProfileCount = std::size(kMachineProfiles);

constexpr const MachineProfile* machineProfile(SnapMachine id) {
    for (const auto& profile : kMachineProfiles)
        if (profile.snapshot == id) return &profile;
    return nullptr;
}

consteval bool validMachineCatalog() {
    for (std::size_t i = 0; i < kMachineProfileCount; ++i) {
        const auto& a = kMachineProfiles[i];
        if (!a.group || !a.label || !a.slug || !a.romPath) return false;
        if ((a.variantKey == nullptr) != (a.variantValue == nullptr)) return false;
        for (std::size_t j = i + 1; j < kMachineProfileCount; ++j) {
            const auto& b = kMachineProfiles[j];
            if (a.snapshot == b.snapshot) return false;
        }
    }
    // Snapshot ids are append-only and currently dense: a new id without a
    // catalogue row, or a row without an id, must fail the build immediately.
    for (std::uint32_t id = 1; id <= kMachineProfileCount; ++id)
        if (!machineProfile(static_cast<SnapMachine>(id))) return false;
    return true;
}

static_assert(validMachineCatalog(), "invalid or incomplete Macintosh profile catalogue");

} // namespace pom68k
