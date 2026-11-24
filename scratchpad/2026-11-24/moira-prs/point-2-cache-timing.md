# 68020/68030: account for instruction cache fills in the cycle budget

On `master` at `450b15ae`, a warm loop of eight NOPs and a taken DBF takes 4.5 emulated clocks per iteration on both the 68020 and the 68030. The instruction handlers already charge the cached timings. Each time the instruction cache or its longword latch supplies a word, `readInstr` then deducts two clocks. A cached NOP can consequently use zero clocks. This reproduces [point 2 in discussion #36](https://github.com/dirkwhoffmann/Moira/discussions/36#discussioncomment-19835053).

This patch leaves the instruction cost intact on a hit and charges two extra clocks for an external fill on the 020/030. It preserves the existing two-clock difference between a hit and a 32-bit fill, and the existing four-clock surcharge for a narrower port.

Motorola lists two clocks for NOP and six for a taken DBcc in the 68020 cache-only, no-overlap case. That gives a reference of 8 × 2 + 6 = 22 clocks for this loop under Moira’s per-instruction accounting. The 68030 uses the same accounting path. [MC68020UM §8.2](https://www.ele.uva.es/~jesman/BigSeti/ftp/Microprocesadores/Motorola/68020um.pdf#page=224) · [DBcc and NOP tables, §§8.2.15–16](https://www.ele.uva.es/~jesman/BigSeti/ftp/Microprocesadores/Motorola/68020um.pdf#page=252)

Measured over 100 taken iterations on a 32-bit port, for each CPU:

| Loop | Clocks/iteration on master | With patch | Bus longword reads/iteration in both |
|---|---:|---:|---:|
| Warm cache | 4.5 | 22 | 0 |
| Cache disabled, longword-aligned | 12 | 32 | 5 |
| Cache disabled, shifted by one word | 14 | 34 | 6 |

The included regression test covers the warm and aligned cache-disabled cases on both CPUs. It fails on master and passes with the patch. Both unit suites and the four 020/030 UAE sequence suites pass.

In a downstream POM68K A/B boot test, only this accounting change differed between builds. With the same LC III+ ROM and System 7.5.3 disk image, the original timing left the screen black with zero SCSI commands. With the patch the Mac reached Finder after 2,930 SCSI commands and matched the reference screen hash (`3537bb072c049592`).
