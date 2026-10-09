// Original mixed bit/flux capture: Finder launch, galaxy selection, flight
// input and fresh-machine replay. No ROM/program patches or host file calls.
#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "SaveStateMachines.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

using Bytes = std::vector<uint8_t>;
namespace {
struct Machine {
    MacMemory mem{pom68k::defaultCoreConfig()};
    Cpu68k cpu;
    MacFrameClock clock;
    MacVideo video;
    Machine(const Bytes& rom, const jit::ResolvedConfig& config) : cpu(mem, config) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset(); clock.resync(cpu);
    }
    void run(int frames) {
        for (int f = 0; f < frames && !cpu.isHalted(); ++f)
            clock.runFrame(cpu, mem, [&] { video.raster(mem); });
    }
    Bytes pixels() {
        const auto* fb = video.raster(mem);
        Bytes result(512 * 342);
        for (size_t i = 0; i < result.size(); ++i) result[i] = (fb[i] & 255) > 128;
        return result;
    }
    void dump(const char* phase) {
        const char* prefix = std::getenv("POM68K_OIDS_DUMP");
        if (!prefix) return;
        std::ofstream out(std::string(prefix) + "-" + phase + ".pgm", std::ios::binary);
        out << "P5\n512 342\n255\n";
        for (auto p : pixels()) out.put(char(p ? 255 : 0));
    }
    int word(int address) const {
        return int16_t(uint16_t(mem.peek8(address)) << 8 | mem.peek8(address + 1));
    }
    bool steer(int x, int y) {
        for (int i = 0; i < 800; ++i) {
            int dx = x - word(0x832), dy = y - word(0x830);
            if (!dx && !dy) return true;
            auto step = [](int d) { return std::clamp(d / 2 ? d / 2 : (d > 0 ? 1 : -1), -8, 8); };
            mem.mouseMove(dx ? step(dx) : 0, dy ? step(dy) : 0); run(1);
        }
        return false;
    }
    bool click(int x, int y, int settle) {
        if (!steer(x, y)) return false;
        mem.mouseButton(true); run(3); mem.mouseButton(false); run(3); run(settle);
        return true;
    }
    void command(uint8_t key, int settle) {
        mem.keyEvent(0x37, true); run(6); mem.keyEvent(key, true); run(12);
        mem.keyEvent(key, false); run(6); mem.keyEvent(0x37, false); run(settle);
    }
    Bytes save() {
        Bytes bytes; pom68k::save(mem, cpu, pom68k::SnapMachine::Plus, bytes); return bytes;
    }
    bool load(const Bytes& bytes) {
        std::string error;
        if (!pom68k::load(mem, cpu, pom68k::SnapMachine::Plus, bytes.data(), bytes.size(), error)) {
            std::fprintf(stderr, "FAIL: fresh restore: %s\n", error.c_str()); return false;
        }
        clock.resync(cpu); video = MacVideo{}; return true;
    }
    Bytes flight(bool thrust) {
        if (thrust) mem.keyEvent(0x3a, true); // Option: this capture's thrust control, over M0110.
        run(180);
        const bool delivered = (mem.peek8(0x17b) & 0x04) != 0;
        std::printf("flight: thrust=%d, guest Option=%d\n", thrust, delivered);
        if (thrust) mem.keyEvent(0x3a, false);
        run(6);
        Bytes visible(512 * 342);
        // Accumulate displayed pixels across a second, so the blitter's
        // instantaneous clear/redraw phase cannot masquerade as flight.
        for (int f = 0; f < 60; ++f) {
            run(1); const auto frame = pixels();
            for (size_t i = 0; i < visible.size(); ++i) visible[i] |= frame[i];
        }
        dump(thrust ? "thrust" : "neutral");
        if (thrust && (!delivered || (mem.peek8(0x17b) & 0x04))) return {};
        return visible;
    }
};
bool check(bool value, const char* description) {
    std::printf("%s: %s\n", value ? "ok" : "FAIL", description); std::fflush(stdout); return value;
}
}
int main(int argc, char** argv) {
    const std::string romPath = testasset::find("roms/macplus.rom");
    const std::string boot = testasset::find("disks35/Disk605.moof");
    const std::string game = testasset::find(argc > 1 ? argv[1] : "disks35/Oids v1.4.moof");
    if (romPath.empty() || boot.empty() || game.empty()) {
        std::printf("SKIP: needs Plus ROM, Disk605.moof and original Oids v1.4.moof\n"); return 0;
    }
    testasset::report({romPath, boot, game});
    const std::string digest = testasset::sha256File(game);
    if (!check(digest == "63580ede7817cbf72bd8c053736707dcb8784f0eff2ac8de314606e173bb5a6f",
               "exact mixed bit/flux source capture, not a substituted image")) return 1;
    std::ifstream in(romPath, std::ios::binary);
    const Bytes rom((std::istreambuf_iterator<char>(in)), {});
    const auto config = testjit::resolveFromEnvironment();
    auto original = std::make_unique<Machine>(rom, config);
    auto& a = *original;
    const bool useJit = config.engineForGuest(false) == jit::EngineKind::Jit;
    std::printf("CPU: %s\n", useJit ? a.cpu.jit().backendName() : "interpreter");
    if (!a.mem.internalDrive().insert(boot) || !a.mem.externalDrive().insert(game)) return 1;
    a.run(4500); a.dump("finder");
    if (!check(findersig::curApName(a.mem) == "Finder", "real System 6 Finder runs with the game mounted")) return 1;
    const long mountedReads = a.mem.externalDrive().nibblesRead;
    // Coordinates belong to this immutable capture's saved Finder layout.
    if (!a.click(124, 140, 30)) return 1;
    a.command(0x1f, 4800); a.dump("title");
    if (!check(findersig::curApName(a.mem) == "OIDS" &&
               a.mem.externalDrive().nibblesRead > mountedReads && !a.cpu.isHalted(),
               "Finder launches OIDS and reads application resources through the IWM")) return 1;
    if (!a.click(255, 234, 3000)) return 1;
    a.dump("galaxies");
    const long beforePlay = a.mem.externalDrive().nibblesRead;
    if (!a.click(95, 264, 3000)) return 1; // Load, warp animation, then the playable scene.
    a.dump("play");
    const auto scene = a.pixels();
    const long light = std::count(scene.begin(), scene.end(), 1);
    std::printf("play: app=%s light=%ld floppy reads=%ld -> %ld\n",
                findersig::curApName(a.mem).c_str(), light, beforePlay, a.mem.externalDrive().nibblesRead);
    if (!check(findersig::curApName(a.mem) == "OIDS" && !a.cpu.isHalted() &&
               a.mem.externalDrive().nibblesRead > beforePlay && light > 1000 && light < 30000,
               "Play loads a galaxy and displays its terrain/HUD")) return 1;
    const auto start = a.save();
    const auto controlled = a.flight(true);
    const auto future = a.save();
    auto fresh = std::make_unique<Machine>(rom, config);
    auto& b = *fresh;
    if (!check(b.load(start) && b.save() == start, "full game state restores byte-identically into a fresh Macintosh")) return 1;
    if (!check(b.flight(true) == controlled && b.save() == future && !controlled.empty(),
               "same flight input reproduces pixels and complete machine state after restore")) return 1;
    if (!b.load(start)) return 1;
    const auto neutral = b.flight(false);
    long changed = 0, added = 0;
    for (size_t i = 20 * 512; i < controlled.size(); ++i) {
        changed += controlled[i] != neutral[i]; added += controlled[i] && !neutral[i];
    }
    std::printf("flight image: %ld changed, %ld added displayed pixels versus equal-time neutral branch\n", changed, added);
    if (!check(changed > 1000 && added > 200 && findersig::curApName(b.mem) == "OIDS" && !b.cpu.isHalted(),
               "thrust causes visible flight beyond ordinary elapsed-time animation")) return 1;
    if (!check(!a.mem.externalDrive().dirty() && !b.mem.externalDrive().dirty() &&
               !a.mem.externalDrive().writeBackEnabled() && testasset::sha256File(game) == digest,
               "original game capture remains immutable")) return 1;
    const auto stats = a.cpu.jit().stats().snapshot();
    std::printf("JIT execution: %llu window instructions, %llu blocks\n",
                static_cast<unsigned long long>(stats.windowInstrs),
                static_cast<unsigned long long>(stats.blocksRun));
    if (!check(!useJit || stats.windowInstrs > 0 || stats.blocksRun > 0,
               "selected JIT actually executes its code-window or block fast path")) return 1;
    std::printf("PASSED: original Oids capture launch, flight and fresh-machine replay\n"); return 0;
}
