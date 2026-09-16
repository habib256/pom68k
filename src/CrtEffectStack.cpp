// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See CrtEffectStack.h. The shader bodies are NeoST's (gui/CrtEffectStack.cpp,
// 2026-09-16), unchanged; the GL side is POM68K's 3.x core context: a VAO is
// always used (core profile requires one), the entry points come from
// GlEntryPoints.h, and every GL state touched is restored for the ImGui
// OpenGL3 backend that draws after us.

#include "CrtEffectStack.h"
#include "GlEntryPoints.h"
#include "OpenGLShader.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace pom68k::gui {
namespace {

const char* kVertexShader = R"GLSL(
#if __VERSION__ < 130
attribute vec2 aPos;
varying vec2 vUv;
#else
in vec2 aPos;
out vec2 vUv;
#endif
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#if __VERSION__ < 130
varying vec2 vUv;
#define texture texture2D
#define FRAG_COLOR gl_FragColor
#else
in vec2 vUv;
out vec4 fragColor;
#define FRAG_COLOR fragColor
#endif

uniform sampler2D uSrc;        // framebuffer RGBA source
uniform sampler2D uPrev;       // sortie précédente (persistance)
uniform vec2  uSrcSize;        // (largeur, hauteur) de uSrc
uniform vec2  uOutSize;        // (largeur, hauteur) de cette passe
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHue;            // -0.5..+0.5 → rotation chroma ±π
uniform float uSharpness;      // 0.5 = neutre ; >0.5 accentue, <0.5 adoucit
uniform float uPersistence;
uniform float uScanlines;
uniform float uBarrel;
uniform int   uShadowMask;     // 0=off,1=triade,2=grille,3=points
uniform float uShadowStrength; // 0..1
uniform float uLuminanceGain;  // re-brillance post-verre, 1.0 = neutre
uniform float uCenterLighting; // vignette : 1.0 = plat (off), <1 assombrit les bords
uniform float uPhosphorGamma;  // réponse phosphore γ : 1.0 = plat (off)

// Poids cubique Catmull-Rom (4 taps/axe). Utilisé quand la passe agrandit le
// framebuffer basse-rés pour que scanlines/masque reposent sur une couleur
// lisse plutôt que des blocs NEAREST.
float cubicWeight(float x)
{
    x = abs(x);
    if (x < 1.0) return x * x * (1.5 * x - 2.5) + 1.0;
    if (x < 2.0) return x * (x * (-0.5 * x + 2.5) - 4.0) + 2.0;
    return 0.0;
}

vec3 sampleSrc(vec2 uv)
{
    uv = clamp(uv, 0.0, 1.0);
    float mag = max(uOutSize.x / uSrcSize.x, uOutSize.y / uSrcSize.y);
    if (mag <= 1.25)
        return texture(uSrc, uv).rgb;

    vec2 coord = uv * uSrcSize - 0.5;
    vec2 f = fract(coord);
    coord = floor(coord);
    vec3 col = vec3(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            vec2 offs = vec2(float(i), float(j));
            vec2 samp = (coord + offs + 0.5) / uSrcSize;
            float w = cubicWeight(offs.x - f.x) * cubicWeight(offs.y - f.y);
            col += texture(uSrc, clamp(samp, 0.0, 1.0)).rgb * w;
            wsum += w;
        }
    }
    return col / max(wsum, 1e-4);
}

void main()
{
    // ── Distorsion de baril ───────────────────────────────────────
    vec2 cuv = vUv * 2.0 - 1.0;
    float r2 = dot(cuv, cuv);
    vec2 buv = cuv * (1.0 + uBarrel * r2);
    vec2 uv  = buv * 0.5 + 0.5;
    // Bord anti-aliasé : fond en noir sur un pixel de sortie au bord déformé.
    vec2  edge     = min(uv, 1.0 - uv);
    vec2  edgeFw   = max(fwidth(uv), vec2(1e-4));
    float edgeMask = clamp(min(edge.x / edgeFw.x, edge.y / edgeFw.y), 0.0, 1.0);
    vec3 rgb = sampleSrc(uv);

    // ── Sharpness (unsharp mask / adoucissement, neutre à 0.5) ────
    {
        float amt = (uSharpness - 0.5) * 2.0;   // -1 (doux) .. +1 (net)
        if (amt != 0.0) {
            vec2 t = 1.0 / uSrcSize;
            vec3 blur = (
                sampleSrc(uv + vec2(-t.x, 0.0)) +
                sampleSrc(uv + vec2( t.x, 0.0)) +
                sampleSrc(uv + vec2(0.0, -t.y)) +
                sampleSrc(uv + vec2(0.0,  t.y))) * 0.25;
            rgb = clamp(rgb + amt * (rgb - blur), 0.0, 1.0);
        }
    }

    // ── Rotation de teinte ────────────────────────────────────────
    // RGB→YUV (BT.601), rotation U/V de uHue·π, YUV→RGB (matrice OpenEmulator).
    if (uHue != 0.0) {
        float Y = dot(rgb, vec3( 0.299,    0.587,    0.114));
        float U = dot(rgb, vec3(-0.14713, -0.28886,  0.436));
        float V = dot(rgb, vec3( 0.615,   -0.51499, -0.10001));
        float a  = uHue * 3.14159265;
        float cs = cos(a), sn = sin(a);
        float Ur = U * cs - V * sn;
        float Vr = U * sn + V * cs;
        rgb = vec3(Y                 + 1.139883 * Vr,
                   Y - 0.394642 * Ur - 0.580622 * Vr,
                   Y + 2.032062 * Ur);
    }

    // ── Luminosité / contraste / saturation ───────────────────────
    rgb = (rgb - 0.5) * uContrast + 0.5 + uBrightness;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, clamp(uSaturation, 0.0, 4.0));
    rgb = clamp(rgb, 0.0, 1.0);

    // ── Courbe de réponse phosphore (gamma CRT) ───────────────────
    if (uPhosphorGamma != 1.0) {
        rgb = pow(max(rgb, vec3(0.0)), vec3(uPhosphorGamma));
    }

    // ── Scanlines (faisceau doux, anti-alias analytique) ──────────
    float outRow = uv.y * (uSrcSize.y * 2.0);
    float rowFw  = max(fwidth(outRow), 1e-4);
    float scanAA = clamp(1.0 - (rowFw - 0.5) / 0.5, 0.0, 1.0); // 1 net → 0 alias
    float beam   = 0.5 + 0.5 * cos(3.14159265 * outRow);       // période 2, doux
    rgb *= 1.0 - uScanlines * (1.0 - beam) * scanAA;

    // ── Shadow mask (procédural, anti-alias analytique) ───────────
    if (uShadowMask != 0 && uShadowStrength > 0.0) {
        float oxBase = uv.x * (uSrcSize.x * 2.0);
        float maskFw   = max(fwidth(oxBase), 1e-4);
        float maskAA   = clamp(1.0 - (maskFw - 1.0) / 2.0, 0.0, 1.0);
        float ox = oxBase;
        if (uShadowMask == 3) {
            ox += (mod(floor(outRow * 0.5), 2.0) < 1.0) ? 0.0 : 1.5;
        }
        float strength = uShadowStrength * maskAA;
        int phase = int(mod(floor(ox), 3.0));
        // Triplet sombre/clair de Lottes : préserve la luma moyenne.
        const float maskDark = 0.5, maskLight = 1.5;
        vec3 mask = vec3(maskDark);
        if      (phase == 0) mask.r = maskLight;
        else if (phase == 1) mask.g = maskLight;
        else                 mask.b = maskLight;
        vec3 atten = mix(vec3(1.0), mask, strength);
        if (uShadowMask == 1 || uShadowMask == 3) {
            float vrow = mod(floor(outRow), 3.0);
            if (vrow < 1.0) atten *= mix(1.0, 0.7, strength);
        }
        rgb *= atten;
    }

    // ── Center lighting / vignette (ordre OpenEmulator : après le masque) ──
    {
        // max() défensif : l'UI borne à 0.5..1.0, mais un neost.cfg chargeant
        // crt_center=0 donnerait 1/0 → inf. On garde une vignette bien définie.
        vec2 lighting = cuv * (1.0 / max(uCenterLighting, 0.01) - 1.0);
        rgb *= exp(-dot(lighting, lighting));
    }

    // ── Luminance gain (post-verre) ───────────────────────────────
    rgb *= uLuminanceGain;

    // ── Persistance (rémanence phosphore) ─────────────────────────
    // Sur la couleur finale corrigée. Le plancher -0.5/256 traîne les rémanences
    // faibles jusqu'au noir en temps fini. `prev` est la sortie masquée de la trame
    // précédente (edgeMask appliqué EN DERNIER, ci-dessous) → elle est déjà nulle
    // hors du cadre courbé, donc la rémanence ne bave pas au-delà du bord baril.
    vec3 prev = texture(uPrev, vUv).rgb;
    rgb = max(rgb, prev * clamp(uPersistence, 0.0, 0.98) - 0.5 / 256.0);

    // Masque de bord appliqué EN TOUT DERNIER : le résultat écrit (= `prev` de la
    // trame suivante) est noir hors du cadre déformé, sans halo de rémanence débordant.
    FRAG_COLOR = vec4(rgb * edgeMask, 1.0);
}
)GLSL";

} // namespace

CrtEffectStack::~CrtEffectStack() = default;

bool CrtEffectStack::initialize() {
    if (initialized_) return ready_;
    initialized_ = true;
    if (!pom68k::gl::load()) {
        error_ = "GL 3.x entry points unavailable";
        return false;
    }
    program_ = compileShaderProgram(kVertexShader, kFragmentShader, &error_);
    if (!program_) return false;

    uSrc_ = glGetUniformLocation(program_, "uSrc");
    uPrev_ = glGetUniformLocation(program_, "uPrev");
    uSrcSize_ = glGetUniformLocation(program_, "uSrcSize");
    uOutSize_ = glGetUniformLocation(program_, "uOutSize");
    uBrightness_ = glGetUniformLocation(program_, "uBrightness");
    uContrast_ = glGetUniformLocation(program_, "uContrast");
    uSaturation_ = glGetUniformLocation(program_, "uSaturation");
    uHue_ = glGetUniformLocation(program_, "uHue");
    uSharpness_ = glGetUniformLocation(program_, "uSharpness");
    uPersistence_ = glGetUniformLocation(program_, "uPersistence");
    uScanlines_ = glGetUniformLocation(program_, "uScanlines");
    uBarrel_ = glGetUniformLocation(program_, "uBarrel");
    uShadowMask_ = glGetUniformLocation(program_, "uShadowMask");
    uShadowStr_ = glGetUniformLocation(program_, "uShadowStrength");
    uLuminanceGain_ = glGetUniformLocation(program_, "uLuminanceGain");
    uCenterLighting_ = glGetUniformLocation(program_, "uCenterLighting");
    uPhosphorGamma_ = glGetUniformLocation(program_, "uPhosphorGamma");

    const float verts[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,   1.0f,  1.0f,
    };
    GLint prevVao = 0, prevBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuffer);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(GLuint(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(prevBuffer));

    ready_ = true;
    std::fprintf(stderr, "[CRT] CRT effect stack ready\n");
    return true;
}

bool CrtEffectStack::createTextures(int w, int h) {
    outW_ = w;
    outH_ = h;
    glGenFramebuffers(2, fbo_);
    glGenTextures(2, outputTex_);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, outputTex_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW_, outH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error_ = "FBO incomplete";
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(2, fbo_);
            glDeleteTextures(2, outputTex_);
            fbo_[0] = fbo_[1] = 0;
            outputTex_[0] = outputTex_[1] = 0;
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    firstFrame_ = true;
    return true;
}

unsigned int CrtEffectStack::process(unsigned int srcTex, int srcW, int srcH, int dstW, int dstH) {
    if (!ready_ || srcTex == 0) return 0;
    dstW = std::max(1, dstW);
    dstH = std::max(1, dstH);

    if (outputTex_[0] == 0) {
        // A refused allocation is not final: passthrough this frame, retry
        // when the requested size changes.
        if (dstW == failedW_ && dstH == failedH_) return 0;
        if (!createTextures(dstW, dstH)) { failedW_ = dstW; failedH_ = dstH; return 0; }
        failedW_ = failedH_ = -1;
    } else if (dstW != outW_ || dstH != outH_) {
        outW_ = dstW; outH_ = dstH;
        bool resizeOk = true;
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, outputTex_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW_, outH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_[i]);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) resizeOk = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!resizeOk) {
            error_ = "FBO incomplete after resize";
            glDeleteFramebuffers(2, fbo_);
            glDeleteTextures(2, outputTex_);
            fbo_[0] = fbo_[1] = 0;
            outputTex_[0] = outputTex_[1] = 0;
            outW_ = outH_ = 0;
            failedW_ = dstW; failedH_ = dstH;
            return 0;
        }
        firstFrame_ = true;
    }

    // Save what the ImGui backend expects to find afterwards.
    GLint prevFbo = 0, prevViewport[4] = {0, 0, 0, 0}, prevProgram = 0, prevVao = 0;
    GLint prevTex0 = 0, prevTex1 = 0, prevActive = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex1);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);

    const int writeIdx = pingPong_;
    const int readIdx = 1 - pingPong_;
    pingPong_ = readIdx;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_[writeIdx]);
    glViewport(0, 0, outW_, outH_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    // The source is uploaded NEAREST for the 1:1 desktop path; the pass
    // samples it LINEAR (sampleSrc adds bicubic when magnifying further).
    GLint prevMinFilter = GL_NEAREST, prevMagFilter = GL_NEAREST;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &prevMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &prevMagFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glUseProgram(program_);
    glUniform1i(uSrc_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, firstFrame_ ? srcTex : outputTex_[readIdx]);
    glUniform1i(uPrev_, 1);
    if (uSrcSize_ >= 0) glUniform2f(uSrcSize_, float(srcW), float(srcH));
    if (uOutSize_ >= 0) glUniform2f(uOutSize_, float(outW_), float(outH_));
    if (uBrightness_ >= 0) glUniform1f(uBrightness_, params_.brightness);
    if (uContrast_ >= 0) glUniform1f(uContrast_, params_.contrast);
    if (uSaturation_ >= 0) glUniform1f(uSaturation_, params_.saturation);
    if (uHue_ >= 0) glUniform1f(uHue_, params_.hue);
    if (uSharpness_ >= 0) glUniform1f(uSharpness_, params_.sharpness);
    if (uPersistence_ >= 0) glUniform1f(uPersistence_, params_.persistence);
    if (uScanlines_ >= 0) glUniform1f(uScanlines_, params_.scanlines);
    if (uBarrel_ >= 0) glUniform1f(uBarrel_, params_.barrel);
    if (uShadowMask_ >= 0) glUniform1i(uShadowMask_, static_cast<int>(params_.shadowMask));
    if (uShadowStr_ >= 0) glUniform1f(uShadowStr_, params_.shadowMaskStrength);
    if (uLuminanceGain_ >= 0) glUniform1f(uLuminanceGain_, params_.luminanceGain);
    if (uCenterLighting_ >= 0) glUniform1f(uCenterLighting_, params_.centerLighting);
    if (uPhosphorGamma_ >= 0) glUniform1f(uPhosphorGamma_, params_.phosphorGamma);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Restore everything the ImGui backend depends on.
    glBindVertexArray(GLuint(prevVao));
    glUseProgram(GLuint(prevProgram));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, prevMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prevMagFilter);
    glBindTexture(GL_TEXTURE_2D, GLuint(prevTex0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, GLuint(prevTex1));
    glActiveTexture(GLenum(prevActive));
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    firstFrame_ = false;
    return outputTex_[writeIdx];
}

void bindCrtPass(GuiDisplayState& display, CrtEffectStack& stack) {
    display.pass = [&stack, &display](unsigned tex, int sw, int sh, int dw, int dh) -> unsigned {
        if (!stack.available() && (stack.attempted() || !stack.initialize())) return 0;
        stack.setParams(display.crt);
        return stack.process(tex, sw, sh, dw, dh);
    };
    display.passError = [&stack] {
        return stack.attempted() && !stack.available() ? stack.lastError() : std::string();
    };
}

} // namespace pom68k::gui
