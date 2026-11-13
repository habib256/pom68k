#!/usr/bin/env python3
"""Reproduce and replay the small, repository-owned WinUAE CPU corpus.

The full corpora remain optional assets. This one must execute, including in
CI and sanitizer jobs: absent/corrupt data or a successful soft-skip is red.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/cpu_vectors"


def manifest():
    data = json.loads((FIXTURES / "manifest.json").read_text())
    if data["version"] != 1 or set(data["models"]) != {"030", "040"}:
        raise ValueError("unsupported CPU corpus manifest")
    return data


def verify(model, entry):
    raw = gzip.decompress((FIXTURES / entry["file"]).read_bytes())
    if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raise ValueError(f"{model}: corpus identity mismatch")
    rows = json.loads(raw)
    if not rows or len(rows) != entry["vectors"]:
        raise ValueError(f"{model}: empty or incomplete CPU corpus")
    names = set()
    for row in rows:
        if row["name"] in names or not row["initial"] or not row["final"]:
            raise ValueError(f"{model}: duplicate or empty vector")
        names.add(row["name"])
    return raw


def replay(model, entry, runner, emulator="", jit=False):
    raw = verify(model, entry)
    with tempfile.TemporaryDirectory(prefix="pom68k-cpu-vectors-") as work:
        (Path(work) / "public.json").write_bytes(raw)
        cmd = [part for part in emulator.split(";") if part] + [str(runner), work]
        if model == "040":
            cmd.append("--fpu")
        # --jit: the runner judges the translator against the interpreter,
        # so the oracle rulings in the known list do not apply (a listed
        # vector agrees on both sides and would read as a stale entry).
        if entry.get("known") and not jit:
            cmd += ["--known", str(FIXTURES / entry["known"])]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    output = result.stdout + result.stderr
    print(output, end="")
    summary = re.search(r"\[sst680" + model[-2:] + r"\] OK: (\d+)/(\d+) across (\d+) file", output)
    known = re.search(r"(\d+) known divergence", output)
    mmu = re.search(r"\b(\d+) mmu-skipped\b", output)
    skipped = re.search(r"(?im)^\s*(?:SKIP:|SOFT[ -]SKIP\b)", output)
    expected = entry["vectors"]
    if (result.returncode or skipped
            or not summary or int(summary[2]) != expected or int(summary[3]) != 1
            or (known and (jit or not entry.get("known")))
            or int(summary[1]) + (int(known[1]) if known else 0) != expected
            or not mmu or int(mmu[1]) != 0):
        raise ValueError(f"{model}: replay did not execute every pinned vector")
    print(f"cpu_vectors: {model} executed {expected} pinned vectors; no soft skip")


def regenerate(data, oracle, write=False):
    for model, entry in data["models"].items():
        rows = []
        with tempfile.TemporaryDirectory(prefix="pom68k-cpu-generate-") as work:
            for index, recipe in enumerate(entry["recipes"]):
                out = Path(work) / f"cell-{index}.json"
                cmd = [sys.executable, str(ROOT / f"oracle/fuzz/fuzz{model}.py"),
                       "--a", str(Path(oracle).resolve()), "--out", str(out)]
                for key, value in recipe.items():
                    cmd += ["--" + key, str(value)]
                if model == "030":
                    cmd.append("--strict-fpu")
                subprocess.run(cmd, check=True, timeout=180)
                cell = json.loads(out.read_bytes())
                if len(cell) != recipe["n"]:
                    raise ValueError(f"{model}: generator halted or omitted vectors in {recipe}")
                rows.extend(cell)
        raw = (json.dumps(rows, sort_keys=True, indent=2) + "\n").encode()
        identity = dict(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw), vectors=len(rows))
        if write:
            (FIXTURES / entry["file"]).write_bytes(gzip.compress(raw, mtime=0))
            entry.update(identity)
        elif any(entry[key] != value for key, value in identity.items()):
            raise ValueError(f"{model}: regenerated corpus differs from its pin: {identity}")
        print(f"cpu_vectors: {model} regenerated {len(rows)} vectors, SHA-256 {identity['sha256']}", flush=True)
    if write:
        (FIXTURES / "manifest.json").write_text(json.dumps(data, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("verify")
    run = commands.add_parser("replay")
    run.add_argument("--model", choices=("030", "040"), required=True)
    run.add_argument("--runner", type=Path, required=True)
    run.add_argument("--emulator", default="")
    run.add_argument("--jit", action="store_true",
                     help="the runner is sst68030_jit/sst68040_jit (no known list)")
    generate = commands.add_parser("regenerate")
    generate.add_argument("--oracle", type=Path, required=True)
    generate.add_argument("--write", action="store_true", help="replace fixtures and pins for review")
    args = parser.parse_args()
    data = manifest()
    if args.command == "regenerate":
        regenerate(data, args.oracle, args.write)
    elif args.command == "replay":
        replay(args.model, data["models"][args.model], args.runner.resolve(), args.emulator,
               args.jit)
    else:
        for model, entry in data["models"].items():
            verify(model, entry)
            print(f"cpu_vectors: {model} identity and {entry['vectors']} vectors verified")


if __name__ == "__main__":
    try:
        main()
    except (OSError, EOFError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"FAIL cpu_vectors: {error}", file=sys.stderr)
        sys.exit(1)
