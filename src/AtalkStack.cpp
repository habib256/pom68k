// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// AtalkStack — see AtalkStack.h. Wire formats pinned against Inside
// AppleTalk 2nd ed. and extern/netatalk2 include/atalk/{ddp,nbp,zip,
// rtmp,atp}.h; router behaviour mirrors extern/tashrouter (single
// LocalTalk segment, one zone).

#include "AtalkStack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Opt-in wire tracer: POM68K_ATALK_DEBUG=1 logs DDP/NBP/ATP traffic to
// stderr. Off by default (checked once).
static bool atalkDbg() {
    static int on = -1;
    if (on < 0) on = std::getenv("POM68K_ATALK_DEBUG") ? 1 : 0;
    return on == 1;
}

namespace {
constexpr uint8_t kLlapShortDdp = 0x01;
constexpr uint8_t kLlapLongDdp = 0x02;
constexpr uint8_t kLlapEnq = 0x81;
constexpr uint8_t kLlapAck = 0x82;

constexpr uint8_t kDdpRtmpData = 1;   // DDP protocol types (ddp.h:29-35)
constexpr uint8_t kDdpNbp = 2;
constexpr uint8_t kDdpAtp = 3;
constexpr uint8_t kDdpAep = 4;
constexpr uint8_t kDdpRtmpReq = 5;
constexpr uint8_t kDdpZip = 6;

constexpr uint8_t kSockRtmp = 1;      // statically assigned sockets
constexpr uint8_t kSockNbp = 2;
constexpr uint8_t kSockAep = 4;
constexpr uint8_t kSockZip = 6;

constexpr size_t kMaxDdpData = 586;

// ATP control-info byte (atp.h packet diagram)
constexpr uint8_t kAtpFuncMask = 0xC0;
constexpr uint8_t kAtpTReq = 0x40;
constexpr uint8_t kAtpTResp = 0x80;
constexpr uint8_t kAtpTRel = 0xC0;
constexpr uint8_t kAtpXo = 0x20;
constexpr uint8_t kAtpEom = 0x10;

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}
void pstr(std::vector<uint8_t>& v, const std::string& s) {
    v.push_back(uint8_t(std::min<size_t>(s.size(), 32)));
    v.insert(v.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 32));
}
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return true;
}
// NBP name-part match: "=" (and the ≈ wildcard byte $C5) match anything.
bool nbpMatch(const std::string& pat, const std::string& name) {
    if (pat.empty() || pat == "=" || (pat.size() == 1 && uint8_t(pat[0]) == 0xC5))
        return true;
    return ieq(pat, name);
}
} // namespace

void AtalkStack::configure(uint16_t net, uint8_t node, const std::string& zone,
                           int64_t cpuHz) {
    net_ = net;
    node_ = node;
    zone_ = zone;
    cpuHz_ = cpuHz > 0 ? cpuHz : 15667200;
    nextRtmp_ = 0;                        // first tick broadcasts immediately
    bindAtp(kSockZip, [this](std::shared_ptr<AtpTxn> t) { zipAtpHandler(std::move(t)); });
}

// ── wire ────────────────────────────────────────────────────────────────

void AtalkStack::onGuestFrame(const uint8_t* d, size_t n) {
    if (n < 3) return;
    uint8_t dst = d[0], src = d[1], type = d[2];
    if (type == kLlapEnq) {
        stats_.enqSeen++;
        // The probe is for OUR address: defend it (lapACK) so the guest
        // moves on to another ID — the express path keeps the ACK inside
        // the prober's window, like the cable's synthesized CTS.
        if (dst == node_ && src == node_ && sendFrame) {
            const uint8_t ack[3] = { src, node_, kLlapAck };
            sendFrame(ack, 3);
        }
        return;
    }
    if (type != kLlapShortDdp && type != kLlapLongDdp) return;   // RTS/CTS…
    if (src != node_) {
        stats_.guestNode = src;
        stats_.lastGuestCycles = now_;
    }
    if (dst != node_ && dst != 0xFF) return;
    stats_.framesIn++;

    Addr from;
    uint8_t dstSock, ddpType;
    const uint8_t* payload;
    size_t plen;
    if (type == kLlapShortDdp) {
        if (n < 3 + 5) return;
        size_t len = (size_t(d[3] & 0x03) << 8) | d[4];
        if (len < 5 || len > n - 3) return;
        dstSock = d[5];
        from = { net_, src, d[6], false };
        ddpType = d[7];
        payload = d + 8;
        plen = len - 5;
    } else {
        if (n < 3 + 13) return;
        size_t len = (size_t(d[3] & 0x03) << 8) | d[4];
        if (len < 13 || len > n - 3) return;
        uint16_t snet = uint16_t(d[9]) << 8 | d[10];
        dstSock = d[13];
        from = { snet, d[12], d[14], true };
        ddpType = d[15];
        payload = d + 16;
        plen = len - 13;
    }
    stats_.ddpIn++;
    if (atalkDbg())
        std::fprintf(stderr, "[atalk] IN  %s ddp %u.%u:%u->:%u type=%u len=%zu\n",
                     type == kLlapShortDdp ? "short" : "long",
                     from.net, from.node, from.sock, dstSock, ddpType, plen);
    handleDdp(from, dstSock, ddpType, payload, plen);
}

void AtalkStack::sendDdp(const Addr& dst, uint8_t srcSock, uint8_t ddpType,
                         const uint8_t* p, size_t n) {
    if (!sendFrame || n > kMaxDdpData) return;
    std::vector<uint8_t> f;
    f.reserve(16 + n);
    f.push_back(dst.node);                // LLAP dst (0xFF = broadcast)
    f.push_back(node_);
    if (!dst.longHdr) {
        f.push_back(kLlapShortDdp);
        uint16_t len = uint16_t(5 + n);
        f.push_back(uint8_t(len >> 8));
        f.push_back(uint8_t(len));
        f.push_back(dst.sock);
        f.push_back(srcSock);
        f.push_back(ddpType);
    } else {
        f.push_back(kLlapLongDdp);
        uint16_t len = uint16_t(13 + n);
        f.push_back(uint8_t(len >> 8));
        f.push_back(uint8_t(len));
        put16(f, 0);                      // no DDP checksum
        put16(f, dst.net ? dst.net : net_);
        put16(f, net_);
        f.push_back(dst.node);
        f.push_back(node_);
        f.push_back(dst.sock);
        f.push_back(srcSock);
        f.push_back(ddpType);
    }
    f.insert(f.end(), p, p + n);
    stats_.ddpOut++;
    stats_.framesOut++;
    sendFrame(f.data(), f.size());
}

void AtalkStack::tick(int64_t nowCycles) {
    now_ = nowCycles;
    if (now_ >= nextRtmp_) {
        sendRtmpData(true, {});
        nextRtmp_ = now_ + 10 * cpuHz_;   // RTMP broadcast period: 10 s
    }
    // XO cache release sweep
    for (auto it = xoCache_.begin(); it != xoCache_.end();)
        it = it->second.expires <= now_ ? xoCache_.erase(it) : std::next(it);
    // requester retries
    for (auto it = requests_.begin(); it != requests_.end();) {
        if (now_ < it->nextRetry) { ++it; continue; }
        if (++it->tries > 5) {
            auto done = std::move(it->done);
            it = requests_.erase(it);
            std::vector<std::vector<uint8_t>> none;
            if (done) done(false, none);
            continue;
        }
        std::vector<uint8_t> pkt;
        pkt.push_back(kAtpTReq | (it->xo ? kAtpXo : 0));
        pkt.push_back(it->need);
        put16(pkt, it->tid);
        pkt.insert(pkt.end(), it->req.begin(), it->req.end());
        sendDdp(it->dst, it->srcSock, kDdpAtp, pkt.data(), pkt.size());
        it->nextRetry = now_ + cpuHz_;    // 1 s between tries
        ++it;
    }
}

// ── DDP dispatch ────────────────────────────────────────────────────────

void AtalkStack::handleDdp(const Addr& src, uint8_t dstSock, uint8_t type,
                           const uint8_t* p, size_t n) {
    // ATP owns type 3 on sockets with a bound transaction handler; every
    // other type on the same socket can still have a raw DDP handler
    // (MacIP: ATP assignment AND IP-in-DDP-22 both live on socket 72).
    if (type == kDdpAtp && atpHandlers_.count(dstSock)) {
        handleAtp(src, dstSock, p, n);
        return;
    }
    auto it = ddpHandlers_.find(dstSock);
    if (it != ddpHandlers_.end()) { it->second(src, type, p, n); return; }
    switch (dstSock) {
    case kSockRtmp:
        if (type == kDdpRtmpReq) handleRtmpReq(src, p, n);
        return;
    case kSockNbp:
        if (type == kDdpNbp) handleNbp(src, p, n);
        return;
    case kSockAep:
        if (type == kDdpAep && n >= 1 && p[0] == 1) {
            std::vector<uint8_t> echo(p, p + n);
            echo[0] = 2;                  // Echo Reply
            sendDdp(src, kSockAep, kDdpAep, echo.data(), echo.size());
        }
        return;
    case kSockZip:
        if (type == kDdpZip) handleZipDdp(src, p, n);
        return;
    default:
        return;
    }
}

// ── router-lite: RTMP ───────────────────────────────────────────────────

void AtalkStack::sendRtmpData(bool broadcast, const Addr& to) {
    // RTMP Data (nonextended net): sender net(2), ID len (8 bits), sender
    // node, then one tuple advertising this network at distance 0.
    std::vector<uint8_t> p;
    put16(p, net_);
    p.push_back(8);
    p.push_back(node_);
    put16(p, net_);
    p.push_back(0);
    Addr dst = broadcast ? Addr{ net_, 0xFF, kSockRtmp, false } : to;
    dst.sock = kSockRtmp;
    sendDdp(dst, kSockRtmp, kDdpRtmpData, p.data(), p.size());
}

void AtalkStack::handleRtmpReq(const Addr& src, const uint8_t* p, size_t n) {
    if (n < 1 || p[0] != 1) return;
    // RTMP Response: the header alone (net, ID len, node).
    std::vector<uint8_t> r;
    put16(r, net_);
    r.push_back(8);
    r.push_back(node_);
    sendDdp(src, kSockRtmp, kDdpRtmpData, r.data(), r.size());
}

// ── router-lite: ZIP ────────────────────────────────────────────────────

void AtalkStack::handleZipDdp(const Addr& src, const uint8_t* p, size_t n) {
    // GetNetInfo (function 5): [5][flags? unused][zone len][zone].
    // Per Inside AppleTalk ch.8 the request is function + zone name.
    if (n < 2 || p[0] != 5) return;
    size_t zl = 0, zoff = 1;
    // Tolerate an old-style pad byte before the zone length.
    if (p[1] <= 32 && 1 + 1 + p[1] <= n) { zl = p[1]; zoff = 2; }
    std::string reqZone(reinterpret_cast<const char*>(p + zoff), zl);
    bool valid = reqZone.empty() || reqZone == "*" || ieq(reqZone, zone_);

    std::vector<uint8_t> r;
    r.push_back(6);                       // GetNetInfo Reply
    uint8_t flags = 0x40 | 0x20;          // UseBroadcast | OnlyOneZone
    if (!valid) flags |= 0x80;            // ZoneInvalid
    r.push_back(flags);
    put16(r, net_);                       // net range: just this net
    put16(r, net_);
    pstr(r, reqZone);                     // echo of the requested zone
    r.push_back(0);                       // no zone multicast on LocalTalk
    if (!valid) pstr(r, zone_);           // default zone
    sendDdp(src, kSockZip, kDdpZip, r.data(), r.size());
}

void AtalkStack::zipAtpHandler(std::shared_ptr<AtpTxn> t) {
    // ZIP over ATP (zip.h): GETMYZONE=7, GETZONELIST=8, GETLOCALZONES=9.
    // Request user bytes: [func, 0, startIndex16]. Reply user bytes:
    // [lastFlag, 0, count16], data = packed zone names.
    if (t->req.size() < 4) return;
    uint8_t func = t->req[0];
    if (func != 7 && func != 8 && func != 9) return;
    uint16_t start = uint16_t(t->req[2]) << 8 | t->req[3];
    std::vector<uint8_t> pkt;
    pkt.push_back(1);                     // last buffer
    pkt.push_back(0);
    uint16_t count = (start <= 1) ? 1 : 0;
    put16(pkt, count);
    if (count) pstr(pkt, zone_);
    t->respond({ std::move(pkt) });
}

// ── NBP ─────────────────────────────────────────────────────────────────

void AtalkStack::nbpRegister(const std::string& obj, const std::string& type,
                             uint8_t sock) {
    nbpUnregister(obj, type);
    nbp_.push_back({ obj, type, sock });
}

void AtalkStack::nbpUnregister(const std::string& obj, const std::string& type) {
    nbp_.erase(std::remove_if(nbp_.begin(), nbp_.end(),
                              [&](const NbpEntry& e) {
                                  return ieq(e.obj, obj) && ieq(e.type, type);
                              }),
               nbp_.end());
}

void AtalkStack::handleNbp(const Addr& src, const uint8_t* p, size_t n) {
    if (n < 2 + 5 + 3) return;
    uint8_t op = p[0] >> 4;
    uint8_t id = p[1];
    // First tuple = the requester (return address for replies).
    const uint8_t* t = p + 2;
    Addr requester;
    requester.net = uint16_t(t[0]) << 8 | t[1];
    requester.node = t[2];
    requester.sock = t[3];
    requester.longHdr = src.longHdr;
    size_t off = 2 + 5;
    auto rdStr = [&](std::string& s) -> bool {
        if (off >= n) return false;
        size_t l = p[off++];
        if (off + l > n) return false;
        s.assign(reinterpret_cast<const char*>(p + off), l);
        off += l;
        return true;
    };
    std::string obj, type, zone;
    if (!rdStr(obj) || !rdStr(type) || !rdStr(zone)) return;

    if (atalkDbg())
        std::fprintf(stderr, "[atalk] NBP op=%u id=%u from %u.%u:%u  \"%s:%s@%s\"\n",
                     op, id, requester.net, requester.node, requester.sock,
                     obj.c_str(), type.c_str(), zone.c_str());

    if (op != 0x1 && op != 0x2) return;   // BrRq / LkUp only
    if (!zone.empty() && zone != "*" && !ieq(zone, zone_)) return;

    if (op == 0x1 && bridgeRelay_ && sendFrame) {
        // Router duty: rebroadcast the BrRq as a LkUp on the segment so
        // external LToUDP peers get a shot at answering too. Only when the
        // cable is up — otherwise the broadcast + our own LkUpReply below
        // land back-to-back in the guest's Rx FIFO and both are lost
        // (empty-Chooser bug, APPLETALK.md §2.4).
        std::vector<uint8_t> lk(p, p + n);
        lk[0] = uint8_t(0x2 << 4 | (p[0] & 0x0F));
        Addr bcast{ net_, 0xFF, kSockNbp, src.longHdr };
        sendDdp(bcast, kSockNbp, kDdpNbp, lk.data(), lk.size());
    }

    std::vector<const NbpEntry*> matches;
    for (const NbpEntry& e : nbp_)
        if (nbpMatch(obj, e.obj) && nbpMatch(type, e.type))
            matches.push_back(&e);
    if (atalkDbg())
        std::fprintf(stderr, "[atalk] NBP registry has %zu entr%s, %zu match(es)\n",
                     nbp_.size(), nbp_.size() == 1 ? "y" : "ies", matches.size());
    if (!matches.empty()) {
        stats_.nbpLookups++;
        nbpReply(requester, id, matches);
    }
}

void AtalkStack::nbpReply(const Addr& to, uint8_t id,
                          const std::vector<const NbpEntry*>& matches) {
    std::vector<uint8_t> r;
    size_t cnt = std::min<size_t>(matches.size(), 15);
    r.push_back(uint8_t(0x3 << 4 | cnt));  // LkUpReply
    r.push_back(id);
    for (size_t i = 0; i < cnt; i++) {
        put16(r, net_);
        r.push_back(node_);
        r.push_back(matches[i]->sock);
        r.push_back(0);                    // enumerator
        pstr(r, matches[i]->obj);
        pstr(r, matches[i]->type);
        pstr(r, zone_);
    }
    if (atalkDbg())
        std::fprintf(stderr, "[atalk] OUT LkUpReply %zu tuple(s) to %u.%u:%u "
                     "(first \"%s:%s@%s\" @ 2.%u:%u)\n",
                     cnt, to.net, to.node, to.sock,
                     cnt ? matches[0]->obj.c_str() : "", cnt ? matches[0]->type.c_str() : "",
                     zone_.c_str(), node_, cnt ? matches[0]->sock : 0);
    sendDdp(to, kSockNbp, kDdpNbp, r.data(), r.size());
}

// ── ATP ─────────────────────────────────────────────────────────────────

void AtalkStack::AtpTxn::respond(std::vector<std::vector<uint8_t>> pkts) {
    if (done_ || !st_) return;
    done_ = true;
    st_->sendAtpResponses(src, sock_, tid_, pkts, bitmap_);
    uint64_t key = txnKey(src, tid_);
    if (xo_) {
        // Exactly-once: cache the reply under the release timer so a
        // retransmitted TReq is answered from here, not re-executed.
        st_->xoCache_[key] = { std::move(pkts), src, sock_,
                              st_->now_ + 30 * st_->cpuHz_, st_->now_ };
    }
    st_->pendingTxns_.erase(key);
}

void AtalkStack::sendAtpResponses(const Addr& dst, uint8_t srcSock,
                                  uint16_t tid,
                                  const std::vector<std::vector<uint8_t>>& pkts,
                                  uint8_t bitmap) {
    for (size_t i = 0; i < pkts.size() && i < 8; i++) {
        if (!(bitmap & (1u << i))) continue;
        std::vector<uint8_t> p;
        p.push_back(uint8_t(kAtpTResp | (i + 1 == pkts.size() ? kAtpEom : 0)));
        p.push_back(uint8_t(i));
        put16(p, tid);
        const std::vector<uint8_t>& pk = pkts[i];
        // netatalk convention: pk = 4 user bytes + data
        if (pk.size() >= 4) p.insert(p.end(), pk.begin(), pk.end());
        else { p.insert(p.end(), pk.begin(), pk.end()); p.resize(p.size() + (4 - pk.size()), 0); }
        sendDdp(dst, srcSock, kDdpAtp, p.data(), p.size());
    }
}

void AtalkStack::handleAtp(const Addr& src, uint8_t dstSock, const uint8_t* p,
                           size_t n) {
    if (n < 8) return;
    uint8_t ctrl = p[0], bs = p[1];
    uint16_t tid = uint16_t(p[2]) << 8 | p[3];
    switch (ctrl & kAtpFuncMask) {
    case kAtpTReq: {
        stats_.atpReqIn++;
        uint64_t key = txnKey(src, tid);
        auto cached = xoCache_.find(key);
        if (cached != xoCache_.end()) {
            // The client asked again for a transaction we HAVE answered.
            // How long after our reply tells which failure it is: ~1-2 s =
            // its ATP timer expired (the reply never reached the driver);
            // much less = the reply reached it damaged or incomplete.
            stats_.atpDupReqs++;
            const int64_t lag = now_ - cached->second.sentAt;
            const long lagMs = cpuHz_ ? long(lag * 1000 / cpuHz_) : 0;
            stats_.atpDupLagLastMs = lagMs;
            if (lagMs > stats_.atpDupLagMaxMs) stats_.atpDupLagMaxMs = lagMs;
            if (atalkDbg())
                std::fprintf(stderr, "[atalk] ATP RETRANS tid=%u sock=%u from "
                             "%u.%u:%u  %ld ms after our reply  (bitmap asked "
                             "$%02X, %zu pkt cached)\n", tid, dstSock,
                             src.net, src.node, src.sock, lagMs, bs,
                             cached->second.pkts.size());
            sendAtpResponses(src, dstSock, tid, cached->second.pkts, bs);
            return;
        }
        if (pendingTxns_.count(key)) {          // reply in flight (deferred)
            stats_.atpDupPending++;             // = we are the slow one
            if (atalkDbg())
                std::fprintf(stderr, "[atalk] ATP RETRANS tid=%u sock=%u while "
                             "still serving the original (server too slow)\n",
                             tid, dstSock);
            return;
        }
        auto txn = std::make_shared<AtpTxn>();
        txn->src = src;
        txn->req.assign(p + 4, p + n);          // user bytes + data
        txn->st_ = this;
        txn->tid_ = tid;
        txn->bitmap_ = bs;
        txn->sock_ = dstSock;
        txn->xo_ = (ctrl & kAtpXo) != 0;
        pendingTxns_[key] = txn;
        atpHandlers_[dstSock](txn);
        // Synchronous handlers already erased the entry via respond();
        // a handler that ignored the request should not leak it.
        if (!txn->done_ && pendingTxns_.count(key) && txn.use_count() <= 2)
            pendingTxns_.erase(key);
        return;
    }
    case kAtpTResp: {
        for (auto it = requests_.begin(); it != requests_.end(); ++it) {
            if (it->tid != tid) continue;
            uint8_t seq = bs & 0x07;
            if (it->resp.size() <= seq) it->resp.resize(seq + 1);
            it->resp[seq].assign(p + 4, p + n);
            it->need &= ~uint8_t(1u << seq);
            if (ctrl & kAtpEom) {
                it->respCount = seq + 1;
                for (int b = seq + 1; b < 8; b++) it->need &= ~uint8_t(1u << b);
            }
            if (it->need == 0) {
                if (it->respCount >= 0) it->resp.resize(it->respCount);
                if (it->xo) {
                    std::vector<uint8_t> rel;
                    rel.push_back(kAtpTRel);
                    rel.push_back(0);
                    put16(rel, tid);
                    rel.resize(8, 0);
                    sendDdp(it->dst, it->srcSock, kDdpAtp, rel.data(), rel.size());
                }
                auto done = std::move(it->done);
                auto resp = std::move(it->resp);
                requests_.erase(it);
                if (done) done(true, resp);
            }
            return;
        }
        return;
    }
    case kAtpTRel:
        xoCache_.erase(txnKey(src, tid));
        return;
    default:
        return;
    }
}

void AtalkStack::atpRequest(const Addr& dst, uint8_t srcSock,
                            std::vector<uint8_t> req, int maxResp, bool xo,
                            std::function<void(bool, std::vector<std::vector<uint8_t>>&)> done) {
    if (req.size() < 4) req.resize(4, 0);
    uint16_t tid = nextTid_++;
    if (!nextTid_) nextTid_ = 1;
    uint8_t bitmap = maxResp > 0 ? uint8_t((1u << std::min(maxResp, 8)) - 1) : 0;
    std::vector<uint8_t> pkt;
    pkt.push_back(kAtpTReq | (xo ? kAtpXo : 0));
    pkt.push_back(bitmap);
    put16(pkt, tid);
    pkt.insert(pkt.end(), req.begin(), req.end());
    sendDdp(dst, srcSock, kDdpAtp, pkt.data(), pkt.size());
    if (maxResp <= 0) {
        // Fire-and-forget (ASP/PAP tickles): no transaction state.
        std::vector<std::vector<uint8_t>> none;
        if (done) done(true, none);
        return;
    }
    PendingReq pr;
    pr.tid = tid;
    pr.dst = dst;
    pr.srcSock = srcSock;
    pr.req = std::move(req);
    pr.need = bitmap;
    pr.xo = xo;
    pr.tries = 1;
    pr.nextRetry = now_ + cpuHz_;
    pr.done = std::move(done);
    requests_.push_back(std::move(pr));
}
