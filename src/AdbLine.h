// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Bit-serial ADB bus + devices (LLE) ──
// The device side of the Apple Desktop Bus: a wired-AND open-collector line
// with a keyboard (addr 2) and a mouse (addr 3) that decode command bits and
// reply with data bits in real time. Driven by the PIC1654S transceiver
// (Pic1654s / AdbVia) exactly as on hardware — no command-level shortcut.
//
// Ported from MAME src/mame/apple/macadb.cpp (R. Belmont, BSD-3-Clause,
// GPLv3-compatible; header preserved). Timing rebased from MAME's 2 MHz
// attotime ticks onto Mac II CPU cycles (1 ADB tick ≈ 7.8336 cycles).
//
// Line convention: level 1 = released (pulled high), 0 = asserted (low).
// combined line = hostDrive AND deviceDrive (open collector).

#pragma once
#include <cstdint>
#include <deque>

class AdbLine {
public:
    void reset();

    // Host input events (UI thread → machine, MacInput conventions).
    void keyEvent(uint8_t adbCode, bool down);
    void mouseMove(int dx, int dy);
    void mouseButton(bool down);

    // Wired-AND line interface with the transceiver (PIC RA2 out / RA3 in).
    void setHostDrive(bool high);                 // PIC's contribution to the line
    bool line() const { return hostDrive_ && deviceDrive_; }  // combined level

    // Advance the device timers by `cyc` CPU cycles (runs the send machine).
    void tick(int cyc);

    // Debug accessors.
    uint8_t mouseAddr() const { return mouseAddr_; }
    uint8_t keyboardAddr() const { return kbdAddr_; }
    uint8_t dbgCommand() const { return command_; }
    int     dbgDatasize() const { return datasize_; }
    int     dbgLinestate() const { return linestate_; }
    uint8_t dbgBuffer(int i) const { return buffer_[i & 7]; }
    bool    dbgTransmitting() const { return linestate_ >= LST_TSTOPSTART; }

private:
    // ADB line states (MAME macadb ordering: receive then send).
    enum {
        LST_IDLE = 0, LST_ATTENTION,
        LST_BIT0, LST_BIT1, LST_BIT2, LST_BIT3, LST_BIT4, LST_BIT5, LST_BIT6, LST_BIT7,
        LST_TSTOP, LST_WAITT1T, LST_RCVSTARTBIT, LST_SRQNODATA,
        LST_TSTOPSTART, LST_TSTOPSTARTa, LST_STARTBIT,
        LST_SENDBIT0, LST_SENDBIT0a, LST_SENDBIT1, LST_SENDBIT1a,
        LST_SENDBIT2, LST_SENDBIT2a, LST_SENDBIT3, LST_SENDBIT3a,
        LST_SENDBIT4, LST_SENDBIT4a, LST_SENDBIT5, LST_SENDBIT5a,
        LST_SENDBIT6, LST_SENDBIT6a, LST_SENDBIT7, LST_SENDBIT7a,
        LST_SENDSTOP, LST_SENDSTOPa,
    };

    void receiveEdge(bool level, int64_t dtime);  // host-driven line change
    void timerTick();                             // device send machine step
    void adbTalk();                               // decode m_command, fill buffer
    void writeData(bool level);                   // device drives the line
    void armTimer(int64_t cyc) { sendTimer_ = cyc; }

    bool  mousePending() const;
    bool  keyPending() const { return !keyBuf_.empty(); }

    // Line + timing.
    bool     hostDrive_ = true;
    bool     deviceDrive_ = true;
    int      linestate_ = LST_IDLE;
    int64_t  now_ = 0;                             // running CPU-cycle clock
    int64_t  lastEdge_ = 0;                        // cycle of last line change
    int64_t  sendTimer_ = -1;                      // <0 = disabled

    // Transaction state.
    uint8_t  command_ = 0;
    bool     waitingCmd_ = false;
    int      direction_ = 0;                       // 1 = Listen (host→device)
    uint8_t  buffer_[8] = {};
    int      datasize_ = 0;
    int      streamPtr_ = 0;
    bool     srqFlag_ = false;
    bool     srqSwitch_ = false;
    uint8_t  listenAddr_ = 0, listenReg_ = 0;      // pending Listen target

    // Devices.
    uint8_t  kbdAddr_ = 2, kbdHandler_ = 0x22;
    uint8_t  mouseAddr_ = 3, mouseHandler_ = 0x23;
    std::deque<uint8_t> keyBuf_;                   // ADB key transition bytes
    int      mdx_ = 0, mdy_ = 0;
    bool     mbtn_ = false, mbtnSent_ = false;
    uint8_t  lastMouse_[2] = { 0xFF, 0xFF };
    uint8_t  lastKbd_[2] = { 0xFF, 0xFF };
};
