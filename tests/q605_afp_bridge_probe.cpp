// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The AppleShare session over the REAL bridge (TODO § Services réseau) ──
// Every AFP gate in this tree talks to the in-process server: AtalkHub owns
// AtalkStack + AfpServer and answers the guest inside the same process. This
// probe takes that server away and puts a real one in its place — netatalk
// 2.4.9's afpd — reached over a real wire:
//
//   guest SCC channel B (LLAP)
//     ⇄ LToUDP multicast 239.192.76.84:1954   (src/LtoUdp.*)
//     ⇄ TashRouter                            (extern/tashrouter)
//     ⇄ pomtap0, a plain TAP
//     ⇄ the kernel's own AppleTalk/DDP stack
//     ⇄ atalkd + afpd serving `input/` as the volume "Input"
//
// The guest is unchanged and the gestures are the live gate's, calibrated on
// the same image (hdv/MacOS-8.1-boot.vhd) and overridable by the same
// POM68K_AFP_* knobs. What changes is who answers — and therefore what the
// proof is: not a hub counter but the HOST FILESYSTEM, where a directory the
// guest created and a two-fork copy the guest made must appear, written by
// netatalk, byte-exact against afplive's independent oracle.
//
// Not a gate. It needs an external daemon brought up with sudo
// (tools/netatalk2/appleshare.sh), which no CI runner can provide, so a
// registered test could only soft-skip — and CLAUDE.md's rule is that a
// soft-skipping gate proves asset detection, nothing else. Dev tool:
//
//   sudo tools/netatalk2/appleshare.sh
//   make -C build q605_afp_bridge_probe
//   POM68K_DUMP=1 build/q605_afp_bridge_probe
//
// POM68K_AFP_PHASE=N stops after phase N (calibration, same as the gate).
// POM68K_BRIDGE_SHARE=<dir> points at the served folder (default `input`).
// POM68K_BRIDGE_KEEP=1 leaves what the guest created on the share; by
// default the probe removes its own fixture and the guest's artefacts, so a
// user's own share comes back as it was.

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "FinderSignature.h"
#include "LtoUdp.h"
#include "Q605ApplicationHarness.h"
#include "Scc8530.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#include "afp_live_transfer.h"

using namespace q605app;

namespace {

int knob(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

// The gate's own click: a settle long enough for a dialog, and the count so
// a server row can be opened with a double click.
void clickTimes(int x, int y, int times, long settle) {
    steer(x, y);
    for (int c = 0; c < times; c++) {
        gMem->mouseButton(true);
        runFrames(6);
        gMem->mouseButton(false);
        runFrames(6);
    }
    runFrames(settle);
}

// Cmd-<code> with the gate's 75-frame hold: the 8.1 reference image ships
// with Slow Keys on, and this probe never types text, so it holds rather
// than toggling the feature.
void cmdKey(std::uint8_t code, long settle) {
    gMem->keyEvent(0x37, true);
    runFrames(12);
    keyHold(code, 75);
    gMem->keyEvent(0x37, false);
    runFrames(settle);
}

// The guest's own word on what is in front: WindowPeek->windowKind at +108
// (Inside Macintosh I-274). A dialog or alert is dialogKind = 2, a desk
// accessory's window is negative, an application's is 8. Reading it is how
// a gesture can be aimed at what is actually there instead of at what the
// script assumes.
int frontWindowKind() {
    const std::uint32_t w = peek32(0x09D6);
    if (!w) return 0;
    return int(std::int16_t(gMem->peek8(w + 108) << 8 | gMem->peek8(w + 109)));
}

// The Chooser's right-hand panel — "Select a file server:" — measured on
// this image on 2026-09-18: 253.6 mean luminance with the Chooser open
// (white list), 234.9 on the bare desktop (the wallpaper's sea and sky) and
// 123.2 under the startup alert. The guest's own WindowList was tried first
// and is NOT an instrument here: $09D6 is per-LAYER, so a sample taken while
// the Finder is the current process reports the Finder's front window —
// kind 8, title "" — with the Chooser plainly open (both measured the same
// day, and the retry loop they fed clicked the Apple menu on top of an open
// Chooser three times).
double chooserPanelMean() {
    const Screen s = decodeScreen();
    if (s.pixels.empty() || s.width < 480) return 0.0;
    double sum = 0.0;
    long count = 0;
    for (int y = 64; y < 246 && y < s.height; y++)
        for (int x = 280; x < 464 && x < s.width; x++) {
            sum += luminance(s.pixels[std::size_t(y) * s.width + x]);
            count++;
        }
    return count ? sum / double(count) : 0.0;
}

// Dark pixels inside the Chooser's right-hand panel: an entry is black text
// on white, an empty list is white. "No server answered" then reads as
// itself instead of as a mis-aimed click.
long listEntryPixels() {
    const Screen s = decodeScreen();
    if (s.pixels.empty() || s.width < 480) return -1;
    long dark = 0;
    // INSIDE the list: its frame sits at x 272 and x 465 and its scrollbar
    // just left of that, so a rectangle that includes them counts 398 dark
    // pixels on an EMPTY list and reads as an entry (measured 2026-09-18).
    for (int y = 82; y < 252 && y < s.height; y++)
        for (int x = 276; x < 446 && x < s.width; x++)
            if (luminance(s.pixels[std::size_t(y) * s.width + x]) < 0x60) dark++;
    return dark;
}

// Did the Apple menu drop? The open menu is a white panel with black item
// text where the wallpaper has neither. The live gate's own probe and its
// hard-won cut: Platinum menu paper is (231,231,231) and a 0xE8 threshold
// missed every panel pixel by one luminance point, reading an open menu as
// a desktop; the wallpaper's brightest sky is ~210, so 0xE0 separates.
bool menuDropped() {
    const Screen s = decodeScreen();
    if (s.pixels.empty()) return false;
    long white = 0, dark = 0, total = 0;
    for (int y = 24; y < 130 && y < s.height; y++)
        for (int x = 4; x < 130 && x < s.width; x++) {
            const double lum = luminance(s.pixels[std::size_t(y) * s.width + x]);
            if (lum >= 0xE0) white++;
            else if (lum < 0x30) dark++;
            total++;
        }
    return total > 0 && white > total * 55 / 100 && dark > total / 100;
}

// The guest's own list of mounted volumes: VCBQHdr ($0356) is a standard
// queue, each record linked by its first long, with vcbVN — the volume's
// name, a Str27 — at +44.
std::string mountedVolumeNames() {
    std::string out;
    std::uint32_t vcb = peek32(0x0356 + 2);
    for (int n = 0; vcb && n < 16; n++) {
        const int len = gMem->peek8(vcb + 44);
        std::string name;
        for (int i = 1; i <= len && i <= 27; i++)
            name.push_back(char(gMem->peek8(vcb + 44 + std::uint32_t(i))));
        if (!out.empty()) out += ", ";
        out += '"' + name + '"';
        vcb = peek32(vcb);
    }
    return out.empty() ? "(none)" : out;
}

std::set<std::string> names(const fs::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        out.insert(it->path().filename().string());
    return out;
}

} // namespace

int main() {
    const std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom" });
    const std::string diskPath = testasset::findAny({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    const int stopPhase = knob("POM68K_AFP_PHASE", 99);
    const char* shareKnob = std::getenv("POM68K_BRIDGE_SHARE");
    const fs::path shareDir = shareKnob ? fs::path(shareKnob) : fs::path("input");
    const bool keep = std::getenv("POM68K_BRIDGE_KEEP") != nullptr;
    if (!fs::is_directory(shareDir)) {
        std::fprintf(stderr, "FAIL: %s is not a directory — this must be the "
                     "folder afpd serves (AppleVolumes.default)\n",
                     shareDir.string().c_str());
        return 1;
    }

    // ── The fixture, seeded on the HOST side of the real server ──────────
    // Two forks, in netatalk 2's own on-disk format: the data fork as the
    // file, the resource fork and the Finder info in .AppleDouble/. That is
    // exactly what afplive::seed writes for the in-process server, which is
    // not a coincidence — AfpServer was built to netatalk's layout — so the
    // same host-side oracle verifies both servers.
    const std::set<std::string> beforeAll = names(shareDir);
    const bool hadFixture = fs::exists(shareDir / "BONJOUR.txt");
    if (!hadFixture && !afplive::seed(shareDir)) {
        std::fprintf(stderr, "FAIL: cannot seed the two-fork fixture in %s\n",
                     shareDir.string().c_str());
        return 1;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    auto cfg = pom68k::defaultCoreConfig();
    Q605Memory mem(cfg, 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), cfg.cpu, cfg.diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    gAzertyGuest = false;                   // the 8.1 reference volume is US

    // ── The wire ─────────────────────────────────────────────────────────
    // No hub, no boost. GuiHostServices raises the SCC's pace only for the
    // in-process peer (`hub && !cable`): with a real cable the frames must
    // cross a real socket, a real router and a real kernel stack, so the
    // guest's LocalTalk runs at its own 230.4 kbit/s and the probe waits.
    LtoUdp cable;
    if (!cable.start()) {
        std::fprintf(stderr, "FAIL: cannot join the LToUDP multicast group\n");
        return 1;
    }
    const int byteCycles = int(mem.cpuHz() / 28800);
    mem.scc().setByteCycles(byteCycles);
    // Queue a frame that lands while the guest's receiver is down instead of
    // dropping it (Scc8530.cpp: "receiver off = no ear"). On a real wire that
    // drop is the truth — a peer physically cannot answer inside the sender's
    // own transmission. Over a socket it is an ARTEFACT of the cable being
    // too fast: TashRouter answers the Chooser's BrRq in microseconds, while
    // LocalTalk is half-duplex and the driver only re-arms Rx on its EOM
    // interrupt, so the reply lands in a deaf receiver. Measured here on
    // 2026-09-18 — 128 BrRq sent, 60 LkUp-Replies back on the cable, DDP
    // checksum verified correct, and an empty server list; the same symptom
    // that comment already records from a 2026-07-22 live capture. The queue
    // does not shorten the wait: playback still defers to LLAP's 400 us
    // inter-dialog gap, measured from the previous frame's END.
    mem.scc().setLosslessRx(true);
    mem.scc().onTxFrame = [&](int channel, const std::uint8_t* d, std::size_t n) {
        if (channel != 0) return;
        // lapRTS → the local lapCTS, the one frame that never leaves the
        // machine: LLAP's handshake is half-duplex and its partner has to
        // answer inside the 200 us IFG, which no socket can (Scc8530.cpp).
        if (n == 3 && d[2] == 0x84) {
            if (d[0] != 0xFF) {
                const std::uint8_t cts[3] = { d[1], d[0], 0x85 };
                mem.scc().injectRxFrame(0, cts, 3, Scc8530::RxFrameKind::CtsReply);
            }
            return;
        }
        if (n == 3 && d[2] == 0x85) return;
        cable.send(d, n);
    };
    // main.cpp's own granularity: the socket is drained 64 times a frame,
    // not once. Measured 2026-09-18 — once a frame, the Chooser's server
    // list stayed empty with the replies visibly arriving at the socket.
    gFrameSlices = 64;
    gAfterFrame = [&] {
        cable.poll([&](const std::uint8_t* d, std::size_t n) {
            mem.scc().injectRxFrame(0, d, n);
        });
    };

    auto snap = [&](const char* name) {
        dump(name);
        std::printf("trace: %s clock=%lld fp=%016llx wire=%ld/%ld "
                    "rx=%zu/%zu drops=%ld\n", name,
                    (long long)cpu.machineClock(),
                    (unsigned long long)bench::fingerprint(cpu),
                    cable.framesTx, cable.framesRx,
                    mem.scc().rxBacklog(0), mem.scc().rxBacklogMax(0),
                    mem.scc().rxOverflowDrops(0));
        std::fflush(stdout);
    };

    // ── Phase 0: boot to the 8.1 Finder ──────────────────────────────────
    if (!bootToFinder(12000)) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d SCSI=%ld)\n",
                     cpu.isHalted(), mem.scsi().commands);
        return 1;
    }
    for (int poll = 0; poll < 60 && findersig::curApName(mem) != "Finder"; poll++)
        runFrames(120);
    runFrames(300);
    snap("afp_bridge_0_finder.ppm");
    std::printf("phase 0: Finder up, SCSI=%ld, front \"%s\", wire tx=%ld rx=%ld\n",
                mem.scsi().commands, findersig::curApName(mem).c_str(),
                cable.framesTx, cable.framesRx);
    std::fflush(stdout);
    // The guest has been speaking LLAP since the startup range: if nothing
    // came back, nothing on the segment is listening and every phase below
    // would fail on a blank list instead of saying why.
    if (cable.framesRx == 0)
        std::fprintf(stderr, "[bridge] nothing heard on the segment yet — is "
                     "`sudo tools/netatalk2/appleshare.sh` running?\n");

    // « Your AppleTalk network is now available. » — a guest that brought
    // its stack up with no router on the segment and then HEARS one says so,
    // in a modal alert, and a modal alert eats every gesture below (measured
    // 2026-09-18: the Apple menu, the Chooser and the AppleShare click all
    // landed on it, and phase 3 sent zero lookup frames). Dismiss whatever
    // dialog is up — Return is its default button — and aim by the front
    // window's KIND, because Return on a plain desktop starts renaming the
    // selected icon instead.
    for (int tries = 0; tries < 4 && frontWindowKind() == 2; tries++) {
        std::printf("phase 0: a modal dialog is in front — Return\n");
        std::fflush(stdout);
        keyHold(0x24, 8);
        runFrames(240);
    }
    if (frontWindowKind() == 2) {
        std::fprintf(stderr, "FAIL: a modal dialog stayed up\n");
        snap("afp_bridge_0_dialog.ppm");
        return 1;
    }
    if (stopPhase <= 0) return 0;

    // ── Phases 1-2: Apple menu → Chooser ─────────────────────────────────
    // Both gestures are VERIFIED and the PAIR is retried. A click that lands
    // during a late Finder redraw is eaten — the live gate already retries
    // the menu for that reason — and a menu that drops but whose row click
    // misses leaves an ordinary window in front instead of the Chooser
    // (measured 2026-09-18, front window kind 8). Neither is worth failing a
    // by-hand probe over, so the sequence starts again from the menu.
    bool chooserOpen = false;
    for (int round = 0; round < 3 && !chooserOpen; round++) {
        bool appleOpen = false;
        for (int attempt = 0; attempt < 6 && !appleOpen; attempt++) {
            clickTimes(10, 8, 1, 60);
            appleOpen = menuDropped();
            if (!appleOpen) {
                char name[64];
                std::snprintf(name, sizeof name, "afp_bridge_1_attempt%d%d.ppm",
                              round, attempt);
                snap(name);
                runFrames(540);
            }
        }
        if (!appleOpen) {
            std::fprintf(stderr, "FAIL: the Apple menu never dropped\n");
            return 1;
        }
        snap("afp_bridge_1_applemenu.ppm");
        if (stopPhase <= 1) return 0;

        // The Chooser is a desk accessory, so the guest's own window list
        // says whether it opened: a DA's window has a NEGATIVE windowKind.
        clickTimes(60, knob("POM68K_AFP_CHOOSER_Y", 139), 1, 600);
        snap("afp_bridge_2_chooser.ppm");
        chooserOpen = chooserPanelMean() > 245.0;
        if (!chooserOpen) {
            std::printf("phase 2: no Chooser (panel %.1f, want > 245) — "
                        "starting the menu again\n", chooserPanelMean());
            std::fflush(stdout);
            runFrames(300);
        }
    }
    if (!chooserOpen) {
        std::fprintf(stderr, "FAIL: the Chooser did not open\n");
        return 1;
    }
    std::printf("phase 2: Chooser open (panel %.1f, front application "
                "\"%s\")\n", chooserPanelMean(),
                findersig::curApName(*gMem).c_str());
    std::fflush(stdout);
    if (stopPhase <= 2) return 0;

    // ── Phase 3: AppleShare → NBP lookup on the real segment ─────────────
    const long wireBeforeLookup = cable.framesTx;
    clickTimes(knob("POM68K_AFP_AS_X", 88), knob("POM68K_AFP_AS_Y", 84), 1, 900);
    snap("afp_bridge_3_servers.ppm");
    const long entries = listEntryPixels();
    std::printf("phase 3: server list — %ld frames sent looking up, %ld dark "
                "pixels in the list panel\n",
                cable.framesTx - wireBeforeLookup, entries);
    std::fflush(stdout);
    if (entries < 20) {
        std::fprintf(stderr, "FAIL: no server answered the lookup — the "
                     "Chooser's list is empty (afpd registered? "
                     "`nbplkup` on the host says)\n");
        return 1;
    }
    if (stopPhase <= 3) return 0;

    // ── Phase 4: the server row → login dialog ───────────────────────────
    // afpd.conf names the server "POM68K", the same name the in-process hub
    // advertises, so the gate's calibrated row holds.
    clickTimes(knob("POM68K_AFP_SV_X", 300), knob("POM68K_AFP_SV_Y", 88), 2, 600);
    snap("afp_bridge_4_login.ppm");
    std::printf("phase 4: login dialog (panel %.1f)\n", chooserPanelMean());
    std::fflush(stdout);
    if (stopPhase <= 4) return 0;

    // ── Phase 5: Guest, Return → volume list ─────────────────────────────
    clickTimes(knob("POM68K_AFP_GUEST_X", 146), knob("POM68K_AFP_GUEST_Y", 153), 1, 60);
    keyHold(0x24, 8);                       // Return = Connect
    runFrames(600);
    snap("afp_bridge_5_volumes.ppm");
    std::printf("phase 5: volume list\n");
    if (stopPhase <= 5) return 0;

    // ── Phase 6: mount "Input", close the Chooser ────────────────────────
    keyHold(0x24, 8);                       // Return = OK on the volume
    runFrames(600);
    clickTimes(knob("POM68K_AFP_CLOSE_X", 34), knob("POM68K_AFP_CLOSE_Y", 40), 1, 300);
    for (int again = 0; again < 3; ++again) {
        const int kind = frontWindowKind();
        if (kind >= 0) break;
        std::printf("phase 6: a desk accessory is still in front (kind %d), "
                    "closing again\n", kind);
        clickTimes(knob("POM68K_AFP_CLOSE_X", 34), knob("POM68K_AFP_CLOSE_Y", 40), 1, 300);
    }
    snap("afp_bridge_6_mounted.ppm");
    std::printf("phase 6: chooser closed, wire tx=%ld rx=%ld\n",
                cable.framesTx, cable.framesRx);
    std::fflush(stdout);
    if (stopPhase <= 6) return 0;

    // ── Phase 7: open the freshly mounted volume (already selected) ──────
    cmdKey(0x1F, 900);                      // Cmd-O
    snap("afp_bridge_7_window.ppm");
    // What the guest itself says is mounted: the VCB queue ($0356), whose
    // records link through their first long and carry the volume name at
    // +44 (Str27). afpd serves `input/` as "Input", so that name appearing
    // here is the guest's own word for the mount.
    std::printf("phase 7: Cmd-O; volumes mounted: %s\n",
                mountedVolumeNames().c_str());
    std::fflush(stdout);
    if (stopPhase <= 7) return 0;

    // ── Phase 8: Cmd-N → a directory netatalk creates on the host ────────
    const std::set<std::string> beforeFolder = names(shareDir);
    cmdKey(0x2D, 0);                        // Cmd-N
    std::string created;
    for (int poll = 0; poll < 120 && created.empty(); poll++) {
        runFrames(30);
        std::error_code ec;
        for (fs::directory_iterator it(shareDir, ec), end; it != end && !ec;
             it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (!beforeFolder.count(name) && fs::is_directory(it->path()))
                created = name;
        }
    }
    snap("afp_bridge_8_created.ppm");
    std::printf("phase 8: host saw %s\n",
                created.empty() ? "NOTHING" : ("\"" + created + "\"").c_str());
    std::fflush(stdout);
    if (created.empty()) {
        std::fprintf(stderr, "FAIL: the guest's new folder never reached the "
                     "host filesystem\n");
        return 1;
    }
    if (stopPhase <= 8) return 0;

    // ── Phase 9: Cmd-D → both forks across the real wire ─────────────────
    // The live gate clicks the fixture at a calibrated icon position, which
    // it can: its share holds that one file. A REAL share is someone's
    // folder — `input/` here carries the user's own items — so the fixture
    // is selected by NAME instead, the one pointer that does not move when
    // the window's contents do. Slow Keys is on in this image and a
    // type-select needs several characters inside one second, so the
    // feature is probed and toggled first (Q605ApplicationHarness).
    runFrames(180);
    if (ensureFastKeys()) {
        typeText("bonjour");
        runFrames(60);
    } else {
        std::fprintf(stderr, "[bridge] type-select unavailable — falling back "
                     "to the live gate's calibrated icon position\n");
        clickTimes(46, 88, 1, 120);
    }
    snap("afp_bridge_9_selected.ppm");
    const std::set<std::string> beforeCopy = names(shareDir);
    const std::int64_t clock0 = cpu.machineClock();
    const long txBefore = cable.framesTx, rxBefore = cable.framesRx;
    cmdKey(0x02, 0);                        // Cmd-D: Duplicate
    std::string copied;
    for (int poll = 0; poll < 900 && copied.empty() && !cpu.isHalted(); ++poll) {
        runFrames(30);
        std::error_code ec;
        for (fs::directory_iterator it(shareDir, ec), end; it != end && !ec;
             it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            if (!beforeCopy.count(name) && it->is_regular_file() &&
                afplive::exactCopy(it->path()))
                copied = name;
        }
    }
    runFrames(120);
    snap("afp_bridge_9_copied.ppm");
    const double seconds = double(cpu.machineClock() - clock0) / double(mem.cpuHz());
    const long bytes = long(afplive::data.size() + afplive::resource.size());
    std::printf("phase 9: duplicate=\"%s\" data=%zu resource=%zu\n",
                copied.c_str(), afplive::data.size(), afplive::resource.size());
    std::printf("phase 9: %ld bytes in %.2f s of guest time (%.1f KiB/s), "
                "wire tx=%ld rx=%ld\n", bytes, seconds,
                seconds > 0 ? double(bytes) / 1024.0 / seconds : 0.0,
                cable.framesTx - txBefore, cable.framesRx - rxBefore);
    const bool sourceIntact = afplive::exactCopy(shareDir / "BONJOUR.txt");
    std::printf("phase 9: the source file is still byte-exact: %s\n",
                sourceIntact ? "yes" : "NO");
    std::fflush(stdout);

    // ── Leave the share as we found it ───────────────────────────────────
    std::error_code ec;
    if (!keep) {
        auto drop = [&](const fs::path& p) {
            fs::remove_all(p, ec);
            fs::remove_all(p.parent_path() / ".AppleDouble" / p.filename(), ec);
        };
        if (!created.empty()) drop(shareDir / created);
        if (!copied.empty()) drop(shareDir / copied);
        if (!hadFixture) drop(shareDir / "BONJOUR.txt");
        for (const auto& name : names(shareDir))
            if (!beforeAll.count(name) && name != ".AppleDouble" && name != ".AppleDB")
                std::printf("note: %s stayed behind on the share\n", name.c_str());
    }

    if (copied.empty() || !sourceIntact || cpu.isHalted()) {
        std::fprintf(stderr, "FAILED — no byte-exact two-fork copy through "
                     "netatalk\n");
        return 1;
    }
    std::printf("PASS: a real AppleShare server, a real wire, a guest that "
                "created \"%s\" and copied both forks into \"%s\"\n",
                created.c_str(), copied.c_str());
    return 0;
}
