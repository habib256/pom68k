// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate): where does an ADB keystroke DIE?
//
// Written 2026-07-31 for the `q605_cudalle_key_etalon` investigation. That
// gate is red because keystrokes never reach the guest's KeyMap, while a
// full ADB trace shows the transport working end to end (the guest polls
// Talk R0 to device 2, our device answers, the Cuda relays, and the guest
// even stamps KeyTime). So the break is inside the guest — and the
// question becomes whether it is the MACHINE or the SYSTEM VERSION.
//
// This probe answers that by running the SAME keystroke scenario against
// any (machine, image) pair and reporting the four low-memory globals that
// mark each stage of the guest's own keyboard pipeline:
//
//   KeyTime ($0186)  the ADB keyboard driver stamped activity
//   KeyLast ($0184)  the Event Manager translated a key
//   KeyMap  ($0174)  8 bytes — the live key bitmap (NOT 16: KeyMap is 8,
//                    and a wider window is what made a dead ADB stack look
//                    half-alive once already)
//   KbdType ($021E)  the keyboard type the System settled on
//
//   POM68K_PROBE_MACHINE  q605 (default) | lcii
//   POM68K_PROBE_IMG      disk image path (default: the machine's usual)
//   POM68K_PROBE_FRAMES   boot frames before typing (default 9000)
//   POM68K_PROBE_WWATCH   hex RAM address: print PC + regs on every guest
//                         write covering it (Q605 only), from first keystroke
//                         on — the tool that answers "WHO stamps KeyTime yet
//                         never touches KeyMap" on Mac OS 8.1
//   POM68K_PROBE_HOLD     frames each key is held (default 6) — the knob
//                         that unmasked Slow Keys: 6-frame taps rejected,
//                         150-frame holds accepted
//   POM68K_PROBE_RETURN_TOGGLE  hold Return N frames post-boot (the Easy
//                         Access gesture, toggles Slow Keys) and report the
//                         engine flag before/after
//   POM68K_PROBE_DUMPADB  hexdump the ADB Manager device table (ADBBase
//                         $0CF8) after boot: addr → (service, data) map
//
// Where the 2026-07-31 hunt ended: the Q605+8.1 cell was dead because the
// IMAGE has Easy Access Slow Keys enabled — Mac OS 8.1 registers its
// acceptance-delay wrapper ($00484A54, globals $00484184) as the keyboard's
// ADB service routine; taps shorter than the delay are rejected (with the
// beep the 2026-07-23 field report describes), releases pass through to the
// classic driver (RAM $D5xx). KeyTime is stamped by the wrapper's periodic
// task with or without keystrokes — never trust it as a delivery observable.

#include "Cpu030.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "V8Memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string find(const std::string& rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

std::vector<uint8_t> readAll(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

// The gate's own sequence: main-row '8'/'.' then the numeric keypad.
const uint8_t kSeq[] = { 0x1C, 0x2F, 0x1C, 0x2F, 0x1C, 0x2F, 0x1C,
                         0x5B, 0x41, 0x5B, 0x41, 0x5B, 0x41, 0x5B };

// One scenario over any machine exposing peek8/keyEvent and a CPU with
// runCycles/isHalted. Reports which stage of the guest pipeline moved.
// FNV-1a over the machine's video memory. The IIsi lesson
// (pom68k-peek-is-physical-rbv): when a low-memory global disagrees with
// what the user sees, believe the pixels. Typing into the Finder selects
// an icon by name, which repaints — so a screen that changes while typing
// and does NOT change while idle is proof the keys landed, whatever
// KeyMap says.
template <class Mem>
uint64_t screenHash(const Mem& mem, size_t bytes) {
    const uint8_t* p = mem.vram();
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < bytes; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

template <class Mem, class Cpu>
int probe(Mem& mem, Cpu& cpu, const char* what, int64_t frameCycles,
          long bootFrames, size_t vramBytes) {
    auto peek16 = [&](uint32_t a) {
        return uint16_t((mem.peek8(a) << 8) | mem.peek8(a + 1));
    };
    auto peek32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    auto keymapBits = [&] {                    // KeyMap is EIGHT bytes
        int n = 0;
        for (int i = 0; i < 8; i++)
            for (int b = 0; b < 8; b++)
                if (mem.peek8(0x0174 + uint32_t(i)) & (1 << b)) n++;
        return n;
    };

    for (long f = 0; f < bootFrames && !cpu.isHalted(); f++)
        cpu.runCycles(frameCycles);
    std::printf("%s: booted, Ticks=%u KbdType=%02X\n",
                what, peek32(0x016A), mem.peek8(0x021E));

    // POM68K_PROBE_DUMPADB=1: hexdump the ADB Manager's device table
    // (ADBBase $0CF8) right after boot, so the addr→(service, data) map the
    // dispatcher actually uses can be read — the question the 8.1 cell poses
    // is whether keyboard packets are routed to the driver at all.
    if (std::getenv("POM68K_PROBE_DUMPADB")) {
        const uint32_t adbBase = peek32(0x0CF8);
        std::printf("%s: ADBBase($0CF8)=$%08X\n", what, adbBase);
        for (int i = 0; i < 0xC0; i++) {
            if (i % 16 == 0) std::printf("  $%08X:", adbBase + uint32_t(i));
            std::printf(" %02X", mem.peek8(adbBase + uint32_t(i)));
            if (i % 16 == 15) std::printf("\n");
        }
    }

    const uint32_t keyTime0 = peek32(0x0186);
    const uint16_t keyLast0 = peek16(0x0184);
    int keymapSeen = 0, keyTimeMoves = 0, keyLastMoves = 0;
    uint32_t keyTimePrev = keyTime0;
    uint16_t keyLastPrev = keyLast0;

    // POM68K_PROBE_RETURN_TOGGLE: hold the Return key ($24) this many frames
    // before the sequence — the Easy Access keyboard gesture that toggles
    // Slow Keys — then report the engine flag so the flip is observable.
    if (const char* rt = std::getenv("POM68K_PROBE_RETURN_TOGGLE")) {
        const int rf = std::atoi(rt);
        std::printf("%s: holding Return %d frames (Slow Keys toggle), flag($484185)=%02X\n",
                    what, rf, mem.peek8(0x484185));
        mem.keyEvent(0x24, true);
        for (int f = 0; f < rf && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
        mem.keyEvent(0x24, false);
        for (int f = 0; f < 120 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
        std::printf("%s: after Return hold, flag($484185)=%02X\n",
                    what, mem.peek8(0x484185));
    }

    // POM68K_PROBE_HOLD: frames each key is held (default 6). The Slow Keys
    // test: an acceptance-delay engine rejects a 6-frame tap but must accept
    // a 150-frame hold.
    const int holdFrames = [&] {
        const char* h = std::getenv("POM68K_PROBE_HOLD");
        return h ? std::atoi(h) : 6;
    }();
    for (uint8_t code : kSeq) {
        mem.keyEvent(code, true);
        for (int f = 0; f < holdFrames && !cpu.isHalted(); f++) {
            cpu.runCycles(frameCycles);
            if (int n = keymapBits(); n > keymapSeen) keymapSeen = n;
        }
        mem.keyEvent(code, false);
        for (int f = 0; f < 6 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
        const uint32_t kt = peek32(0x0186);
        const uint16_t kl = peek16(0x0184);
        if (kt != keyTimePrev) { keyTimeMoves++; keyTimePrev = kt; }
        if (kl != keyLastPrev) { keyLastMoves++; keyLastPrev = kl; }
    }

    std::printf("%s: KeyTime moved %d/%zu, KeyLast moved %d/%zu (now $%04X), "
                "KeyMap peak %d bit(s)\n",
                what, keyTimeMoves, sizeof kSeq, keyLastMoves, sizeof kSeq,
                keyLastPrev, keymapSeen);
    // The verdict this probe exists to give: which stage is the last one
    // that moved. A guest whose driver stamps time but never translates is
    // a very different bug from one that never hears the wire at all.
    const char* verdict =
        keymapSeen                 ? "KEYS FULLY DELIVERED (KeyMap live)" :
        keyLastMoves               ? "translated but KeyMap not maintained" :
        keyTimeMoves               ? "driver hears ADB, Event Manager never translates" :
                                     "nothing reaches the guest at all";
    std::printf("%s: VERDICT — %s\n", what, verdict);

    // ── The observable that cannot lie: does the SCREEN answer? ─────────
    // Baseline first: how much does the display churn on its own over the
    // same span (a blinking caret, the menu-bar clock)? Then the same span
    // with letters typed into the Finder, which selects icons by name.
    // Hash the WHOLE video memory, not a guessed slice: the framebuffer
    // base is guest-programmed and a slice that misses it reports "nothing
    // happened" on a machine where keys demonstrably work — which is
    // exactly what a first attempt did, caught only by the control cell.
    const size_t kVramProbe = vramBytes;
    const uint64_t h0 = screenHash(mem, kVramProbe);
    for (int f = 0; f < 120 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
    const uint64_t hIdle = screenHash(mem, kVramProbe);

    // Cmd-N: the gesture lcii_beyond_etalon already proves is observable —
    // the Finder opens a new folder, which repaints a large region. Typing
    // letters is not: with no matching icon on the desktop, a working
    // keyboard changes nothing at all.
    const uint8_t kCmd = 0x37, kN = 0x2D;
    for (int rep = 0; rep < 2; rep++) {
        mem.keyEvent(kCmd, true);
        for (int f = 0; f < 3 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
        mem.keyEvent(kN, true);
        for (int f = 0; f < 6 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
        mem.keyEvent(kN, false);
        mem.keyEvent(kCmd, false);
        for (int f = 0; f < 45 && !cpu.isHalted(); f++) cpu.runCycles(frameCycles);
    }
    const uint64_t hTyped = screenHash(mem, kVramProbe);
    std::printf("%s: screen idle-span %s, typing-span %s\n", what,
                hIdle == h0 ? "UNCHANGED" : "changed",
                hTyped == hIdle ? "UNCHANGED" : "CHANGED");
    std::printf("%s: PIXEL VERDICT — %s\n", what,
                hTyped != hIdle ? "typing repaints the screen: keys ARRIVE"
                                : (hIdle != h0
                                   ? "inconclusive (screen churns on its own)"
                                   : "typing changes NOTHING: keys are lost"));
    return keymapSeen || keyLastMoves ? 0 : 1;
}

int runQ605(const std::string& img, long bootFrames) {
    std::string rom = find("roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,"
                           "Quadra 605,Performa 475,476,575,577,578.ROM");
    if (rom.empty()) rom = find("roms/mame/macqd605/ff7439ee.bin");
    if (rom.empty() || img.empty()) { std::printf("SKIP: no ROM/image\n"); return 0; }
    std::printf("machine=Quadra605 rom=%s img=%s\n", rom.c_str(), img.c_str());

    static Q605Memory mem(32u << 20);
    if (!mem.loadRom(readAll(rom)) || !mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: bad ROM/image\n");
        return 2;
    }
    static Cpu040 cpu(mem);
    mem.setCpu(&cpu);

    // POM68K_PROBE_WWATCH=<hex>: catch every guest write covering that RAM
    // address and print the writer's PC — then disassemble each new writer
    // site once, so the guest routine can be read without a second run.
    if (const char* ww = std::getenv("POM68K_PROBE_WWATCH")) {
        const uint32_t addr = uint32_t(std::strtoul(ww, nullptr, 16));
        mem.ramWatch_ = addr;
        mem.onRamWrite = [&](uint32_t a, int sz, uint32_t v) {
            static long n = 0;
            static std::vector<uint32_t> seenPc;
            if (n++ > 300) return;
            std::printf("  WWATCH $%08X sz%d = $%0*X pc=$%08X D0=$%08X D1=$%08X "
                        "A0=$%08X A1=$%08X A2=$%08X clk=%lld\n",
                        a, sz, sz * 2, v, cpu.getPC0(), cpu.getD(0), cpu.getD(1),
                        cpu.getA(0), cpu.getA(1), cpu.getA(2),
                        (long long)cpu.getClock());
            const uint32_t pc = cpu.getPC0();
            for (uint32_t s : seenPc) if (s == pc) return;
            if (seenPc.size() >= 8) return;
            seenPc.push_back(pc);
            char da[128];
            uint32_t dpc = pc - 32;
            for (int i = 0; i < 24; i++) {
                int len = 2;
                try { len = cpu.disassemble(da, dpc); }
                catch (...) { std::snprintf(da, sizeof da, "<fault>"); }
                std::printf("   %c $%08X  %s\n", dpc == pc ? '>' : ' ', dpc, da);
                dpc += len;
            }
        };
    }

    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);
    return probe(mem, cpu, "q605", 416667, bootFrames, Q605Memory::kVramSize);
}

int runLcII(const std::string& img, long bootFrames) {
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty() || img.empty()) { std::printf("SKIP: no ROM/image\n"); return 0; }
    std::printf("machine=LCII rom=%s img=%s\n", rom.c_str(), img.c_str());

    static V8Memory mem;
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 2; }
    static Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad image\n"); return 2; }
    while (mem.cpuHeld()) mem.tick(1000);
    return probe(mem, cpu, "lcii", 640 * 407, bootFrames, V8Memory::kVramSize);
}

}  // namespace

int main() {
    const char* m = std::getenv("POM68K_PROBE_MACHINE");
    const char* i = std::getenv("POM68K_PROBE_IMG");
    const char* f = std::getenv("POM68K_PROBE_FRAMES");
    const long frames = f ? std::atol(f) : 9000;
    const bool lcii = m && std::string(m) == "lcii";

    std::string img = i ? find(i) : std::string();
    if (img.empty())
        img = lcii ? find("hdv/GISTPERSO-boot.vhd") : find("hdv/MacOS-8.1-boot.vhd");
    return lcii ? runLcII(img, frames) : runQ605(img, frames);
}
