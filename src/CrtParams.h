// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// CrtParams -- the settings of the CRT "glass" pass (CrtEffectStack.h), and
// the named presets. Ported from NeoST (gui/CrtParams.h, itself a subset of
// POM2's NtscParams) on 2026-09-16; field names kept so the shader's uniform
// wiring stays identical across the three emulators.
//
// Defaults are a "visibly neutral" look: flat geometry, identity BCS, light
// scanlines and barrel. A preset overwrites them; the settings window then
// edits every value live.

#pragma once

#include <string>

namespace pom68k::gui {

struct CrtParams {
    float brightness  = 0.0f;   // -0.5..+0.5 added to luma
    float contrast    = 1.0f;   //  0.5..1.5 around 0.5
    float saturation  = 1.0f;   //  0..2 chroma multiplier
    float hue         = 0.0f;   // -0.5..+0.5 chroma rotation (±π)
    // Sharpness: 0.5 = neutral (passthrough); >0.5 sharpens (unsharp mask
    // against a 4-tap blur), <0.5 softens.
    float sharpness   = 0.5f;
    // Phosphor persistence: 0 = none, 0.98 = near-infinite (per-frame
    // retention factor, POM2's "punchy" model).
    float persistence = 0.4f;
    // Pure post effects.
    float scanlines   = 0.25f;  // 0 = off, 1 = black between every line
    float barrel      = 0.05f;  // 0 = flat, 0.2 = old bulging CRT
    enum class ShadowMask : int {
        Off      = 0,
        Triad    = 1,   // three-stripe triad
        Aperture = 2,   // aperture grille (Trinitron)
        Dot      = 3,   // dot mask (offset triads)
    };
    ShadowMask shadowMask         = ShadowMask::Off;
    float      shadowMaskStrength = 0.5f;  // 0..1
    // Post-glass re-brightening: compensates scanlines + mask darkening.
    float luminanceGain = 1.0f;  // 1.0..2.0
    // Vignette / center lighting: 1.0 = flat (off), lower darkens the edges.
    float centerLighting = 1.0f; // 0.5..1.0
    // Phosphor response curve (CRT gamma): 1.0 = flat, >1 deepens shadows.
    float phosphorGamma = 1.0f;  // 0.6..2.6
};

// Named presets, the same three NeoST ships plus "off". A preset is a
// starting point: the settings window may then move every slider. Returns
// false on an unknown name (params untouched). "off" turns the pass off and
// leaves the params as they were.
inline bool applyCrtPreset(const std::string& name, CrtParams& p, bool& on) {
    using SM = CrtParams::ShadowMask;
    if (name == "off" || name == "0" || name.empty()) { on = false; return true; }
    CrtParams q{};
    if (name == "light" || name == "leger" || name == "léger") {
        q.scanlines = 0.18f; q.barrel = 0.03f; q.persistence = 0.20f;
        q.luminanceGain = 1.10f; q.centerLighting = 0.96f;
    } else if (name == "arcade" || name == "1") {
        q.scanlines = 0.45f; q.barrel = 0.12f; q.persistence = 0.35f;
        q.shadowMask = SM::Triad; q.shadowMaskStrength = 0.60f;
        q.luminanceGain = 1.50f; q.centerLighting = 0.82f; q.phosphorGamma = 1.30f;
    } else if (name == "phosphor" || name == "phosphore") {
        q.scanlines = 0.30f; q.barrel = 0.08f; q.persistence = 0.60f;
        q.shadowMask = SM::Aperture; q.shadowMaskStrength = 0.40f;
        q.luminanceGain = 1.35f; q.centerLighting = 0.88f; q.phosphorGamma = 1.50f;
    } else {
        return false;
    }
    p = q;
    on = true;
    return true;
}

inline const char* const kCrtPresetNames[] = {"off", "light", "arcade", "phosphor"};

} // namespace pom68k::gui
