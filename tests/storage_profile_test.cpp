// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Asset-free storage matrix gate.  It accounts for every catalogue profile,
// pins the IWM/SWIM split on the eight early models where board wiring differs,
// and sends real READ commands through a compact Mac's NCR 5380 to both a hard
// disk and a CD-ROM.  Format-level GCR/MFM and CD image-format coverage remains
// in iwm_*_test, swim{1,2}_*_test and scsi_cdrom_test.

#include "CoreConfig.h"
#include "CentrisMemory.h"
#include "IIfxMemory.h"
#include "MacIIMemory.h"
#include "MacMemory.h"
#include "MachineCatalog.h"
#include "MscMemory.h"
#include "Ncr5380.h"
#include "Q605Memory.h"
#include "Q630Memory.h"
#include "Q700Memory.h"
#include "RbvMemory.h"
#include "SonyDrive.h"
#include "SonoraMemory.h"
#include "V8Memory.h"
#include "VaspMemory.h"

#include <concepts>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
int fails = 0;

template <class T>
concept ScsiCdHost = requires(T& mem, const std::string& path) {
    { mem.attachScsi(path, false, 0) } -> std::same_as<bool>;
    { mem.attachCdrom(path, 1) } -> std::same_as<bool>;
};

template <class T>
concept EmptyCdHost = requires(T& mem) {
    { mem.attachCdromEmpty(1) } -> std::same_as<bool>;
};

template <class T>
concept FloppyHost = requires(T& mem) {
    { mem.internalDrive() } -> std::same_as<SonyDrive&>;
};

#define CHECK_DESKTOP_STORAGE(Type) \
    static_assert(ScsiCdHost<Type> && EmptyCdHost<Type> && FloppyHost<Type>)
CHECK_DESKTOP_STORAGE(MacMemory);
CHECK_DESKTOP_STORAGE(MacIIMemory);
CHECK_DESKTOP_STORAGE(IIfxMemory);
CHECK_DESKTOP_STORAGE(RbvMemory);
CHECK_DESKTOP_STORAGE(V8Memory);
CHECK_DESKTOP_STORAGE(SonoraMemory);
CHECK_DESKTOP_STORAGE(VaspMemory);
CHECK_DESKTOP_STORAGE(Q605Memory);
CHECK_DESKTOP_STORAGE(CentrisMemory);
CHECK_DESKTOP_STORAGE(Q700Memory);
CHECK_DESKTOP_STORAGE(Q630Memory);
#undef CHECK_DESKTOP_STORAGE
static_assert(ScsiCdHost<MscMemory> && !EmptyCdHost<MscMemory> &&
              !FloppyHost<MscMemory>);

void check(bool ok, const char* what) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++fails;
}

void switchToIsm(Swim1& swim) {
    constexpr int kQ6On = 13;
    constexpr int kQ7On = 15;
    swim.read(kQ6On);
    swim.write(kQ7On, 0x57);
    swim.write(kQ7On, 0x17);
    swim.write(kQ7On, 0x57);
    swim.write(kQ7On, 0x57);
}

bool req(Ncr5380& scsi) {
    return (scsi.read(Ncr5380::R_CSR) & Ncr5380::CBS_REQ) != 0;
}

void sendByte(Ncr5380& scsi, uint8_t value) {
    scsi.write(Ncr5380::R_DATA, value);
    scsi.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    scsi.write(Ncr5380::R_ICR, 0);
}

uint8_t recvByte(Ncr5380& scsi) {
    const uint8_t value = scsi.read(Ncr5380::R_DATA);
    scsi.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    scsi.write(Ncr5380::R_ICR, 0);
    return value;
}

bool readTarget(Ncr5380& scsi, int id, const uint8_t* cdb, int cdbSize,
                std::size_t byteCount, std::vector<uint8_t>& data) {
    scsi.write(Ncr5380::R_DATA, 0x80);
    scsi.write(Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    scsi.write(Ncr5380::R_DATA, uint8_t(0x80 | (1u << id)));
    scsi.write(Ncr5380::R_MODE, 0);
    scsi.write(Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    if (!req(scsi)) return false;

    for (int i = 0; i < cdbSize; ++i) {
        if (!req(scsi)) return false;
        sendByte(scsi, cdb[i]);
    }
    data.clear();
    data.reserve(byteCount);
    for (std::size_t i = 0; i < byteCount; ++i) {
        if (!req(scsi)) return false;
        data.push_back(recvByte(scsi));
    }
    if (!req(scsi) || recvByte(scsi) != 0x00) return false;
    if (!req(scsi) || recvByte(scsi) != 0x00) return false;
    return (scsi.read(Ncr5380::R_CSR) & Ncr5380::CBS_BSY) == 0;
}

void checkCompactModel(const pom68k::CoreConfig& core, MacMemory::Model model,
                       const char* name, bool superDrive) {
    MacMemory mem(core, model);
    mem.reset();
    char label[128];
    std::snprintf(label, sizeof(label), "%s: board reports the right mechanism", name);
    check(mem.hasSuperDrive() == superDrive &&
          mem.internalDrive().isSuperDrive() == superDrive, label);

    check(mem.internalDrive().insertImage(
              std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
          (std::string(name) + ": accepts 800K GCR media").c_str());
    mem.ejectDisk();
    if (superDrive) {
        check(mem.internalDrive().insertImage(
                  std::vector<uint8_t>(SonyDrive::kSize1440K, 0)) &&
              mem.internalDrive().isHd(),
              (std::string(name) + ": accepts 1.44 MB MFM media").c_str());
        mem.ejectDisk();
    }

    switchToIsm(mem.swim());
    check(mem.swim().ism() == superDrive,
          (std::string(name) + (superDrive
              ? ": 1-0-1-1 enters SWIM ISM"
              : ": 1-0-1-1 remains plain IWM")).c_str());
}

void checkGlueModel(const pom68k::CoreConfig& core, MacIIMemory::Model model,
                    const char* name, bool superDrive) {
    MacIIMemory mem(core, 0x100000, model);
    mem.reset();
    check(mem.hasSuperDrive() == superDrive &&
          mem.internalDrive().isSuperDrive() == superDrive,
          (std::string(name) + ": board reports the right mechanism").c_str());
    check(mem.internalDrive().insertImage(
              std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
          (std::string(name) + ": accepts 800K GCR media").c_str());
    mem.ejectDisk();
    if (superDrive) {
        check(mem.internalDrive().insertImage(
                  std::vector<uint8_t>(SonyDrive::kSize1440K, 0)) &&
              mem.internalDrive().isHd(),
              (std::string(name) + ": accepts 1.44 MB MFM media").c_str());
        mem.ejectDisk();
    }
    switchToIsm(mem.swim());
    check(mem.swim().ism() == superDrive,
          (std::string(name) + (superDrive
              ? ": 1-0-1-1 enters SWIM ISM"
              : ": 1-0-1-1 remains plain IWM")).c_str());
}
} // namespace

int main() {
    std::printf("storage_profile_test — 37-profile floppy/SCSI/CD matrix\n");

    std::size_t none = 0, gcr800 = 0, super = 0, scsi = 0, cdrom = 0;
    for (const auto& profile : pom68k::kMachineProfiles) {
        const auto caps = pom68k::storageCapabilities(profile);
        none += caps.floppy == pom68k::FloppyKind::None;
        gcr800 += caps.floppy == pom68k::FloppyKind::Gcr800K;
        super += caps.floppy == pom68k::FloppyKind::SuperDrive;
        scsi += caps.scsi;
        cdrom += caps.cdrom;
        std::printf("    %-10s floppy=%-10s SCSI=%s CD=%s\n", profile.slug,
                    caps.floppy == pom68k::FloppyKind::None ? "none" :
                    caps.floppy == pom68k::FloppyKind::Gcr800K ? "800K" : "SuperDrive",
                    caps.scsi ? "yes" : "no", caps.cdrom ? "yes" : "no");
    }
    check(pom68k::kMachineProfileCount == 37, "catalogue contains all 37 profiles");
    check(none == 1 && gcr800 == 3 && super == 33,
          "floppy matrix = 1 none + 3 800K-only + 33 SuperDrive");
    check(scsi == 37 && cdrom == 37, "all profiles expose SCSI HDD and SCSI CD-ROM");

    const auto& core = pom68k::defaultCoreConfig();
    checkCompactModel(core, MacMemory::Model::Plus, "Plus", false);
    checkCompactModel(core, MacMemory::Model::SE, "SE", false);
    checkCompactModel(core, MacMemory::Model::SEFDHD, "SE FDHD", true);
    checkCompactModel(core, MacMemory::Model::Classic, "Classic", true);
    checkGlueModel(core, MacIIMemory::Model::MacII, "Mac II", false);
    checkGlueModel(core, MacIIMemory::Model::IIx, "IIx", true);
    checkGlueModel(core, MacIIMemory::Model::IIcx, "IIcx", true);
    checkGlueModel(core, MacIIMemory::Model::SE30, "SE/30", true);

    // Exercise the newly exposed compact SCSI topology, including a target
    // above ID 0.  Distinct patterns prove that the controller selected the
    // intended device and used the correct 512/2048-byte block size.
    const std::string hdPath = "storage_profile_test.hd";
    const std::string isoPath = "storage_profile_test.iso";
    std::vector<uint8_t> hd(4 * 512), iso(4 * 2048);
    for (std::size_t i = 0; i < hd.size(); ++i) hd[i] = uint8_t(0x31 ^ i);
    for (std::size_t i = 0; i < iso.size(); ++i) iso[i] = uint8_t(0xC7 ^ i);
    {
        std::ofstream out(hdPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(hd.data()),
                  std::streamsize(hd.size()));
    }
    {
        std::ofstream out(isoPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(iso.data()),
                  std::streamsize(iso.size()));
    }

    {
        MacMemory mem(core, MacMemory::Model::Plus);
        mem.reset();
        check(mem.attachScsi(hdPath, false, 0), "compact attaches SCSI HDD at ID 0");
        check(mem.attachCdrom(isoPath, 1), "compact attaches ISO CD-ROM at ID 1");
        check(mem.bayIsCdrom(1), "compact GUI sees the live CD-ROM bay");

        const uint8_t read6[6] = {0x08, 0, 0, 0, 1, 0};
        std::vector<uint8_t> out;
        check(readTarget(mem.scsi(), 0, read6, 6, 512, out) &&
              out.size() == 512 && !std::memcmp(out.data(), hd.data(), 512),
              "compact NCR 5380 READ(6) returns the HDD block");

        const uint8_t read10[10] = {0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0};
        check(readTarget(mem.scsi(), 1, read10, 10, 2048, out) &&
              out.size() == 2048 &&
              !std::memcmp(out.data(), iso.data() + 2048, 2048),
              "compact NCR 5380 READ(10) returns the ISO sector");

        check(mem.attachCdromEmpty(2) && mem.bayIsCdrom(2),
              "compact attaches an empty removable CD-ROM bay");
        check(mem.insertBayMedia(2, isoPath), "compact hot-inserts ISO media");
        mem.ejectBayMedia(2);
        check(mem.bayIsCdrom(2), "compact eject keeps the CD-ROM target attached");
    }

    std::remove(hdPath.c_str());
    std::remove(isoPath.c_str());
    if (fails) {
        std::printf("FAILED (%d)\n", fails);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
