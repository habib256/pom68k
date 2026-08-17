// POM68K — immutable reference-fixture routing, asset-free gate.

#include "FixtureStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}

static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = "fixture_store_test_tmp";
    const fs::path ref = root / "ref" / "system" / "boot.vhd";
    const fs::path work = root / "work" / "system" / "boot.vhd";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(ref.parent_path(), ec);
    { std::ofstream f(ref, std::ios::binary); f << "REFERENCE"; }

    auto first = pom68k::writableFixture(ref.string());
    check(first.reference && first.writable && first.copied,
          "first writable open clones a reference fixture");
    check(fs::path(first.path) == work && read(work) == "REFERENCE",
          "ref/ path maps byte-for-byte to the sibling work/ path");
    { std::ofstream f(work, std::ios::binary | std::ios::trunc); f << "GUEST-WRITE"; }
    check(read(ref) == "REFERENCE", "guest work cannot alter the reference bytes");

    auto second = pom68k::writableFixture(ref.string());
    check(second.reference && second.writable && !second.copied,
          "later sessions reopen the persistent work copy");
    check(read(second.path) == "GUEST-WRITE", "work copy persists across sessions");

    auto ordinary = pom68k::writableFixture((root / "user.vhd").string());
    check(!ordinary.reference && ordinary.path == (root / "user.vhd").string(),
          "an explicit non-reference image remains directly writable");

    fs::remove_all(root, ec);
    std::printf("%s\n", fails ? "FAILED" : "PASS");
    return fails ? 1 : 0;
}
