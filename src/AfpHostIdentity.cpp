// Object identity must survive content/mtime changes but not inode reuse.
#include "AfpServer.h"
#include <cerrno>
#include <sys/stat.h>
#ifdef _WIN32
#include "AtomicReplace.h"              // portable Windows header setup
#elif defined(__linux__)
#include <fcntl.h>
#endif

std::string AfpServer::hostIdentity(const std::string& relative) const {
    const std::string path = dir_ + "/" + relative;
    auto field = [](uint64_t value) { return std::to_string(value) + ":"; };
#ifdef _WIN32
    const HANDLE handle = CreateFileA(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
        throw CatalogError("Cannot inspect AFP host identity");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool good = GetFileInformationByHandle(handle, &info) != 0;
    CloseHandle(handle);
    if (!good) throw CatalogError("Cannot read AFP host identity");
    return field(info.dwVolumeSerialNumber) + field(info.nFileIndexHigh) +
        field(info.nFileIndexLow) + field(info.ftCreationTime.dwHighDateTime) +
        field(info.ftCreationTime.dwLowDateTime);
#elif defined(__linux__)
    // statx(2): returned stx_mask, not requested mask, decides field validity.
    // https://man7.org/linux/man-pages/man2/statx.2.html
    struct statx info{};
    constexpr unsigned fields = STATX_INO | STATX_BTIME;
    if (::statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, fields, &info) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return {};
        throw CatalogError("Cannot inspect AFP host identity with statx");
    }
    if ((info.stx_mask & fields) != fields)
        throw CatalogError("AFP identity requires filesystem inode and birth-time support");
    return field(info.stx_dev_major) + field(info.stx_dev_minor) + field(info.stx_ino) +
        field(uint64_t(info.stx_btime.tv_sec)) + field(info.stx_btime.tv_nsec);
#elif defined(__APPLE__)
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return {};
        throw CatalogError("Cannot inspect AFP host identity");
    }
    return field(uint64_t(info.st_dev)) + field(info.st_ino) +
        field(uint64_t(info.st_birthtimespec.tv_sec)) + field(info.st_birthtimespec.tv_nsec);
#else
    throw CatalogError("Persistent AFP identity is unsupported on this host platform");
#endif
}
