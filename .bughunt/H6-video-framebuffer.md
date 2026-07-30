### high — VaspVideo::decode reads up to ~730 KB past the end of the 1 MB VRAM vector at monitor sense 1
- **Where:** `/home/gistarcade/src/POM68K/src/VaspVideo.h:47` (also 55, 63, 71)
- **Defect:** `resolution()` admits montype 1 → 640×870, but the 2048-byte-pitch indexing into a fixed 1 MB `std::vector` is unbounded in every branch except 16 bpp.
- **Trigger:** `POM68K_SENSE=1 ./iivx_boot_etalon` — `tests/iivx_boot_etalon.cpp:79-80` reads that env var (its header documents senses 1/2/6) and `VaspMemory::setMonitorSense` (`src/VaspMemory.h:142`) applies no clamp; line 107-109 then calls `video.decode(fb)`. At 1 bpp the last read is `vram[869*2048 + 79]` = byte 1 779 791 vs `VaspMemory::kVramSize` = 1 048 576 (`src/VaspMemory.h:43`) → 731 215 bytes of heap over-read; at 8 bpp `vram[869*2048 + 639]` = 1 780 351. Only case 4 is guarded (`VaspVideo.h:78`), and the sibling `V8Video.h:90-93` documents exactly this contract ("reachable via setMonitorSense … so bound the read").
- **Fix:** bound every branch like the 16 bpp one, or reject the mode up front:
  ```cpp
  // in decode(), after resolution():
  if (size_t(vres) * 2048 > VaspMemory::kVramSize) { std::fill(out.begin(), out.end(), 0); return; }
  ```
  `V8Video.h:62/70/78/86` has the identical unguarded shape with `*1024` against `kVramSize = 0x80000`; it is latent only because the GUI (`src/main.cpp:1604-1608`) offers senses 2 and 6 only.

### high — TobyVideo RAMDAC decoded on the wrong address bits; the real Apple TFB driver's CLUT programming is silently dropped
- **Where:** `/home/gistarcade/src/POM68K/src/TobyVideo.cpp:97` (and the mirror at `:42`)
- **Defect:** MAME maps the Bt453 with `.umask32(0xff000000)` over `0x090000-0x09001f`, so the register index is the 32-bit word index `(byteaddr-0x90000)>>2`; POM68K uses `(byteaddr-0x90000) & 3`, which decodes a lane the hardware does not use.
- **Trigger:** verified against the shipped decl ROM `tests/data/342-0008-a.bin` (loaded by `MacIIMemory::installTobyVideo`, `src/MacIIMemory.cpp:42-66`). Descrambled (reverse + invert, byteLanes `$E1`) and disassembled, the .Display_Video_Apple_TFB driver does at logical offset `0x00aa`:
  ```
  00aa  adda.l  #$90000,a0
  00b0  move.b  #$ff,$1c(a0)     ; DAC address  -> $9001C
  00b6  adda.w  #$18,a0          ; a0 = $90018
  00bc  move.w  #$17f,d0
  00c0  move.b  d1,(a0)          ; 384 palette bytes -> $90018 (128 entries)
  ```
  plus `05ca move.b d1,$1c(a3)` / `05d4 move.b d1,(a3)` at `a3=$90018`, and `049e adda.l #$90018,a3; 04b2 move.b d1,$4(a3)`. Every one of those byte offsets has `addr & 3 == 0`, so `int o = int(r - 0x90000) & 3` yields 0, which matches neither `o==1||o==3` nor `o==2` — `write8` falls through and returns. `pens_` therefore keeps its reset fill (`TobyVideo.cpp:14-16`: all `0x00FFFFFF`, `[1]`=black), so selecting 4-bit or 256-colour in Moniteurs — now reachable since the TFB byte register path was added at `TobyVideo.cpp:83-93` — renders an all-white screen. `read8` at `$9001C` likewise returns 0 instead of the address register. `docs/LLE_VS_HLE.md:336` classifies TobyVideo as "register/CLUT/sense faithful", contradicting this.
- **Fix:** decode like MAME in both `read8:42` and `write8:97`:
  ```cpp
  if ((r & 3) != 0) return;                 // write8 (read8: return 0)
  int o = (int(r - 0x90000) >> 2) & 3;
  ```
  Also drop the `dacWrite_` gate on `o==2` (MAME's `palette_w` is unconditional). This must land with the pen-index/polarity item below, and `tests/toby_test.cpp:28-29` (which writes `base+0x90002`) has to be retargeted to `$9001C`/`$90018` — it currently locks the buggy decode in.

### medium — the 040 host decoder silently renders the 16 bpp and 24 bpp modes the hardware can select as 1 bpp
- **Where:** `/home/gistarcade/src/POM68K/src/main.cpp:2824` (switch at `:2846`)
- **Defect:** `Dafb::depth()` returns 16 or 24 and `Valkyrie::depth()` returns 16, but the ternary only accepts 1/2/4/8, so `depth` collapses to 1 and the frame is decoded as 1 bpp with the 16/24 bpp stride.
- **Trigger:** on any `DafbMachine<>` profile (Q605, LC 475/575, Centris 610/650, Quadra 610/650/800/700, Q630/LC 580 — `main.cpp:2956-2959`), pick "Milliers de couleurs" at 640×480 (614 400 B, fits 1 MB VRAM). Antelope PCBR takes `mode_ = 5` (`Dafb.cpp:146`) → `depth()` = 16 (`Dafb.h:54`), or Valkyrie `mode_ = 4` → 16 with `stride() = 80 << 4 = 1280` (`Valkyrie.h:57-58`). `pmDepth` is 0 (next finding), so `depth = 1`; `minStride` = 80 ≤ `hwStride` = 1280, so the stride check passes and the loop paints 640 columns out of the first 80 bytes of each 1280-byte row. MAME renders both (`dafb.cpp` case 4/5, `valkyrie.cpp` case 4); nothing in `TODO.md` or `docs/LLE_VS_HLE.md` declares them out of scope.
- **Fix:** add the two cases at `main.cpp:2846`:
  ```cpp
  case 16: { uint16_t p = uint16_t(vb(rowOff + 2*x) << 8 | vb(rowOff + 2*x + 1));
             rgb = uint32_t(((p>>10)&0x1F)<<19 | ((p>>5)&0x1F)<<11 | (p&0x1F)<<3); break; }
  case 24: { rgb = uint32_t(vb(rowOff + 4*x + 1))<<16 | uint32_t(vb(rowOff + 4*x + 2))<<8
                 | vb(rowOff + 4*x + 3); break; }
  ```
  and let `depth` carry 16/24 through instead of falling back to 1.

### medium — PixMap `pixelSize` read from offset 0x1C (inside `vRes`) instead of 0x20, so the documented depth fallback is dead code
- **Where:** `/home/gistarcade/src/POM68K/src/main.cpp:2809`
- **Defect:** `pmDepth = (pk32(pmap+0x1C)>>16)&0xFFFF` reads bytes 28-29, the low half of the `vRes` Fixed (`00 48 00 00` at 72 dpi → 0), not `pixelSize` at byte 32.
- **Trigger:** the comment at `main.cpp:2790-2792` promises "the PixMap is only a fallback while the video driver is publishing a new mode", but `pmDepth` is 0 for every real screen, so the second arm of the ternary at `:2826` can never fire — every non-{1,2,4,8} hardware depth (and any transient during a mode switch) drops straight to 1 bpp. Every neighbouring field in the same block (`0x00`/`0x04`/`0x06`/`0x0A`) uses the correct Inside Macintosh offset, which is what makes this an off-by-4 rather than a different convention.
- **Fix:** `pmDepth = (pk32(pmap + 0x20) >> 16) & 0xFFFF;`

### medium — `Dafb::clockgenWrite8` implements only the MEMCjr "Gazelle" port, so Centris/Quadra 6x0/800 and Quadra 700 clock programming is dropped
- **Where:** `/home/gistarcade/src/POM68K/src/Dafb.cpp:198` (`if ((off & 0xFF) != 0xC3) return;`)
- **Defect:** MAME has three distinct clock generators behind the same `$300` window — `dafb_memcjr_device::clockgen_w` (Gazelle, `case 0xc3`, `dafb.cpp:1322`), `dafb_memc_device::clockgen_w` (DP8534, `case 3`/`case 19`, `dafb.cpp:1197`) and `dafb_base::clockgen_w` (DP8531, `(offset & 3) == 3`, register `offset>>4`, `dafb.cpp:884`). POM68K uses the Gazelle form for all owners.
- **Trigger:** `CentrisMemory.cpp:259-260` (djMEMC = `dafb_memc_device`) routes offsets 3 and 19 into `clockgenWrite8`, which discards them (`!= 0xC3`), so `pixelClock_` stays at the reset 31 334 400 and `Dafb::tick`'s `frameLen = htotal*vtotal*cpuHz/pixelClock_` (`Dafb.cpp:232`) gives the wrong VBL cadence for every Centris 610/650, Quadra 610/650/800 and LC 575. `Q700Memory.cpp:234-235` (plain `DAFB` — `macquadra700.cpp:729` instantiates `dafb_device`) is worse: the DP8531 nibble writes to register 12 land on offset `$C3`, whose bit 1 is read as a serial clock edge and bit 0 as a data bit, so after 20 rising edges `pixelClock_` is latched from unrelated nibbles (`N = 0` ⇒ `pixelClock_ = 0`, which then makes `tick` fall back to the 60 Hz/525-line shape for the rest of the session). `docs/LLE_VS_HLE.md:345-347` claims the Gazelle clockgen as DAFB parity and lists no gap here.
- **Fix:** add a variant enum to `Dafb` (ctor parameter: `MemcJr` / `Memc` / `Discrete`, defaulting to `MemcJr`) and dispatch in `clockgenWrite8`; port `dafb_base::clockgen_w` (DP8531: `if ((off & 3) != 3) return; dp8531_[off>>4] = v & 0xF;` then the R/P/N math at `dafb.cpp:896-909` when `(off>>4) == 15`) and `dafb_memc_device::clockgen_w` (DP8534 shift at offset 3, commit at 19). Pass the variant from `Q700Memory`/`CentrisMemory`.

### medium — TobyVideo::decode indexes the CLUT with low-aligned pen numbers while the DAC address it latches is in the inverted hardware domain
- **Where:** `/home/gistarcade/src/POM68K/src/TobyVideo.cpp:243` (also 254, 262-263)
- **Defect:** MAME's Bt453 sub-8bpp lookup is high-aligned — `pens[pixels&0x80]`, `pens[pixels&0xc0]`, `pens[pixels&0xf0]` / `pens[(pixels&0x0f)<<4]` (`nubus_m2video.cpp:186-243`) — POM68K uses `pens_[bit]`, `pens_[..&3]`, `pens_[..&0xF]`. Compounding it, `write8:96` inverts the RAMDAC byte (`v ^= 0xFF`, so `dacAddr_` is the hardware-domain address) while the VRAM byte path at `:105-119` stores the guest byte **un**-inverted (unlike `write32:145` and MAME's `vram_w`), so pixel value and DAC address live in opposite polarities.
- **Trigger:** today it is masked because the CLUT is never programmed at all (previous finding) and `reset()` pre-seeds `pens_[0]`=white/`pens_[1]`=black, which happens to match `decode`'s 1 bpp indices. Fix the RAMDAC decode alone and the screen goes black: the driver's programming lands as hardware entries 0-127 = black / 128-255 = white, and `decode` reads `pens_[0]`/`pens_[1]` — both black. At 4/8 bpp the mismatch is unconditional.
- **Fix:** land as one change with the RAMDAC decode — pick a single polarity. Simplest is to match MAME end-to-end: invert on the VRAM byte path (`write8:110` → `| (uint32_t(uint8_t(~v)) << sh)`, `read8:40` → `^ 0xFF`), then high-align the lookups: `pens_[(px << b) & 0x80]` (1 bpp), `pens_[(px << (b*2)) & 0xC0]` (2 bpp), `pens_[px & 0xF0]` / `pens_[(px & 0x0F) << 4]` (4 bpp), and drop the `pens_[1] = black` seeding at `:16`.

### low — `Dafb` reports version 3 unconditionally; the Quadra 700's discrete DAFB is version 1
- **Where:** `/home/gistarcade/src/POM68K/src/Dafb.cpp:65`
- **Defect:** `case 0x2C: return (regs_[0x2C>>2] & 0x1FF) | (3u << 9);` hardcodes the MEMC/MEMCjr revision; `dafb_base` ctor sets `m_dafb_version(1)` (`dafb.cpp:84`) and only `dafb_q950`/`dafb_memc`/`dafb_memcjr` raise it to 3 (`dafb.cpp:1099/1182/1309`), and `macquadra700.cpp:729` instantiates plain `DAFB`.
- **Trigger:** on the Quadra 700 profile the driver reads `$F980002C`, sees 3 ("MEMC/MEMCjr integrated cell") instead of 1 ("NTSC and PAL fix") and takes the wrong path. The paired quirk is also missing from `Dafb::recalcMode`: MAME `dafb.cpp:835-839` does `if ((m_hres == 512) && (m_dafb_version == 1)) { m_base = 0x1000; m_vres = 384; }`, so a 512×384 monitor on the Q700 derives the off-by-one vres and the wrong base.
- **Fix:** add `uint8_t version_` (ctor param, default 3), return `(regs_[0x2C>>2] & 0x1FF) | (uint32_t(version_) << 9)`, pass 1 from `Q700Memory`, and add the `hres_ == 512 && version_ == 1` branch to `recalcMode()`.

### low — Valkyrie RAMDAC read side effects fire once per byte lane; the write path guards for exactly this, the read path does not
- **Where:** `/home/gistarcade/src/POM68K/src/Q630Memory.cpp:332`
- **Defect:** `ioRead8` calls `video_.readRamdac32(...)` for every lane of a multi-byte access, and `Valkyrie::readRamdac32` mutates state (`Valkyrie.cpp:102-103`: `uint8_t idx = palIdx_; palIdx_ = uint8_t((palIdx_ + 1) % 3);`).
- **Trigger:** a 16-bit read of `$50F24004` splits into two `ioRead8` calls → `palIdx_` advances by 2 where MAME's single `u32` handler advances by 1, permanently desyncing the R/G/B read phase (a 32-bit read survives only because 4 ≡ 1 mod 3). The write path guards this explicitly at `Q630Memory.cpp:415` (`if ((sub & 3) == 0) video_.writeRamdac32(...)`), and `Q605Memory.cpp:309` applies the analogous CLUT-lane guard. Separately, `readRamdac32`/`writeRamdac32`'s `(off >> 2) & 3` aliases `$50F24010`/`$50F24014` onto the palette address/data registers, where `valkyrie_device::ramdac_r/w` (`valkyrie.cpp:377-411`) decodes only offsets 0 and 1 and returns 0 elsewhere.
- **Fix:** mirror the write path — `if ((sub & 3) != 0) return 0;` before the `readRamdac32` call — and change `(off >> 2) & 3` to `off >> 2` with a `default: return 0;` in both Valkyrie RAMDAC entry points.

### low — the 040 host decoder hardcodes black/white at 1 bpp instead of CLUT entries 0 and 1
- **Where:** `/home/gistarcade/src/POM68K/src/main.cpp:2848`
- **Defect:** `rgb = bit ? 0x000000u : 0xFFFFFFu;` ignores the DAFB/Valkyrie CLUT, while the 2/4/8 bpp arms of the same switch all go through `mem.clut()`.
- **Trigger:** on any DAFB/Valkyrie machine at 1-bit depth, a guest that swaps CLUT entries 0 and 1 (the classic screen-invert used by Easy Access, screen savers, splash screens) produces no visible change. MAME looks both up: `dafb.cpp` screen_update case 0 emits `pens[(pixels>>7)&1] … pens[pixels&1]`, and `valkyrie.cpp:163-172` does the same.
- **Fix:** `const uint8_t* c = cl[bit]; rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2];` — same shape as `case 2`.
