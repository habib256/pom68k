// POM68K — portable replacement of an existing file after writing a temp.
#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
// windows.h defines min/max as macros unless told not to; every
// `std::numeric_limits<T>::max()` in a file that includes this header then
// fails with C2589 under MSVC (SonyDrive.cpp, release dispatch 2026-09-07).
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

inline bool atomicReplaceFile(const std::string& temporary,
                              const std::string& destination) {
#ifdef _WIN32
    // MSVCRT rename() refuses to replace an existing destination. The Win32
    // primitive supplies the POSIX semantics required by write-back.
    return MoveFileExA(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}
