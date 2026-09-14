// POM68K — the shipped guest agent (share/README.md)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// share/POM68KDisques.bin is what every package installs beside the
// executable and what GuiAgentAutostart.h puts into the boot volume. Three
// facts: it decodes as the MacBinary of an APPL named POM68KDisques with a
// resource fork; when dev/scsiagent/build/POM68KDisques.bin exists (a
// Retro68 build on this host) the two agree fork for fork — dates aside,
// which the build stamps — so a rebuilt agent cannot ship stale; and each
// packaging carries the file where findPath looks. Runs from the repository
// root; reads the tree, boots nothing.

#include "HfsInject.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) gFails++;
}
static std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}
static std::string text(const std::string& path) {
    const std::vector<uint8_t> b = slurp(path);
    return std::string(b.begin(), b.end());
}

int main() {
    using namespace hfsinject;
    std::string err;
    const std::vector<uint8_t> shipped = slurp("share/POM68KDisques.bin");
    check(!shipped.empty(), "share/POM68KDisques.bin is present");
    MacBinary app;
    check(decodeMacBinary(shipped, app, err), "the shipped file decodes as MacBinary (" + err + ")");
    check(app.name == "POM68KDisques" && app.type == "APPL",
          "it is the application POM68KDisques (" + app.name + " / " + app.type + ")");
    check(app.rsrc.size() > 16 * 1024 && app.data.empty(),
          "its code is in the resource fork (" + std::to_string(app.rsrc.size()) + " bytes)");

    const std::vector<uint8_t> built = slurp("dev/scsiagent/build/POM68KDisques.bin");
    if (built.empty()) {
        std::printf("note: no Retro68 build output on this host; identity with the "
                    "build not compared\n");
    } else {
        MacBinary fresh;
        check(decodeMacBinary(built, fresh, err), "the Retro68 build output decodes");
        check(fresh.name == app.name && fresh.type == app.type && fresh.creator == app.creator &&
              fresh.rsrc == app.rsrc && fresh.data == app.data,
              "the shipped copy matches the Retro68 build fork for fork — copy "
              "dev/scsiagent/build/POM68KDisques.bin to share/ otherwise");
    }

    struct Packaging { const char* file; const char* needle; };
    const Packaging packagings[] = {
        {"packaging/linux/build_appimage.sh", "share/POM68KDisques.bin"},
        {"packaging/raspberry/build_native_pi.sh", "share"},
        {"package_macos_release.sh", "Resources/POM68KDisques.bin"},
        {".github/workflows/release.yml", "share/POM68KDisques.bin"},
    };
    for (const Packaging& p : packagings)
        check(text(p.file).find(p.needle) != std::string::npos,
              std::string("packaging carries the agent: ") + p.file);
    check(text("src/GuiAgentAutostart.h").find("\"share/POM68KDisques.bin\"") != std::string::npos,
          "the runner looks for the shipped copy");

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS: share/POM68KDisques.bin is the agent, matches the build, and ships in every package\n");
    return 0;
}
