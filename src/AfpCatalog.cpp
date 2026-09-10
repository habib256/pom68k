// POM68K — AFP catalogue identity and path mapping.
#include "AfpServer.h"
#include "AfpAtomicFile.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/file.h>
#else
#include <io.h>
#include <sys/locking.h>
#endif

namespace {
constexpr const char* catalogName = "/.pom68k-afp-catalog";
constexpr size_t maxCatalogBytes = 16 * 1024 * 1024;
void closeCatalogLock(int& fd) {
    if (fd < 0) return;
#ifdef _WIN32
    ::_close(fd);
#else
    ::close(fd);
#endif
    fd = -1;
}
void append32(std::string& bytes, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes += char(v >> shift);
}
uint32_t checksum(const std::string& bytes) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : bytes) hash = (hash ^ c) * 16777619u;
    return hash;
}
bool validCatalogPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.back() == '/') return false;
    size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end - start);
        if (part.empty() || part.front() == '.' || part.find('\0') != std::string::npos)
            return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
}

AfpServer::~AfpServer() {
    setEnabled(false);
    closeCatalogLock(catalogLock_);
}

void AfpServer::loadCatalog() {
    closeCatalogLock(catalogLock_);
    pendingSource_.clear(); pendingDestination_.clear(); pendingSidecar_ = false;
    pendingDelete_ = false;
    if (dir_.empty()) return;
    const std::string path = dir_ + catalogName;
    const std::string lock = path + ".lock";
#ifdef _WIN32
    catalogLock_ = ::_open(lock.c_str(), _O_RDWR | _O_CREAT | _O_BINARY,
                           _S_IREAD | _S_IWRITE);
    const bool locked = catalogLock_ >= 0 && ::_locking(catalogLock_, _LK_NBLCK, 1) == 0;
#else
    catalogLock_ = ::open(lock.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat lockStat{};
    const bool locked = catalogLock_ >= 0 && ::fstat(catalogLock_, &lockStat) == 0 &&
                        S_ISREG(lockStat.st_mode) && ::flock(catalogLock_, LOCK_EX | LOCK_NB) == 0;
#endif
    if (!locked) {
        closeCatalogLock(catalogLock_);
        throw CatalogError("AFP catalogue is busy or its lock is inaccessible");
    }
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == std::filesystem::file_type::not_found)) return;
    if (ec || !std::filesystem::is_regular_file(status))
        throw CatalogError("Cannot read AFP catalogue: " + path);
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 20 || size > maxCatalogBytes)
        throw CatalogError("Invalid AFP catalogue size: " + path);
    std::ifstream in(path, std::ios::binary);
    std::string bytes(size_t(size), '\0');
    if (!in.read(bytes.data(), std::streamsize(size)))
        throw CatalogError("Cannot read AFP catalogue: " + path);
    size_t offset = 8;
    auto read32 = [&]() {
        if (offset + 4 > bytes.size()) throw CatalogError("Truncated AFP catalogue");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | uint8_t(bytes[offset++]);
        return v;
    };
    const bool identities = bytes.substr(0, 8) == "POMCNID4";
    const bool deletion = identities || bytes.substr(0, 8) == "POMCNID3";
    const bool journal = deletion || bytes.substr(0, 8) == "POMCNID2";
    if (!journal && bytes.substr(0, 8) != "POMCNID1")
        throw CatalogError("Unknown AFP catalogue format");
    const uint32_t next = read32(), count = read32();
    if (next < 16 || count > (bytes.size() - 20) / 9)
        throw CatalogError("Invalid AFP catalogue header");
    std::map<uint32_t, std::string> ids{{2, ""}};
    std::map<std::string, uint32_t> paths{{"", 2}};
    std::map<uint32_t, std::string> hosts;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t id = read32(), length = read32();
        if (id < 16 || id >= next || length > bytes.size() - offset)
            throw CatalogError("Invalid AFP catalogue record");
        const auto rel = bytes.substr(offset, length);
        offset += length;
        if (!validCatalogPath(rel) || !ids.emplace(id, rel).second ||
            !paths.emplace(rel, id).second) throw CatalogError("Invalid AFP catalogue identity");
        if (identities) {
            const auto size = read32();
            if (size == 0 || size > 256 || size > bytes.size() - offset)
                throw CatalogError("Invalid AFP host identity length");
            const auto identity = bytes.substr(offset, size); offset += size;
            if (identity.find_first_not_of("0123456789:") != std::string::npos)
                throw CatalogError("Invalid AFP host identity");
            hosts.emplace(id, identity);
        }
    }
    std::string source, destination;
    uint32_t sidecar = 0;
    if (journal) {
        auto readPath = [&]() {
            const auto length = read32();
            if (length > bytes.size() - offset) throw CatalogError("Truncated AFP move journal");
            auto value = bytes.substr(offset, length); offset += length;
            return value;
        };
        source = readPath(); destination = readPath(); sidecar = read32();
        const bool deleting = (sidecar & 2) != 0;
        if (sidecar > (deletion ? 3u : 1u) || (source.empty() &&
            (!destination.empty() || sidecar)) || (!source.empty() &&
             (!validCatalogPath(source) || !paths.count(source) ||
              (deleting ? !destination.empty() :
               (!validCatalogPath(destination) || source == destination)))))
            throw CatalogError("Invalid AFP move journal");
    }
    const size_t payload = offset;
    const auto stored = read32();
    if (offset != bytes.size() || checksum(bytes.substr(0, payload)) != stored)
        throw CatalogError("AFP catalogue checksum mismatch");
    idToPath_ = std::move(ids); pathToId_ = std::move(paths); nextId_ = next;
    idToHostIdentity_ = std::move(hosts);
    pendingSource_ = std::move(source); pendingDestination_ = std::move(destination);
    pendingSidecar_ = (sidecar & 1) != 0; pendingDelete_ = (sidecar & 2) != 0;
    recoverCatalogMove();
}

void AfpServer::saveCatalog() {
    std::string bytes = "POMCNID4";
    append32(bytes, nextId_); append32(bytes, uint32_t(pathToId_.size() - 1));
    for (const auto& [path, id] : pathToId_) {
        if (id == 2) continue;
        append32(bytes, id); append32(bytes, uint32_t(path.size())); bytes += path;
        auto& identity = idToHostIdentity_[id];
        if (identity.empty()) identity = hostIdentity(path); // legacy snapshot migration
        if (identity.empty()) throw CatalogError("Cannot bind missing legacy AFP identity");
        append32(bytes, uint32_t(identity.size())); bytes += identity;
        if (bytes.size() > maxCatalogBytes - 4) throw CatalogError("AFP catalogue is full");
    }
    append32(bytes, uint32_t(pendingSource_.size())); bytes += pendingSource_;
    append32(bytes, uint32_t(pendingDestination_.size())); bytes += pendingDestination_;
    append32(bytes, (pendingSidecar_ ? 1 : 0) | (pendingDelete_ ? 2 : 0));
    if (bytes.size() > maxCatalogBytes - 4) throw CatalogError("AFP catalogue is full");
    append32(bytes, checksum(bytes));
    const std::string destination = dir_ + catalogName;
    if (!afpAtomicWrite(destination, destination + ".tmp",
        {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}))
        throw CatalogError("Cannot commit AFP catalogue");
}

std::string AfpServer::pathForId(uint32_t id) const {
    auto it = idToPath_.find(id);
    if (it == idToPath_.end()) return "\x01";
    const auto host = idToHostIdentity_.find(id);
    if (host != idToHostIdentity_.end() && hostIdentity(it->second) != host->second)
        return "\x01";                       // never serve replacement bytes through an old ID
    return it->second;
}

uint32_t AfpServer::idForPath(const std::string& rel) {
    if (rel.empty()) return 2;
    const auto identity = hostIdentity(rel);
    if (identity.empty()) throw CatalogError("Cannot identify missing AFP object");
    auto it = pathToId_.find(rel);
    if (it != pathToId_.end()) {
        const auto known = idToHostIdentity_.find(it->second);
        if (known == idToHostIdentity_.end() || known->second == identity) {
            idToHostIdentity_[it->second] = identity;
            return it->second;
        }
        dropId(rel, false);                   // same name, different host object
    }
    uint32_t moved = 0;
    for (const auto& [id, known] : idToHostIdentity_) {
        if (known != identity || hostIdentity(idToPath_.at(id)) == identity) continue;
        // Hard links can make an offline rename ambiguous. Only recover a
        // unique displaced identity; never merge two catalogue entries.
        if (moved) { moved = 0; break; }
        moved = id;
    }
    if (moved) {
        const auto previous = idToPath_.at(moved);
        moveIds(previous, rel);
        return moved;
    }
    if (nextId_ == std::numeric_limits<uint32_t>::max())
        throw CatalogError("AFP catalogue identifiers exhausted");
    uint32_t id = nextId_++;
    pathToId_[rel] = id;
    idToPath_[id] = rel;
    idToHostIdentity_[id] = identity;
    saveCatalog();
    return id;
}

void AfpServer::dropId(const std::string& rel, bool persist) {
    const std::string prefix = rel + "/";
    for (auto it = pathToId_.begin(); it != pathToId_.end();) {
        if (it->first == rel || it->first.rfind(prefix, 0) == 0) {
            idToHostIdentity_.erase(it->second);
            idToPath_.erase(it->second);
            it = pathToId_.erase(it);
        } else ++it;
    }
    if (persist) saveCatalog();
}

void AfpServer::moveIds(const std::string& source, const std::string& destination) {
    // Open forks and guest directory references carry CNIDs. Moving a parent
    // changes every descendant's path, never its identity (afp_catalog_checks).
    std::map<std::string, uint32_t> moved;
    const std::string prefix = source + "/";
    for (const auto& [path, id] : pathToId_)
        if (path == source || path.rfind(prefix, 0) == 0)
            moved.emplace(destination + path.substr(source.size()), id);
    const auto identities = idToHostIdentity_;
    dropId(source, false);
    for (const auto& [path, id] : moved) {
        pathToId_[path] = id;
        idToPath_[id] = path;
        const auto identity = identities.find(id);
        if (identity != identities.end()) idToHostIdentity_[id] = identity->second;
    }
    saveCatalog();
}
