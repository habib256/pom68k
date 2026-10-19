// POM68K — Mitsubishi M50753 Power Manager MCU (GPLv3).
// Map, port directions, IRQ priority, timers and ADC follow MAME m5074x.cpp
// (R. Belmont, Olivier Galibert, BSD-3-Clause). See docs/PB030_BRINGUP.md.
#pragma once
#include "M740.h"
#include <array>
#include <vector>

class M50753 {
public:
    static constexpr size_t kRomSize=0x1800;
    explicit M50753(uint32_t oscillatorHz=3932160);
    M50753(const M50753&)=delete;
    M50753& operator=(const M50753&)=delete;
    // ROM is private, immutable input and is not part of a snapshot.
    bool loadRom(const std::vector<uint8_t>& rom);
    uint64_t romIdentity() const { return romIdentity_; }
    void reset(); // RAM survives reset, as on the continuously powered PMU
    int step();
    int run(int cycles);
    uint8_t read(uint16_t a);
    void write(uint16_t a,uint8_t v);
    void setInterrupt(unsigned input,bool asserted); // external INT1 / INT2
    void setPullups(unsigned port,uint8_t bits);
    M740& cpu() { return cpu_; }
    const M740& cpu() const { return cpu_; }
    std::function<uint8_t(unsigned)> readPort;
    std::function<void(unsigned,uint8_t)> writePort;
    std::function<uint8_t()> readInput;
    std::function<uint8_t(unsigned)> readAnalog;
    template<class Ar> void visit(Ar& ar) {
        ar(cpu_,ram_,ports_,ddr_,intCtrl_,timerCtrl_,prescale_,count_,latch_,
           phase_,started_,adControl_,adcLeft_,pwm_);
        if constexpr(Ar::loading) {
            irqs();
            for(unsigned p=0;p<5;++p) sendPort(p);
        }
    }
private:
    M740 cpu_;
    uint32_t oscillatorHz_;
    uint64_t romIdentity_=0;
    std::array<uint8_t,kRomSize> rom_{};
    std::array<uint8_t,0xc0> ram_{};
    std::array<uint8_t,5> ports_{},ddr_{},pullups_{};
    uint8_t intCtrl_=0,timerCtrl_=0;
    std::array<uint8_t,2> prescale_{};
    std::array<uint8_t,3> count_{},latch_{};
    std::array<int,3> phase_{};
    std::array<bool,3> started_{};
    uint8_t adControl_=0;
    int adcLeft_=0;
    bool pwm_=false;
    void irqs();
    void tick(int cycles);
    void sendPort(unsigned p);
    static int portIndex(uint16_t a);
};
