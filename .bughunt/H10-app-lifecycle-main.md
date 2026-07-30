# H10-app-lifecycle-main — verified findings

### HIGH — `DafbMachine::applyCmds` reads/clears `floppyPending_` outside `cmdMu_` (data race → use-after-free)
- **Where:** `src/main.cpp:2924` (also `:2926`), against writer `src/main.cpp:2588-2592`
- **Defect:** The machine thread dereferences and clears the plain `std::string floppyPending_` after releasing `cmdMu_`, while the GUI thread reassigns it under the lock, so a second floppy insert can free the buffer being read.
- **Trigger:** Quadra 605 / Centris / Q700 / Q630, Disques → Floppy: click image A then image B within one quantum. Machine thread has already done `cmdsApply_.swap(cmds_)` and is inside `mem.insertDisk(floppyPending_)`; GUI thread runs `floppyPending_ = std::move(path)` for B, destroying A's heap buffer → dangling `const char*` into `std::ifstream`, garbage path or crash. The unlocked `.clear()` also races and can silently drop B.
- **Fix:** move the string out under the same lock:
```cpp
std::string pending;
{ std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); pending.swap(floppyPending_); }
// ... case Cmd::InsertFloppy: if (!pending.empty() && mem.insertDisk(pending)) ...
```

### HIGH — Host wall-clock RTC seed goes to the *inactive* HLE MCU; every firmware-LLE machine boots at the 1904 epoch
- **Where:** `src/main.cpp:1394` (same at `:1876`, `:2138`, `:2382`, `:3035`, `:3922`)
- **Defect:** `mem.egret().setSeconds(hostMacSeconds())` / `mem.cuda().setSeconds(...)` write the HLE `Egret` object, but `V8Memory::egret()` (`src/V8Memory.h:108`) / `Q605Memory::cuda()` (`src/Q605Memory.h:81`) / `Q630Memory::cuda()` (`src/Q630Memory.h:90`) are unconditional, while the default path is the firmware LLE, whose clock lives in `M68hc05` RAM and is never seeded.
- **Trigger:** Any machine with `roms/egret/` or `roms/cuda/` present (the documented default): every other host-facing accessor branches on the LLE flag — `pramByte`/`setPramByte` (`V8Memory.h:154-161`), `loadPram`/`savePram` (`:163-173`), `keyEvent`/`mouseMove` (`:175-187`), `cpuHeld` (`:145`) — but the clock does not. `CudaLle` has no seconds hook at all (`src/CudaLle.h:64-65`; `CudaLle.cpp:150-156` stages only PRAM $100-$1FF into `M68hc05::ram_`, `M68hc05.h:109 uint8_t ram_[0x200] = {}`). The guest asks the MCU for the RTC and gets the firmware's un-seeded counter → 1 Jan 1904 on every launch; the seconds written back to `<disk>.pram` are likewise the untouched HLE copy.
- **Fix:** add a seconds stage to `CudaLle` (installed at reset release next to `pramInstalled_`), expose `setRtcSeconds(uint32_t)` on `V8Memory`/`SonoraMemory`/`VaspMemory`/`Q605Memory`/`Q630Memory` branching on the LLE flag exactly like `loadPram`, and call that from main.cpp instead of `egret()/cuda().setSeconds`.

### HIGH — `POM68K_CENTRIS_FPU` is set but never unset; a Quadra→Centris switch boots the Centris as a full 68040
- **Where:** `src/main.cpp:3278`
- **Defect:** `if (q650 || q610 || q800) setenv("POM68K_CENTRIS_FPU", "1", 1);` has no `else unsetenv`, and the Machine menu relaunches via `execv` (`main.cpp:635`), which inherits the environment; only `POM68K_CENTRIS_MODEL` is rewritten.
- **Trigger:** Machine → "Macintosh Quadra 650" (sets the FPU flag), then Machine → "Macintosh Centris 650". `CentrisCpu.cpp:12-19` keys on mere presence: `if (getenv("POM68K_CENTRIS_FPU")) { setModel(M68040); setFPUModel(M68882); }` — so the Centris runs as a full 68040 while `main.cpp:3286-3287` prints "68LC040", diverging from the documented profile and from the FPU-absent SANE/PACK-4 branch. No recovery short of relaunching from a clean shell. The author already handles the symmetric case for the sibling knob (`main.cpp:595-598`, `unsetenv("POM68K_Q605_NOFPU")`).
- **Fix:** `if (q650 || q610 || q800) setenv("POM68K_CENTRIS_FPU","1",1); else unsetenv("POM68K_CENTRIS_FPU");`

### MEDIUM — GUI floppy swap inserts over a dirty image without ejecting: committed writes are dropped
- **Where:** `src/main.cpp:2923-2926`
- **Defect:** `Cmd::InsertFloppy` calls `mem.insertDisk(path)` on a drive that may still hold a dirty image; `SonyDrive::insert` (`SonyDrive.cpp:119-137`) → `insertImage` (`:140-152`) does `path_.clear(); dirty_ = false; image_ = std::move(data);` with no `flushToFile()`, while `eject()` (`SonyDrive.cpp:101-104`) *does* flush.
- **Trigger:** Insert A, let the guest write (dirty_=true, path_=A), then pick B from Disques → Floppy. A's modified sectors are discarded, violating the stated contract at `main.cpp:3062-3064` ("persist committed writes back to the image file on eject and on exit").
- **Fix:** eject first in the handler — `mem.ejectDisk();` immediately before `mem.insertDisk(...)` — or make `SonyDrive::insert()` call `flushToFile()` before replacing `image_`/`path_`.

### MEDIUM — `DafbMachine::kFrame` hardcodes a 25 MHz quantum for the 20 MHz Centris 610 and the 33 MHz Quadra 650/800/630
- **Where:** `src/main.cpp:2867`
- **Defect:** `static constexpr int kFrame = 416667;` is shared by all four instantiations (`main.cpp:2956-2959`: `Q605Memory` 25 MHz, `CentrisMemory` 20/25/33.33 MHz, `Q700Memory` 25 MHz, `Q630Memory` 33 MHz — `Q630Memory.h:64 kCpuHz = 33000000`, `CentrisMemory.h:53-56`).
- **Trigger:** Untick Turbo on the Quadra 630: `stepTick` runs exactly `kFrame` cycles then sleeps `16625 - spent` µs (`main.cpp:2648-2663`), so the guest advances 25 M cycles per wall second instead of 33 M — ~24 % slow, and the same wrong budget feeds `mem.tick(kFrame)` during the Cuda power-on hold and the LLE MCU seconds counter (the drift class already gated against). The Centris 610 (20 MHz) errs the other way, ~25 % fast. The sibling wrappers in the same file do it right: `main.cpp:1757 const int64_t kFrame = mem.cpuHz() / 60;`, `main.cpp:1220-1221`, `main.cpp:790`; so does the gate (`tests/q630_boot_etalon.cpp:156`, 550000).
- **Fix:** add `int64_t cpuHz() const` to `Q605Memory`/`CentrisMemory`/`Q700Memory`/`Q630Memory` (as `SonoraMemory.h:75` has) and use `const int64_t kFrame = mem.cpuHz() / 60;`.

### MEDIUM — `wireLocalTalk` byte pace hardcoded to a 25 MHz constant on machines that are not 25 MHz
- **Where:** `src/main.cpp:2110` (VASP), `:1335` (V8 incl. Mac TV), `:1848` (Sonora incl. LC III+/550/CC II), `:3296` (Centris/Quadra 610-800)
- **Defect:** the argument must equal `cpuHz / 28800`, and `wireLocalTalk` reconstructs the machine clock from it: `int64_t hubHz = int64_t(byteCycles) * 28800; // real machine clock` (`main.cpp:119`), then `g_atalk.attach(mem, hubHz, ...)` (`:159`), which is `AtalkStack::configure`'s `cpuHz` ("cpuHz drives the stack's second-scale timers", `AtalkHub.h:47-54`). `runVasp` passes 868 for both `kCpuHzVx = 31334400` and `kCpuHzVi = 15667200` (`VaspMemory.h:44-45`, selected at `main.cpp:2102-2104`); `runLcII` passes 544 for the 31.3344 MHz Mac TV (`V8Memory.h:45`); `runLc3` passes 868 for the 33.33 MHz variants; `runCentris` passes 868 for the 20 MHz Centris 610 and the 33.33 MHz Quadra 650/800.
- **Trigger:** Mac IIvi with the default in-process AppleTalk: `hubHz = 24 998 400` against a real 15 667 200 Hz machine → every ATP retransmit window, ASP tickle and RTMP age expires ~60 % early; the IIvx is 20 % late, the Mac TV 2× off. `setWirePace(byteCycles/boost)` (`main.cpp:128`) and the SDLC fallback pace (`Scc8530.h:236`) are skewed by the same factor. `main.cpp:2347` (`iici ? 868 : 694`) and `:3875` (1146 @ 33 MHz) show the per-clock derivation is the intended contract.
- **Fix:** derive it — pass `int(cpuHz / 28800)` at each site (VASP: `vi ? 544 : 1088`; V8: `int(mem.cpuHz()/28800)`; Sonora: `int(pr.cpuHz/28800)`; Centris: `int(cinfo.hz/28800)`), or compute it inside `wireLocalTalk` from the machine's clock.

### MEDIUM — Machine menu: "Macintosh II" is permanently "current", so it can never be selected (and blocks nothing else from being checked)
- **Where:** `src/main.cpp:546` (profile row) + `:581-591` (selection logic)
- **Defect:** `variantCur` defaults to `true` when `envKey == nullptr`, and `runMacII` uses one `MachineKind::MacII` for II/IIx/IIcx (`main.cpp:928`), so on any Mac II-family machine the "Macintosh II" row computes `isCur = true` and `if (ImGui::MenuItem(...) && !isCur)` swallows the click.
- **Trigger:** Running the default IIx (`POM68K_MACII_MODEL` unset): both "Macintosh II" (envKey null → variantCur true) and "Macintosh IIx" (`dflt = true`) show a checkmark and neither is clickable; from the IIcx, "Macintosh II" is still uncheckable. With both ROMs present the plain Mac II is unreachable from the documented switching mechanism. Every other multi-profile kind gives an `envKey` to all its rows.
- **Fix:** tag the plain row — `{ kGlue, "Macintosh II", MachineKind::MacII, "roms/macii.rom", "9779D2C4", "POM68K_MACII_MODEL", "fdhd", false }` (`main.cpp:4277` already accepts `"fdhd"`), or compare the loaded ROM signature in `isCur`.

### LOW — `MacAudioHost` has no destructor: on the `exit()` path the realtime callback outlives the `FloppySound` globals
- **Where:** `src/MacAudioHost.h:35` (only teardown; no `~MacAudioHost`)
- **Defect:** `void stop() { if (started_) ma_device_uninit(&device_); started_ = false; }` is the sole uninit and is only reached on the normal return path (`main.cpp:1049/1631/2078/…`). On `exit()` the function-local `static MacAudioHost audioHost` is destroyed with the device still running, and the namespace-scope `gFloppySfx`/`gHddSfx` (`main.cpp:358-359`, constructed pre-main, destroyed *after* it) are freed under the still-live callback, which dereferences them unconditionally (`MacAudioHost.h:131-135 fx->fillAudioBuffer(buf, int(n))`).
- **Trigger:** Xlib's default error handler calls `exit()` behind GLFW's back — the exact case the machine wrapper already guards (`main.cpp:1078-1081`). miniaudio's playback thread then reads `s.data[k]` out of freed sample buffers.
- **Fix:** `~MacAudioHost() { stop(); }` (and null the `fx_` slots in `stop()`); `audioHost` is constructed after the globals, so it is destroyed first.

### LOW — `MacAudioHost::start()` leaks an initialized `ma_device` when `ma_device_start` fails
- **Where:** `src/MacAudioHost.h:31-35`
- **Defect:** `started_ = ma_device_start(&device_) == MA_SUCCESS;` leaves a successfully `ma_device_init`'d device with no owner when start fails, and `stop()` is gated on `started_`, so `ma_device_uninit` is never called — backend handles/thread leak for the process lifetime.
- **Trigger:** ALSA/Pulse device present but busy or refusing the 22254 Hz f32 config; all callers only print "audio: no output device (silent)".
- **Fix:**
```cpp
if (ma_device_start(&device_) != MA_SUCCESS) { ma_device_uninit(&device_); return false; }
started_ = true; return true;
```

### LOW — AppleTalk window reads live `Scc8530` receive-queue state from the GUI thread
- **Where:** `src/main.cpp:408` → `AtalkHub.h:150-151` → `AtalkHub.h:65-69` → `Scc8530.h:132-135`
- **Defect:** `appleTalkWindow()` calls `g_atalk.snapshot()` every GUI frame; `snapshot()` invokes `wire_()`, which does `scc.rxBacklog(0)` = `ch_[0].rxQueue.size()`. `AtalkHub::mu_` serializes hub-side injections but the machine thread pops from `rxQueue` inside `cpu.runCycles`/the SDLC Rx engine without any lock.
- **Trigger:** Open Réseau → AppleTalk during an AppleShare copy: unsynchronized read of a `std::deque` mid-`pop_front` → nonsense backlog values displayed, formally UB. The file's own contract says these are machine-thread bound (`main.cpp:88-90`).
- **Fix:** sample the four counters into `std::atomic` fields inside `AtalkHub::tick` (already machine-thread bound) and have `snapshot()` read those instead of touching the SCC.

### LOW — GUI thread reads `monitorSense()` directly while the machine thread writes it
- **Where:** `src/main.cpp:1597` (and the same pattern at `:2044`)
- **Defect:** `int sense = c.m.mem.monitorSense();  // byte read; only the GUI changes it` — the comment is wrong: the write happens on the machine thread at `main.cpp:1251` (`case Cmd::Sense: mem.setMonitorSense(uint8_t(c.a)); cpu.hardReset();`), and `V8Memory.h:199-203` shows it is a multi-field non-atomic update (`vidSpram_`, `vidSpramSaved_`, `montype_`).
- **Trigger:** Click "512x384": the GUI polls `montype_` every frame while the machine thread mutates it — torn/hoisted read (the highlighted button can stop updating), formally a data race. Every other machine→GUI value crosses as a relaxed atomic in `publish()` (`main.cpp:1208-1214`).
- **Fix:** publish the sense byte from the machine thread into the existing `Status` atomics and read it from there.

### LOW — PRAM battery files collide across machine profiles
- **Where:** `src/main.cpp:1389-1390`, `:1873-1874`, `:3030-3031`
- **Defect:** the V8 family, the Sonora family and the Quadra 605 all name the file `<boot image>.pram` with no machine tag, while every later profile adds one — `.iivx.pram` (`:2135-2136`), `.iici/.iisi.pram` (`:2374-2377`), `.centris.pram` (`:3338-3339`), `.q700.pram` (`:3627-3628`), `.q630.pram` (`:3917-3918`).
- **Trigger:** One shared boot volume (e.g. the generic `hdv/boot.vhd`): the Quadra 605 writes its Cuda XPRAM to `hdv/boot.vhd.pram`; switching to the LC II loads that same file into the Egret. `Egret::loadPram` (`Egret.cpp:27-36`) validates nothing — 256 raw bytes plus a seconds longword — so the LC II starts from a Quadra's video/boot/32-bit-mode XPRAM. Same collision inside `runLc3` (Egret LC III vs Cuda AIO) and `runLcII` (Egret LC II vs Cuda Color Classic/Mac TV).
- **Fix:** tag them like the newer paths — `hddPath + "." + prof.shortName + ".pram"` in `runLcII`, a per-model slug in `runLc3`, `hddPath + ".q605.pram"` in `runQuadra`.
