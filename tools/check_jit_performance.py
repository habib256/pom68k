#!/usr/bin/env python3
"""Check structured JIT metric artifacts against performance_budgets.tsv."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def budgets(path: pathlib.Path):
    rows = []
    seen = set()
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        cells = raw.split("\t")
        if len(cells) != 6:
            raise ValueError(f"{path}:{number}: expected six columns")
        workload, family, host, metric, value, unit = cells
        key = (workload, family, host, metric)
        if key in seen:
            raise ValueError(f"{path}:{number}: duplicate budget {key}")
        seen.add(key)
        rows.append((workload, family, host, metric, int(value), unit))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("metrics", nargs="+", type=pathlib.Path)
    parser.add_argument("--budgets", type=pathlib.Path,
                        default=pathlib.Path("performance_budgets.tsv"))
    parser.add_argument("--require", action="append", default=[],
                        help="workload that must occur in the supplied artifacts")
    args = parser.parse_args()

    policy = budgets(args.budgets)
    records = [json.loads(path.read_text()) for path in args.metrics]
    failed = False
    workloads = {record.get("workload") for record in records}
    for wanted in args.require:
        if wanted not in workloads:
            print(f"FAIL missing required metrics workload {wanted}")
            failed = True

    for record in records:
        if record.get("schema") != "pom68k.jit.metrics.v1":
            print(f"FAIL unsupported metrics schema in {record.get('gate', '?')}")
            failed = True
            continue
        if record.get("status") != "pass":
            print(f"FAIL {record.get('workload', '?')}: "
                  f"status={record.get('status', 'missing')}")
            failed = True
        host_profile = record.get("host_profile", record.get("host_arch"))
        matched = [row for row in policy
                   if row[0] == record.get("workload")
                   and row[1] == record.get("cpu_family")
                   and row[2] in (host_profile, "any")]
        # An exact host policy shadows the portable fallback.
        exact_metrics = {row[3] for row in matched
                         if row[2] == host_profile}
        matched = [row for row in matched
                   if row[2] == host_profile or row[3] not in exact_metrics]
        for workload, family, host, metric, limit, unit in matched:
            key = metric.removeprefix("min_").removeprefix("max_")
            if key not in record:
                print(f"FAIL {workload}/{host}: metric artifact lacks {key}")
                failed = True
                continue
            actual = int(record[key])
            ok = actual >= limit if metric.startswith("min_") else actual <= limit
            print(f"{'PASS' if ok else 'FAIL'} {workload}/{family}/{host} "
                  f"{metric}: {actual} vs {limit} {unit}")
            failed |= not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
