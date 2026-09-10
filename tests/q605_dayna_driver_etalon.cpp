// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Dayna's OWN driver against the emulated SCSI/Link (TODO §2) ──
//
// Everything the DaynaPort target knew until this gate came from Dayna's
// SLINKCMD.TXT as PiSCSI implements it: a specification read, never a
// driver's opinion. `daynaport_test` pins that command set from the host
// side; this gate hands the card to the software Dayna shipped for it.
//
// The machine is a Quadra 605 with the card on the bus and three volumes:
// a writable System 7.5.5 clone (classic networking — the Network cdev
// picks the AppleTalk link, MacTCP 2.0.6 picks the IP one), the ORIGINAL
// DaynaPORT installer floppy image, and a data volume carrying Apple's
// MacTCP Ping 2.0.2.
//
// The legs, each asserted on the guest's own observables:
//
//   install    Dayna's installer probes the bus, reports "DaynaPORT
//              SCSI/Link version 1.2.5" as the software it will place, and
//              copies it into the System file. Artefact: the host-owned
//              image carries the driver's own strings afterwards.
//   ethertalk  Chooser → AppleTalk active, Network cdev → "EtherTalk
//              Alternative" (the installed SCSI/Link ADEV). The driver
//              opens the card (ENABLE) and puts real AARP probes and DDP
//              on the wire, addressed to the AppleTalk multicast group.
//   mactcp     MacTCP → the Ethernet link, manual 192.168.151.2 with the
//              gateway at .1 (MacIpGateway's own subnet). MacTCP Ping
//              ARPs for the gateway, EtherLink's proxy answers, and the
//              echo requests leave as IPv4 frames.
//   receive    An ICMP echo REQUEST injected toward the guest is answered
//              by the guest's own stack — the receive path proven from
//              outside the machine as well as from inside it.
//
// This gate found one real defect and is what proves the fix: the NAT
// answered INSIDE the guest's own send call, so every reply reached the
// Rx ring before the WRITE(6) that carried the request had finished, and
// MacTCP Ping reported "timeout" for answers whose checksums verify.
// `EtherLink` now holds frames for the segment's own latency (1 ms of
// machine time, set by `AtalkHub::attach`); the application reports five
// successes out of five, 0% loss, round trip 0/6/13 ticks.
//
// Assets, all private and user-provided:
//   roms/…FF7439EE…                the Quadra 605 ROM
//   hdv/System 7.5.5 HD.dsk        cloned to hdv/work/ before each run
//   hdv/ref/DAYNA.vhd              the DaynaPORT installer floppy image
//   hdv/ref/TOOLS.vhd              a data volume holding MacTCP Ping

#include "AtalkHub.h"
#include "PortableEnv.h"
#include "Q605ApplicationHarness.h"
#include "JitTestConfig.h"

#include <cctype>
#include <string>
#include <vector>

using namespace q605app;

namespace {

constexpr int kDaynaId = 3;                  // where the card answers
constexpr int kDriverVolumeId = 5;           // the installer floppy image
constexpr int kToolsVolumeId = 4;            // MacTCP Ping
constexpr uint32_t kGuestIp = 0xC0A89702;    // 192.168.151.2
constexpr uint32_t kGatewayIp = 0xC0A89701;  // MacIpGateway's own address

// What the guest's driver puts on the wire, classified as it arrives.
struct Sniffer {
    long aarp = 0, ddp = 0, arp = 0, ip = 0, other = 0;
    long arpForGateway = 0, icmpRequests = 0, icmpReplies = 0;
    long ddpFromStartupRange = 0, ddpFromRouterNet = 0;
    std::string firstDescription;

    static uint32_t be32(const uint8_t* p) {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
               uint32_t(p[2]) << 8 | p[3];
    }

    void operator()(const uint8_t* d, size_t n) {
        if (n < 14) { other++; return; }
        const uint16_t typeLen = uint16_t(d[12]) << 8 | d[13];
        const char* what = "type";
        if (typeLen <= 1500 && n >= 22 && d[14] == 0xAA && d[15] == 0xAA &&
            d[16] == 0x03) {                        // 802.3 + LLC/SNAP
            const uint16_t snap = uint16_t(d[20]) << 8 | d[21];
            if (snap == 0x80F3) { aarp++; what = "AARP"; }
            else if (snap == 0x809B) {
                ddp++;
                what = "DDP";
                // Source network, from the long DDP header: a Macintosh
                // with no router sits in the startup range ($FF00-$FFFE);
                // one that accepted our RTMP has moved onto net 2.
                if (n >= 22 + 8) {
                    const uint16_t srcNet = uint16_t(d[22 + 6]) << 8 | d[22 + 7];
                    if (srcNet >= 0xFF00 && srcNet <= 0xFFFE) ddpFromStartupRange++;
                    else if (srcNet == 2) ddpFromRouterNet++;
                }
            }
            else { other++; what = "SNAP"; }
        } else if (typeLen == 0x0806) {
            arp++;
            what = "ARP";
            // Sender 192.168.151.2 asking for the gateway: the guest's own
            // configuration, spoken on the wire.
            if (n >= 42 && be32(d + 28) == kGuestIp && be32(d + 38) == kGatewayIp)
                arpForGateway++;
        } else if (typeLen == 0x0800) {
            ip++;
            what = "IPv4";
            const size_t ihl = size_t(d[14] & 0x0F) * 4;
            if (n >= 14 + ihl + 8 && d[23] == 1) {  // ICMP
                if (d[14 + ihl] == 8) icmpRequests++;
                if (d[14 + ihl] == 0) icmpReplies++;
            }
        } else other++;
        if (firstDescription.empty()) {
            char buf[192];
            std::snprintf(buf, sizeof buf,
                          "%s, %zu bytes, dst %02X:%02X:%02X:%02X:%02X:%02X "
                          "src %02X:%02X:%02X:%02X:%02X:%02X len/type %04X",
                          what, n, d[0], d[1], d[2], d[3], d[4], d[5],
                          d[6], d[7], d[8], d[9], d[10], d[11], typeLen);
            firstDescription = buf;
        }
    }
};

// Key acceptance on this volume measures 4 frames (the 8.1 reference image
// answers in 2-3), so the harness's 3-frame tap is rejected and
// ensureFastKeys() misreads that as Slow Keys — its eight-second Return
// hold then turns the feature ON (measured 4 frames before, 33 after).
// Type-select here holds each key 8 frames, well inside the Finder's
// one-second type-select window.
void typeSlow(const char* value) {
    for (const char* p = value; *p; p++) {
        const uint8_t code = adbFor(*p);
        if (code != 0xFF) keyHold(code, 8);
    }
}

// The guest's own word for what is open: WindowList ($9D6) points at the
// frontmost WindowRecord, whose titleHandle sits at +134. "A window
// appeared" is NOT enough on this volume — Stickies puts its note up on
// its own schedule, and a gesture that missed looked successful whenever
// the note happened to land in the same second (measured 2026-09-10).
std::string frontWindowTitle() {
    const uint32_t window = peek32(0x09D6);
    if (!window) return {};
    const uint32_t handle = peek32(window + 134);
    if (!handle) return {};
    const uint32_t text = peek32(handle);
    if (!text) return {};
    const uint8_t n = gMem->peek8(text);
    std::string title;
    for (uint8_t i = 0; i < n; i++) title += char(gMem->peek8(text + 1 + i));
    return title;
}

bool titleIs(const std::string& title, const char* want) {
    const size_t n = std::strlen(want);
    if (title.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(title[i]) != std::tolower(want[i])) return false;
    return true;
}

// A volume on the desktop is opened with the MOUSE: select the icon,
// then Cmd-O. Type-select is not dependable there on this volume — the
// Startup Items (Stickies among them) are still coming up when the
// desktop is drawn, and keystrokes typed into that window select nothing
// at all, while a click on the icon always lands (measured 2026-09-10).
// Desktop icon positions are the volume's own, in its Desktop database.
bool openVolume(int x, int y, const char* expectTitle, long settle,
                int tries = 3) {
    for (int t = 0; t < tries; t++) {
        click(x, y, 60);
        command(0x1F, settle);                      // 'o' — Open
        if (titleIs(frontWindowTitle(), expectTitle)) return true;
        std::fprintf(stderr, "[nav] icon (%d,%d) -> front window is '%s', "
                     "wanted '%s' (try %d)\n", x, y,
                     frontWindowTitle().c_str(), expectTitle, t + 1);
        runFrames(300);
    }
    return false;
}

// Inside a window, the item is reached by type-select + Cmd-O, retried
// until the guest says the right window is front.
bool openWindow(const char* prefix, const char* expectTitle, long settle,
                int tries = 3) {
    for (int t = 0; t < tries; t++) {
        typeSlow(prefix);
        runFrames(30);
        command(0x1F, settle);                      // 'o' — Open
        if (titleIs(frontWindowTitle(), expectTitle)) return true;
        std::fprintf(stderr, "[nav] '%s' -> front window is '%s', wanted '%s' "
                     "(try %d)\n", prefix, frontWindowTitle().c_str(),
                     expectTitle, t + 1);
        runFrames(300);
    }
    return false;
}

// Same gesture, for an item that launches an application instead of
// opening a window: the guest's process list is the answer.
bool openApplication(const char* prefix, const char* appName, long settle,
                     int tries = 3) {
    for (int t = 0; t < tries; t++) {
        typeSlow(prefix);
        runFrames(30);
        command(0x1F, settle);                      // 'o' — Open
        const auto seen = runningProcesses(60);
        if (processRuns(seen, appName)) return true;
        std::fprintf(stderr, "[nav] '%s' -> processes %s, wanted '%s' (try %d)\n",
                     prefix, describe(seen).c_str(), appName, t + 1);
        runFrames(300);
    }
    return false;
}

// The guest's own volume count: VCBQHdr ($0356) is a standard queue whose
// records start with the link to the next one. The two data volumes mount
// a few seconds AFTER the Finder draws its desktop, and a type-select for
// a volume that is not there yet silently selects its alphabetical
// neighbour — so every leg that opens one waits for the count first.
int mountedVolumes() {
    uint32_t vcb = peek32(0x0356 + 2);
    int n = 0;
    while (vcb && n < 16) { n++; vcb = peek32(vcb); }
    return n;
}

bool waitForVolumes(int want, int maxFrames) {
    for (int f = 0; f < maxFrames && !gCpu->isHalted(); f += 30) {
        if (mountedVolumes() >= want) return true;
        runFrames(30);
    }
    return mountedVolumes() >= want;
}

// The 7.5.5 volume draws a brighter menu bar than the 8.1 reference image
// the shared finderUp() signature is calibrated on (mean 240 measured,
// above its 235 ceiling), so the Finder is recognised by the guest's own
// word plus a drawn menu bar.
bool finderReady() {
    Screen s = decodeScreen();
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    return m.mean > 200 && m.deviation > 20 &&
           findersig::curApName(*gMem) == "Finder";
}

bool bootTo755Finder(int maxFrames) {
    while (gMem->cpuHeld()) gMem->tick(1000);
    for (int frame = 0; frame < maxFrames && !gCpu->isHalted(); frame++) {
        gCpu->runCycles(kFrameCycles);
        if (gAfterFrame) gAfterFrame();
        if (frame >= 3000 && !(frame % 60) && gMem->scsi().commands > 2000 &&
            finderReady()) {
            runFrames(300);
            return !gCpu->isHalted();
        }
    }
    return false;
}

// Special → Restart, rows read off the guest's own menu dump
// (q605_dayna_20_special.ppm): Special at x 232, Restart at y 122.
void restartFromFinder() {
    steer(232, 9);
    gMem->mouseButton(true);
    runFrames(60);
    steer(245, 122);
    runFrames(30);
    gMem->mouseButton(false);
    runFrames(600);
}

// The gesture is not the event: a menu pick swallowed by whatever owns the
// keyboard leaves the machine running, and every later leg then measures a
// system that never rebooted (measured 2026-09-10: a "restart" that served
// 339 SCSI commands instead of ~4700). So watch for the machine LEAVING
// the Finder, and try again if it did not.
bool restartAndBoot(int maxFrames) {
    for (int attempt = 0; attempt < 3; attempt++) {
        // Empty desktop, clear of both the Control Panels window and the
        // control panel the previous leg opened — a control panel is its
        // OWN application in System 7, and its menu bar has no Special.
        click(520, 420, 180);
        std::fprintf(stderr, "[restart] front '%s', window '%s'\n",
                     findersig::curApName(*gMem).c_str(),
                     frontWindowTitle().c_str());
        dump("q605_dayna_nav_restart.ppm");
        restartFromFinder();
        bool left = false;
        for (int f = 0; f < 1800 && !left && !gCpu->isHalted(); f++) {
            runFrames(1);
            const std::string app = findersig::curApName(*gMem);
            if (app != "Finder") left = true;
        }
        if (left) return bootTo755Finder(maxFrames);
        std::fprintf(stderr, "[restart] the machine did not leave the Finder "
                     "(attempt %d)\n", attempt + 1);
    }
    return false;
}

// A writable clone, made here so the gate is repeatable: the guest
// installs software onto it and the reference image must not move.
bool cloneVolume(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    return out.good();
}

// Occurrences of a literal in the host-owned image: the artefact check.
// The installed ADEV lives inside the System file's resource fork, so the
// driver's own version string is what survives on the volume the guest
// wrote — not a catalog entry.
long countLiteral(const std::string& path, const char* needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;
    const std::string hay((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    long hits = 0;
    for (size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + 1))
        hits++;
    return hits;
}

}  // namespace

int main() {
    const std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    const std::string refDisk = testasset::findAny({ "hdv/System 7.5.5 HD.dsk" });
    const std::string drvPath = testasset::findAny({ "hdv/DAYNA.vhd" });
    const std::string toolsPath = testasset::findAny({ "hdv/TOOLS.vhd" });
    if (romPath.empty() || refDisk.empty() || drvPath.empty() || toolsPath.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM and, under hdv/ref/, "
                    "System 7.5.5 HD.dsk, DAYNA.vhd (the DaynaPORT installer "
                    "disk) and TOOLS.vhd (MacTCP Ping)\n");
        return 0;
    }
    testasset::report({ romPath, refDisk, drvPath, toolsPath });
    std::fflush(stdout);

    const std::string diskPath = "hdv/work/dayna-755.dsk";
    if (!cloneVolume(refDisk, diskPath)) {
        std::fprintf(stderr, "FAIL: could not clone %s to %s\n",
                     refDisk.c_str(), diskPath.c_str());
        return 1;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    auto cfg = pom68k::defaultCoreConfig();
    cfg.bus.daynaPortId = kDaynaId;
    Q605Memory mem(cfg, 32u << 20);
    if (!mem.loadRom(rom) ||
        !mem.attachScsi(diskPath, true, 0) ||
        !mem.attachScsi(drvPath, false, kDriverVolumeId) ||
        !mem.attachScsi(toolsPath, false, kToolsVolumeId)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disks\n");
        return 1;
    }
    Cpu040 cpu(mem, testjit::resolveFromEnvironment(), cfg.cpu, cfg.diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    gAzertyGuest = false;                    // the 7.5.5 volume is US/QWERTY

    Sniffer sniffer;
    mem.daynaPort().sendFrame = [&](const uint8_t* d, size_t n) { sniffer(d, n); };

    struct Result {
        bool finder = false, installed = false, artefact = false;
        bool ethertalk = false, aarp = false, joinedNetwork = false;
        bool namedService = false;
        bool mactcpBound = false, icmpOut = false, received = false;
        bool halted = false;
        bool ok() const {
            return finder && installed && artefact && ethertalk && aarp &&
                   joinedNetwork && namedService && mactcpBound && icmpOut &&
                   received && !halted;
        }
    } r;

    // ── boot ─────────────────────────────────────────────────────────────
    r.finder = bootTo755Finder(14000);
    dump("q605_dayna_1_boot.ppm");
    std::printf("boot: Finder %s, SCSI %ld, card present=%d enabled=%d\n",
                r.finder ? "up" : "NOT UP", mem.scsi().commands,
                mem.daynaPort().present(), mem.daynaPort().enabled());
    if (!r.finder) return 1;

    // The volume's startup alias alert ("Infinite HD") owns the front:
    // Return takes its default (Continue) before any Finder gesture.
    keyHold(0x24, 6);
    runFrames(240);
    std::printf("keys: acceptance %d frames\n", keyLatency());
    const bool volumes = waitForVolumes(3, 3600);
    // The desktop is drawn before the volume's Startup Items have finished
    // launching (Stickies among them). A type-select typed into that gap
    // selects nothing; fifteen seconds of guest time later the same
    // gesture lands (measured 2026-09-10).
    runFrames(900);
    std::printf("volumes: %d mounted (boot + installer disk + tools)%s\n",
                mountedVolumes(), volumes ? "" : " — TIMED OUT");

    // ── install: Dayna's own installer, from the original floppy ─────────
    if (!openVolume(600, 210, "DaynaPORT Installer", 900)) {
        std::fprintf(stderr, "FAIL: the DaynaPORT Installer volume did not open\n");
        return 1;
    }
    dump("q605_dayna_2_volume.ppm");
    if (!openApplication("daynaport s", "Installer", 2400)) {
        std::fprintf(stderr, "FAIL: Apple's Installer never became the "
                     "running process\n");
        return 1;
    }
    keyHold(0x24, 8);                        // splash: Continue…
    runFrames(1200);
    dump("q605_dayna_3_easyinstall.ppm");    // "DaynaPORT SCSI/Link version 1.2.5"
    keyHold(0x24, 8);                        // Install
    runFrames(3000);
    keyHold(0x24, 8);                        // "…quit other applications": Continue
    runFrames(7200);
    dump("q605_dayna_4_installed.ppm");      // "Installation was successful"

    // Restart from the installer's own default button and come back up.
    long before = mem.scsi().commands;
    keyHold(0x24, 8);
    r.installed = bootTo755Finder(16000);
    dump("q605_dayna_5_rebooted.ppm");
    std::printf("install: restart %s, second boot served %ld SCSI commands\n",
                r.installed ? "reached the Finder" : "DID NOT reach the Finder",
                mem.scsi().commands - before);
    if (!r.installed) return 1;

    // ── AppleTalk over the card ──────────────────────────────────────────
    // The volume ships with AppleTalk inactive, and the Network control
    // panel refuses to open until it loads at startup.
    keyHold(0x24, 6);
    runFrames(240);
    steer(14, 9);                            // Apple menu → Chooser (y 132)
    mem.mouseButton(true);
    runFrames(60);
    steer(60, 132);
    runFrames(30);
    mem.mouseButton(false);
    runFrames(900);
    click(497, 367, 120);                    // AppleTalk: Active on restart
    keyHold(0x24, 8);                        // "…then restart your machine": OK
    runFrames(300);
    dump("q605_dayna_6_chooser.ppm");
    command(adbFor('w'), 300);               // close the Chooser
    before = mem.scsi().commands;
    if (!restartAndBoot(16000)) {
        std::fprintf(stderr, "FAIL: no Finder after the AppleTalk restart\n");
        return 1;
    }
    std::printf("appletalk: restart served %ld SCSI commands\n",
                mem.scsi().commands - before);

    keyHold(0x24, 6);
    runFrames(240);
    closeAllFinderWindows();
    if (!openVolume(600, 57, "Macintosh HD", 900) ||
        !openWindow("system f", "System Folder", 900) ||
        !openWindow("control p", "Control Panels", 900) ||
        !openWindow("network", "Network", 1800)) {
        std::fprintf(stderr, "FAIL: could not reach the Network control panel\n");
        return 1;
    }
    dump("q605_dayna_7_network.ppm");
    // "LocalTalk Built-In" and "EtherTalk Alternative" — the second IS the
    // installed SCSI/Link ADEV. Cancel is the confirmation's default
    // button, so the OK needs the mouse.
    click(209, 110, 300);
    click(322, 190, 900);
    runFrames(900);
    dump("q605_dayna_8_ethertalk.ppm");
    r.ethertalk = mem.daynaPort().enabled();
    r.aarp = sniffer.aarp > 0 && sniffer.ddp > 0;
    std::printf("ethertalk: card enabled=%d, AARP %ld DDP %ld frames "
                "(first: %s)\n", mem.daynaPort().enabled(), sniffer.aarp,
                sniffer.ddp, sniffer.firstDescription.c_str());

    // ── MacTCP over the same card ────────────────────────────────────────
    command(adbFor('w'), 300);               // close the Network panel
    if (!openWindow("mactcp", "MacTCP", 1800)) {
        std::fprintf(stderr, "FAIL: the MacTCP control panel did not open\n");
        return 1;
    }
    dump("q605_dayna_9_mactcp.ppm");
    // MacTCP lists the AppleTalk link ("EtherTalk (A)", which would carry
    // IP inside DDP — MacIP, and this card bridges IPv4 and ARP only) and
    // the Ethernet one. The Ethernet link is the card's own protocol.
    click(263, 85, 60);
    click(215, 293, 900);                    // More…
    click(126, 127, 300);                    // Obtain Address: Manually
    click(232, 309, 60);                     // gateway field, caret past the end
    for (int i = 0; i < 16; i++) keyHold(0x33, 6);
    typeSlow("192.168.151.1");
    runFrames(120);
    dump("q605_dayna_10_gateway.ppm");
    click(139, 381, 600);                    // OK
    click(285, 222, 60);                     // the panel's own address field
    for (int i = 0; i < 16; i++) keyHold(0x33, 6);
    typeSlow("192.168.151.2");
    runFrames(180);
    dump("q605_dayna_11_address.ppm");
    command(adbFor('w'), 600);               // close: MacTCP writes its config

    // The card's uplink, attached the way the GUI attaches it live. With
    // the hub present from power-on the guest's Finder stopped opening
    // control panels (measured 2026-09-10), and every leg above needs no
    // network at all.
    AtalkHub hub;
    hub.setService("afp", false);
    hub.setService("pap", false);
    hub.setService("macip", true);
    hub.setService("stack", false);          // no LocalTalk peer
    // AppleTalk on the CARD instead: AARP, extended RTMP and DDP over
    // 802.3/SNAP (EtherTalkLink.h). The guest is already running EtherTalk
    // in the startup range; the restart below is where it hears a router.
    hub.setService("ethertalk", true);
    hub.setService("afp", true);
    const int byteCycles = int(mem.cpuHz() / 28800);
    hub.attach(mem, int64_t(byteCycles) * 28800, nullptr);
    gAfterFrame = [&] { hub.tick(cpu.machineClock()); };
    {   // Sniff in FRONT of the link EtherLink installed, never instead of it.
        auto uplink = mem.daynaPort().sendFrame;
        mem.daynaPort().sendFrame = [&sniffer, uplink](const uint8_t* d, size_t n) {
            sniffer(d, n);
            if (uplink) uplink(d, n);
        };
    }
    // ── the guest joins the network the bridge advertises ────────────────
    // The RTMP beacon reaches a Macintosh that came up with no router, and
    // it says so itself: "Access to your AppleTalk internet has now become
    // available. To use the internet, please open the Network icon in the
    // Control Panels Folder, then click the selected AppleTalk connection
    // icon." That instruction IS the gesture below — no restart needed.
    runFrames(1800);
    dump("q605_dayna_13_internet.ppm");
    keyHold(0x24, 8);                        // OK
    runFrames(300);
    if (!openWindow("network", "Network", 1800)) {
        std::fprintf(stderr, "FAIL: the Network control panel did not reopen\n");
        return 1;
    }
    click(209, 110, 300);                    // EtherTalk Alternative, again
    click(322, 190, 900);                    // the change-connection OK
    runFrames(3600);
    dump("q605_dayna_14_joined.ppm");
    r.joinedNetwork = sniffer.ddpFromRouterNet > 0;
    std::printf("ethertalk: DDP from the startup range %ld, from the router's "
                "network %ld, AARP %ld — the node %s\n",
                sniffer.ddpFromStartupRange, sniffer.ddpFromRouterNet,
                sniffer.aarp, r.joinedNetwork ? "JOINED" : "did NOT join");
    command(adbFor('w'), 300);               // close the Network panel

    // ── and the services on it ───────────────────────────────────────────
    // The Chooser is the guest's own name lookup: selecting AppleShare
    // sends an NBP BrRq for AFPServer in the current zone, and what comes
    // back is this node's AFP server — over the card, with the SCC idle.
    const auto beforeChooser = hub.snapshot().net;
    steer(14, 9);                            // Apple menu → Chooser
    mem.mouseButton(true);
    runFrames(60);
    steer(60, 132);
    runFrames(30);
    mem.mouseButton(false);
    runFrames(900);
    click(228, 140, 1800);                   // the AppleShare icon
    runFrames(1800);
    dump("q605_dayna_15_chooser.ppm");
    const auto afterChooser = hub.snapshot().net;
    r.namedService = afterChooser.nbpLookups > beforeChooser.nbpLookups;
    std::printf("ethertalk: NBP lookups served %ld (Chooser), DDP in %ld\n",
                afterChooser.nbpLookups - beforeChooser.nbpLookups,
                afterChooser.ddpIn - beforeChooser.ddpIn);
    command(adbFor('w'), 600);               // close the Chooser

    before = mem.scsi().commands;
    if (!restartAndBoot(16000)) {
        std::fprintf(stderr, "FAIL: no Finder after the MacTCP restart\n");
        return 1;
    }
    std::printf("mactcp: restart served %ld SCSI commands\n",
                mem.scsi().commands - before);


    // MacTCP opens its link on first use, so the proof needs a client:
    // Apple's MacTCP Ping 2.0.2, aimed at the gateway's own address
    // (MacIpGateway answers ICMP echo internally).
    keyHold(0x24, 6);
    runFrames(240);
    closeAllFinderWindows();
    if (!openVolume(600, 160, "TOOLS", 900) ||
        !openApplication("mactcp p", "MacTCP Ping", 2400)) {
        std::fprintf(stderr, "FAIL: MacTCP Ping did not launch\n");
        return 1;
    }
    runFrames(600);
    const std::string front = findersig::curApName(mem);
    std::printf("ping: front application '%s'\n", front.c_str());
    click(420, 71, 60);                      // Ping Host Address
    for (int i = 0; i < 24; i++) keyHold(0x33, 5);
    typeSlow("192.168.151.1");
    runFrames(120);
    const long rxBefore = mem.daynaPort().framesToGuest;
    click(340, 309, 1800);                   // Start Ping
    dump("q605_dayna_12_ping.ppm");
    r.mactcpBound = sniffer.arpForGateway > 0;
    r.icmpOut = sniffer.icmpRequests > 0 &&
                mem.daynaPort().framesToGuest > rxBefore;
    std::printf("mactcp: ARP for the gateway %ld, ICMP requests out %ld, "
                "answers handed to the card %ld\n", sniffer.arpForGateway,
                sniffer.icmpRequests,
                mem.daynaPort().framesToGuest - rxBefore);

    // ── receive, asked for from outside ──────────────────────────────────
    // MacTCP Ping's own display shows "timeout" although the gateway's
    // replies are valid and the driver READs each one back within a frame
    // of its arrival. So put the question the other way round: an ICMP
    // echo REQUEST addressed to the guest. A reply is the guest's stack
    // saying it received over this card.
    {
        auto ones = [](const std::vector<uint8_t>& v) {
            uint32_t sum = 0;
            for (size_t i = 0; i + 1 < v.size(); i += 2)
                sum += uint32_t(v[i]) << 8 | v[i + 1];
            if (v.size() & 1) sum += uint32_t(v.back()) << 8;
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            return uint16_t(~sum);
        };
        std::vector<uint8_t> f;
        const auto& guestMac = mem.daynaPort().mac();
        f.insert(f.end(), guestMac.begin(), guestMac.end());
        const uint8_t gwMac[6] = { 0x02, 0x00, 0x4B, 0x36, 0x38, 0x01 };
        f.insert(f.end(), gwMac, gwMac + 6);
        f.push_back(0x08); f.push_back(0x00);
        std::vector<uint8_t> ip = {
            0x45, 0x00, 0x00, 0x00, 0x12, 0x34, 0x00, 0x00, 0x40, 0x01,
            0x00, 0x00, 0xC0, 0xA8, 0x97, 0x01, 0xC0, 0xA8, 0x97, 0x02 };
        std::vector<uint8_t> icmp = { 0x08, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0x00, 0x01 };
        icmp.resize(8 + 32, 0x5A);
        const uint16_t ic = ones(icmp);
        icmp[2] = uint8_t(ic >> 8); icmp[3] = uint8_t(ic);
        const uint16_t total = uint16_t(ip.size() + icmp.size());
        ip[2] = uint8_t(total >> 8); ip[3] = uint8_t(total);
        const uint16_t hc = ones(ip);
        ip[10] = uint8_t(hc >> 8); ip[11] = uint8_t(hc);
        f.insert(f.end(), ip.begin(), ip.end());
        f.insert(f.end(), icmp.begin(), icmp.end());
        const long repliesBefore = sniffer.icmpReplies;
        mem.daynaPort().receiveFrame(f.data(), f.size());
        runFrames(600);
        r.received = sniffer.icmpReplies > repliesBefore;
        std::printf("receive: echo requests answered by the guest %ld\n",
                    sniffer.icmpReplies - repliesBefore);
    }

    r.halted = cpu.isHalted();
    // The artefact: the driver Dayna's installer placed is IN the volume
    // the guest wrote (Easy Install merges the SCSI/Link 'adev' into the
    // System file — infs 1, special-macs:System).
    const long strings = countLiteral(diskPath, "DaynaPORT SCSI/Link");
    r.artefact = strings > 0;
    std::printf("artefact: \"DaynaPORT SCSI/Link\" appears %ld times in %s\n",
                strings, diskPath.c_str());
    std::printf("wire: AARP %ld DDP %ld ARP %ld IPv4 %ld other %ld "
                "(card tx %ld rx %ld dropped %ld)\n", sniffer.aarp, sniffer.ddp,
                sniffer.arp, sniffer.ip, sniffer.other,
                mem.daynaPort().framesFromGuest, mem.daynaPort().framesToGuest,
                mem.daynaPort().framesDropped);
    std::printf("%s — Quadra 605 real DaynaPORT SCSI/Link driver etalon\n",
                r.ok() ? "PASSED" : "FAILED");
    return r.ok() ? 0 : 1;
}
