// Uses the independently assembled MOOF fixture and the actual IWM engine.
#include "Iwm.h"
#include <functional>
static Bytes noisyRead(Iwm& iwm, SonyDrive& drive) {
    Bytes data;
    bool armed = true;
    for (int i = 0; i < 30000; ++i) {
        iwm.tick(4); drive.tick(4);
        const uint8_t value = iwm.read(0);
        if (!(value & 0x80)) armed = true;
        else if (armed) { data.push_back(value); armed = false; }
    }
    return data;
}
static void readNoiseCases(const std::filesystem::path& path) {
    auto blank = fixture();
    std::fill(blank.begin() + 88, blank.begin() + 248, 255);
    putFile(path, blank);
    SonyDrive drive;
    check(drive.insert(path.string()), "unrecorded native track insert");
    const auto surface = save(drive);
    auto pulses = [&](int64_t from) {
        std::vector<int64_t> result;
        for (int i = 0; i < 128; ++i) {
            from = drive.nextFluxAfter(from, false);
            result.push_back(from); ++from;
        }
        return result;
    };
    const auto first = pulses(0);
    check(first.front() >= moof::ticks(128) && first.back() < moof::ticks(16000),
          "blank read amplifier produces pulses after its gain delay");
    const auto revolution = drive.fluxRevTicks();
    auto later = pulses(revolution);
    for (auto& pulse : later) pulse -= revolution;
    check(pulses(0) == first && later != first,
          "same-time queries replay; later revolutions have a different noise stream");
    bool consistent = true;
    for (size_t i = 1; i < first.size(); ++i)
        consistent &= drive.nextFluxAfter(first[i - 1] + 1, false) == first[i] &&
                      drive.nextFluxAfter(first[i], false) == first[i];
    check(consistent && drive.debugFlux().empty() && !drive.dirty() && save(drive) == surface,
          "polling granularity does not alter noise or magnetic surface/state");
    Iwm iwm;
    iwm.attachDrive(&drive, nullptr);
    iwm.read(13); iwm.write(15, 0x1f); iwm.read(12); iwm.read(14); iwm.read(9);
    drive.setMotor(true);
    auto bytes = noisyRead(iwm, drive);
    check(bytes.size() > 64 && std::adjacent_find(bytes.begin(), bytes.end(),
          std::not_equal_to<uint8_t>()) != bytes.end(),
          "actual IWM frames varied bytes from an unrecorded track");
    Bytes snapshot; sav::Writer writer(snapshot); drive.visit(writer); iwm.visit(writer);
    SonyDrive restored; Iwm restoredIwm;
    sav::Reader reader(snapshot.data(), snapshot.size()); restored.visit(reader); restoredIwm.visit(reader);
    restoredIwm.attachDrive(&restored, nullptr);
    check(reader.ok() && noisyRead(iwm, drive) == noisyRead(restoredIwm, restored),
          "fresh drive/controller restore continues weak reads byte-identically");
    drive.setMotor(false);
    check(noisyRead(iwm, drive).size() <= 1,
          "stopped motor supplies no new IWM noise bytes (last data latch may remain)");
    auto gap = fixture();
    moof::put32(gap, 260, 512);
    std::fill(gap.begin() + 1536, gap.begin() + 1600, 0); gap[1536] = 0x80;
    putFile(path, gap); drive.insert(path.string());
    drive.commitFlux(0, drive.fluxRevTicks(), {0, moof::ticks(1600)}, false);
    check(drive.nextFluxAfter(1, false) < moof::ticks(1600) &&
          drive.nextFluxAfter(moof::ticks(1600), false) == moof::ticks(1600) &&
          drive.nextFluxAfter(moof::ticks(1600) + 1, false) >= moof::ticks(1728),
          "long recorded gaps become weak and a real edge resets amplifier delay");
    check(drive.nextFluxAfter(drive.fluxRevTicks() - moof::ticks(80), false) < drive.fluxRevTicks(),
          "weak gap continues across index without an invented delay reset");
    const auto magnetic = drive.debugFlux();
    drive.setWriteBack(true);
    SonyDrive reopened;
    check(drive.flushToFile() && reopened.insert(path.string()) && reopened.debugFlux() == magnetic,
          "native export retains recorded transitions without baking in amplifier noise");
    drive.insertImage(Bytes(SonyDrive::kSize800K));
    bool clean = true;
    const auto normal = drive.debugFlux();
    for (size_t i = 0; i < normal.size(); ++i) {
        const auto edge = normal[i];
        const auto next = i + 1 == normal.size() ? drive.fluxRevTicks() + normal[0] : normal[i + 1];
        clean &= drive.nextFluxAfter(edge, false) == edge && drive.nextFluxAfter(edge + 1, false) == next;
    }
    check(clean, "canonical GCR has neither shifted edges nor added noise pulses");
    drive.setSuperDrive(true); drive.insertImage(Bytes(SonyDrive::kSize1440K)); drive.setMfmMode(true);
    clean = true;
    const auto mfm = drive.debugFlux();
    for (size_t i = 0; i < mfm.size(); ++i) {
        const auto next = i + 1 == mfm.size() ? drive.fluxRevTicks() + mfm[0] : mfm[i + 1];
        clean &= drive.nextFluxAfter(mfm[i], false) == mfm[i] &&
                 drive.nextFluxAfter(mfm[i] + 1, false) == next;
    }
    check(clean, "canonical HD MFM spacing stays free of amplifier noise");
    drive.eject();
    check(drive.nextFluxAfter(0, false) == FluxPll::kNever, "no media produces no fabricated weak pulses");
}
