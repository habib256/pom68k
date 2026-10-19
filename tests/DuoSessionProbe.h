// POM68K — prove the MSC II's running guest, input and state restoration.
#pragma once
#include "Mmu040Peek.h"
#include "MscCpu.h"
#include "PixelPin.h"
#include "SaveStateMachines.h"
#include <array>

namespace duosession {

inline bool check(MscMemory& mem, MscCpu& cpu, pom68k::SnapMachine profile,
                  int64_t frameCycles) {
    if (!mem.is040()) return true;
    auto frame = [&] {
        if (mem.cpuHeld()) mem.tick(int(frameCycles));
        else cpu.runCycles(frameCycles);
    };
    auto logical = [&](uint32_t address, unsigned bytes, uint32_t& value) {
        const mmu040peek::Registers r{cpu.getTC040(), cpu.getURP040(),
            cpu.getSRP040(), cpu.getDTT0(), cpu.getDTT1()};
        value = 0;
        for (unsigned i = 0; i < bytes; i++) {
            uint32_t physical = 0;
            if (!mmu040peek::translate(r, address + i, true,
                    [&](uint32_t a) { return mem.peek8(a); }, &physical)) return false;
            value = value << 8 | mem.peek8(physical);
        }
        return true;
    };
    auto keys = [&] {
        std::array<uint32_t, 4> map{};
        for (unsigned i = 0; i < map.size(); i++)
            if (!logical(0x174 + 4 * i, 4, map[i])) map.fill(0xFFFFFFFFu);
        return map;
    };
    auto mouse = [&] {
        uint32_t position = 0;
        return logical(0x830, 4, position) ? position : 0xFFFFFFFFu;
    };
    const auto idle = keys();
    mem.keyEvent(0x05, true);
    bool pressed = false;
    for (int f = 0; f < 120 && !pressed && !cpu.isHalted(); f++) {
        frame();
        pressed = keys() != idle;
    }
    mem.keyEvent(0x05, false);
    bool released = false;
    for (int f = 0; f < 120 && !released && !cpu.isHalted(); f++) {
        frame();
        released = keys() == idle;
    }
    const uint32_t pointer = mouse();
    // Steer toward the centre so a pointer already at an edge can move.
    bool moved = false;
    for (int f = 0; f < 120 && !moved && !cpu.isHalted(); f++) {
        // Each delta is latched for one 60 Hz interval. Exercise sustained
        // motion so the PMU can sample it across processor idle resets.
        if (!(f % 10))
            mem.mouseMove(int16_t(pointer) < 320 ? 8 : -8,
                          int16_t(pointer >> 16) < mem.screenHeight() / 2 ? 8 : -8);
        frame();
        moved = mouse() != pointer;
    }
    const bool input = pressed && released && moved && pointer != 0xFFFFFFFFu;
    std::printf("MSC II input: key %s/%s, trackball %s ($%08X->$%08X), TC=$%08X\n",
                pressed ? "down" : "MISSING", released ? "up" : "STUCK",
                moved ? "moved" : "STUCK", pointer, mouse(), cpu.getTC040());

    std::vector<uint32_t> pixels;
    mem.decodeScreen(pixels);
    const uint64_t before = pixelpin::hashRegion(pixels, 640, mem.screenHeight(), 20);
    uint32_t savedTicks = 0;
    const bool clockReadable = logical(0x16A, 4, savedTicks);
    const auto savedClock = cpu.getClock();
    std::vector<uint8_t> state;
    pom68k::save(mem, cpu, profile, state);
    const auto other = profile == pom68k::SnapMachine::Duo280
        ? pom68k::SnapMachine::Duo280c : pom68k::SnapMachine::Duo280;
    std::string error;
    const bool refused = !pom68k::load(mem, cpu, other, state.data(), state.size(), error)
        && error == "save state is for another machine profile";
    for (int f = 0; f < 60 && !cpu.isHalted(); f++) frame();
    mem.write8(0x60000000, mem.read8(0x60000000) ^ 0xFF);
    const bool loaded = pom68k::load(mem, cpu, profile, state.data(), state.size(), error);
    mem.decodeScreen(pixels);
    uint32_t restoredTicks = 0;
    const bool restored = loaded && cpu.getClock() == savedClock &&
        logical(0x16A, 4, restoredTicks) && restoredTicks == savedTicks &&
        pixelpin::hashRegion(pixels, 640, mem.screenHeight(), 20) == before;
    for (int f = 0; f < 120 && !cpu.isHalted(); f++) frame();
    uint32_t resumedTicks = 0;
    const bool resumed = logical(0x16A, 4, resumedTicks) && resumedTicks > savedTicks;
    std::printf("MSC II state: %zu bytes, profile %s, restore %s, guest clock %s\n",
                state.size(), refused ? "isolated" : "WRONG",
                restored ? "exact" : "FAILED", resumed ? "running" : "STUCK");
    return input && clockReadable && restored && resumed && refused && !cpu.isHalted()
        && cpu.getModel() == moira::Model::M68LC040
        && cpu.getFPUModel() == moira::FPUModel::NONE;
}

} // namespace duosession
