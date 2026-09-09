// AFP catalogue regressions through the ASP wire harness.
#pragma once

void catalogMoveChecks(Wire& w, uint8_t sid, uint16_t& seq,
                       const std::string& dir) {
    fs::create_directories(dir + "/Tree/Child");
    fs::create_directories(dir + "/TreeOther");
    { std::ofstream out(dir + "/Tree/Child/data"); out << "catalogue"; }
    std::vector<uint8_t> reply;
    auto identify = [&](const std::string& path) {
        std::vector<uint8_t> cmd{34, 0};
        put16v(cmd, 1); put32v(cmd, 2);
        put16v(cmd, 0x0100); put16v(cmd, 0x0100);
        cmd.push_back(2); putP(cmd, path);
        CHECK(aspCmd(w, sid, seq++, cmd, reply) == 0,
              "catalogue object identified over AFP");
        return reply.size() >= 10 ? get32(reply.data() + 6) : 0;
    };
    const auto tree = identify("Tree");
    const auto child = identify(std::string("Tree\0Child", 10));
    const auto file = identify(std::string("Tree\0Child\0data", 15));
    const auto sibling = identify("TreeOther");
    std::vector<uint8_t> open{26, 0};
    put16v(open, 1); put32v(open, child);
    put16v(open, 0); put16v(open, 1);
    open.push_back(2); putP(open, "data");
    CHECK(aspCmd(w, sid, seq++, open, reply) == 0,
          "open descendant fork before moving parent");
    const uint16_t fork = reply.size() >= 4 ? get16(reply.data() + 2) : 0;
    std::vector<uint8_t> rename{28, 0};
    put16v(rename, 1); put32v(rename, 2);
    rename.push_back(2); putP(rename, "Tree");
    rename.push_back(2); putP(rename, "Renamed");
    CHECK(aspCmd(w, sid, seq++, rename, reply) == 0, "rename parent via AFP");
    CHECK(identify("Renamed") == tree, "renamed parent retains CNID");
    CHECK(identify(std::string("Renamed\0Child", 13)) == child,
          "renamed parent's child retains CNID");
    CHECK(identify(std::string("Renamed\0Child\0data", 18)) == file,
          "renamed parent's file retains CNID");
    CHECK(identify("TreeOther") == sibling, "prefix sibling retains CNID");
    std::vector<uint8_t> move{23, 0};
    put16v(move, 1); put32v(move, 2); put32v(move, sibling);
    move.push_back(2); putP(move, "Renamed");
    move.push_back(2); putP(move, "");
    move.push_back(2); putP(move, "Moved");
    CHECK(aspCmd(w, sid, seq++, move, reply) == 0, "move parent via AFP");
    CHECK(identify(std::string("TreeOther\0Moved\0Child", 21)) == child,
          "moved descendant retains CNID");
    std::vector<uint8_t> read{27, 0};
    put16v(read, fork); put32v(read, 0); put32v(read, 9);
    read.push_back(0); read.push_back(0);
    CHECK(aspCmd(w, sid, seq++, read, reply) == 0 &&
          std::string(reply.begin(), reply.end()) == "catalogue",
          "original open fork reads exact bytes after parent rename and move");

    // Host rename replaces an existing file on POSIX. AFP must instead refuse
    // the collision, preserving both contents and both already-issued CNIDs.
    { std::ofstream out(dir + "/source"); out << "source bytes"; }
    { std::ofstream out(dir + "/target"); out << "target bytes"; }
    const auto sourceId = identify("source");
    const auto targetId = identify("target");
    auto unchanged = [&] {
        std::ifstream source(dir + "/source"), target(dir + "/target");
        const std::string a((std::istreambuf_iterator<char>(source)), {});
        const std::string b((std::istreambuf_iterator<char>(target)), {});
        CHECK(a == "source bytes" && b == "target bytes",
              "collision preserves both files byte for byte");
        CHECK(identify("source") == sourceId && identify("target") == targetId,
              "collision preserves both CNIDs");
    };
    rename = {28, 0}; put16v(rename, 1); put32v(rename, 2);
    rename.push_back(2); putP(rename, "source");
    rename.push_back(2); putP(rename, "target");
    CHECK(aspCmd(w, sid, seq++, rename, reply) == -5017,
          "FPRename rejects existing destination");
    unchanged();
    move = {23, 0}; put16v(move, 1); put32v(move, 2); put32v(move, 2);
    move.push_back(2); putP(move, "source");
    move.push_back(2); putP(move, "");
    move.push_back(2); putP(move, "target");
    CHECK(aspCmd(w, sid, seq++, move, reply) == -5017,
          "FPMoveAndRename rejects existing destination");
    unchanged();
    fs::create_directories(dir + "/HiddenOnly/.AppleDouble");
    { std::ofstream out(dir + "/HiddenOnly/.host-data"); out << "hidden host data"; }
    { std::ofstream out(dir + "/HiddenOnly/.AppleDouble/keep"); out << "metadata"; }
    std::vector<uint8_t> remove{8, 0}; put16v(remove, 1); put32v(remove, 2);
    remove.push_back(2); putP(remove, "HiddenOnly");
    CHECK(aspCmd(w, sid, seq++, remove, reply) == -5007 &&
          fs::exists(dir + "/HiddenOnly/.host-data") &&
          fs::exists(dir + "/HiddenOnly/.AppleDouble/keep"),
          "failed delete preserves hidden host data and metadata");
    const auto movedPath = dir + "/TreeOther/Moved/Child/data";
    std::vector<uint8_t> resize{31, 0}; put16v(resize, fork);
    put16v(resize, 0x0200); put32v(resize, 1);
    CHECK(aspCmd(w, sid, seq++, resize, reply) == -5000 && fs::file_size(movedPath) == 9,
          "read-only open fork cannot truncate the host file");
    fs::rename(movedPath, dir + "/.old-host-object");
    { std::ofstream out(movedPath); out << "replacement"; }
    CHECK(aspCmd(w, sid, seq++, read, reply) != 0,
          "old open fork cannot read a host replacement through the same pathname");
    CHECK(identify(std::string("TreeOther\0Moved\0Child\0data", 26)) != file,
          "host replacement receives a new CNID");
}
