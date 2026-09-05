=== round A — knob ON, fresh seed 1 (BUDGET=6144 FINE_AT=73331) ===
A-on-seed1     jit_lockstep_030_test                      rc=0  80s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
A-on-seed1     jit_lockstep_030_blocks_test               rc=0  167s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 78862818 hits / 14808274 refusals
A-on-seed1     jit_lockstep_030_x64_experimental_test     rc=0  136s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
A-on-seed1     jit_lockstep_030_x64_packed_ccr_test       rc=0  139s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10879475 hits / 14122271 refusals
A-on-seed1     jit_lockstep_030_x64_alignment_test        rc=0  120s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
=== round B — knob ON, fresh seed 2 (BUDGET=12288 FINE_AT=95003) + FULL_RAM_AT=119000 ===
B-on-seed2     jit_lockstep_030_test                      rc=0  314s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 164098856 hits / 13733123 refusals · jit 34218001 hits / 15099177 refusals
B-on-seed2     jit_lockstep_030_blocks_test               rc=0  446s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 164098856 hits / 13733123 refusals · jit 162000927 hits / 15831052 refusals
B-on-seed2     jit_lockstep_030_x64_experimental_test     rc=0  327s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 164098856 hits / 13733123 refusals · jit 34218001 hits / 15099177 refusals
B-on-seed2     jit_lockstep_030_x64_packed_ccr_test       rc=0  349s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 164098856 hits / 13733123 refusals · jit 34441682 hits / 15116836 refusals
B-on-seed2     jit_lockstep_030_x64_alignment_test        rc=0  313s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 164098856 hits / 13733123 refusals · jit 34218001 hits / 15099177 refusals
=== round C — knob ON, seed 1, ICTRACE=1 (the fetch side) ===
C-on-ictrace   jit_lockstep_030_test                      rc=0  134s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
C-on-ictrace   jit_lockstep_030_blocks_test               rc=0  163s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 78862818 hits / 14808274 refusals
C-on-ictrace   jit_lockstep_030_x64_experimental_test     rc=0  131s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
C-on-ictrace   jit_lockstep_030_x64_packed_ccr_test       rc=0  121s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10879475 hits / 14122271 refusals
C-on-ictrace   jit_lockstep_030_x64_alignment_test        rc=0  100s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 80059089 hits / 13612003 refusals · jit 10667684 hits / 14108863 refusals
=== round D — knob OFF control, seed 1 ===
D-off-seed1    jit_lockstep_030_test                      rc=0  64s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 0 hits / 0 refusals · jit 0 hits / 0 refusals  [POM68K_DATA_WINDOW off]
D-off-seed1    jit_lockstep_030_blocks_test               rc=0  99s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 0 hits / 0 refusals · jit 0 hits / 0 refusals  [POM68K_DATA_WINDOW off]
D-off-seed1    jit_lockstep_030_x64_experimental_test     rc=0  49s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 0 hits / 0 refusals · jit 0 hits / 0 refusals  [POM68K_DATA_WINDOW off]
D-off-seed1    jit_lockstep_030_x64_packed_ccr_test       rc=0  81s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 0 hits / 0 refusals · jit 0 hits / 0 refusals  [POM68K_DATA_WINDOW off]
D-off-seed1    jit_lockstep_030_x64_alignment_test        rc=0  72s  [jit_lockstep_030] OK — 120000 steps identical | data window: interp 0 hits / 0 refusals · jit 0 hits / 0 refusals  [POM68K_DATA_WINDOW off]
=== ictrace lines printed in round C (must be zero) ===
/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/scratchpad/2026-09-04/b2s5/logs/lockstep-C-on-ictrace-jit_lockstep_030_blocks_test.log:0
/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/scratchpad/2026-09-04/b2s5/logs/lockstep-C-on-ictrace-jit_lockstep_030_test.log:0
/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/scratchpad/2026-09-04/b2s5/logs/lockstep-C-on-ictrace-jit_lockstep_030_x64_alignment_test.log:0
/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/scratchpad/2026-09-04/b2s5/logs/lockstep-C-on-ictrace-jit_lockstep_030_x64_experimental_test.log:0
/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/scratchpad/2026-09-04/b2s5/logs/lockstep-C-on-ictrace-jit_lockstep_030_x64_packed_ccr_test.log:0
