"""Synthetic checks; no private stage assets or game checkout required."""
import importlib.util
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'packing', Path(__file__).resolve().parents[1] / 'tools/verify_static_stage_packing.py')
packing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packing)


class HeaderTests(unittest.TestCase):
    def test_decode_bits_and_compressed_rows(self):
        # Three-bit pixels start at bit 3 and cross byte boundaries.
        packed = sum(v << (3 + i * 3) for i, v in enumerate([1, 7, 2, 6]))
        pixels, start, end = packing.decode_header(
            packed.to_bytes(2, 'little'), 0, [4, 1, 0x2000003, 0x3000])
        self.assertEqual((pixels, start, end), (bytes([1, 7, 2, 6]), 3, 15))
        # One transparent pixel on each edge; each row has its own run header.
        pixels, start, end = packing.decode_header(
            bytes([0x11, 0x71, 0x11, 0x62]), 0, [4, 2, 0x2000000, 0x4080])
        self.assertEqual(pixels, bytes([0, 1, 7, 0, 0, 2, 6, 0]))
        self.assertEqual(end, 32)
        with self.assertRaises(AssertionError):
            packing.decode_header(bytes([0x55]), 0, [4, 1, 0x2000000, 0x4080])
        with self.assertRaises(AssertionError):
            packing.decode_header(b'', 0, [4, 1, 0x2000000, 0x3000])

    def test_shared_payloads(self):
        tables = packing.header_tables('''OLHDRS:
    .word 4,8 ; dimensions
    .long 100
    .word 20480
    .word 8,8
    .long 200
    .word 24576
STOP:
    .word 999
HDRS:
    .word 4,8
    .long 100
    .word 20480
alHDRS:
    .word 4,8
    .long 100
    .word 20480
''', int)
        self.assertEqual(len(tables['OLHDRS']), 2)
        self.assertEqual(packing.shared_images(tables, 'OLHDRS', [36, 21]), [
            {'image': 36, 'address': 100, 'other_header_tables': ['HDRS', 'alHDRS']}])
        # Repeated references within a single stage are not cross-stage sharing.
        self.assertEqual(packing.shared_images({'HDRS': [[4, 8, 100, 0]] * 2},
                                              'HDRS', [1, 2]), [])

    def test_incomplete_header_rejected(self):
        with self.assertRaises(AssertionError):
            packing.header_tables('HDRS:\n .word 4,8\n', int)

    def test_bank_comparison_detects_other_stage_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            roots = [Path(folder) / name for name in ['before', 'after']]
            for root, address in zip(roots, [0x2000000, 0x2000008]):
                (root / 'tmp/load2').mkdir(parents=True)
                (root / 'tmp/load2/BGNDTBL.MK7').write_text(
                    f'KEEPHDRS:\n .word 2,1\n .long {address}\n .word 16384\n'
                    f'EDITHDRS:\n .word 2,1\n .long {address}\n .word 16384\n')
            payload_after = b'\0\x21'

            def parse(path):
                return 0, 1, b'\x21' if roots[0] in path.parents else payload_after

            def module(_path, name):
                return (SimpleNamespace(_num=int) if name == 'preview'
                        else SimpleNamespace(parse_irw=parse))

            with patch.object(packing, 'module', module):
                result = packing.compare_stage_bank(*roots, ['EDITHDRS'])
                self.assertTrue(result['unchanged_stage_pixels_identical'])
                # A relocated unchanged image passes; one changed pixel fails.
                payload_after = b'\0\x31'
                result = packing.compare_stage_bank(*roots, ['EDITHDRS'])
                self.assertFalse(result['unchanged_stage_pixels_identical'])
                self.assertEqual(result['tables'][0]['changes'][0]['header_index'], 0)
                with self.assertRaises(AssertionError):
                    packing.compare_stage_bank(*roots, ['TYPOHDRS'])


if __name__ == '__main__':
    unittest.main()
