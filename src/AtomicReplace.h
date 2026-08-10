// POM68K — portable replacement of an existing file after writing a temp.
#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
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
