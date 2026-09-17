// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "ScsiDisk.h"
#include "FixtureStore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

uint32_t scsiAppleImageBlockSize(const uint8_t* prefix, size_t n) {
    if (!prefix || n < 1026) return 0;
    if (prefix[0] == 'E' && prefix[1] == 'R') {
        const uint32_t sb = (uint32_t(prefix[2]) << 8) | prefix[3];
        return (sb == 512 || sb == 2048) ? sb : 0;
    }
    if (prefix[1024] == 'B' && prefix[1025] == 'D') return 512;
    return 0;
}

uint32_t scsiAppleImageBlockSize(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    uint8_t b[1026];
    in.read(reinterpret_cast<char*>(b), 1026);
    return scsiAppleImageBlockSize(b, size_t(in.gcount()));
}

namespace {
constexpr int kBlockSize = 512;
// SCSI status bytes
constexpr uint8_t kGood = 0x00, kCheck = 0x02;
// Sense keys
constexpr uint8_t kNoSense = 0x00, kNotReady = 0x02, kIllegalRequest = 0x05,
                  kDataProtect = 0x07, kUnitAttention = 0x06,
                  kMiscompare = 0x0E;
// Additional sense codes used outside their one call site.
constexpr uint8_t kAscLunNotSupported = 0x25;    // SCSI-2 appendix D
// Fixed-format sense data is 18 bytes: an 8-byte head plus the 10 the
// "additional sense length" field at byte 7 announces (SCSI-2 § 8.2.14).
constexpr size_t kFixedSenseLen = 18;

// wrap_hfs.py layout: 96-block head (DDM + map + Apple_Driver43) then HFS.
constexpr uint32_t kFacadePrefixBlocks = 96;
constexpr uint32_t kFacadePrefixBytes = kFacadePrefixBlocks * kBlockSize;

static void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}
static void be16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8); p[1] = uint8_t(v);
}

static bool loadTemplateHead(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(kFacadePrefixBytes);
    in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    if (in.gcount() != std::streamsize(out.size())) return false;
    return out.size() >= 2 && out[0] == 'E' && out[1] == 'R';
}

// Same candidate order as tools/wrap_hfs.py (+ env + beside the .dsk).
static bool findDdmTemplate(const std::string& imagePath,
                            const std::string& configured,
                            std::vector<uint8_t>& head) {
    std::vector<std::string> cands;
    if (!configured.empty()) cands.emplace_back(configured);

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path img = fs::absolute(imagePath, ec);
    if (!ec && img.has_parent_path()) {
        fs::path dir = img.parent_path();
        cands.push_back((dir / "HD20SC.vhd").string());
        cands.push_back((dir / "boot.vhd").string());
        cands.push_back((dir / "scsi_ddm_template.vhd").string());
    }
    for (const char* rel : {
             "hdv/HD20SC.vhd", "../hdv/HD20SC.vhd",
             "hdv/boot.vhd", "../hdv/boot.vhd",
             "hdv/scsi_ddm_template.vhd", "../hdv/scsi_ddm_template.vhd" })
        cands.emplace_back(rel);

    for (const auto& p : cands) {
        if (loadTemplateHead(p, head)) {
            std::fprintf(stderr, "SCSI: flat HFS façade — DDM template from %s\n",
                         p.c_str());
            return true;
        }
    }
    return false;
}
} // namespace

// Bare-HFS detection: bootable volumes carry 'LK' boot blocks at 0; a
// data-only volume (tools/dir2hfs.py bake) has ZERO boot blocks and is
// recognized by the MDB signature 'BD' at $400. Data-only volumes must NOT
// be given fake 'LK' blocks: StartBoot scans SCSI 6→0, sees the higher ID
// first, believes the LK, jumps into zeroed boot blocks and lands in the
// ROM serial-debugger stub ($408BA0EA on FF7439EE) before video is up.
static bool looksBareHfs(const std::vector<uint8_t>& img) {
    if (img.size() >= 2 && img[0] == 'L' && img[1] == 'K') return true;
    return img.size() >= 0x402 && img[0x400] == 'B' && img[0x401] == 'D'
        && !(img[0] == 'E' && img[1] == 'R');       // already partitioned
}

// A Toast/DDM dump (ER, 512-byte blocks, no driver) is not a SCSI disk the
// ROM will bind: sbDrvrCount is 0, the map is a CD layout, and Mac OS
// mounts nothing — Theme Park on the LC II and the Quadra 700. Keep the
// Apple_HFS partition; `open` then applies the same façade a flat `.dsk`
// gets. Write-back is refused: the in-memory layout no longer matches the
// file. CHANGELOG 2026-08-15, 2026-09-13.
static bool unwrapDriverless512Dump(std::vector<uint8_t>& img) {
    if (img.size() < 0x600) return false;
    if (img[0] != 'E' || img[1] != 'R') return false;
    const uint32_t sb = (uint32_t(img[2]) << 8) | img[3];
    if (sb != 512) return false;
    if (((img[0x10] << 8) | img[0x11]) != 0) return false;
    uint32_t start = 0, len = 0;
    for (int i = 1; i < 64; i++) {
        const size_t b = size_t(i) * 512;
        if (b + 80 > img.size() || img[b] != 'P' || img[b + 1] != 'M') break;
        char typ[33] = {};
        std::memcpy(typ, img.data() + b + 48, 32);
        if (std::strcmp(typ, "Apple_HFS") != 0) continue;
        start = (uint32_t(img[b + 8]) << 24) | (uint32_t(img[b + 9]) << 16)
              | (uint32_t(img[b + 10]) << 8) | img[b + 11];
        len   = (uint32_t(img[b + 12]) << 24) | (uint32_t(img[b + 13]) << 16)
              | (uint32_t(img[b + 14]) << 8) | img[b + 15];
        break;
    }
    if (!start || !len) return false;
    const uint64_t off = uint64_t(start) * 512;
    uint64_t bytes = uint64_t(len) * 512;
    if (off >= img.size()) return false;
    if (off + bytes > img.size()) bytes = img.size() - off;
    if (bytes < 0x402) return false;
    if (img[off + 0x400] != 'B' || img[off + 0x401] != 'D') return false;
    std::vector<uint8_t> hfs(img.begin() + off, img.begin() + off + bytes);
    img.swap(hfs);
    return true;
}

bool ScsiDisk::applyFlatHfsFacade(const std::string& imagePath) {
    if (!looksBareHfs(image_)) return false;
    if (image_.size() % kBlockSize) {
        std::fprintf(stderr, "SCSI: %s: bare HFS not a multiple of 512 — "
                     "no façade\n", imagePath.c_str());
        return false;
    }

    std::vector<uint8_t> head;
    if (!findDdmTemplate(imagePath, ddmTemplate_, head)) {
        std::fprintf(stderr, "SCSI: %s looks like flat HFS ('LK') but no DDM "
                     "template found (set POM68K_SCSI_DDM_TEMPLATE or place "
                     "HD20SC.vhd / boot.vhd in hdv/) — leaving raw\n",
                     imagePath.c_str());
        return false;
    }

    const uint32_t hfsBlocks = uint32_t(image_.size() / kBlockSize);
    const uint32_t total = kFacadePrefixBlocks + hfsBlocks;

    // DDM sbBlkCount
    be32(head.data() + 4, total);

    // Ensure a ddType $6A driver entry (LC II StartBoot @ $A07264) alongside
    // the template's $0001 entry (Plus / Mac II wantType).
    int count = (head[0x10] << 8) | head[0x11];
    bool has6A = false;
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= kBlockSize; i++) {
        int e = 0x12 + i * 8;
        if (((head[e + 6] << 8) | head[e + 7]) == 0x6A) has6A = true;
    }
    if (!has6A && count >= 1 && 0x12 + count * 8 + 8 <= kBlockSize) {
        int src = 0x12, dst = 0x12 + count * 8;
        std::memcpy(head.data() + dst, head.data() + src, 8);
        head[dst + 6] = 0x00;
        head[dst + 7] = 0x6A;
        be16(head.data() + 0x10, uint16_t(count + 1));
    }

    // Patch Apple_HFS partition to cover the appended volume at LBA 96.
    for (int i = 1; i < 64; i++) {
        size_t b = size_t(i) * kBlockSize;
        if (b + 88 > head.size() || head[b] != 'P' || head[b + 1] != 'M') break;
        char typ[33] = {};
        std::memcpy(typ, head.data() + b + 48, 32);
        if (std::strcmp(typ, "Apple_HFS") == 0) {
            be32(head.data() + b + 8, kFacadePrefixBlocks);  // pmPyPartStart
            be32(head.data() + b + 12, hfsBlocks);           // pmPartBlkCnt
            be32(head.data() + b + 84, hfsBlocks);           // pmDataCnt
        }
    }

    std::vector<uint8_t> wrapped;
    wrapped.reserve(head.size() + image_.size());
    wrapped.insert(wrapped.end(), head.begin(), head.end());
    wrapped.insert(wrapped.end(), image_.begin(), image_.end());
    image_.swap(wrapped);
    blocks_ = total;
    hfsPrefixBlocks_ = kFacadePrefixBlocks;
    std::fprintf(stderr, "SCSI: flat HFS → façade (%u + %u = %u blocks)\n",
                 kFacadePrefixBlocks, hfsBlocks, total);
    return true;
}

bool ScsiDisk::open(const std::string& path, bool writeBack) {
    const std::string backingPath =
        pom68k::routeWritableOpen(path, "SCSI", writeBack);
    std::ifstream in(backingPath, std::ios::binary);
    if (!in) return false;
    // Block read, not istreambuf_iterator: byte-wise iteration measured
    // 1.6 % of a whole bench run in callgrind (2026-07-31) — ~10 G host
    // instructions to load a disk image before the guest ran at all.
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    in.seekg(0, std::ios::beg);
    image_.resize(n > 0 ? size_t(n) : 0);
    if (!image_.empty() &&
        !in.read(reinterpret_cast<char*>(image_.data()), image_.size()))
        return false;
    hfsPrefixBlocks_ = 0;
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    if (unwrapDriverless512Dump(image_)) {
        writeBack = false;
        std::fprintf(stderr, "SCSI: %s: driverless 512 dump → HFS, read-only\n",
                     backingPath.c_str());
    }
    blocks_ = uint32_t(image_.size() / kBlockSize);

    if (blocks_ && looksBareHfs(image_))
        applyFlatHfsFacade(backingPath);

    // The save-state write log is relative to the image as just loaded.
    resetWriteLog();

    if (blocks_ && writeBack) {
        file_.open(backingPath, std::ios::in | std::ios::out | std::ios::binary);
        writeBack_ = file_.is_open();
        if (!writeBack_)
            std::fprintf(stderr, "SCSI: %s not writable — session writes "
                         "will be lost on exit\n", backingPath.c_str());
        else if (hfsPrefixBlocks_)
            std::fprintf(stderr, "SCSI: write-back maps LBA≥%u onto flat HFS file\n",
                         hfsPrefixBlocks_);
    }
    return blocks_ > 0;
}

// ── CD-ROM personality ────────────────────────────────────────────────
// Raw MODE1 images only (.iso/.cdr/.toast): 2048-byte user data per
// sector, which is what every Mac CD carries and what READ(10) returns.
// 2352-byte raw-with-subchannel rips are rejected rather than silently
// mis-read — a wrong block size looks like a corrupt volume to the guest,
// and that is exactly the kind of silent failure this project keeps
// finding the hard way.
// MODE1/2352 sectors carry 12 sync bytes, a 4-byte header, 2048 bytes of
// user data, then EDC/ECC. Drives hand the initiator the 2048 only, so a
// raw rip has to be de-framed rather than served as-is.
static bool deframeMode1_2352(std::vector<uint8_t>& img) {
    static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (img.size() < 2352 || img.size() % 2352) return false;
    if (std::memcmp(img.data(), sync, sizeof sync) != 0) return false;
    const size_t n = img.size() / 2352;
    std::vector<uint8_t> out(n * 2048);
    for (size_t i = 0; i < n; i++)
        std::memcpy(&out[i * 2048], &img[i * 2352 + 16], 2048);
    img.swap(out);
    return true;
}

// A .cue sheet, read WHOLE: the FILE it names (resolved beside the sheet),
// the first MODE1 track's framing, and every track with its INDEX 01 start,
// so a mixed-mode disc can answer READ TOC for its audio tracks. Before
// 2026-09-17 this stopped at the first data track and the rest of the disc
// did not exist as far as the guest could tell.
struct CueTrack { unsigned number; bool audio; uint32_t startLba; };

// A cue sheet's INDEX times are measured from the start of the FILE it
// names, NOT as absolute disc addresses — so MM:SS:FF converts straight to
// a sector offset into the .bin, with no two-second lead-in to take off.
// (Cue Sheet File Format Specification, § INDEX; Hydrogenaudio's cue sheet
// page says the same.) POM68K used to subtract 150 here and compensate by
// ADDING 150 when it wrote sheets of its own, which round-tripped its own
// discs perfectly and started every track of a real rip two seconds early
// — corrected 2026-09-17.
static uint32_t cueMsfToLba(const std::string& s) {
    unsigned m = 0, ss = 0, f = 0;
    if (std::sscanf(s.c_str(), "%u:%u:%u", &m, &ss, &f) != 3) return 0;
    return (m * 60 + ss) * 75 + f;
}

static std::string cueDataFile(const std::string& cuePath, bool& mode1_2352,
                               std::vector<CueTrack>* tracks) {
    std::ifstream in(cuePath);
    if (!in) return {};
    std::string line, file;
    mode1_2352 = false;
    bool firstData = true, pendingAudio = false;
    unsigned pendingNumber = 0;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        std::string t = line.substr(a);
        if (t.rfind("FILE", 0) == 0) {
            size_t q1 = t.find('"');
            size_t q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                file = t.substr(q1 + 1, q2 - q1 - 1);
        } else if (t.rfind("TRACK", 0) == 0) {
            if (firstData && t.find("MODE1") != std::string::npos) {
                mode1_2352 = t.find("MODE1/2352") != std::string::npos;
                firstData = false;
            }
            pendingNumber = 0;
            std::sscanf(t.c_str(), "TRACK %u", &pendingNumber);
            pendingAudio = t.find("AUDIO") != std::string::npos;
            if (!tracks) { if (!firstData) break; }   // legacy: data track only
        } else if (tracks && pendingNumber &&
                   t.rfind("INDEX 01", 0) == 0) {
            const size_t sp = t.find_last_of(' ');
            if (sp != std::string::npos)
                tracks->push_back({ pendingNumber, pendingAudio,
                                    cueMsfToLba(t.substr(sp + 1)) });
            pendingNumber = 0;
        }
    }
    if (file.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::path(cuePath).parent_path() / file;
    if (std::ifstream(p.string(), std::ios::binary)) return p.string();
    return file;
}

bool ScsiDisk::openCdrom(const std::string& path) {
    // Loading media into a drive that already existed on the bus is a
    // medium change: the guest's driver must see UNIT ATTENTION ($28,
    // not-ready-to-ready) or it will never mount the new disc. The first
    // attach (boot-time bus population) owes nothing.
    const bool mediumChange = attached_ && kind_ != Kind::Disk;
    kind_ = Kind::Cdrom;                             // re-decided below
    attached_ = true;
    unitAttention_ = false;
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    hfsPrefixBlocks_ = 0;
    image_.clear();
    blocks_ = 0;
    tracks_.clear();
    discLba_ = 0;
    rawPath_.clear();
    audioOnly_ = false;
    dataStartLba_ = 0;
    if (rawFile_.is_open()) rawFile_.close();
    if (cdAudio_ && audio_ != Audio::Stopped) cdAudio_->cdAudioStopped();
    audio_ = Audio::Stopped;
    audioLba_ = audioEnd_ = 0;

    std::string data = path;
    auto endsWith = [](const std::string& s, const char* e) {
        size_t n = std::strlen(e);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; i++)
            if (std::tolower(s[s.size() - n + i]) != e[i]) return false;
        return true;
    };
    if (endsWith(path, ".cue")) {
        bool raw = false;
        std::vector<CueTrack> cue;
        data = cueDataFile(path, raw, &cue);
        for (const CueTrack& t : cue)
            tracks_.push_back({ uint8_t(t.number), t.audio, t.startLba });
        if (data.empty()) {
            std::fprintf(stderr, "CD-ROM: %s names no usable FILE/TRACK\n",
                         path.c_str());
            return false;
        }
        std::fprintf(stderr, "CD-ROM: %s → %s\n", path.c_str(), data.c_str());
    }

    std::ifstream in(data, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    in.seekg(0, std::ios::beg);
    image_.resize(n > 0 ? size_t(n) : 0);
    if (!image_.empty() &&
        !in.read(reinterpret_cast<char*>(image_.data()), image_.size()))
        return false;

    // A mixed-mode sheet: the whole .bin is raw 2352, but only track 1 is
    // MODE1 — de-framing the audio sectors would turn music into "user
    // data". Cut the file down to the data track's extent first, and
    // remember the whole disc's length for the TOC's lead-out.
    if (!tracks_.empty() && image_.size() % 2352 == 0) {
        discLba_ = uint32_t(image_.size() / 2352);
        rawPath_ = data;                     // the audio tracks stay on disk
        // The data track is not always the first one: a CD Extra disc puts
        // its audio in session 1 and its data track thousands of sectors in.
        // Find it rather than assuming, and remember where it starts —
        // READ(10) addresses are absolute disc LBAs, not offsets into the
        // track (see dataStartLba_).
        const CdTrack* dataTrack = nullptr;
        for (const CdTrack& t : tracks_)
            if (!t.audio) { dataTrack = &t; break; }
        if (dataTrack) {
            uint32_t end = discLba_;             // to EOF if it is the last
            for (const CdTrack& t : tracks_)
                if (t.startLba > dataTrack->startLba && t.startLba < end)
                    end = t.startLba;            // the NEXT start, sorted or not
            dataStartLba_ = dataTrack->startLba;
            const size_t from = size_t(dataStartLba_) * 2352;
            const size_t to = std::min(size_t(end) * 2352, image_.size());
            if (to > from) image_.assign(image_.begin() + from, image_.begin() + to);
        }
    }

    // ── An audio CD: no user data anywhere on it ────────────────────────
    // The most ordinary CD-DA case, and the one the AppleCD Audio Player
    // exists for. There is no data track to de-frame and nothing to mount
    // as a volume; the disc is its TOC and its tracks. Judged from the
    // sheet, not from the bytes — a `.bin` of audio sectors looks like
    // arbitrary data by construction.
    audioOnly_ = !tracks_.empty();
    for (const CdTrack& t : tracks_) if (!t.audio) audioOnly_ = false;
    if (audioOnly_) {
        kind_ = Kind::Cdrom;
        image_.clear();
        blocks_ = 0;
        hfsPrefixBlocks_ = 0;
        unitAttention_ = mediumChange;
        return true;
    }

    // A 2352-multiple that starts with the MODE1 sync pattern is a raw rip:
    // de-frame it. Anything else must already be 2048-byte user data —
    // guessing would mount a mis-framed volume, which looks to the guest
    // exactly like a corrupt disc.
    if (image_.size() % 2048) {
        if (!deframeMode1_2352(image_)) {
            std::fprintf(stderr, "CD-ROM: %s is %zu bytes — neither 2048-byte "
                         "user data nor a MODE1/2352 raw rip\n",
                         data.c_str(), image_.size());
            image_.clear();
            return false;
        }
    } else if (image_.size() % 2352 == 0) {
        deframeMode1_2352(image_);           // 2352-multiple that is ALSO a
                                             // 2048-multiple: only de-frame
                                             // when the sync says so
    }
    // ── Let the medium say how big its blocks are ───────────────────────
    // See the Kind note in ScsiDisk.h: a dump taken at 512 declares it, and
    // serving it as 2048 mounts nothing. `ER` at 0 is the Apple driver
    // descriptor and +2 is sbBlkSize; no descriptor at all with an HFS `BD`
    // at 1024 is the same thing without a map. Anything else — ISO 9660, a
    // de-framed raw rip — is a real 2048-byte disc.
    uint32_t bs = scsiAppleImageBlockSize(image_.data(), image_.size());
    if (!bs) bs = 2048;
    kind_ = bs == 2048 ? Kind::Cdrom : Kind::Removable;
    if (bs != 2048)
        std::fprintf(stderr, "CD-ROM: %s declares %u-byte blocks — attaching "
                     "it as a removable disk, not a CD\n", data.c_str(), bs);
    blocks_ = uint32_t(image_.size() / bs);
    hfsPrefixBlocks_ = 0;
    // A bare HFS volume with no partition map needs the same façade a
    // `.dsk` gets through open(): a DDM plus an Apple_Driver43 borrowed from
    // a template, or the ROM finds no driver to load and mounts nothing.
    // That is why `Apeiron_1_0_3.toast` — 512-byte, no map — appeared on the
    // desktop when it was attached as a SCSI disk and never through the CD
    // bay (measured 2026-08-15).
    if (kind_ == Kind::Removable && blocks_ && looksBareHfs(image_))
        applyFlatHfsFacade(data);
    unitAttention_ = mediumChange && blocks_ > 0;
    return blocks_ > 0;
}

void ScsiDisk::attachCdromEmpty() {
    kind_ = Kind::Cdrom;
    attached_ = true;
    audioOnly_ = false;
    unitAttention_ = false;
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    hfsPrefixBlocks_ = 0;
    image_.clear();
    blocks_ = 0;
    setSense(kNotReady, 0x3A);                   // MEDIUM NOT PRESENT
}

// One raw sector straight from the medium. The audio tracks were cut out
// of image_ at open() (de-framing them would turn music into user data),
// so a play reads them back from the file the .cue named. 75 reads a
// second of 2352 bytes each: the host page cache absorbs it.
bool ScsiDisk::readRawSector(uint32_t lba, uint8_t* out) {
    if (rawPath_.empty()) return false;
    if (!rawFile_.is_open()) {
        rawFile_.open(rawPath_, std::ios::binary);
        if (!rawFile_.is_open()) { rawPath_.clear(); return false; }
    }
    rawFile_.clear();
    rawFile_.seekg(std::streamoff(lba) * 2352);
    if (!rawFile_) return false;
    rawFile_.read(reinterpret_cast<char*>(out), 2352);
    return rawFile_.gcount() == 2352;
}

// CD-DA runs at 75 sectors a second, so one sector is 13 333 us. Machine
// time, not host time: a paused emulator must not let the disc run on.
void ScsiDisk::advanceAudio(uint64_t micros) {
    if (audio_ != Audio::Playing) return;
    audioFrac_ += micros;
    const uint64_t perSector = 1000000ull / 75;
    while (audioFrac_ >= perSector && audio_ == Audio::Playing) {
        audioFrac_ -= perSector;
        // The sector the head is ON is the one that sounds, so hand it over
        // BEFORE stepping off it. A sink is optional: a headless gate plays
        // the transport with nothing listening.
        if (cdAudio_) {
            uint8_t raw[2352];
            if (readRawSector(audioLba_, raw)) cdAudio_->cdAudioSector(raw);
        }
        if (++audioLba_ >= audioEnd_) { audioLba_ = audioEnd_; audio_ = Audio::Completed; }
    }
}

void ScsiDisk::eject() {
    // Back to a plain empty tray: the next medium re-decides the kind (a
    // 2048 disc and a 512 dump can follow each other in the same drive).
    if (kind_ == Kind::Removable) kind_ = Kind::Cdrom;
    image_.clear();
    blocks_ = 0;
    tracks_.clear();
    discLba_ = 0;
    // An audio disc has no data blocks, so without these an ejected one
    // would still answer "medium present" through discLoaded().
    rawPath_.clear();
    audioOnly_ = false;
    dataStartLba_ = 0;
    if (rawFile_.is_open()) rawFile_.close();
    if (cdAudio_ && audio_ != Audio::Stopped) cdAudio_->cdAudioStopped();
    audio_ = Audio::Stopped;
    audioLba_ = audioEnd_ = 0;
    hfsPrefixBlocks_ = 0;
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    // A CD drive with no disc is still a target; a disk image that is
    // closed is simply gone.
    unitAttention_ = false;
    setSense(kNotReady, 0x3A);                   // MEDIUM NOT PRESENT
}

void ScsiDisk::close() {
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    kind_ = Kind::Disk;
    attached_ = false;
    unitAttention_ = false;
    image_.clear();
    image_.shrink_to_fit();              // hundreds of MB: give them back now
    blocks_ = 0;
    hfsPrefixBlocks_ = 0;
    identifyLun_ = kNoIdentify;
    resetWriteLog();
    setSense(0, 0);
}

void ScsiDisk::read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out) {
    readCommands++;
    readBlocks += count;
    if (kind_ == Kind::Cdrom) {
        out.assign(size_t(count) * 2048, 0);
        // Absolute disc LBA → offset into the data track's image.
        if (lba < dataStartLba_) return;         // before the data track
        uint64_t off = uint64_t(lba - dataStartLba_) * 2048;
        if (off < image_.size()) {
            uint64_t n = uint64_t(count) * 2048;
            uint64_t avail = image_.size() - off;
            std::memcpy(out.data(), image_.data() + off,
                        size_t(n < avail ? n : avail));
        }
        return;
    }
    if (sound_) {
        sound_->motor(true, true);
        sound_->step(int(lba >> 11), FloppySoundSink::kNoStamp);
    }
    out.clear();
    out.resize(size_t(count) * kBlockSize, 0);
    uint64_t off = uint64_t(lba) * kBlockSize;
    uint64_t n = uint64_t(count) * kBlockSize;
    if (off < image_.size()) {
        uint64_t avail = image_.size() - off;
        std::memcpy(out.data(), image_.data() + off, size_t(n < avail ? n : avail));
    }
}

// ── Save-state write log (design note in ScsiDisk.h § Save states) ──────
// Copy-on-first-write: the first time the guest writes a block, its
// pre-write bytes are appended to `pristine_`. That is what lets a restore
// put the image back exactly as it was at snapshot time, including blocks
// the guest modified AFTER the snapshot was taken — those are dirty now but
// absent from the snapshot, so reverting is the only way to reach the
// recorded state.
void ScsiDisk::resetWriteLog() {
    dirtyBits_.assign((std::size_t(blocks_) + 63) / 64, 0);
    dirtyList_.clear();
    pristine_.clear();
}

void ScsiDisk::markDirty(uint32_t lba, uint32_t count) {
    const uint32_t bs = blockSize();
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t blk = lba + i;
        const std::size_t word = blk >> 6;
        if (blk >= blocks_ || word >= dirtyBits_.size()) break;
        const uint64_t bit = 1ull << (blk & 63);
        if (dirtyBits_[word] & bit) continue;              // already logged
        dirtyBits_[word] |= bit;
        dirtyList_.push_back(blk);
        const uint64_t off = uint64_t(blk) * bs;
        // Keep one slot per list entry unconditionally, so slot i always
        // belongs to dirtyList_[i] even for a block past the image end.
        pristine_.resize(pristine_.size() + bs, 0);
        if (off + bs <= image_.size())
            std::memcpy(pristine_.data() + pristine_.size() - bs,
                        image_.data() + off, bs);
    }
}

void ScsiDisk::revertToPristine() {
    const uint32_t bs = blockSize();
    for (std::size_t i = 0; i < dirtyList_.size(); i++) {
        const uint64_t off = uint64_t(dirtyList_[i]) * bs;
        if (off + bs <= image_.size() && (i + 1) * bs <= pristine_.size())
            std::memcpy(image_.data() + off, pristine_.data() + i * bs, bs);
    }
    resetWriteLog();
}

void ScsiDisk::applySnapshotBlock(uint32_t blk, const uint8_t* data) {
    const uint32_t bs = blockSize();
    markDirty(blk, 1);                    // logs the pristine bytes first
    const uint64_t off = uint64_t(blk) * bs;
    if (off + bs <= image_.size())
        std::memcpy(image_.data() + off, data, bs);
}

// Writes land in the in-memory image; with write-back each one is also
// written through to the backing file immediately, so nothing is lost
// even if the process dies (no exit-time flush to miss). Flat-HFS façade:
// only LBAs past the synthetic prefix hit the original file.
void ScsiDisk::write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in) {
    if (sound_) {
        sound_->motor(true, true);
        sound_->step(int(lba >> 11), FloppySoundSink::kNoStamp);
    }
    writeCommands++;
    writeBlocks += long(count);
    store(lba, count, in.data(), in.size());
}

void ScsiDisk::hostWrite(uint32_t lba, const uint8_t* data, uint32_t count) {
    store(lba, count, data, size_t(count) * kBlockSize);
}

void ScsiDisk::store(uint32_t lba, uint32_t count, const uint8_t* in, size_t inSize) {
    uint64_t off = uint64_t(lba) * kBlockSize;
    uint64_t n = uint64_t(count) * kBlockSize;
    if (off >= image_.size()) return;
    uint64_t avail = image_.size() - off;
    uint64_t w = n < avail ? n : avail;
    if (w > inSize) w = inSize;
    // Log the pre-write bytes BEFORE the memcpy — that ordering is the
    // whole point of the copy-on-first-write log (ScsiDisk.h § Save states).
    markDirty(lba, uint32_t((w + kBlockSize - 1) / kBlockSize));
    std::memcpy(image_.data() + off, in, size_t(w));
    if (!writeBack_ || !w) return;

    // Façade prefix is synthetic — never write it into the flat .dsk.
    if (hfsPrefixBlocks_ && lba < hfsPrefixBlocks_) {
        uint32_t skip = hfsPrefixBlocks_ - lba;
        if (skip >= count) return;
        uint64_t skipBytes = uint64_t(skip) * kBlockSize;
        if (skipBytes >= w) return;
        const char* src = reinterpret_cast<const char*>(in) + skipBytes;
        uint64_t fileOff = 0;
        uint64_t fileW = w - skipBytes;
        file_.seekp(std::streamoff(fileOff));
        file_.write(src, std::streamoff(fileW));
    } else {
        uint64_t fileOff = hfsPrefixBlocks_
            ? uint64_t(lba - hfsPrefixBlocks_) * kBlockSize
            : off;
        file_.seekp(std::streamoff(fileOff));
        file_.write(reinterpret_cast<const char*>(in), std::streamoff(w));
    }
    file_.flush();
    if (!file_) {
        std::fprintf(stderr, "SCSI: write-back failed at block %u — "
                     "disabling (session writes stay in memory)\n", lba);
        writeBack_ = false;
    }
}

void ScsiDisk::setSense(uint8_t key, uint8_t asc, uint8_t ascq) {
    senseKey_ = key; senseAsc_ = asc; senseAscq_ = ascq;
}

// ── Which logical unit is this CDB for? ─────────────────────────────────
// SCSI-2 § 5.6.7 / § 7.5.3: the IDENTIFY message sent after selection owns
// the nexus's LUN; the CDB's byte-1 bits 7-5 are the SCSI-1 field, and are
// consulted only when the initiator sent no IDENTIFY (the Ncr5380 path).
// MAME resolves the same precedence in `nscsi_full_device::get_lun(default)`.
uint8_t ScsiDisk::effectiveLun(const uint8_t* cdb, int cdbLen) const {
    if (identifyLun_ != kNoIdentify) return identifyLun_ & 0x07;
    return cdbLen > 1 ? uint8_t((cdb[1] >> 5) & 0x07) : 0;
}

// ── MODE SENSE pages, hard-disk personality ─────────────────────────────
// MAME's nscsi_hd answers MODE SENSE with a bare header and no pages at all
// (hd.cpp:632-660). That is enough to boot a volume the host prepared and
// not enough for anything running INSIDE the guest to work on the drive:
// HD SC Setup, Drive Setup, Silverlining and FWB all read pages 1/3/4 before
// they will touch a disk, and Apple's tools additionally require the page
// $30 signature — the same gate the CD-ROM personality already answers.
//
// The layouts below are RaSCSI's, which is the closest thing to an oracle
// for "what a Mac SCSI drive must report": RASCSI-X68k
// src/raspberrypi/disk.cpp:1473-1616 (Disk::AddError / AddFormat / AddDrive /
// AddCache) and :2897-2918 (SCSIHD_APPLE::AddVendor), BSD-3-Clause — a
// license GPLv3 can incorporate, and a source of truth outside the usual
// MAME ranking precisely because MAME models none of this.
//
// `changeable` = MODE SENSE PC field 1: the same pages, but every field the
// target will let MODE SELECT alter is reported as all-ones and everything
// else as zero. Returns false for a page this target does not carry, which
// is a CHECK CONDITION / INVALID FIELD IN CDB and not an empty reply.
static bool appendDiskModePage(std::vector<uint8_t>& body, uint8_t page,
                               bool changeable, uint32_t blocks) {
    const size_t b = body.size();
    switch (page) {
        case 0x01:                                   // read-write error recovery
            body.resize(b + 12, 0);
            body[b] = 0x01; body[b + 1] = 0x0A;
            // Retry count 0, device-internal limit time: RaSCSI leaves the
            // whole page zero in both PC modes (disk.cpp:1473-1490), which
            // reads as "no AWRE/ARRE, retries are the drive's business".
            return true;

        case 0x02:                                   // disconnect-reconnect
            // Not in RaSCSI (the X68000 never asks); SCSI-2 §8.3.6. Some Mac
            // formatters do ask, and an all-zero page is the legal way for a
            // target to say it has no buffer-ratio or disconnect preference.
            body.resize(b + 16, 0);
            body[b] = 0x02; body[b + 1] = 0x0E;
            return true;

        case 0x03:                                   // format device
            body.resize(b + 24, 0);
            body[b] = 0x80 | 0x03;                   // PS: parameters saveable
            body[b + 1] = 0x16;
            if (changeable) {                        // only the sector size is
                body[b + 0x0C] = 0xFF;               // offered as changeable —
                body[b + 0x0D] = 0xFF;               // and even that is a
                return true;                         // polite fiction (RaSCSI
            }                                        // disk.cpp:1508-1512)
            body[b + 0x03] = 0x08;                   // tracks per zone
            body[b + 0x0A] = 0x00;                   // sectors per track = 25
            body[b + 0x0B] = 0x19;
            body[b + 0x0C] = uint8_t(kBlockSize >> 8);
            body[b + 0x0D] = uint8_t(kBlockSize);
            return true;

        case 0x04: {                                 // rigid drive geometry
            body.resize(b + 24, 0);
            body[b] = 0x04; body[b + 1] = 0x16;
            if (changeable) return true;             // geometry is not settable
            // The geometry is invented, and must be: an image has no platters.
            // RaSCSI's convention (8 heads × 25 sectors, cylinders derived —
            // disk.cpp:1553-1565) keeps cylinders inside the 24-bit field for
            // every image size a 68k Mac can address.
            const uint32_t cyl = (blocks >> 3) / 25;
            body[b + 0x02] = uint8_t(cyl >> 16);
            body[b + 0x03] = uint8_t(cyl >> 8);
            body[b + 0x04] = uint8_t(cyl);
            body[b + 0x05] = 0x08;                   // heads
            return true;
        }

        case 0x08:                                   // caching
            body.resize(b + 12, 0);
            body[b] = 0x08; body[b + 1] = 0x0A;
            // All zero = read cache on, no prefetch, write cache off. Write
            // cache off is the honest answer: ScsiDisk::write already writes
            // through to the backing file on every WRITE.
            return true;

        case 0x30:                                   // the Apple signature
            body.resize(b + 30, 0);
            body[b] = 0x30; body[b + 1] = 0x1C;
            // This string is the whole point. Apple's disk tools read it and
            // refuse the drive without it — the hard-disk twin of the CD-ROM
            // page $30 check in MAME cd.cpp:604-618.
            if (!changeable)
                std::memcpy(&body[b + 0x0A], "APPLE COMPUTER, INC.", 20);
            return true;

        default:
            return false;
    }
}

// ── MODE SENSE pages, CD-ROM personality ────────────────────────────────
// A different page set AND a different page $30 layout from the hard disk
// above: the CD signature is MAME's (cd.cpp:604-618, page length byte 0 and
// the string at offset 2), the disk's is RaSCSI's. They are not
// interchangeable — each is what its own driver reads.
static bool appendCdModePage(std::vector<uint8_t>& body, uint8_t page,
                             bool changeable) {
    switch (page) {
        case 0x0E: {                                 // CD audio control
            // Mac OS asks for this right after it accepts the disc
            // (1A 00 0E 00 1C) and stops there if it does not come back.
            static const uint8_t audio[16] = {
                0x8E, 0x0E, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0xFF, 0x02, 0xFF, 0x04, 0xFF, 0x08, 0xFF };
            const size_t b = body.size();
            body.insert(body.end(), audio, audio + sizeof audio);
            if (changeable) std::memset(&body[b] + 2, 0, sizeof(audio) - 2);
            return true;
        }
        case 0x30: {                                 // the Apple signature
            static const uint8_t magic[0x18] = {
                0x30, 0x00, 'A','P','P','L','E',' ','C','O','M','P','U',
                'T','E','R',',',' ','I','N','C',' ',' ',' ' };
            const size_t b = body.size();
            body.insert(body.end(), magic, magic + sizeof magic);
            if (changeable) std::memset(&body[b] + 2, 0, sizeof(magic) - 2);
            return true;
        }
        default:
            return false;
    }
}

// ── MODE SELECT, page $0E: the drive's own volume ───────────────────────
// The CD Audio Control page is not decoration: it is what the Sound control
// panel's CD slider and the AppleCD Audio Player's volume actually move,
// and a drive that reports the page in MODE SENSE (we do, above) but throws
// away what MODE SELECT sets is a drive whose volume control does nothing.
//
// Layout (SCSI-2 § 14.3.3.2, and the MODE SENSE twin above): four output
// ports, each a selection byte and a volume byte at page offsets 8/9,
// 10/11, 12/13, 14/15. The selection is a channel mask — bit 0 is audio
// channel 0 (left), bit 1 is channel 1 (right) — so a port contributes its
// volume to every channel it names, and a port selecting 0 is muted. Taking
// the strongest port per channel is what a mixer does with parallel feeds.
// No sink (a headless gate, a machine with no audio host) means nothing to
// tell, so the page is parsed only when someone is listening.
void ScsiDisk::modeSelect(const std::vector<uint8_t>& params, bool ten) {
    if (kind_ != Kind::Cdrom || !cdAudio_) return;
    const size_t header = ten ? 8u : 4u;
    if (params.size() < header + 2) return;
    const size_t bdLen = ten ? size_t((params[6] << 8) | params[7])
                             : size_t(params[3]);
    size_t at = header + bdLen;
    while (at + 1 < params.size()) {
        const uint8_t page = params[at] & 0x3F;
        const size_t len = size_t(params[at + 1]) + 2;
        if (page == 0x0E && at + 12 <= params.size()) {
            unsigned left = 0, right = 0;
            for (int port = 0; port < 4; port++) {
                const size_t sel = at + 8 + size_t(port) * 2;
                if (sel + 1 >= params.size()) break;
                const uint8_t channels = params[sel] & 0x0F;
                const uint8_t volume = params[sel + 1];
                if (channels & 0x01) left = std::max(left, unsigned(volume));
                if (channels & 0x02) right = std::max(right, unsigned(volume));
            }
            cdAudio_->cdAudioVolume(uint8_t(left), uint8_t(right));
        }
        at += len;                                    // len >= 2 always:
                                                      // the length byte counts
                                                      // from page offset 2
    }
}

// MODE SENSE(6) $1A and MODE SENSE(10) $5A differ only in header shape and
// allocation-length width, so they share one body — and both personalities
// share the header, differing only in their page set, their block size and
// the write-protect bit.
uint8_t ScsiDisk::modeSense(const uint8_t* cdb, bool ten,
                            std::vector<uint8_t>& out) {
    const bool cd = (kind_ == Kind::Cdrom);
    const uint8_t page = cdb[2] & 0x3F;
    // PC: 0 = current, 1 = changeable, 2 = default, 3 = saved. Current and
    // default are identical here (nothing is ever saved), and PC 3 is
    // reported as current rather than refused — RaSCSI does the same, and a
    // driver that asks for saved values wants a plausible answer, not an
    // error it has no path for.
    const bool changeable = ((cdb[2] >> 6) & 3) == 1;
    const bool dbd = (cdb[1] & 0x08) != 0;           // disable block descriptor
    const size_t alloc = ten ? size_t((cdb[7] << 8) | cdb[8])
                             : size_t(cdb[4] ? cdb[4] : 4);

    auto addPage = [&](std::vector<uint8_t>& body, uint8_t p) {
        return cd ? appendCdModePage(body, p, changeable)
                  : appendDiskModePage(body, p, changeable, blocks_);
    };
    static const uint8_t kDiskPages[] = { 0x01, 0x02, 0x03, 0x04, 0x08, 0x30 };
    static const uint8_t kCdPages[]   = { 0x0E, 0x30 };

    std::vector<uint8_t> body;
    bool valid = false;
    if (page == 0x3F) {                              // all pages
        if (cd) for (uint8_t p : kCdPages)   valid |= addPage(body, p);
        else    for (uint8_t p : kDiskPages) valid |= addPage(body, p);
    } else {
        valid = addPage(body, page);
    }
    if (!valid) {                                    // unsupported page
        setSense(kIllegalRequest, 0x24);
        return kCheck;
    }

    const size_t hdr = ten ? 8 : 4;
    out.assign(hdr, 0);
    out[ten ? 3 : 2] = cd ? 0x80 : 0x00;             // device-specific: a disc
                                                     // is write-protected
    if (!dbd) {
        // The block descriptor is how the driver learns the block size.
        // Omitting it (DBD clear) is what made Mac OS 8.1 probe the Apple
        // page and then give up: it asked 1A 00 30 00 24 and never spoke to
        // the target again.
        const uint32_t bs = blockSize();
        // Deliberate divergence, CD only and load-bearing: the count field
        // carries blocks-1 rather than the block count. It is what the Apple
        // CD driver was gated against and changing it is a mount regression,
        // not a spec cleanup.
        const uint32_t count = cd ? (blocks_ ? blocks_ - 1 : 0) : blocks_;
        if (ten) out[7] = 8; else out[3] = 8;        // block descriptor length
        out.push_back(0x00);                         // density code
        out.push_back(uint8_t(count >> 16));         // number of blocks
        out.push_back(uint8_t(count >> 8));
        out.push_back(uint8_t(count));
        out.push_back(0x00);
        out.push_back(uint8_t(bs >> 16));            // block length
        out.push_back(uint8_t(bs >> 8));
        out.push_back(uint8_t(bs));
    }
    out.insert(out.end(), body.begin(), body.end());
    // The length field describes what the target HAS, not what fits in the
    // initiator's buffer — a short allocation truncates the data and leaves
    // the count alone, which is how the driver learns to ask again bigger.
    if (ten) {
        const size_t n = out.size() - 2;
        out[0] = uint8_t(n >> 8); out[1] = uint8_t(n);
    } else {
        out[0] = uint8_t(out.size() - 1);
    }
    if (alloc && out.size() > alloc) out.resize(alloc);
    return kGood;
}

// ── How many DATA OUT bytes this CDB owes the target ────────────────────
// The controller handshakes exactly this many bytes out of the initiator,
// then calls command() with them. Getting it wrong does not corrupt data —
// it hangs the bus, because the initiator and the target disagree about
// whose turn it is.
int ScsiDisk::writeByteCount(const uint8_t* cdb, int cdbLen) const {
    if (!cdb || cdbLen < 6) return 0;
    const int bs = int(blockSize());
    switch (cdb[0]) {
        case 0x0A:                                    // WRITE(6)
            return (cdb[4] ? cdb[4] : 256) * bs;
        case 0x2A:                                    // WRITE(10)
        case 0x2E:                                    // WRITE AND VERIFY(10)
            return cdbLen >= 9 ? ((cdb[7] << 8) | cdb[8]) * bs : 0;
        case 0x2F:                                    // VERIFY(10), BytChk set:
            // the comparison data comes over the bus like a write.
            return (cdbLen >= 9 && (cdb[1] & 0x02))
                 ? ((cdb[7] << 8) | cdb[8]) * bs : 0;
        case 0x15:                                    // MODE SELECT(6)
            return cdb[4];                            // parameter list length
        case 0x55:                                    // MODE SELECT(10)
            return cdbLen >= 9 ? ((cdb[7] << 8) | cdb[8]) : 0;
        case 0x1D:                                    // SEND DIAGNOSTIC
            return cdbLen >= 5 ? ((cdb[3] << 8) | cdb[4]) : 0;
        case 0x04:                                    // FORMAT UNIT, FmtData:
            return (cdb[1] & 0x10) ? 4 : 0;           // header first, then
        case 0x07:                                    // REASSIGN BLOCKS:
            return 4;                                 // extendDataOut()
        default:
            return 0;
    }
}

// FORMAT UNIT / REASSIGN BLOCKS: the CDB carries no length — the 4-byte
// defect-list header does (bytes 2-3, SCSI-1 §9.2.6). A zero-length list
// completes right away. The bytes are accepted for the handshake and then
// discarded: an image has no defect list to keep.
std::size_t ScsiDisk::extendDataOut(const uint8_t* cdb, int cdbLen,
                                    const std::vector<uint8_t>& sofar,
                                    std::size_t expected) const {
    if (!cdb || cdbLen < 1) return expected;
    if (cdb[0] != 0x04 && cdb[0] != 0x07) return expected;
    if (expected != 4 || sofar.size() != 4) return expected;
    return expected + std::size_t((sofar[2] << 8) | sofar[3]);
}

uint8_t ScsiDisk::command(const uint8_t* cdb, int cdbLen,
                          std::vector<uint8_t>& dataOut,
                          const std::vector<uint8_t>& dataIn) {
    dataOut.clear();
    // Honour cdbLen: the parameter was discarded while the body indexes
    // cdb[4]/cdb[8] unconditionally, so a short (or empty) CDB read past the
    // buffer. Group code (cdb[0] bits 7-5) fixes the required length.
    if (!cdb || cdbLen <= 0) { setSense(kIllegalRequest, 0x20); return kCheck; }
    // ── The pending sense belongs to the PREVIOUS command ───────────────
    // SCSI-2 § 7.2.14: sense data is preserved for the initiator until it
    // is read by REQUEST SENSE **or until the target receives any other
    // command** on the same nexus. Holding it past a successful command is
    // what makes a driver's recovery path misdiagnose: it retries, the
    // retry succeeds, it asks for sense anyway (several Mac drivers do) and
    // is handed the failure it had already recovered from. Clearing here —
    // before the command runs, so the failures below still install theirs —
    // is the whole of the rule.
    if (cdb[0] != 0x03) setSense(kNoSense, 0x00);
    {
        static const int kGroupLen[8] = { 6, 10, 10, 6, 16, 12, 6, 6 };
        if (cdbLen < kGroupLen[(cdb[0] >> 5) & 7]) {
            setSense(kIllegalRequest, 0x20);         // INVALID COMMAND
            return kCheck;
        }
    }
    // ── POM68K_SCSI_TRACE: every CDB the guest issues, as it issues it ──
    // The counters answer "what landed on the medium"; the beyond-boot
    // campaign needs "what did the guest ASK for". A guest that has gone
    // silent, one that reads and never writes, and one that spins the
    // drive down (START/STOP UNIT) are three different failures and the
    // counters cannot tell them apart — see CHANGELOG 2026-08-13 (sixth),
    // where "zero writes in ten emulated minutes" was all the evidence
    // there was. Sequence number included so a gate's own printf can be
    // located in the stream.
    if (trace_) {
        static long seq = 0;
        const uint32_t lba = cdb[0] & 0x20
            ? (cdbLen > 5 ? uint32_t(cdb[2]) << 24 | uint32_t(cdb[3]) << 16 |
                            uint32_t(cdb[4]) << 8 | cdb[5] : 0)
            : (uint32_t(cdb[1] & 0x1F) << 16 | uint32_t(cdb[2]) << 8 | cdb[3]);
        const uint32_t cnt = cdb[0] & 0x20
            ? (cdbLen > 8 ? uint32_t(cdb[7]) << 8 | cdb[8] : 0) : cdb[4];
        std::fprintf(stderr, "[scsi %ld] op $%02X lba %u n %u\n",
                     seq++, cdb[0], lba, cnt);
    }
    if (kind_ == Kind::Cdrom && cdTrace_)
        std::fprintf(stderr, "[cd] cdb %02X %02X %02X %02X %02X %02X"
                     " %02X %02X %02X %02X\n", cdb[0],
                     cdbLen > 1 ? cdb[1] : 0, cdbLen > 2 ? cdb[2] : 0,
                     cdbLen > 3 ? cdb[3] : 0, cdbLen > 4 ? cdb[4] : 0,
                     cdbLen > 5 ? cdb[5] : 0, cdbLen > 6 ? cdb[6] : 0,
                     cdbLen > 7 ? cdb[7] : 0, cdbLen > 8 ? cdb[8] : 0,
                     cdbLen > 9 ? cdb[9] : 0);
    // ── LUN != 0: the target exists, the logical unit does not ─────────
    // A populated ID must still refuse the LUNs it does not implement, and
    // refuse them the way SCSI-2 spells out, or a driver's bus scan mounts
    // seven copies of the boot volume. INQUIRY answers GOOD with peripheral
    // qualifier 011b + device type $1F (§ 8.2.5.1 — MAME's nscsi_hd and
    // nscsi_cd write the same `$7f` into byte 0 of their INQUIRY reply when
    // `get_lun()` is non-zero); REQUEST SENSE answers GOOD carrying the
    // sense (§ 8.2.14 — a REQUEST SENSE is never refused for a bad LUN,
    // that is how the initiator finds out); everything else is CHECK
    // CONDITION / ILLEGAL REQUEST / LOGICAL UNIT NOT SUPPORTED (§ 7.5.3).
    if (effectiveLun(cdb, cdbLen) != 0) {
        if (cdb[0] == 0x12) {                        // INQUIRY
            const uint8_t alloc = cdb[4] ? cdb[4] : 36;
            dataOut.assign(alloc, 0);
            dataOut[0] = 0x7F;                       // qualifier 011b, type $1F
            if (dataOut.size() > 4) dataOut[4] = 0x20;   // additional length
            return kGood;
        }
        setSense(kIllegalRequest, kAscLunNotSupported);
        // REQUEST SENSE falls through to the shared path below, which
        // returns that sense with GOOD status and clears it.
        if (cdb[0] != 0x03) return kCheck;
    }
    // A medium change owes exactly one CHECK CONDITION / UNIT ATTENTION
    // before anything else executes (SCSI-2 §7.9); INQUIRY and REQUEST
    // SENSE are the two commands that must not be blocked by it.
    if (kind_ != Kind::Disk && unitAttention_
        && cdb[0] != 0x03 && cdb[0] != 0x12) {
        unitAttention_ = false;
        setSense(kUnitAttention, 0x28);          // NOT READY TO READY
        return kCheck;
    }
    // ── CD-ROM personality: the commands that differ, then fall through
    // to the shared ones (REQUEST SENSE, READ(6)/(10)). Everything a Mac
    // needs to see a disc; audio playback is deliberately absent (no
    // consumer yet — see ScsiDisk.h).
    if (kind_ == Kind::Cdrom) {
        switch (cdb[0]) {
            case 0x00:                               // TEST UNIT READY
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                return kGood;

            case 0x12: {                             // INQUIRY
                uint8_t alloc = cdb[4] ? cdb[4] : 36;
                dataOut.assign(alloc, 0);
                if (dataOut.size() > 0) dataOut[0] = 0x05;   // CD-ROM device
                if (dataOut.size() > 1) dataOut[1] = 0x80;   // removable
                if (dataOut.size() > 2) dataOut[2] = 0x02;   // SCSI-2
                if (dataOut.size() > 3) dataOut[3] = 0x02;   // response format
                if (dataOut.size() > 4) dataOut[4] = 0x20;   // additional length
                // The Apple CDSC identifies as a Sony mechanism with Apple
                // firmware (MAME cd.cpp:99). The driver's real gate is the
                // MODE SENSE page $30 below, not these strings.
                static const char id[] = "SONY    CD-ROM CDU-8003A1.0i";
                for (size_t i = 8; i < dataOut.size() && i - 8 < sizeof(id) - 1; i++)
                    dataOut[i] = uint8_t(id[i - 8]);
                for (size_t i = 8; i < dataOut.size() && i < 36; i++)
                    if (!dataOut[i]) dataOut[i] = ' ';       // space-padded
                return kGood;
            }

            case 0x25: {                             // READ CAPACITY (10)
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                if (audioOnly_) {
                    // No data track, so there is no last data block: real
                    // drives answer with the lead-out address. The driver
                    // uses it to size the disc, never to read it.
                    const uint32_t last = discLba_ ? discLba_ - 1 : 0;
                    dataOut = { uint8_t(last >> 24), uint8_t(last >> 16),
                                uint8_t(last >> 8), uint8_t(last),
                                0, 0, 0x08, 0x00 };
                    return kGood;
                }
                uint32_t last = blocks_ - 1;
                dataOut = { uint8_t(last >> 24), uint8_t(last >> 16),
                            uint8_t(last >> 8), uint8_t(last),
                            0, 0, 0x08, 0x00 };      // 2048-byte blocks
                return kGood;
            }

            // MODE SENSE(6)/(10) are shared with the hard disk below —
            // same header, CD page set (audio control + the Apple
            // signature), 2048-byte block descriptor.

            case 0x45:                               // PLAY AUDIO (10)
            case 0x47: {                             // PLAY AUDIO MSF
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                uint32_t start = 0, end = discLba_ ? discLba_ : blocks_;
                if (cdb[0] == 0x45) {
                    start = uint32_t(cdb[2]) << 24 | uint32_t(cdb[3]) << 16 |
                            uint32_t(cdb[4]) << 8 | cdb[5];
                    const uint32_t len = uint32_t(cdb[7]) << 8 | cdb[8];
                    if (len) end = start + len;
                } else {
                    auto msfLba = [](const uint8_t* p) -> uint32_t {
                        const uint32_t f = (uint32_t(p[0]) * 60 + p[1]) * 75 + p[2];
                        return f >= 150 ? f - 150 : 0;
                    };
                    start = msfLba(&cdb[3]);
                    end = msfLba(&cdb[6]);
                }
                // A play that names no audio is refused rather than faked:
                // ILLEGAL REQUEST / $64 "illegal mode for this track".
                bool onAudio = false;
                for (const CdTrack& t : tracks_)
                    if (t.audio && start >= t.startLba) onAudio = true;
                if (!onAudio) { setSense(kIllegalRequest, 0x64); return kCheck; }
                audioLba_ = start;
                audioEnd_ = std::max(end, start);
                audioFrac_ = 0;
                audio_ = audioLba_ < audioEnd_ ? Audio::Playing : Audio::Completed;
                return kGood;
            }

            case 0x4B: {                             // PAUSE / RESUME
                if (audio_ == Audio::Stopped) { setSense(kIllegalRequest, 0x64); return kCheck; }
                const bool resume = (cdb[8] & 1) != 0;
                if (resume) { if (audio_ == Audio::Paused) audio_ = Audio::Playing; }
                else        { if (audio_ == Audio::Playing) audio_ = Audio::Paused; }
                return kGood;
            }

            case 0x43: {                             // READ TOC
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                const bool msf = (cdb[1] & 0x02) != 0;
                // Format lives in cdb[2] low nibble; when zero, the SFF8020
                // legacy field in cdb[9] bits 7-6 (MAME cd.cpp:803). Mac OS
                // asks for BOTH the session-info and full-TOC forms.
                uint8_t format = cdb[2] & 0x0F;
                if (!format) format = (cdb[9] >> 6) & 3;
                auto addr = [&](uint32_t lba, uint8_t* p) {
                    if (!msf) { p[0] = uint8_t(lba >> 24); p[1] = uint8_t(lba >> 16);
                                p[2] = uint8_t(lba >> 8);  p[3] = uint8_t(lba); return; }
                    uint32_t f = lba + 150;          // MSF is offset by 2 s
                    p[0] = 0; p[1] = uint8_t(f / (60 * 75));
                    p[2] = uint8_t((f / 75) % 60); p[3] = uint8_t(f % 75);
                };
                uint16_t alloc = uint16_t(cdb[7] << 8 | cdb[8]);
                // A .cue names every track; a flat image is one data track
                // by construction. ADR 1 with control $4 = data, $0 = audio
                // (SFF8020 § 9.2), which is what tells AppleCD Audio Player
                // a track is playable at all.
                const bool sheet = !tracks_.empty();
                const uint8_t firstTrk = sheet ? tracks_.front().number : 1;
                const uint8_t lastTrk  = sheet ? tracks_.back().number : 1;
                const uint32_t leadOut = sheet && discLba_ ? discLba_ : blocks_;
                if (format == 0) {
                    const size_t count = sheet ? tracks_.size() : 1;
                    dataOut.assign(4 + 8 * (count + 1), 0);
                    const uint16_t len = uint16_t(dataOut.size() - 2);
                    dataOut[0] = uint8_t(len >> 8); dataOut[1] = uint8_t(len);
                    dataOut[2] = firstTrk; dataOut[3] = lastTrk;
                    size_t o = 4;
                    for (size_t i = 0; i < count; i++, o += 8) {
                        const bool audio = sheet && tracks_[i].audio;
                        dataOut[o + 1] = audio ? 0x10 : 0x14;
                        dataOut[o + 2] = sheet ? tracks_[i].number : 1;
                        addr(sheet ? tracks_[i].startLba : 0, &dataOut[o + 4]);
                    }
                    // The lead-out's control follows the LAST TRACK, it is
                    // not always data: a driver that classifies a disc by
                    // scanning the control nibbles sees one data entry on an
                    // all-audio disc and hands it to the File Manager, which
                    // finds no volume and offers to initialize the CD
                    // (observed on Mac OS 8.1, 2026-09-17).
                    dataOut[o + 1] = (sheet && tracks_.back().audio) ? 0x10 : 0x14;
                    dataOut[o + 2] = 0xAA;           // lead-out
                    addr(leadOut, &dataOut[o + 4]);
                } else if (format == 1) {
                    // Session info: one session, from its first track
                    // (MAME cd.cpp:866).
                    dataOut.assign(12, 0);
                    dataOut[0] = 0; dataOut[1] = 10; // length
                    dataOut[2] = 1; dataOut[3] = 1;  // first / last session
                    dataOut[5] = sheet && tracks_.front().audio ? 0x10 : 0x14;
                    dataOut[6] = firstTrk;           // first track of session
                    addr(sheet ? tracks_.front().startLba : 0, &dataOut[8]);
                } else if (format == 2) {
                    // Full TOC, the raw lead-in Q the drive read off the
                    // disc. MAME refuses this (cd.cpp:890-900) and POM68K
                    // used to copy that refusal — but Mac OS 8.1's Apple
                    // CD-ROM driver ASKS FOR IT on every disc it sees
                    // (43 02 00 00 00 00 01 00 30 80, observed 2026-09-17),
                    // and a refusal is not a neutral answer to a consumer
                    // that uses the reply to decide what kind of disc it is
                    // holding.
                    //
                    // Layout (SCSI-2 § 14.2.8 / SFF8020 § 9.2): a four-byte
                    // header, then 11-byte descriptors — session, ADR/CTRL,
                    // TNO, POINT, the descriptor's own MSF, a zero, and the
                    // POINT's MSF. POINT $A0 carries the first track number
                    // and the disc type, $A1 the last, $A2 the lead-out;
                    // then one descriptor per track.
                    const size_t count = sheet ? tracks_.size() : 1;
                    dataOut.assign(4 + 11 * (count + 3), 0);
                    const uint16_t len = uint16_t(dataOut.size() - 2);
                    dataOut[0] = uint8_t(len >> 8); dataOut[1] = uint8_t(len);
                    dataOut[2] = 1; dataOut[3] = 1;   // first / last session
                    size_t o = 4;
                    auto point = [&](uint8_t ctrl, uint8_t pt,
                                     uint8_t pmin, uint8_t psec, uint8_t pfrm) {
                        dataOut[o] = 1;               // session 1
                        dataOut[o + 1] = ctrl;
                        dataOut[o + 3] = pt;
                        dataOut[o + 8] = pmin;
                        dataOut[o + 9] = psec;
                        dataOut[o + 10] = pfrm;
                        o += 11;
                    };
                    const uint8_t firstCtrl = (sheet && tracks_.front().audio) ? 0x10 : 0x14;
                    const uint8_t lastCtrl  = (sheet && tracks_.back().audio) ? 0x10 : 0x14;
                    // $A0: PMIN = first track, PSEC = disc type (0 = CD-DA
                    // or CD-ROM, which is every disc POM68K can hold).
                    point(firstCtrl, 0xA0, firstTrk, 0x00, 0);
                    point(lastCtrl, 0xA1, lastTrk, 0, 0);
                    {
                        const uint32_t f = leadOut + 150;
                        point(lastCtrl, 0xA2, uint8_t(f / (60 * 75)),
                              uint8_t((f / 75) % 60), uint8_t(f % 75));
                    }
                    for (size_t i = 0; i < count; i++) {
                        const bool audio = sheet && tracks_[i].audio;
                        const uint32_t f =
                            (sheet ? tracks_[i].startLba : 0) + 150;
                        point(audio ? 0x10 : 0x14,
                              sheet ? tracks_[i].number : uint8_t(1),
                              uint8_t(f / (60 * 75)), uint8_t((f / 75) % 60),
                              uint8_t(f % 75));
                    }
                } else {
                    // PMA and ATIP describe a recordable disc's unfinished
                    // areas; POM68K holds finished images only, and MAME
                    // refuses them too (cd.cpp:890-900).
                    setSense(kIllegalRequest, 0x24);
                    return kCheck;
                }
                if (alloc && dataOut.size() > alloc) dataOut.resize(alloc);
                return kGood;
            }

            case 0x42: {                             // READ SUB-CHANNEL
                // The Apple CD extension sends this right after a hot
                // insert (42 02 40 01 — MSF, SubQ, current position); a
                // CHECK CONDITION here aborts the mount it had already
                // started. MAME cd.cpp:709-770. Since 2026-09-17 it also
                // carries the CD-DA transport: the audio status byte and a
                // position that MOVES while a play runs, which is what the
                // AppleCD Audio Player watches.
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                const bool msf  = (cdb[1] & 0x02) != 0;
                const bool subq = (cdb[2] & 0x40) != 0;
                const uint8_t param = cdb[3];
                uint16_t alloc = uint16_t(cdb[7] << 8 | cdb[8]);
                dataOut.assign(alloc ? alloc : 4, 0);
                if (dataOut.size() > 1)
                    dataOut[1] = audio_ == Audio::Playing   ? 0x11
                               : audio_ == Audio::Paused    ? 0x12
                               : audio_ == Audio::Completed ? 0x13
                                                            : 0x15;
                if (subq && param == 0x01 && dataOut.size() >= 16) {
                    dataOut[3] = 12;                 // sub-channel data length
                    dataOut[4] = 0x01;               // format: current position
                    // The track under the play head, and the addresses that
                    // go with it; a disc that never played answers exactly as
                    // it did before (track 1, position 0).
                    uint8_t trk = 1;
                    uint32_t base = 0;
                    bool onAudio = false;
                    for (const CdTrack& t : tracks_)
                        if (audioLba_ >= t.startLba) {
                            trk = t.number; base = t.startLba; onAudio = t.audio;
                        }
                    dataOut[5] = onAudio ? 0x10 : 0x14;   // Q: audio / data
                    dataOut[6] = trk;
                    dataOut[7] = audio_ == Audio::Stopped ? 0 : 1;   // index
                    auto put = [&](uint32_t lba, uint8_t* p) {
                        if (!msf) { p[0] = uint8_t(lba >> 24); p[1] = uint8_t(lba >> 16);
                                    p[2] = uint8_t(lba >> 8);  p[3] = uint8_t(lba); return; }
                        const uint32_t f = lba + 150;
                        p[0] = 0; p[1] = uint8_t(f / (60 * 75));
                        p[2] = uint8_t((f / 75) % 60); p[3] = uint8_t(f % 75);
                    };
                    put(audioLba_, &dataOut[8]);                 // absolute
                    put(audioLba_ - base, &dataOut[12]);         // track-relative
                }
                return kGood;
            }

            case 0x1B:                               // START/STOP UNIT
                // bit 1 of byte 4 = LoEj: a "stop + eject" empties the drive.
                if ((cdb[4] & 0x02) && !(cdb[4] & 0x01)) eject();
                return kGood;

            case 0x15:                               // MODE SELECT(6)
            case 0x55:                               // MODE SELECT(10)
                modeSelect(dataIn, cdb[0] == 0x55);
                return kGood;

            case 0x1E:                               // PREVENT/ALLOW REMOVAL
            case 0x2B:                               // SEEK(10)
                return kGood;

            case 0x0A: case 0x2A: case 0x2E:         // WRITE(6)/(10),
                                                     // WRITE AND VERIFY(10)
                setSense(kDataProtect, 0x27);        // WRITE PROTECTED
                return kCheck;

            case 0x07:                               // REASSIGN BLOCKS
            case 0x37:                               // READ DEFECT DATA(10)
                // Defect management on a read-only medium: real drives
                // refuse, and the shared disk path below would answer GOOD.
                setSense(kIllegalRequest, 0x20);
                return kCheck;

            case 0x2F: {                             // VERIFY(10)
                // Range check against 2048-byte blocks. BytChk is accepted
                // and not compared — the shared path's comparison indexes
                // 512-byte blocks, which would be silently wrong here, and a
                // disc that reads at all reads correctly.
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                             | (uint32_t(cdb[4]) << 8) | cdb[5];
                uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
                if (uint64_t(lba) + cnt > dataEndLba()) {
                    setSense(kIllegalRequest, 0x24);
                    return kCheck;
                }
                return kGood;
            }

            case 0x08: case 0x28: {                  // READ(6)/(10)
                if (!discLoaded()) { setSense(kNotReady, 0x3A); return kCheck; }
                const uint32_t at = cdb[0] == 0x08
                    ? ((uint32_t(cdb[1] & 0x1F) << 16) |
                       (uint32_t(cdb[2]) << 8) | cdb[3])
                    : ((uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16) |
                       (uint32_t(cdb[4]) << 8) | cdb[5]);
                if (audioOnly_ || at < dataStartLba_) {
                    // No user data lives here: the disc is all audio, or
                    // the address is inside an audio track ahead of the
                    // data one. SCSI-2 § 14.2.6 calls this ILLEGAL MODE FOR
                    // THIS TRACK, and a drive that answered zeroes instead
                    // would hand the guest a "volume" made of silence.
                    setSense(kIllegalRequest, 0x64);
                    return kCheck;
                }
                break;                               // shared path below
            }

            default:
                break;                               // shared path below
        }
    }

    switch (cdb[0]) {
        case 0x00:                                   // TEST UNIT READY
            // An empty removable tray answers NOT READY, exactly as the CD
            // branch does — that is what makes the driver keep polling and
            // notice the next medium.
            if (kind_ == Kind::Removable && !blocks_) {
                setSense(kNotReady, 0x3A);
                return kCheck;
            }
            return kGood;

        case 0x03: {                                 // REQUEST SENSE
            // Fixed format, 18 bytes (SCSI-2 § 8.2.14). The reply is the
            // LESSER of the allocation length and what the target has: a
            // drive asked for 255 returns 18, it does not pad to 255. An
            // allocation length of 0 keeps the SCSI-1 4-byte short form.
            size_t alloc = cdb[4] ? size_t(cdb[4]) : 4;
            if (alloc > kFixedSenseLen) alloc = kFixedSenseLen;
            dataOut.assign(alloc, 0);
            dataOut[0] = 0x70;                       // current error, fixed format
            if (dataOut.size() > 2) dataOut[2] = senseKey_ & 0x0F;
            if (dataOut.size() > 7) {                // additional length =
                size_t addl = dataOut.size() - 8;    // min(10, alloc-8) per SCSI-1
                dataOut[7] = uint8_t(addl < 10 ? addl : 10);
            }
            if (dataOut.size() > 12) dataOut[12] = senseAsc_;
            if (dataOut.size() > 13) dataOut[13] = senseAscq_;
            setSense(kNoSense, 0);
            return kGood;
        }

        case 0x12: {                                 // INQUIRY
            uint8_t alloc = cdb[4] ? cdb[4] : 36;
            dataOut.assign(alloc, 0);
            if (dataOut.size() > 0) dataOut[0] = 0x00;   // direct-access device
            // RMB: a 512-byte disc dump IS a removable direct-access
            // device, and it says so. Measured both ways on 2026-08-15 —
            // `Apeiron_1_0_3.toast` in the bay at boot mounts with RMB set
            // and with it clear, so the bit is not what gates the mount (the
            // driver partition is); it is set because it is true.
            if (dataOut.size() > 1)
                dataOut[1] = kind_ == Kind::Removable ? 0x80 : 0x00;
            if (dataOut.size() > 2) dataOut[2] = 0x01;   // SCSI-1 (ANSI)
            if (dataOut.size() > 4) dataOut[4] = 31;     // additional length
            // 8 bytes vendor, 16 product, 4 revision. The default is the
            // Apple-branded Seagate an internal Mac drive reports, because
            // that is what the guest's own tools expect to find next to the
            // page $30 signature (RASCSI-X68k disk.cpp:2866-2890 —
            // SCSIHD_APPLE exists for exactly this reason).
            // POM68K_SCSI_INQUIRY=pom68k answers with the emulator's own
            // identity instead, for anyone who would rather see the truth in
            // SCSIProbe than have HD SC Setup cooperate.
            static const char apple[]  = " SEAGATE          ST225N1.0 ";
            static const char pom68k[] = "POM68K  POM68K HD DISK  1.0 ";
            const char* id = ownInquiry_ ? pom68k : apple;
            for (size_t i = 8; i < dataOut.size() && i - 8 < 28; i++)
                dataOut[i] = uint8_t(id[i - 8]);
            return kGood;
        }

        case 0x25: {                                 // READ CAPACITY (10)
            uint32_t last = blocks_ ? blocks_ - 1 : 0;
            dataOut = { uint8_t(last >> 24), uint8_t(last >> 16), uint8_t(last >> 8),
                        uint8_t(last), 0, 0, uint8_t(kBlockSize >> 8), uint8_t(kBlockSize) };
            return kGood;
        }

        case 0x08: {                                 // READ(6)
            uint32_t lba = (uint32_t(cdb[1] & 0x1F) << 16) | (uint32_t(cdb[2]) << 8) | cdb[3];
            uint32_t cnt = cdb[4] ? cdb[4] : 256;
            // Out-of-range: CHECK CONDITION + ILLEGAL REQUEST / INVALID
            // FIELD IN CDB $24 (MAME hd.cpp:216-222), never a silent
            // zero-fill + GOOD — a driver probing past the end must see
            // the error, not a phantom block of zeroes.
            if (uint64_t(lba) + cnt > dataEndLba()) {
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            read(lba, cnt, dataOut);
            return kGood;
        }

        case 0x28: {                                 // READ(10)
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            if (uint64_t(lba) + cnt > dataEndLba()) {     // MAME hd.cpp:567-580
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            read(lba, cnt, dataOut);
            return kGood;
        }

        case 0x1A:                                   // MODE SENSE(6)
            return modeSense(cdb, false, dataOut);
        case 0x5A:                                   // MODE SENSE(10)
            return modeSense(cdb, true, dataOut);

        case 0x0A: {                                 // WRITE(6)
            // A 512-byte disc dump is media, not a disk: read-only, the same
            // answer the CD branch gives.
            if (kind_ == Kind::Removable) {
                setSense(kDataProtect, 0x27);        // WRITE PROTECTED
                return kCheck;
            }
            uint32_t lba = (uint32_t(cdb[1] & 0x1F) << 16) | (uint32_t(cdb[2]) << 8) | cdb[3];
            uint32_t cnt = cdb[4] ? cdb[4] : 256;
            // Out-of-range: CHECK CONDITION instead of the old silent clamp
            // (MAME hd.cpp:225-241). The 5380 has already collected the
            // DATA OUT bytes by the time this runs — the status byte is
            // where the initiator learns the write never landed.
            if (uint64_t(lba) + cnt > dataEndLba()) {
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            write(lba, cnt, dataIn);
            return kGood;
        }
        case 0x2A: {                                 // WRITE(10)
            if (kind_ == Kind::Removable) {          // read-only medium
                setSense(kDataProtect, 0x27);
                return kCheck;
            }
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            if (uint64_t(lba) + cnt > dataEndLba()) {     // MAME hd.cpp:584-600
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            write(lba, cnt, dataIn);
            return kGood;
        }

        case 0x15:                                   // MODE SELECT(6)
        case 0x55:                                   // MODE SELECT(10)
            // Parameter list (delivered via `dataIn` by the controller's
            // DATA OUT phase) is accepted and ignored, status GOOD — MAME's
            // target does exactly this (hd.cpp:622-631). The old default
            // answered CHECK CONDITION, which desynchronized drivers that
            // set error-recovery pages before their first READ.
            //
            // Deliberate simplification, and a real one: a formatter that
            // MODE SELECTs a 1024-byte sector size is told GOOD and keeps
            // getting 512. No Mac tool does this (HFS is 512-bound), and
            // honouring it would mean re-blocking the whole image. Reopening
            // condition: a guest tool observed to set page 3 byte $0C/$0D.
            return kGood;

        case 0x01:                                   // REZERO UNIT
        case 0x1D:                                   // SEND DIAGNOSTIC
        case 0x1E:                                   // PREVENT/ALLOW REMOVAL
        case 0x1B:                                   // START/STOP UNIT
            // Mechanical or self-test commands with nothing to move: a
            // fixed disk cannot be ejected, has no spindle to park and
            // passes every self-test it is asked to run.
            return kGood;

        case 0x07:                                   // REASSIGN BLOCKS
            // The defect list arrives in `dataIn` and is discarded: an image
            // has no spare sectors because it has no bad ones. Answering
            // GOOD is what lets a formatter finish its surface scan.
            return kGood;

        case 0x35:                                   // SYNCHRONIZE CACHE(10)
            // Every WRITE already wrote through to the backing file, so the
            // cache to flush is the host's. Make it explicit anyway — a
            // driver that issues this before telling the user it is safe to
            // power off deserves the real flush, not just a GOOD.
            if (writeBack_ && file_.is_open()) file_.flush();
            return kGood;

        case 0x0B: {                                 // SEEK(6)
            uint32_t lba = (uint32_t(cdb[1] & 0x1F) << 16)
                         | (uint32_t(cdb[2]) << 8) | cdb[3];
            if (lba >= dataEndLba()) { setSense(kIllegalRequest, 0x24); return kCheck; }
            return kGood;
        }
        case 0x2B: {                                 // SEEK(10)
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            if (lba >= dataEndLba()) { setSense(kIllegalRequest, 0x24); return kCheck; }
            return kGood;
        }

        case 0x2E: {                                 // WRITE AND VERIFY(10)
            // Same wire shape as WRITE(10); the verify is free because the
            // write cannot half-land — it is a memcpy into an image.
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            if (uint64_t(lba) + cnt > dataEndLba()) {
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            write(lba, cnt, dataIn);
            return kGood;
        }

        case 0x2F: {                                 // VERIFY(10)
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            if (uint64_t(lba) + cnt > dataEndLba()) {
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            // BytChk clear = "is this range readable" — always yes here.
            // BytChk set = the initiator sent bytes to compare against, and
            // a real MISCOMPARE ($1D) is the only useful answer. Reporting
            // GOOD unconditionally would make a formatter's verify pass a
            // test that never ran.
            if (cdb[1] & 0x02) {
                const uint64_t off = uint64_t(lba) * kBlockSize;
                const uint64_t n = uint64_t(cnt) * kBlockSize;
                if (off + n > image_.size() || dataIn.size() < n ||
                    std::memcmp(image_.data() + off, dataIn.data(), size_t(n)) != 0) {
                    setSense(kMiscompare, 0x1D);
                    return kCheck;
                }
            }
            return kGood;
        }

        case 0x37: {                                 // READ DEFECT DATA(10)
            // An empty list, in the format the initiator asked for. A drive
            // with no defects is exactly what an image is, and formatters
            // read this to decide the surface is clean.
            uint16_t alloc = uint16_t((cdb[7] << 8) | cdb[8]);
            dataOut.assign(4, 0);
            dataOut[1] = uint8_t(cdb[2] & 0x1F);     // echo P/G list + format
            // bytes 2-3 = defect list length = 0
            if (alloc && dataOut.size() > alloc) dataOut.resize(alloc);
            return kGood;
        }

        case 0x04:                                   // FORMAT UNIT
            // MAME answers GOOD (hd.cpp:601-620; its zero-fill loop indexes
            // cyl*head*sector — degenerate — so no data expectation exists).
            // Media is left untouched; a FmtData defect list (`dataIn`) is
            // discarded. A read-only medium refuses — CD or 512-byte dump.
            if (kind_ != Kind::Disk) {
                setSense(kIllegalRequest, 0x20);
                return kCheck;
            }
            return kGood;

        default:
            setSense(kIllegalRequest, 0x20);         // invalid command
            return kCheck;
    }
}
