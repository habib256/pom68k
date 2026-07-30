### medium — Mac Plus/SE/SE FDHD/Classic: the VIA answers over the whole $F00000-$FFFFFF quarter, so a stray access can re-assert the boot overlay
- **Where:** `/home/gistarcade/src/POM68K/src/MacMemory.cpp:233` (read) and `/home/gistarcade/src/POM68K/src/MacMemory.cpp:266` (write)
- **Defect:** `switch (addr >> 20)` groups `case 0xE: case 0xF:` and then only tests `addr >= 0xE80000`, which is unconditionally true for the entire $F00000-$FFFFFF "phase read" space, so the VIA is decoded 8× wider than the hardware.
- **Trigger:** a guest byte write to $F00200 (wild 24-bit pointer, heap walk past the top of the map, ROM address-map probe) selects reg = `($F00200 >> 9) & 0xF` = 1 = ORA; `MacMemory.cpp:179` then runs `overlay_ = (via_.portA() & 0x10) != 0`, re-asserting the ROM overlay: every later RAM write is dropped at line 243 and low memory reads back ROM — instant death. The same access re-drives IWM SEL (line 180). On the read side any access in $F00000-$FFFFFF runs `via_.read(reg)`, clearing IFR flags and silently losing a pending VBL/one-second interrupt. MAME `~/src/refs/mame/src/mame/apple/mac128.cpp:1121` (`macplus_map`) and `:1133` (`macse_map`) map the VIA only at `0xe80000-0xefffff`, with `$fffff0-$ffffff` going to `mac_autovector_r/w` and the rest unmapped; `MacMemory.h:6` and `MacMemory.cpp:152` state the same contract.
- **Fix:** split the two cases.
```cpp
case 0xE:
    if (addr >= 0xE80000) return viaAccess(addr, false, 0);
    return 0xFF;
case 0xF:
    return 0xFF;                 // phase read / autovector space
// and symmetrically in write8:  case 0xF: return;
```

### medium — `peek8()` returns machine-ID bytes for every unmapped address above the VRAM window on four machines
- **Where:** `/home/gistarcade/src/POM68K/src/Q605Memory.cpp:616`, `/home/gistarcade/src/POM68K/src/Q630Memory.cpp:559`, `/home/gistarcade/src/POM68K/src/SonoraMemory.cpp:482`, `/home/gistarcade/src/POM68K/src/VaspMemory.cpp:338`
- **Defect:** the board-ID test in `peek8` is a bare `if (addr >= 0x5FFFFFFC)` with no upper bound, so every address above the VRAM window falls into it — unlike `read8`, which reaches the ID only after the `addr >= 0x60000000` / I/O-select guards (`SonoraMemory.cpp:293-300`, `VaspMemory.cpp:163-169`), which is exactly what the comment `// board ID (mirrors read8)` claims.
- **Trigger:** the GUI/etalon framebuffer decoder in `main.cpp:2794-2809` pointer-chases QuickDraw structures with `pk32()` built on `mem.peek8()`. Early in boot `pk32(0x08A4)` yields garbage such as $F9800000; `pk32(0xF9800000)` then returns $A55A2221 instead of $FFFFFFFF, so the decoder treats it as a live GDevice handle and derives a bogus base/rowBytes/bounds instead of falling back to the default. Same for any debugger peek of the DAFB register cell ($F9800000) or NuBus space.
- **Fix:** bound the test to the I/O select at each of the four sites: `if (addr >= 0x5FFFFFFC && addr < 0x60000000)`.

### low — Quadra 605/630: the IOSB/PrimeTime ID window $5FFF0000-$5FFFFFFB reads 0 instead of $A55A2BAD
- **Where:** `/home/gistarcade/src/POM68K/src/Q630Memory.cpp:281`, `/home/gistarcade/src/POM68K/src/Q605Memory.cpp:333`
- **Defect:** `ioRead8` answers only the last longword (`sub >= 0x0FFFFFFC`); the remaining 64 KB of the ID window matches no other window (`base = sub & 0x3FFFF` = $30000, `sub & ~0xF00000` = $0F0F0000) and falls through to the unmapped-0 default at `Q630Memory.cpp:351`.
- **Trigger:** guest/ROM code reads a longword in $5FFF0000-$5FFFFFFB to identify the I/O ASIC and sees 0 — "no IOSB/PrimeTime present". MAME `~/src/refs/mame/src/mame/apple/iosb.cpp:64-65` installs `0xa55a2bad` across `0x0fff0000-0x0fffffff` (inherited by `primetimeii_device::map`, `iosb.cpp:655-659`), and `macquadra630.cpp:134` overrides only the final longword. `CentrisMemory.cpp:274` already does it right (`if (sub >= 0x0FFF0000)`), so the family is internally inconsistent.
- **Fix:** answer the whole window, with the last longword overridden:
```cpp
if (sub >= 0x0FFF0000) {
    uint32_t id = (sub >= 0x0FFFFFFC) ? machineId_ : 0xA55A2BADu;
    return uint8_t(id >> (8 * (3 - (sub & 3))));
}
```

### low — Valkyrie register window aliases every $100 bytes, letting an offset-$110 write ack the VBL interrupt
- **Where:** `/home/gistarcade/src/POM68K/src/Valkyrie.cpp:43` (read) and `/home/gistarcade/src/POM68K/src/Valkyrie.cpp:69` (write)
- **Defect:** `readReg8`/`writeReg8` do `switch (off & 0xFF)` although `Q630Memory.cpp:336/420` hand them an 8 KB offset (`sub & 0x1FFF`), so $100, $110, $11C … alias onto the timing/config/sense registers MAME leaves unmapped.
- **Trigger:** a guest byte write to $50F2A110 reaches `writeReg8(0x110)` → `case 0x10:` → `config_ = v; intStatus_ &= ~1; recalcIrq();` — a spurious VBL acknowledge that drops the pending frame interrupt. A write to $50F2A100 lands on `case 0x00:` and reprograms `videoTiming_`, which can blank the screen (bit 7) or re-derive the mode/stride under the running video driver. MAME `~/src/refs/mame/src/mame/apple/valkyrie.cpp:242/293` switches on the raw 0..$1FFF offset (cases only up to $85), so all of these hit `default:` and do nothing.
- **Fix:** `switch (off)` in both functions, keeping the existing `default:` arms.

### low — $600000-$7FFFFF is mapped to RAM unconditionally, ignoring the overlay the code says gates it
- **Where:** `/home/gistarcade/src/POM68K/src/MacMemory.cpp:222-223` (read) and `:252-254` (write)
- **Defect:** both arms are labelled `// RAM while overlay on` (and `MacMemory.h:7` says the overlay "maps ROM at $000000 and RAM at $600000 until the ROM clears VIA PA4") but neither consults `overlay_`, unlike the sibling arm at `:243`.
- **Trigger:** after the overlay clears, a read/write at $600000 still resolves to `ram_[0x200000]`. An overlay-state probe that writes a signature at $200000 and re-reads it at $600000 concludes the overlay is still asserted, and a stray write at $600000 silently corrupts physical $200000 inside the live 4 MB image. MAME's `macplus_map` (`mac128.cpp:1113`) and `macse_map` (`:1125`) install nothing at $600000 — only `mac512ke_map` (`:1105`) has `map(0x600000, 0x6fffff)`.
- **Fix:** honour the flag in both arms — read: `if (overlay_) return ram_[addr & (kRamSize - 1)]; return uint8_t(addr >> 16);`; write: `if (overlay_) ram_[addr & (kRamSize - 1)] = v; return;` (if the Plus ROM turns out to need the post-overlay alias, fix the two comments and `MacMemory.h:7` instead so code and contract agree).

### low — SE/SE FDHD/Classic: the overlay clears on a ROM read, not on the first low-RAM write as `ram_w_se` does
- **Where:** `/home/gistarcade/src/POM68K/src/MacMemory.cpp:212-216` (clear site) and `:242-244` (dropped write)
- **Defect:** the code cites `mac128.cpp ram_w_se` but wires the clear to the $400000 ROM-window *read*; `ram_w_se` (`~/src/refs/mame/src/mame/apple/mac128.cpp:380-384`) is the RAM *write* handler installed at $000000-$3FFFFF by `macse_map` (`:1127`), and it clears the overlay **and** commits the byte.
- **Trigger:** two-sided divergence. (a) A compact-model write to low RAM before any $400000 access (reset-time stack push, diagnostic path running from the overlay ROM) is silently dropped and leaves the overlay asserted, where MAME would clear it and store the byte. (b) Conversely, POM68K drops the overlay on the first ROM fetch, so low-address reads return RAM while MAME still returns ROM (`ram_r`, `mac128.cpp:362-369`) until the first RAM write.
- **Fix:** make the write path the authoritative trigger on the ADB models:
```cpp
case 0x0: case 0x1: case 0x2: case 0x3:
    if (isAdb()) overlay_ = false;               // mac128.cpp ram_w_se
    if (!overlay_) ram_[addr & (kRamSize - 1)] = v;
    return;
```
and drop the `isAdb()` clear at `:215`.
