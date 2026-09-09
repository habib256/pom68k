// AFP moves/deletions span host data, AppleDouble metadata and persistent CNIDs.
// The catalogue snapshot doubles as a write-ahead journal for those operations.
#include "AfpServer.h"
#include "AfpAtomicFile.h"
#include <filesystem>

namespace fs = std::filesystem;
namespace {
fs::path sidecar(const fs::path& host) {
    return host.parent_path() / ".AppleDouble" / host.filename();
}
// Unlike exists(), this also sees dangling symlinks. Never overwrite a name
// because its referent is absent; report inspection errors instead of guessing.
bool present(const fs::path& path) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
        return false;
    if (ec) throw std::runtime_error("Cannot inspect AFP move path: " + path.string());
    return status.type() != fs::file_type::not_found;
}
void syncParent(const fs::path& path) {
    if (!afpSyncDirectory(path.parent_path()))
        throw std::runtime_error("Cannot sync AFP mutation directory");
}
}

int AfpServer::moveHostPath(const std::string& source, const std::string& destination) {
    if (source.empty() || destination.empty()) return -5000;
    const auto src = fs::path(dir_) / source, dst = fs::path(dir_) / destination;
    try {
        if (!present(src)) return -5018;
        if (present(dst) || present(sidecar(dst))) return -5017;
        const bool hasSidecar = present(sidecar(src));
        idForPath(source);
        pendingSource_ = source; pendingDestination_ = destination;
        pendingSidecar_ = hasSidecar;
        saveCatalog();                        // durable BEFORE the host rename
        std::error_code ec;
        fs::rename(src, dst, ec);
        if (ec) {
            pendingSource_.clear(); pendingDestination_.clear(); pendingSidecar_ = false;
            saveCatalog();
            return -5018;
        }
        // Persist the data namespace before touching metadata. Otherwise a
        // power loss could restore the old data name after its sidecar moved.
        syncParent(src); syncParent(dst);
        finishCatalogMove();
        return 0;
    } catch (const std::runtime_error& e) { throw CatalogError(e.what()); }
}

void AfpServer::recoverCatalogMove() {
    if (pendingSource_.empty()) return;
    try {
        const bool source = present(fs::path(dir_) / pendingSource_);
        const auto oldId = pathToId_.find(pendingSource_);
        const auto identity = oldId == pathToId_.end() ? idToHostIdentity_.end() :
            idToHostIdentity_.find(oldId->second);
        if (identity != idToHostIdentity_.end()) {
            const auto actual = source ? hostIdentity(pendingSource_) :
                (pendingDelete_ ? std::string{} : hostIdentity(pendingDestination_));
            if ((source || !pendingDelete_) && actual != identity->second)
                throw CatalogError("Host object changed during pending AFP operation");
        }
        if (pendingDelete_) {
            if (!source) finishCatalogDelete();
            else {
                pendingSource_.clear(); pendingSidecar_ = false; pendingDelete_ = false;
                saveCatalog();             // interrupted before deleting data
            }
            return;
        }
        const bool destination = present(fs::path(dir_) / pendingDestination_);
        if (source && !destination) {
            // Interrupted after preparation, before data rename. Nothing moved.
            pendingSource_.clear(); pendingDestination_.clear(); pendingSidecar_ = false;
            saveCatalog();
        } else if (!source && destination) {
            finishCatalogMove();
        } else {
            // External host mutations made the journal ambiguous. Do not guess
            // which file owns a previously issued identity or overwrite data.
            throw CatalogError("Ambiguous pending AFP move; catalogue recovery refused");
        }
    } catch (const std::runtime_error& e) { throw CatalogError(e.what()); }
}

void AfpServer::finishCatalogMove() {
    if (pendingSidecar_) {
        const auto src = sidecar(fs::path(dir_) / pendingSource_);
        const auto dst = sidecar(fs::path(dir_) / pendingDestination_);
        const bool source = present(src), destination = present(dst);
        if (source == destination) throw CatalogError("Ambiguous AFP sidecar recovery");
        if (source) {
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            if (!ec) fs::rename(src, dst, ec);
            if (ec) throw CatalogError("Cannot finish AFP metadata move: " + ec.message());
        }
        syncParent(src); syncParent(dst);
    }
    const auto source = pendingSource_, destination = pendingDestination_;
    syncParent(fs::path(dir_) / source); syncParent(fs::path(dir_) / destination);
    pendingSource_.clear(); pendingDestination_.clear(); pendingSidecar_ = false;
    moveIds(source, destination);             // one commit clears the journal
}

int AfpServer::deleteHostPath(const std::string& source) {
    if (source.empty()) return -5000;
    const auto host = fs::path(dir_) / source;
    try {
        if (!present(host)) return -5018;
        std::error_code ec;
        const bool directory = fs::is_directory(fs::symlink_status(host, ec));
        if (ec) return -5000;
        // Inspect ALL entries before touching metadata, including host dotfiles.
        // A directory with hidden data is not empty just because AFP hides it.
        if (directory) {
            fs::directory_iterator entries(host, ec), end;
            if (ec) return -5000;
            while (entries != end) {
                if (entries->path().filename() != ".AppleDouble") return -5007;
                entries.increment(ec);
                if (ec) return -5000;
            }
        }
        idForPath(source);
        pendingSource_ = source; pendingDestination_.clear(); pendingDelete_ = true;
        pendingSidecar_ = present(sidecar(host));
        saveCatalog();
        if (directory) fs::remove_all(host / ".AppleDouble", ec);
        bool removed = false;
        if (!ec) removed = fs::remove(host, ec);
        if (ec || !removed) {
            pendingSource_.clear(); pendingSidecar_ = false; pendingDelete_ = false;
            saveCatalog();
            return -5000;
        }
        // Do not let sidecar removal become durable before data removal.
        syncParent(host);
        finishCatalogDelete();
        return 0;
    } catch (const std::runtime_error& e) { throw CatalogError(e.what()); }
}

void AfpServer::finishCatalogDelete() {
    const auto host = fs::path(dir_) / pendingSource_;
    if (pendingSidecar_) {
        const auto metadata = sidecar(host);
        std::error_code ec;
        fs::remove(metadata, ec);           // absence means an earlier replay removed it
        if (ec) throw CatalogError("Cannot finish AFP metadata deletion: " + ec.message());
        syncParent(metadata);
    }
    syncParent(host);
    const auto source = pendingSource_;
    pendingSource_.clear(); pendingSidecar_ = false; pendingDelete_ = false;
    dropId(source);                         // retire ID and clear intent atomically
}
