// POM68K — Macintosh Portable / PowerBook 100 Power Manager (GPLv3).
// Wiring: MAME macprtb.cpp (R. Belmont, BSD-3-Clause).
#pragma once
#include "M50753.h"
#include "AdbLine.h"
#include <array>

class PortablePmu {
public:
    explicit PortablePmu(int64_t hostHz);
    bool loadFirmware(const std::vector<uint8_t>& bytes) { active_=mcu_.loadRom(bytes);return active_; }
    bool active() const { return active_; }
    void reset();
    void tick(int cycles);
    void vblank() { mcu_.setInterrupt(0,true); }
    void setRequest(bool level) { request_=level; }
    bool request() const { return request_; }
    bool ack() const { return (port3_&0x40)!=0; }
    // SYS_RST alone drives the 68000; port 3 bit 0 is not a HALT line.
    bool cpuHeld() const { return !active_ || !(port3_&2); }
    bool displayBlank() const { return !(port4_&4); }
    uint8_t data() const { return toHost_; }
    void setData(uint8_t value) { fromHost_=value; }
    void keyEvent(uint8_t key,bool down) { keys_[key&127]=down;line_.keyEvent(key,down); }
    void mouseMove(int dx,int dy) { line_.mouseMove(dx,dy); }
    void mouseButton(bool down,int button=0) { line_.mouseButton(down,button); }
    M50753& mcu() { return mcu_; }
    AdbLine& adbLine() { return line_; }
    const AdbLine& adbLine() const { return line_; }
    // These callbacks carry physical pin levels (VIA CA2/CB1), not IRQ polarity.
    std::function<void(bool)> onSecondPin,onInterruptPin;
    std::function<void(bool)> onResetPin;
    template<class Ar> void visit(Ar& ar) {
        uint64_t firmware=mcu_.romIdentity();int64_t hostHz=hostHz_;ar(firmware,hostHz);
        if constexpr(Ar::loading) {
            if(firmware!=mcu_.romIdentity()) { ar.fail("Power Manager firmware differs");return; }
            if(hostHz!=hostHz_) { ar.fail("Power Manager host clock differs");return; }
        }
        ar(acc_,debt_,wireAcc_,request_,fromHost_,toHost_,port3_,port4_,keys_,line_);
        // GPIO republication restores VIA levels but must not trigger a CPU reset.
        restoring_=Ar::loading;ar(mcu_);restoring_=false;
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
