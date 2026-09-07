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

#ifdef _WIN32
inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name, value ? value : "");
}
inline int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif
