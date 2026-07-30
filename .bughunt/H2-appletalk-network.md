# H2-appletalk-network — verified findings

### HIGH — ZIP GetNetInfo: zone name read at offset 1 instead of 7; empty zone treated as valid
- **Where:** `src/AtalkStack.cpp:271-275` (`handleZipDdp`)
- **Defect:** The request is parsed as `[func][zonelen][zone]`, but the wire format is `[func][zh_zero][firstnet16][lastnet16][zonelen][zone]`, so `p[1]` is always the mandatory zero byte, `zl` is always 0, the requested zone is always `""`, and `valid` is therefore always true.
- **Trigger:** Guest sends a real GetNetInfo `05 00 0000 0000 06 "POM68K"`. `p[1]==0 <= 32` → `zl=0, zoff=2`, `reqZone=""`, `valid=true`. Reply carries flags `$60` (no ZoneInvalid), echoes a zero-length zone and omits the trailing default-zone pascal string. So (a) a guest with cleared PRAM that deliberately sends a NULL zone to *learn* the default zone (netatalk `etc/atalkd/zip.c` "Set our requesting zone to NULL, so the response will contain the default zone") gets nothing back; (b) a guest holding a stale zone from another net is told it is still valid, keeps it, and every NBP BrRq it then emits is dropped by `AtalkStack.cpp:352` (`!ieq(zone, zone_) return`) — the silent empty-Chooser failure. Both oracles put the length at offset 6: `extern/tashrouter/.../zip/responding.py:120-122` (`data[7:7+data[6]]`, and it *starts* with `ZONE_INVALID` set) and `extern/netatalk2/etc/atalkd/zip.c:494-513` (skip `ziphdr`, then two `unsigned short`, then `zlen = *data++`). The gate `tests/atalk_stack_test.cpp:51-52` builds the request in the same wrong layout, so it cannot catch this.
- **Fix:**
```cpp
if (n < 7 || p[0] != 5) return;
if (p[1] || p[2] || p[3] || p[4] || p[5]) return;   // zh_zero + net range 0
size_t zl = p[6];
if (zl > 32 || 7 + zl > n) return;
std::string reqZone(reinterpret_cast<const char*>(p + 7), zl);
bool valid = !reqZone.empty() && (reqZone == "*" || ieq(reqZone, zone_));
```
(then `if (!valid) pstr(r, zone_);` actually fires). Update the gate to emit the 7-byte header.

### HIGH — Deferred ATP transactions are never expired; PAP abandons them, permanently poisoning that (client, TID)
- **Where:** `src/AtalkStack.cpp:467` (drop path) + `src/PapServer.cpp:129` (the abandoning storer)
- **Defect:** `pendingTxns_` has no age sweep and is erased only from `AtpTxn::respond()` (`AtalkStack.cpp:417`); any handler that stores the `shared_ptr` and later drops it without responding leaves a dead entry that silently discards every future TReq with the same `(net,node,sock,tid)` key.
- **Trigger:** PAP `kRead` stores the txn (`pendingClientRead_ = t;`, PapServer.cpp:129) and `flushClientRead()` defers it (`if (answers_.empty() && !eofSeen_) return;`, PapServer.cpp:217). `handleAtp`'s cleanup `if (!txn->done_ && pendingTxns_.count(key) && txn.use_count() <= 2)` (AtalkStack.cpp:487) does not fire — use_count is 3. The connection then dies via any of `setEnabled(false)` (PapServer.cpp:54), the 120 s conn timer (:72), `kOpen` re-init (:114), `kClose` (:147), or the `!ok` "client vanished" branch (:171-175, which does not even reset the field). None calls `respond()`. The entry lives for the process lifetime, leaking the `req` buffer; and `tick()` sweeps only `xoCache_` (AtalkStack.cpp:182-183) while the XO cache *does* carry a release timer (:414-415). When the guest's 16-bit TID counter later reuses that value from the same socket, the fresh legitimate TReq hits `if (pendingTxns_.count(key)) { stats_.atpDupPending++; ... return; }` and is answered with silence until the guest's own retry limit — a permanently stuck TID.
- **Fix:** two parts. (a) Make every drop terminal in PapServer — a helper `if (pendingClientRead_) { pendingClientRead_->respond({{ connId_, kData, 1, 0 }}); pendingClientRead_.reset(); }` called from :54, :72, :114, :147 and the `!ok` branch. (b) Give `pendingTxns_` the same release timer `xoCache_` has: store an `expires` stamp and sweep it in `AtalkStack::tick()` next to the `xoCache_` sweep. Also erase `pendingTxns_[key]` from `case kAtpTRel:` (:529).

### HIGH — FPMoveAndRename silently orphans the `.AppleDouble` sidecar (resource fork + Finder info)
- **Where:** `src/AfpServer.cpp:821`
- **Defect:** The sidecar rename targets `<dstdir>/.AppleDouble/<leaf>` without creating that directory, and the `std::error_code` is discarded, so a cross-directory move that fails reports success while losing the fork.
- **Trigger:** `/share/App` has a resource fork → `/share/.AppleDouble/App` exists. Guest does FPCreateDir "New" (nothing creates `.AppleDouble` there — only `adWrite` does, `AfpServer.cpp:120-122`). Finder drags App into New: cmd 23 renames the data file (line 819, OK), then `fs::rename("/share/.AppleDouble/App", "/share/New/.AppleDouble/App", ec)` returns ENOENT (missing parent). `ec` is never inspected → `ok()`. Next `statNode("/share/New/App")` → `adRead` finds nothing → `hasAd=false` → `packParams` reports RFLEN 0 and an all-zero 32-byte FinderInfo. The app is now typeless/creatorless with an empty resource fork and will not launch. `FPRename` (line 788) makes the same call but is harmless (parent unchanged). Contradicts `AfpServer.h:14-18` and the care taken in FPDelete (`AfpServer.cpp:750-760`) to avoid exactly this loss. `tests/afp_server_test.cpp` never exercises cmd 23.
- **Fix:**
```cpp
const std::string sAd = adPath(dir_ + "/" + src), dAd = adPath(dir_ + "/" + dst);
std::error_code ec2;
if (fs::exists(sAd, ec2)) {
    fs::create_directories(fs::path(dAd).parent_path(), ec2);
    fs::rename(sAd, dAd, ec2);
    if (ec2) { fs::copy_file(sAd, dAd, fs::copy_options::overwrite_existing, ec2);
               if (!ec2) fs::remove(sAd, ec2); }
}
```

### MEDIUM — `adRead()` slurps a sidecar of unbounded size; the data-fork write path has no offset clamp
- **Where:** `src/AfpServer.cpp:97` (slurp) and `src/AfpServer.cpp:1041-1043` (unclamped write)
- **Defect:** `adRead` builds `std::vector<uint8_t> b` from the whole sidecar stream before any validation, and the guest can make that sidecar arbitrarily large because the data-fork `FPWrite` branch does `f.seekp(std::streamoff(at)); f.write(...)` with no ceiling while `resolvePath` accepts a `.AppleDouble` path element.
- **Trigger:** `resolvePath` (`AfpServer.cpp:310-313`) rejects only `"."`, `".."` and empty, and `macToUnix` only swaps `/`↔`:`, so FPCreateFile with `".AppleDouble\0a.txt"` yields `rel = ".AppleDouble/a.txt"`. FPOpenFork + FPWrite `fromEof=0, offset=0xF0000000` creates a ~4 GB sparse `/share/.AppleDouble/a.txt` (the resource branch clamps at `AfpServer.cpp:1049-1053`; the data branch does not). The next FPEnumerate of the root calls `statNode("/share/a.txt")` → `adRead` → line 97 allocates 4 GB. POM68K installs no `bad_alloc` handler → `std::terminate`. This is verbatim the hazard the same file already documents for the resource fork (`AfpServer.cpp:44-49`, `kMaxRsrcFork`). `adRead` is also called once per entry from `statNode` (:433), so even a legitimately large sidecar turns one FPEnumerate into hundreds of MB of I/O.
- **Fix:** stat before allocating —
```cpp
std::error_code ec; auto sz = fs::file_size(adPath(host), ec);
if (ec || sz > kMaxRsrcFork + 4096) return false;
```
plus clamp the data-fork `at` the way the resource fork is clamped, and reject a leading `'.'` on every element in `resolvePath`.

### MEDIUM — MacIP parses non-first IP fragments as fresh TCP/UDP segments
- **Where:** `src/MacIpGateway.cpp:279-283` (`handleIp`)
- **Defect:** `handleIp` validates version, IHL and total length but never reads the flags/fragment word at `p+6` before dispatching on the protocol byte, so a fragment with a non-zero offset has its payload bytes decoded as an L4 header.
- **Trigger:** Guest sends a 1024-byte UDP datagram; MacTCP's MacIP MTU is 586, so it emits `[IP|UDP|558 B]` (MF=1) and `[IP|offset=70|466 B]`. Both reach `handleUdpFromGuest`, which for the second does `gPort = rd16(p+ihl), rPort = rd16(p+ihl+2)`, matches no flow, opens a *new* host socket `connect()`ed to a garbage port and `send()`s the user's payload there — while the real peer sees only a truncated 558-byte datagram. Each such fragment leaks a socket until the 120 s reap. Same mis-parse for proto 6 fabricates a TCP segment with arbitrary flags/seq. The file's own tracer already computes and tests exactly this field ten lines away (`MacIpGateway.cpp:79`, `:89`: `!(frag & 0x1FFF)`), so the data path is inconsistent with the diagnostic path in the same TU. The macipgw oracle avoids it structurally by letting the kernel reassemble on a tun device.
- **Fix:** after the `totLen` clamp add
```cpp
const uint16_t frag = rd16(p + 6);
if (frag & 0x1FFF) return;              // non-first fragment: no L4 header
```
(and ideally drop `frag & 0x2000` first-fragments too, since there is no reassembly buffer).

### MEDIUM — Oversized host UDP replies are kernel-truncated, then re-length'd and re-checksummed
- **Where:** `src/MacIpGateway.cpp:589-613` (`tick`)
- **Defect:** The host-side read uses a fixed `uint8_t buf[2048]` with plain `recv()` and no `MSG_TRUNC`, so a larger datagram is clipped by the kernel; the code then writes a UDP length (line 610) and pseudo-header checksum (line 613) computed over the *truncated* payload.
- **Trigger:** Any host UDP service answering the guest with more than 2048 bytes (large DNS, TFTP, SunRPC). `plen` becomes 2048, the guest's MacTCP/OpenTransport validates the checksum successfully and delivers a silently truncated datagram to the application. This is exactly what the comment four lines above forbids: *"rewriting the UDP length + checksum to match a truncated payload hands the guest a valid-looking short reply it cannot detect."*
- **Fix:** `uint8_t buf[65536];` (a UDP payload tops out at 65507), or read with `MSG_TRUNC` and drop when `r > sizeof buf` rather than forwarding a re-checksummed truncation.

### LOW — Over-31-byte names are truncated on the way out with no reverse mapping
- **Where:** `src/AfpServer.cpp:490` (via `pstr`'s default `maxLen = 31`, `:61-65`)
- **Defect:** `packParams` emits the host filename through a lossy one-way truncation, while `resolvePath` rebuilds the host path verbatim from the name the client sends back, so any entry longer than 31 bytes is advertised under a name no subsequent lookup can resolve.
- **Trigger:** `Mac OS 8.1 Installer Disk Image.img` (35 B) in the share. FPEnumerate shows `Mac OS 8.1 Installer Disk Imag`; the user double-clicks, FPOpenFork resolves `rel = "Mac OS 8.1 Installer Disk Imag"`, `statNode` stats a nonexistent path and the server returns `kErrNoObj` on a file the Finder is displaying. Two entries sharing a 31-byte prefix enumerate as one duplicate name and neither resolves; FPCreateFile on such a name silently makes a *second* truncated file. netatalk mangles to a reversible form for this reason; not listed in TODO.md or docs/LLE_VS_HLE.md.
- **Fix:** emit a deterministic mangled name (netatalk style: first 26 bytes + `#` + 4 hex of a hash of the full name) and keep a `mangled → real` map consulted by `resolvePath`. Cheap interim: skip over-long entries in FPEnumerate so the Finder never shows an unopenable file.

### LOW — ASP session IDs come from a wrapping counter with no in-use check
- **Where:** `src/AfpServer.cpp:334`, `:341`
- **Defect:** `uint8_t sid = nextSid_++;` then `sessions_[sid] = std::move(s);` — no test that the id is free, so the 256th concurrent OpenSession destroys a live session.
- **Trigger:** Any node on the segment (the guest, or an external LToUDP peer once `POM68K_LTOUDP=1`) sends 255 OpenSessions on socket 129 inside one quantum — faster than the 120 s reaper at `AfpServer.cpp:201`. `sessions_[1]` is overwritten, taking the real client's `volOpen` flag and its whole `forks` map with it; its next FPRead fails the net/node check at `:383` with aspSessClosed (-1072) mid-copy and its open refnums are unrecoverable. Defeats the invariant documented at `AfpServer.cpp:378-386` ("without the address check, any node on the segment … drives someone else's mounted volume"). netatalk's `asp_getsess.c` picks an id that is not already allocated.
- **Fix:**
```cpp
uint8_t sid = 0;
for (int i = 0; i < 255; i++) { uint8_t cand = nextSid_++; if (!nextSid_) nextSid_ = 1;
                                if (!sessions_.count(cand)) { sid = cand; break; } }
if (!sid) { t->respond({ { 0, 0, 0xFB, 0xF0 } }); return; }   // aspTooMany
```

### LOW — MacIP leases are allocated but never reclaimed; `Lease::lastSeen` is dead
- **Where:** `src/MacIpGateway.cpp:164` (allocation), `MacIpGateway.h:65` (field)
- **Defect:** `lastSeen` is written at `:160` and `:168` and read nowhere; `leases_.erase` appears nowhere, so the ~253-address pool can only shrink over the process lifetime.
- **Trigger:** Every distinct node asking for MACIP_ASSIGN, plus every in-subnet source learned from traffic (`leases_[sip] = { src, st_.now() };`, `:262`), plus every external LToUDP peer, permanently consumes one address. Once exhausted `leaseFor` returns 0 and MacTCP can never configure again for the session; only restarting POM68K or toggling the service (`leases_.clear()`, `:141`) recovers. The cited oracle does reclaim — `extern/macipgw/macip.c:604-624` probes then frees on `ARPTIMEOUT`.
- **Fix:** add a lease sweep to `MacIpGateway::tick()` alongside the existing UDP-flow reap, erasing entries older than a lease lifetime, and refresh `lastSeen` on the traffic-learning path at `:262` as well.

### LOW — LLAP address-defence ACK cannot use the express path its comment promises
- **Where:** `src/AtalkStack.cpp:90-96`
- **Defect:** The lapENQ defence comment states *"the express path keeps the ACK inside the prober's window"*, but the only production binding of `sendFrame` (`AtalkHub.h:71-82`) merely appends to `pending_`, and the flush at `AtalkHub.h:119-121` calls `scc.injectRxFrame(0, d, n, false)` — non-express, one tick later, then further delayed by LLAP's 400 µs inter-dialog gap in `Scc8530`.
- **Trigger:** A guest probing node 128 (the internal node's id) times out before the ACK arrives and takes the address. Both nodes then answer to 128: `onGuestFrame`'s `if (src != node_)` guard (`AtalkStack.cpp:102`) stops recording the guest, and every DDP the stack sends to 128 is also the guest's own address. The `enqSeen` statistic advertises a defence the wiring cannot deliver. The mechanism exists — `main.cpp:171` uses `express=true` for the CTS synth — it is simply not applied here. Note the hub's deferral is deliberate and correct for *reply* frames (non-express injection during `onTxFrame` is dropped, Scc8530.cpp:261); the bug is that a control frame with a hard deadline shares that path.
- **Fix:** give `AtalkStack` a separate `sendControlFrame` hook wired straight to `scc.injectRxFrame(0, d, n, /*express=*/true)`, bypassing `pending_`, mirroring the lapRTS→lapCTS synth; or, at minimum, correct the comment so it stops claiming a guarantee the wiring does not provide.
