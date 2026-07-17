// Scratch diagnostic — LC II arrow-key path. Boots the etalon image to
// the Finder, injects ADB key codes through AdbBus::keyEvent (the same
// entry point the GUI uses), and reads back the System's KeyMap low-mem
// global ($174, 16 bytes = 128 bits, one per virtual key code) that
// GetKeys copies out. Control = 'a' ($00); probes = arrows $3B-$3E.

#include "V8Memory.h"
#include "Cpu030.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

static void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

int main() {
    std::string rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) { std::printf("SKIP\n"); return 0; }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    V8Memory mem;
    if (!mem.loadRom(romData)) return 1;
    Cpu030 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) return 1;
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;
    for (long f = 0; f < 16000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "halted\n"); return 1; }

    auto rd = [&](uint32_t a) { return mem.peek8(a); };
    auto dumpKeymap = [&](const char* tag) {
        std::printf("%-14s KeyMap:", tag);
        for (int i = 0; i < 16; i++) std::printf(" %02X", rd(0x174 + i));
        std::printf("\n");
    };
    auto probe = [&](const char* name, uint8_t code) {
        uint8_t before[16];
        for (int i = 0; i < 16; i++) before[i] = rd(0x174 + i);
        mem.adb().keyEvent(code, true);
        for (long f = 0; f < 60; f++) cpu.runCycles(kFrame);
        int changed = -1;
        for (int i = 0; i < 16; i++) if (rd(0x174 + i) != before[i]) changed = i;
        dumpKeymap(name);
        mem.adb().keyEvent(code, false);
        for (long f = 0; f < 60; f++) cpu.runCycles(kFrame);
        bool idle = true;
        for (int i = 0; i < 16; i++) if (rd(0x174 + i) != before[i]) idle = false;
        std::printf("%-14s code $%02X: down changed byte %d, back to idle %d\n",
                    name, code, changed, idle ? 1 : 0);
        return changed >= 0 && idle;
    };

    dumpKeymap("baseline");
    // raw ADB -> virtual: press each code, find which KeyMap bit appears
    auto rawToVirt = [&](uint8_t code) {
        uint8_t before[16];
        for (int i = 0; i < 16; i++) before[i] = rd(0x174 + i);
        mem.adb().keyEvent(code, true);
        for (long f = 0; f < 40; f++) cpu.runCycles(kFrame);
        int virt = -1;
        for (int i = 0; i < 16; i++) {
            uint8_t d = uint8_t(rd(0x174 + i) ^ before[i]);
            for (int b = 0; b < 8; b++) if (d & (1 << b)) virt = i * 8 + b;
        }
        mem.adb().keyEvent(code, false);
        for (long f = 0; f < 40; f++) cpu.runCycles(kFrame);
        return virt;
    };
    struct { const char* n; uint8_t raw; } keys[] = {
        {"kp0",0x52},{"kp1",0x53},{"kp2",0x54},{"kp3",0x55},{"kp4",0x56},
        {"kp5",0x57},{"kp6",0x58},{"kp7",0x59},{"kp8",0x5B},{"kp9",0x5C},
        {"left",0x3B},{"right",0x3C},{"down",0x3D},{"up",0x3E},{"a",0x00},
    };
    bool ok = true;
    for (auto& k : keys) {
        int v = rawToVirt(k.raw);
        std::printf("raw $%02X (%s) -> virtual %s$%02X\n", k.raw, k.n,
                    v < 0 ? "NONE " : "", v < 0 ? 0 : v);
        if (v < 0) ok = false;
    }
    return ok ? 0 : 1;
}
