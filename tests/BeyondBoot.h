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
    // Optional: blocks the guest has WRITTEN to its boot volume so far
    // (ScsiDisk::writeBlocks). The persist flow polls it instead of
    // budgeting a fixed number of frames for the flush — see persist().
    std::function<long()> writes;
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
    // Captured HERE, not after the gesture: the flush can land while the
    // keys are still being held, and a counter sampled afterwards would
    // wait for a second write that never comes.
    const long w0 = h.writes ? h.writes() : 0;

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
    if (h.dump) h.dump("gesture");             // the screen AT the peak
    h.frames(75);
    h.key(0x2D, false);
    h.frames(6);
    h.key(0x37, false);
    h.frames(120);                             // rename field appears
    hold(0x24, 150);                           // Return — commit the name

    // ── Wait for the FLUSH, do not budget for it ────────────────────────
    // Creating the folder and landing it on the medium are two different
    // events. The Finder's half is fast — a screen dump at the gesture's
    // peak shows the icon already on the desktop — but the catalog, the
    // volume bitmap and the MDB live in the File Manager's cache until
    // something flushes the volume, and how long THAT takes is a property
    // of the machine and its System, not a constant. The original fixed
    // 900 frames (~15 s) was tuned on the Egret/Cuda 7.5/8.1 volumes and
    // silently mis-judged the slower ones: the Plus and the Mac II created
    // the folder every run and were failed for it, because the gate
    // sampled the image before the write existed.
    //
    // So poll the write counter and stop at the first byte that lands.
    // Faster than the old budget where the guest is quick, correct where
    // it is not; a gate that cannot see the counter keeps the old budget.
    const long kFlushCap = 7200;               // 2 emulated minutes
    long waited = 0;
    if (h.writes) {
        while (waited < kFlushCap && h.writes() == w0) {
            h.frames(60);
            waited += 60;
        }
        const bool landed = h.writes() != w0;
        // The old fixed budget, kept AFTER the first write rather than
        // instead of it: a gate that was green on 900 frames still gets
        // its 900 to finish the catalog + bitmap + MDB burst.
        // 0 = the flush landed during the gesture itself, before this poll.
        std::printf("persist: first write %ld frames after the commit%s\n",
                    waited, landed ? "" : " — NONE within the cap");
        h.frames(900);
    } else {
        h.frames(900);
    }

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
    if (h.dump) h.dump("reboot");              // the screen it came back to
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

// ── "Is a modal dialog up?" ─────────────────────────────────────────────
// The longest horizontal run of LIGHT pixels below the menu bar. Every
// boot signature here is a pair of dark ratios, and every one of them is
// satisfied WITH an alert on screen — which is how three gates came to
// send a whole persist gesture into a modal dialog that swallows keys, and
// be read as broken input paths for it. A ratio cannot fix that: it has to
// guess where the box is, and the first attempt sampled a band the alert
// only half covered and passed anyway. A run length does not care where
// the box is. Measured on the gates' own dumps: clean desktops 47 (Plus),
// 65, 70 (Mac II) — a dither cannot hold a light run — against 381 for the
// alert both the 7.5.5 and the 7.1 volumes open at boot. Judge at 120.
inline int lightRun(const std::vector<uint32_t>& fb, int W, int H) {
    int best = 0;
    for (int y = 30; y < H - 30; y += 2) {
        if (fb.size() < size_t(y) * size_t(W) + size_t(W)) break;
        int run = 0;
        for (int x = 0; x < W; x++) {
            if ((fb[size_t(y) * W + x] & 0xFF) >= 0x80) {
                if (++run > best) best = run;
            } else {
                run = 0;
            }
        }
    }
    return best;
}
// 200, not 120: a desktop can put two icon LABELS side by side (measured
// 129 on a Mac II desktop carrying two volume icons in adjacent columns),
// and that must not read as a dialog. The alert measures 381, so the gap
// is wide either way.
inline constexpr int kDialogRun = 200;

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
