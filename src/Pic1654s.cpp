// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// PIC1654S (PIC16C54 core, GI/NMOS variant). Instruction semantics follow
// MAME pic16c5x.cpp (Tony La Porta, BSD-3-Clause) + the PIC16C5X datasheet.
// See header for the 0x1654-specific quirks reproduced here.

#include "Pic1654s.h"

bool Pic1654s::loadRom(const uint8_t* data, size_t bytes, bool bigEndian) {
    if (!data || bytes < 2 || (bytes & 1)) return false;
    size_t words = bytes / 2;
    size_t sz = 1;
    while (sz < words) sz <<= 1;             // pow2 so PC wraps and reset = top
    prog_.assign(sz, 0x0FFF);                // unprogrammed = all ones
    for (size_t i = 0; i < words; i++) {
        uint16_t w = bigEndian ? uint16_t(data[i * 2] << 8 | data[i * 2 + 1])
                               : uint16_t(data[i * 2 + 1] << 8 | data[i * 2]);
        prog_[i] = w & 0x0FFF;
    }
    progMask_ = uint16_t(sz - 1);
    reset();
    return true;
}

void Pic1654s::reset() {
    ram_.assign(0x20, 0);                    // 32 file registers, 5-bit direct
    w_ = 0;
    status_ = uint8_t(kStatusHi | ST_TO | ST_PD) & ~ST_PA;  // TO=PD=1, page bits clear
    fsr_ = 0;
    option_ = 0x3F;
    portA_ = portB_ = 0;
    stack_[0] = stack_[1] = 0;
    pclWritten_ = false;
    rtcc_ = true;
    pc_ = progMask_;                         // reset vector = top of ROM
}

void Pic1654s::setRtcc(bool level) {
    // OPTION fixed at 0x3F → T0CS=1 (external), T0SE=1 (count high→low edges).
    if (rtcc_ && !level) ram_[F_TMR0]++;     // falling edge increments TMR0
    rtcc_ = level;
}

uint8_t Pic1654s::readReg(uint8_t f) {
    f &= 0x1F;
    switch (f) {
    case F_INDF: { uint8_t a = uint8_t(fsr_ & 0x1F); return a ? readReg(a) : 0; }
    case F_TMR0:   return ram_[F_TMR0];
    case F_PCL:    return uint8_t(pc_ & 0xFF);
    case F_STATUS: return status_;
    case F_FSR:    return uint8_t(fsr_ | 0xE0);          // unused hi bits read 1
    case F_PORTA:  return uint8_t((readA ? readA() : 0x0F) & portA_) & 0x0F;
    case F_PORTB:  return uint8_t((readB ? readB() : 0xFF) & portB_);
    default:       return ram_[f];
    }
}

void Pic1654s::writeReg(uint8_t f, uint8_t v) {
    f &= 0x1F;
    switch (f) {
    case F_INDF: { uint8_t a = uint8_t(fsr_ & 0x1F); if (a) writeReg(a, v); return; }
    case F_TMR0:   ram_[F_TMR0] = v; return;
    case F_PCL:    pc_ = uint16_t((((status_ & ST_PA) << 4) | v) & progMask_);
                   pclWritten_ = true; return;
    case F_STATUS: status_ = uint8_t(v | kStatusHi); return;   // NMOS: hi bits forced 1
    case F_FSR:    fsr_ = uint8_t(v & 0x1F); return;
    case F_PORTA:  portA_ = uint8_t(v & 0x0F); if (writeA) writeA(portA_); return;
    case F_PORTB:  portB_ = v; if (writeB) writeB(portB_); return;
    default:       ram_[f] = v; return;
    }
}

void Pic1654s::push(uint16_t addr) {         // 2-level hardware stack (shift model)
    stack_[0] = stack_[1];
    stack_[1] = uint16_t(addr & progMask_);
}

uint16_t Pic1654s::pop() {
    uint16_t d = stack_[1];
    stack_[1] = stack_[0];
    return uint16_t(d & progMask_);
}

int Pic1654s::run(int cycles) {
    if (prog_.empty()) return cycles;
    int spent = 0;
    while (spent < cycles) {
        uint16_t op = prog_[pc_ & progMask_];
        pc_ = uint16_t((pc_ + 1) & progMask_);
        int cost = 1;
        pclWritten_ = false;

        if (op & 0x0800) {                   // 1xxx: literal & control
            uint8_t k = uint8_t(op & 0xFF);
            switch (op & 0x0F00) {
            case 0x0800: w_ = k; pc_ = pop(); cost = 2; break;   // RETLW
            case 0x0900:                                         // CALL (8-bit target)
                push(pc_);
                pc_ = uint16_t(((((status_ & ST_PA) << 4) | k) & 0x6FF) & progMask_);
                cost = 2; break;
            case 0x0A00: case 0x0B00:                            // GOTO (9-bit)
                pc_ = uint16_t((((status_ & ST_PA) << 4) | (op & 0x1FF)) & progMask_);
                cost = 2; break;
            case 0x0C00: w_ = k; break;                          // MOVLW
            case 0x0D00: w_ |= k; setZ(w_); break;               // IORLW
            case 0x0E00: w_ &= k; setZ(w_); break;               // ANDLW
            case 0x0F00: w_ ^= k; setZ(w_); break;               // XORLW
            }
        } else if ((op & 0x0C00) == 0x0400) {   // 01xx: bit operations
            uint8_t bit = uint8_t(1u << ((op >> 5) & 7));
            uint8_t f = uint8_t(op & 0x1F);
            switch (op & 0x0300) {
            case 0x0000: writeReg(f, uint8_t(readReg(f) & ~bit)); break;   // BCF
            case 0x0100: writeReg(f, uint8_t(readReg(f) | bit)); break;    // BSF
            case 0x0200: if (!(readReg(f) & bit)) {                        // BTFSC
                             pc_ = uint16_t((pc_ + 1) & progMask_); cost = 2; } break;
            case 0x0300: if (readReg(f) & bit) {                           // BTFSS
                             pc_ = uint16_t((pc_ + 1) & progMask_); cost = 2; } break;
            }
        } else {                                 // 00xx: byte-oriented + control
            uint8_t f = uint8_t(op & 0x1F);
            bool d = (op & 0x20) != 0;           // 0 -> W, 1 -> file
            auto dest = [&](uint8_t r) { if (d) writeReg(f, r); else w_ = r; };
            switch ((op >> 6) & 0x0F) {
            case 0x00:                           // 0x000-0x00F NOP / 0x010-0x01F illegal / MOVWF
                if (d) writeReg(f, w_);          // MOVWF (0x020-0x03F). Else: NOP/illegal —
                break;                           // OPTION/SLEEP/CLRWDT/TRIS do not exist here.
            case 0x01: if (d) writeReg(f, 0); else w_ = 0; status_ |= ST_Z; break; // CLRF/CLRW
            case 0x02: {                                       // SUBWF
                uint8_t fv = readReg(f), r = uint8_t(fv - w_); dest(r);
                if (!(fv < w_)) status_ |= ST_C; else status_ &= ~ST_C;
                if (!((fv & 0x0F) < (w_ & 0x0F))) status_ |= ST_DC; else status_ &= ~ST_DC;
                setZ(r); break; }
            case 0x03: { uint8_t r = uint8_t(readReg(f) - 1); dest(r); setZ(r); break; } // DECF
            case 0x04: { uint8_t r = uint8_t(readReg(f) | w_); dest(r); setZ(r); break; } // IORWF
            case 0x05: { uint8_t r = uint8_t(readReg(f) & w_); dest(r); setZ(r); break; } // ANDWF
            case 0x06: { uint8_t r = uint8_t(readReg(f) ^ w_); dest(r); setZ(r); break; } // XORWF
            case 0x07: {                                       // ADDWF
                uint8_t fv = readReg(f), r = uint8_t(fv + w_); dest(r);
                if (fv > r) status_ |= ST_C; else status_ &= ~ST_C;
                if ((fv & 0x0F) > (r & 0x0F)) status_ |= ST_DC; else status_ &= ~ST_DC;
                setZ(r); break; }
            case 0x08: { uint8_t r = readReg(f); dest(r); setZ(r); break; }  // MOVF
            case 0x09: { uint8_t r = uint8_t(~readReg(f)); dest(r); setZ(r); break; } // COMF
            case 0x0A: { uint8_t r = uint8_t(readReg(f) + 1); dest(r); setZ(r); break; } // INCF
            case 0x0B: {                                       // DECFSZ
                uint8_t r = uint8_t(readReg(f) - 1); dest(r);
                if (!r) { pc_ = uint16_t((pc_ + 1) & progMask_); cost = 2; } break; }
            case 0x0C: {                                       // RRF
                uint8_t fv = readReg(f);
                uint8_t r = uint8_t((fv >> 1) | ((status_ & ST_C) ? 0x80 : 0)); dest(r);
                if (fv & 1) status_ |= ST_C; else status_ &= ~ST_C; break; }
            case 0x0D: {                                       // RLF
                uint8_t fv = readReg(f);
                uint8_t r = uint8_t((fv << 1) | ((status_ & ST_C) ? 1 : 0)); dest(r);
                if (fv & 0x80) status_ |= ST_C; else status_ &= ~ST_C; break; }
            case 0x0E: { uint8_t fv = readReg(f);              // SWAPF
                         dest(uint8_t((fv >> 4) | (fv << 4))); break; }
            case 0x0F: {                                       // INCFSZ
                uint8_t r = uint8_t(readReg(f) + 1); dest(r);
                if (!r) { pc_ = uint16_t((pc_ + 1) & progMask_); cost = 2; } break; }
            }
        }

        if (pclWritten_) cost++;             // computed goto / PCL write = +1 cycle
        spent += cost;
    }
    return spent;
}
