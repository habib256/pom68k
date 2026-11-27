Title: Reset: a bus error during the reset sequence halts the CPU

Body:

`reset()` reads the initial SSP and PC through `read16OnReset()` and fills the prefetch queue, all outside any handler. If the host throws `BusError` from one of these reads, for example because nothing is mapped at address 0 yet, the exception leaves `reset()` and reaches the caller. On master, with the `BusErrorTests` host faulting on address 0, 2, 4 or 6, on the first instruction word or on the second one, `cpu.reset()` throws on all five models (68000 to 68040), and `isHalted()` is false.

The bus error guide says "Moira lets your application signal a bus error from any memory access function". Its list of those functions (`read8`, `read16`, `write8`, `write16`, and `read32` / `write32`) does not name `read16OnReset()`, but `read16OnReset()` forwards to `read16()` by default ("The default implementation forwards to read16()"). I found nothing that tells a host not to throw during `reset()`.

The manuals describe a double bus fault and a halted CPU:

- MC68000UM §5.4.4: "A double bus fault occurs during a reset operation when a bus error occurs while the processor is reading the vector table (before the first instruction is executed)". §6.3.9.1, the 68000 subsection: "If a bus error occurs during the exception processing for a bus error, an address error, or a reset, the processor halts and all processing ceases." For the 68010, §6.3.9.2 says nothing about reset. The model-neutral passages of the same manual do: §6.3.9, "The bus error facility is identical on the all processors", and §6.3.10, "if an address error occurs during the exception processing for a bus error, address error, or reset, the processor is halted."
- MC68020UM §6.1.1: "if a bus error or address error occurs during the exception processing sequence for a reset, a double bus fault occurs. The processor halts".
- MC68030UM §8.1.1: "if bus error or address error occur during the exception processing sequence for a reset a double bus fault occurs. The processor halts".
- MC68040UM §8.2.10: "If an access fault or address error occurs during the exception processing sequence for a reset, a double bus fault is generated. The processor halts" ([page 231](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf#page=231)).

The halt covers both prefetch words, not only the vectors:

- MC68040UM §8.2.10: "Reset exception processing concludes with the prefetch of the first four long words beginning at the memory location pointed to by the PC." ([page 230](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf#page=230))
- The 68020 and 68030 reset flowcharts (MC68020UM Figure 6-1, MC68030UM Figure 8-1) put "PREFETCH 3 WORDS" inside the reset sequence, with a double bus fault exit.
- MC68000UM §6.2.5: "The last step is the same for all exceptions. The new program counter value is fetched from the exception vector. The processor then resumes instruction execution. The instruction at the address in the exception vector is fetched, and normal instruction decoding and execution is started." §6.3.9.1 counts that fetch as part of exception processing: "If a bus error occurs during the last step of exception processing, while either reading the exception vector or fetching the instruction, the value of the program counter is the address of the exception vector."
- Moira already halts on a bus error on either prefetch word after the bus error vector: `processException()` catches a `BusError` around `execBusError()`, which ends with `jumpToVector()`.

The change catches `BusError` around the vector reads and the prefetch in `reset<C>()`. The rest of `reset()` runs as before: the debugger is reset and `didResetCPU()` is called. Then the CPU halts through `halt()`, so `didHaltCPU()` follows, and `execute()` only lets time pass until the next `reset()`. A reset without a fault is unchanged. The doc comments of `reset()` and `isHalted()` and the "Double Bus Faults" paragraph of the bus error guide now mention the halt during `reset()`.

What a host sees after such a reset:
- When `didResetCPU()` runs, the registers are partially loaded and `isHalted()` is still false.
- Afterwards, `isHalted()` is true and SR is $2700. `getPC()` and `getPC0()` return 0 in every case, including a fault on the first instruction word after PC $1000 has been loaded, because `halt()` sets the PC from PC0, which is still 0.
- The SSP holds whatever had been assembled before the fault. In the test, that is 0 for a fault at 0 or 2 (the high word there is 0), and $0F00 for a fault at 4, 6 or either instruction word.

The test "A bus error during reset halts the CPU" faults each of the six reads on the 68000, 68010, 68020, 68030 and 68040. In every case it checks that:
- `reset()` does not throw, and the CPU is halted;
- the delegates are called in this order: one `didResetCPU()` while the CPU is not yet halted, then one `didHaltCPU()`;
- the debugger was reset (a breakpoint set before the reset is still checked);
- a second reset without the fault leaves the halted state, PC0 is the initial PC, and the first instruction executes.

On master the test fails 120 of its 210 assertions (`CHECK_NOTHROW(cpu.reset())`, `isHalted()`, the delegate order and the debugger check, in all 30 cases); with the change it passes. Each of these mutations makes the test fail: skipping `debugger.reset()` or `didResetCPU()` (30 failures each), or halting and returning from the catch block (60).

What was run: the full ctest passes on this commit (26/26).

Not covered:
- The catch type is not pinned by the test: `catch (...)` would also pass it. I did not add a test that a host exception of another type leaves `reset()`, because that would make an undesirable behaviour part of the contract.
- `AddressError` is not caught, because Moira clears bit 0 of the initial SSP and PC, so no address error can arise during the reset. A real CPU with an odd initial PC would take an address error and halt; Moira's behaviour there is unchanged.
- Moira fetches two words at reset, where the flowcharts prefetch 3 words (68020/68030) or 4 long words (68040). A fault on a later word is not modelled.
- MAME's 68000 (the copy in `Tests/ThirdParty/MAME`, d077ea24) treats a bus error on the four vector reads and on the first instruction word as a double fault, but not on the second word, which it fetches after IRD is loaded; there it becomes a bus error of the first instruction. This change halts on the second word as well. Only a real 68000 would tell which is right for it.
