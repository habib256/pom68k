// POM68K — the relaunch line keeps every SCSI id where it was
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The Disques window's extras list is positional (entry i = SCSI id i+1)
// and the relaunch line is built from it. A gap in the middle — a disk
// detached live under an occupied higher id, or one attached live into a
// gap — used to vanish from the line and shift every id above it on
// relaunch. `relaunchExtras` (DiskBays.h) carries such a gap as
// kEmptyBayToken, which every runner's media loop consumes as « nothing
// here, next id », and drops the gaps at the end, which carry nothing.
// No ROM, no image.

#include "DiskBays.h"

#include <cstdio>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

int main() {
    using pom68k::kCdBayToken;
    using pom68k::kEmptyBayToken;
    using pom68k::relaunchExtras;
    using V = std::vector<std::string>;
    const std::string tok = kEmptyBayToken;

    check(relaunchExtras({}) == V{}, "an empty list stays empty");
    check(relaunchExtras({"a.vhd", "b.vhd"}) == V{"a.vhd", "b.vhd"},
          "a dense list is carried verbatim");
    check(relaunchExtras({kCdBayToken, "b.vhd"}) == V{kCdBayToken, "b.vhd"},
          "the CD-bay token is a bay, not a gap");
    check(relaunchExtras({"", "b.vhd"}) == V{tok, "b.vhd"},
          "an interior gap becomes the empty-bay token: SCSI 2 stays SCSI 2");
    check(relaunchExtras({kCdBayToken, "", "", "d.vhd"}) == V{kCdBayToken, tok, tok, "d.vhd"},
          "several interior gaps each keep their id");
    check(relaunchExtras({"a.vhd", "", ""}) == V{"a.vhd"},
          "trailing gaps carry nothing and are dropped");
    check(relaunchExtras({"a.vhd", tok, ""}) == V{"a.vhd"},
          "a trailing token is a gap too");
    check(relaunchExtras({"", "", ""}) == V{},
          "a list of gaps is an empty line");
    check(relaunchExtras({tok, "b.vhd"}) == V{tok, "b.vhd"},
          "a token already in the list passes through");

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS: the relaunch line carries interior gaps by token and drops trailing ones\n");
    return 0;
}
