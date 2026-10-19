// POM68K — PowerBook 150 storage on the MSC bus.
// FDA22562 ROM: ATA task file at $50F1A000, four-byte register stride.
#pragma once
#include "AtaDisk.h"
#include "SonyDrive.h"
#include "Swim1.h"
#include "AdbBus.h"
#include "ScsiAgentTarget.h"

struct MscPb150Devices {
    AtaDisk ide;
    Swim1 swim;
    SonyDrive drive;
    AdbBus mouse;
    ScsiAgentTarget agentTarget;
    uint16_t dataLatch = 0;

    void reset() {
        ide.reset();
        mouse.reset();
        agentTarget.reset();
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        dataLatch = 0;
    }
    uint8_t ataRead(uint32_t offset) {
        if (!ide.present()) return 0;
        const uint8_t reg = uint8_t((offset >> 2) & 15);
        if (reg != AtaDisk::kData) return ide.readRegister(reg);
        if (!(offset & 1)) dataLatch = ide.readData();
        return uint8_t(dataLatch >> ((offset & 1) * 8));
    }
    void ataWrite(uint32_t offset, uint8_t value) {
        if (!ide.present()) return;
        const uint8_t reg = uint8_t((offset >> 2) & 15);
        if (reg != AtaDisk::kData) { ide.writeRegister(reg, value); return; }
        if (!(offset & 1)) dataLatch = value;
        else ide.writeData(uint16_t(dataLatch | (uint16_t(value) << 8)));
    }
    std::vector<uint8_t> adbCommand(uint8_t command, const std::vector<uint8_t>& data) {
        // Only the physical trackball is on this bus; the PG&E scans the keyboard.
        if ((command & 15) == 0) return mouse.command(command, data);
        if ((command >> 4) != mouse.mouseAddr()) return {};
        if ((command & 15) == 12 && !mouse.mousePending()) return {};
        return mouse.command(command, data);
    }
    template<class Ar> void visit(Ar& ar) { ar(ide, swim, drive, dataLatch, mouse, agentTarget); }
};
