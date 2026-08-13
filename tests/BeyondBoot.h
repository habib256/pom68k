// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The shared beyond-boot engine ──
//
// One implementation of the two scenarios every machine owes past its boot
// etalon (`TODO.md` § 2, the depth axis):
//
//   soak    — idle ~3 emulated minutes after the Finder: the Time global
//             must advance in step (the gate that catches the MCU-overclock
//             class), the CPU must not halt, and the Finder must still be
//             up at the end.
//   persist — Cmd-N creates a folder: the disk image bytes change, the
//             catalog name appears (FolderProbe: the signal is the count
//             that CHANGES, not the biggest count), and after a hard reset
//             the machine boots back off the modified — deliberately
//             dirty — volume with the folder still there.
//
// The first four machines (LC II, Q605, IIvx, IIsi) each carry a private
// copy of these flows; extending to the whole roster made an engine worth
// having. What stays PER MACHINE is exactly what really differs: the rig
// and its assets, the boot loop, the Finder signature (each boot etalon's
// own thresholds), the Time read (physical peek where low memory is
// physical, the Mmu030Peek walk on RAM-based-video machines), and the
// reboot procedure. Key gestures use the 150-frame hold everywhere — it
// outlasts a Slow Keys acceptance delay and a normal keyboard accepts it
// just the same (the ten-month-red-gate lesson, `pom68k-81-image-slow-keys`).
//
// The hooks are std::function on purpose: eight thin gates bind lambdas
// over their rig, and a test pays nothing for the indirection.

#pragma once
#include "FolderProbe.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace beyondboot {

struct Hooks {
    const char* name;                          // "Macintosh LC III"
    std::function<void(long)> frames;          // run n 60 Hz frames
    std::function<bool()> halted;
    std::function<bool()> finderUp;            // the boot etalon's signature
    std::function<bool(uint32_t*)> time;       // Mac clock (seconds); false = unreadable
    std::function<void(uint8_t, bool)> key;    // ADB/M0110 code, down/up
    std::function<std::vector<uint8_t>&()> disk;
    std::function<bool()> reboot;              // hard reset → Finder again
    std::function<void(const char*)> dump;     // POM68K_DUMP screenshot, or {}
    // Optional: called at the persist gesture's peak — Cmd held, 'n' down
    // for ~75 frames — so a gate can print the guest's own KeyMap and turn
    // "no folder appeared" into a verdict: keys-never-arrived vs
    // Finder-ignored-them. The adb_key_probe lesson: never diagnose an
    // input path without sampling KeyMap DURING the gesture.
    std::function<void()> probe;
    // Optional: rouse the machine after the idle soak, the way a user would
    // (a mouse wiggle), BEFORE the final Finder check. Machines whose System
    // blanks the display after a few idle minutes (7.5.x screen dimming on
    // Sonora — hardware video blank, all-black frame) need it; a machine
    // that never blanks passes with or without. The wake is part of what the
    // soak proves on such a machine: idle survival AND waking from it.
    std::function<void()> wake;
};

inline int soak(const Hooks& h) {
    uint32_t t0 = 0;
    if (!h.time(&t0)) {
        std::fprintf(stderr, "FAIL: Time global unreadable before the soak\n");
        return 1;
    }
    const long kSoak = 10800;                  // 180 s of 60 Hz frames
    h.frames(kSoak);
    uint32_t t1 = 0;
    const bool readable = h.time(&t1);
    const long dt = readable ? long(t1 - t0) : -1;
    if (h.wake) h.wake();                      // un-blank before judging
    if (h.dump) h.dump("soak");
    const bool alive = h.finderUp();
    std::printf("soak: %ld s elapsed on the Mac clock (want 135-225), "
                "halted=%d, Finder %s\n",
                dt, h.halted() ? 1 : 0, alive ? "still up" : "GONE");
    const bool ok = readable && !h.halted() && dt >= 135 && dt <= 225 && alive;
    std::printf("%s — %s soak\n", ok ? "PASSED" : "FAILED", h.name);
    return ok ? 0 : 1;
}

inline int persist(const Hooks& h) {
    std::vector<uint8_t>& disk = h.disk();
    long before[folderprobe::kCount];
    folderprobe::sample(disk, before, "before");
    const std::vector<uint8_t> snap = disk;

    auto hold = [&](uint8_t code, int frames) {
        h.key(code, true);
        h.frames(frames);
        h.key(code, false);
        h.frames(6);
    };
    h.key(0x37, true);                         // Cmd down
    h.frames(6);
    h.key(0x2D, true);                         // 'n' down, held past Slow Keys
    h.frames(75);
    if (h.probe) h.probe();                    // both keys should be live NOW
    h.frames(75);
    h.key(0x2D, false);
    h.frames(6);
    h.key(0x37, false);
    h.frames(120);                             // rename field appears
    hold(0x24, 150);                           // Return — commit the name
    h.frames(900);                             // ~15 s: create + flush catalog

    long after[folderprobe::kCount];
    folderprobe::sample(disk, after, "after");
    const bool wrote = disk != snap;
    const size_t grew = folderprobe::grew(before, after);
    std::printf("persist: %s, image %s\n",
                grew < folderprobe::kCount
                    ? (std::string("'") + folderprobe::kNames[grew] + "' " +
                       std::to_string(before[grew]) + " -> " +
                       std::to_string(after[grew])).c_str()
                    : "NO candidate folder name appeared",
                wrote ? "modified" : "UNCHANGED");
    if (h.dump) h.dump("persist");

    // Reboot on the modified volume — deliberately dirty (no clean unmount
    // before the reset); surviving THAT is part of what "persist" claims.
    const bool rebooted = h.reboot();
    long survived[folderprobe::kCount];
    folderprobe::sample(disk, survived, "reboot");
    const bool kept = grew < folderprobe::kCount && survived[grew] > before[grew];
    std::printf("persist: reboot %s, folder %s\n",
                rebooted ? "reached the Finder" : "FAILED",
                kept ? "survived" : "did NOT survive");
    const bool ok = wrote && grew < folderprobe::kCount && rebooted && kept;
    std::printf("%s — %s persist\n", ok ? "PASSED" : "FAILED", h.name);
    return ok ? 0 : 1;
}

// Entry point: dispatch on POM68K_BEYOND (default "soak").
inline int run(const Hooks& h) {
    const char* m = getenv("POM68K_BEYOND");
    const std::string mode = m ? m : "soak";
    if (mode == "soak") return soak(h);
    if (mode == "persist") return persist(h);
    std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
    return 1;
}

// ── Shared screen helpers (every gate draws the same statistics) ────────

inline double darkRatio(const std::vector<uint32_t>& fb, int W,
                        int x0, int x1, int y0, int y1) {
    if (fb.size() < size_t(W) * y1) return -1.0;
    long dark = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = fb[size_t(y) * W + x];
            int luma = (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                      + int(p & 0xFF)) / 8;
            if (luma < 0x80) dark++;
        }
    return double(dark) / (double(x1 - x0) * (y1 - y0));
}

inline void dumpPpm(const char* name, const std::vector<uint32_t>& fb,
                    int W, int H) {
    if (!getenv("POM68K_DUMP")) return;
    FILE* fp = fopen(name, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t p = fb[size_t(y) * W + x];
            uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
            fwrite(rgb, 1, 3, fp);
        }
    fclose(fp);
}

// The boot etalons' shared image fixup: some images carry no $6A driver
// descriptor, which the ROM's boot scan requires.
inline void ensureBootDriverType(std::vector<uint8_t>& img) {
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

} // namespace beyondboot
