### critical — DiskCopy 4.2 `dataSize` guard wraps in 32-bit: unbounded heap over-read on any crafted/corrupt `.image`
- **Where:** `src/SonyDrive.cpp:129` (use at `:131`)
- **Defect:** `raw.size() < 0x54 + dataSize` is evaluated in `unsigned int` (`0x54` is `int`, `dataSize` is `uint32_t`), so a `dataSize` ≥ `0xFFFFFFAC` wraps past the guard, while the very next line widens the *unwrapped* value to `ptrdiff_t` when forming the `assign()` range.
- **Trigger:** Any file > 0x54 bytes with `01 00` at 0x52/0x53 and big-endian `0xFFFFFFFF` at 0x40 (truncated/fuzzed/untrusted `.image`). `0x54 + 0xFFFFFFFF` == `0x53`, guard passes; `raw.assign(raw.begin()+0x54, raw.begin()+0x54+dataSize)` then copies 4294967295 bytes from the heap. Reachable from every insert path: CLI media args and `mem.insertDisk()` (`V8Memory.h:140`, `Q605Memory.h:88`, `SonoraMemory.h:108`, `CentrisMemory.h:97`, `RbvMemory.h:104`, `Q630Memory.h:97`, `MacMemory.h:96`). `insertImage()`'s exact-size whitelist (`:141`) only runs *after* the copy. Every other size decision in this file is done in 64-bit — this line is inconsistent with its own file.
- **Fix:**
```cpp
if (dataSize > kSize1440K || raw.size() - 0x54 < size_t(dataSize)) return false;
header.assign(raw.begin(), raw.begin() + 0x54);
raw.assign(raw.begin() + 0x54, raw.begin() + 0x54 + size_t(dataSize));
```

### high — `encodeTrackGcr()` reads up to 4 KB past `image_` on side 1 of a 400K single-sided disk
- **Where:** `src/SonyDrive.cpp:288`
- **Defect:** The GCR encoder dereferences `&image_[imageOffset(track_, side1_?1:0, sector)]` with no bounds test, while `imageOffset()` (`:170-175`) adds a whole side of sectors on a single-sided image, so track 79 / side 1 lands exactly at and past `image_.size()`.
- **Trigger:** Insert a 409600-byte image (`insertImage` accepts `kSize400K` and sets `doubleSided_ = false`, `:141-148`). Seek to track 79 (HFS alternate MDB lives there, so every mount goes there) and let the IWM stream a nibble with SEL high — `MacMemory.cpp:180` drives `iwm_.setSel((via_.portA() & 0x20) != 0)` from guest VIA PA5, and every odd IWM sense address (CSTIN/WRTPRT/TK0/TACH, `Iwm.cpp:23`) needs SEL=1, so `Iwm.cpp:144 nextNibble(sel_)` → `nextByte` → `selectSide(true)` (`:177`, no `doubleSided_` guard) → `encodeTrackGcr()`. `imageOffset(79,1,s)` = 405504 + 8·512 + s·512 = 409600 + s·512 ≥ `image_.size()`; the loop then reads 512 bytes there for each of the 8 sectors. On tracks 0-78 the same line silently encodes the *next* track's data (fidelity bug on the same expression). The write path already guards this geometry (`writeSector`, `:636`: `side > (doubleSided_ ? 1 : 0)`), and `readSector` (`:693`) checks `off + 512 > image_.size()` — only the encoder is unguarded. No gate covers 400K media.
- **Fix:** clamp the head to the media in `selectSide()` — a single-sided mechanism has no side-1 head:
```cpp
void SonyDrive::selectSide(bool side1) {
    side1 = side1 && doubleSided_;
    if (side1 == side1_) return; ...
```
plus a per-sector `if (off + 512 > image_.size()) continue;` in `encodeTrackGcr()`/`encodeTrackMfm()`.

### high — DiskCopy 4.2 write-back produces a malformed file: tags dropped, `tagSize`/`tagChecksum` left intact
- **Where:** `src/SonyDrive.cpp:666-674` (tags stripped at `:130-131`)
- **Defect:** `insert()` keeps only `dataSize` bytes (discarding the DC42 tag block), but `flushToFile()` re-emits the original 0x54-byte header refreshing *only* the data checksum at +$48, so the persisted file still declares a non-zero `tagSize` (+$44) and `tagChecksum` (+$4C) with no bytes behind them.
- **Trigger:** Write-back is on by default (`main.cpp:907` etc., `setWriteBack(getenv("POM68K_FLOPPY_RO") == nullptr)`). Mount a real Apple-produced 800K `.image` with tags (`tagSize` = 19200), let the guest write one sector, eject: `flushToFile()` `rename()`s over the original a 819284-byte file claiming 19200 bytes of tags past EOF. The original tags are gone irrecoverably and Disk Copy / MAME's dc42 loader / Mini vMac / Basilisk fail the tag checksum or read past EOF. `tests/floppy_persist_test.cpp:79-84` only builds a `tagSize == 0` header, so the gate never sees it; `SonyDrive.h:86-88` promises a regenerated header the code does not deliver.
- **Fix:** zero the fields the writer can no longer honour, alongside the +$48 checksum:
```cpp
for (int i = 0x44; i < 0x48; i++) dc42Header_[i] = 0;   // tagSize   = 0
for (int i = 0x4C; i < 0x50; i++) dc42Header_[i] = 0;   // tagChecksum = 0
```
(fuller fix: stash the stripped tag block at insert and write it back).

### medium — Inserting a floppy over a dirty one silently discards every committed sector
- **Where:** `src/SonyDrive.cpp:144` (in `insertImage`, reached from `insert()` `:133`)
- **Defect:** `insertImage()` does `path_.clear(); dc42Header_.clear(); dirty_ = false; image_ = std::move(data);` without calling `flushToFile()`, so the outgoing media's pending write-back is dropped with its path.
- **Trigger:** GUI, write-back default on. Guest writes to floppy A (`writeSector` sets `dirty_`, nothing hits the host file yet). User picks floppy B directly from Machine → Floppy: `main.cpp:3151-3153` calls `requestInsertFloppy(d)` with **no** preceding eject (the "Éjecter" item at `:3143` is a separate menu entry), → `applyCmds` → `mem.insertDisk()` → `insert()` → `insertImage()`. All of floppy A's writes are lost, no diagnostic. `eject()` (`:101-103`) does honour the contract; insert-over-insert does not.
- **Fix:** flush the outgoing media first — `if (hasDisk()) flushToFile();` as the first statement of `SonyDrive::insert()` (before the new file is read).

### medium — `floppyPending_` is written under `cmdMu_` by the UI thread but read unlocked by the emulation thread
- **Where:** `src/main.cpp:2924` (producer at `:2588-2592`; same shape duplicated at the other machine shells)
- **Defect:** `applyCmds()` holds `cmdMu_` only for `cmdsApply_.swap(cmds_)` (`:2914`), then reads/clears the shared `std::string floppyPending_` unlocked while the UI thread may reassign it under the lock — a data race that can free the buffer being read.
- **Trigger:** User picks a second image from Machine → Floppy while the emu thread is inside `mem.insertDisk(floppyPending_)`: the UI thread's `floppyPending_ = std::move(path)` reallocates/frees the heap buffer `insert()` is reading as `const std::string&` → use-after-free or a torn path (opens a garbage file). Mirror case: `floppyPending_.clear()` at `:2926` races the same assignment. Every other cross-thread datum here is either inside the swapped `Cmd` vector or an `std::atomic`.
- **Fix:** move the string out under the lock:
```cpp
std::string pending;
{ std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); pending.swap(floppyPending_); }
```
(or carry the path in `Cmd`).

### medium — `CI_PAD` moves no bytes, and undrained DATA-IN residue then shadows the STATUS byte latched by `CI_COMPLETE`
- **Where:** `src/Ncr53c96.cpp:397-399` (shadowing at `:253-262`)
- **Defect:** Transfer Pad is implemented as a bare `raiseIrq(I_BUS)` — no `tcounter_` decrement, no `dataInPos_` advance, no `advanceToStatus()` — while MAME's `CI_PAD` (`~/src/refs/mame-apple/ncr53c90.cpp:1031-1040`) enters `INIT_XFR_RECV_PAD_WAIT_REQ` and actually clocks bytes off the bus until `S_TC0`; the file header claims to follow that model.
- **Trigger:** A driver discards a short-read remainder with `CI_PAD` ($18/$98). POM68K leaves `dataInPos_ < dataIn_.size()` and `phase_ == DATA_IN`; the driver then issues `CI_COMPLETE`, which does `fifoPos_ = 0; fifoPush(targetStatus_); fifoPush(0x00);` **without clearing `dataIn_`**, and the next `read(R_FIFO)` takes the `dataInPos_ < dataIn_.size()` branch (keyed on residue, not phase) and returns stale block data as the SCSI status — an arbitrary byte instead of 0x00 GOOD.
- **Fix:** make `CI_PAD` discard for real — `size_t n = std::min<size_t>(tcounter_ ? tcounter_ : 1, dataIn_.size() - dataInPos_); dataInPos_ += n; tcounter_ -= n; if (dataInPos_ >= dataIn_.size()) { status_ |= S_TC0; advanceToStatus(); }` before `raiseIrq(I_BUS)` — and have `CI_COMPLETE` drop residue (`dataIn_.clear(); dataInPos_ = 0; dataXfer_ = false;`) so the latched STATUS/MSG bytes are what `R_FIFO` returns.

### medium — `CD_SELECT_ATN_STOP` is treated as plain `CD_SELECT_ATN`: the whole CDB is sent and the command executes
- **Where:** `src/Ncr53c96.cpp:359-360`
- **Defect:** Select-with-ATN-and-stop must halt after the single MSG-OUT (IDENTIFY) byte with `seq_step = 2`, leaving the CDB in the FIFO for a later Transfer Information; POM68K routes it into the same `selectTarget(true)` that drains the FIFO as the CDB, calls `runTarget()` and reports `seq_step = 4`. MAME distinguishes them (`ncr53c90.cpp:535-542`: `if (c == CD_SELECT_ATN_STOP) { seq = 2; function_bus_complete(); }`).
- **Trigger:** A driver negotiating synchronous transfer pushes IDENTIFY + CDB, issues $43 intending to inject an SDTR message with a MSG-OUT `CI_XFER` before the CDB goes out. The command executes *before* the negotiation, `seq_step` reads 4 instead of 2, and the follow-up MSG-OUT `CI_XFER` lands in `DATA_IN`/`DATA_OUT`, where `transferInfo()` gathers the SDTR bytes into the write payload.
- **Fix:** split the case — pop only the IDENTIFY byte, leave the rest of the FIFO, set `phase_ = COMMAND; seq_ = 2; selCdbWait_ = true;` and raise `I_BUS|I_FUNCTION` without calling `runTarget()`; the CDB then arrives through the existing `fifoPush`/`transferInfo` COMMAND path.

### low — `runTarget()`'s WRITE branch leaves the previous transaction's DATA-IN buffer live
- **Where:** `src/Ncr53c96.cpp:483-486`
- **Defect:** `if (wbytes > 0) { phase_ = DATA_OUT; dataOut_.clear(); dataOutExpected_ = wbytes; return; }` returns before `dataIn_.clear(); dataInPos_ = 0;` (`:487`) and never clears `dataXfer_`, so stale read payload survives into the next command — and `read(R_FIFO)` (`:253`), `read(R_FLAGS)` (`:296-298`) and `dmaRead()` (`:632`) all key on `dataInPos_ < dataIn_.size()`, not on the phase.
- **Trigger:** A READ whose payload is not fully drained (short read, abort, or the `CI_PAD` path above) followed by a WRITE(10): `R_FLAGS` reports `min(remaining, 16)` instead of 0 during the write, and any `R_FIFO`/`dmaRead` returns stale block data, which then also shadows the STATUS byte at `CI_COMPLETE`.
- **Fix:** hoist the reset to the top of `runTarget()`, before the `writeByteCount()` branch: `dataIn_.clear(); dataInPos_ = 0; dataXfer_ = false;`

### low — Empty CDB reaches `ScsiDisk::command()`, which dereferences `cdb[0]` and ignores `cdbLen`
- **Where:** `src/Ncr5380.cpp:138` (and `:295`), landing in `src/ScsiDisk.cpp:236`
- **Defect:** `enterCommand()` leaves `cmd_` empty with `cmdLen_ = 0` (`:59`), and `ackFalling()` fires `execute()` on `0 >= 0`; `execute()` passes `cmd_.data()` (nullptr for a never-grown vector) and size 0 to `ScsiDisk::command()`, whose first statement is `switch (cdb[0])` — the function discards its `cdbLen` parameter entirely and also indexes `cdb[4]`/`cdb[8]` unconditionally.
- **Trigger:** Three guest register writes: ICR = `ICR_ACK` while BUS_FREE (no edge — `targetPhase()` is false), then ICR = `ICR_SEL|ICR_ACK` (→ `trySelect()` → `enterCommand()`, ACK bit unchanged so no rising edge), then ICR = `ICR_SEL` → `dif & ~v & ICR_ACK` → `ackFalling()` with `cmd_` empty → `execute()` → null-pointer dereference, i.e. an emulator crash from pure guest I/O.
- **Fix:** `if (cmdLen_ && int(cmd_.size()) >= cmdLen_) execute();` in both `ackFalling()` and `dmaWrite()`, and make `ScsiDisk::command()` honour `cdbLen` — return `kCheck`/ILLEGAL REQUEST when `cdbLen` is shorter than the group length implied by `cdb[0]`.
