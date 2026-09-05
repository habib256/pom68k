# The recorded x86-64 noise floor, measured properly — and it was right all along

*2026-09-05. Closes the TODO § B.2 item "trancher le plancher de bruit
inscrit". Eighteen null experiments — three repeats × two budgets × three
engines — on a host with nothing else running (1-minute load 0.25 at the
start, 1.01 at the end).*

## Why the item existed, and why its premise was wrong

`performance_budgets.tsv:67` records
`host_wallclock/any/x86_64/noise_floor_permille = 10`. During the B.2 campaign
the bench printed `HOST NOISIER THAN POLICY` several times and, on one
2000-frame null, computed a floor of **0.2 %** and advised recording 2
permille. I wrote that up as evidence the policy was too loose, and paired it
with a 1.7 % reading at 6000 frames as evidence that one constant could not
express both.

Both readings were taken while other work shared the machine — three agents
reading and a second project compiling. `docs/MEASURING.md` § 4.1bis says in as
many words that such a measurement is provisional whatever its spread says,
and that applies to a *floor* exactly as it applies to a delta. A tight null
taken under load is not evidence the host is quiet; it is one sample of a
distribution whose tail the load was busy producing elsewhere.

## The campaign

`POM68K_BENCH_NULL=1 POM68K_BENCH_COMPARE=5` — the reference arm against
itself inside one process, so whatever it reports is the harness. Every delta
came back at ±0.1 % or better, as a null must.

| engine | budget | worst arm spread over three repeats |
|---|---|---:|
| `threaded` | 2000 | 0.3 % |
| `threaded` | 6000 | 0.2 % |
| x64 | 2000 | **1.8 %** |
| x64 | 6000 | 0.9 % |
| interpreter | 2000 | **1.7 %** |
| interpreter | 6000 | 0.8 % |

Thirteen of the eighteen nulls read **≤ 0.3 %**. Five read **0.8 – 1.8 %**.
The distribution is bimodal: this host is usually very tight and occasionally
produces a one-to-two-percent excursion with *nothing changed between the
arms*.

## Ruling: leave it at 10 permille

The tempting reading is "13 of 18 say 0.3 %, so record 3 permille". That is
the mistake `MEASURING.md` § R2's own retraction warns about, run in the
opposite direction: the first floor in this tree was wrong by 6× because
someone generalised from too few runs, and taking the *narrowest* evidence is
how a phantom "2.9 % win" once survived its first re-measurement.

The bar the harness applies is already the widest of (arm spread A, arm spread
B, recorded floor). The recorded constant's job is therefore to catch the run
whose own spreads happen to look tight while the host is in its excursion
mode — precisely the five nulls above. A recorded 2 or 3 permille would let a
1 % claim through on a host that demonstrably produces 1.8 % with nothing
changed. **10 permille is the right number, it is below the worst measured
excursion rather than above it, and it stays.**

What was actually wrong was not the budget but my earlier reading of it. There
is no `POLICY TOO LOOSE` condition on this host when it is measured properly:
the bench printed that line from a contaminated sample.

## What this does and does not license

It does *not* license claiming a 1 % effect here. The B.2 results that stand —
−10 % for the fused fetch, −5.5 % for the data window — are five to ten times
this floor and were never close to it. It does mean that a future 1-2 % idea
on this host needs either many more paired repeats or a same-process
comparator, not a re-argued floor.

Raw: `floor.txt` (the table), `floor.raw` (all eighteen runs in full).
