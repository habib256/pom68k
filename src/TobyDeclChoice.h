// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// TobyDeclChoice -- which declaration ROM the Toby video card presents to the
// Slot Manager, chosen and reported like every other firmware choice.
//
// The card in slot 9 of the Mac II, IIx, IIcx and IIfx is the Apple
// Macintosh II Video Card; its declaration ROM is the 4 KB 342-0008-a dump.
// POM68K emulates machines that existed, so the alternative is not a
// second firmware but a FALLBACK: `DeclRom::buildSynthetic` describes the
// emulated frame buffer truthfully and nothing more. System 6 and 7.0 boot on
// it; System 7.5.5 parks a slot VBL task on the card and never gets it back
// (CHANGELOG 2026-09-15). That difference is what the Périphériques window
// must say, and the only place it can learn it from is the device's own
// report — hence `fw::select`, with the same knobs, override and reason
// vocabulary as the MCU dumps (docs/LLE_VS_HLE.md § 2).
//
// Both boards used to carry this search as twenty duplicated lines; the
// product's candidate list also lacked the archive path every Mac II etalon
// searched, so a GUI session on a host holding the dump ran synthetic.

#pragma once

#include "CoreConfig.h"
#include "DeclRom.h"
#include "FirmwareChoice.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pom68k::toby {

// Where the dump is looked for, repo root first, then from build/. The
// canonical drop point is documented in README § Additional firmware; the
// archive path is the layout the MAME romset ships it under.
inline std::vector<std::string> declRomCandidates() {
    return {
        "roms/342-0008-a.bin",
        "../roms/342-0008-a.bin",
        "roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin",
        "../roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin",
        "tests/data/342-0008-a.bin",
        "../tests/data/342-0008-a.bin",
    };
}

inline const char* kSubstituteConsequence =
    "Sans le dump 342-0008-a : System 6 et 7.0 démarrent, System 7.5.5 reste "
    "sur « Welcome to Macintosh ».";

// The guest-ordered image to install on the NuBus: the real card ROM when
// one loads, the synthetic fallback otherwise. `injectedPath` is an
// etalon's explicit dump and outranks the configured override, so a gate
// that names its asset never depends on the environment it runs in.
inline std::vector<std::uint8_t> selectDeclRom(const CoreFirmwareConfig& firmware,
                                               const std::string& injectedPath,
                                               std::uint32_t slotBase,
                                               const char* logTag) {
    std::vector<std::uint8_t> image;
    fw::Request req{lle::HleTobyDeclRom, FirmwareTarget::TobyDecl};
    req.name = "Toby — ROM de déclaration de la carte vidéo Macintosh II "
               "(342-0008-a)";
    req.enableKnob = "POM68K_TOBY_DECL_LLE";
    req.pathKnob = "POM68K_TOBY_DECL";
    req.logTag = logTag;
    req.candidates = declRomCandidates();
    req.enabled = firmware.tobyDeclLle;
    req.forcedPath = !injectedPath.empty()
                         ? injectedPath
                         : firmware.tobyDeclPath.value_or(std::string());
    req.registry = firmware.registry;
    req.consequence = kSubstituteConsequence;
    const bool real = fw::select(req, [&](const std::vector<std::uint8_t>& raw) {
        // The MAME dump is exactly 4 KB (DeclRom::loadTobyRaw's rule); a file
        // of another size is not this card's ROM, whatever its name.
        if (raw.size() != 4096) return false;
        image = DeclRom::installRaw(raw.data(), raw.size());
        return !image.empty();
    });
    if (!real) image = DeclRom::buildSynthetic(slotBase);
    return image;
}

} // namespace pom68k::toby
