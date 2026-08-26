// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LLE_VS_HLE §1.1 gate, step 3 — the row-range invariant, on every decoder
// that has one.
//
// Converting nine whole-frame decoders to `decodeRows(out, y0, y1)` moved
// the row's source offset from an implicit `*ptr++` walk to explicit
// arithmetic — `y × 1024` on the V8, `y × 2048` on VASP, `y × hres × bpp/8`
// on Sonora and RBV. That arithmetic is exactly where a row-ranged decode
// goes wrong, and a boot etalon cannot see it: the Finder looks fine while
// one row in eight comes from the wrong place at some depth nobody boots in.
//
// The invariant, at every depth: decoding a frame in ARBITRARY row chunks
// must be bit-identical to decoding it in one pass. Chunk sizes are
// deliberately ragged (1, 7, 13, 64, …) so no run lines up with a
// power-of-two pitch.
//
// **What this invariant does NOT catch, stated so nobody trusts it too far.**
// `decode()` is implemented as `decodeRows(0, vres)`, so both sides share the
// same pitch arithmetic: a pitch that is wrong but CONSISTENT passes. What it
// catches is the class of bug the conversion actually introduces — a row
// offset that depends on where the chunk started rather than on the absolute
// row (a `*ptr++` walk continued across calls, a `dst` that forgot its `y0`
// offset, a fill that clears rows outside [y0, y1)). Pinning the pitch itself
// needs an INDEPENDENT reference, which is what the RBV block below does by
// comparing an undefined config against the 8 bpp one it must fold into, and
// what `v8_video_test` does with its explicit 1024-byte row check.

#include "RbvMemory.h"
#include "RbvVideo.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"
#include "V8Memory.h"
#include "V8Video.h"
#include "VaspMemory.h"
#include "VaspVideo.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// A deterministic pattern with no period in common with any row pitch.
uint8_t noise(size_t i) { return uint8_t((i * 131 + (i >> 5) * 17 + 7) & 0xFF); }

// Decode the whole frame in one pass, then again in ragged row chunks, and
// compare. The geometry is asked of the DECODER — imposing one here would
// only test that the test guessed the reset monitor sense right.
// `requireContent` makes the harness REFUSE a vacuous comparison: a frame
// that decodes to a single uniform colour compares equal under any pitch, so
// asserting on it proves nothing. That is not hypothetical — the first draft
// of this gate never filled the RBV framebuffer (it is system RAM, not VRAM,
// and low writes are blocked while the boot overlay is up), so every RBV
// check passed against an all-black screen, and a deliberately reintroduced
// pitch bug still passed. Only the blanked-by-design cases opt out.
template <class Video>
bool equivalent(Video& video, bool requireContent = true) {
    int hres = 0, vres = 0;
    video.size(hres, vres);
    if (hres <= 0 || vres <= 0) return false;
    std::vector<uint32_t> whole, chunked;
    video.decode(whole);
    if (whole.size() != size_t(hres) * vres) return false;
    if (requireContent) {
        bool varied = false;
        for (size_t i = 1; i < whole.size(); i++)
            if (whole[i] != whole[0]) { varied = true; break; }
        if (!varied) return false;          // nothing to compare — not a pass
    }

    chunked.assign(size_t(hres) * vres, 0xDEADBEEFu);
    static const int kRuns[] = {1, 7, 13, 64, 3, 100, 2, 31};
    int y = 0, k = 0;
    while (y < vres) {
        int n = kRuns[k++ % 8];
        int end = y + n > vres ? vres : y + n;
        video.decodeRows(chunked, y, end);
        y = end;
    }
    return whole == chunked;
}
} // namespace

int main() {
    std::printf("raster_equiv_test — decodeRows(y0,y1) ≡ decode() (§1.1)\n");

    {   // ── V8 (LC II): fixed 1024-byte row pitch at every depth ──
        V8Memory mem(pom68k::defaultCoreConfig());
        V8Video video(mem);
        for (size_t i = 0; i < V8Memory::kVramSize; i += 7)
            mem.write8(uint32_t(0xF40000 + i), noise(i));
        for (int n = 0; n < 256; n++) {              // fill the Ariel CLUT
            mem.write8(0xF24000, uint8_t(n));
            mem.write8(0xF24001, noise(size_t(n) * 3));
            mem.write8(0xF24001, noise(size_t(n) * 3 + 1));
            mem.write8(0xF24001, noise(size_t(n) * 3 + 2));
        }
        bool all = true;
        for (int depth = 0; depth <= 3; depth++) {   // 1/2/4/8 bpp
            mem.write8(0xF26010, uint8_t(depth));
            if (!equivalent(video)) all = false;
        }
        check(all, "V8 sense 2, 1/2/4/8 bpp: chunked ≡ whole-frame");

        mem.setMonitorSense(6);                      // 640×480
        all = true;
        for (int depth = 0; depth <= 3; depth++) {
            mem.write8(0xF26010, uint8_t(depth));
            if (!equivalent(video)) all = false;
        }
        check(all, "V8 sense 6 (640×480), 1/2/4/8 bpp: chunked ≡ whole-frame");
    }

    {   // ── VASP (IIvx): the same model at a 2048-byte pitch ──
        VaspMemory mem(pom68k::defaultCoreConfig());
        VaspVideo video(mem);
        for (size_t i = 0; i < VaspMemory::kVramSize; i += 7)
            mem.write8(uint32_t(0x60000000 + i), noise(i));
        for (int n = 0; n < 256; n++) {              // Ariel CLUT: reg0/reg1
            mem.write8(0x50F24000, uint8_t(n));
            mem.write8(0x50F24001, noise(size_t(n) * 7));
            mem.write8(0x50F24001, noise(size_t(n) * 7 + 1));
            mem.write8(0x50F24001, noise(size_t(n) * 7 + 2));
        }
        bool all = true;
        for (int depth = 0; depth <= 3; depth++) {
            mem.write8(0x50F26010, uint8_t(depth));
            if (!equivalent(video)) all = false;
        }
        check(all, "VASP 1/2/4/8 bpp: chunked ≡ whole-frame");
    }

    {   // ── RBV (IIsi): packed pitch, HIGH-padded pens, framebuffer in RAM ──
        RbvMemory mem(pom68k::defaultCoreConfig());
        RbvVideo video(mem);
        // The framebuffer IS the start of system RAM, and low writes are
        // ignored while the boot overlay is up. The overlay drops on a read
        // in the ROM range ($40000000+), NOT on a low read — a low read
        // just returns the mirrored ROM (RbvMemory::read8). Getting that
        // wrong is what made the first draft of this block compare black
        // frames against black frames and call it a pass.
        (void)mem.read8(0x40000000);
        for (uint32_t a = 0; a < 640u * 480u; a += 3)
            mem.write8(a, noise(a));
        // The Bt478 here decodes only (low & 3) == 0, with the register in
        // bits 3-2 — so the data port is +$04, not +$01 as on the V8's Ariel.
        for (int n = 0; n < 256; n++) {
            mem.write8(0x50F24000, uint8_t(n));
            mem.write8(0x50F24004, noise(size_t(n) * 5));
            mem.write8(0x50F24004, noise(size_t(n) * 5 + 1));
            mem.write8(0x50F24004, noise(size_t(n) * 5 + 2));
        }
        bool all = true;
        for (int depth = 0; depth <= 3; depth++) {
            mem.write8(0x50F26000 + 0x10, uint8_t(depth));
            if (!equivalent(video)) all = false;
        }
        check(all, "RBV 1/2/4/8 bpp: chunked ≡ whole-frame");

        // Config 4-7 are undefined on the RBV (there is no 16 bpp) and the
        // decode folds them into the 8 bpp `default` — so the row PITCH has
        // to fold with them. Indexing a 4-entry bpp table by the raw config
        // gave a 1 bpp pitch here and sheared the picture. The equivalence
        // check above cannot see that (both sides would share the wrong
        // pitch), so this pins it against an independent reference: an
        // undefined config must render IDENTICALLY to config 3.
        mem.write8(0x50F26000 + 0x10, 3);
        std::vector<uint32_t> ref;
        video.decode(ref);
        all = !ref.empty();
        for (int depth = 4; depth <= 7; depth++) {
            mem.write8(0x50F26000 + 0x10, uint8_t(depth));
            std::vector<uint32_t> got;
            video.decode(got);
            if (got != ref) all = false;
            if (!equivalent(video)) all = false;
        }
        check(all, "RBV undefined configs 4-7 render exactly like 8 bpp");
    }

    {   // ── Sonora (LC III): pitch derived from the modeline, not fixed ──
        SonoraMemory mem(pom68k::defaultCoreConfig());
        SonoraVideo video(mem);
        // Sonora at reset has no modeline, so it decodes to a uniform black
        // frame BY DESIGN — the one place the content guard has to be off,
        // and it is off explicitly rather than by accident.
        check(equivalent(video, /*requireContent=*/false),
              "Sonora at reset (blanked by design): chunked ≡ whole-frame");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
