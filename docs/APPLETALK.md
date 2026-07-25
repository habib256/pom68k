# AppleTalk, LocalTalk, printing, and the bridge to CUPS

A reference for continuing POM68K's networking work. It is written from
**two points of view at once** and keeps switching between them, because
that is the fastest way to actually understand AppleTalk:

- **Top-down (the service):** what a person at the Mac sees — "a server
  named *POM68K* appeared in the Chooser", "the *Input* volume mounted",
  "the LaserWriter is printing".
- **Bottom-up (the wire):** the bytes POM68K's `Scc8530` actually clocks
  onto the SDLC line and the `LtoUdp` cable actually multicasts. This is
  the angle the 1980s Apple manuals never take, and it is the one that
  matters when you are debugging an emulator.

Everything here is cross-checked against *Inside AppleTalk* (2nd ed.,
Sidhu/Andrews/Oppenheimer — the definitive spec), Apple's *Inside
Macintosh: Networking*, and the **netatalk 2.4.9** source vendored in
`extern/netatalk2/` (cited as `file:line`, verified 2026-07-22). Sources
are listed at the end.

> **Why this doc exists.** POM68K already puts an emulated Mac on a real
> AppleTalk network: `Scc8530` speaks the LocalTalk link layer, `LtoUdp`
> is the cable, TashRouter routes, and netatalk's `afpd` serves files
> (`tools/netatalk2/appleshare.sh`). To push that further — reliable AFP
> sessions, printing to CUPS — you have to know the whole stack, not just
> the link layer we implement. The last section maps every protocol back
> to the POM68K file that touches it.

---

## 1. The shape of the whole thing

AppleTalk is a full protocol suite, designed 1983-85 to be **zero-config
on a physical layer nobody else wanted** (a daisy-chained serial cable).
Its defining traits, and why they surprise people used to TCP/IP:

1. **Addresses are dynamic and cheap.** A node picks its own address at
   power-on by guessing and checking (no DHCP, no admin). Names, not
   numbers, are the user-facing identifier — *Name Binding Protocol* is
   as fundamental here as DNS is optional in IP.
2. **The unit of work is the transaction, not the stream.** The workhorse
   transport (*ATP*) is request/response with exactly-once semantics, not
   a byte stream. File sharing and printing are both built on transaction
   round-trips, not sockets-as-pipes.
3. **Everything rides on one connectionless datagram (*DDP*)**, the way
   everything in the IP world rides on IP. DDP's "port" is the *socket*.
4. **Zones are a naming/broadcast-scoping concept, not a subnet.** A zone
   is a human-readable group ("Marketing", "POM68K") that can span
   networks; it exists to make the Chooser's lists manageable.

### 1.1 The layer cake (OSI mapping, sockets, and DDP types)

| OSI layer | AppleTalk protocol(s) | POM68K owner |
|---|---|---|
| Application | **AFP** (file sharing / AppleShare), print drivers | netatalk `afpd` (host side) |
| Presentation | AFP (also does data representation) | — |
| Session | **ASP** (sessions for AFP), **ADSP** (streams), **PAP** (printing), **ZIP** (zone names) | netatalk / guest ROM |
| Transport | **ATP** (transactions), **NBP** (naming), **AEP** (echo/ping), **RTMP** (routing) | netatalk / TashRouter |
| Network | **DDP** (datagram delivery, the socket layer) | TashRouter / guest |
| Data link | **LLAP** (LocalTalk), **ELAP**+**AARP** (EtherTalk), TLAP (TokenTalk) | **`Scc8530` (LLAP)** |
| Physical | LocalTalk (RS-422, 230.4 kbps), Ethernet, … | **`Scc8530` SDLC + `LtoUdp` cable** |

Two numbering spaces trip everyone up; keep them separate:

- **DDP socket numbers** identify a *process/endpoint* on a node (like a
  TCP port). Well-known "statically assigned sockets" (SAS): **1 = RTMP,
  2 = NBP, 4 = AEP (echo), 6 = ZIP**. SAS = 1-127 (Apple reserves 1-63,
  64-127 "experimental"); **dynamically assigned sockets** (DAS) =
  128-254; 0 and 255 reserved. (`extern/netatalk2/include/atalk/`
  headers; DDP chapter of *Inside AppleTalk*.)
- **DDP protocol type** is a byte *inside* the DDP header that says which
  upper protocol owns the payload, independent of socket:
  **1 = RTMP-response/data, 2 = NBP, 3 = ATP, 4 = AEP, 5 = RTMP-request,
  6 = ZIP, 7 = ADSP** (`include/atalk/ddp.h:29-35`).

So an ATP transaction (used by ASP/AFP and by PAP) is **DDP type 3**,
delivered to whatever *socket* the two endpoints negotiated.

A full AppleTalk address is three numbers: **network (16-bit) . node
(8-bit) : socket (8-bit)** — e.g. `2.145:253`. On the wire you saw this
directly in the LToUDP capture: `DDP 2.145:2->1.1:254 NBP-LkUpReply`.

---

## 2. LocalTalk / LLAP — the layer POM68K actually implements

This is the layer `Scc8530` *is*. Everything above it is (for now) done
by netatalk and the guest ROM; this is the one where our bytes are the
real bytes, so it gets the most detail.

### 2.1 Physical: RS-422, 230.4 kbps, FM0, SDLC

- **Electrically**: RS-422 differential pair, daisy-chained, self-
  terminating, passive. No hub. Max ~32 nodes, ~300 m.
- **Bit rate: 230.4 kbit/s.** In POM68K this is `byteCycles_` in
  `Scc8530` — 544 CPU cycles/byte at 15.6672 MHz (LC II / Mac II),
  272 at 7.8336 (Plus), 868 at 25 MHz (Q605). One LocalTalk byte-time
  ≈ 34.7 µs.
- **Encoding: FM0** (differential Manchester) — self-clocking, DC-
  balanced. The real SCC's DPLL recovers the clock from FM0; POM68K
  does not model the DPLL (we deliver whole bytes at wire pace instead —
  see `docs/LLE_VS_HLE.md` §3).
- **Framing: SDLC** (IBM's bit-oriented HDLC variant). Frames are
  delimited by the **flag byte `0x7E`**; the transmitter does **zero-bit
  insertion** (a 0 after five 1s) so `0x7E` never appears inside data;
  an **abort** is 7+ consecutive 1s. `Scc8530` runs the SCC's SDLC mode;
  the flag/zero-insertion is handled by the (modeled) SCC hardware.

### 2.2 The LLAP frame

```
 flag   dst    src    type   … data …    FCS(2)   flag  [abort]
 7E     nn     nn     tt     0..600 B     CRC-16   7E    1111…
        └─ 3-byte LLAP header ─┘
```

- **Destination node**, **source node** (1 byte each), **LLAP type**
  (1 byte). Header is 3 bytes; total frame 5–603 bytes.
- **FCS** = CRC-CCITT / CRC-16 (X.25 polynomial, computed over dst+src+
  type+data). POM68K computes it in `Scc8530::crc16x25` and appends it
  low-byte-first when it synthesizes a received frame.
- **Node IDs**: `0` invalid, **1-127 = user/workstation**, **128-254 =
  server**, **255 = broadcast**. The split lets a server (which answers
  more, so is busier) use a slower, more thorough address probe.

**LLAP type values** — POM68K refers to these by these exact names in
`Scc8530`/`LtoUdp`/`main.cpp`:

| Type | Name | Meaning |
|---|---|---|
| `0x01` | (short DDP) | DDP datagram, short header, same network |
| `0x02` | (long DDP) | DDP datagram, long header, routed |
| `0x81` | **lapENQ** | node-ID enquiry ("is this address taken?") |
| `0x82` | **lapACK** | reply to ENQ ("yes, it's mine") |
| `0x84` | **lapRTS** | request-to-send (directed data handshake) |
| `0x85` | **lapCTS** | clear-to-send (the answer) |

Control frames (`0x81`-`0x85`) are exactly 3 bytes (header only, no
data, no FCS in the payload sense) — the "high bit of the type byte set"
rule. Data frames (`0x01`/`0x02`) carry the length in the DDP header.

### 2.3 Dynamic node-ID acquisition (the ENQ dance)

At startup a node has no address. It:

1. Picks a tentative ID (from PRAM's last value, or the user/server
   range at random).
2. Broadcasts **lapENQ** to that ID, repeatedly.
3. If anyone answers **lapACK**, the ID is taken → pick another, goto 2.
4. If silence after enough tries → the ID is ours.

You see exactly this in POM68K's two-System etalon
(`llap_two_system_etalon`): each Mac sends ~650 ENQ probes and they
settle on distinct IDs. On the LToUDP capture it is the burst of
`LLAP 1->1 type=$81 lapENQ` lines.

### 2.4 Media access: CSMA/CA with RTS/CTS

LocalTalk has **no collision detection** (you cannot listen while you
drive an RS-422 pair hard). Instead it *avoids* collisions:

- **Carrier sense**: before sending, the line must be idle for the
  **Inter-Dialog Gap (IDG) = 400 µs minimum**, plus a random extra
  based on recent collision/defer history.
- **Directed frames use an RTS/CTS handshake.** Sender → **lapRTS**;
  destination must answer **lapCTS** within the **Inter-Frame Gap
  (IFG) = 200 µs maximum**; then the sender sends the data frame. Up to
  **32 retries** before failure.
- **Broadcast frames**: sender → lapRTS to node 255, waits one IFG with
  the line idle, then sends the data (nobody CTSes a broadcast).
- A "dialog" is the whole RTS/CTS/data exchange; **frames within one
  dialog are separated by ≤ IFG (200 µs); separate dialogs by
  ≥ IDG (400 µs)**.

**This is not academic for POM68K — it is the thing that broke and got
fixed twice this month:**

- The synthesized **lapCTS** the LToUDP cable answers to a directed
  lapRTS must land *inside the sender's IFG window* — hence the "express"
  path in `Scc8530::injectRxFrame` (short 4-byte-time gap, `kCtsGapBytes`).
- Every *other* injected frame must instead wait a full **IDG** so it
  starts on an idle line the driver has finished re-arming for — the
  `kIdgBytes = 12` byte-times (~417 µs) deferral, filled from the
  `rxIdle` line-idle counter (commit "incoming frames defer until the
  line has idled the LLAP IDG"). Getting this wrong is what left the
  Chooser's server list empty: afpd's `LkUpReply` arrived back-to-back
  with the router's broadcast and played into a closing FIFO.

### 2.5 Where POM68K is faithful and where it isn't

`Scc8530` models the SDLC **frame** level well (hunt/carrier-sense,
CRC-16, address search, RTS/CTS, ENQ, Tx-underrun = end-of-frame), and
per the 2026-07-22 MAME `z80scc.cpp` audit it models *more* of SDLC than
MAME does (MAME's SCC is async-serial-centric; its Send Abort, CRC
resets and hunt/sync are stubbed). What we do **not** model: the FM0 bit
clock / DPLL, per-bit timing, and the baud-rate generator (fine for
LocalTalk's fixed rate; a blocker only for async serial). See
`docs/LLE_VS_HLE.md` §3 for the gap list and the MAME line references.

---

## 3. The network and transport core

### 3.1 DDP — the datagram everything rides on

DDP is connectionless best-effort delivery — AppleTalk's IP. Two header
forms:

- **Short header (5 bytes)** — LLAP type `0x01`, used when source and
  destination are on the same network (no routing needed): length(10
  bits) + dest socket + source socket + DDP type.
- **Long/extended header (13 bytes)** — LLAP type `0x02`, used when
  routed: hop count + length + optional **DDP checksum** + dest net +
  src net + dest node + src node + dest socket + src socket + DDP type.

Max DDP data payload ≈ **586 bytes**. The optional checksum is a
rotate-and-add over the bytes after the checksum field (0 means "not
checksummed"; a computed 0 is stored as `0xFFFF`). POM68K never has to
build these — TashRouter and the guest do — but you read them constantly
when sniffing the cable.

### 3.2 RTMP and ZIP — routing and zones (what TashRouter does)

- **RTMP (Routing Table Maintenance Protocol)** — distance-vector
  routing between AppleTalk routers, split-horizon, **max 15 hops**,
  **31 = "poison"/unreachable** (`include/atalk/rtmp.h`). Routers
  broadcast their tables every 10 s; a Mac learns "who is my router" and
  its own network number from these. In the capture: `sDDP 1->1 RTMPreq`
  / `RTMP` exchanges right after the ENQ burst.
- **ZIP (Zone Information Protocol)** — maps network numbers ↔ zone
  names. `GetNetInfo`/`GetZoneList`/`GetMyZone` ops
  (`include/atalk/zip.h`: QUERY=1, REPLY=2, GNI=5, GNIREPLY=6,
  GETMYZONE=7, GETZONELIST=8, GETLOCALZONES=9). This is how the Chooser's
  zone list is populated. TashRouter answers ZIP so the guest learns it
  is in zone **"POM68K"**.

For POM68K's single-segment bridge, TashRouter is the router: it owns
RTMP/ZIP, hands the guest a network number and the "POM68K" zone, and
forwards NBP.

### 3.3 NBP — names, and therefore the Chooser

**NBP is the protocol that makes AppleTalk feel magic.** Every service
registers a **name tuple**:

```
Object : Type @ Zone           e.g.  POM68K : AFPServer @ POM68K
                                     Front Office : LaserWriter @ *
```

Each field ≤ 32 chars. NBP runs on **DDP socket 2 / DDP type 2**. Ops
(`include/atalk/nbp.h:81-91`): **BrRq=1** (broadcast request), **LkUp=2**
(lookup), **LkUpReply=3**, **FwdReq=4**, plus register/confirm.

The Chooser is *just an NBP client*:
- Click "AppleShare" → the Chooser does an NBP lookup for
  `=:AFPServer@<zone>` (`=` = wildcard object).
- Click "LaserWriter" → lookup for `=:LaserWriter@<zone>`.
- Each responder returns its tuple → its DDP address → the Chooser lists
  the *Object* names.

On the wire it is a broadcast request the router turns into per-network
lookups, and unicast replies:

```
1->254  NBP-BrRq  '='                 (guest asks the router)
254->255 NBP-LkUp '='                 (router broadcasts the lookup)
254->1  NBP-LkUpReply 'POM68K'        (afpd answers with its tuple)
```

That is literally the trace POM68K produced when the Chooser found the
server. `getzones`/`nbplkup` in `appleshare.sh`'s self-check are the
host-side NBP tools.

### 3.4 ATP — the reliable transaction (foundation of ASP and PAP)

**ATP (DDP type 3)** turns DDP's best-effort datagrams into reliable
request/response:

- **TReq** (request) → **TResp** (up to **8 response packets**, tracked
  by an 8-bit **bitmap/sequence** so lost ones are re-requested
  individually) → optional **TRel** (release).
- Two service levels: **ALO (At-Least-Once)** — retransmit until a
  response arrives, duplicates possible; **XO (Exactly-Once)** — the
  responder holds the result under the transaction ID and the **release
  timer** (`ATP_RELTIME = 30 s`, `include/atalk/atp.h`) so a retransmit
  gets the cached reply, not a re-execution. AFP writes need XO.
- Header: control-info byte (function TReq/TResp/TRel + XO + EOM + STS
  flags) + bitmap/sequence + 16-bit **transaction ID**
  (`struct atphdr`, `include/atalk/atp.h:59-63`). Max data 578 + 4 user
  bytes = **582** (`ATP_MAXDATA`).

Both file sharing (via ASP) and printing (via PAP) are just structured
conversations of ATP transactions.

---

## 4. The session and application protocols

### 4.1 ASP — sessions, and the tickle that explains the dropped mount

**ASP (AppleTalk Session Protocol)** sits on ATP and gives AFP a
long-lived, ordered session. It is asymmetric: the **workstation opens
the session and sends commands**, the server replies. Function codes
(`include/atalk/asp.h:71-78`): `CLOSE=1, CMD=2, STAT=3, OPEN=4,
TICKLE=5, WRITE=6, WRTCONT=7, ATTN=8` (ATTN = server→client attention,
e.g. "server shutting down").

**The tickle keep-alive is the key to the "connection unexpectedly
closed down" alert you hit.** In netatalk:

- The server (`afpd`) runs a tickle timer every `tickleval` seconds
  (`libatalk/asp/asp_getsess.c`). Each interval it **increments each
  session's `ac_state`** and sends an `asp_tickle()`
  (`libatalk/asp/asp_tickle.c` — a best-effort ATP request, `sreqtries
  = 1`).
- Receiving a tickle from the client **resets `ac_state` to
  `ACSTATE_OK` (1)** (`asp_getsess.c`).
- If `ac_state` reaches **`ACSTATE_BAD = 7`** — i.e. **6 consecutive
  missed tickles** (`libatalk/asp/asp_child.h:33-35`) — the parent
  **SIGTERMs the session child** → the client sees exactly *"the file
  server's connection has unexpectedly closed down."*

A tickle timeout is a real risk on a lossy cable and looks *exactly*
like this alert — so it was the first suspect for the 2026-07-22
"Input mounted then dropped" screenshot. **It was not the cause here.**
The live LToUDP capture showed the wire working perfectly (NBP lookups
answered, the reply reaching the guest node every time), and the afpd
syslog gave the real reason:

```
afpd: AFPVersion 2.1 Login by gistarcade          ← login OK
afpd: no suitable network config from CNID server (localhost:4700)
afpd: get_id: Connection to the CNID backend DB failed ... fatal error.
      Is the cnid_metad process running?
```

**The real cause was the CNID backend, not ASP.** afpd's default CNID
scheme (`dbd`) asks a **`cnid_metad`** daemon on `localhost:4700` to
spawn a per-volume `cnid_dbd` that hands out Catalog Node IDs. Our
`appleshare.sh` never started `cnid_metad`, so the instant afpd needed a
CNID (right after the volume mounted) it hit a *fatal* error and killed
the session child — which the Mac reports with the same
"unexpectedly closed down" string. The master afpd stayed up (still
NBP-registered), which is why the server kept appearing in the Chooser
and the wire looked healthy. **Fix:** `appleshare.sh` now starts
`cnid_metad` between `atalkd` and `afpd` (commit 2026-07-22). Moral: on
a "connection closed" alert, read the **afpd syslog first** — the
network path is usually fine.

**If you ever *do* hit a genuine tickle timeout** (long idle mount that
dies on a busy/lossy cable), the levers are: capture with the LToUDP
sniffer (`scratchpad/ltoudp_sniff.py`) and watch for the periodic ATP
tickle (DDP type 3) each `tickleval` s and the guest's reply — a
one-sided exchange localizes it to our Rx or Tx; a best-effort tickle
(`sreqtries=1`) that is lost in the half-duplex Rx-off window or a
FIFO-close race just goes missing, and 6 in a row drops the session.
Mitigations: keep `runQuantumWithWire`'s 1 ms slicing active while the
cable is up, ensure the IDG deferral (417 µs ≪ tickleval) never queues
the reply behind another frame, or raise `tickleval`.

### 4.2 ADSP — the full-duplex stream

**ADSP (AppleTalk Data Stream Protocol, DDP type 7)** is the one
connection-oriented *byte stream* in the suite (think TCP): full-duplex,
reliable, flow-controlled, with its own connection setup and keep-alive.
Used by things that want a pipe rather than transactions (some databases,
`ADSP`-based apps, the old AppleTalk Remote Access). AFP uses ASP, not
ADSP, so POM68K does not need it for AppleShare; noted for completeness.

### 4.3 AFP — the file service (AppleShare)

**AFP (AppleTalk Filing Protocol)** is the application protocol behind
AppleShare — the volume that mounted as *Input*. It rides ASP: login
(with a User Authentication Method — guest, cleartext, or DES random-
number exchange), then a large call vocabulary (`FPOpenVol`,
`FPGetSrvrParms`, `FPEnumerate`, `FPRead`, `FPWrite`, resource/data
fork ops, Desktop database…). The classic **empty-volume-list bug** you
hit earlier was AFP-level authorization: `FPGetSrvrParms` returned zero
volumes because the guest UNIX account could not traverse to the share —
fixed by `-guestname "$REAL_USER"` in `appleshare.sh`.

### 4.4 PAP — the printer service (the LaserWriter path)

**PAP (Printer Access Protocol)** is to printers what ASP+AFP is to
files. It rides **ATP** (DDP type 3) and finds the printer via **NBP
type "LaserWriter"**. Function codes (`include/atalk/pap.h:29-41`):
`OPEN=1, OPENREPLY=2, READ=3 (SendData), DATA=4, TICKLE=5, CLOSE=6,
CLOSEREPLY=7, SENDSTATUS=8, STATUS=9`; `PAP_MAXDATA=512`,
`PAP_MAXQUANTUM=8`.

How a print job actually flows (top-down then bottom-up):

1. **Chooser → NBP** lookup for `=:LaserWriter@zone`; user picks a
   printer; the LaserWriter driver gets its DDP address.
2. **Open**: client sends `OpenConn` (ATP) with a **connection ID**, its
   responding socket, and a **flow quantum** (how many 512-byte buffers
   it can receive — the LaserWriter uses **8**). Server replies
   `OpenConnReply` or "busy".
3. **Read-driven data pull**: PAP is *pull*, not push. Whichever side
   wants data issues a **SendData** (`READ`) credit; the other side then
   sends up to `quantum × 512` bytes as ATP `DATA` responses. To print,
   the *printer* issues SendData and the Mac streams **PostScript** back;
   the last packet carries **EOF**.
4. **Status**: `SendStatus`/`Status` returns a Pascal string ("status:
   idle" / "busy" / "%%[ PrinterError… ]%%") — that is the text the
   Chooser and PrintMonitor display, retrievable *without* opening a
   full connection.
5. **Tickle**: each side runs a **2-minute connection timer** and sends
   a **Tickle every 60 s** (ALO ATP) to prove liveness — the same
   pattern as ASP, so the same class of drop can bite printing on a
   lossy cable.
6. **Close**: `CloseConn`/`CloseConnReply`; the printer accepts the next
   job.

Note the two authentication conventions ride *inside the PostScript
stream* as `%%?BeginQuery` comments (`NoUserAuthent`, `CleartxtPasswrd`,
`RandnumExchange`) — a spooler answers them to impersonate a real
LaserWriter; a real LaserWriter passes them through.

---

## 5. End-to-end walkthroughs

### 5.1 Mounting AppleShare (what POM68K does today)

```
Chooser (guest)                 TashRouter            afpd (host)
  │  NBP BrRq =:AFPServer@POM68K ──► broadcast LkUp ──►
  │                                             ◄── LkUpReply "POM68K" (2.145)
  │  (user double-clicks POM68K, logs in as Guest)
  │  ASP OpenSession ───────── ATP TReq ─────────────►
  │                                             ◄── OpenSessionReply (session id)
  │  AFP FPLogin(Guest)/FPGetSrvrParms/FPOpenVol "Input" (ASP CMD on ATP)
  │                                             ◄── volume "Input" → mounts
  │  ⟳ ASP Tickle every tickleval s  ◄────────►  ⟳   (must keep completing!)
```

Every arrow above is DDP datagrams, in LLAP frames, over the SDLC line
`Scc8530` drives, over the `LtoUdp` multicast, through TashRouter's TAP,
into `afpd`. The mount working proves the whole path is bidirectional;
the tickle (§4.1) is the part still to harden.

### 5.2 Printing to a (CUPS-backed) LaserWriter

```
App "Print" → LaserWriter driver → Chooser's NBP-chosen printer
  → PAP OpenConn (ATP) → printer SendData → Mac streams PostScript
  → EOF → CloseConn.   The "printer" is netatalk papd, which spools the
  PostScript to CUPS (§6.2).
```

---

## 6. Bridging AppleTalk to the modern host — and to CUPS

This is the practical payoff. POM68K's guest speaks AppleTalk; the host
speaks IP/CUPS. Three pieces bridge them, and **yes, printing to CUPS
works** (both directions), via netatalk.

### 6.1 The transport bridge (already built)

- **`LtoUdp`** (`src/LtoUdp.*`, `POM68K_LTOUDP=1`) carries raw LLAP
  frames over UDP **multicast 239.192.76.84:1954** with a 4-byte sender
  tag — the Mini vMac / TashTalk "LToUDP" format. This *is* the
  LocalTalk cable, just made of UDP. `Scc8530`'s Tx frames go out as
  multicast; inbound multicast is injected as SDLC Rx frames.
- **TashRouter** (`extern/tashrouter`) is the DDP **router**: its
  `LtoudpPort` speaks our exact format, its `LinuxTapPort` bridges to a
  host TAP. It runs RTMP/ZIP/NBP so the guest gets a network number and
  the "POM68K" zone and can find host services.
- **netatalk 2.4.9** (`extern/netatalk2`) on the host TAP: `atalkd`
  (the host's AppleTalk stack/interface), `afpd` (AFP file server),
  and — for printing — **`papd`**.

`tools/netatalk2/appleshare.sh` wires all three up in order (module +
TAP, router, then `atalkd`+`afpd`).

### 6.2 Mac → CUPS: sharing a modern printer to the vintage Mac (papd)

`papd` is netatalk's **PAP server**: it registers an NBP `LaserWriter`
entity, accepts the PAP job (PostScript) from the Mac, and **spools it
to a UNIX print system — CUPS included** (`etc/papd/print_cups.c`). This
is the direction the user guessed, and it works cleanly.

Minimal `papd.conf` to export **all** CUPS queues to the Chooser:

```
cupsautoadd:op=root:
```

`cupsautoadd` (`etc/papd/main.c:767-775`) triggers
`cups_autoadd_printers()` — every CUPS queue becomes a `LaserWriter`
NBP entity, using this stanza's parameters as defaults; later stanzas
can override a single queue. Per-printer form and the useful options:

```
"Front Office":\
    :pr=hp_laserjet:\      # CUPS queue name (or |pipe-command for P_PIPED)
    :pd=/path/to.ppd:\     # PPD to advertise
    :op=root:\             # operator / job owner
    :co=media=A4 sides=two-sided:   # CUPS options passed through
```

Option keys (`etc/papd/main.c` `getprinters()`): `pr=` queue-or-pipe,
`pd=` PPD, `op=` operator, `pa=` AppleTalk address, `co=` CUPS options,
`ca=` authenticated-capture dir, `am=` UAM list. Printer flags
(`etc/papd/printer.h:55-66`): `P_PIPED`, `P_SPOOLED`, `P_CUPS`,
`P_CUPS_PPD`, `P_CUPS_AUTOADDED`.

Guest side: **LaserWriter 8** driver → pick the printer in the Chooser →
select a **Generic/plain LaserWriter PPD** (LaserWriter 7 auto-detects).
The Mac generates PostScript; `papd` hands it to CUPS; CUPS rasterizes
to whatever the real printer is (or a PDF). A vintage Mac thus prints to
a USB inkjet it has no driver for, because CUPS owns the last mile.

> **papd's own tickle/timeout:** `papd` sessions
> (`etc/papd/session.c:101-144`) use a **60 s select timeout, 3 misses =
> close**, and send `PAP_TICKLE` when idle — the same lossy-cable
> caveat as AFP applies to long print jobs.

### 6.3 CUPS → AppleTalk: printing from the host to a real LaserWriter

The reverse direction — a modern host sending to a vintage AppleTalk
LaserWriter — uses netatalk's **`pap` client** (`bin/pap/pap.c`):

```
pap -A 'PrinterName:LaserWriter@ZoneName' document.ps
```

`pap` does the NBP lookup, opens the PAP connection, and streams the
file, tracking printer status (`-s statusfile`, `-w/-W` wait-for-idle).
To make CUPS drive it, wrap `pap` in a **CUPS backend** script under
`/usr/lib/cups/backend/` (e.g. `appletalk`) that reads the job on stdin
and execs `pap` to the target NBP name; add the printer with
`device-uri appletalk://Zone/PrinterName`. (Modern CUPS ships no
AppleTalk backend — Apple removed it when it deprecated AppleTalk in
Mac OS X 10.6/10.7 — so the `pap`-wrapper backend is the route.) This is
mostly relevant if POM68K ever emulates *toward* real LocalTalk
hardware; for the emulator's own use, §6.2 (Mac→CUPS) is the interesting
half.

### 6.4 MacIP — real TCP/IP for the guest, tunneled in DDP

This is the layer that puts the emulated Mac **on the internet**: IP
datagrams ride *inside* AppleTalk (IP-in-DDP), the inverse of §6.1's
LLAP-over-UDP. Historically this is **KIP** (Kinetics IP, for the
FastPath LocalTalk↔Ethernet gateways), later standardized as **MacIP**;
Open Transport calls it "AppleTalk (MacIP)". It exists precisely for our
situation: a Mac whose only network is LocalTalk, wanting TCP/IP.

**Top-down:** the user opens the TCP/IP control panel, picks "Connect
via: AppleTalk (MacIP)" in zone POM68K, and Netscape loads
http://frogfind.com. **Bottom-up:** each IP packet is the payload of one
DDP datagram of **type 22**, unicast between the guest and a *MacIP
gateway* node; the gateway moves them to a host `tun` device where
Linux routes and NATs them.

The gateway is the vendored **macipgw** (`extern/macipgw`, Stefan
Bethke 1997 / Jason King's Linux port — see `POM68K_VENDOR.md` there),
built against the vendored netatalk 2.4.9 libatalk
(`tools/macip/build_macipgw.sh`) and run next to `afpd` on the host
AppleTalk stack. The protocol, all of it (`extern/macipgw/macip.c`,
verified 2026-07-23):

- **Finding the gateway — NBP again.** The gateway registers
  `<gw-ip>:IPGATEWAY@<zone>` (`macip.c:71-72,654-670`); the client's
  MacIP stack does an NBP lookup for type `IPGATEWAY` in its zone.
  That is the *whole* discovery story — no magic, same LkUp/LkUpReply
  frames as the Chooser.
- **Getting an address — one ATP transaction.** The client sends an ATP
  request to the gateway's **DDP socket 72**: function 1 = *assign me
  an address* (the reply carries IP, name server, broadcast, netmask —
  `struct macip_req`, `macip.c:87-104,398-415`), function 3 = *are you
  a MacIP server* (probe). DHCP in one round-trip, years before DHCP.
- **"ARP" — NBP a third time.** Each client with an address registers
  `<client-ip>:IPADDRESS@<zone>`; the gateway resolves IP→AppleTalk
  address by looking up the dotted-quad *as an NBP object name* of type
  `IPADDRESS` (`arp_lookup`, `macip.c:224-244`) and also learns
  mappings from inbound packets. Idle leases are probed with ICMP echo
  every 30 s and reclaimed after 10 misses (`macip.c:79-81,604-626`).
- **Data — DDP type 22**, max 586 bytes of IP per datagram, so the tun
  MTU is 586 (`tunnel_linux.c`). TCP MSS adapts; nothing fragments.

**What runs where:**

```
guest Mac OS (OT or MacTCP + AppleTalk on)          host
  IP in DDP-22 ── LLAP/Scc8530 ── LtoUdp ── TashRouter ── pomtap0
                                                            │ kernel DDP
                                             macipgw (NBP IPGATEWAY, ATP :72)
                                                            │ tunX  192.168.151.1/24, MTU 586
                                             Linux ip_forward + MASQUERADE → internet
```

**Bring-up** (extends §6.1's bridge):

```bash
sudo tools/netatalk2/appleshare.sh --macip     # bridge + MacIP in one go
# or, bridge already up:
tools/macip/build_macipgw.sh                   # once, no sudo
sudo tools/macip/macip.sh                      # start · stop to undo
```

`macip.sh` makes exactly three host changes, echoes each, and reverses
all of them on `stop`: `net.ipv4.ip_forward=1` (previous value saved),
three `iptables` rules tagged `pom68k-macip` (MASQUERADE for the MacIP
subnet + two FORWARD accepts), and the `macipgw` process itself (its
tun device disappears with it). Tunables: `MACIP_NET`/`MACIP_MASK`
(default 192.168.151.0/24 — gateway .1, clients .2-.254; must not
collide with a real host network), `MACIP_DNS` (default 8.8.8.8 —
must be a resolver reachable *through the NAT*, so never the systemd
stub 127.0.0.53), `MACIP_ZONE` (POM68K), `MACIP_DEBUG`.

**Guest configuration — Mac OS 8.1 / Open Transport (Quadra 605):**

1. AppleTalk must already be up (boot with `POM68K_LTOUDP=1
   POM68K_APPLETALK=1`, the §6.1 bridge running).
2. **TCP/IP control panel** → *Connect via:* **AppleTalk (MacIP)** →
   *Configure:* **Using MacIP Server**.
3. *MacIP server zone:* **Select Zone…** → **POM68K** (the gateway is
   found by NBP in that zone).
4. *Name server addr:* type the DNS the gateway advertises (default
   **8.8.8.8**) — the assignment reply carries it too, but filling it
   in removes one variable. *Search domains* can stay empty.
5. Close, save. The first TCP/IP user (e.g. Netscape) triggers the
   NBP lookup + ATP assign; `run/macipgw.log` shows the lease
   (`MACIP_DEBUG=0x1111` logs `assigned 192.168.151.2`).

**Guest configuration — System 7.5 / classic MacTCP (LC II, Plus,
Mac II):** MacTCP ships on the System 7.5 install media (Networking
extras).

1. **MacTCP control panel** → click the **LocalTalk** icon; in the
   zone list under the icons pick **POM68K**.
2. **More…** → *Obtain Address:* **Server** (that is MacIP
   server-assigned mode; leave Gateway Address alone).
3. *Domain Name Server Information:* domain `.`, IP **8.8.8.8**,
   *Default* checked.
4. OK, close, **reboot** (classic MacTCP only reads its config at
   startup).

**Era caveats — the web side.** The stack is genuine TCP/IP, but a 1996
browser cannot shake hands with 2026 TLS: **plain HTTP only**. Practical
destinations: <http://frogfind.com> (search + readability proxy, made
for this), <http://theoldnet.com> (archived-web proxy, can serve
year-matched pages), or a self-hosted down-converting proxy (WebOne,
macproxy) on the host as a follow-up. Netscape 2-4 on OS 8.1 and
Netscape 2/MacWeb on 7.5 all work against these. Expect the odd 4xx
from sites that never learned HTTP/1.0.

**Troubleshooting** (host side unless noted):

| Symptom | Check |
|---|---|
| Guest OT/MacTCP "can't find MacIP server" | `nbplkup "=:IPGATEWAY@POM68K"` must list the gateway. Empty → `run/macipgw.log` (NBP registration retries 5×5 s; `atalkd` running? started *after* the router?) |
| Gateway registered but no address assigned | `MACIP_DEBUG=0x1111`, restart `macip.sh`, watch for `MacIP req`/`assigned` in `run/macipgw.log`; sniff the LToUDP cable (`scratchpad/ltoudp_sniff.py`) for ATP to socket 72 |
| Guest has an IP but nothing loads | `ping 192.168.151.2` from the host (works once the guest holds the lease); `tcpdump -i tunX -n` (guest packets must appear); `iptables -t nat -S \| grep pom68k`; `sysctl net.ipv4.ip_forward` |
| Sites fail by name, work by IP | DNS: advertised server must be a real resolver reachable through NAT (never 127.0.0.53); try `MACIP_DNS=1.1.1.1` and re-enter it guest-side |
| `https://` anything | Expected failure — TLS era gap, use FrogFind / theoldnet (above) |
| Large transfers stall | Check tun MTU is 586 (`ip link show tunX`) — DDP's payload ceiling |

---

## 6.5 The in-process stack — POM68K as its own router, server, printer and gateway

Everything in §§3–6 above describes the **external** bridge: TashRouter
routes, netatalk's `afpd`/`papd` serve, `macipgw` tunnels, and
`appleshare.sh`/`macip.sh` wire them up (needing root). That path is
still here and is the way to reach a **real** LocalTalk network. But as
of 2026-07-24 POM68K also carries the **entire service side in-process**,
on the same `Scc8530` wire the guest drives — no external processes, no
root, on by default in the GUI. It is the practical default; the
external chain is for interop.

**What runs where (internal path):**

```
guest Mac OS                                   POM68K process
  LLAP/DDP ── Scc8530 wire ── AtalkHub ── AtalkStack (node 2.128, zone POM68K)
                                            ├── router-lite: RTMP/ZIP/NBP/AEP
                                            ├── AfpServer   (NBP AFPServer,  ASP/AFP → host folder)
                                            ├── PapServer   (NBP LaserWriter, PAP → lp/CUPS or .ps file)
                                            └── MacIpGateway (NBP IPGATEWAY, ATP :72, DDP-22 → user-mode NAT → host sockets)
```

The internal node is a real, terminated LocalTalk peer: it answers the
guest's ENQ address probes (defending its own node 128), so the guest
settles on a different ID exactly as against a hardware peer. When
`POM68K_LTOUDP=1` is *also* set, the internal node's frames are
multicast onto the cable too — the emulator then looks like one more
node on the shared virtual LocalTalk, and external TashRouter/netatalk
can coexist (the guest simply sees two responders and de-dups).

**Mapping the layer cake (§1.1) to the internal owners:**

| AppleTalk layer | Internal owner | File |
|---|---|---|
| LLAP node presence, DDP short/long | `AtalkStack` | `src/AtalkStack.*` |
| RTMP (beacon + req/resp), ZIP (GetNetInfo/ZoneList), NBP (registry + LkUp + BrRq relay), AEP echo | `AtalkStack` router-lite | `src/AtalkStack.*` |
| ATP (responder XO cache + release timer; requester retries) | `AtalkStack::AtpTxn` / `atpRequest` | `src/AtalkStack.*` |
| ASP sessions + AFP 2.1 file service, `.AppleDouble` sidecars | `AfpServer` | `src/AfpServer.*` |
| PAP printer → CUPS (`lp`) or `.ps` spool | `PapServer` | `src/PapServer.*` |
| MacIP (ATP :72 assign, IP-in-DDP-22) + user-mode NAT (TCP-lite/UDP/ICMP) | `MacIpGateway` | `src/MacIpGateway.*` |
| Wiring to the SCC + status/toggles for the GUI | `AtalkHub` | `src/AtalkHub.h`, `src/main.cpp` |

**Faithfulness vs the external stack.** The internal path is a
pragmatic subset, not a second netatalk. It implements the transactions
a period Finder/Chooser/MacTCP actually issues; it does **not** do ADSP,
the Desktop database (OpenDT answers "no item"), AFP ≥ 3.0/UTF-8,
authenticated UAMs beyond guest/cleartext, PAP status polling subtleties,
or MacIP outbound ICMP/raw sockets. CNIDs are per-run (root = 2), so
they are stable within a session but not persisted across restarts. For
anything the subset misses — or to serve a real LocalTalk segment —
run the external bridge (`POM68K_LTOUDP=1` + `appleshare.sh`), which the
internal node coexists with. Migration notes and the gap list live in
`docs/LLE_VS_HLE.md`.

**Using it.** Nothing to start: launch the GUI, open **Réseau →
AppleTalk**. The window shows AppleTalk on/off, the router zone + guest
node + traffic counters, AppleShare (registered? folder writable? name,
sessions, last user/command, bytes), the printer (idle/busy, jobs, last
job path), and MacIP (gateway visible? a lease attributed means it
*works*; IP counters, live flows) — each service has a live toggle.
Guest-side setup is identical to §6 (Chooser → AppleShare / a
LaserWriter; TCP/IP or MacTCP control panel → MacIP server, zone
POM68K). Env: `POM68K_APPLETALK=0` disables the whole stack;
`POM68K_SHARE_DIR=/path` sets the shared folder (default `./AppleShare`,
created if absent).

## 7. Map back to POM68K code

| AppleTalk layer | POM68K / host component | File |
|---|---|---|
| LocalTalk physical + SDLC framing | `Scc8530` SDLC engine, CRC-16, hunt/RTS/CTS/ENQ, IDG/IFG pacing | `src/Scc8530.{h,cpp}` |
| LocalTalk "cable" | LToUDP multicast, 4-byte tag | `src/LtoUdp.{h,cpp}`, `POM68K_LTOUDP=1` |
| LLAP dialogue glue (RTS→CTS synth, per-frame poll) | GUI wire pump, sub-frame slicing | `src/main.cpp` (`wireLocalTalk`, `runQuantumWithWire`) |
| PRAM AppleTalk-active seed (SPConfig `$21`) | `POM68K_APPLETALK=1` | `src/Egret.cpp`, `src/Rtc.cpp` |
| DDP / RTMP / ZIP / NBP routing | TashRouter | `extern/tashrouter` |
| ATP / ASP / AFP file service | netatalk `afpd` | `extern/netatalk2` |
| PAP print service → CUPS | netatalk `papd` (`cupsautoadd`) | `extern/netatalk2/etc/papd` |
| MacIP (IP-in-DDP → tun + NAT) | vendored `macipgw` gateway | `extern/macipgw`, `tools/macip/{build_macipgw,macip}.sh` |
| Bring-up / teardown | one-command bridge (`--macip` adds TCP/IP) | `tools/netatalk2/appleshare.sh` |
| Wire debugging | LToUDP sniffer (DDP/NBP decoder) | `scratchpad/ltoudp_sniff.py` |

**Gates** that exercise this stack: `llap_loop_test` (RTS/CTS, ENQ,
address filter, express CTS, carrier sense), `llap_two_system_etalon`
(two Macs acquire node IDs over real ENQ traffic), `ltoudp_test` (the
multicast cable). For the **in-process stack** (§6.5):
`atalk_stack_test` (ENQ defence, RTMP/ZIP/NBP/AEP, ATP exactly-once),
`afp_server_test` (OpenSession→Login→OpenVol→Enumerate→Read, the ASP
SPWrite→WriteContinue→FPWrite round-trip, resource fork →
`.AppleDouble`), `pap_server_test` (OpenConn→SendData→PostScript→EOF→
spool, query `*` answer), `macip_gw_test` (address assign + ICMP echo +
a real UDP round-trip and a full TCP SYN→data→FIN both ways through the
user-mode NAT on loopback).

---

## 8. Quick reference tables

**LLAP types:** `01` short DDP · `02` long DDP · `81` ENQ · `82` ACK ·
`84` RTS · `85` CTS.
**Node IDs:** `0` invalid · `1-127` user · `128-254` server · `255`
broadcast.
**LLAP timing:** bit rate 230.4 kbit/s · IDG ≥ 400 µs · IFG ≤ 200 µs ·
32 retries.

**DDP well-known sockets:** `1` RTMP · `2` NBP · `4` AEP(echo) · `6` ZIP ·
`72` MacIP config (ATP).
**DDP protocol types:** `1` RTMP-RD · `2` NBP · `3` ATP · `4` AEP ·
`5` RTMP-R · `6` ZIP · `7` ADSP · `22` MacIP (IP-in-DDP).
**MacIP NBP types:** gateway = `IPGATEWAY` · client = `IPADDRESS`
(object = the dotted-quad IP). Assign = ATP func 1, server probe = 3.
**DDP:** short hdr 5 B · long hdr 13 B · max data ~586 B · address =
net(16)·node(8)·socket(8).

**ATP:** DDP type 3 · TReq/TResp/TRel · ≤8 response pkts (bitmap) ·
ALO/XO · release timer 30 s · max data 582 B.
**ASP funcs:** CLOSE 1 · CMD 2 · STAT 3 · OPEN 4 · TICKLE 5 · WRITE 6 ·
WRTCONT 7 · ATTN 8. **Timeout: 6 missed tickles → session killed.**
**NBP ops:** BrRq 1 · LkUp 2 · LkUpReply 3 · FwdReq 4. Name
`Object:Type@Zone`, ≤32 ch each.
**PAP funcs:** OPEN 1 · OPENREPLY 2 · READ/SendData 3 · DATA 4 ·
TICKLE 5 · CLOSE 6 · CLOSEREPLY 7 · SENDSTATUS 8 · STATUS 9 · quantum 8 ·
512 B/pkt · NBP type "LaserWriter".
**ZIP ops:** QUERY 1 · REPLY 2 · GNI 5 · GNIREPLY 6 · GETMYZONE 7 ·
GETZONELIST 8. **RTMP:** max 15 hops · 31 poison. **AARP:** REQUEST 1 ·
RESPONSE 2 · PROBE 3.

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
- **netatalk 2.4.9 source vendored in `extern/netatalk2/`** (verified
  `file:line` 2026-07-22): `include/atalk/{ddp,atp,asp,nbp,zip,rtmp,pap}.h`,
  `libatalk/asp/{asp_getsess.c,asp_tickle.c,asp_child.h}` (tickle/timeout),
  `etc/papd/{main.c,session.c,print_cups.c,printer.h}` (papd + CUPS),
  `bin/pap/pap.c` (pap client).
- MacIP: **macipgw source vendored in `extern/macipgw/`** (Stefan Bethke
  1997, Linux port <https://github.com/zero2sixd/macipgw>, provenance in
  `POM68K_VENDOR.md` there — `macip.c` *is* the MacIP wire spec in
  practice); the macip.net appliance (same code) for cross-checking;
  FrogFind <http://frogfind.com> / theoldnet <http://theoldnet.com> for
  the HTTP-only era gap.
- POM68K itself: `docs/LLE_VS_HLE.md` §3 (SCC gaps + MAME `z80scc.cpp`
  audit), `CHANGELOG.md` (LLAP milestone 1, SCC IDG fix, AppleShare
  bridge), `src/Scc8530.*`, `src/LtoUdp.*`.
