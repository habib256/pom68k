# Pull-request drafts for Dirk Hoffmann's Moira

Texts of pull requests that are prepared but not opened. Each file is named
after its branch on the fork `habib256/Moira` (one commit on his `master`,
`bf98107f`). Nothing here is posted without the owner's approval of the
exact text.

| Draft | Branch | Commit | Queue patch |
|---|---|---|---|
| `6888x-fmove-out-mid-instruction.md` | `6888x-fmove-out-mid-instruction` | `8869492` | 16 |
| `trace-frame-fault.md` | `trace-frame-fault` | `cf7f5f0` | 4 (trace half) |
| `reset-bus-fault.md` | `reset-bus-fault` | `4592ccd` | 4 (reset half) |

Each was verified twice (the manuals; reproduction and code) and passes his
full `ctest` (26/26). Fetch them with
`git fetch https://github.com/habib256/Moira.git <branch>`.
Delete a draft once its pull request is open and recorded in
`../patches/queue.tsv`.
