## H9-serial-input-nubus — verified findings

### HIGH — SDLC Rx overrun stalls the wire forever instead of dropping the byte
- **Where:** `src/Scc8530.cpp:188-192` (`Scc8530::rxPushByte`)
- **Defect:** On a full 3-deep FIFO the function flags RR1 bit 5 and `return`s **without advancing `c.rxPos`**, so the byte is re-presented every byte-time instead of being lost — the frame in flight never terminates.
```cpp
    if (c.fifo.size() >= 3) {                    // overrun: drop, flag RR1.5
        if (!c.fifo.empty()) c.fifo.back().rr1 |= 0x20;
        raiseRxInt(c, true);
        return;                                  // <-- rxPos NOT advanced
    }
```
  (contrast the `!rxEnabled` branch two lines above, which *does* `c.rxPos++` and terminates the frame; and MAME `z80scc.cpp` `receive_data`, which stores-but-does-not-step the FIFO, i.e. the byte is consumed and lost.)
- **Trigger:** Any non-lossless configuration — `main.cpp:121` only calls `setLosslessRx(true)` when `hub && !cable`, so the LToUDP-cable path and `POM68K_ATALK_WIRE_BOOST=1` run here, and `Scc8530.h:126` states this path "keeps real hardware semantics". Guest has WR3 bit 0 set, 3 bytes queued, and stops servicing (interrupts masked, LAP torn down, modal loop). `tick()` (`Scc8530.cpp:849-853`) re-calls `rxPushByte` every pace forever. Consequences: `c.rxCur` never empties → `c.rxIdle = 0` (`:794`) → the IDG test `c.rxIdle >= kIdgBytes * realPaceOf(c)` (`:815`) can never pass, so **every later frame is stuck in `rxQueue` permanently**; and `rr0()`'s `rxBusy` mask (`:116,124`) suppresses the open-line Abort forever, so LLAP carrier sense reads "carrier present" for good. Even transiently, RR1 bit 5 is reported on a frame from which no byte was actually lost.
- **Fix:** consume the byte, matching MAME:
```cpp
    if (c.fifo.size() >= 3) {                    // overrun: byte lost on the wire
        if (!c.fifo.empty()) c.fifo.back().rr1 |= 0x20;
        raiseRxInt(c, true);
        c.rxPos++;
        if (last) { c.rxCur.clear(); c.rxPos = 0; if (!c.hunt) c.hunt = true; }
        return;
    }
```

### MEDIUM — Rx frame queue is unbounded exactly on the network-facing (LToUDP) path
- **Where:** `src/Scc8530.cpp:309` (`injectRxFrame`)
- **Defect:** The tail-drop cap is gated on `losslessRx_`, which `main.cpp:121-130` enables only for the in-process hub *without* a cable; with the multicast socket up the cap is dead code and the only other discard (`:270`, `!express && !rxEnabled(c) && !losslessRx_`) fires only while the receiver is **off**.
```cpp
if (losslessRx_ && !express && c.rxQueue.size() >= kLosslessQueueMax) { c.rxDropped++; return; }
c.rxQueue.push_back({std::move(f), pace, delay, express, c.wireClk});
```
- **Trigger:** `POM68K_LTOUDP=1` binds `INADDR_ANY:1954` and joins `239.192.76.84` with no source filtering (`LtoUdp.cpp:41-57`); every datagram reaches `injectRxFrame` (`main.cpp:183`). Playback of one 600-byte frame costs ~602 × 544 cycles plus the IDG (≈45 frames/s drained at 15.67 MHz). Any LAN peer emitting ~1000 datagrams/s grows `rxQueue` by ~950 vectors/s (~600 KB/s) with no ceiling until OOM. The comment above the cap ("a real wire … has NO buffer, so congestion self-limits by dropping") applies verbatim to this queue.
- **Fix:** drop the mode gate — `if (!express && c.rxQueue.size() >= kLosslessQueueMax) { c.rxDropped++; return; }`.

### MEDIUM — SCC back-pressure meters read from the GUI thread while the emulation thread mutates them
- **Where:** `src/Scc8530.h:132-135`, consumed at `src/AtalkHub.h:67-70` / `:152-155`
- **Defect:** `rxBacklog()` etc. read `std::deque::size()` and plain `size_t/int64_t/long` members with no synchronisation; `AtalkHub::mu_` protects the hub's own state, not the SCC.
```cpp
size_t  rxBacklog(int ch)        const { return ch_[ch & 1].rxQueue.size(); }
int64_t rxHoldMaxCycles(int ch)  const { return ch_[ch & 1].rxHoldMax; }
```
- **Trigger:** GUI run — the machine loop is its own `std::thread` (`main.cpp:752`) doing `rxQueue.push_back` (`injectRxFrame`, called at `main.cpp:183` outside any hub lock) and `rxQueue.pop_front` / `rxHoldMax` writes (`Scc8530.cpp:820-838`), while the render thread calls `appleTalkWindow()` (`main.cpp:406-408`) → `snapshot()` → `wire_()`. libstdc++'s `deque::size()` derives from `_M_start`/`_M_finish` members being concurrently rewritten (map reallocation on push) — a data race, and in practice a garbage backlog that `snapshot()` then scales (`s.wire.holdMax * 1000 / cpuHz_`).
- **Fix:** have the machine thread publish the four values into `std::atomic` members (or into the mutex-protected `AtalkHub` state) at slice end in `runQuantumWithWire`, and read those in `snapshot()` instead of the live SCC.

### MEDIUM — RR4-RR14 all read back 0 instead of their NMOS 8530 aliases (RR11 repeats the already-fixed RR15 bug)
- **Where:** `src/Scc8530.cpp:389` (`readCtl_` `default: return 0;`)
- **Defect:** Only RR0/RR1/RR2/RR3/RR15 are implemented. MAME `z80scc.cpp:1461-1467` does the NMOS remap up front (`reg>3 && reg<8 → reg&3`, `9→13`, `11→15`) and additionally serves `RR8 = data_read()`, `RR12 = m_wr12`, `RR13 = m_wr13`, `RR14 → m_rr10`.
- **Trigger:** A driver pointing the register pointer at 11 asks for the Ext/Status IE mask (RR11 is the NMOS image of RR15) and gets 0 — every ext/status source reads as disabled. That is exactly the failure this file documents and fixed for RR15 itself (`Scc8530.cpp:381-388`: "returning 0 made every ext/status look disabled and the LAP state machine never advanced past carrier sense"), and the alias at 11 still returns 0. Likewise RR12/RR13 baud-readback returns 0/0, and RR8 returns 0 instead of the FIFO byte. `docs/LLE_VS_HLE.md:419-446` does not list these as accepted gaps.
- **Fix:** remap before the switch and add the real registers:
```cpp
    if (reg > 3 && reg < 8) reg &= 3;
    else if (reg == 9)  reg = 13;
    else if (reg == 11) reg = 15;
    switch (reg) { ... case 8: return readData(channel);
                       case 12: return c.wr[12];
                       case 13: return c.wr[13];
                       case 14: return 0; ... }
```

### LOW — WR9 channel/hardware reset leaves every write register armed
- **Where:** `src/Scc8530.cpp:556-580` (`resetChan` lambda)
- **Defect:** The reset purges Rx/Tx machinery and IP latches but never touches `c2.wr[]`, so WR1 (Ext/Tx IE), WR3, WR4 (mode), WR5 bit 3 (Tx Enable), WR14 and WR15 survive a reset the guest believes clears them. Zilog/MAME `z80scc.cpp:984-1013`: `m_wr1 &= 0x24; m_wr3 &= 0x01; m_wr4 = 0x04; m_wr5 = 0x00; m_wr14 = (m_wr14 & 0xc3)|0x20; m_wr15 = 0xf8;`.
- **Trigger:** The 7.6 LAP open (cited in the comment at `:552-555`) does Channel Reset B then reprograms. In the window between, WR15 bit 7 + WR1 bit 0 are still armed, so `tick()`'s relatch path (`:862`) can raise an ext/status interrupt on a channel whose driver state is torn down; WR5 bit 3 still set means a data-port write before the driver re-enables Tx is shifted onto the wire (`txLoad`, `:401`) instead of being held; RR15 reads stale rather than the `$F8` reset value.
- **Fix:** add the six assignments above inside `resetChan` (the `if (ptr_ == 9) updateSerial(...)` at `:581` already re-derives the pace).

### LOW — `DeclRom::installRaw` reads `rom[n-2]` with only `n == 0` guarded
- **Where:** `src/DeclRom.cpp:214`
```cpp
    if (!raw || n == 0) return {};
    ...
    uint8_t byteLanes = rom[n - 1];
    bool inverted = rom[n - 2] == 0xFF;   // n == 1 -> rom[SIZE_MAX]
```
- **Defect:** With `n == 1`, `rom[n - 2]` is `std::vector::operator[](SIZE_MAX)` — an unchecked out-of-bounds read (UB). The sibling `validateFormatBlock` in the same file guards properly (`DeclRom.cpp:112`: `if (n < 20) return false;`), so the file is inconsistent with its own contract.
- **Trigger:** `tests/declrom_test.cpp:43-47` reads `342-0008-a.bin` from disk and passes `file.size()` straight in; the `check(file.size() == 4096, ...)` on line 45 only increments a counter, it does not stop execution — a truncated/placeholder 1-byte file reaches `installRaw`. (The production caller `loadTobyRaw` does enforce 4096, so exposure is the test path only.)
- **Fix:** `if (!raw || n < 20) return {};`

### LOW — LToUDP silently truncates oversized datagrams and the SCC then regenerates a *valid* FCS over the truncated body
- **Where:** `src/LtoUdp.cpp:97`
```cpp
    uint8_t pkt[4 + kMaxFrame];
    ssize_t n = ::recv(fd_, pkt, sizeof pkt, 0);   // no MSG_TRUNC, no n == sizeof pkt test
```
- **Defect:** A datagram larger than 704 bytes is delivered as a silently shortened frame (the send side does bound it, `LtoUdp.cpp:77`). `injectRxFrame` then computes the FCS itself over whatever arrived (`Scc8530.cpp:277-279`), and `rxPushByte` recomputes the same CRC (`:198-203`), so the guest accepts a mutilated frame as CRC-good.
- **Trigger:** Any peer or malformed packet >704 bytes on `239.192.76.84:1954`. This contradicts the stated contract at `src/Scc8530.h:78-80` ("a truncated transport datagram would look the same" as `badFcs`) — it cannot, because the FCS is regenerated from the truncation. No LLAP length check exists in `injectRxFrame` either.
- **Fix:** `ssize_t n = ::recv(fd_, pkt, sizeof pkt, MSG_TRUNC); if (n > ssize_t(sizeof pkt)) continue;` (or compare `n == sizeof pkt` and drop), plus reject `n > 603` payloads in `Scc8530::injectRxFrame`.
