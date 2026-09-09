#include "AfpAtomicFile.h"
#include "AtomicReplace.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <io.h>
#endif

bool afpSyncDirectory(const std::filesystem::path& directory) {
#ifndef _WIN32
    const int fd = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool good = ::fsync(fd) == 0;
    ::close(fd);
    return good;
#else
    (void)directory;                      // atomicReplaceFile uses WRITE_THROUGH
    return true;
#endif
}

bool afpAtomicWrite(const std::filesystem::path& destination,
                    const std::filesystem::path& temporary,
                    std::span<const uint8_t> bytes) {
    namespace fs = std::filesystem;
    // Caller holds the volume's writer lock. Staging names must be reserved,
    // not aliases of other valid sidecars ("foo.tmp" can be a guest filename).
    std::error_code ec;
    const auto status = fs::symlink_status(temporary, ec);
    if (!ec && status.type() != fs::file_type::not_found) {
        if (!fs::is_regular_file(status) || !fs::remove(temporary, ec) || ec) return false;
    } else if (ec && ec != std::errc::no_such_file_or_directory) return false;
    FILE* out = std::fopen(temporary.string().c_str(), "wbx");
    if (!out) return false;
    bool good = std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
    good = std::fflush(out) == 0 && good;
#ifdef _WIN32
    good = ::_commit(::_fileno(out)) == 0 && good;
#else
    good = ::fsync(::fileno(out)) == 0 && good;
#endif
    good = std::fclose(out) == 0 && good;
    if (!good || !atomicReplaceFile(temporary.string(), destination.string())) return false;
    return afpSyncDirectory(destination.parent_path());
}
