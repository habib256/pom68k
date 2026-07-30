## H9b — input / NuBus / declaration ROM

### HIGH — Synthetic Toby declaration ROM fallback installs an EMPTY decl ROM
- **Where:** `src/MacIIMemory.cpp:64`
- **Defect:** `buildSynthetic()` returns an already guest-ordered image, but it is piped through `DeclRom::installRaw()`, which is written for the MAME *reversed* file dump — it reverses first, reads `byteLanes` from the wrong end (`0x00`), and falls into `default: return {}` (`src/DeclRom.cpp:244-245`).
- **Trigger:** Any Mac II / IIx / IIcx run where none of the four `342-0008-a.bin` paths resolve (`tests/data/` is gitignored, the ROM is user-provided). Verified empirically by compiling `src/DeclRom.cpp` standalone: `syn n=284 first=00 last=0F` → `installRaw(syn) size=0`. `nubus_.installCard(9, toby_, {})` then leaves `slots_[9].declRom` empty, so `NuBus::read8` (`src/NuBus.cpp:80-83`) falls through to `TobyVideo::read8`, which returns `0xFF` for every offset ≥ `0xE0000`; the Slot Manager reads no `$5A932BC7` test pattern at `$F9FFFFFC` → `smNoBoard`. The machine boots with *no* video card, i.e. the fallback does nothing while printing "using synthetic". `tests/nubus_test.cpp:44` and `tests/toby_test.cpp:20` install the synthetic ROM directly, which is why no gate catches it.
- **Fix:** drop the round trip.
```cpp
        auto syn = DeclRom::buildSynthetic(nubus_.slotBase(9));
        decl = std::move(syn);              // already guest-ordered, byteLanes 0x0F
```

### HIGH — Data race / use-after-free on `floppyPending_` between the UI and machine threads
- **Where:** `src/main.cpp:2924` (reader), `src/main.cpp:2588-2592` (writer)
- **Defect:** `applyCmds()` releases `cmdMu_` after the queue swap (`src/main.cpp:2913`) and then reads/clears the plain `std::string floppyPending_` (`src/main.cpp:2939`) unlocked, while `requestInsertFloppy()` reassigns it on the GLFW thread under the mutex.
- **Trigger:** User picks a floppy from the Disques menu (`src/main.cpp:3152, 3459, 3748, 4038`); the machine thread passes `floppyPending_` by `const std::string&` into `mem.insertDisk(...)`, which does file I/O for milliseconds. A second pick in that window makes the UI thread's `operator=` free the heap buffer being read → use-after-free, or a torn read of the SSO/pointer/size union. `floppyPending_.clear()` at :2926 races identically. Every other cross-thread field in this struct is either mutex-guarded (`fbShared_`/`fbW_`/`fbH_`) or `std::atomic` — the omission is inconsistent with the struct's own contract.
- **Fix:** take the payload under the same lock:
```cpp
    std::string pending;
    { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); pending.swap(floppyPending_); }
    ... case Cmd::InsertFloppy: if (!pending.empty() && mem.insertDisk(pending)) ...
```

### MEDIUM — `validateFormatBlock` inverts unconditionally, rejecting ROMs `installRaw` accepts
- **Where:** `src/DeclRom.cpp:127`
- **Defect:** the reversed-path branch detects the "inverted" marker (`tmp[n-2] == 0xFF`) only to un-invert `lanes`, then XORs the whole image with `0xFF` regardless of that flag before `check()`.
- **Trigger:** a reversed-but-not-inverted card ROM. Verified by compiling `src/DeclRom.cpp`: `validate(reversed) = 0` while `installRaw(rev) = 284` — the two functions in the same file disagree about which ROMs are valid, so a card the machine can boot fails the only validator the project has (`tests/declrom_test.cpp:46`). The sibling `installRaw` guards it correctly (`src/DeclRom.cpp:214, 248-249`), as does MAME's `install_declaration_rom`.
- **Fix:**
```cpp
    bool inv = (tmp[n - 2] == 0xFF);
    uint8_t lanes = inv ? uint8_t(tmp[n - 1] ^ 0xFF) : tmp[n - 1];
    if (lanes != kByteLanes && lanes != 0xE1) return false;
    if (inv) for (auto& b : tmp) b ^= 0xFF;
    return check(tmp.data());
```

### MEDIUM — `buildSynthetic` VendorInfo offset points 16 bytes ahead, into the middle of the string
- **Where:** `src/DeclRom.cpp:139`
- **Defect:** `b.offs(0x01, b.pos() + 16)` — `Builder::offs` computes `rel = target - pos()` with `pos()` still at the entry (`src/DeclRom.cpp:22-28`), but the string is emitted immediately after the 4-byte entry, so the Vendor-ID offset lands 12 bytes into "Apple Computer".
- **Trigger:** any Slot Manager walk of the synthetic board sResource (`tests/nubus_test.cpp:44` installs it verbatim; also the fixed fallback above). Dump of the built image at 0x18 (verified by running the built code): `72 00 | 01 00 00 10 | 41 70 70 6C 65 20 43 6F 6D 70 75 74 65 72 00 00 | FF 00 00 00` — entry at 0x1A, string at 0x1E, target 0x2A = `65 72 00` = "er". Worse, the string sits *before* the `$FF` end-of-list, so a directory walker reads 0x1E as the next entry: type `$41` ('A') with a 24-bit offset `$70706C`, inventing bogus sResources until it hits the `$FF` at 0x2E.
- **Fix:** emit the data after the terminator and point at it:
```cpp
    b.offs(0x01, b.pos() + 8); b.endOfList(); b.string("Apple Computer");
```
(entry 4 B + end-of-list 4 B = 8), or minimally change the target to `b.pos() + 4`.

### MEDIUM — M0110 Inquiry ($10) is answered like Instant ($14): no ~1/4 s hold
- **Where:** `src/MacInput.cpp:19`
- **Defect:** `MacKeyboard::respond()` falls Inquiry through to the Instant arm and returns Null `$7B` as soon as the fixed 3 ms byte time elapses; the real M0110 answers an Inquiry only on a key transition, or with Null after roughly 250 ms — and that hold is what paces the Mac's keyboard poll loop.
- **Trigger:** idle Mac Plus. The driver writes `$10` (`src/MacMemory.cpp:184-187`, `kbdTimer_ = 23500` ≈ 3 ms), gets SHIFT #1, flips ACR to mode 011, gets `$7B` 3 ms later (`src/MacMemory.cpp:95-111`), then re-issues Inquiry immediately (the "re-issue Inquiry forever" loop of CHANGELOG 2026-07-21) — ~166 transactions and ~333 level-1 VIA SR interrupts per second where hardware idles at ~4/s. That collides with the fragile Plus IPL arbitration (DEV.md § Input: the glue suppresses VIA /IPL0 while the SCC interrupts, and an unserviced ext/status source latches only once), stealing mouse DCD servicing — the quadrature loss `tests/input_etalon.cpp:60` has to absorb with its ±2 window. DEV.md:220-227 pins the command set and the two-SR pacing but never states the hold is dropped, and TODO.md's only M0110 gap is the `$79` keypad prefix (TODO.md:426).
- **Fix:** give Inquiry its own delay — stay in `KBD_AWAIT_IN`/`KBD_SHIFT_IN` until the queue is non-empty or ~1 958 400 cycles (250 ms @ 7.8336 MHz) have elapsed, then emit `$7B`; Instant keeps the immediate path.

### LOW — `AdbLine`: Talk to an unconnected address never raises the mouse SRQ
- **Where:** `src/AdbLine.cpp:292`
- **Defect:** the Talk case handles only `addr == mouseAddr_` and `addr == kbdAddr_`; the MAME branch that sets `srqflag` when a Talk hits an unconnected device and the mouse has data pending is missing.
- **Trigger:** ADB Manager polls of a vacant address — routine during ADBReInit's 1..15 scan and after address relocation. MAME `src/mame/apple/macadb.cpp:714-724` does `m_buffer[0]=m_buffer[1]=0; m_datasize=0; if ((adb_pollmouse()) && (m_mouse_handler & 0x20)) m_srqflag = true;`. POM68K just times out, so accumulated motion sits undelivered until the host happens to Talk the keyboard address (the only surviving SRQ path, `src/AdbLine.cpp:286`). POM68K reproduced MAME's other two SRQ sites faithfully (`:269`, `:286`), so this is an omission, not a documented divergence.
- **Fix:**
```cpp
            } else {                              // unconnected device
                buffer_[0] = buffer_[1] = 0; datasize_ = 0;
                if (mousePending() && (mouseHandler_ & 0x20)) srqFlag_ = true;
            }
```

### LOW — `MacMouse` state survives a machine hard reset
- **Where:** `src/MacInput.h:33`
- **Defect:** `MacKeyboard` has `reset()` and is cleared in `MacMemory::reset()` (`src/MacMemory.cpp:62`), but `MacMouse` has no `reset()` at all, so `dx_`/`dy_`, `button_` and the X1/X2/Y1/Y2 line levels carry across a hard reset.
- **Trigger:** GUI Reset after a fast drag. `MacMouse::move` accumulates with no clamp while `tick()` drains ≤1 step per 4000 cycles (~32/frame), so hundreds of queued steps keep firing SCC DCD interrupts and PB4/PB5 transitions into the freshly booting ROM (`src/MacMemory.cpp:113-118`), and a stale `button_` keeps PB3 low (`src/MacMemory.cpp:142`) — which the boot ROM reads as "mouse button held at startup" (eject-boot-floppy path). The ADB side does both: `AdbBus::reset()` clears `mdx_/mdy_/mbtn_` and `AdbBus::mouseMove` clamps to ±256 (`src/AdbBus.cpp:7-13, 20-21`).
- **Fix:** add `void reset() { dx_ = dy_ = 0; phase_ = 0; button_ = false; x1 = y1 = x2 = y2 = false; }` to `MacMouse` and call `mouse_.reset();` beside `kbd_.reset();`; clamp the accumulator in `move()` as `AdbBus` does.

### LOW — Left and Right Shift share one transition code with no press count
- **Where:** `src/main.cpp:4423` (and the nine ADB duplicates at `:999, 1545, 2012, 2278, 2519, 3194, 3501, 3790, 4080`)
- **Defect:** both `ImGuiKey_LeftShift` and `ImGuiKey_RightShift` map to M0110 `$71` (ADB `$38`), and each release unconditionally enqueues the up transition `$F1` (`src/main.cpp:4431-4433`).
- **Trigger:** hold Left Shift, add Right Shift, release Left Shift — the guest gets down, down, up, so the System clears the KeyMap bit while Shift is still physically held and subsequent characters come out lowercase. The `AdbLine` key path just pushes into `keyBuf_` (`src/AdbLine.cpp:275-283`), so no dedup saves it.
- **Fix:** a `static uint8_t held[128]` indexed by `e.code >> 1`; emit the down transition only on 0→1 and the up transition only on 1→0. Same helper serves the ADB branch.

### LOW — Declaration ROM aliased into the 256 MB super-slot space (256 copies)
- **Where:** `src/NuBus.cpp:77`
- **Defect:** `read8` derives the decl-ROM window from `off & 0x0FFFFF` without distinguishing slot space from super-slot space, so the top 128 KB of *every* 1 MB block of the `$s0000000` window is answered by the declaration ROM.
- **Trigger:** a read of `$900E0000` decodes via the `super` branch (`src/NuBus.cpp:40-43`) to slot 9 / `off = 0x000E0000`; `low = 0xE0000 >= 0xE0000` → `readDecl8(9, 0x000E0000)`. Repeats for all 256 MB-blocks and every populated slot, shadowing any card that uses super-slot space for a >1 MB aperture. MAME installs the declaration ROM only inside the 16 MB `$Fs` window (`~/src/refs/mame/src/devices/bus/nubus/nubus_m2video.cpp:151`, `mirror 0x00f00000`), never in super-slot space, and `NuBus.h:4-5` states the same contract ("at the top of each populated slot").
- **Fix:** have `decode()` report which space matched and only consult the decl ROM for slot space: `if (isSlotSpace && low >= 0xE0000 && !declRom.empty()) return readDecl8(...); return dev->read8(off);`
