#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Hold the evidence checker itself, then check the actual Moira comments."""
import importlib.util
import contextlib
import hashlib
import io
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

    def test_manual_needs_its_own_edition_and_grade(self):
        valid = "// [B] MC68030UM-1989 §9.7.2: translation control register"
        self.assertEqual(grades.manual_problems(valid), [])
        for wrong in (
            "// [B] MC68030UM §9.7.2",
            "// [B] MC68881/882UM §6.1",
            "// [B] MC68030UM-1999 §9.7.2",
            "// [D] WinUAE @ Hatari e77819f7\n// MC68030UM-1989 §9.7.2",
            "// [B] MC68040UM-1993 §3.1\n// MC68030UM-1989 §9.7.2",
        ):
            with self.subTest(wrong=wrong):
                self.assertTrue(grades.manual_problems(wrong))

    def test_manual_rejects_missing_and_inherited_bad_locators(self):
        for wrong in (
            "// [B] MC68030UM-1989 §9.7.2.2",
            "// [B] MC68030UM-1989 §9.7.2\n// MMUSR (§9.7.2.6)",
            "// [B] MC68881UM-1989 §4.6.2.2",
            "// [B] MC68040UM-1993 §3.4.1",
            "// [B] MC68881UM-1989 Table 8-999",
            "// [B] MC68030UM-1989: MMU",
            "// Table 8-3: timing",
            "// §9.7.2: translation control register",
        ):
            with self.subTest(wrong=wrong):
                self.assertTrue(grades.manual_problems(wrong))
        self.assertEqual(grades.manual_problems(
            "// [B] MC68020UM-1992 §6.2.2: DF-cleared software completion"), [])
        self.assertTrue(grades.manual_problems(
            "// [B] MC68020UM §6.2.2: missing edition"))
        self.assertEqual(grades.manual_problems("// [B] M68000PRM-1992 CINV: physical address"), [])
        self.assertEqual(grades.manual_problems("// POM68K_SURFACE.md § 1: host contract"), [])

    def test_unknown_manual_claim_needs_a_witness(self):
        unknown = "// [F] MC68040UM-1993 §4.3.2 does not prove this bypass"
        self.assertTrue(grades.manual_problems(unknown))
        self.assertEqual(grades.manual_problems(
            unknown + "\n// Settled by: a cache-resident MOVES bus trace"), [])
        self.assertTrue(grades.comment_problems("// [F] Host timing approximation"))
        self.assertEqual(grades.comment_problems(
            "// [F] Host timing approximation\n// Settled by: a calibrated bus trace"), [])

    def test_retired_emulator_is_not_blessed_by_the_active_one(self):
        active = "// [D] WinUAE @ Hatari e77819f7\n"
        for wrong in ("// Musashi agrees", "// CLK rule", "// [D] Musashi agrees",
                      "// [D] Musashi @ latest"):
            self.assertTrue(grades.other_emulator_problems(active + wrong))
        self.assertEqual(grades.other_emulator_problems(
            "// [F] Musashi tree revision unavailable\n"
            "// Settled by: replay at a pinned revision"), [])
        self.assertEqual(grades.other_emulator_problems("// Musashi compatibility"), [])
        self.assertEqual(grades.other_emulator_problems("// [D] Musashi @ 4.60"), [])

    def test_manual_catalog_has_bounded_locators_and_pdf_identity(self):
        catalog = grades.manual_catalog()
        self.assertEqual(grades.catalog_problems(catalog), [])
        import copy
        bad = copy.deepcopy(catalog)
        bad["MC68030UM-1989"]["sha256"] = "unverified"
        self.assertTrue(grades.catalog_problems(bad))
        bad = copy.deepcopy(catalog)
        bad["MC68030UM-1989"]["locators"]["§9.7.2"] = 10000
        self.assertTrue(grades.catalog_problems(bad))

    def test_optional_pdf_verification_rejects_changed_and_missing_bytes(self):
        original = b"reviewed source bytes"
        source = {"checked-edition": {"file": "source.pdf",
                  "sha256": hashlib.sha256(original).hexdigest()}}
        with tempfile.TemporaryDirectory() as directory:
            pdf = Path(directory) / "source.pdf"
            pdf.write_bytes(original)
            with patch.object(grades, "manual_catalog", return_value=source), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(grades.verify_manual_files(directory), 0)
                pdf.write_bytes(original + b"changed")
                self.assertEqual(grades.verify_manual_files(directory), 1)
                pdf.unlink()
                self.assertEqual(grades.verify_manual_files(directory), 1)


if __name__ == "__main__":
    result = unittest.TextTestRunner().run(
        unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceCheckerTest))
    sys.exit(grades.check() if result.wasSuccessful() else 1)
