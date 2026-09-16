// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// CrtEffectStack -- the "glass" of a CRT monitor put back in front of the
// pixels: barrel geometry, brightness/contrast/saturation, hue, phosphor
// curve, scanlines, shadow mask, vignette, luminance gain and phosphor
// persistence, in one FBO pass rendered at output resolution so scanlines
// and mask are anti-aliased analytically (fwidth) without moiré. Ported from
// NeoST (gui/CrtEffectStack, itself from POM2) on 2026-09-16; the shader is
// the same, the GL binding is POM68K's 3.x core context (GlEntryPoints.h).
//
// Opt-in and safe: when the shader does not compile or the entry points are
// missing, available() stays false and process() returns 0 — the caller
// shows the raw texture.

#pragma once

#include "CrtParams.h"

#include <string>

namespace pom68k::gui {

class CrtEffectStack {
public:
    CrtEffectStack() = default;
    ~CrtEffectStack();
    CrtEffectStack(const CrtEffectStack&) = delete;
    CrtEffectStack& operator=(const CrtEffectStack&) = delete;

    // Compiles the shader and allocates the quad. Textures/FBO come lazily
    // at the first process() (they need the output size). Returns false on
    // failure; process() is then a no-op.
    bool initialize();
    bool available() const { return ready_; }
    bool attempted() const { return initialized_; }

    void setParams(const CrtParams& p) { params_ = p; }
    const CrtParams& params() const { return params_; }

    // Applies the pass to the RGBA texture `srcTex` (logical size srcW × srcH,
    // which drives the scanline/mask frequency) and renders dstW × dstH.
    // Returns a GL texture name of that size, or 0 when unavailable.
    unsigned int process(unsigned int srcTex, int srcW, int srcH, int dstW, int dstH);

    const std::string& lastError() const { return error_; }

private:
    bool createTextures(int w, int h);

    bool ready_ = false;
    bool initialized_ = false;
    std::string error_;
    unsigned int program_ = 0;
    unsigned int outputTex_[2] = {0, 0};   // ping-pong for persistence
    unsigned int fbo_[2] = {0, 0};
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int uSrc_ = -1, uPrev_ = -1, uSrcSize_ = -1, uOutSize_ = -1;
    int uBrightness_ = -1, uContrast_ = -1, uSaturation_ = -1, uHue_ = -1;
    int uSharpness_ = -1, uPersistence_ = -1, uScanlines_ = -1, uBarrel_ = -1;
    int uShadowMask_ = -1, uShadowStr_ = -1, uLuminanceGain_ = -1;
    int uCenterLighting_ = -1, uPhosphorGamma_ = -1;
    int outW_ = 0, outH_ = 0;
    int failedW_ = -1, failedH_ = -1;   // an FBO size the driver refused
    int pingPong_ = 0;
    bool firstFrame_ = true;
    CrtParams params_{};
};

} // namespace pom68k::gui
