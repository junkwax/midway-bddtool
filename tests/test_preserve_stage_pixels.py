"""Regression tests for the isolated baseline-pixel compatibility pass."""
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import preserve_stage_pixels as preservation
from preserve_stage_pixels import restore_pixels


class PreservationTests(unittest.TestCase):
    def setUp(self):
        # Two 4-BPP pixels at bit offset three, with unrelated prefix bits set.
        self.before = (5 | (9 << 3) | (7 << 7)).to_bytes(2, 'little')
        self.after = (5 | (8 << 3) | (7 << 7)).to_bytes(2, 'little')
        self.header = [2, 1, 0x2000003, 0x4000]
        self.tables = {'AHDRS': [self.header], 'BHDRS': [self.header]}
        self.approved = [('AHDRS', 0), ('BHDRS', 0)]

    def test_shared_pixel_restored_without_touching_neighbors(self):
        fixed, changes = restore_pixels(self.before, self.after, 0, 0, self.tables,
                                        self.tables, self.approved)
        self.assertEqual(fixed, self.before)
        self.assertEqual(len(changes), 2)  # One physical pixel, two consumers.
        self.assertEqual(sum((a ^ b).bit_count() for a, b in zip(fixed, self.after)), 1)
        again, changes = restore_pixels(self.before, fixed, 0, 0, self.tables,
                                        self.tables, self.approved)
        self.assertEqual(again, fixed)
        self.assertEqual(changes, [])

    def test_unapproved_and_sprite_aliases_refused(self):
        with self.assertRaisesRegex(ValueError, 'alias'):
            restore_pixels(self.before, self.after, 0, 0, self.tables, self.tables,
                           [('AHDRS', 0)])
        with self.assertRaisesRegex(ValueError, 'sprite'):
            restore_pixels(self.before, self.after, 0, 0, self.tables, self.tables,
                           self.approved, other_spans=[(5, 6)])

    def test_edited_alias_cannot_be_restored(self):
        with self.assertRaisesRegex(ValueError, 'edited'):
            restore_pixels(self.before, self.after, 0, 0, self.tables, self.tables,
                           self.approved, edited=['BHDRS'])

    def test_partial_background_alias_refused(self):
        partial = {**self.tables, 'BHDRS': [[2, 1, 0x2000007, 0x4000]]}
        with self.assertRaisesRegex(ValueError, 'partial background alias'):
            restore_pixels(self.before, self.after, 0, 0, partial, partial, self.approved)

    def test_changed_geometry_and_compression_refused(self):
        changed = {'AHDRS': [[1, 2, 0x2000003, 0x4000]], 'BHDRS': [self.header]}
        with self.assertRaisesRegex(ValueError, 'geometry'):
            restore_pixels(self.before, self.after, 0, 0, self.tables, changed, self.approved)
        compressed = {'AHDRS': [[2, 1, 0x2000000, 0x4080]]}
        with self.assertRaisesRegex(ValueError, 'Compressed'):
            restore_pixels(b'\0\x79', b'\0\x78', 0, 0, compressed, compressed,
                           [('AHDRS', 0)])

    def test_unapproved_changed_record_is_not_hidden(self):
        extra = {**self.tables, 'CHDRS': [[2, 1, 0x2000010, 0x4000]]}
        with self.assertRaisesRegex(ValueError, 'CHDRS'):
            restore_pixels(self.before + b'\x21', self.after + b'\x31', 0, 0,
                           extra, extra, self.approved)

    def test_disagreeing_baseline_alias_refused(self):
        old = {**self.tables, 'BHDRS': [[2, 1, 0x2000010, 0x4000]]}
        with self.assertRaisesRegex(ValueError, 'Conflicting|verification'):
            restore_pixels(self.before + b'\x78', self.after, 0, 0,
                           old, self.tables, self.approved)

    def test_large_change_refused(self):
        table = {'AHDRS': [[32, 1, 0x2000000, 0x4000]]}
        with self.assertRaisesRegex(ValueError, '16 changed'):
            restore_pixels(b'\x11' * 16, b'\x22' * 16, 0, 0, table, table, [('AHDRS', 0)])

    def test_legacy_overflow_depends_on_word_position(self):
        # Reproduce LOAD2's 16-bit writer: oversized pixel data is not masked.
        def legacy(start):
            word, filled, output = 0, start, []
            for value in [0] * 14 + [17, 8] + [0] * 8:
                word = (word | (value << filled)) & 0xffff
                filled += 4
                if filled >= 16:
                    output.extend(word.to_bytes(2, 'little'))
                    filled -= 16
                    word = (value >> (4 - filled)) if filled else 0
            data = int.from_bytes(bytes(output), 'little')
            return (data >> (start + 15 * 4)) & 15
        self.assertEqual(legacy(2), 9)
        self.assertEqual(legacy(4), 8)

    def test_new_artifact_keeps_inputs_and_updates_checksum(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            before, after, output = root / 'before', root / 'after', root / 'output'
            originals = {}
            for directory, pixels in [(before, self.before), (after, self.after)]:
                (directory / 'data').mkdir(parents=True)
                (directory / 'tmp/load2').mkdir(parents=True)
                raw = bytearray(68) + pixels
                struct.pack_into('<I', raw, 48, len(pixels))
                struct.pack_into('<I', raw, 60, 1)
                originals[directory] = bytes(raw)
                (directory / 'data/MK7MIL.IRW').write_bytes(raw)
                for ext in ('BDD', 'BDB'):
                    (directory / ('data/STAGE.' + ext)).write_bytes(b'unchanged fixture')
                (directory / 'tmp/load2/BGNDTBL.MK7').write_text(
                    'AHDRS:\n .word 2,1\n .long 33554435\n .word 16384\n')

            reader = SimpleNamespace(_num=int, load_bdd=lambda _p: ({1: {'w': 2, 'h': 1}}, [1], []))
            video = SimpleNamespace(parse_irw=lambda p: (0, 1, p.read_bytes()[68:]),
                                    BANK_OFFSETS={1: 0x800000}, image_byte_offset=lambda b, k: 0)
            sprites = SimpleNamespace(parse_tbls=lambda _p: [])
            modules = {'preview': reader, 'video': video, 'sprite_reader': sprites}
            with patch.object(preservation.packing, 'module', lambda _p, name: modules[name]):
                report = preservation.preserve(before, after, output, ['STAGE:AHDRS:0'], [])
                raw = (output / 'MK7MIL.IRW').read_bytes()
                self.assertEqual(raw[68:], self.before)
                self.assertEqual(struct.unpack_from('<I', raw, 52)[0],
                                 int.from_bytes(self.before, 'little'))
                self.assertEqual(report['changed_payload_bits'], 1)
                self.assertEqual(raw[:52] + raw[56:68], originals[after][:52] + originals[after][56:68])
                for directory in [before, after]:
                    self.assertEqual((directory / 'data/MK7MIL.IRW').read_bytes(), originals[directory])
                with self.assertRaisesRegex(ValueError, 'already exists'):
                    preservation.preserve(before, after, output, ['STAGE:AHDRS:0'], [])
                # Bank-one sprite SAGs are flat addresses, not raw IRW bases.
                sprites.parse_tbls = lambda _p: [('FRAME.TBL', 'ALIAS', 2, 1, 0x4000003, 0x4000)]
                with self.assertRaisesRegex(ValueError, 'sprite'):
                    preservation.preserve(before, after, root / 'alias-refused',
                                          ['STAGE:AHDRS:0'], [])
                self.assertFalse((root / 'alias-refused').exists())
                sprites.parse_tbls = lambda _p: []
                (after / 'data/STAGE.BDD').write_bytes(b'changed source')
                with self.assertRaisesRegex(ValueError, 'Authored source changed'):
                    preservation.preserve(before, after, root / 'refused', ['STAGE:AHDRS:0'], [])
                self.assertFalse((root / 'refused').exists())


if __name__ == '__main__':
    unittest.main()
