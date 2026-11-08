#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Hold the evidence checker itself, then check the actual Moira comments."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "core_grades", Path(__file__).resolve().parents[1] / "tools/core_grades.py")
grades = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(grades)


class EvidenceCheckerTest(unittest.TestCase):
    def test_corpus_pin_cannot_replace_oracle_pin(self):
        header = "// [D] SingleStepTests/680x0 68000/v1 @ " + grades.SST_REV
        self.assertTrue(grades.comment_problems(header + "\n// WinUAE agrees"))
        self.assertEqual(grades.comment_problems(
            header + "\n// [D] WinUAE @ Hatari e77819f7"), [])
        self.assertTrue(grades.comment_problems(
            "// [B] WinUAE @ Hatari e77819f7\n" + header))

    def test_apply_grades_both_sources_and_is_idempotent(self):
        # This is the mixed-source path that used to let one source's
        # freshly inserted [D] suppress the other source's stamp.
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "Moira.cpp"
            source.write_text("    // WinUAE and SingleStepTests agree\n"
                              "    execute(); // WinUAE and SingleStepTests agree\n"
                              '    const char* url = "https://SingleStepTests/680x0";\n'
                              '    emit("https://WinUAE/"); // SingleStepTests agrees\n')
            with patch.object(grades, "core_files", return_value=[str(source)]):
                self.assertEqual(grades.apply(), 0)
                first = source.read_text()
                os.utime(source, (1000000000, 1000000000))
                mtime = source.stat().st_mtime_ns
                self.assertEqual(grades.apply(), 0)
                self.assertEqual(source.read_text(), first)
                self.assertEqual(source.stat().st_mtime_ns, mtime)
            lines = first.splitlines()
            for a, b, inline in grades.comment_units(lines):
                self.assertEqual(grades.comment_problems(
                    grades.comment_text(lines, a, b, inline)), [])
            self.assertIn("    execute(); //", first)
            self.assertIn('    const char* url = "https://SingleStepTests/680x0";\n', first)
            self.assertIn('    emit("https://WinUAE/"); // [D] SingleStepTests/', first)
            wrong = "// [D] WinUAE @ Hatari deadbee — original reading\n"
            source.write_text(wrong)
            with patch.object(grades, "core_files", return_value=[str(source)]):
                self.assertEqual(grades.apply(), 0)
            self.assertEqual(source.read_text(), wrong)
            self.assertTrue(grades.comment_problems(wrong))

    def test_each_source_needs_its_own_pin(self):
        for text in (
            "// SingleStepTests/680x0 v1 — same bus order",
            "// [D] SingleStepTests/680x0 v1 — same bus order",
            "// [D] WinUAE @ Hatari e77819f7\n// SingleStepTests agrees",
            "// [B] SingleStepTests/680x0 68000/v1 @ " + grades.SST_REV,
            "// [D] SingleStepTests/680x0 68000/v1 @ deadbee",
            "// [D] SingleStepTests/680x0 68000/v1 @ " + grades.SST_REV +
            "\n// [D] SingleStepTests/680x0 68000/v1 @ deadbee",
        ):
            with self.subTest(text=text):
                self.assertTrue(grades.sst_problems(text))

    def test_pinned_headers_and_mixed_evidence(self):
        header = "// [D] SingleStepTests/680x0 68000/v1 @ "
        for text in (
            "// Unrelated comment",
            header + grades.SST_REV,
            header + grades.SST_REV[:7],
            "// [D] WinUAE @ Hatari e77819f7\n" + header + grades.SST_REV,
        ):
            with self.subTest(text=text):
                self.assertEqual(grades.sst_problems(text), [])

    def test_fetcher_and_lock_must_both_match(self):
        fetcher = 'REF="' + grades.SST_REV + '"\n'
        lock = '#   ' + grades.SST_REV + ' (2024-05-14)\n'
        self.assertEqual(grades.sst_pin_problems(fetcher, lock), [])
        self.assertTrue(grades.sst_pin_problems(fetcher.replace(grades.SST_REV, 'deadbee'), lock))
        self.assertTrue(grades.sst_pin_problems(fetcher, lock.replace(grades.SST_REV, 'deadbee')))


if __name__ == "__main__":
    result = unittest.TextTestRunner().run(
        unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceCheckerTest))
    sys.exit(grades.check() if result.wasSuccessful() else 1)
