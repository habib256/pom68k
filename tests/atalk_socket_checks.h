// A restarted AFP listener has a new local socket. Transactions on the old
// socket must neither supply cached replies nor swallow requests on the new one.
#pragma once
inline void socketScopedAtpChecks() {
    Wire wire;
    int first = 0, second = 0;
    wire.st.bindAtp(129, [&](auto t) { ++first; t->respond({{0, 0, 0, 0, 'A'}}); });
    wire.st.bindAtp(132, [&](auto t) { ++second; t->respond({{0, 0, 0, 0, 'B'}}); });
    wire.atpReq(47, 201, 129, 0x7000, {3, 0, 0, 0});
    wire.clear();
    wire.atpReq(47, 201, 132, 0x7000, {3, 0, 0, 0});
    auto packets = wire.atpResps(0x7000, 201);
    CHECK(first == 1 && second == 1 && packets.size() == 1 &&
          packets[0] == std::vector<uint8_t>({0, 0, 0, 0, 'B'}),
          "same client/TID on a new listener cannot reuse the old listener reply");
    wire.sendDdp(47, 201, 129, 3, {0xc0, 0, 0x70, 0, 0, 0, 0, 0});
    wire.clear();
    wire.atpReq(47, 201, 132, 0x7000, {3, 0, 0, 0});
    CHECK(second == 1, "release on old listener does not release new listener's XO cache");

    std::shared_ptr<AtalkStack::AtpTxn> deferred;
    wire.st.bindAtp(129, [&](auto t) { deferred = std::move(t); });
    wire.atpReq(47, 201, 129, 0x7001, {3, 0, 0, 0});
    wire.clear();
    wire.atpReq(47, 201, 132, 0x7001, {3, 0, 0, 0});
    packets = wire.atpResps(0x7001, 201);
    CHECK(second == 2 && packets.size() == 1,
          "deferred old-listener request cannot swallow a new-listener request");
    if (deferred) deferred->respond({{0, 0, 0, 0, 'A'}});
    wire.clear();
    wire.atpReq(47, 201, 132, 0x7001, {3, 0, 0, 0});
    packets = wire.atpResps(0x7001, 201);
    CHECK(packets.size() == 1 && packets[0] == std::vector<uint8_t>({0, 0, 0, 0, 'B'}),
          "late old-listener completion cannot replace new-listener cache contents");
}
