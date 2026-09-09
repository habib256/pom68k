// Restart/persistence checks using new server instances and real ASP commands.
#pragma once

void catalogPersistenceChecks(const std::string& parent) {
    const std::string dir = parent + "/Persistence";
    fs::create_directories(dir + "/A");
    fs::create_directories(dir + "/Z");
    uint32_t a = 0, z = 0, deleted = 0, replacement = 0;
    auto visit = [&](auto checks) {
        Wire wire;
        AfpServer server(wire.st);
        server.configure("POM68K", "Partage", dir);
        CHECK(server.status().catalogError.empty(), "persistent catalogue loads");
        server.setEnabled(true);
        const uint16_t tid = g_tid++;
        wire.atpReq(47, 200, 129, tid, {4, 200, 1, 0});
        const auto packets = wire.atpResps(tid, 200);
        CHECK(!packets.empty() && packets[0].size() >= 4, "restart opens ASP session");
        if (packets.empty() || packets[0].size() < 4) return;
        const uint8_t sid = packets[0][1];
        uint16_t seq = 0;
        std::vector<uint8_t> reply;
        auto command = [&](const std::vector<uint8_t>& cmd) {
            return aspCmd(wire, sid, seq++, cmd, reply);
        };
        std::vector<uint8_t> login{18};
        putP(login, "AFPVersion 2.1"); putP(login, "No User Authent");
        CHECK(command(login) == 0, "restart logs in");
        auto identify = [&](const std::string& path) {
            std::vector<uint8_t> cmd{34, 0};
            put16v(cmd, 1); put32v(cmd, 2);
            put16v(cmd, 0x100); put16v(cmd, 0x100);
            cmd.push_back(2); putP(cmd, path);
            CHECK(command(cmd) == 0 && reply.size() >= 10, "restart identifies object");
            return reply.size() >= 10 ? get32(reply.data() + 6) : 0;
        };
        checks(identify, command);
    };
    visit([&](auto identify, auto) {
        a = identify("A"); z = identify("Z");
        Wire competitorWire;
        AfpServer competitor(competitorWire.st);
        competitor.configure("POM68K", "Partage", dir);
        CHECK(!competitor.status().catalogError.empty(), "second catalogue writer refused");
    });
    // Reverse discovery order: a per-run allocator cannot pass by coincidence.
    visit([&](auto identify, auto command) {
        CHECK(identify("Z") == z && identify("A") == a,
              "CNIDs survive destruction and reversed discovery after restart");
        deleted = a;
        std::vector<uint8_t> cmd{8, 0}; put16v(cmd, 1); put32v(cmd, 2);
        cmd.push_back(2); putP(cmd, "A");
        CHECK(command(cmd) == 0, "persistent object deleted via AFP");
        cmd[0] = 6;
        CHECK(command(cmd) == 0, "same pathname recreated via AFP");
        replacement = identify("A");
        CHECK(replacement > deleted && replacement > z, "deleted CNID never recycled");
    });
    // Simulate a terminated write before the atomic replacement. The last
    // committed snapshot, not the incomplete staging bytes, is authoritative.
    { std::ofstream out(dir + "/.pom68k-afp-catalog.tmp"); out << "POMCNID1 torn"; }
    visit([&](auto identify, auto) {
        CHECK(identify("Z") == z && identify("A") == replacement,
              "interrupted staging write preserves committed identities");
        fs::create_directory(dir + "/New");
        CHECK(identify("New") > replacement, "allocation recovers abandoned staging file");
    });
    // A storage error must be an AFP error, not a successful unpersisted ID.
    fs::create_directory(dir + "/.pom68k-afp-catalog.tmp");
    fs::create_directory(dir + "/Uncommitted");
    visit([&](auto, auto command) {
        std::vector<uint8_t> cmd{34, 0}; put16v(cmd, 1); put32v(cmd, 2);
        put16v(cmd, 0x100); put16v(cmd, 0x100);
        cmd.push_back(2); putP(cmd, "Uncommitted");
        CHECK(command(cmd) == -5014, "persistence failure returns AFP miscellaneous error");
        CHECK(command({16, 0}) == -5014, "later commands fail closed after storage failure");
    });
    fs::remove(dir + "/.pom68k-afp-catalog.tmp");
    visit([&](auto identify, auto) {
        CHECK(identify("A") == replacement && identify("Z") == z,
              "write failure leaves committed catalogue readable on restart");
    });
    uint32_t movedChild = 0;
    fs::create_directories(dir + "/.AppleDouble");
    { std::ofstream out(dir + "/A/child"); out << "retained child"; }
    { std::ofstream out(dir + "/.AppleDouble/A"); out << "retained metadata"; }
    // Data rename succeeds, but the destination sidecar directory cannot be
    // created. Restart must complete the pending operation, not lose metadata
    // or reassign the moved subtree's identifiers.
    { std::ofstream out(dir + "/Z/.AppleDouble"); out << "block sidecar directory"; }
    visit([&](auto identify, auto command) {
        movedChild = identify(std::string("A\0child", 7));
        std::vector<uint8_t> cmd{23, 0}; put16v(cmd, 1); put32v(cmd, 2); put32v(cmd, z);
        cmd.push_back(2); putP(cmd, "A");
        cmd.push_back(2); putP(cmd, "");
        cmd.push_back(2); putP(cmd, "Recovered");
        CHECK(command(cmd) == -5014, "interrupted metadata move fails closed");
    });
    std::ifstream journalIn(dir + "/.pom68k-afp-catalog", std::ios::binary);
    const std::string journal((std::istreambuf_iterator<char>(journalIn)), {});
    journalIn.close();
    auto restoreJournal = [&] {
        std::ofstream out(dir + "/.pom68k-afp-catalog", std::ios::binary | std::ios::trunc);
        out.write(journal.data(), std::streamsize(journal.size()));
        CHECK(bool(out), "restore owned crash-state catalogue fixture");
    };
    fs::remove(dir + "/Z/.AppleDouble");
    visit([&](auto identify, auto) {
        CHECK(identify(std::string("Z\0Recovered", 11)) == replacement &&
              identify(std::string("Z\0Recovered\0child", 17)) == movedChild,
              "restart recovers moved parent and descendant CNIDs");
        std::ifstream in(dir + "/Z/.AppleDouble/Recovered");
        const std::string metadata((std::istreambuf_iterator<char>(in)), {});
        CHECK(metadata == "retained metadata" && !fs::exists(dir + "/.AppleDouble/A"),
              "restart finishes pending sidecar move without losing bytes");
    });
    // Crash after sidecar rename but before the final catalogue commit: replay
    // must accept the already-moved metadata without moving or replacing it.
    restoreJournal();
    visit([&](auto identify, auto) {
        CHECK(identify(std::string("Z\0Recovered\0child", 17)) == movedChild,
              "recovery is idempotent after metadata has already moved");
    });
    // Crash immediately after journal preparation: host paths still have their
    // original names, so cancel the pending operation and retain the old IDs.
    fs::rename(dir + "/Z/Recovered", dir + "/A");
    fs::rename(dir + "/Z/.AppleDouble/Recovered", dir + "/.AppleDouble/A");
    restoreJournal();
    visit([&](auto identify, auto) {
        CHECK(identify("A") == replacement && identify(std::string("A\0child", 7)) == movedChild,
              "recovery cancels an unstarted move without changing identities");
    });
    fs::rename(dir + "/A", dir + "/.original-A");
    fs::create_directory(dir + "/A");
    restoreJournal();
    {
        Wire changedWire;
        AfpServer changed(changedWire.st);
        changed.configure("POM68K", "Partage", dir);
        CHECK(!changed.status().catalogError.empty(),
              "recovery refuses a replacement object at the pending source pathname");
    }
    fs::remove(dir + "/A");                 // empty replacement owned by this test
    fs::rename(dir + "/.original-A", dir + "/A");
    uint32_t victim = 0;
    { std::ofstream out(dir + "/Victim"); out << "old bytes"; }
    fs::create_directories(dir + "/.AppleDouble/Victim");
    { std::ofstream out(dir + "/.AppleDouble/Victim/blocker"); out << "block metadata removal"; }
    visit([&](auto identify, auto command) {
        victim = identify("Victim");
        std::vector<uint8_t> cmd{8, 0}; put16v(cmd, 1); put32v(cmd, 2);
        cmd.push_back(2); putP(cmd, "Victim");
        CHECK(command(cmd) == -5014, "post-delete metadata failure is reported, not ignored");
        CHECK(!fs::exists(dir + "/Victim"), "delete fault occurs after data removal");
    });
    fs::remove(dir + "/.AppleDouble/Victim/blocker");
    visit([&](auto identify, auto command) {
        CHECK(!fs::exists(dir + "/.AppleDouble/Victim"), "restart finishes metadata deletion");
        std::vector<uint8_t> cmd{7, 0}; put16v(cmd, 1); put32v(cmd, 2);
        cmd.push_back(2); putP(cmd, "Victim");
        CHECK(command(cmd) == 0 && identify("Victim") > victim,
              "recreated pathname cannot inherit interrupted deletion's CNID");
    });
    uint32_t stable = 0, replaced = 0;
    visit([&](auto identify, auto) { stable = identify("Victim"); });
    // Keep the old inode alive so this test never relies on allocator timing.
    fs::rename(dir + "/Victim", dir + "/.old-host-object");
    { std::ofstream out(dir + "/Victim"); out << "new object"; }
    visit([&](auto identify, auto) {
        replaced = identify("Victim");
        CHECK(replaced > stable, "offline host replacement cannot inherit a persisted CNID");
        { std::ofstream out(dir + "/Victim", std::ios::app); out << " edited"; }
        CHECK(identify("Victim") == replaced, "in-place content change preserves identity");
    });
    visit([&](auto identify, auto) {
        CHECK(identify("Victim") == replaced, "replacement identity persists across another restart");
    });
    fs::create_directories(dir + "/HostParent/Child");
    uint32_t hostParent = 0, hostChild = 0;
    visit([&](auto identify, auto) {
        hostParent = identify("HostParent");
        hostChild = identify(std::string("HostParent\0Child", 16));
    });
    fs::rename(dir + "/HostParent", dir + "/HostMoved");
    visit([&](auto identify, auto) {
        CHECK(identify("HostMoved") == hostParent &&
              identify(std::string("HostMoved\0Child", 15)) == hostChild,
              "rediscovery after offline host rename preserves subtree CNIDs");
    });
    { std::ofstream out(dir + "/.pom68k-afp-catalog", std::ios::binary | std::ios::app);
      out << "corrupt"; }
    Wire wire;
    AfpServer corrupt(wire.st);
    corrupt.configure("POM68K", "Partage", dir);
    CHECK(!corrupt.status().catalogError.empty(), "corrupt catalogue fails closed instead of resetting IDs");
}
