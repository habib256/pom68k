# H1-worktree-diff — no findings survived

3 candidate findings raised, all 3 killed by the adversarial panel:
1. "VIA T1 continuous mode now raises IFR6 for a timer that was never started,
   and forges the one-shot arm flag" — refuted
2. "Freeze probe disassembles through the live, side-effecting bus and can throw
   MmuBusError out of the emulation thread" — refuted
3. "68HC05 interrupt sequence charged 11 cycles (NMOS 6805 value) instead of the
   M68HC05's 10" — refuted

COVERAGE GAP: the `memory-safety` finder died on ECONNRESET before reporting.
That axis is re-run as hunt H1b.
