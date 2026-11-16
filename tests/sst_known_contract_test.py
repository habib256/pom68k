#!/usr/bin/env python3
"""Manual frame rulings must keep every unrelated SST assertion active."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True


class FrameContract(unittest.TestCase):
    runner = None
    emulator = []

    @classmethod
    def setUpClass(cls):
        fixture = Path(__file__).with_name('cpu_vectors') / 'd23a-contract.json'
        cls.original = json.loads(fixture.read_text())[0]
        cls.frame = cls.original['final']['a7'] & 0xFFFFFF
        cls.name = cls.original['name']
        cls.rule = f'@D23a {cls.frame:06x} | {cls.name}\n'

    def replay(self, row=None, rule=None):
        row = copy.deepcopy(self.original if row is None else row)
        with tempfile.TemporaryDirectory(prefix='pom68k-sst-known-') as work:
            root = Path(work)
            corpus = root / 'vectors'
            corpus.mkdir()
            (corpus / 'one.json').write_text(json.dumps([row]))
            known = root / 'known.txt'
            known.write_text(self.rule if rule is None else rule)
            result = subprocess.run(self.emulator + [self.runner, str(corpus),
                                    '--strict-ram', '--known', str(known)],
                                    capture_output=True, text=True, timeout=30)
            return result.returncode, result.stdout + result.stderr

    def test_manual_frame_compares_whole_vector(self):
        code, out = self.replay()
        self.assertEqual(code, 0, out)
        self.assertIn('1 known divergence(s)', out)

    def test_unrelated_register_still_fails(self):
        row = copy.deepcopy(self.original)
        row['final']['d7'] ^= 1
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('D? got', out)

    def test_unrelated_ram_still_fails(self):
        row = copy.deepcopy(self.original)
        at = 0xF00000
        self.assertNotIn(at, dict(row['initial']['ram']))
        self.assertNotIn(at, dict(row['final']['ram']))
        row['final']['ram'].append([at, 0x5A])
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('RAM[$F00000]', out)

    def test_stale_oracle_frame_fails(self):
        row = copy.deepcopy(self.original)
        ram = dict(row['final']['ram'])
        self.assertEqual(ram[self.frame + 6], 0x30)
        ram[self.frame + 6] = 0x20
        row['final']['ram'] = sorted(ram.items())
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)

    def test_wrong_frame_location_fails(self):
        code, out = self.replay(rule=f'@D23a {self.frame + 2:06x} | {self.name}\n')
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)

    def test_missing_vector_fails(self):
        code, out = self.replay(rule=f'@D23a {self.frame:06x} | missing-vector\n')
        self.assertEqual(code, 1, out)
        self.assertIn('absent from the corpus', out)

    def test_invalid_rules_fail_closed(self):
        for rule in (f'@D23a xyz | {self.name}\n',
                     f'@D23a 1000000 | {self.name}\n',
                     f'@D23a {self.frame + 1:06x} | {self.name}\n',
                     f'@D23a {self.frame:06x} {self.name}\n',
                     self.rule + self.rule,
                     self.rule + self.name + '\n'):
            with self.subTest(rule=rule):
                code, out = self.replay(rule=rule)
                self.assertEqual(code, 2, out)
                self.assertIn('cannot load --known', out)

    def test_legacy_named_rules_remain_supported(self):
        code, out = self.replay(rule=self.name + '\n')
        self.assertEqual(code, 0, out)
        self.assertIn('1 known divergence(s)', out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--emulator', default='')
    args, remaining = parser.parse_known_args()
    FrameContract.runner = args.runner
    FrameContract.emulator = [part for part in args.emulator.split(';') if part]
    unittest.main(argv=[sys.argv[0]] + remaining)
