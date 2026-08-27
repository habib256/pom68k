// POM68K — DAFB-family 68040 platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"

// This composer's own family — see the header's note on the fan-in.
#include "Cpu040.h"
#include "Q605Memory.h"
#include "CentrisMemory.h"
#include "CentrisCpu.h"
#include "Q700Memory.h"
#include "Q700Cpu.h"
#include "Q630Memory.h"
#include "Q630Cpu.h"
#include "VideoBeam.h"
#include "MacAudioHost.h"

#include "GuiRunnerDafb.h"

using pom68k::gui::DafbRunnerSpec;
using pom68k::gui::runDafbGui;

// ── Quadra 605 / LC 475 machine thread ──────────────────────────────────
// Same GUI ↔ machine contract as LcMachine (commands queued, framebuffer +
// status copied out), but the Q605 has no ASC wired in POM68K yet, so the
// pacing is the plain time-budgeted turbo — no audio-clocked path. The
// framebuffer is decoded straight from VRAM using the live screen geometry
// read from the main GDevice's PixMap (same derivation as q605_trace); the
// Mac OS 8.1 Finder comes up 1bpp 640×480, colour modes decode via the
// Antelope CLUT.
// The DAFB-video machine thread — shared by the Quadra 605 / LC 475
// (Q605Memory + Cpu040) and the Centris 610/650 (CentrisMemory + CentrisCpu).
// Both expose the same surface, so one template drives both.
template <class Mem, class Cpu>
struct DafbMachine
    : MachineHost<DafbMachine<Mem, Cpu>, Mem, Cpu, MacAudioHost> {
    using Base = MachineHost<DafbMachine<Mem, Cpu>, Mem, Cpu, MacAudioHost>;
    GuiHostServices& services;
    DafbMachine(Mem& m, Cpu& c, MacAudioHost& a,
                GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          services(hostServices),
          heartbeatEnabled_(hostServices.config().diagnostics().keyTrace),
          freezeProbeEnabled_(
              hostServices.config().diagnostics().freezeProbe) {}
    using Base::mem; using Base::cpu; using Base::fb_; using Base::samp_;
    using Base::framesRun_; using Base::stPc_; using Base::stClock_;
    using Base::stFlags_;
    // The IOSB ASC is the one stereo sound path in the tree.
    static constexpr bool kStereo = true;

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held; int w, h, depth; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0,
                 stW_.load(std::memory_order_relaxed),
                 stH_.load(std::memory_order_relaxed),
                 stDepth_.load(std::memory_order_relaxed) };
    }

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        newFrameGeom();
        auto beam = [this] { rasterBeam(); };
        if (mem.cpuHeld()) { mem.tick(kFrame); beam(); }
        else runQuantumWithWire(services, mem, cpu, kFrame, beam);
        framesRun_++;
        // POM68K_KEY_TRACE heartbeat: proves the machine thread and the
        // guest are still advancing (~1 s of emulated time per line).
        if (heartbeatEnabled_) {
            if (++heartbeatFrames_ % 60 == 0) {
                const uint32_t pc = cpu.getPC();
                std::fprintf(stderr, "[hb] frames=%ld clock=%lld pc=%08X\n",
                             heartbeatFrames_, (long long)cpu.getClock(), pc);
                // Same 64-byte window for 5 consecutive beats (~5 s) in RAM:
                // dump the spin loop once so it can be disassembled.
                if (pc < 0x40000000 && (pc >> 6) == (heartbeatLastPc_ >> 6)) {
                    if (++heartbeatStable_ == 5 && !heartbeatDumped_) {
                        heartbeatDumped_ = true;
                        const uint32_t base = (pc & ~15u) - 16;
                        std::fprintf(stderr, "[hb] spin dump @%08X:", base);
                        for (uint32_t i = 0; i < 48; i++)
                            std::fprintf(stderr, "%s%02X", (i % 16) ? " " :
                                         "\n[hb]   ", mem.peek8(base + i));
                        std::fprintf(stderr, "\n");
                    }
                } else { heartbeatStable_ = 0; }
                heartbeatLastPc_ = pc;
            }
        }
    }

    // Drain interleaved IOSB ASC stereo frames and report real AC content.
    bool drainAudio() {
        samp_.clear();
        int16_t left, right;
        while (mem.asc().popStereo(left, right)) {
            samp_.push_back(float(left) / 32768.0f);
            samp_.push_back(float(right) / 32768.0f);
        }
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }

    void renderFrame(std::vector<uint32_t>& out, int& w, int& h) {
        // `fb_` is the RASTER SURFACE — emulateQuantum() decoded each row as
        // the beam scanned it. Catch up once more so a paused or held machine
        // still publishes a complete frame. decodeRows() already forced the
        // alpha byte, so the copy out needs no fixup.
        newFrameGeom();
        rasterBeam(true);
        w = geom_.w; h = geom_.h;
        out.assign(fb_.begin(), fb_.end());
    }

    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC040() & 0x8000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        freezeProbe(cpu.getPC(), cpu.getSR());
        stW_.store(geom_.w, std::memory_order_relaxed);
        stH_.store(geom_.h, std::memory_order_relaxed);
        stDepth_.store(geom_.depth, std::memory_order_relaxed);
    }

    // Decode the Q605 framebuffer (VRAM at $F9000000) into 00RRGGBB. Screen
    // base and bounds are read live from the main GDevice → PixMap. Pixel
    // depth and stride come from the DAFB hardware registers; the PixMap is
    // only a fallback while the video driver is publishing a new mode.
    // The geometry a frame is scanned with. Resolved ONCE per frame (it
    // costs a walk of the guest's GDevice → PixMap through peek8, far more
    // than the pixels themselves) and then held for every row of that
    // frame — which is also more correct than re-reading it per row: on
    // real hardware the CRTC latches its parameters for the frame.
    struct Geom {
        int w = 0, h = 0, depth = 0;
        uint32_t off = 0, stride = 0;
        bool operator!=(const Geom& o) const {
            return w != o.w || h != o.h || depth != o.depth ||
                   off != o.off || stride != o.stride;
        }
    };

    Geom resolveGeom() {
        int w = 0, h = 0, depth = 0;
        auto pk32 = [&](uint32_t a) {
            return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a+1)) << 16 |
                   uint32_t(mem.peek8(a+2)) << 8 | mem.peek8(a+3);
        };
        uint32_t scrnBase = pk32(0x0824);
        uint32_t mainDevH = pk32(0x08A4);
        uint32_t mainDev  = mainDevH ? pk32(mainDevH) : 0;
        uint32_t pmapH    = mainDev ? pk32(mainDev + 0x16) : 0;
        uint32_t pmap     = pmapH ? pk32(pmapH) : 0;
        uint32_t pmBase = 0, pmRow = 0, pmDepth = 0, pmT = 0, pmL = 0, pmB = 0, pmR = 0;
        if (pmap) {
            pmBase = pk32(pmap + 0x00);
            pmRow  = (pk32(pmap + 0x04) >> 16) & 0x3FFF;
            pmT = (pk32(pmap+0x06)>>16)&0xFFFF; pmL = pk32(pmap+0x06)&0xFFFF;
            pmB = (pk32(pmap+0x0A)>>16)&0xFFFF; pmR = pk32(pmap+0x0A)&0xFFFF;
            // PixMap.pixelSize is at +$20; +$1C is the low half of the vRes
            // Fixed (0 at 72 dpi), which made this fallback dead code.
            pmDepth = (pk32(pmap+0x20)>>16)&0xFFFF;
        }
        // The framebuffer pointer is either the physical VRAM window
        // ($F9000000 + off) or a MMU/alias logical view of it ($5190xxxx —
        // Mac OS 8.1 runs the Quadra 32-bit clean and hands QuickDraw a
        // logical base). The aperture is VRAM-size aligned, so the low
        // log2(kVramSize) bits are the byte offset into VRAM either way —
        // masking works for both forms and skips the leading offscreen band
        // (the "same"/"diff" scratch at VRAM 0 the ROM leaves before the
        // visible screen, which otherwise paints a stray white strip on top).
        uint32_t src = pmBase ? pmBase : scrnBase;
        uint32_t off = src & (Mem::kVramSize - 1);
        w = (pmR > pmL && pmR - pmL <= 1600) ? int(pmR - pmL) : 640;
        h = (pmB > pmT && pmB - pmT <= 1200) ? int(pmB - pmT) : 480;
        // DAFB can select 16 and 24 bpp and Valkyrie 16; dropping them to 1
        // painted 640 columns out of the first 80 bytes of a 1280-byte row.
        auto okDepth = [](uint32_t d) {
            return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 24;
        };
        uint32_t hwDepth = mem.dafbDepth();
        depth = okDepth(hwDepth) ? int(hwDepth)
                                 : (okDepth(pmDepth) ? int(pmDepth) : 1);
        uint32_t minStride = uint32_t((w * depth + 7) / 8);
        uint32_t hwStride = mem.dafbStride();
        uint32_t stride = (hwStride >= minStride && hwStride <= Mem::kVramSize)
                        ? hwStride : (pmRow >= minStride ? pmRow : minStride);
        // Guard a bogus base (before the driver publishes one): the visible
        // screen must fit within VRAM, else fall back to offset 0.
        if (uint64_t(off) + uint64_t(h) * stride > Mem::kVramSize) off = 0;

        return Geom{w, h, depth, off, stride};
    }

    // Render visible rows [y0, y1) of `g` into an existing g.w×g.h surface.
    void decodeRows(std::vector<uint32_t>& out, const Geom& g, int y0, int y1) {
        const int w = g.w, depth = g.depth;
        const uint32_t off = g.off, stride = g.stride;
        if (out.size() < size_t(g.w) * g.h) return;
        y0 = y0 < 0 ? 0 : y0;
        y1 = y1 > g.h ? g.h : y1;
        if (y0 >= y1) return;
        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        auto vb = [&](uint32_t o) -> uint8_t {
            return o < Mem::kVramSize ? vr[o] : 0;
        };
        for (int y = y0; y < y1; y++) {
            uint32_t rowOff = off + uint32_t(y) * stride;
            for (int x = 0; x < w; x++) {
                uint32_t rgb;
                switch (depth) {
                    case 1: { int bit = (vb(rowOff + (x >> 3)) >> (7 - (x & 7))) & 1;
                              const uint8_t* c = cl[bit];   // CLUT, not hardcoded B/W
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 2: { int v = (vb(rowOff + (x >> 2)) >> (6 - 2*(x & 3))) & 3;
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 4: { uint8_t bt = vb(rowOff + (x >> 1));
                              int v = (x & 1) ? (bt & 0xF) : (bt >> 4);
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 16: { uint16_t p = uint16_t(vb(rowOff + 2*x) << 8
                                                   | vb(rowOff + 2*x + 1));
                              rgb = uint32_t(((p>>10)&0x1F)<<19 | ((p>>5)&0x1F)<<11
                                           | (p&0x1F)<<3); break; }   // xRRRRRGGGGGBBBBB
                    case 24: { rgb = uint32_t(vb(rowOff + 4*x + 1))<<16
                                   | uint32_t(vb(rowOff + 4*x + 2))<<8
                                   | vb(rowOff + 4*x + 3); break; }   // xRGB
                    default: { const uint8_t* c = cl[vb(rowOff + x)];   // 8 bpp
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                }
                out[size_t(y) * w + x] = 0xFF000000u | rgb;
            }
        }
    }

    // Advance the beam off the video cell's own frame accumulator (DAFB's
    // Swatch clock, or Valkyrie's) and decode the rows it has crossed —
    // each visible row rendered once, when it is scanned out (LLE_VS_HLE
    // §1.1, VideoBeam.h). The geometry is resolved once per frame, at the
    // wrap, because walking the guest's PixMap costs more than the pixels.
    // `full` = this is the once-per-publish call, so a machine whose CRTC
    // is not programmed yet (Valkyrie before the guest sets a mode, DAFB
    // before its first tick) still gets a picture. Without that flag the
    // fallback would run a whole-frame decode on EVERY slice — 64 of them
    // per frame throughout the POST.
    void rasterBeam(bool full = false) {
        const size_t need = size_t(geom_.w) * geom_.h;
        beam_.setGeometry(mem.frameCycles(), mem.frameActiveCycles(),
                          mem.frameTotalLines(), geom_.h);
        if (!beam_.valid() || need == 0) {
            if (full && need) decodeRows(fb_, geom_, 0, geom_.h);
            beam_.restartFrame();
            return;
        }
        beam_.setPos(mem.framePos(), mem.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(fb_, geom_, a, b); });
    }

    // Re-resolve the frame geometry and, if it moved, restart the frame so
    // no surface is left half-rendered under two different modes.
    void newFrameGeom() {
        const Geom g = resolveGeom();
        const size_t need = size_t(g.w) * g.h;
        if (g != geom_ || fb_.size() != need) {
            geom_ = g;
            fb_.assign(need, 0xFF000000u);
            beam_.restartFrame();
        }
    }

    // ── Freeze probe (POM68K_FREEZE_PROBE=1) ──
    // A guest that looks frozen while the CPU keeps executing is either
    // spinning in a loop or stuck in an interrupt handler that never
    // returns. From outside the two are identical, and telling them apart
    // IS the diagnosis. Sample PC + SR at the publish rate and print the
    // distribution every ~2 s: a live guest spreads over hundreds of
    // addresses, a wedged one collapses onto one or two. SR answers the
    // rest — supervisor bit ($2000) and the interrupt mask (bits 8-10):
    // mask 7 in supervisor on a tight PC range means an interrupt handler
    // that never rearmed, which no guest-side UI action can recover from.
    void freezeProbe(uint32_t pc, uint16_t sr) {
        if (!freezeProbeEnabled_) return;
        probeHist_[pc]++;
        probeSr_ = sr;
        if (++probeSamples_ < 125) return;          // ~2 s at 16 ms
        std::vector<std::pair<uint32_t, int>> top(probeHist_.begin(),
                                                  probeHist_.end());
        std::sort(top.begin(), top.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::fprintf(stderr, "[freeze] %d samples, %zu distinct PC  SR=$%04X "
                     "(%s, IPL mask %u)\n", probeSamples_, probeHist_.size(),
                     probeSr_, (probeSr_ & 0x2000) ? "supervisor" : "user",
                     unsigned((probeSr_ >> 8) & 7));
        for (size_t i = 0; i < top.size() && i < 4; i++)
            std::fprintf(stderr, "[freeze]   PC=$%08X  %d× (%d%%)\n",
                         top[i].first, top[i].second,
                         top[i].second * 100 / probeSamples_);
        // Collapsed onto a handful of addresses = a real spin, not a busy
        // stretch. Dump the loop body and the register file: what the loop
        // polls (which address, which bit) is the whole answer, and a spin
        // waiting on a flag that never sets names the subsystem that owes
        // it. Disassembling live RAM is the only way here — the code is a
        // driver loaded into the system heap, absent from the ROM.
        // Dump once per *loop*, not once per run: boot legitimately spins
        // (ROM device polls) long before the failure under study, and a
        // single global one-shot is always spent on the wrong one. Two
        // dominant PCs within 64 bytes are the same loop; anything further
        // is a new one and earns its own dump, capped at 8.
        bool fresh = true;
        // Signed compare: seen - 64 wraps for a spin PC below $40, which made
        // the neighbourhood test unable to suppress a redump there.
        for (uint32_t seen : probeDumped_) {
            const int64_t d = int64_t(top[0].first) - int64_t(seen);
            if (d > -64 && d < 64) fresh = false;
        }
        if (probeHist_.size() <= 8 && fresh && probeDumped_.size() < 8) {
            probeDumped_.push_back(top[0].first);
            // Window = top[0] plus only the dominant PCs that belong to the
            // SAME loop (within 64 bytes). Spanning min..max of the whole
            // top-4 is what turns one stray far-away sample into millions of
            // disassembly lines.
            uint32_t lo = top[0].first, hi = top[0].first;
            for (size_t i = 1; i < top.size() && i < 4; i++) {
                if (top[i].first + 64 < top[0].first ||
                    top[i].first > top[0].first + 64) continue;
                lo = std::min(lo, top[i].first);
                hi = std::max(hi, top[i].first);
            }
            // Clamp, don't wrap: a spin PC below $18 sent lo to ~$FFFFFFE8,
            // so the loop below never ran and the dump was silently empty —
            // and the PC was recorded as dumped, so it was never retried.
            lo = (lo > 24 ? lo - 24 : 0) & ~1u;
            std::fprintf(stderr, "[freeze] spin loop at $%08X — disassembly:\n",
                         top[0].first);
            char line[256];
            for (uint32_t a = lo; a < hi + 24;) {
                int n = cpu.disassemble(line, a);
                std::fprintf(stderr, "[freeze]   %c $%08X  %s\n",
                             a == top[0].first ? '>' : ' ', a, line);
                a += uint32_t(n > 0 ? n : 2);
            }
            for (int i = 0; i < 8; i++)
                std::fprintf(stderr, "[freeze]   D%d=$%08X  A%d=$%08X\n", i,
                             cpu.getD(i), i, cpu.getA(i));
            std::fprintf(stderr, "[freeze]   SP=$%08X  ISP=$%08X\n",
                         cpu.getSP(), cpu.getISP());
        }
        probeHist_.clear();
        probeSamples_ = 0;
    }

private:
    const int kFrame = int(mem.cpuHz() / 60);
    const bool heartbeatEnabled_;
    const bool freezeProbeEnabled_;
    long heartbeatFrames_ = 0;
    uint32_t heartbeatLastPc_ = 0, heartbeatStable_ = 0;
    bool heartbeatDumped_ = false;

    std::atomic<int> stW_{0}, stH_{0}, stDepth_{0};
    Geom geom_;                    // geometry the current frame is scanned with
    VideoBeam beam_;               // not serialized: pure cache

    std::vector<uint32_t> probeDumped_;
    std::map<uint32_t, int> probeHist_;
    int probeSamples_ = 0;
    uint16_t probeSr_ = 0;
};

using QuadraMachine  = DafbMachine<Q605Memory, Cpu040>;
using CentrisMachine = DafbMachine<CentrisMemory, CentrisCpu>;
using Q700Machine    = DafbMachine<Q700Memory, Q700Cpu>;
using Q630Machine    = DafbMachine<Q630Memory, Q630Cpu>;

 // ── LC 475 / Quadra 605 (Q6): MEMCjr/PrimeTime + 68LC040, selected by a
// 1 MB ROM. Structure mirrors runLcII; the Q605 has no ASC yet (silent).
static int runQuadra(std::vector<uint8_t> rom, const std::string& romName,
                     const std::vector<std::string>& media,
                     GuiHostServices& services, pom68k::SnapMachine selected) {
    // Same FF7439EE ROM, three model identities (MAME macquadra605.cpp):
    // LC 475 ($A55A2221, 68LC040 @ 25), Quadra 605 ($A55A2225, 68040+FPU @ 25)
    // and LC/Performa 575 "Optimus" ($A55A222E, 68LC040 @ 33). POM68K_Q605_ID
    // / POM68K_Q605_NOFPU select; --machine-profile carries both policies;
    // default = LC 475.
    const bool q605 = selected == pom68k::SnapMachine::Q605;
    const bool lc575 = selected == pom68k::SnapMachine::Lc575;
    // A $2225 dump IS a Quadra 605, which has the FPU on die; NOFPU only
    // speaks for the two LC identities. This was already the rule the boot
    // banner applied, and it is now also the rule the CPU window shows.
    const bool fpu = q605 || services.config().cpu().q605Fpu;
    pom68k::CoreConfig core = services.config().core();
    core.cpu.q605Fpu = q605 ? pom68k::Q605FpuMode::Integrated
        : core.cpu.q605Fpu == pom68k::Q605FpuMode::None
            ? pom68k::Q605FpuMode::None : pom68k::Q605FpuMode::Soft68882;
    const int mhz = lc575 ? 33 : 25;
    const std::string cpuLine =
        std::string(fpu ? "68040 @ " : "68LC040 @ ") +
        std::to_string(mhz) + " MHz (Moira + 040 MMU)";
    const DafbRunnerSpec spec{
        q605 ? "Quadra 605" : lc575 ? "Macintosh LC 575" : "Macintosh LC 475",
        q605 ? "q605" : lc575 ? "lc575" : "lc475",
        cpuLine.c_str(), "Cuda 341s0788", MachineKind::Quadra,
        q605 ? pom68k::SnapMachine::Q605
             : lc575 ? pom68k::SnapMachine::Lc575
                     : pom68k::SnapMachine::Lc475};
    std::printf("Machine: %s (68%s040 @ %d MHz, MEMCjr+PrimeTime)\n",
                spec.name.c_str(), fpu ? "" : "LC", mhz);
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    Q605Memory& mem = services.own<Q605Memory>(core, 36u << 20);
    Cpu040& cpu = services.own<Cpu040>(mem, services.config().jit().resolved, core.cpu, core.diagnostics);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runDafbGui<QuadraMachine>(
        mem, cpu, audioHost, spec, mem.cudaLleActive(),
        [&] { mem.setRtcSeconds(services.hostMacSeconds()); mem.cuda().factoryDefaults(); },
        romName, media, services);
}

// ── Centris 610/650 + Quadra 610/650/800: djMEMC + IOSB, one ROM, five
// model identities.
static int runCentris(std::vector<uint8_t> rom, const std::string& romName,
                      const std::vector<std::string>& media,
                      GuiHostServices& services, pom68k::SnapMachine selected) {
    // Shared F1A6F343/F1ACAD13 ROM on the djMEMC + IOSB machine (MAME
    // macquadra800.cpp), five models by POM68K_CENTRIS_MODEL (GUI menu sets
    // it before relaunch; default = Centris 650): Centris 650 (68LC040 @
    // 25 MHz, ID $46) / Centris 610 (20 MHz, $40) / Quadra 650 (full 68040 @
    // 33 MHz, $52) / Quadra 610 (25 MHz, $44) / Quadra 800 (full 68040 @
    // 33 MHz, $12 — same board plus SONIC Ethernet and three NuBus slots,
    // neither of which the boot path binds). POM68K_CENTRIS610 = legacy
    // alias for c610.
    const bool c610 = selected == pom68k::SnapMachine::Centris610;
    const bool q650 = selected == pom68k::SnapMachine::Quadra650;
    const bool q610 = selected == pom68k::SnapMachine::Quadra610;
    const bool q800 = selected == pom68k::SnapMachine::Quadra800;
    const std::string cmodel = c610 ? "c610" : q650 ? "q650" :
                               q610 ? "q610" : q800 ? "q800" : "c650";
    pom68k::CoreConfig core = services.config().core();
    const bool fpu = q650 || q610 || q800 || core.cpu.centrisFull040;
    core.cpu.centrisFull040 = fpu;
    struct CInfo { const char* name; int mhz; int64_t hz; uint8_t pins;
                   pom68k::SnapMachine snap; };
    const CInfo cinfo =
          q800 ? CInfo{"Quadra 800", 33, CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra800, pom68k::SnapMachine::Quadra800}
        : q650 ? CInfo{"Quadra 650", 33, CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra650, pom68k::SnapMachine::Quadra650}
        : q610 ? CInfo{"Quadra 610", 25, CentrisMemory::kCpuHzQ610, CentrisMemory::kIdQuadra610, pom68k::SnapMachine::Quadra610}
        : c610 ? CInfo{"Centris 610", 20, CentrisMemory::kCpuHz610, CentrisMemory::kIdCentris610, pom68k::SnapMachine::Centris610}
               : CInfo{"Centris 650", 25, CentrisMemory::kCpuHz650, CentrisMemory::kIdCentris650, pom68k::SnapMachine::Centris650};
    const std::string cpuLine =
        std::string(fpu ? "68040 @ " : "68LC040 @ ") +
        std::to_string(cinfo.mhz) + " MHz (Moira + 040 MMU)";
    const DafbRunnerSpec spec{cinfo.name, cmodel.c_str(), cpuLine.c_str(),
                              "ADB PIC1654S 342s0440-b", MachineKind::Centris,
                              cinfo.snap};
    std::printf("Machine: Macintosh %s (68%s040 @ %d MHz, djMEMC+IOSB)\n",
                spec.name.c_str(), fpu ? "" : "LC", cinfo.mhz);
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    CentrisMemory& mem = services.own<CentrisMemory>(core, 36u << 20, cinfo.hz, cinfo.pins);
    CentrisCpu& cpu = services.own<CentrisCpu>(mem, services.config().jit().resolved, core.cpu);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runDafbGui<CentrisMachine>(
        mem, cpu, audioHost, spec, mem.adbLleActive(),
        [&] { mem.rtc().setSeconds(services.hostMacSeconds()); },
        romName, media, services);
}

// ── Quadra 700 ("Spike") and the Eclipse towers (Quadra 900/950).
static int runQ700(std::vector<uint8_t> rom, const std::string& romName,
                   const std::vector<std::string>& media,
                   GuiHostServices& services, pom68k::SnapMachine selected) {
    // Macintosh Quadra 700 ("Spike", MAME macquadra700.cpp): the first
    // Quadra — a full 68040 @ 25 MHz on discrete chips. Mac II VIA1/VIA2 +
    // 343-0042 RTC + PIC1654S ADB in front, Quadra DAFB/53C96/SWIM1/EASC
    // behind, SCSI through DAFB's own TurboSCSI cell. $420DBFF3 ROM.
    //
    // The same board carries the "Eclipse"/"Zydeco" towers (Quadra 900/950,
    // docs/IOP_BRINGUP.md § M7): the Mac IIfx front end grafted on — two
    // Apple PIC IOPs, the Egret instead of the discrete RTC, a second 53C96
    // bus. POM68K_Q700_MODEL picks the variant (the GUI menu sets it before
    // relaunch); the Q950 ROM forces its own model, because a $3DC27823 dump
    // IS a Quadra 950 whatever the environment inherited from the last run.
    const bool q900 = selected == pom68k::SnapMachine::Quadra900;
    const bool q950 = selected == pom68k::SnapMachine::Quadra950;
    const auto qkind = q950 ? Q700Memory::Model::Q950
                     : q900 ? Q700Memory::Model::Q900
                            : Q700Memory::Model::Spike;
    const int64_t qhz = q950 ? Q700Memory::kCpuHzQ950 : Q700Memory::kCpuHz;
    const char* qname = q950 ? "Quadra 950" : q900 ? "Quadra 900"
                                                   : "Quadra 700";
    const std::string cpuLine =
        "68040 @ " + std::to_string(qhz / 1000000) +
        " MHz (Moira + 040 MMU)";
    const DafbRunnerSpec spec{
        qname, q950 ? "q950" : q900 ? "q900" : "q700", cpuLine.c_str(),
        qkind == Q700Memory::Model::Spike ? "ADB PIC1654S 342s0440-b"
                                          : "Egret 341s0851",
        MachineKind::Q700,
        q950 ? pom68k::SnapMachine::Quadra950
             : q900 ? pom68k::SnapMachine::Quadra900
                    : pom68k::SnapMachine::Q700};
    std::printf("Machine: Macintosh %s (68040 @ %lld MHz, %s)\n",
                spec.name.c_str(),
                (long long)(qhz / 1000000),
                q900 || q950 ? "discret + IOP Apple PIC" : "discret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    Q700Memory& mem = services.own<Q700Memory>(
        services.config().core(), 32u << 20, qhz, qkind);
    Q700Cpu& cpu = services.own<Q700Cpu>(
        mem, services.config().jit().resolved, services.config().core().cpu);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runDafbGui<Q700Machine>(
        mem, cpu, audioHost, spec, mem.adbLleActive(),
        // On the Eclipse there is no discrete RTC in the loop: the Egret
        // keeps time and runs its own second counter.
        [&] {
            if (mem.eclipse()) mem.setEgretSeconds(services.hostMacSeconds());
            else               mem.rtc().setSeconds(services.hostMacSeconds());
        },
        romName, media, services);
}

// ── Quadra 630 / LC 580: F108 + PrimeTime II + Valkyrie.
static int runQ630(std::vector<uint8_t> rom, const std::string& romName,
                   const std::vector<std::string>& media,
                   GuiHostServices& services, pom68k::SnapMachine selected) {
    // Macintosh Quadra 630 / LC 580 ("Show and Tell", MAME macquadra630.cpp):
    // the last 68k desktop board — F108 memory controller + PrimeTime II I/O
    // + the fixed-mode Valkyrie framebuffer + Cuda 341S0060, 68040 @ 33 MHz.
    // POM68K_Q630_ID picks the identity ($A55A2252 = Quadra 630 by default,
    // $A55A225A = LC/Performa 580). $06684214 / $064DC91D ROMs.
    const bool lc580 = selected == pom68k::SnapMachine::Lc580;
    pom68k::CoreConfig core = services.config().core();
    core.cpu.q630Lc040 = lc580 || core.cpu.q630Lc040;
    const DafbRunnerSpec spec{
        lc580 ? "LC 580" : "Quadra 630",
        lc580 ? "lc580" : "q630",
        "68040 @ 33 MHz (Moira + 040 MMU)", "Cuda 341s0060",
        MachineKind::Q630,
        lc580 ? pom68k::SnapMachine::Lc580 : pom68k::SnapMachine::Q630};
    std::printf("Machine: Macintosh %s (68040 @ 33 MHz, F108 + Valkyrie)\n",
                spec.name.c_str());
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    Q630Memory& mem = services.own<Q630Memory>(core, 32u << 20);
    Q630Cpu& cpu = services.own<Q630Cpu>(
        mem, services.config().jit().resolved, core.cpu);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runDafbGui<Q630Machine>(
        mem, cpu, audioHost, spec, mem.cudaLleActive(),
        // The Cuda holds the clock on this board (no discrete RTC).
        [&] { mem.setRtcSeconds(services.hostMacSeconds()); },
        romName, media, services);
}


int pom68k::gui::composeDafb(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    switch (launch.platform) {
    case pom68k::PlatformKind::MemcJr:
        return runQuadra(std::move(launch.rom), launch.romName, launch.media,
                         services, launch.selected);
    case pom68k::PlatformKind::DjMemc:
        return runCentris(std::move(launch.rom), launch.romName, launch.media,
                          services, launch.selected);
    case pom68k::PlatformKind::Spike:
        return runQ700(std::move(launch.rom), launch.romName, launch.media,
                       services, launch.selected);
    case pom68k::PlatformKind::F108:
        return runQ630(std::move(launch.rom), launch.romName, launch.media,
                       services, launch.selected);
    default:
        return 1;
    }
}
