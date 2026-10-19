// POM68K — PowerBook 140/170 M50753 board wiring, GPLv3.
// Reference: MAME macpwrbk030.cpp pmu_* handlers; docs/PB030_BRINGUP.md.
#pragma once
#include "M50753.h"
#include "AdbLine.h"
#include <array>

class Pb030Pmu {
public:
    explicit Pb030Pmu(int64_t hostHz);
    bool loadFirmware(const std::vector<uint8_t>& bytes) { active_=mcu_.loadRom(bytes);return active_; }
    bool active() const { return active_; }
    void reset();
    void tick(int cycles);
    void vblank() { mcu_.setInterrupt(0,true); }
    void setRequest(bool level) { request_=level; }
    bool request() const { return request_; }
    bool ack() const { return (port3_&0x40)!=0; }
    bool cpuHeld() const { return !active_ || (port3_&3)!=3; }
    bool displayBlank() const { return !(port4_&4); }
    uint8_t data() const { return toHost_; }
    void setData(uint8_t value) { fromHost_=value; }
    void keyEvent(uint8_t key,bool down) { keys_[key&127]=down;line_.keyEvent(key,down); }
    void mouseMove(int dx,int dy) { line_.mouseMove(dx,dy); }
    void mouseButton(bool down,int button=0) { line_.mouseButton(down,button); }
    M50753& mcu() { return mcu_; }
    AdbLine& adbLine() { return line_; }
    const AdbLine& adbLine() const { return line_; }
    std::function<void()> onResetRelease;
    std::function<void(bool)> onInterruptPin;
    template<class Ar> void visit(Ar& ar) {
        uint64_t firmware=mcu_.romIdentity();ar(firmware);
        if constexpr(Ar::loading) {
            if(firmware!=mcu_.romIdentity()) {ar.fail("Power Manager firmware differs");return;}
        }
        // Restore the latches before MCU.visit republishes its output pins.
        // Republishing is a level restore, never a fresh reset edge.
        ar(acc_,debt_,wireAcc_,request_,fromHost_,toHost_,port3_,port4_,keys_,line_);
        restoring_=Ar::loading;
        ar(mcu_);
        restoring_=false;
    }
private:
    M50753 mcu_;
    AdbLine line_;
    int64_t hostHz_,acc_=0,wireAcc_=0;
    int debt_=0;
    bool active_=false,request_=true,restoring_=false;
    uint8_t fromHost_=0,toHost_=0,port3_=0,port4_=0;
    std::array<bool,128> keys_{};
};
