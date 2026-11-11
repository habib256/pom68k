// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Compact-board peripheral deadline gate (Cpu68k::catchUp,
// MacMemory::cyclesToNextEvent). The claim under test: feeding the board
// at its next event, and before every device access, leaves the machine
// byte-identical to feeding it at every bus access.
//
// Two Mac Plus machines run the same ROM on the interpreter, frame by frame
// in MacFrameClock's own shape (sixteen display slices, the vertical blank
// raised between two runs). The reference has batching switched off
// (Cpu68k::setPeriphBatching) and ticks at every access; the other batches.
// Both receive the same mouse deltas, button presses and key transitions at
// the same slice boundaries. After every slice the CPU registers and clock
// are compared, and after every frame the complete serialized machine —
// RAM, VIA, SCC, IWM, drives, RTC, keyboard, mouse, the board's own
// accumulators — and the CPU chunk, byte for byte, plus the 370 per-line
// sound latches.
//
// Two passes: with the boot floppy (the IWM stream is the state the bound
// refuses to batch, so the pass crosses in and out of batching many times)
// and ROM only (the blinking-disk loop, where the board idles and batching
// is at its widest). Soft-skips without the Mac Plus ROM.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include "SaveState.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

struct Machine {
    MacMemory mem;
    Cpu68k cpu;
    explicit Machine(const jit::ResolvedConfig& jitConfig)
        : mem(pom68k::defaultCoreConfig()), cpu(mem, jitConfig) {
        mem.setCpu(&cpu);
    }
    std::vector<sav::u8> bytes() {
        std::vector<sav::u8> out;
        sav::Writer ar(out);
        ar(mem);
        ar(cpu);
        return out;
    }
    bool sameCpu(const Machine& o) const {
        if (cpu.getClock() != o.cpu.getClock() || cpu.getPC() != o.cpu.getPC() ||
            cpu.getSR() != o.cpu.getSR()) return false;
        for (int r = 0; r < 8; r++)
            if (cpu.getD(r) != o.cpu.getD(r) || cpu.getA(r) != o.cpu.getA(r))
                return false;
        return true;
    }
};

// Deterministic host input, keyed on the frame and the slice it lands in.
void inject(MacMemory& mem, long frame, int slice) {
    if (frame % 7 == 3 && slice == 5) mem.mouseMove(int(frame % 23) - 11, int(frame % 13) - 6);
    if (frame % 31 == 9 && slice == 11) mem.mouseMove(40, -25);
    if (frame % 50 == 20 && slice == 2) mem.mouseButton(true);
    if (frame % 50 == 24 && slice == 9) mem.mouseButton(false);
    if (frame % 40 == 12 && slice == 7) mem.keyEvent(uint8_t(frame % 48), true);
    if (frame % 40 == 15 && slice == 1) mem.keyEvent(uint8_t((frame - 3) % 48), false);
}

// A threshold nearer than the armed deadline, created by a device write:
// VIA timer 2 loaded with 3 (an interrupt some 40 CPU cycles on) while the
// board's deadline is the next sound line, up to 352 cycles away. The
// handler samples the free-running timer 1, so an interrupt delivered late
// reads a different count. One runCycles() call: no host boundary helps.
bool shortTimer(Machine& ref, Machine& bat, const char* name) {
    static const uint16_t main_[] = {
        0x13FC, 0x007F, 0x00EF, 0xFDFE,     // move.b #$7F,IER  (all off)
        0x13FC, 0x00A0, 0x00EF, 0xFDFE,     // move.b #$A0,IER  (timer 2 on)
        0x13FC, 0x0003, 0x00EF, 0xF1FE,     // move.b #3,T2C-L
        0x13FC, 0x0000, 0x00EF, 0xF3FE,     // move.b #0,T2C-H  (starts)
        0x46FC, 0x2000,                     // move.w #$2000,sr (unmask)
        0x5281, 0x60FC,                     // loop: addq.l #1,d1 ; bra loop
    };
    static const uint16_t handler[] = {
        0x1039, 0x00EF, 0xE9FE,             // move.b T1C-L,d0
        0x1439, 0x00EF, 0xF1FE,             // move.b T2C-L,d2  (acknowledge)
        0x5283, 0x4E73,                     // addq.l #1,d3 ; rte
    };
    for (Machine* m : { &ref, &bat }) {
        auto put = [&](uint32_t a, const uint16_t* w, size_t n) {
            for (size_t i = 0; i < n; i++) {
                m->mem.write8(a + uint32_t(i) * 2, uint8_t(w[i] >> 8));
                m->mem.write8(a + uint32_t(i) * 2 + 1, uint8_t(w[i]));
            }
        };
        put(0x3000, main_, sizeof main_ / 2);
        put(0x3100, handler, sizeof handler / 2);
        static const uint16_t vec[] = { 0x0000, 0x3100 };
        put(0x64, vec, 2);                  // level-1 autovector
        m->cpu.setSR(0x2700);               // masked until the timer runs
        m->cpu.setSP(0x8000);
        m->cpu.setPC(0x3000); m->cpu.setPC0(0x3000);
        m->cpu.setIRD(main_[0]); m->cpu.setIRC(main_[1]);
        m->cpu.setD(0, 0); m->cpu.setD(1, 0); m->cpu.setD(3, 0);
        m->cpu.runCycles(3000);
    }
    const bool taken = ref.cpu.getD(3) == 1 && ref.cpu.getD(1) > 50;
    const bool same = ref.sameCpu(bat) && ref.bytes() == bat.bytes();
    if (!taken || !same)
        std::printf("  FAIL %s: short timer — interrupts %u, timer-1 sample "
                    "%02X / %02X, clock %lld / %lld\n", name, ref.cpu.getD(3),
                    ref.cpu.getD(0) & 0xFF, bat.cpu.getD(0) & 0xFF,
                    (long long)ref.cpu.getClock(), (long long)bat.cpu.getClock());
    return taken && same;
}

bool runPass(const std::vector<uint8_t>& rom, const std::string& disk,
             long frames, const char* name, bool mustBatch) {
    static const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    auto ref = std::make_unique<Machine>(jitConfig);
    auto bat = std::make_unique<Machine>(jitConfig);
    if (!ref->mem.loadRom(rom) || !bat->mem.loadRom(rom)) {
        std::printf("SKIP: [compact_deadline] loadRom failed\n");
        return true;
    }
    if (!disk.empty()) { ref->mem.insertDisk(disk); bat->mem.insertDisk(disk); }
    for (Machine* m : { ref.get(), bat.get() }) {
        m->cpu.setEngine(0);
        m->cpu.hardReset();
    }
    ref->cpu.setPeriphBatching(false);

    constexpr int kSlices = 16;
    int64_t frameBase = 0;
    for (long f = 0; f < frames; f++) {
        for (int i = 1; i <= kSlices + 1; i++) {
            const int64_t target = i <= kSlices
                ? frameBase + kVblankStart * i / kSlices
                : frameBase + kCyclesPerFrame;
            if (i == kSlices + 1)
                for (Machine* m : { ref.get(), bat.get() }) {
                    m->mem.via().raiseCa1();             // vertical blank
                    m->mem.updateIrq();
                }
            for (Machine* m : { ref.get(), bat.get() }) {
                inject(m->mem, f, i);
                m->cpu.runUntil(target);
            }
            if (!ref->sameCpu(*bat)) {
                std::printf("  FAIL %s: CPU diverged in frame %ld slice %d "
                            "(clock %lld / %lld, pc %08X / %08X)\n", name, f, i,
                            (long long)ref->cpu.getClock(), (long long)bat->cpu.getClock(),
                            ref->cpu.getPC(), bat->cpu.getPC());
                return false;
            }
        }
        frameBase += kCyclesPerFrame;
        // The per-line sound latches are derived, not serialized, and they
        // are what the audio renders: sample byte and VIA volume/enable bits
        // as they stood when the beam fetched each line.
        for (int line = 0; line < 370; line++) {
            const auto& a = ref->mem.soundLine(line);
            const auto& b = bat->mem.soundLine(line);
            if (std::memcmp(&a, &b, sizeof a) != 0) {
                std::printf("  FAIL %s: sound latch of line %d differs in frame %ld\n",
                            name, line, f);
                return false;
            }
        }
        if (ref->bytes() != bat->bytes()) {
            std::printf("  FAIL %s: serialized machines differ after frame %ld\n",
                        name, f);
            return false;
        }
    }
    if (mustBatch && !shortTimer(*ref, *bat, name)) return false;
    const long refTicks = ref->cpu.periphTicks, batTicks = bat->cpu.periphTicks;
    std::printf("  %-10s %ld frames identical · board ticks %ld per-access, "
                "%ld batched (1 in %.1f) · overlay %s · IWM polls %ld\n",
                name, frames, refTicks, batTicks,
                batTicks ? double(refTicks) / double(batTicks) : 0.0,
                ref->mem.overlay() ? "up" : "down", ref->mem.iwm().dataReads);
    // Not vacuous: batching must actually have removed ticks.
    if (mustBatch && batTicks * 2 > refTicks) {
        std::printf("  FAIL %s: batching removed fewer than half the ticks\n", name);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    long frames = 2600;
    if (argc > 1) frames = std::atol(argv[1]);
    const std::string romPath = testasset::findAny({ "roms/macplus.rom",
                                                     "roms/mac128k/macplus.rom" });
    if (romPath.empty()) {
        std::printf("SKIP: [compact_deadline] no Mac Plus ROM\n");
        return 0;
    }
    const std::string diskPath = testasset::findAny({ "disks35/Disk605.dsk" });
    testasset::report({ romPath, diskPath });
    const std::vector<uint8_t> rom = readFile(romPath);
    std::printf("compact_deadline_etalon — per-access ticks against deadlines\n");
    bool ok = runPass(rom, "", frames, "ROM only", true);
    // The floppy pass runs on to the Finder, where the keyboard and mouse
    // drivers are live and the scripted input reaches them.
    if (!diskPath.empty()) ok = runPass(rom, diskPath, frames * 5 / 2, "floppy", false) && ok;
    else std::printf("  (no boot floppy: floppy pass not run)\n");
    std::printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
