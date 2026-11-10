#!/usr/bin/env python3
"""The public CPU proof must fail on missing data, tampering and soft skips."""
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "cpu_vectors", Path(__file__).resolve().parents[1] / "tools/cpu_vectors.py")
vectors = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vectors)


class CorpusContract(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.directory = Path(self.work.name)
        self.fixture_patch = patch.object(vectors, "FIXTURES", self.directory)
        self.fixture_patch.start()
        self.addCleanup(self.fixture_patch.stop)
        self.entry = {"file": "030.json.gz"}
        self.install([{"name": "one", "initial": {"d0": 1}, "final": {"d0": 2}}])

    def install(self, rows):
        raw = json.dumps(rows).encode()
        (self.directory / self.entry["file"]).write_bytes(gzip.compress(raw, mtime=0))
        self.entry.update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(), vectors=len(rows))

    def run_fake(self, output, code=0):
        completed = subprocess.CompletedProcess([], code, output, "")
        with patch.object(vectors.subprocess, "run", return_value=completed), contextlib.redirect_stdout(io.StringIO()):
            vectors.replay("030", self.entry, Path("runner"))

    def test_missing_payload_fails(self):
        self.entry["file"] = "missing.json.gz"
        with self.assertRaises(OSError):
            vectors.verify("030", self.entry)

    def test_modified_payload_fails(self):
        (self.directory / self.entry["file"]).write_bytes(gzip.compress(b"[]", mtime=0))
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            vectors.verify("030", self.entry)

    def test_empty_even_with_matching_hash_fails(self):
        self.install([])
        with self.assertRaisesRegex(ValueError, "empty or incomplete"):
            vectors.verify("030", self.entry)

    def test_successful_soft_skip_fails(self):
        with self.assertRaisesRegex(ValueError, "did not execute"):
            self.run_fake("SKIP: no CPU data\n")

    def test_zero_partial_and_unlisted_known_counts_fail(self):
        for counts in ["0/0", "1/2", "0/1"]:
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                self.run_fake(f"[sst68030] OK: {counts} across 1 file(s), 1 known divergence(s), 0 mmu-skipped\n")

    def test_mmu_skip_or_failed_exit_fails(self):
        summary = "[sst68030] OK: 1/1 across 1 file(s), 0 mmu-skipped\n"
        cases = [(summary.replace("0 mmu", f"{n} mmu"), 0) for n in [1, 10, 100]]
        for output, code in cases + [(summary, 1)]:
            with self.subTest(code=code), self.assertRaises(ValueError):
                self.run_fake(output, code)

    def test_complete_run_passes(self):
        self.run_fake("[sst68030] dir=/home/skipper/data files=1\n"
                      "[sst68030] OK: 1/1 across 1 file(s), 0 mmu-skipped\n")


if __name__ == "__main__":
    unittest.main()
