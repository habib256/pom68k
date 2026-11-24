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
    fixture = 'd23a-contract.json'
    tag = '@D23a'
    instruction = 0x1000

    @classmethod
    def make_rule(cls, frame, name):
        instruction = f' {cls.instruction:08x}' if cls.tag == '@D12' else ''
        return f'{cls.tag} {frame:06x}{instruction} | {name}\n'

    @classmethod
    def setUpClass(cls):
        if cls.tag == '@D23a':
            # Ruling 2026-11-24: the D23a vector stacks WinUAE's post-instruction
            # frame for a 68882 FMOVE; MC68881UM-1989 §6.1 says the 6888x reports
            # no post-instruction exception, and Dirk Hoffmann's core takes none.
            # There is no frame left to rule; the D12 contracts below keep the
            # --known mechanism under test.
            raise unittest.SkipTest('D23a: no post-instruction frame on the 6888x (MC68881UM §6.1)')
        fixture = Path(__file__).with_name('cpu_vectors') / cls.fixture
        cls.original = json.loads(fixture.read_text())[0]
        cls.frame = cls.original['final']['a7'] & 0xFFFFFF
        cls.name = cls.original['name']
        cls.rule = cls.make_rule(cls.frame, cls.name)

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
        if self.tag == '@D12':
            for i in range(4):
                ram[self.frame + 8 + i] = (self.instruction >> (24 - 8 * i)) & 0xFF
        else:
            self.assertEqual(ram[self.frame + 6], 0x30)
            ram[self.frame + 6] = 0x20
        row['final']['ram'] = sorted(ram.items())
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)

    def test_wrong_frame_location_fails(self):
        code, out = self.replay(rule=self.make_rule(self.frame + 2, self.name))
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)

    def test_missing_vector_fails(self):
        code, out = self.replay(rule=self.make_rule(self.frame, 'missing-vector'))
        self.assertEqual(code, 1, out)
        self.assertIn('absent from the corpus', out)

    def test_invalid_rules_fail_closed(self):
        for rule in (self.make_rule(self.frame, self.name).replace(f'{self.frame:06x}', 'xyz', 1),
                     self.make_rule(0x1000000, self.name),
                     self.make_rule(self.frame + 1, self.name),
                     self.rule.replace(' | ', ' '),
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


class Group2Contract(FrameContract):
    fixture = 'd12-contract.json'
    tag = '@D12'

    def test_wrong_instruction_address_still_fails(self):
        code, out = self.replay(rule=self.rule.replace('00001000', '00001004'))
        self.assertEqual(code, 1, out)
        self.assertIn('RAM[', out)

    def test_invalid_instruction_addresses_fail_closed(self):
        for address in ('xyz', '100000000', '1001', '', '-2'):
            with self.subTest(address=address):
                code, out = self.replay(rule=self.rule.replace('00001000', address))
                self.assertEqual(code, 2, out)

    def test_resume_pc_is_still_compared(self):
        row = copy.deepcopy(self.original)
        ram = dict(row['final']['ram'])
        ram[self.frame + 5] ^= 2
        row['final']['ram'] = sorted(ram.items())
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)


class Group2On040(Group2Contract):
    runner = None


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--emulator', default='')
    parser.add_argument('--runner040', required=True)
    args, remaining = parser.parse_known_args()
    FrameContract.runner = args.runner
    Group2On040.runner = args.runner040
    FrameContract.emulator = [part for part in args.emulator.split(';') if part]
    unittest.main(argv=[sys.argv[0]] + remaining)
