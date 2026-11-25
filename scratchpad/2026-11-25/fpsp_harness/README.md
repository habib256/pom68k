# FPSP harness — Apple's 68040 FPSP run on bare Moira

Local evidence behind Dirk's PR #49 (68040 FSAVE unimplemented instruction
frame) and the 2026-11-25 CHANGELOG entry. It loads a user-provided Quadra 605
ROM (never committed), points vector 11 at the ROM's `fpsp_unimp` and runs a
few unimplemented FPU instructions (FSIN, FETOX, FMOD, FSCALE, FMOVECR) with
register, single, packed, denormal and unnormal operands, then prints the
results next to the expected values.

Build against any Moira tree (POM68K's `extern/moira/Moira`, or a clone of
Dirk's `master` at or after `bf98107f`), from the repository root:

```sh
g++ -std=c++20 -O1 -Iextern/moira/Moira \
    scratchpad/2026-11-25/fpsp_harness/harness.cpp \
    extern/moira/Moira/*.cpp extern/moira/Moira/*/*.cpp \
    -o /tmp/fpsp_harness
/tmp/fpsp_harness "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM" 8db26
```

(`8db26` is the offset of `fpsp_unimp` in that ROM. If the source glob does
not match Moira's layout, link the `moira` library target of a POM68K build
instead.)

Results recorded on 2026-11-25 (x86-64):
- with the FSAVE rewrite (now upstream): 14 of 14 cases correct, bit-exact;
- with the earlier queue patch (E1 always set): every case wrong except
  FMOVECR (FSIN(0.5) = 0.7174, FMOD(7.5, 2) = 0.3, FETOX(1) = 2.2255);
- on `863b8d02` without the patch: the FPSP ends in `fpsp_fmt_error`.
