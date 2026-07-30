### medium — FPWrite Offset is decoded unsigned, so a from-EOF write with a negative offset seeks ~4 GB forward; the new 16 MB clamp guards only the resource fork
- **Where:** `/home/gistarcade/src/POM68K/src/AfpServer.cpp:1015` (decode), `:1039` (data fork, unclamped), `:1048`-`1049` (resource fork, clamped)
- **Defect:** AFP defines FPWrite's `Offset` as a *signed* long, but it is read as `uint32_t offW = rd32(c + 4)` and added to a `uint64_t`, so a legal from-EOF offset of `-N` becomes `+4294967296-N`; the guard this diff added (`at > kMaxRsrcFork || data.size() > kMaxRsrcFork - at`) sits inside the `else` (resource) branch only, leaving `f.seekp(std::streamoff(at)); f.write(...)` on the data fork completely unbounded.
- **Trigger:** guest issues `PBWrite` with `ioPosMode = fsFromLEOF`, `ioPosOffset = -512` (rewrite the tail of an open data fork). The client sends FPWrite with StartEndFlag set and Offset `$FFFFFE00`. Line 1039 computes `at = eof + 4294966784`, `seekp` succeeds, and the 512 bytes land ~4 GB into the file: the shared file balloons to a ~4 GB sparse file whose AFP data-fork length is now reported as 4 GB, the intended bytes are never overwritten, and the ok-reply carries a wrapped `uint32_t(at + data.size())`. The identical request on a *resource* fork now takes the opposite wrong turn — line 1049 rejects it with `kErrMisc`, so a legal AFP operation fails outright and the two forks disagree about what the same request means. A hostile/buggy client can also reach line 1039 with StartEndFlag clear and Offset `$FFFFFF00`, i.e. exactly the input the new comment at `AfpServer.cpp:44-48` says it is defending against.
- **Fix:** normalise once, signed, before the branch, and bound both forks:
```cpp
const int64_t offS = int64_t(int32_t(rd32(c + 4)));   // line 1015
...
int64_t at = fromEof ? int64_t(base) + offS : offS;    // base = eof or m.rsrc.size()
if (at < 0 || uint64_t(at) + data.size() > kMaxRsrcFork) { aspReply(t, kErrParam, {}); return; }
```
(keep `kMaxRsrcFork` for the resource fork; for the data fork any equivalent sanity ceiling plus the `at < 0` test is enough).

### low — MacIP UDP replies above 2048 bytes are silently truncated and then re-lengthed/re-checksummed, which is exactly what the comment added in this hunk forbids
- **Where:** `/home/gistarcade/src/POM68K/src/MacIpGateway.cpp:589` (`uint8_t buf[2048]`), `:592` (`recv` without `MSG_TRUNC`), `:610`/`:613` (length + checksum over the truncated payload)
- **Defect:** the hunk removed the 1400-byte clip with the rationale "Do NOT clip: rewriting the UDP length + checksum to match a truncated payload hands the guest a *valid-looking* short reply it cannot detect", but the recv buffer still caps the datagram at 2048 and the kernel discards the tail — the boundary moved, the contract violation did not.
- **Trigger:** any host→guest UDP datagram over 2048 bytes (EDNS0 DNS response, NFS/UDP read reply, SNMP bulk). `recv` returns 2048, the gateway builds `pkt(28 + 2048)`, writes `wr16(ip + 24, uint16_t(8 + plen))` and `l4sum(...)` over the truncated bytes, and fragments a structurally perfect short datagram to MacTCP/OT, which has no way to detect the loss.
- **Fix:** detect truncation instead of laundering it — on Linux `SOCK_DGRAM` `recv` with `MSG_TRUNC` returns the true datagram length:
```cpp
while ((r = ::recv(it->fd, buf, sizeof buf, MSG_TRUNC)) > 0) {
    if (size_t(r) > sizeof buf) { it->lastAct = now; continue; }   // drop, don't lie
```
or size the buffer to the real UDP ceiling (heap/member `uint8_t buf[65535]`, not stack).

### low — unsigned underflow in the freeze probe's disassembly window silently suppresses the dump for a low-address wedge
- **Where:** `/home/gistarcade/src/POM68K/src/main.cpp:2742` (`lo = (lo - 24) & ~1u;`), same wrap at `:2728` (`top[0].first > seen - 64`)
- **Defect:** `lo`/`seen` are `uint32_t` and the subtractions are modular with no lower-bound clamp, so a spin PC below `$18` makes `lo` wrap to ~`$FFFFFFE8` and the loop at `:2746` (`a < hi + 24`) never executes.
- **Trigger:** guest wedges spinning at, say, PC = `$00000010` (wild jump into the vector/low-memory area). The probe prints `[freeze] spin loop at $00000010 — disassembly:` followed immediately by the register dump with zero disassembly lines, and `probeDumped_.push_back(top[0].first)` at `:2739` means the window is never retried. The `:2728` neighbourhood test wraps the same way and can never suppress a redump for `seen < 64`.
- **Fix:** clamp instead of wrapping, and compare in signed 64-bit:
```cpp
lo = (lo > 24 ? lo - 24 : 0) & ~1u;
...
int64_t d = int64_t(top[0].first) - int64_t(seen);
if (d > -64 && d < 64) fresh = false;
```
