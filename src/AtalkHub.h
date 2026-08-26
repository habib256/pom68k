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
#include "MacIpGateway.h"
#include "LtoUdp.h"

#include <cstdint>
#include <functional>
#include <memory>
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
            mem.scc().injectRxFrame(0, d, n, false);
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
            // LocalTalk's half-duplex Rx is still OFF — a non-express
            // injectRxFrame at that instant is DROPPED (Scc8530.cpp:261, "no
            // ear"). So queue here and flush from tick(), which runs after
            // the CPU has executed the EOM ISR and re-armed Rx — exactly the
            // timing the working LToUDP poll path already has. Multicast to
            // external peers immediately (the cable has no such window).
            pending_.emplace_back(d, d + n);
            if (cable_ && cable_->active()) cable_->send(d, n);
        };
        if (cfg_.shareDir.empty()) cfg_.shareDir = defaultShareDir_;
        // The volume takes the shared folder's OWN name (netatalk does the
        // same when a volume has no explicit name) — so a folder called
        // "AppleShare" mounts as "AppleShare", not a hardcoded label.
        std::string vol = cfg_.volName.empty() ? folderName(cfg_.shareDir)
                                               : cfg_.volName;
        afp_.configure(cfg_.serverName, vol, cfg_.shareDir);
        pap_.configure(cfg_.printerName, cfg_.spoolDir);
        macip_.configure(cfg_.gwIp, cfg_.gwMask, cfg_.dns);
        // A machine carrying a DaynaPort SCSI/Link gets that card wired to
        // the SAME NAT the MacIP gateway uses — the guest's MacTCP then has
        // two ways to the outside (IP-in-DDP over LocalTalk, or IP over
        // Ethernet through the SCSI bus) and one gateway behind both.
        // `requires` rather than a virtual: the eleven machines with no card
        // compile exactly as before, and adding one to a machine is a member
        // plus an accessor.
        if constexpr (requires { mem.daynaPort(); }) {
            if (mem.daynaPort().present()) {
                ether_ = std::make_unique<EtherLink>(mem.daynaPort(), macip_);
                ether_->attach();
            }
        }
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
        if (!cfg_.stack) { pending_.clear(); return; }
        stack_.tick(nowCycles);
        afp_.tick(nowCycles);
        pap_.tick(nowCycles);
        macip_.tick(nowCycles);
        // Flush every frame the node queued this quantum (replies from the
        // guest's TX callback + RTMP/ATP timers) now that Rx is re-armed.
        if (inject_)
            for (const auto& f : pending_) inject_(f.data(), f.size());
        pending_.clear();
    }

    // ── GUI ──
    struct WireMeter { size_t backlog = 0, backlogMax = 0; int64_t holdMax = 0;
                       long drops = 0; };
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
        if (cpuHz_) s.wireHoldMaxMs = long(s.wire.holdMax * 1000 / cpuHz_);
        return s;
    }

    Config config() { std::lock_guard<std::mutex> l(mu_); return cfg_; }
    void setDefaultShareDir(const std::string& d) {
        std::lock_guard<std::mutex> l(mu_);
        defaultShareDir_ = d;
    }
    // Toggle a service live (from the GUI). key: "afp" | "pap" | "macip".
    void setService(const std::string& key, bool on) {
        std::lock_guard<std::mutex> l(mu_);
        if (key == "afp") cfg_.afp = on;
        else if (key == "pap") cfg_.pap = on;
        else if (key == "macip") cfg_.macip = on;
        else if (key == "stack") cfg_.stack = on;
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

    void applyLocked() {
        afp_.setEnabled(cfg_.stack && cfg_.afp);
        pap_.setEnabled(cfg_.stack && cfg_.pap);
        macip_.setEnabled(cfg_.stack && cfg_.macip);
    }

    std::mutex mu_;
    AtalkStack stack_;
    AfpServer afp_;
    PapServer pap_;
    MacIpGateway macip_;
    // Non-null only on a machine whose DaynaPort was put on the bus.
    std::unique_ptr<EtherLink> ether_;
    LtoUdp* cable_ = nullptr;
    std::function<void(const uint8_t*, size_t)> inject_;
    std::function<WireMeter()> wire_;
    WireMeter wireMeter_;            // last machine-thread sample (mu_-guarded)
    int64_t cpuHz_ = 0;
    std::vector<std::vector<uint8_t>> pending_;   // frames awaiting Rx re-arm
    Config cfg_;
    std::string defaultShareDir_;
    bool attached_ = false;
    bool stackDebug_ = false;
};
