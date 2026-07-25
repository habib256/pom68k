// POM68K — shared harness for the in-process AppleTalk stack gates
// (atalk_stack_test, afp_server_test, pap_server_test, macip_gw_test).
// Drives AtalkStack directly at the LLAP frame level: builds the guest's
// short-DDP frames, parses everything the stack transmits, and speaks
// enough client-side ATP to exercise both engine roles.

#pragma once
#include "AtalkStack.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); failures++; } } while (0)

struct Datagram {
    uint8_t dstNode = 0, srcNode = 0;
    uint8_t dstSock = 0, srcSock = 0, ddpType = 0;
    std::vector<uint8_t> pay;
};

struct Wire {
    AtalkStack st;
    std::vector<Datagram> out;          // everything the stack transmitted
    int64_t now = 0;
    int64_t hz = 1000000;               // 1 MHz keeps second-scale timers cheap

    explicit Wire(uint8_t node = 128, uint16_t net = 2) {
        st.configure(net, node, "POM68K", hz);
        st.sendFrame = [this](const uint8_t* d, size_t n) { parse(d, n); };
        st.tick(0);
    }

    void parse(const uint8_t* d, size_t n) {
        if (n < 3) return;
        Datagram g;
        g.dstNode = d[0];
        g.srcNode = d[1];
        if (d[2] == 0x01 && n >= 8) {             // short DDP
            g.dstSock = d[5];
            g.srcSock = d[6];
            g.ddpType = d[7];
            g.pay.assign(d + 8, d + n);
        } else if (d[2] == 0x02 && n >= 16) {     // long DDP
            g.dstSock = d[13];
            g.srcSock = d[14];
            g.ddpType = d[15];
            g.pay.assign(d + 16, d + n);
        } else {
            g.ddpType = d[2];                     // LLAP control (ENQ/ACK…)
        }
        out.push_back(std::move(g));
    }

    void run(double secs) {
        int64_t end = now + int64_t(secs * double(hz));
        while (now < end) {
            now += hz / 100;                      // 10 ms steps
            st.tick(now);
        }
    }

    // guest → stack, short-DDP form
    void sendDdp(uint8_t gNode, uint8_t gSock, uint8_t dSock, uint8_t type,
                 const std::vector<uint8_t>& pay, uint8_t dstNode = 0) {
        std::vector<uint8_t> f;
        f.push_back(dstNode ? dstNode : st.node());
        f.push_back(gNode);
        f.push_back(0x01);
        uint16_t len = uint16_t(5 + pay.size());
        f.push_back(uint8_t(len >> 8));
        f.push_back(uint8_t(len));
        f.push_back(dSock);
        f.push_back(gSock);
        f.push_back(type);
        f.insert(f.end(), pay.begin(), pay.end());
        st.onGuestFrame(f.data(), f.size());
    }

    // client-side ATP: one TReq (payload = 4 user bytes + data)
    void atpReq(uint8_t gNode, uint8_t gSock, uint8_t dSock, uint16_t tid,
                const std::vector<uint8_t>& userAndData, bool xo = true,
                uint8_t bitmap = 0xFF) {
        std::vector<uint8_t> p;
        p.push_back(uint8_t(0x40 | (xo ? 0x20 : 0)));
        p.push_back(bitmap);
        p.push_back(uint8_t(tid >> 8));
        p.push_back(uint8_t(tid));
        p.insert(p.end(), userAndData.begin(), userAndData.end());
        if (p.size() < 12) p.resize(12, 0);
        sendDdp(gNode, gSock, dSock, 3, p);
    }

    // collect the TResp packets for a tid (each = 4 user bytes + data)
    std::vector<std::vector<uint8_t>> atpResps(uint16_t tid, uint8_t toSock) {
        std::vector<std::vector<uint8_t>> r;
        for (const Datagram& g : out) {
            if (g.ddpType != 3 || g.dstSock != toSock || g.pay.size() < 8) continue;
            if ((g.pay[0] & 0xC0) != 0x80) continue;
            uint16_t t = uint16_t(g.pay[2]) << 8 | g.pay[3];
            if (t != tid) continue;
            size_t seq = g.pay[1] & 7;
            if (r.size() <= seq) r.resize(seq + 1);
            r[seq].assign(g.pay.begin() + 4, g.pay.end());
        }
        return r;
    }

    // concatenated ASP reply: {result, data} from the TResp set
    int32_t aspResult(uint16_t tid, uint8_t toSock, std::vector<uint8_t>& data) {
        auto pk = atpResps(tid, toSock);
        data.clear();
        if (pk.empty() || pk[0].size() < 4) return 0x7FFFFFFF;
        int32_t res = int32_t(uint32_t(pk[0][0]) << 24 | uint32_t(pk[0][1]) << 16
                            | uint32_t(pk[0][2]) << 8 | pk[0][3]);
        for (auto& p : pk)
            if (p.size() > 4) data.insert(data.end(), p.begin() + 4, p.end());
        return res;
    }

    // find a server-initiated TReq addressed to (node, sock); returns tid
    // and fills userAndData, or -1
    int findTReq(uint8_t node, uint8_t sock, std::vector<uint8_t>& userAndData,
                 size_t skip = 0) {
        for (const Datagram& g : out) {
            if (g.dstNode != node || g.dstSock != sock || g.ddpType != 3) continue;
            if (g.pay.size() < 8 || (g.pay[0] & 0xC0) != 0x40) continue;
            if (skip) { skip--; continue; }
            userAndData.assign(g.pay.begin() + 4, g.pay.end());
            return uint16_t(g.pay[2]) << 8 | g.pay[3];
        }
        return -1;
    }

    // answer a server TReq with response packets (each = user4 + data)
    void atpRespond(uint8_t gNode, uint8_t gSock, uint8_t toSock, uint16_t tid,
                    const std::vector<std::vector<uint8_t>>& pkts) {
        for (size_t i = 0; i < pkts.size(); i++) {
            std::vector<uint8_t> p;
            p.push_back(uint8_t(0x80 | (i + 1 == pkts.size() ? 0x10 : 0)));
            p.push_back(uint8_t(i));
            p.push_back(uint8_t(tid >> 8));
            p.push_back(uint8_t(tid));
            p.insert(p.end(), pkts[i].begin(), pkts[i].end());
            sendDdp(gNode, gSock, toSock, 3, p);
        }
    }

    void clear() { out.clear(); }
};

inline void putP(std::vector<uint8_t>& v, const std::string& s) {
    v.push_back(uint8_t(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}
inline void put16v(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x >> 8); v.push_back(uint8_t(x));
}
inline void put32v(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
inline uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
