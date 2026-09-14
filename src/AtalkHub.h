// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AtalkHub: the GUI-side owner of the in-process AppleTalk world ──
// One instance ties AtalkStack + AfpServer + PapServer + MacIpGateway to
// a machine's Scc8530 LocalTalk wire, so a stock POM68K needs no external
// TashRouter / netatalk / macipgw. It coexists with the LToUDP cable:
// when POM68K_LTOUDP=1 the same guest frames still reach real peers, and
// the internal node's frames are multicast alongside — the emulator then
// looks like one more node on the shared virtual LocalTalk.
//
// Attachment is machine-agnostic (templated on the memory type: every
// machine exposes scc()); status is mutex-guarded so the GUI thread can
// read it while the machine thread pumps the wire. The AppleTalk window
// in main.cpp renders status() and drives the enable flags.
//
// Thread contract: mu_ guards the hub's OWN state, never the machine's.
// Anything living in the Scc8530 (the Rx queue, its high-water marks) is
// unlocked machine-thread state, so the hub samples it in tick() — which
// runs on the machine thread — and snapshot() serves that copy. No GUI-
// thread path may dereference the SCC.

#pragma once
#include "AtalkStack.h"
#include "AfpServer.h"
#include "PapServer.h"
#include "EtherLink.h"
#include "EtherTalkLink.h"
#include "MacIpGateway.h"
#include "LtoUdp.h"
#include "Scc8530.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

class AtalkHub {
public:
    AtalkHub() : afp_(stack_), pap_(stack_), macip_(stack_) {}

    void configureDiagnostics(bool stackDebug, bool macIpDebug) {
        stackDebug_ = stackDebug;
        macip_.setDebug(macIpDebug);
    }

    struct Config {
        bool stack = true;               // the node/router itself
        // The node ALSO on the Ethernet segment a DaynaPort provides.
        // Off by default: it changes which wire AppleTalk lives on, and
        // every LocalTalk gate is calibrated on the SCC (EtherTalkLink.h).
        bool ethertalk = false;
        // The DaynaPort's CABLE. Plugged by default, and unplugging it is
        // the only honest host-side switch this card has: the target stays
        // on the SCSI bus (`present()` stays true, the ROM's boot-time probe
        // is not re-run, DiskBays.h) and its ENABLE INTERFACE bit stays the
        // guest driver's (DaynaPort.h). Only the uplink stops.
        bool ethernetCable = true;
        bool afp = true;
        bool pap = true;
        bool macip = true;
        std::string serverName = "POM68K";
        std::string volName;             // '' → derived from the share
                                         // folder's own name (like netatalk)
        std::string shareDir;            // '' → default set at attach
        std::string printerName = "POM68K";
        std::string spoolDir = "run/print";
        uint32_t gwIp = 0xC0A89701, gwMask = 0xFFFFFF00, dns = 0x08080808;
    };

    // Wire onto a machine's SCC. cpuHz drives the stack's second-scale
    // timers. Safe once per machine at startup. Idempotent.
    template <class M>
    void attach(M& mem, int64_t cpuHz, LtoUdp* cable) {
        std::lock_guard<std::mutex> l(mu_);
        cable_ = cable;
        (void)mem.scc(); // synchronize an event-driven device at attachment
        stack_.configure(2, 128, cfg_.serverName.empty() ? "POM68K" : cfg_.serverName,
                         cpuHz, stackDebug_);
        // Relay BrRq to the segment only when a real cable carries external
        // peers; solo, the relay + our reply collide in the guest Rx FIFO.
        stack_.setBridgeRelay(cable && cable->active());
        cpuHz_ = cpuHz;
        inject_ = [&mem](const uint8_t* d, size_t n) {
            mem.scc().injectRxFrame(0, d, n);
        };
        stack_.sendAddressDefence = [this, &mem](const uint8_t* d, size_t n) {
            mem.scc().injectRxFrame(
                0, d, n, Scc8530::RxFrameKind::AddressDefence);
            if (cable_ && cable_->active()) cable_->send(d, n);
        };
        // The lossless wire never drops — it DELAYS. Surface that delay:
        // a retransmit whose lag matches a deep backlog / long hold is
        // congestion, not loss (and then lowering the boost is the wrong
        // knob — the guest's Rx drain rate is the cap).
        wire_ = [&mem] {
            auto& scc = mem.scc();
            return WireMeter{ scc.rxBacklog(0), scc.rxBacklogMax(0),
                              scc.rxHoldMaxCycles(0), scc.rxOverflowDrops(0) };
        };
        stack_.sendFrame = [this](const uint8_t* d, size_t n) {
            // DEFER delivery: the node's replies are generated inside the
            // guest's TX callback (onGuestFrame runs during onTxFrame), when
            // LocalTalk's half-duplex Rx is still OFF — an ordinary
            // injectRxFrame at that instant is DROPPED (Scc8530.cpp, "no
            // ear"). So queue here and flush from tick(), which runs after
            // the CPU has executed the EOM ISR and re-armed Rx — exactly the
            // timing the working LToUDP poll path already has. Multicast to
            // external peers immediately. lapACK uses its dedicated prompt
            // path above; it must start inside the 200 us LLAP IFG.
            if (cfg_.ethertalk && etalk_) etalk_->onStackFrame(d, n);
            if (!cfg_.stack) return;      // no LocalTalk wire to defer onto
            pending_.emplace_back(d, d + n);
            if (cable_ && cable_->active()) cable_->send(d, n);
        };
        configureServicesLocked();
        // A machine carrying a DaynaPort SCSI/Link gets that card wired to
        // the SAME NAT the MacIP gateway uses — the guest's MacTCP then has
        // two ways to the outside (IP-in-DDP over LocalTalk, or IP over
        // Ethernet through the SCSI bus) and one gateway behind both.
        // `requires` rather than a virtual: a machine without the accessor
        // compiles exactly as before, and giving one the card is a member
        // plus an accessor — all twelve carry it since 2026-09-12.
        if constexpr (requires { mem.daynaPort(); }) {
            // Sampled on the machine thread, like the SCC's wire meters.
            // Set whether or not the card is on the bus, so "no card" is a
            // reported answer rather than an absent one.
            dayna_ = [&mem] {
                auto& card = mem.daynaPort();
                return DaynaMeter{ card.present(), card.enabled(),
                                   card.framesToGuest, card.framesFromGuest,
                                   card.framesDropped, card.bytesToGuest,
                                   card.bytesFromGuest, card.commands,
                                   card.queued() };
            };
            if (mem.daynaPort().present()) {
                ether_ = std::make_unique<EtherLink>(mem.daynaPort(), macip_);
                // One millisecond of the machine's own clock between the
                // NAT's answer and the card seeing it. Without it the reply
                // lands inside the guest's own send call and a real MacTCP
                // application never matches it (EtherLink.h).
                ether_->setLatency(cpuHz / 1000);
                ether_->attach();
                // The card's OTHER protocol family. Its RTMP beacon runs at
                // the same 10 s period the LocalTalk node uses.
                etalk_ = std::make_unique<EtherTalkLink>(mem.daynaPort(), stack_);
                etalk_->configure(cpuHz / 1000, 10 * cpuHz);
                // One card, two protocol families: AppleTalk frames are
                // 802.3 with an LLC/SNAP header, IPv4 and ARP are DIX.
                // EtherLink::attach took the callback first; this demux
                // replaces it and keeps both halves reachable.
                // This runs on the MACHINE thread inside the card's TX,
                // with no lock: the EtherTalk switch it reads is the
                // atomic mirror setService keeps, never cfg_ itself.
                mem.daynaPort().sendFrame =
                    [this](const uint8_t* d, size_t n) {
                        if (EtherTalkLink::isAppleTalk(d, n)) {
                            if (ethertalkLive_.load(std::memory_order_relaxed))
                                etalk_->onGuestFrame(d, n);
                            return;
                        }
                        ether_->onGuestFrame(d, n);
                    };
            }
        }
        // One sample now: the window must not read "no card" for the frames
        // between attachment and the first tick().
        if (dayna_) etherMeter_ = dayna_();
        ethertalkLive_.store(cfg_.ethertalk, std::memory_order_relaxed);
        applyLocked();
        attached_ = true;
    }

    // A frame the guest transmitted (call from the SCC onTxFrame hook,
    // AFTER the RTS→CTS handshake synth). Feeds the internal node.
    void onGuestFrame(const uint8_t* d, size_t n) {
        std::lock_guard<std::mutex> l(mu_);
        if (cfg_.stack) stack_.onGuestFrame(d, n);
    }
    // Inject a frame that arrived on the LToUDP cable (external peer).
    void onCableFrame(const uint8_t* d, size_t n) {
        std::lock_guard<std::mutex> l(mu_);
        if (cfg_.stack) stack_.onGuestFrame(d, n);
    }

    // Advance every timer. Call each emulation slice with cumulative CPU
    // cycles (cpu.getClock()).
    void tick(int64_t nowCycles) {
        std::lock_guard<std::mutex> l(mu_);
        // Sample the SCC's Rx meters HERE and nowhere else. They are plain
        // deque/scalar members the machine thread mutates without a lock, so
        // only the machine thread may read them — and tick() is the hub's
        // machine-thread entry point. snapshot() (GUI thread) then serves
        // the copy below under mu_, instead of reaching into the SCC mid-pop.
        if (wire_) wireMeter_ = wire_();
        if (dayna_) etherMeter_ = dayna_();
        stack_.tick(nowCycles, cfg_.stack || cfg_.ethertalk);
        macip_.tick(nowCycles);
        // The Ethernet segment carries its own latency and must advance
        // whether or not LocalTalk is running.
        if (ether_) ether_->tick(nowCycles);
        if (etalk_ && cfg_.ethertalk) etalk_->tick(nowCycles);
        if (!cfg_.stack) { pending_.clear(); return; }
        afp_.tick(nowCycles);
        pap_.tick(nowCycles);
        // Flush every frame the node queued this quantum (replies from the
        // guest's TX callback + RTMP/ATP timers) now that Rx is re-armed.
        if (inject_)
            for (const auto& f : pending_) inject_(f.data(), f.size());
        pending_.clear();
    }

    // ── GUI ──
    struct WireMeter { size_t backlog = 0, backlogMax = 0; int64_t holdMax = 0;
                       long drops = 0; };
    // What the DaynaPort reports about itself, for the AppleTalk window's
    // Ethernet line. Same rule as WireMeter above: the card's counters and
    // its ENABLE bit are plain members the SCSI code mutates unlocked, so
    // they are sampled in tick() — machine thread — and snapshot() serves
    // the copy. `enabled` is the GUEST driver's bit and is displayed READ
    // ONLY: writing it from the host would forge guest state (DaynaPort.h).
    struct DaynaMeter {
        bool present = false;            // on the bus at all
        bool enabled = false;            // guest's ENABLE INTERFACE ($0E)
        long framesToGuest = 0, framesFromGuest = 0, framesDropped = 0;
        long bytesToGuest = 0, bytesFromGuest = 0;
        long commands = 0;
        size_t queued = 0;               // frames waiting in the Rx ring
    };
    struct Snapshot {
        bool attached = false;
        AtalkStack::Stats net;
        WireMeter wire;                 // injection backlog / worst hold
        long wireHoldMaxMs = 0;         // holdMax converted with cpuHz
        std::string zone;
        uint8_t node = 0;
        AfpServer::Status afp;
        PapServer::Status pap;
        MacIpGateway::Status macip;
        Config cfg;
        bool cableUp = false;
        DaynaMeter ether;               // machine-thread sample, see tick()
    };
    Snapshot snapshot() {
        std::lock_guard<std::mutex> l(mu_);
        Snapshot s;
        s.attached = attached_;
        s.net = stack_.stats();
        s.zone = stack_.zone();
        s.node = stack_.node();
        s.afp = afp_.status();
        s.pap = pap_.status();
        s.macip = macip_.status();
        s.cfg = cfg_;
        s.cableUp = cable_ && cable_->active();
        s.wire = wireMeter_;                 // machine-thread sample, see tick()
        s.ether = etherMeter_;               // idem, the DaynaPort's own line
        if (cpuHz_) s.wireHoldMaxMs = long(s.wire.holdMax * 1000 / cpuHz_);
        return s;
    }

    Config config() { std::lock_guard<std::mutex> l(mu_); return cfg_; }
    // The editable half of Config — names, folders, addresses — applied LIVE
    // from the GUI. Each service already restarts itself on configure()
    // (disable → set → enable): the AFP server drops its sessions and
    // re-registers its NBP name, the printer closes an open connection,
    // the gateway retires its leases. That cut is the honest price of a
    // rename and the window says so. The toggles in `next` are ignored (the
    // checkboxes own them, live), and the LocalTalk ZONE keeps the server
    // name the stack was attached with — the guest chose it at boot. Before
    // attach, this is simply the configuration the attach will use.
    void reconfigure(const Config& next) {
        std::lock_guard<std::mutex> l(mu_);
        cfg_.serverName = next.serverName.empty() ? "POM68K" : next.serverName;
        cfg_.volName = next.volName;
        cfg_.shareDir = next.shareDir;
        cfg_.printerName = next.printerName.empty() ? "POM68K" : next.printerName;
        cfg_.spoolDir = next.spoolDir.empty() ? "run/print" : next.spoolDir;
        cfg_.gwIp = next.gwIp;
        cfg_.gwMask = next.gwMask;
        cfg_.dns = next.dns;
        if (!attached_) return;
        configureServicesLocked();
        applyLocked();
    }

    // Dotted-quad helpers for the window and the relaunch line; the mask
    // travels as a prefix length ("192.168.151.1/24").
    static bool parseIpv4(const std::string& text, uint32_t& out) {
        unsigned a, b, c, d; char tail;
        if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
            return false;
        if (a > 255 || b > 255 || c > 255 || d > 255) return false;
        out = (a << 24) | (b << 16) | (c << 8) | d;
        return true;
    }
    static std::string formatIpv4(uint32_t ip) {
        char b[20];
        std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255,
                      (ip >> 8) & 255, ip & 255);
        return b;
    }
    // "a.b.c.d/n" → address + mask. A bare address means /24.
    static bool parseCidr(const std::string& text, uint32_t& ip, uint32_t& mask) {
        const size_t slash = text.find('/');
        int prefix = 24;
        if (slash != std::string::npos) {
            char tail;
            if (std::sscanf(text.c_str() + slash + 1, "%d%c", &prefix, &tail) != 1 ||
                prefix < 1 || prefix > 30)
                return false;
        }
        if (!parseIpv4(text.substr(0, slash), ip)) return false;
        mask = uint32_t(0xFFFFFFFFu << (32 - prefix));
        return true;
    }
    static std::string formatCidr(uint32_t ip, uint32_t mask) {
        int prefix = 0;
        for (uint32_t m = mask; m & 0x80000000u; m <<= 1) ++prefix;
        return formatIpv4(ip) + "/" + std::to_string(prefix);
    }
    void setDefaultShareDir(const std::string& d) {
        std::lock_guard<std::mutex> l(mu_);
        defaultShareDir_ = d;
    }
    // Pin the date FPGetSrvrParms reports, so a gate asserting a deterministic
    // trajectory does not consume host wall-time through it. 0 = real clock.
    void setAfpFixedDate(int64_t unixSecs) {
        std::lock_guard<std::mutex> l(mu_);
        afp_.setFixedDate(unixSecs);
    }
    // Toggle a service live (from the GUI). key: "afp" | "pap" | "macip".
    void setService(const std::string& key, bool on) {
        std::lock_guard<std::mutex> l(mu_);
        if (key == "afp") cfg_.afp = on;
        else if (key == "pap") cfg_.pap = on;
        else if (key == "macip") cfg_.macip = on;
        else if (key == "stack") cfg_.stack = on;
        else if (key == "ethertalk") cfg_.ethertalk = on;
        else if (key == "ethernet") cfg_.ethernetCable = on;
        ethertalkLive_.store(cfg_.ethertalk, std::memory_order_relaxed);
        if (attached_) applyLocked();
    }

private:
    // The trailing path component of a folder path (its "name"), for use
    // as the AFP volume name. Strips trailing slashes; falls back to a
    // sensible label if the path is empty or the filesystem root.
    static std::string folderName(const std::string& path) {
        std::string d = path;
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        size_t p = d.find_last_of('/');
        std::string name = (p == std::string::npos) ? d : d.substr(p + 1);
        return name.empty() ? "Partage" : name;
    }

    // The three services from cfg_: at attach, and again on reconfigure().
    void configureServicesLocked() {
        if (cfg_.shareDir.empty()) cfg_.shareDir = defaultShareDir_;
        // The volume takes the shared folder's OWN name (netatalk does the
        // same when a volume has no explicit name) — so a folder called
        // "AppleShare" mounts as "AppleShare", not a hardcoded label.
        const std::string vol = cfg_.volName.empty() ? folderName(cfg_.shareDir)
                                                     : cfg_.volName;
        afp_.configure(cfg_.serverName, vol, cfg_.shareDir);
        pap_.configure(cfg_.printerName, cfg_.spoolDir);
        macip_.configure(cfg_.gwIp, cfg_.gwMask, cfg_.dns);
    }

    void applyLocked() {
        // A service is reachable when the node is on ANY wire: the SCC's
        // LocalTalk, the card's EtherTalk, or both.
        const bool node = cfg_.stack || cfg_.ethertalk;
        afp_.setEnabled(node && cfg_.afp);
        pap_.setEnabled(node && cfg_.pap);
        macip_.setEnabled(node && cfg_.macip);
        // The card's uplink, both protocol families on the one wire. These
        // objects are the HUB's own (unique_ptr members), so driving them
        // from here keeps the GUI thread out of the machine entirely.
        if (ether_) ether_->setUplink(cfg_.ethernetCable);
        if (etalk_) etalk_->setUplink(cfg_.ethernetCable);
    }

    std::mutex mu_;
    // cfg_.ethertalk as the machine thread may read it without mu_: the
    // card's TX demux above. Kept equal to cfg_.ethertalk by setService()
    // and attach(); the closed race of 2026-09-14 (TODO § Services réseau).
    std::atomic<bool> ethertalkLive_{false};
    AtalkStack stack_;
    AfpServer afp_;
    PapServer pap_;
    MacIpGateway macip_;
    // Non-null only on a machine whose DaynaPort was put on the bus.
    std::unique_ptr<EtherLink> ether_;
    std::unique_ptr<EtherTalkLink> etalk_;
    LtoUdp* cable_ = nullptr;
    std::function<void(const uint8_t*, size_t)> inject_;
    std::function<WireMeter()> wire_;
    WireMeter wireMeter_;            // last machine-thread sample (mu_-guarded)
    std::function<DaynaMeter()> dayna_;
    DaynaMeter etherMeter_;          // idem, for the card
    int64_t cpuHz_ = 0;
    std::vector<std::vector<uint8_t>> pending_;   // frames awaiting Rx re-arm
    Config cfg_;
    std::string defaultShareDir_;
    bool attached_ = false;
    bool stackDebug_ = false;
};
