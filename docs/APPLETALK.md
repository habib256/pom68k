# AppleTalk, LocalTalk, printing, and the bridge to CUPS

Index for POM68K's networking: the protocol facts, each mapped to the
class and gate that implement it — plus the short operating guide for
someone who just wants a shared folder.

**Since 2026-07-24 POM68K carries the whole service side in-process**
(`AtalkHub` → `AtalkStack` + `AfpServer` + `PapServer` + `MacIpGateway`),
on by default in the GUI, no root and no external daemons. The netatalk /
TashRouter / macipgw chain is still here (§6.1-§6.4) and remains the way
onto a **real** LocalTalk network; the two coexist on the same wire.

| You want to… | Go to |
|---|---|
| share a folder, print, put the guest on the web | **§0 Operating it** |
| debug LLAP framing, ENQ, RTS/CTS, the IDG | §2 |
| debug DDP/RTMP/ZIP/NBP/ATP (empty Chooser, retransmits) | §3 |
| debug a mount that drops, or a print job | §4 |
| reach a real LocalTalk network / real netatalk | §6.1-§6.4 |
| know what the in-process subset does **not** do | §6.5 |
| find the file or gate for a layer | §7 |
| look up a constant | §8 |

Protocol facts are cross-checked against *Inside AppleTalk* (2nd ed.,
Sidhu/Andrews/Oppenheimer) and the vendored **netatalk 2.4.9**
(`extern/netatalk2/`, `file:line` verified 2026-07-22). Full source list
in §9. Section numbers §2.4, §3.3, §4.4, §6.4 and §6.5 are cited from
code comments and other docs — **do not renumber them.**

---

## 0. Operating it

### 0.1 Knobs

`DEV.md` §5 is the complete environment-variable list; only the ones that
change AppleTalk behaviour are repeated here.

| Knob | Default | Effect |
|---|---|---|
| `POM68K_APPLETALK=0` | (unset = on) | kills the in-process stack; the **Réseau → AppleTalk** menu item greys out (`src/main.cpp:94`) |
| `POM68K_APPLETALK=1` | — | *different job*: seeds PRAM SPConfig `$21` = LocalTalk **active at boot** (`src/Egret.cpp:77`, `src/Rtc.cpp:44`). Unset seeds `$22` (async) — a fresh PRAM then needs the Chooser's AppleTalk radio button, or an image whose prefs already have it on |
| `POM68K_SHARE_DIR=/path` | `<repo>/AppleShare`, created if absent (`src/main.cpp:147`) | host folder served as the AFP volume. **The volume takes the folder's own name**, netatalk-style (`AtalkHub.h:93`) |
| `POM68K_ATALK_WIRE_BOOST=N` | `8` | virtual-wire speed-up (`src/main.cpp:128-138`); `=1` restores authentic 230.4 kbit/s. See §0.4 |
| `POM68K_LTOUDP=1` | off | also join the real LToUDP cable (§6.1). Suppresses the boost — external peers are real wires |
| `POM68K_ATALK_DEBUG=1` | off | DDP/NBP/ATP tracer + one line per client retransmit with its lag (`src/AtalkStack.cpp:16`) |
| `POM68K_MACIP_DEBUG=1` | off | every IP datagram both ways, with TCP flags/seq/ack |
| `POM68K_DAYNAPORT=<id>` | off | put a DaynaPort SCSI/Link (Ethernet as a SCSI target) at that SCSI ID on the Quadra 605; `=1` picks the default ID 3. Its uplink is the same NAT the MacIP gateway uses — §6.4bis |

### 0.2 Guest side

Nothing to start on the host. Launch the GUI, then in the guest:

| Service | Guest steps |
|---|---|
| **File sharing** | Chooser → **AppleShare** → server *POM68K* → log in as **Guest** → the volume (named after the share folder) mounts |
| **Printing** | Chooser → **LaserWriter 8** → printer *POM68K* → a generic/plain LaserWriter PPD. The job is spooled to CUPS via `lp` when the host has it, else to a timestamped `.ps` in `run/print` (`PapServer.h:63-64`) |
| **Internet** | TCP/IP (Open Transport) or MacTCP control panel → *Connect via* **AppleTalk (MacIP)**, server zone **POM68K** — full steps and per-OS quirks in §6.4 |

The internal node is a real terminated peer at **net 2, node 128**, zone
**POM68K** (`AtalkHub.h:60`): it defends its own address against the
guest's lapENQ probes, so the guest settles on a different ID exactly as
it would against hardware.

### 0.3 The GUI window (`Réseau → AppleTalk`, `src/main.cpp:559`)

Four blocks, each with a live enable checkbox and a green/red bullet:

- **Nœud / routeur** — net/node/zone, guest node seen, frames in/out,
  NBP lookups served, ATP transactions, and the retransmission
  diagnostic (see §0.4).
- **Partage de fichiers** — registered in NBP? share folder writable?
  server/volume/host path, sessions, last user, last AFP command, bytes
  read/written.
- **Imprimante** — registered? state, completed jobs, where the last job
  went, spool target.
- **Internet (MacIP)** — gateway registered? **a lease attributed is the
  proof it works**; gateway/DNS addresses, leases, live UDP/TCP flows,
  IP datagram counters.

### 0.4 When it misbehaves

The in-process wire is **lossless by design** — `setLosslessRx(true)`
makes a full Rx FIFO *pause* the wire instead of dropping (`src/main.cpp:116`).
So it never loses a reply; it can only **delay** one. That turns the
window's counters into a diagnosis rather than a score:

| Reading | Means |
|---|---|
| retransmissions 0 | clean |
| retransmit lag ~1-2 s | the guest's own ATP timer fired — the reply played late |
| retransmit lag tens of ms | the guest gave up early / the reply was mangled |
| "dont N pendant le service" | the retransmit arrived while we were *still* serving the original — server too slow, not the wire (`AtalkStack.h:136-146`) |
| "débordement du fil" > 0 | the guest stopped listening long enough to blow the 64-frame lossless backlog (`Scc8530.h:339`) |

Lowering `POM68K_ATALK_WIRE_BOOST` is the wrong reflex for a backlog: the
cap is the guest's Rx drain rate, not the pace. Tracers: `POM68K_ATALK_DEBUG=1`,
`POM68K_MACIP_DEBUG=1`. Passive wire capture (throughput / gap / RTT
distributions, LLAP header decode): `scratchpad/ltoudp_measure.py`, and it
only sees traffic when the LToUDP cable is up.

### 0.5 Gates

`ctest -L unit` runs the first seven in seconds; the last two need ROM +
disk assets and soft-skip without them.

| Gate | Covers |
|---|---|
| `llap_loop_test` | RTS/CTS, ENQ, address filter, express CTS, carrier sense |
| `ltoudp_test` | the multicast cable |
| `atalk_stack_test` | ENQ defence, RTMP/ZIP/NBP/AEP, ATP exactly-once |
| `afp_server_test` | OpenSession→Login→OpenVol→Enumerate→Read; ASP SPWrite→WriteContinue→FPWrite; resource fork → `.AppleDouble` |
| `pap_server_test` | OpenConn→SendData→PostScript→EOF→spool; `%%?Query` answered `*` |
| `macip_gw_test` | address assign, ICMP echo, a real UDP round-trip and a full TCP SYN→data→FIN both ways through the user-mode NAT on loopback |
| `daynaport_test` | the SCSI/Link command set (READ/WRITE frame formats, the 6-byte header + more-data flag, SET MAC, the 37-byte INQUIRY) and the round trip guest → Ethernet frame → `EtherLink` → NAT → back, plus proxy-ARP refusing the guest's own address (§6.4bis) |
| `llap_two_system_etalon` | two Macs acquire node IDs over real ENQ traffic |
| `q605_ot_bind_etalon` | Open Transport's `.MPP` binds against the in-process stack (§2.5) |

---

## 1. The shape of the whole thing

AppleTalk (1983-85) is zero-config on a physical layer nobody else
wanted. Four traits explain most of the surprises:

1. **Addresses are dynamic and cheap** — a node guesses its own address
   at power-on and checks (§2.3). Names, not numbers, are the
   user-facing identifier: NBP is as fundamental here as DNS is optional
   in IP.
2. **The unit of work is the transaction, not the stream** — ATP is
   request/response with exactly-once semantics (§3.4). File sharing and
   printing are both round-trip conversations, not pipes.
3. **Everything rides one connectionless datagram, DDP** (§3.1); DDP's
   "port" is the *socket*.
4. **Zones are naming/broadcast scope, not subnets** — a human-readable
   group that can span networks, existing to keep the Chooser's lists
   manageable.

### 1.1 The layer cake, and who owns it here

| OSI layer | AppleTalk protocol(s) | In-process owner | External (§6.1) |
|---|---|---|---|
| Application / Presentation | **AFP**, print drivers | `AfpServer` | netatalk `afpd` |
| Session | **ASP**, **PAP**, **ZIP**; **ADSP** (not implemented) | `AfpServer` / `PapServer` / `AtalkStack` | netatalk / guest ROM |
| Transport | **ATP**, **NBP**, **AEP**, **RTMP** | `AtalkStack` | netatalk / TashRouter |
| Network | **DDP** | `AtalkStack` | TashRouter |
| Data link | **LLAP**; ELAP+AARP, TLAP (n/a) | **`Scc8530`** + `AtalkStack` node | `Scc8530` |
| Physical | LocalTalk RS-422 230.4 kbps | **`Scc8530` SDLC** | + `LtoUdp` cable |

Two numbering spaces trip everyone up; keep them separate.

- **DDP socket numbers** identify an endpoint on a node (like a TCP
  port). Statically assigned (SAS) 1-127, Apple reserving 1-63;
  dynamically assigned (DAS) 128-254; 0 and 255 reserved. Well-known:
  **1 RTMP, 2 NBP, 4 AEP, 6 ZIP** (`src/AtalkStack.cpp:37-40`), plus
  **72 = MacIP config** by convention.
- **DDP protocol type** is a byte *inside* the DDP header naming the
  upper protocol, independent of socket: **1 RTMP-data, 2 NBP, 3 ATP,
  4 AEP, 5 RTMP-request, 6 ZIP, 7 ADSP** (`include/atalk/ddp.h:29-35`,
  mirrored `src/AtalkStack.cpp:30-35`), **22 = IP-in-DDP (MacIP)**.

So an ATP transaction — ASP/AFP and PAP alike — is **DDP type 3**
delivered to whatever *socket* the endpoints negotiated. A full address
is **network(16) . node(8) : socket(8)**, e.g. `2.145:253`.

---

## 2. LocalTalk / LLAP — the layer POM68K really implements

This is what `Scc8530` *is*: our bytes are the real bytes, so it gets the
most detail. `AtalkStack` sits directly on it as a second node.

### 2.1 Physical: RS-422, 230.4 kbps, FM0, SDLC

- **Electrically** — RS-422 differential pair, daisy-chained, passive,
  self-terminating. No hub, ~32 nodes, ~300 m.
- **Bit rate 230.4 kbit/s** → one byte ≈ 34.7 µs. In POM68K the pace is
  **derived, never hardcoded**: `byteCycles = cpuHz / 28800`
  (`src/main.cpp:104`) — 272 cycles/byte at 7.8336 MHz (Plus), 544 at
  15.6672 (LC II), 868 at 25 MHz (Q605). Hardcoded 868s once fed a
  25 MHz clock to 15.67 and 33.33 MHz machines and skewed every
  second-scale AppleTalk timer by up to 2×.
- **The in-process wire is boosted.** A real 230 kbit/s cable makes a
  multi-MB Finder copy take minutes. With the hub up and no external
  cable, `setWirePace(byteCycles / 8)` (floor 64) plus `setLosslessRx`
  give a fast lossless virtual wire (`src/main.cpp:128-138`). What stays
  at **real** pace: the express-CTS gap, the LLAP IDG and the Tx-underrun
  grace — those are guest-code turnaround windows, not wire properties.
  Async serial is untouched (the override applies in SDLC mode only).
- **Encoding FM0** (differential Manchester), self-clocking and
  DC-balanced. The real SCC's DPLL recovers the clock; POM68K does not
  model it — whole bytes are delivered at wire pace (`docs/LLE_VS_HLE.md` §3).
- **Framing SDLC**: flag `0x7E`, zero-bit insertion after five 1s, abort
  = 7+ consecutive 1s.

### 2.2 The LLAP frame

```
 flag   dst    src    type   … data …    FCS(2)   flag  [abort]
 7E     nn     nn     tt     0..600 B     CRC-16   7E    1111…
        └─ 3-byte LLAP header ─┘
```

- 3-byte header, total frame 5-603 bytes.
- **FCS** = CRC-16/X.25 (poly `$1021` reflected, init/xorout `$FFFF`)
  over dst+src+type+data, appended **low byte first** —
  `crc16x25()` at `src/Scc8530.cpp:88`, applied in `injectRxFrame`
  (`src/Scc8530.cpp:297-299`, which can also inject a deliberately bad
  FCS to model wire damage).
- **Node IDs**: `0` invalid, **1-127 user/workstation**, **128-254
  server**, **255 broadcast**. The split lets a busier server use a
  slower, more thorough address probe.

| Type | Name | Meaning |
|---|---|---|
| `0x01` | short DDP | datagram, short header, same network |
| `0x02` | long DDP | datagram, long header, routed |
| `0x81` | **lapENQ** | node-ID enquiry ("is this address taken?") |
| `0x82` | **lapACK** | reply to ENQ ("yes, it's mine") |
| `0x84` | **lapRTS** | request-to-send |
| `0x85` | **lapCTS** | clear-to-send |

Control frames `0x81`-`0x85` are exactly 3 bytes — header only, the
"high bit of the type byte set" rule; POM68K dispatches on exactly that
(`src/main.cpp:175-182`, `src/AtalkStack.cpp:100`). Data frames carry
their length in the DDP header.

### 2.3 Dynamic node-ID acquisition (the ENQ dance)

A node with no address picks a tentative ID (PRAM's last value, or
random in its range), broadcasts **lapENQ** to it repeatedly, and takes
it if nobody answers **lapACK**. `llap_two_system_etalon` shows two Macs
sending ~650 probes each and settling on distinct IDs.
`AtalkStack::onGuestFrame` answers ENQ for node 128 through the *express*
path, for the same reason the CTS uses it (`src/AtalkStack.cpp:91-99`).

### 2.4 Media access: CSMA/CA with RTS/CTS — and the IDG

LocalTalk has **no collision detection** (you cannot listen while you
drive an RS-422 pair). It avoids collisions instead:

- **Carrier sense** — the line must be idle for the **Inter-Dialog Gap
  (IDG) ≥ 400 µs** plus a random extra before sending.
- **Directed frames handshake**: lapRTS → the destination must answer
  **lapCTS within the Inter-Frame Gap (IFG) ≤ 200 µs** → data frame.
  Up to **32 retries**.
- **Broadcast**: lapRTS to node 255, one idle IFG, then the data
  (nobody CTSes a broadcast).
- Frames *within* a dialog are separated by ≤ IFG; separate dialogs by
  ≥ IDG.

**This is not academic — it is what broke twice and what §6.5's stack
still lives by:**

- The synthesized **lapCTS** must land *inside the sender's IFG window*,
  so it takes the express path with a short fixed gap —
  `kCtsGapBytes = 4` byte-times ≈ 139 µs (`src/Scc8530.h:332`), and
  express frames bypass the "Rx is off during my own transmit" drop
  (`src/Scc8530.cpp:290`).
- Every *other* injected frame defers a full **IDG** — `kIdgBytes = 12`
  byte-times ≈ 417 µs (`src/Scc8530.h:336`) — and that idle is evaluated
  **at dequeue** from the `rxIdle` counter, never baked in at injection.
  When two frames are injected in one poll (the router's LkUp broadcast,
  then afpd's LkUpReply), the second's gap must be measured from the
  first's *end*. Baking it in made the reply start the instant the
  broadcast finished, its head landing in a still-closing FIFO and its
  tail playing into hunt — **the Chooser re-sent the AFPServer lookup
  forever and never listed the server** (2026-07-22 live capture,
  `src/Scc8530.cpp:300-310`).
- Same failure mode, second cause: with only the internal registry
  answering, relaying a BrRq as a segment LkUp put our broadcast and our
  own LkUpReply back-to-back in the guest's Rx FIFO. Hence
  `setBridgeRelay` — **off unless the LToUDP cable is up**
  (`AtalkStack.h:90-96`, `AtalkHub.h:64`).
- Replies generated inside the guest's TX callback would hit a deaf
  receiver, so `AtalkHub::sendFrame` **queues** and flushes from `tick()`,
  after the guest's EOM ISR has re-armed Rx (`AtalkHub.h:77-88`). This
  is why finer quantum slicing matters: 64 slices/frame ≈ 260 µs of
  latency per AFP round-trip (`src/main.cpp:208`).

### 2.5 Where POM68K is faithful and where it isn't

`Scc8530` models the SDLC **frame** level well — hunt/carrier-sense,
CRC-16, address search, RTS/CTS, ENQ, Tx-underrun = end-of-frame — and
per the 2026-07-22 MAME `z80scc.cpp` audit it models *more* of SDLC than
MAME does (MAME's SCC is async-centric; Send Abort, CRC resets and
hunt/sync are stubbed there). Not modelled: the FM0 bit clock / DPLL,
per-bit timing, and the baud-rate generator (harmless at LocalTalk's
fixed rate).

One line-state subtlety, because Open Transport depends on it: a
**virgin line reads clean**. No FM0 edges → no recovered clock → no
sampled 1s → no abort condition; the standing abort only begins once the
line has carried a frame (`Scc8530::lineDriven_`, `src/Scc8530.h:309-313`).
System 7's LAP does not care, but OT waits for the abort to *clear*
before binding `.MPP` — gate `q605_ot_bind_etalon`, and the env hatch
that used to paper over it (`POM68K_SCC_CLEANLINE`) is retired
(`docs/LLE_VS_HLE.md` §10, CHANGELOG 2026-07-28).

Gap list and MAME line references: `docs/LLE_VS_HLE.md` §3.

---

## 3. The network and transport core

Implemented in-process by `AtalkStack` (`src/AtalkStack.cpp`), whose
router behaviour mirrors `extern/tashrouter` for a single LocalTalk
segment with one zone. Gate: `atalk_stack_test`.

### 3.1 DDP — the datagram everything rides on

Connectionless best-effort delivery. Two header forms, and replies
**mirror the requester's form** (`AtalkStack.h:50-51`):

- **Short (5 bytes)** — LLAP type `0x01`, same network: length(10 bits)
  + dest socket + source socket + DDP type.
- **Long (13 bytes)** — LLAP type `0x02`, routed: hop count + length +
  optional checksum + dest/src net + dest/src node + dest/src socket +
  DDP type.

Max payload **586 bytes** (`kMaxDdpData`, `src/AtalkStack.cpp:42`). The
optional checksum is a rotate-and-add over the bytes after the checksum
field (0 = not checksummed; a computed 0 is stored `0xFFFF`); POM68K
sends 0 (`src/AtalkStack.cpp:160`).

### 3.2 RTMP and ZIP — routing and zones

- **RTMP** — distance-vector routing between routers, split-horizon,
  **max 15 hops**, **31 = poison** (`include/atalk/rtmp.h`). Routers
  broadcast their tables every 10 s; a Mac learns its network number and
  "who is my router" from them. `AtalkStack` beacons RTMP Data every
  **10 s** and answers RTMP Requests with the header alone
  (`src/AtalkStack.cpp:179, 243-266`).
- **ZIP** — maps network numbers ↔ zone names: `QUERY=1, REPLY=2,
  GNI=5, GNIREPLY=6, GETMYZONE=7, GETZONELIST=8, GETLOCALZONES=9`
  (`include/atalk/zip.h`). GetNetInfo rides DDP; GetMyZone/GetZoneList
  ride ATP on socket 6 (`src/AtalkStack.cpp:81, 302`).

**The ZIP bug worth remembering** (`src/AtalkStack.cpp:272-298`): in a
GetNetInfo request the **zone length is at offset 6, not 1**. Reading
`p[1]` always found the mandatory zero byte, so the requested zone was
always `""` and always judged *valid* — a guest holding a stale zone was
told it was still good, and every NBP BrRq it then sent was dropped:
another silent empty Chooser. Oracles: tashrouter
`zip/responding.py` (`data[7:7+data[6]]`), netatalk `etc/atalkd/zip.c`.
An *empty* zone is the documented "tell me the default zone" probe, so it
must be judged invalid — that is what makes the trailing default-zone
string fire.

### 3.3 NBP — names, and therefore the Chooser

Every service registers a **name tuple**, each field ≤ 32 chars:

```
Object : Type @ Zone      e.g.  POM68K : AFPServer @ POM68K
                                Front Office : LaserWriter @ *
```

NBP runs on **DDP socket 2 / DDP type 2**. Ops
(`include/atalk/nbp.h:81-91`): **BrRq=1, LkUp=2, LkUpReply=3, FwdReq=4**,
plus register/confirm. Wildcards `=` and the `≈` byte `$C5` match
anything (`src/AtalkStack.cpp:66`).

The Chooser is *just an NBP client*: "AppleShare" is a lookup for
`=:AFPServer@<zone>`, "LaserWriter" for `=:LaserWriter@<zone>`; each
responder returns its tuple → its DDP address → the Chooser lists the
*Object* names.

```
1->254   NBP-BrRq  '='            (guest asks the router)
254->255 NBP-LkUp  '='            (router broadcasts the lookup)
254->1   NBP-LkUpReply 'POM68K'   (the server answers with its tuple)
```

In-process, the registry is `AtalkStack::nbpRegister` and the middle line
happens **only when a real cable carries external peers** (§2.4). Three
services register: `AFPServer`, `LaserWriter`, `IPGATEWAY`.

### 3.4 ATP — the reliable transaction (foundation of ASP and PAP)

**ATP (DDP type 3)** turns best-effort datagrams into reliable
request/response:

- **TReq** → **TResp** (up to **8 packets**, tracked by an 8-bit
  bitmap/sequence so lost ones are re-requested individually) →
  optional **TRel**.
- Two service levels: **ALO** (retransmit until answered, duplicates
  possible) and **XO** (exactly-once — the responder caches the result
  under the transaction ID and a **release timer**, `ATP_RELTIME = 30 s`,
  so a retransmit gets the cached reply instead of a re-execution).
  AFP writes need XO.
- Header: control-info byte (function + XO + EOM + STS) + bitmap/sequence
  + 16-bit transaction ID (`struct atphdr`, `include/atalk/atp.h:59-63`).
  Max data 578 + 4 user bytes = **582** (`ATP_MAXDATA`).

`AtalkStack` implements **both roles** — responder (XO cache with the
same 30 s release timer, `src/AtalkStack.cpp:421-425`; deferred replies
for ASP FPWrite) and requester (5 tries, 1 s apart,
`src/AtalkStack.cpp:187-203`), because ASP tickle, ASP WriteContinue and
PAP SendData are all *server*-initiated. Buffers follow the netatalk
convention: 4 ATP user bytes then the data.

Two hard-won responder details:

- A deferred transaction (PAP's blocking `kRead`) dropped without ever
  calling `respond()` used to sit forever and swallow every later TReq
  reusing that (client, tid) — the guest's 16-bit tid counter wraps, so
  the socket wedged permanently. `pendingTxns_` now carries the same
  30 s release timer as the XO cache (`AtalkStack.h:210-218`).
- MacIP needs ATP *and* a raw DDP handler on the same socket 72, so ATP
  only claims type 3 where a transaction handler is bound
  (`src/AtalkStack.cpp:212-215`).

---

## 4. The session and application protocols

### 4.1 ASP — sessions, tickles, and the mount that dropped

**ASP** sits on ATP and gives AFP a long-lived, ordered, asymmetric
session: the workstation opens it and sends commands, the server replies.
Functions (`include/atalk/asp.h:71-78`): `CLOSE=1, CMD=2, STAT=3, OPEN=4,
TICKLE=5, WRITE=6, WRTCONT=7, ATTN=8`.

**Tickle keep-alive, in-process** (`AfpServer.cpp:208-216`): the server
sends SPTickle every **30 s** and declares a session dead after **120 s**
of silence (4 missed tickles). **External netatalk**: `afpd` increments
`ac_state` each `tickleval` and sends a best-effort tickle
(`sreqtries = 1`, `libatalk/asp/asp_tickle.c`); a client tickle resets it
to `ACSTATE_OK` (1); at `ACSTATE_BAD = 7` — **6 consecutive misses**,
`libatalk/asp/asp_child.h:33-35` — the parent SIGTERMs the session child
and the Mac shows *"the file server's connection has unexpectedly closed
down."*

**Keep the alert and the cause separate.** The 2026-07-22 "Input mounted
then dropped" screenshot looked exactly like a tickle timeout. It was
not. The LToUDP capture showed the wire working perfectly; the afpd
syslog gave the real answer:

```
afpd: AFPVersion 2.1 Login by gistarcade          ← login OK
afpd: no suitable network config from CNID server (localhost:4700)
afpd: get_id: Connection to the CNID backend DB failed ... fatal error.
```

**The cause was the CNID backend.** afpd's default scheme (`dbd`) asks a
**`cnid_metad`** daemon on `localhost:4700` to spawn a per-volume
`cnid_dbd` handing out Catalog Node IDs. `appleshare.sh` never started
it, so the first CNID request after the mount hit a *fatal* error and
killed the session child — same user-visible string. The master afpd
stayed up and NBP-registered, which is why the server kept appearing in
the Chooser. Fixed by starting `cnid_metad` between `atalkd` and `afpd`.
**Moral: on "connection closed", read the afpd syslog before the wire.**

For a *genuine* tickle timeout the levers are: watch the periodic ATP
tickle and its reply (a one-sided exchange localizes it to Rx or Tx),
keep the 1 ms-class slicing active, make sure the IDG deferral (417 µs ≪
tickleval) never queues the reply behind another frame, or raise
`tickleval`.

### 4.2 ADSP — the full-duplex stream

**ADSP (DDP type 7)** is the one connection-oriented byte stream in the
suite: full-duplex, reliable, flow-controlled, own setup and keep-alive.
AFP uses ASP, not ADSP, so nothing here needs it. **Not implemented** in
POM68K, in-process or otherwise.

### 4.3 AFP — the file service (AppleShare)

**AFP** rides ASP: login with a User Authentication Method, then a large
call vocabulary (`FPOpenVol`, `FPGetSrvrParms`, `FPEnumerate`, `FPRead`,
`FPWrite`, fork ops, Desktop database…).

`AfpServer` offers **AFPVersion 1.1 / 2.0 / 2.1** and the UAMs **"No User
Authent"** and **"Cleartxt Passwrd"** (`src/AfpServer.cpp:253-259`). It
covers what System 6-8 Finders actually issue for browsing and copying
both ways; resource forks and Finder info live in netatalk-style
**`.AppleDouble/<name>` sidecars (AppleDouble v2)**, so a folder
previously served by the external `afpd` keeps its metadata. CNIDs are
per-run with **root = 2** (`src/AfpServer.cpp:42`), i.e. stable within a
session, not across restarts.

Historical trap, external path: an **empty volume list** in the Chooser
was AFP-level authorization — `FPGetSrvrParms` returned zero volumes
because the guest UNIX account could not traverse to the share; fixed
with `-guestname "$REAL_USER"` in `appleshare.sh`.

### 4.4 PAP — the printer service (the LaserWriter path)

**PAP** is to printers what ASP+AFP is to files: it rides **ATP** and is
found by **NBP type "LaserWriter"**. Functions
(`include/atalk/pap.h:29-41`): `OPEN=1, OPENREPLY=2, READ=3 (SendData),
DATA=4, TICKLE=5, CLOSE=6, CLOSEREPLY=7, SENDSTATUS=8, STATUS=9`;
`PAP_MAXDATA=512`, `PAP_MAXQUANTUM=8`.

A job, top-down then bottom-up:

1. **NBP** lookup `=:LaserWriter@zone`; the driver gets a DDP address.
2. **Open** — `OpenConn` (ATP) carrying a connection ID, the responding
   socket and a **flow quantum** (512-byte buffers the sender may use;
   the LaserWriter uses **8**). Server answers `OpenConnReply` or busy.
3. **Read-driven pull** — PAP is *pull*, not push. To print, the
   *printer* issues `SendData` credits and the Mac streams **PostScript**
   back as ATP `DATA`; the last packet carries **EOF**.
4. **Status** — `SendStatus`/`Status` returns a Pascal string ("status:
   idle", "%%[ PrinterError… ]%%") readable *without* opening a
   connection; that is the text the Chooser and PrintMonitor show.
5. **Tickle** — each side runs a **2-minute connection timer** and
   tickles every **60 s**. `PapServer` implements exactly that
   (`src/PapServer.cpp:81-90`).
6. **Close** — `CloseConn`/`CloseConnReply`; next job.

The driver's authentication conventions ride *inside the PostScript
stream* as `%%?Begin…Query` comments (`NoUserAuthent`, `CleartxtPasswrd`,
`RandnumExchange`). `PapServer` answers every query with the "unknown"
reply **`*`**, so the driver downloads its own proc sets — a printer with
no spooler smarts, which is exactly what we want since CUPS owns the last
mile (`src/PapServer.h:8-13`).

---

## 5. End-to-end walkthroughs

### 5.1 Mounting AppleShare

```
Chooser (guest)                    router            file server
  │  NBP BrRq =:AFPServer@POM68K ──►  (LkUp) ──►
  │                                        ◄── LkUpReply "POM68K"
  │  (user double-clicks POM68K, logs in as Guest)
  │  ASP OpenSession ───────── ATP TReq ────────►
  │                                        ◄── OpenSessionReply (session id)
  │  AFP FPLogin/FPGetSrvrParms/FPOpenVol (ASP CMD on ATP)
  │                                        ◄── volume mounts
  │  ⟳ ASP Tickle (30 s in-process)  ◄────────►  ⟳
```

Every arrow is DDP in LLAP frames over the SDLC line `Scc8530` drives —
then either straight into `AtalkStack` (default) or out through `LtoUdp`
→ TashRouter → `afpd` (§6.1).

### 5.2 Printing

```
App "Print" → LaserWriter driver → the NBP-chosen printer
  → PAP OpenConn (ATP) → printer SendData → Mac streams PostScript
  → EOF → CloseConn
```

The "printer" is `PapServer` (→ `lp`/CUPS, else a `.ps` file) or, on the
external path, netatalk `papd` (§6.2).

---

## 6. Bridging to the modern host — and to CUPS

§§6.1-6.4 describe the **external** chain: TashRouter routes, netatalk's
`afpd`/`papd` serve, `macipgw` tunnels, `appleshare.sh`/`macip.sh` wire
it up (needing root). Use it to reach a **real** LocalTalk network or
anything the in-process subset skips. §6.5 is the in-process default.

### 6.1 The transport bridge

- **`LtoUdp`** (`src/LtoUdp.cpp:24-25`, `POM68K_LTOUDP=1`) carries raw
  LLAP frames over UDP **multicast 239.192.76.84:1954** with a 4-byte
  sender tag (pid⊕clock, filters self-reception) — the Mini vMac /
  TashTalk "LToUDP" format. This *is* the LocalTalk cable, made of UDP.
- **TashRouter** (`extern/tashrouter`) is the DDP router: `LtoudpPort`
  speaks our exact format, `LinuxTapPort` bridges to a host TAP; it runs
  RTMP/ZIP/NBP so the guest gets a network number and the zone.
- **netatalk 2.4.9** (`extern/netatalk2`) on the TAP: `atalkd`, `afpd`,
  and for printing `papd`.

`tools/netatalk2/appleshare.sh` wires all three in order (module + TAP,
router, then `atalkd` + `cnid_metad` + `afpd`).

### 6.2 Mac → CUPS: sharing a modern printer to the vintage Mac (papd)

`papd` registers an NBP `LaserWriter` entity, accepts the PostScript job,
and spools it to a UNIX print system — **CUPS included**
(`etc/papd/print_cups.c`). Minimal `papd.conf` exporting *all* CUPS
queues to the Chooser:

```
cupsautoadd:op=root:
```

`cupsautoadd` (`etc/papd/main.c:767-775`) calls
`cups_autoadd_printers()`: every CUPS queue becomes a `LaserWriter` NBP
entity using this stanza's parameters as defaults; later stanzas override
a single queue. Per-printer form:

```
"Front Office":\
    :pr=hp_laserjet:\      # CUPS queue name (or |pipe-command for P_PIPED)
    :pd=/path/to.ppd:\     # PPD to advertise
    :op=root:\             # operator / job owner
    :co=media=A4 sides=two-sided:   # CUPS options passed through
```

Keys (`etc/papd/main.c` `getprinters()`): `pr=` queue-or-pipe, `pd=` PPD,
`op=` operator, `pa=` AppleTalk address, `co=` CUPS options, `ca=`
authenticated-capture dir, `am=` UAM list. Flags
(`etc/papd/printer.h:55-66`): `P_PIPED`, `P_SPOOLED`, `P_CUPS`,
`P_CUPS_PPD`, `P_CUPS_AUTOADDED`.

Guest side is §0.2's printing row. **Why this is the interesting
direction:** a vintage Mac prints to a USB inkjet it has no driver for,
because it only ever emits PostScript and CUPS owns the last mile.

> `papd` sessions (`etc/papd/session.c:101-144`) use a 60 s select
> timeout, **3 misses = close**, and send `PAP_TICKLE` when idle — the
> same lossy-cable caveat as AFP applies to long jobs.

### 6.3 CUPS → AppleTalk: printing to a real LaserWriter

The reverse uses netatalk's **`pap` client** (`bin/pap/pap.c`):

```
pap -A 'PrinterName:LaserWriter@ZoneName' document.ps
```

It does the NBP lookup, opens the connection, streams the file and tracks
status (`-s statusfile`, `-w/-W` wait-for-idle). To drive it from CUPS,
wrap `pap` in a **CUPS backend** script under `/usr/lib/cups/backend/`
(e.g. `appletalk`) reading the job on stdin, and add the printer with
`device-uri appletalk://Zone/PrinterName`. Modern CUPS ships no AppleTalk
backend — Apple removed it when it deprecated AppleTalk in Mac OS X
10.6/10.7 — so the wrapper is the route. Relevant only if POM68K ever
drives *real* LocalTalk hardware.

### 6.4 MacIP — real TCP/IP for the guest, tunneled in DDP

IP datagrams ride *inside* AppleTalk (IP-in-DDP), the inverse of §6.1's
LLAP-over-UDP. Historically **KIP** (Kinetics IP, for the FastPath
LocalTalk↔Ethernet gateways), later **MacIP**; Open Transport calls it
"AppleTalk (MacIP)". It exists for exactly our situation: a Mac whose
only network is LocalTalk, wanting TCP/IP.

**The protocol** (`extern/macipgw/macip.c`, verified 2026-07-23 — it *is*
the wire spec in practice; `MacIpGateway` implements the same):

- **Finding the gateway — NBP.** The gateway registers
  `<gw-ip>:IPGATEWAY@<zone>` (`macip.c:71-72,654-670`); the client looks
  up type `IPGATEWAY` in its zone. Same LkUp/LkUpReply frames as the
  Chooser — that is the whole discovery story.
- **Getting an address — one ATP transaction** to the gateway's **DDP
  socket 72**: function **1** = assign me an address (reply carries IP,
  name server, broadcast, netmask — `struct macip_req`,
  `macip.c:87-104,398-415`), function **3** = are you a MacIP server.
  DHCP in one round-trip, years before DHCP.
- **"ARP" — NBP a third time.** Each addressed client registers
  `<client-ip>:IPADDRESS@<zone>`; the gateway resolves IP→AppleTalk by
  looking the dotted-quad up as an NBP *object* of type `IPADDRESS`
  (`arp_lookup`, `macip.c:224-244`), and also learns from inbound
  packets. External macipgw probes idle leases with ICMP echo every 30 s
  and reclaims after 10 misses (`macip.c:79-81,604-626`); `MacIpGateway`
  instead expires a lease after **3600 s** of silence, so a quiet but
  live MacTCP node keeps its address (`MacIpGateway.h:65`).
- **Data — DDP type 22**, ≤586 bytes of IP per datagram.

**Two ways to run it.** In-process (default, no root): `MacIpGateway`
answers on socket 72 and NATs in user mode — see §6.5 for the subset.
External (`extern/macipgw`, Stefan Bethke 1997 / Jason King's Linux port,
built by `tools/macip/build_macipgw.sh` against the vendored libatalk):

```bash
sudo tools/netatalk2/appleshare.sh --macip     # bridge + MacIP in one go
# or, bridge already up:
tools/macip/build_macipgw.sh                   # once, no sudo
sudo tools/macip/macip.sh                      # start · stop to undo
```

`macip.sh` makes exactly three host changes, echoes each and reverses all
of them on `stop`: `net.ipv4.ip_forward=1` (previous value saved), three
`iptables` rules tagged `pom68k-macip`, and the `macipgw` process (its
tun device dies with it). Tunables: `MACIP_NET`/`MACIP_MASK` (default
192.168.151.0/24 — gateway .1, clients .2-.254; must not collide with a
real host network), `MACIP_DNS` (default 8.8.8.8 — must be reachable
*through the NAT*, so never the systemd stub 127.0.0.53), `MACIP_ZONE`,
`MACIP_DEBUG`. `MacIpGateway` defaults mirror these
(`AtalkHub.h:50`, `MacIpGateway.h:111-113`).

```
guest Mac OS (OT or MacTCP, AppleTalk on)              host
  IP in DDP-22 ── LLAP/Scc8530 ──┬── AtalkStack ── MacIpGateway ── user-mode NAT ── sockets
                                 └── LtoUdp ── TashRouter ── pomtap0 ── macipgw ── tunX ── MASQUERADE
```

**Guest configuration — Mac OS 8.1 / Open Transport:**

1. AppleTalk must already be up (§0.1's PRAM seed, or the Chooser).
2. **TCP/IP** control panel → *Connect via:* **AppleTalk (MacIP)** →
   *Configure:* **Using MacIP Server**.
3. *MacIP server zone:* **Select Zone…** → **POM68K**.
4. *Name server addr:* the DNS the gateway advertises (default
   **8.8.8.8**) — the assignment reply carries it, but filling it in
   removes a variable. Search domains can stay empty.
5. Close and save. The first TCP/IP user triggers the NBP lookup + ATP
   assign; the GUI's MacIP block (or `run/macipgw.log` externally) shows
   the lease.

**Guest configuration — System 7.5 / classic MacTCP** (ships on the 7.5
install media, Networking extras):

1. **MacTCP** control panel → click the **LocalTalk** icon; pick zone
   **POM68K** in the list under the icons.
2. **More…** → *Obtain Address:* **Server** (that is MacIP
   server-assigned mode; leave Gateway Address alone).
3. *Domain Name Server Information:* domain `.`, IP **8.8.8.8**,
   *Default* checked.
4. OK, close, **reboot** — classic MacTCP reads its config only at
   startup.

**Era caveat.** The stack is genuine TCP/IP, but a 1996 browser cannot
shake hands with 2026 TLS: **plain HTTP only**. Practical destinations:
<http://frogfind.com> (search + readability proxy, made for this),
<http://theoldnet.com> (archived-web proxy), or a self-hosted
down-converting proxy (WebOne, macproxy). Netscape 2-4 on OS 8.1 and
Netscape 2 / MacWeb on 7.5 all work against these.

**Troubleshooting:**

| Symptom | Check |
|---|---|
| Guest "can't find MacIP server" | In-process: the GUI's *Passerelle visible (NBP IPGATEWAY)* bullet, and that the guest's zone is POM68K. External: `nbplkup "=:IPGATEWAY@POM68K"` must list it; empty → `run/macipgw.log` (NBP registration retries 5×5 s; `atalkd` running, started *after* the router?) |
| Gateway visible, no address assigned | `POM68K_MACIP_DEBUG=1` (external: `MACIP_DEBUG=0x1111`) and watch for the ATP request on socket 72 |
| Guest has an IP, nothing loads | In-process: the IP counters must move both ways. External: `ping` the guest, `tcpdump -i tunX -n`, `iptables -t nat -S \| grep pom68k`, `sysctl net.ipv4.ip_forward` |
| Names fail, IPs work | DNS must be a real resolver reachable through NAT (never 127.0.0.53); try 1.1.1.1 and re-enter it guest-side |
| `https://` anything | Expected — TLS era gap, use FrogFind / theoldnet |
| Large transfers stall | External only: tun MTU must be 586 (`ip link show tunX`) — DDP's payload ceiling |

### 6.4bis The other way to the same NAT: Ethernet over SCSI (2026-08-07)

MacIP exists because the Mac's only network is LocalTalk. The period
answer when that was too slow was a **DaynaPort SCSI/Link**: an Ethernet
card that answers SCSI commands, so any Mac with a SCSI bus — which is
every machine in this tree — gets real Ethernet. POM68K now models one
(`DaynaPort`), bridged by `EtherLink` onto the **same** user-mode NAT
`MacIpGateway` already runs. So there are two roads to the outside and one
gateway behind both:

```
MacTCP "AppleTalk (MacIP)" ── DDP-22 ── Scc8530/LLAP ──┐
                                                        ├── MacIpGateway NAT ── host sockets
MacTCP "Ethernet" ── DaynaPort ── SCSI bus ── EtherLink ┘
```

Why bother, given §6.4 works: the LLAP road runs at 230.4 kbit/s through
the SCC, the most timing-fragile device here (hence
`POM68K_ATALK_WIRE_BOOST`). The SCSI bus is neither slow nor fragile.

Operating it: `POM68K_DAYNAPORT=<id>` (Quadra 605 only today; `=1` picks
the default ID 3, where the CD-ROM normally sits). Guest side needs the
DaynaPort SCSI/Link driver plus a **manual** MacTCP/TCP-IP configuration —
an address in the gateway's subnet (192.168.151.x by default), the gateway
as router, a DNS server. There is no address handout on this road: MacIP's
ATP assign has no Ethernet equivalent and no BOOTP/RARP responder is
modelled, so the lease is learned from the guest's first packet. ARP is
answered as a proxy for the whole subnet, never for the guest's own
address — a reply there reads as a duplicate address and MacTCP refuses to
initialise.

Caveats, all real: **no guest driver has been run against it** (the
command set is gated by `daynaport_test`, the driver's opinion of it is
not); **no EtherTalk** — the card carries IPv4 and ARP only, so everything
in §§1-5 above still travels over the SCC; and the uplink lives in
`AtalkHub`, so `POM68K_APPLETALK=0` leaves the guest a card with nothing
behind it. Design notes: `DEV.md` § 3.3bis; rationale and the RaSCSI/PiSCSI
provenance: `CHANGELOG.md` 2026-08-07 (later).

### 6.5 The in-process stack — POM68K as its own router, server, printer and gateway

The default path since 2026-07-24: no external processes, no root, on
unless `POM68K_APPLETALK=0`. Operating it is §0; this section is what it
*is* and what it is *not*.

```
guest Mac OS                                   POM68K process
  LLAP/DDP ── Scc8530 wire ── AtalkHub ── AtalkStack (2.128, zone POM68K)
                                            ├── router-lite: RTMP/ZIP/NBP/AEP
                                            ├── AfpServer   (NBP AFPServer,   ASP/AFP → host folder)
                                            ├── PapServer   (NBP LaserWriter, PAP → lp/CUPS or .ps)
                                            └── MacIpGateway (NBP IPGATEWAY, ATP :72, DDP-22 → user-mode NAT)
```

| AppleTalk layer | Owner | File |
|---|---|---|
| LLAP node presence (ENQ defence), DDP short/long | `AtalkStack` | `src/AtalkStack.cpp:84-206` |
| RTMP beacon + req/resp, ZIP GetNetInfo/ZoneList, NBP registry + LkUp + BrRq relay, AEP echo | `AtalkStack` router-lite | `src/AtalkStack.cpp:208-410` |
| ATP responder (XO cache, release timer, deferred replies) + requester (retries, bitmap fill) | `AtalkStack::AtpTxn` / `atpRequest` | `src/AtalkStack.cpp:414-580` |
| ASP sessions + AFP 2.1 file service, `.AppleDouble` sidecars | `AfpServer` | `src/AfpServer.{h,cpp}` |
| PAP printer → CUPS (`lp`) or `.ps` spool | `PapServer` | `src/PapServer.{h,cpp}` |
| MacIP (ATP :72 assign, IP-in-DDP-22) + user-mode NAT | `MacIpGateway` | `src/MacIpGateway.{h,cpp}` |
| SCC wiring, service toggles, GUI status snapshot | `AtalkHub` | `src/AtalkHub.h`, `src/main.cpp:79-219, 546-662` |

**Threading contract** (`AtalkHub.h:16-21`): the hub's mutex guards the
hub's own state, never the machine's. The SCC's Rx meters are unlocked
machine-thread state, so they are sampled in `tick()` (machine thread)
and served from that copy by `snapshot()` (GUI thread). No GUI-thread
path may dereference the SCC.

**Coexistence.** With `POM68K_LTOUDP=1` the internal node's frames are
multicast onto the cable too: the emulator looks like one more node on
the shared virtual LocalTalk, external TashRouter/netatalk can run
alongside, and the guest simply sees two responders and de-dups.

**The NAT** (`MacIpGateway.h:13-26`) is slirp-style: one connected host
socket per UDP flow (DNS is just UDP 53 through it); a miniature TCP
endpoint facing the guest (SYN-ACK, ordered delivery,
retransmit-on-timeout, FIN both ways, MSS 536, in-order only) proxied
onto a non-blocking host socket; ICMP echo answered for the gateway
address itself only — raw sockets need privileges, and TCP/UDP is what
era software uses.

**What the subset does not do** — for any of it, run the external bridge
(§6.1-§6.4), which the internal node coexists with:

- **ADSP** (§4.2).
- The **Desktop database** — `OpenDT` and friends answer "no item".
- **AFP ≥ 3.0 / UTF-8** names; UAMs beyond guest/cleartext (§4.3).
- **CNID persistence** — per-run, root = 2; stable within a session only.
- PAP status-polling subtleties; MacIP outbound ICMP / raw sockets.

Backlog: `TODO.md` §6. Migration notes and the HLE/LLE gap list:
`docs/LLE_VS_HLE.md`.

---

## 7. Map back to POM68K code

| Layer | Component | File |
|---|---|---|
| LocalTalk physical + SDLC framing | `Scc8530` SDLC engine, CRC-16, hunt/RTS/CTS/ENQ, IDG/IFG pacing, lossless wire | `src/Scc8530.{h,cpp}` |
| LLAP dialogue glue (RTS→CTS synth, per-frame poll, quantum slicing) | `wireLocalTalk`, `pollLocalTalk`, `runQuantumWithWire` | `src/main.cpp:103-219` |
| LocalTalk "cable" | LToUDP multicast + 4-byte tag | `src/LtoUdp.{h,cpp}`, `POM68K_LTOUDP=1` |
| PRAM AppleTalk-active seed (SPConfig `$21`) | `POM68K_APPLETALK=1` | `src/Egret.cpp:77`, `src/Rtc.cpp:44` |
| **In-process** DDP/RTMP/ZIP/NBP/AEP/ATP | `AtalkStack` | `src/AtalkStack.{h,cpp}` |
| **In-process** ASP + AFP 2.1 | `AfpServer` | `src/AfpServer.{h,cpp}` |
| **In-process** PAP → `lp`/CUPS or `.ps` | `PapServer` | `src/PapServer.{h,cpp}` |
| **In-process** MacIP + user-mode NAT | `MacIpGateway` | `src/MacIpGateway.{h,cpp}` |
| Wiring + GUI window + toggles | `AtalkHub`, `appleTalkWindow` | `src/AtalkHub.h`, `src/main.cpp:546-662` |
| External DDP/RTMP/ZIP/NBP routing | TashRouter | `extern/tashrouter` |
| External ATP/ASP/AFP | netatalk `afpd` | `extern/netatalk2` |
| External PAP → CUPS | netatalk `papd` (`cupsautoadd`) | `extern/netatalk2/etc/papd` |
| External MacIP (IP-in-DDP → tun + NAT) | vendored `macipgw` | `extern/macipgw`, `tools/macip/{build_macipgw,macip}.sh` |
| External bring-up / teardown | one-command bridge (`--macip` adds TCP/IP) | `tools/netatalk2/appleshare.sh` |
| Wire capture (throughput / gaps / RTT, LLAP headers) | passive LToUDP probe | `scratchpad/ltoudp_measure.py` |

Gates: §0.5.

---

## 8. Quick reference

**LLAP types:** `01` short DDP · `02` long DDP · `81` ENQ · `82` ACK ·
`84` RTS · `85` CTS.
**Node IDs:** `0` invalid · `1-127` user · `128-254` server · `255`
broadcast.
**LLAP timing:** 230.4 kbit/s (≈34.7 µs/byte) · IDG ≥ 400 µs · IFG
≤ 200 µs · 32 retries. POM68K: `kCtsGapBytes` 4 · `kIdgBytes` 12 ·
`byteCycles = cpuHz / 28800`.

**DDP sockets:** `1` RTMP · `2` NBP · `4` AEP · `6` ZIP · `72` MacIP
config (ATP). SAS 1-127 · DAS 128-254.
**DDP protocol types:** `1` RTMP-data · `2` NBP · `3` ATP · `4` AEP ·
`5` RTMP-request · `6` ZIP · `7` ADSP · `22` MacIP.
**DDP:** short hdr 5 B · long hdr 13 B · max data 586 B · address =
net(16)·node(8)·socket(8).

**ATP:** DDP type 3 · TReq/TResp/TRel · ≤8 response packets (bitmap) ·
ALO/XO · release timer 30 s · max data 582 B (4 user + 578).
POM68K requester: 5 tries, 1 s apart.
**ASP funcs:** CLOSE 1 · CMD 2 · STAT 3 · OPEN 4 · TICKLE 5 · WRITE 6 ·
WRTCONT 7 · ATTN 8. Session death: **netatalk 6 missed tickles**;
**`AfpServer` 30 s tickle, 120 s idle**.
**NBP ops:** BrRq 1 · LkUp 2 · LkUpReply 3 · FwdReq 4. Name
`Object:Type@Zone`, ≤32 ch each; wildcards `=` and `$C5`.
**PAP funcs:** OPEN 1 · OPENREPLY 2 · READ/SendData 3 · DATA 4 ·
TICKLE 5 · CLOSE 6 · CLOSEREPLY 7 · SENDSTATUS 8 · STATUS 9 · quantum 8 ·
512 B/pkt · NBP type "LaserWriter" · tickle 60 s, 2 min timer.
**ZIP ops:** QUERY 1 · REPLY 2 · GNI 5 · GNIREPLY 6 · GETMYZONE 7 ·
GETZONELIST 8 · GETLOCALZONES 9. GetNetInfo zone length is at **offset 6**.
**RTMP:** max 15 hops · 31 poison · 10 s beacon. **AARP:** REQUEST 1 ·
RESPONSE 2 · PROBE 3.
**MacIP:** gateway NBP type `IPGATEWAY` · client `IPADDRESS` (object =
dotted quad) · assign = ATP func 1 · probe = 3 · default
192.168.151.0/24, gateway .1, DNS 8.8.8.8.
**In-process node:** net 2 · node 128 · zone POM68K · wire boost ×8.

---

## 9. Sources

- *Inside AppleTalk*, 2nd ed. (Sidhu, Andrews, Oppenheimer) — the
  definitive spec. HTML mirror:
  <https://obsoletemadness.github.io/Inside-AppleTalk/books/inside-appletalk-second-edition/>
  (LLAP ch.1, AARP ch.2, EtherTalk/TokenTalk ch.3, DDP ch.4, RTMP ch.5,
  AEP ch.6, NBP ch.7, ZIP ch.8, ATP ch.9, PAP ch.10, ASP ch.11, ADSP
  ch.12, AFP ch.13, print spooling ch.14; PDF:
  <https://www.tmetz.net/os/Apple/Inside_AppleTalk.pdf>).
- Apple, *Inside Macintosh: Networking* (protocol stack, OSI mapping):
  <https://dev.os9.ca/techpubs/mac/Networking/Networking-19.html>,
  <https://dev.os9.ca/techpubs/mac/Networking/Networking-21.html>.
- Apple archived protocol PDFs — ASP (tickle), NBP, AFP:
  <https://developer.apple.com/library/archive/documentation/mac/pdf/Networking/ASP.pdf>,
  `.../NBP.pdf`, `.../AFP.pdf`.
- Stuart Cheshire, "AppleTalk NBP":
  <https://stuartcheshire.org/rants/NBP.html>; RFC 6760 (why NBP mattered).
- TashTalk LLAP protocol notes:
  <https://github.com/lampmerchant/tashtalk/blob/main/documentation/protocol.md>;
  Zilog *Technical Considerations When Implementing LocalTalk* (SCC/SDLC):
  <https://www.zilog.com/docs/z180/appnotes/loctalk.pdf>.
- netatalk manual — papd (PAP↔CUPS), pap, AppleTalk:
  <https://netatalk.io/manual/en/papd.8>,
  <https://netatalk.io/docs/PAP-Print-Server>.
- **netatalk 2.4.9 vendored in `extern/netatalk2/`** (`file:line`
  verified 2026-07-22): `include/atalk/{ddp,atp,asp,nbp,zip,rtmp,pap}.h`,
  `libatalk/asp/{asp_getsess.c,asp_tickle.c,asp_child.h}` (tickle/timeout),
  `etc/papd/{main.c,session.c,print_cups.c,printer.h}`, `bin/pap/pap.c`,
  `etc/atalkd/zip.c`.
- MacIP: **macipgw vendored in `extern/macipgw/`** (Stefan Bethke 1997,
  Linux port <https://github.com/zero2sixd/macipgw>, provenance in its
  `POM68K_VENDOR.md` — `macip.c` *is* the MacIP wire spec in practice).
- **TashRouter** (`extern/tashrouter`) — router behaviour oracle,
  notably `zip/responding.py`.
- POM68K itself: `DEV.md` §5 (every environment knob),
  `docs/LLE_VS_HLE.md` §3 + §10 (SCC gaps, MAME `z80scc.cpp` audit, the
  virgin-line ruling), `CHANGELOG.md` (LLAP milestone 1, the SCC IDG fix,
  the AppleShare bridge, the in-process stack), `TODO.md` §6 (backlog).
