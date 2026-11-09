# Bare-core reproductions of open defects

These are deliberately failing standalone programs, not registered CTest gates.
They preserve evidence for an open TODO; a green test suite does not include them.
Promote the relevant cells into a regular gate when fixing the defect.

## 68040 ATC FC2 — `mmu040_fc2.cpp`

MC68040UM-1993 §3.3, Figures 3-20/3-21, defines FC2 in the ATC tag separately
from the page descriptor's supervisor-protection bit. The current core has
neither that tag nor the hardware's 64-entry/four-way geometry. This program
isolates the FC2 defect; it does not test the remaining geometry or replacement.

From the repository root after building `moira`:

```sh
c++ -O2 -std=c++20 -Itests -Isrc -Iextern/softfloat -isystem extern/moira/Moira \
  tests/repros/mmu040_fc2.cpp build/libmoira.a \
  build/extern/softfloat/libsoftfloat68k.a -o /tmp/pom68k-mmu040-fc2
/tmp/pom68k-mmu040-fc2
```

Expected on the pre-FC2-fix core: exit 1, **184 failed checks**. It covers
68LC040/68040, 4K/8K pages, caches enabled/disabled, either privilege mode
first, instruction/data translations, read-only JIT probes and MOVES SFC/DFC.
After both modes have warmed their ATCs, changing the page descriptors must
not discard either resident translation: flushing on a mode switch is not
an acceptable fix. The repro contains 40 fresh CPU sequences; each sequence
keeps ATC state across its instructions. The hardware cache model is explicitly
enabled or disabled within the program, without an environment variable.

The physical CINV/CPUSH fix is already present in the baseline used here.
The FC2 implementation is still unchanged. See `TODO.md` under Justesse 68040.
