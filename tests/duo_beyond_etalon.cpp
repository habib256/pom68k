// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the PowerBook Duo 230 (MSC + PG&E, 68030 @ 33 MHz, LCD
// 640×400) — shared-engine gate (BeyondBoot.h). Rig, boot loop and Finder
// signature from duo230_boot_etalon; Time through the Mmu030Peek walk.
// Input is the PG&E's own hardware, both halves (DUO_BRINGUP milestone 4):
// the matrix scanner for the keyboard, and since 2026-08-14 the trackball
// quadrature counters for the pointer — which the persist leg steers into
// the Finder's Special menu, because this machine will not flush its volume
// any other way (the long note above `h.stir`).
// It steers it once more BEFORE that, at the desktop, because the Finder
// being up is not the same thing as the Finder being in front on a volume
// whose Startup Items launch Stickies (the note above `h.focusFinder`).
// POM68K_BEYOND=soak|persist. Soft-skips without the Duo ROM +
// pge_boot.bin + a bootable image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "FinderSignature.h"
#include "Mmu030Peek.h"
#include "MscCpu.h"
#include "MscMemory.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/duo230.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/System 7.5.5 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the Duo ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != MscMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }
    // 4 MB — the Duo 230's factory size, and a rig choice with a measured
    // reason: System 7.5.5 sizes its disk cache from RAM, and on 8 MB it
    // holds the new folder's catalog blocks indefinitely (the Finder
    // creates the folder — a screen dump shows the icon — and the guest
    // issues no write command at all in the two emulated minutes that
    // follow). The Mac II showed the identical split at the identical
    // sizes on its own volume.
    MscMemory mem(pom68k::defaultCoreConfig(), 4u << 20,
                  MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.pgeActive()) {
        std::printf("SKIP: no roms/pge/pge_boot.bin — the PMU cannot boot\n");
        return 0;
    }
    MscCpu cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu);
    mem.setCpu(&cpu);
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    cpu.hardReset();
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = 33000000 / 60;
    const int W = MscMemory::kScreenW, H = MscMemory::kScreenH;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    // A LOGICAL read of a low-memory global: the Duo runs its System behind
    // the PMMU like the RBV machines, so `peek8` (physical, and exactly
    // right for descriptor addresses) has to be walked to it — Mmu030Peek.h.
    // Safe at any point of the boot: with TC.E clear the walk is the
    // identity, so this reads the right byte before the tables exist too.
    auto peekLog = [&](uint32_t va, int n, uint32_t* out) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) {
            uint32_t phys = 0;
            if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(),
                                       va + uint32_t(i), 5,
                                       [&](uint32_t a) { return mem.peek8(a); },
                                       &phys))
                return false;
            v = v << 8 | mem.peek8(phys);
        }
        *out = v;
        return true;
    };
    // Which application is frontmost, in the guest's own words ($910
    // CurApName under MultiFinder). Printed at every boot poll because the
    // one time it mattered, the answer was "Stickies" and nothing was
    // looking — see BeyondBoot.h::persist.
    auto curApp = [&]() {
        return findersig::curApNameAt([&](uint32_t a) -> int {
            uint32_t v = 0;
            return peekLog(a, 1, &v) ? int(v) : -1;
        });
    };
    auto finderUp = [&]() {
        std::vector<uint32_t> fb;
        mem.decodeScreen(fb);
        const double menu = beyondboot::darkRatio(fb, W, 0, W, 2, 18);
        const double desk = beyondboot::darkRatio(fb, W, W / 2, W, 40, H - 40);
        // duo230_boot_etalon's signature, plus "no modal dialog": the 7.5.5
        // volume opens one at every boot ("the alias 'Infinite HD' could
        // not be opened", Stop/Continue) and the two ratios are satisfied
        // with it up — 381 pixels of light run against 47-70 for a clean
        // desktop (BeyondBoot.h::lightRun).
        return menu >= 0.0 && menu < 0.35 && desk > 0.20 && desk < 0.80 &&
               beyondboot::lightRun(fb, W, H) < beyondboot::kDialogRun;
    };
    auto boot = [&]() {
        long held = 0;
        std::map<uint16_t, long> pcs;
        while (mem.cpuHeld() && held < 400000) {
            mem.tick(1000);
            held++;
            if ((held & 0x3F) == 0) pcs[mem.pmu().mcu().pc()]++;
        }
        // Say which of the two ways this can fail actually happened: the
        // PMU never released the 68030, or it did and the machine died
        // afterwards. The persist leg's reboot returned "FAILED" with zero
        // SCSI commands behind it and no way to tell the two apart.
        std::printf("boot: PMU released the 68030 after %ld ticks%s\n", held,
                    mem.cpuHeld() ? " — STILL HELD" : "");
        if (mem.cpuHeld()) {
            M68hc05Pge& m = mem.pmu().mcu();
            std::printf("boot: PG&E pc=$%04X waiting=%d illegal=%d "
                        "(op $%02X at $%04X)\n", m.pc(), m.waiting() ? 1 : 0,
                        m.illegal() ? 1 : 0, m.illegalOp(), m.illegalPc());
            std::vector<std::pair<long, uint16_t>> top;
            for (auto& [pc, n] : pcs) top.push_back({n, pc});
            std::sort(top.rbegin(), top.rend());
            for (size_t i = 0; i < top.size() && i < 6; i++)
                std::printf("  PG&E spins at $%04X x%ld\n", top[i].second,
                            top[i].first);
            return false;
        }
        frames(9000);
        if (cpu.isHalted())
            std::printf("boot: CPU HALTED during the first 9000 frames\n");
        // Poll and dismiss, the shape every other gate on the roster uses.
        // The Duo had no dismissal at all: it ran a fixed budget and
        // declared victory, so the boot alert was still up when the persist
        // gesture started and ate every key of it. 150-frame holds — the
        // engine's rule, and above a Slow Keys acceptance delay.
        for (int poll = 0; poll < 16; poll++) {
            if (cpu.isHalted()) { std::printf("boot: CPU HALTED\n"); return false; }
            if (finderUp()) return true;
            {   // what the signature actually saw, every poll
                std::vector<uint32_t> fb;
                mem.decodeScreen(fb);
                std::printf("boot poll %d: menu %.3f desk %.3f run %d, "
                            "front \"%s\"\n", poll,
                            beyondboot::darkRatio(fb, W, 0, W, 2, 18),
                            beyondboot::darkRatio(fb, W, W / 2, W, 40, H - 40),
                            beyondboot::lightRun(fb, W, H), curApp().c_str());
            }
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(450);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d, TC=$%08X, front \"%s\", SCSI %ld\n", W, H,
                cpu.getTC(), curApp().c_str(), mem.scsi().commands);

    // ── Two dead ends this leg cost, kept so nobody buys them twice ─────
    // It skipped for a year on "the built-in keyboard is a PMU matrix
    // POM68K does not model". The matrix landed 2026-08-13 and that reading
    // was wrong twice over: the keyboard works (this machine dismisses its
    // own boot alert with Return, and a dump at the gesture's peak shows
    // `untitled folder` on the desktop), and the byte-identical image had a
    // second cause the SKIP hid — the machine was already DEAD when the
    // gesture arrived, frozen 58 s in at the power_cycle_w spin.
    // Then it skipped on "the System is holding the writes behind the
    // hard-disk SPIN-DOWN". That was wrong too, and its own evidence says
    // so: the Finder issues eleven READ commands between Cmd and N, so the
    // drive is plainly awake at the moment of the creation. Do NOT model
    // spin-down for this, and do NOT re-try the power key — on a Duo it is
    // the PMU's, and 150 frames of it raise no dialog.
    beyondboot::Hooks h;
    h.name = "PowerBook Duo 230";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.frontApp = curApp;
    // The guest's OWN pointer position (Mouse, $830 = v, $832 = h) — the
    // closed loop lcii_beyond_etalon steers on, which is immune to both
    // System 7's mouse scaling curve and the report rate.
    auto pointer = [&](int* x, int* y) {
        uint32_t h32 = 0, v32 = 0;
        if (!peekLog(0x832, 2, &h32) || !peekLog(0x830, 2, &v32)) return false;
        *x = int16_t(h32);
        *y = int16_t(v32);
        return true;
    };
    h.time = [&](uint32_t* out) { return peekLog(0x20C, 4, out); };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    h.probe = [&]() {
        // Has the guest written ANYTHING since it mounted? A System 7 mount
        // alone clears the volume's clean-unmount bit, so a zero here is a
        // dead write path, not a Finder that did nothing.
        std::fprintf(stderr, "[scsi] write cmds %ld blocks %ld, read cmds %ld\n",
                     mem.scsiDisk().writeCommands, mem.scsiDisk().writeBlocks,
                     mem.scsiDisk().readCommands);
    };
    auto counters = [&](const char* when) {
        std::fprintf(stderr, "[scsi] %s: write cmds %ld, read cmds %ld\n", when,
                     mem.scsiDisk().writeCommands, mem.scsiDisk().readCommands);
    };
    // ── Steering the trackball ──────────────────────────────────────────
    // One delta, then wait for the GUEST to answer it. The counters
    // latching (PgePmu, 60 Hz) says only that the hardware presented the
    // motion; the pointer has moved when the Mouse global says so, and
    // injecting before then builds a backlog that lands as one jump — the
    // first attempt put the pointer on the bottom edge and could not
    // bring it back.
    // ── One trackball unit is not one pixel, so MEASURE it ──────────────
    // System 7 scales what the driver reports and accelerates it, so a
    // 6-unit nudge can move the pointer twenty pixels while a 1-unit one
    // moves it none. The first loop here halved the remaining distance *in
    // units*, which only settles while that scale factor is under 2 — above
    // it the loop is a limit cycle, and where the overshoot meets a screen
    // edge it stops moving at all. Both were measured on 2026-08-15: a
    // 10 px residual on a desktop steer, and — steering to the menu bar
    // from the desktop click, a move the leg never made before the Finder
    // had to be brought to the front — the pointer pinned at y=0 for all
    // 60 iterations, wanting y=12.
    // So `nudge` measures pixels per unit on every move it sees, and the
    // step asks for the delta whose PREDICTED move is the whole remaining
    // distance. An overshoot then costs one correction instead of becoming
    // the steady state. What stays irreducible is the quantum: the smallest
    // delta the guest acts on is 2 units, so no tolerance below ~2×gain is
    // reachable, and every target below is given the tolerance its own size
    // allows.
    double gain = 2.0;                              // pixels per unit
    auto nudge = [&](int dx, int dy) {
        int bx = 0, by = 0;
        pointer(&bx, &by);
        mem.mouseMove(dx, dy);
        for (int i = 0; i < 90; i++) {
            frames(1);
            int x = 0, y = 0;
            if (pointer(&x, &y) && (x != bx || y != by)) {
                const int req = std::abs(dx) + std::abs(dy);
                const int got = std::abs(x - bx) + std::abs(y - by);
                // A move that ended on a screen edge under-reports, hence
                // the EMA rather than the last sample.
                if (req && got) gain = 0.5 * gain + 0.5 * (double(got) / req);
                break;
            }
        }
    };
    auto steer = [&](int tx, int ty, int tol) {
        int px = 0, py = 0;
        for (int it = 0; it < 60; it++) {
            if (!pointer(&px, &py)) return false;
            const int dx = tx - px, dy = ty - py;
            if (std::abs(dx) <= tol && std::abs(dy) <= tol) return true;
            // Minimum step 2: one unit is below System 7's mouse-scaling
            // floor and moves the pointer nowhere, so a loop that ends on
            // ±1 steps never converges — it runs out of iterations 10 px
            // short, which is exactly what the first version did.
            const double g = gain < 1.0 ? 1.0 : gain;
            auto step = [&](int d) {
                int s = int(double(d) / g + (d > 0 ? 0.5 : -0.5));
                if (!s) return 0;
                if (std::abs(s) < 2) s = d > 0 ? 2 : -2;
                return std::max(-8, std::min(8, s));
            };
            nudge(step(dx), step(dy));
        }
        pointer(&px, &py);
        std::printf("steer: wanted (%d,%d), reached (%d,%d), gain %.1f px/unit\n",
                    tx, ty, px, py, gain);
        return false;
    };

    // ── The Finder is up, and it is not in front ────────────────────────
    // This volume's System 7.5 launches **Stickies** from Startup Items,
    // and the boot leaves it frontmost over a perfectly good Finder
    // desktop: the two dark ratios see the desktop it is sitting on, the
    // dialog check sees no dialog (Return dismissed the alias alert on the
    // way in), and every keystroke that follows goes to Stickies — whose
    // File menu answers Cmd-N with a new NOTE. That is where the whole
    // "the Duo creates the folder and never writes it" reading came apart
    // on 2026-08-15: it never created one, and no assertion asked.
    //
    // So do what the user would: click the empty desktop, which activates
    // the Finder. (450,300) is clear of the volume and Trash icons
    // top-right, of the Control Strip bottom-left, and of the middle band
    // a Stickies note opens into. `BeyondBoot.h::persist` then asks the
    // guest who is in front and refuses to gesture at anyone else.
    //
    // Tolerance 30, and that is the target's own size, not a loosened
    // threshold: everything within 30 px of (450,300) is the same empty
    // desktop. Asking a pointer with a several-pixel quantum for 8 is how
    // the first version of this hook reported "never reached the desktop"
    // while sitting 10 px away, on the desktop.
    const int kDeskX = 450, kDeskY = 300;
    h.focusFinder = [&]() {
        beyondboot::mark("focus: click the desktop");
        if (!steer(kDeskX, kDeskY, 30)) {
            std::printf("focus: never reached the desktop\n");
            return;
        }
        mem.mouseButton(true);
        frames(10);
        mem.mouseButton(false);
        frames(60);
        if (h.dump) h.dump("focus");
    };

    // ── Why this machine needs a gesture the other eleven do not ────────
    // The Duo creates the folder and never writes it. Measured 2026-08-14,
    // and this time with the guest's own bookkeeping in view: the Finder
    // reads the catalog off the disk to make the folder (11 READ commands
    // between Cmd and N), the icon appears, the rename commits — and the
    // volume's VCB keeps `vcbFlags` $FF00, the File Manager's own DIRTY
    // bit, for two solid minutes with zero write commands. Not a budget:
    // eight further idle minutes left it set, and a Tab, a Cmd-O and a
    // Cmd-Shift-3 — which pulled 4, 28 and a 28-BLOCK DATA WRITE of its own
    // — left the catalog exactly where it was. Not the volume either: the
    // SAME 7.5.5 image on an LC III writes the catalog in the same frame as
    // the commit and passes its persist leg.
    // It is PowerBook system software keeping the drive still, which is
    // what it is for — so the gate does what this machine's user would
    // have to do, and ends the session through the Finder: Special → Shut
    // Down, with the trackball, which flushes every volume on the way out.
    const int kMenuX = 232, kMenuY = 12;       // the "Special" title
    const int kShutDownY = 141;                // Restart 118..133, this 134..149
    h.stir = [&]() {
        beyondboot::mark("stir: Special -> Shut Down");
        // The title box is ~57x20 and the item band is 16 tall, so 8 and 7
        // are what those two targets can actually be asked for — 7 around
        // 141 is 134..148, inside Shut Down's band from end to end, while
        // the 5 this used to ask for is under the pointer's own quantum and
        // failed the steer every run (it landed on 134 anyway, one pixel
        // from Restart, and the return value was not even looked at).
        if (!steer(kMenuX, kMenuY, 8)) {
            std::printf("stir: never reached the menu bar\n");
            return;
        }
        mem.mouseButton(true);
        frames(60);
        if (!steer(kMenuX, kShutDownY, 7))
            std::printf("stir: Shut Down not reached — releasing anyway\n");
        frames(12);
        if (h.dump) h.dump("menu");
        mem.mouseButton(false);
        frames(900);
        counters("after Shut Down");
        if (h.dump) h.dump("shutdown");
    };
    // A CPU reset, not a machine reset — TRIED and reverted 2026-08-13.
    // `mem.reset()` looks more faithful and is worse here: it wipes the
    // IIfx's PRAM (`rtc_.factoryDefaults()`) and zeroes the Duo's GSC mode
    // registers, and the guest does not rewrite the latter, so the second
    // boot decoded at the wrong depth and the screen came back as noise
    // while the machine was plainly alive behind it. It also did not fix
    // what it was tried for: the IIfx's second boot fails identically
    // either way, and even on a machine rebuilt from scratch.
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        std::vector<uint32_t> fb;
        mem.decodeScreen(fb);
        beyondboot::dumpPpm((std::string("duo_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    return beyondboot::run(h);
}
