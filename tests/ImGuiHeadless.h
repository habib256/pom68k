// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ImGuiHeadless -- Dear ImGui without a window system, for the window gates.
//
// A context whose "backend" is this file: the display is a fixed size, the
// font atlas textures are kept as pixel buffers (the 1.92 dynamic texture
// contract — ImDrawData::Textures, WantCreate/WantUpdates/WantDestroy), and
// ImDrawData is rasterised on the CPU into an RGBA frame: textured
// triangles, per-vertex colour, clip rectangles, alpha blend. The result is
// what the OpenGL backend would show, minus filtering — enough to see a
// window's layout in a PPM, and to assert that a region is not blank.
//
// Items are found by LABEL through ImGui's own test-engine hooks
// (IMGUI_ENABLE_TEST_ENGINE; imgui_internal.h:4295): every frame records
// id → bounding box and id → label, so `click("Appliquer et redémarrer")`
// puts the mouse where that button was drawn last frame and presses it.
// ImGui acts on a click at RELEASE on the frame after hover was established,
// hence the four frames per click.
//
// Why this exists: gui_smoke_test opens a real GLFW window and skips on any
// runner without a GL surface (every CI job), and it never looked at a
// window's contents. This harness runs everywhere and looks.

#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace headless {

struct Item {
    ImGuiID id = 0;
    ImRect bb;
    std::string label;
    bool visible = false;   // drawn this frame (bb clipped non-empty)
};

// Frame-scoped item registry, filled by the hooks below.
inline std::map<ImGuiID, Item>& items() {
    static std::map<ImGuiID, Item> m;
    return m;
}

struct Texture {
    int w = 0, h = 0;
    std::vector<std::uint32_t> rgba;   // 0xAABBGGRR as ImGui packs colours
};

class Context {
public:
    Context(int width, int height) : w_(width), h_(height) {
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(width), float(height));
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::StyleColorsDark();
        ctx_->TestEngineHookItems = true;
        frame_.assign(size_t(width) * size_t(height), 0xFF303030u);
    }
    ~Context() { ImGui::DestroyContext(ctx_); }

    // One frame: NewFrame, the caller's draw functions, Render, rasterise.
    template <class Draw> void frame(Draw&& draw) {
        items().clear();
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        rasterise(ImGui::GetDrawData());
    }

    // ── input ────────────────────────────────────────────────────────
    void mouseTo(float x, float y) { ImGui::GetIO().AddMousePosEvent(x, y); }
    void mouseDown(int b = 0) { ImGui::GetIO().AddMouseButtonEvent(b, true); }
    void mouseUp(int b = 0) { ImGui::GetIO().AddMouseButtonEvent(b, false); }
    void type(const char* text) { ImGui::GetIO().AddInputCharactersUTF8(text); }
    void key(ImGuiKey k, bool down) { ImGui::GetIO().AddKeyEvent(k, down); }

    // The item drawn last frame whose label equals `label` (first match in
    // ID order; pass `nth` for the next ones). Labels are what the widget
    // showed, "Chemin" for an InputText, the row text for a Selectable.
    const Item* find(const char* label, int nth = 0) const {
        for (const auto& [id, it] : items())
            if (it.label == label && it.visible && nth-- == 0) return &it;
        return nullptr;
    }

    // The first visible item whose label contains `part` — a combo row that
    // shows a file name and its size, say.
    const Item* findContaining(const char* part) const {
        for (const auto& [id, it] : items())
            if (it.visible && it.label.find(part) != std::string::npos) return &it;
        return nullptr;
    }

    // Widgets that register no label (BeginCombo, InputText with a ##id)
    // are reached by the ID ImGui gave them: the window's ID stack at top
    // level, which `ImGuiWindow::GetID` recomputes.
    ImGuiID idIn(const char* windowTitle, const char* label) const {
        ImGuiWindow* w = ImGui::FindWindowByName(windowTitle);
        return w ? w->GetID(label) : 0;
    }
    const Item* findId(ImGuiID id) const {
        const auto it = items().find(id);
        return it == items().end() || !it->second.visible ? nullptr : &it->second;
    }
    template <class Draw> bool clickId(ImGuiID id, Draw&& draw) {
        const Item* it = findId(id);
        if (!it) {
            std::printf("  click: no visible item with id %08X\n", id);
            return false;
        }
        return clickAt(it->bb.GetCenter(), draw);
    }

    // Move, press, release, settle — four frames through `draw`.
    template <class Draw> bool click(const char* label, Draw&& draw, int nth = 0) {
        const Item* it = find(label, nth);
        if (!it) {
            std::printf("  click: no item labelled '%s'\n", label);
            return false;
        }
        return clickAt(it->bb.GetCenter(), draw);
    }

    template <class Draw> bool clickAt(ImVec2 c, Draw&& draw) {
        mouseTo(c.x, c.y);
        frame(draw);
        trace("moved");
        mouseDown();
        frame(draw);
        trace("down");
        mouseUp();
        frame(draw);
        trace("up");
        frame(draw);
        return true;
    }

    // POM68K_GUI_TRACE=1: where ImGui thinks the mouse is, per click step.
    void trace(const char* step) const {
        if (!getenv("POM68K_GUI_TRACE")) return;
        const ImGuiContext& g = *ctx_;
        std::printf("    [%s] mouse (%.0f,%.0f) hovered window '%s' HoveredId %08X ActiveId %08X\n",
                    step, g.IO.MousePos.x, g.IO.MousePos.y,
                    g.HoveredWindow ? g.HoveredWindow->Name : "-", g.HoveredId, g.ActiveId);
    }

    // ── the frame ────────────────────────────────────────────────────
    int width() const { return w_; }
    int height() const { return h_; }
    std::uint32_t pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
        return frame_[size_t(y) * size_t(w_) + size_t(x)];
    }
    // How many distinct colours a rectangle shows — 1 is blank, a window
    // with text and widgets is in the dozens.
    int distinctColours(const ImRect& r) const {
        std::map<std::uint32_t, int> seen;
        for (int y = int(r.Min.y); y < int(r.Max.y); y++)
            for (int x = int(r.Min.x); x < int(r.Max.x); x++)
                seen[pixel(x, y)]++;
        return int(seen.size());
    }
    bool savePpm(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << "P6\n" << w_ << " " << h_ << "\n255\n";
        for (std::uint32_t p : frame_) {
            const char rgb[3] = {char(p & 0xFF), char((p >> 8) & 0xFF), char((p >> 16) & 0xFF)};
            out.write(rgb, 3);
        }
        return bool(out);
    }

private:
    // Textures: the atlas as ImGui hands it over (Alpha8 or RGBA32), kept
    // whole; updates rewrite the whole image (rects are a subset of it).
    void uploadTextures(ImDrawData* dd) {
        if (!dd->Textures) return;
        for (ImTextureData* tex : *dd->Textures) {
            if (tex->Status == ImTextureStatus_WantCreate ||
                tex->Status == ImTextureStatus_WantUpdates) {
                Texture& t = textures_[tex];
                t.w = tex->Width;
                t.h = tex->Height;
                t.rgba.assign(size_t(t.w) * size_t(t.h), 0);
                for (int y = 0; y < t.h; y++)
                    for (int x = 0; x < t.w; x++) {
                        const unsigned char* p = (const unsigned char*)tex->GetPixelsAt(x, y);
                        t.rgba[size_t(y) * size_t(t.w) + size_t(x)] =
                            tex->Format == ImTextureFormat_Alpha8
                                ? (std::uint32_t(p[0]) << 24) | 0x00FFFFFFu
                                : (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) |
                                      (std::uint32_t(p[1]) << 8) | p[0];
                    }
                tex->SetTexID(ImTextureID(reinterpret_cast<std::uintptr_t>(tex)));
                tex->SetStatus(ImTextureStatus_OK);
            } else if (tex->Status == ImTextureStatus_WantDestroy) {
                textures_.erase(tex);
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    static std::uint32_t blend(std::uint32_t dst, std::uint32_t src) {
        const unsigned a = src >> 24;
        if (a == 0) return dst;
        if (a == 255) return 0xFF000000u | (src & 0x00FFFFFFu);
        std::uint32_t out = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8) {
            const unsigned s = (src >> sh) & 0xFF, d = (dst >> sh) & 0xFF;
            out |= ((s * a + d * (255 - a)) / 255) << sh;
        }
        return out;
    }

    static std::uint32_t modulate(std::uint32_t c, std::uint32_t t) {
        std::uint32_t out = 0;
        for (int sh = 0; sh < 32; sh += 8)
            out |= ((((c >> sh) & 0xFF) * ((t >> sh) & 0xFF)) / 255) << sh;
        return out;
    }

    void rasterise(ImDrawData* dd) {
        uploadTextures(dd);
        std::fill(frame_.begin(), frame_.end(), 0xFF303030u);
        for (int n = 0; n < dd->CmdListsCount; n++) {
            const ImDrawList* cl = dd->CmdLists[n];
            const ImDrawVert* vtx = cl->VtxBuffer.Data;
            const ImDrawIdx* idx = cl->IdxBuffer.Data;
            for (const ImDrawCmd& cmd : cl->CmdBuffer) {
                if (cmd.UserCallback) continue;
                const Texture* tex = nullptr;
                const ImTextureID id = cmd.GetTexID();
                for (const auto& [k, t] : textures_)
                    if (ImTextureID(reinterpret_cast<std::uintptr_t>(k)) == id) tex = &t;
                const int cx0 = std::max(0, int(cmd.ClipRect.x)), cy0 = std::max(0, int(cmd.ClipRect.y));
                const int cx1 = std::min(w_, int(cmd.ClipRect.z)), cy1 = std::min(h_, int(cmd.ClipRect.w));
                for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                    const ImDrawVert& a = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i]];
                    const ImDrawVert& b = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i + 1]];
                    const ImDrawVert& c = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i + 2]];
                    triangle(a, b, c, tex, cx0, cy0, cx1, cy1);
                }
            }
        }
    }

    void triangle(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
                  const Texture* tex, int cx0, int cy0, int cx1, int cy1) {
        const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) -
                           (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
        if (area == 0.0f) return;
        const int x0 = std::max(cx0, int(std::floor(std::min({a.pos.x, b.pos.x, c.pos.x}))));
        const int x1 = std::min(cx1 - 1, int(std::ceil(std::max({a.pos.x, b.pos.x, c.pos.x}))));
        const int y0 = std::max(cy0, int(std::floor(std::min({a.pos.y, b.pos.y, c.pos.y}))));
        const int y1 = std::min(cy1 - 1, int(std::ceil(std::max({a.pos.y, b.pos.y, c.pos.y}))));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
                float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                std::uint32_t col = lerpColour(a.col, b.col, c.col, w0, w1, w2);
                if (tex && tex->w > 0) {
                    const float u = a.uv.x * w0 + b.uv.x * w1 + c.uv.x * w2;
                    const float v = a.uv.y * w0 + b.uv.y * w1 + c.uv.y * w2;
                    int tx = int(u * float(tex->w)), ty = int(v * float(tex->h));
                    tx = std::min(std::max(tx, 0), tex->w - 1);
                    ty = std::min(std::max(ty, 0), tex->h - 1);
                    col = modulate(col, tex->rgba[size_t(ty) * size_t(tex->w) + size_t(tx)]);
                }
                std::uint32_t& dst = frame_[size_t(y) * size_t(w_) + size_t(x)];
                dst = blend(dst, col);
            }
    }

    static std::uint32_t lerpColour(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                    float w0, float w1, float w2) {
        std::uint32_t out = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            const float v = ((a >> sh) & 0xFF) * w0 + ((b >> sh) & 0xFF) * w1 + ((c >> sh) & 0xFF) * w2;
            out |= std::uint32_t(std::min(255.0f, std::max(0.0f, v + 0.5f))) << sh;
        }
        return out;
    }

    ImGuiContext* ctx_ = nullptr;
    int w_, h_;
    std::vector<std::uint32_t> frame_;
    std::map<ImTextureData*, Texture> textures_;
};

} // namespace headless

// ── ImGui's test-engine hooks ────────────────────────────────────────────
// imgui.cpp (built with IMGUI_ENABLE_TEST_ENGINE) calls these as external
// symbols, so they are defined by the ONE translation unit that includes
// this header with POM68K_IMGUI_HEADLESS_HOOKS defined — the gate itself.
#ifdef POM68K_IMGUI_HEADLESS_HOOKS
// ── ImGui's test-engine hooks, satisfied here ────────────────────────────
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData*) {
    headless::Item& it = headless::items()[id];
    it.id = id;
    // Clipped to the window that drew it: a combo popup shows eight rows
    // and scrolls the rest, and a click aimed at a row's unclipped centre
    // lands on whatever window lies below the popup's edge.
    ImRect clipped = bb;
    if (ctx->CurrentWindow) clipped.ClipWith(ctx->CurrentWindow->ClipRect);
    it.bb = clipped;
    it.visible = clipped.GetWidth() > 0 && clipped.GetHeight() > 0;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                         ImGuiItemStatusFlags) {
    headless::Item& it = headless::items()[id];
    it.id = id;
    if (label) it.label = label;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    const auto it = headless::items().find(id);
    return it == headless::items().end() ? nullptr : it->second.label.c_str();
}
#endif
