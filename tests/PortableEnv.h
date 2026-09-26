// POM68K — setenv/unsetenv for the test executables, on every host.
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The gates set process-environment knobs before building their rig (the
// tests are the startup boundary; production reads the snapshot). POSIX
// spells that setenv/unsetenv; MSVC has _putenv_s and no setenv at all —
// the first MSVC compile of the test tree (release dispatch 34149915451,
// 2026-09-07) failed on exactly these calls. Include this header and keep
// writing setenv(); Windows gets an equivalent with the same signature.
#pragma once

#include <cstdlib>

#include <string>

#if defined(_WIN32)
#include <process.h>
inline int pom68kProcessId() { return _getpid(); }
#else
#include <unistd.h>
inline int pom68kProcessId() { return int(getpid()); }
#endif

// A scratch path the host can actually write: TMPDIR (POSIX), then TEMP/TMP
// (Windows), then /tmp. Two gates wrote to a literal /tmp/… and the first
// MSVC asset-none run found no such directory (2026-09-07).
inline std::string pom68kTempPath(const char* leaf) {
    for (const char* var : {"TMPDIR", "TEMP", "TMP"}) {
        const char* dir = std::getenv(var);
        if (dir && *dir) return std::string(dir) + "/" + leaf;
    }
    return std::string("/tmp/") + leaf;
}

// pom68kTempPath with the process id in the leaf: "<stem>_<pid><ext>". A
// scratch file a gate writes, attaches and deletes must not be shared by
// name — gates run in parallel under ctest -j, and one process removing the
// file another is about to open lost the Infinite HD companion a race
// (2026-09-16) and the agent probe's blank volume another (2026-09-26).
inline std::string pom68kProcessTempPath(const char* stem, const char* ext) {
    return pom68kTempPath((std::string(stem) + "_" +
                           std::to_string(long(pom68kProcessId())) + ext)
                              .c_str());
}

#ifdef _WIN32
inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name, value ? value : "");
}
inline int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif
