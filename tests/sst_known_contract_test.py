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

    # How a vector whose rule holds is counted: a D23a vector is judged
    # against a whole derived state and counts as agreement; D12 as ruled.
    agreed = '1/1'
    known = '0 known divergence(s)'

    @classmethod
    def setUpClass(cls):
        # D23a (redefined 2026-11-26): the fixture's oracle frame is WinUAE's
        # 12-byte format $3 for a 68882 FMOVE FPn,<ea>; the rule derives the
        # manual's 20-byte format-$9 mid-instruction frame from it
        # (MC68030UM-1989 Figure 10-43, tests/SstKnown.h).
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
        self.assertIn(f'{self.agreed} across', out)
        self.assertIn(self.known, out)

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


class MidInstructionContract(unittest.TestCase):
    """The format-$9 fields derived from the oracle's frame are compared. The
    internal register (+$0C, [F]) is not: the oracle's PC sits under it, and
    the whole-vector replay above passes with --strict-ram."""
    runner = None
    emulator = []
    fixture, tag, instruction, agreed = (FrameContract.fixture, FrameContract.tag,
                                         FrameContract.instruction, FrameContract.agreed)
    make_rule = classmethod(FrameContract.make_rule.__func__)
    setUpClass = classmethod(FrameContract.setUpClass.__func__)
    replay = FrameContract.replay

    def oracle_frame_changed(self, offset, value):
        row = copy.deepcopy(self.original)
        ram = dict(row['final']['ram'])
        ram[self.frame + offset] = value
        row['final']['ram'] = sorted(ram.items())
        return self.replay(row)

    def test_oracle_frame_fails_unruled(self):
        code, out = self.replay(rule='')
        self.assertEqual(code, 1, out)
        self.assertIn('A7 got $17EC want $17F4', out)

    def test_scan_pc_is_compared(self):
        code, out = self.oracle_frame_changed(5, self.original_byte(5) ^ 2)
        self.assertEqual(code, 1, out)
        self.assertIn(f'RAM[${self.frame - 8 + 5:06X}]', out)

    def test_vector_is_compared(self):
        code, out = self.oracle_frame_changed(7, self.original_byte(7) ^ 4)
        self.assertEqual(code, 1, out)
        self.assertIn(f'RAM[${self.frame - 8 + 7:06X}]', out)

    def test_effective_address_is_compared(self):
        code, out = self.oracle_frame_changed(11, self.original_byte(11) ^ 1)
        self.assertEqual(code, 1, out)
        self.assertIn(f'RAM[${self.frame + 11:06X}]', out)

    def test_post_instruction_rule_is_stale(self):
        # A format-$2 frame (the 2026-11-16 reading of this ruling) is no
        # longer an oracle frame the rule accepts.
        code, out = self.oracle_frame_changed(6, 0x20)
        self.assertEqual(code, 1, out)
        self.assertIn('stale or invalid rule', out)

    def original_byte(self, offset):
        return dict(self.original['final']['ram']).get(self.frame + offset, 0)


class Group2Contract(FrameContract):
    fixture = 'd12-contract.json'
    tag = '@D12'
    agreed = '0/1'
    known = '1 known divergence(s)'

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


class OpenContract(unittest.TestCase):
    """`@OPEN <ruling> | <name>`: the core agrees with the corpus against a
    ruling. Counted open, never agreement; it must keep agreeing."""
    runner = None

    @classmethod
    def setUpClass(cls):
        # The D12 fixture with its manual instruction address written in: a
        # vector this core replays in full agreement.
        fixture = Path(__file__).with_name('cpu_vectors') / Group2Contract.fixture
        row = json.loads(fixture.read_text())[0]
        frame = row['final']['a7'] & 0xFFFFFF
        ram = dict(row['final']['ram'])
        for i in range(4):
            ram[frame + 8 + i] = (Group2Contract.instruction >> (24 - 8 * i)) & 0xFF
        row['final']['ram'] = sorted(ram.items())
        cls.original, cls.name = row, row['name']
        cls.rule = f'@OPEN D99 | {cls.name}\n'

    replay = FrameContract.replay

    def test_open_vector_counts_apart(self):
        code, out = self.replay()
        self.assertEqual(code, 0, out)
        self.assertIn('0 known divergence(s), 1 open', out)
        self.assertIn('0/1', out)

    def test_open_vector_that_stops_agreeing_fails(self):
        row = copy.deepcopy(self.original)
        row['final']['d7'] ^= 1
        code, out = self.replay(row)
        self.assertEqual(code, 1, out)
        self.assertIn('listed @OPEN', out)
        self.assertIn('D? got', out)

    def test_missing_open_vector_fails(self):
        code, out = self.replay(rule='@OPEN D99 | missing-vector\n')
        self.assertEqual(code, 1, out)
        self.assertIn('absent from the corpus', out)

    def test_invalid_open_rules_fail_closed(self):
        for rule in (f'@OPEN | {self.name}\n', f'@OPEN X99 | {self.name}\n',
                     f'@OPEN D99 {self.name}\n', '@OPEN D99 |\n',
                     self.rule + self.rule, self.rule + self.name + '\n',
                     self.name + '\n' + self.rule):
            with self.subTest(rule=rule):
                code, out = self.replay(rule=rule)
                self.assertEqual(code, 2, out)
                self.assertIn('cannot load --known', out)


class OpenOn040(OpenContract):
    runner = None


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--emulator', default='')
    parser.add_argument('--runner040', required=True)
    args, remaining = parser.parse_known_args()
    FrameContract.runner = args.runner
    FrameContract.emulator = [part for part in args.emulator.split(';') if part]
    Group2On040.runner = args.runner040
    OpenContract.runner = args.runner
    OpenOn040.runner = args.runner040
    OpenContract.emulator = FrameContract.emulator
    MidInstructionContract.runner = args.runner
    MidInstructionContract.emulator = FrameContract.emulator
    unittest.main(argv=[sys.argv[0]] + remaining)
