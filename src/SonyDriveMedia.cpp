// POM68K — host containers and physical medium insertion/persistence.
#include "SonyDrive.h"
#include "MoofImage.h"
#include "FixtureStore.h"
#include "AtomicReplace.h"
#include <fstream>
#include <cstdio>

void SonyDrive::eject() {
    if (sound_ && hasDisk()) sound_->click();
    flushToFile();                 // Mac OS has flushed its caches by now
    path_.clear();
    fileImage_.clear();
    dirty_ = false;
    image_.clear();
    medium_.clear(); nativeFile_.clear();
    activeTrack_ = -1; activeTrackWritten_ = false;
    tags_.clear();
    stream_.clear();
    cells_.clear();
    flux_.clear();
    fluxRev_ = 0;
    gcrWrBuf_.clear();
    streamPos_ = 0;
    hd_ = false;
    mfmMode_ = false;
    switched_ = true;
    wrState_ = 0;
}

bool SonyDrive::insert(const std::string& path) {
    // Insert-over-insert must honour the same write-back contract as eject():
    // insertImage() drops path_/dirty_ wholesale, so without this the outgoing
    // media's committed sectors are lost when the user picks a second image
    // straight from the Disques menu (no eject in between).
    if (hasDisk()) flushToFile();
    // The same contract as ScsiDisk::open: a writable session on a
    // disks35/ref/ fixture works on its disks35/work/ clone; the reference
    // bytes assets.lock pins are never opened for writing.
    bool writeBack = writeBack_;
    const std::string backing =
        pom68k::routeWritableOpen(path, "Floppy", writeBack);
    std::ifstream in(backing, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || size > std::streamoff(moof::kMaxBytes)) return false;
    in.seekg(0);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    std::string suffix = path.size() >= 5 ? path.substr(path.size() - 5) : path;
    for (char& c : suffix) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (moof::candidate(raw) || suffix == ".moof") {
        moof::Image imported;
        if (!moof::unpack(raw, imported)) return false;
        medium_ = std::move(imported.medium);
        nativeFile_ = std::move(raw); image_.clear(); tags_.clear(); fileImage_.clear();
        activeTrack_ = -1; activeTrackWritten_ = false;
        hd_ = imported.type == 3; doubleSided_ = imported.type != 1;
        writeProtected_ = imported.protectedDisk;
        path_ = backing; dirty_ = false; track_ = 0; side1_ = false;
        streamPos_ = 0; mfmMode_ = hd_; switched_ = false; wrState_ = 0; gcrWrBuf_.clear();
        if (motorOn_) readyCounter_ = 2;
        encodeTrack();
        if (sound_) sound_->click();
        return true;
    }
    FloppyFileImage fileImage;
    std::vector<uint8_t> tags;
    if (!fileImage.unpack(raw, tags, backing) || !insertImage(std::move(raw))) return false;
    path_ = backing;
    fileImage_ = std::move(fileImage);
    if (!tags.empty()) {
        tags_ = std::move(tags);
        encodeTrack(); // insertImage seeded zero tags before the container payload arrived.
    }
    dirty_ = false;
    return true;
}

bool SonyDrive::insertImage(std::vector<uint8_t> data) {
    if (data.size() != kSize800K && data.size() != kSize400K &&
        data.size() != kSize1440K)
        return false;
    path_.clear();                 // in-memory media has no backing file
    fileImage_.clear();
    dirty_ = false;
    medium_.clear(); nativeFile_.clear(); activeTrack_ = -1; activeTrackWritten_ = false;
    hd_ = (data.size() == kSize1440K);
    doubleSided_ = (data.size() != kSize400K);
    // HD media forces MFM; 800K/400K stay GCR (MAME mfd75w track_changed).
    // The mechanism is NOT promoted to SuperDrive by the media: an HD image
    // in a plain 800K drive is unreadable, exactly like the real thing —
    // SWIM platforms all setSuperDrive(true) at attach.
    mfmMode_ = hd_;
    image_ = std::move(data);
    tags_.assign(hd_ ? 0 : image_.size() / 512 * 12, 0);
    track_ = 0;
    side1_ = false;
    // MAME floppy.cpp:672-673 (init_floppy_load): inserting media SETS
    // m_dskchg on a Mac drive (m_dskchg_writable), i.e. CLEARS the change
    // latch — only eject raises it (call_unload, floppy.cpp:723).
    switched_ = false;
    wrState_ = 0;
    gcrWrBuf_.clear();
    if (motorOn_) readyCounter_ = 2;             // MAME call_load :666-669
    encodeTrack();
    if (sound_) sound_->click();
    return true;
}


bool SonyDrive::flushToFile() {
    if (!writeBack_ || !dirty_ || path_.empty() || !hasDisk()) return false;
    retainActiveTrack();
    // Raw .dsk persists sector data only; physical tags remain in memory
    // and snapshots. DC42/DART preserve the full tagged medium on disk.
    // Write-back switched on after the insert (the DAFB runner inserts its
    // floppy before configureFloppyWriteBack): the reference is still never
    // written in place — the flush goes to the work clone, which the next
    // insert of the same reference then reopens.
    if (pom68k::isReferenceFixturePath(path_)) {
        const pom68k::WritableFixture routed = pom68k::writableFixture(path_);
        if (!routed.reference || !routed.writable) {
            std::fprintf(stderr, "Floppy: immutable reference %s not flushed: %s\n",
                         path_.c_str(), routed.error.c_str());
            return false;
        }
        path_ = routed.path;
    }
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (medium_.native) {
            if (!moof::write(out, medium_, nativeFile_, writeProtected_)) {
                out.close(); std::remove(tmp.c_str()); return false;
            }
        } else if (fileImage_.wrapped()) {
            if (!fileImage_.write(out, image_, tags_)) {
                out.close(); std::remove(tmp.c_str()); return false;
            }
        } else {
            out.write(reinterpret_cast<const char*>(image_.data()), image_.size());
        }
        if (!out) { std::remove(tmp.c_str()); return false; }
    }
    if (!atomicReplaceFile(tmp, path_)) {
        std::remove(tmp.c_str());
        return false;
    }
    dirty_ = false;
    return true;
}
