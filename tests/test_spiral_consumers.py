"""Static consumer proof regressions; no private game inputs required."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_spiral_consumers as audit


class ConsumerTests(unittest.TestCase):
    def test_records_stop_at_next_label_and_ignore_comments(self):
        text = 'p:\t; ignored 999\n .word 2 ; length\n .word 0,17\nq:\n .word 42\n'
        self.assertEqual(audit.record(text, 'p'), ['2', '0', '17'])
        with self.assertRaises(ValueError):
            audit.record(text + '\np:\n .word 1\n', 'p')
        with self.assertRaises(ValueError):
            audit.record(text, 'missing')

    def test_block_header_and_palette_bits(self):
        rows = audit.blocks(['16435', '201', '151', str(0x8009), '65535'], int)
        self.assertEqual(rows[0]['hi'], 9)
        self.assertEqual(rows[0]['pal'], 35)
        self.assertEqual(rows[0]['z'] & 48, 48)
        with self.assertRaises(ValueError):
            audit.blocks(['1', '2', '65535'], int)

    def test_joint_palette_prevents_wrong_recolor_merge(self):
        portal = [0, 15, 15, 0]
        haven = [0, 20, 21, 0]
        self.assertEqual(audit.equivalence([0, 1, 2, 3], [portal]), [[1, 2], [3]])
        self.assertEqual(audit.equivalence([0, 1, 2, 3], [portal, haven]), [[1], [2], [3]])
        with self.assertRaises(ValueError):
            audit.equivalence([4], [portal])

    def test_alternate_palette_transfer_handles_both_flip_axes(self):
        before = dict(w=2, h=2, px=bytes([1, 2, 3, 0]))
        after = dict(w=2, h=2, px=bytes([0, 1, 2, 3]))
        mapping = {}
        audit.transfer_palette(before, after, [0, 10, 20, 30], mapping, 48)
        self.assertEqual(mapping, {3: 10, 2: 20, 1: 30})
        with self.assertRaises(ValueError):
            audit.transfer_palette(before, after, [0, 10, 20, 30], {})

    def test_transfer_rejects_conflicting_shared_palette_and_transparency(self):
        before = dict(w=2, h=1, px=bytes([1, 2]))
        after = dict(w=2, h=1, px=bytes([1, 1]))
        with self.assertRaises(ValueError):
            audit.transfer_palette(before, after, [0, 10, 20], {})
        with self.assertRaises(ValueError):
            audit.transfer_palette(before, dict(w=2, h=1, px=bytes([0, 1])), [0, 10, 20], {})
        with self.assertRaises(ValueError):
            audit.transfer_palette(before, dict(w=1, h=2, px=bytes([1, 2])), [0, 10, 20], {})

    def test_render_preserves_opaque_black_and_both_flips(self):
        images = {9: dict(w=2, h=2, px=bytes([0, 1, 2, 3]))}
        palettes = [[123, 0, 12, 13]]
        b = dict(z=48, x=1, y=0, hi=0, pal=0)
        result = audit.render(images, [9], palettes, [b], 3, 2)
        self.assertEqual(result, [0, 65549, 65548, 0, 65536, 0])


if __name__ == '__main__':
    unittest.main()
