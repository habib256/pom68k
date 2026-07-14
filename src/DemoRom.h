// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Built-in demo ROM ──
// Hand-assembled 68000 program used when no Mac Plus ROM is provided: clears
// the boot overlay through the VIA (like the real ROM), then animates a
// rotating diagonal-stripe pattern in the 512×342 framebuffer. Serves as the
// M1/M3 gate: CPU core + memory map + overlay + video all end-to-end.
//
//   $400000  dc.l $0067FF00            ; initial SSP (unused by the demo)
//   $400004  dc.l $00400010            ; initial PC
//   $400008  4×nop                     ; padding
//   $400010  lea   $EFE7FE,a0          ; VIA DDRA
//            move.b #$7F,(a0)          ; PA6..0 outputs, PA7 input (real ROM value)
//            lea   $EFFFFE,a0          ; VIA ORA (no handshake)
//            move.b #$40,(a0)          ; PA4=0 overlay off, PA6=1 main screen buffer
//            lea   $3FA700,a0          ; main screen buffer (4 MB - $5900)
//            moveq #0,d3               ; frame counter
//   frame:   movea.l a0,a1
//            move.l #$F0F0F0F0,d2
//            move.l d3,d1
//            andi.l #31,d1
//            rol.l d1,d2               ; animate: rotate pattern by frame
//            move.w #341,d0            ; 342 rows
//   rows:    moveq #15,d1              ; 16 longs = 512 px
//   rowl:    move.l d2,(a1)+
//            dbra  d1,rowl
//            rol.l #1,d2               ; diagonal: shift 1 bit per row
//            dbra  d0,rows
//            addq.l #1,d3
//            move.l #20000,d1          ; ~1 frame delay
//   delay:   subq.l #1,d1
//            bne.s delay
//            bra.s frame

#pragma once
#include <cstdint>
#include <cstddef>

inline constexpr uint8_t kDemoRom[] = {
    0x00, 0x67, 0xFF, 0x00,                          // SSP
    0x00, 0x40, 0x00, 0x10,                          // PC
    0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,  // nops
    0x41, 0xF9, 0x00, 0xEF, 0xE7, 0xFE,              // lea $EFE7FE,a0 (DDRA)
    0x10, 0xBC, 0x00, 0x7F,                          // move.b #$7F,(a0)
    0x41, 0xF9, 0x00, 0xEF, 0xFF, 0xFE,              // lea $EFFFFE,a0 (ORA)
    0x10, 0xBC, 0x00, 0x40,                          // move.b #$40,(a0)
    0x41, 0xF9, 0x00, 0x3F, 0xA7, 0x00,              // lea $3FA700,a0
    0x76, 0x00,                                      // moveq #0,d3
    0x22, 0x48,                                      // frame: movea.l a0,a1
    0x24, 0x3C, 0xF0, 0xF0, 0xF0, 0xF0,              // move.l #$F0F0F0F0,d2
    0x22, 0x03,                                      // move.l d3,d1
    0x02, 0x81, 0x00, 0x00, 0x00, 0x1F,              // andi.l #31,d1
    0xE3, 0xBA,                                      // rol.l d1,d2
    0x30, 0x3C, 0x01, 0x55,                          // move.w #341,d0
    0x72, 0x0F,                                      // rows: moveq #15,d1
    0x22, 0xC2,                                      // rowl: move.l d2,(a1)+
    0x51, 0xC9, 0xFF, 0xFC,                          // dbra d1,rowl
    0xE3, 0x9A,                                      // rol.l #1,d2
    0x51, 0xC8, 0xFF, 0xF4,                          // dbra d0,rows
    0x52, 0x83,                                      // addq.l #1,d3
    0x22, 0x3C, 0x00, 0x00, 0x4E, 0x20,              // move.l #20000,d1
    0x53, 0x81,                                      // delay: subq.l #1,d1
    0x66, 0xFC,                                      // bne.s delay
    0x60, 0xCE,                                      // bra.s frame
};
inline constexpr size_t kDemoRomSize = sizeof(kDemoRom);
