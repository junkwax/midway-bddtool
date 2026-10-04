"""Guards for repeatable scratch builds and CRC-padded diagnostic ROMs."""
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import reviewed_stage_build as build


class ReviewedBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def test_snapshot_detects_changed_and_added_nested_inputs(self):
        source = self.root / 'source'
        (source / 'data/nested').mkdir(parents=True)
        image = source / 'data/nested/art.IMG'
        image.write_bytes(b'original')
        pinned = build.inventory(source)
        copy = self.root / 'copy'
        build.copy_verified(source, pinned, copy)
        self.assertEqual(build.inventory(copy), pinned)
        image.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'input changed'):
            build.verify(source, pinned, exact=True)
        image.write_bytes(b'original')
        (image.parent / 'new.IMG').write_bytes(b'new')
        with self.assertRaisesRegex(ValueError, 'input set changed'):
            build.verify(source, pinned, exact=True)
        self.assertEqual((copy / 'data/nested/art.IMG').read_bytes(), b'original')

    def test_mirrored_split_rows_must_be_sorted_within_each_module(self):
        modules = [{'name': 'BAT4', 'blocks': [{'x': 492}, {'x': 687}, {'x': 627}]}]
        with self.assertRaisesRegex(ValueError, 'BATTLE/BAT4: X=687 before X=627'):
            build.check_module_order(modules, 'BATTLE')
        modules[0]['blocks'].sort(key=lambda b: b['x'])
        modules.append({'name': 'BAT2', 'blocks': [{'x': 10}, {'x': 10}, {'x': 20}]})
        build.check_module_order(modules, 'BATTLE')  # Independent module origins and ties are valid.

    def test_rejects_escape_and_overlapping_or_existing_output(self):
        for rel in ('../outside', str(self.root), '.'):
            with self.assertRaises(ValueError):
                build.contained(self.root, rel)
        for out, source in ((self.root, self.root / 'source'),
                            (self.root / 'source/run', self.root / 'source')):
            with self.assertRaises(ValueError):
                build.separate(out, [source])

    def zip_fixture(self, changed_at=1, lane_override=None):
        (self.root / 'rom').mkdir()
        chips = [(f'chip{i}', (i // 4) * 16, 4, i % 4) for i in range(12)]
        if lane_override is not None:
            chips[0] = ('chip0', 0, 4, lane_override)
        (self.root / 'makevrom.py').write_text('MAME_ROMS = ' + repr(chips))
        receipt = self.root / 'receipt'
        # Only flat byte zero is occupied. Chip0 byte1 is padding (flat byte4).
        receipt.write_text('BDDROM 3\n"fixture" 0\n1\n"MK7MIL.IRW" 0 0 1 "hash"\n')
        archive = self.root / 'output.zip'
        targets = {}
        with zipfile.ZipFile(archive, 'w') as zipped:
            for name, _, _, _ in chips:
                (self.root / 'rom' / name).write_bytes(bytes(4))
                data = bytearray(4)
                if name == 'chip0':
                    data[changed_at] = 1
                zipped.writestr(name, data)
                targets[name] = zlib.crc32(data)
        (self.root / 'crc_spoof.py').write_text('TARGETS = ' + repr(targets))
        return archive, receipt

    def test_crc_padding_may_change(self):
        archive, receipt = self.zip_fixture()
        self.assertEqual(build.verify_zip(self.root, archive, receipt), 1)

    def test_zero_filled_payload_is_not_crc_padding(self):
        _, receipt = self.zip_fixture()
        # First window is entirely zero but includes a real payload byte.
        with self.assertRaisesRegex(ValueError, 'No verified video padding'):
            build.video_patch_offsets(self.root, receipt)
        receipt.write_text('BDDROM 3\n"fixture" 0\n1\n"MK7MIL.IRW" 0 12 1 "hash"\n')
        # Four-byte chip has no spare four-byte window if any byte is occupied.
        with self.assertRaisesRegex(ValueError, 'No verified video padding'):
            build.video_patch_offsets(self.root, receipt)
        receipt.write_text('BDDROM 3\n"fixture" 0\n1\n"MK7MIL.IRW" 0 100 1 "hash"\n')
        self.assertEqual(build.video_patch_offsets(self.root, receipt)['chip0'], 0)

    def test_wrong_crc_refused_even_when_payload_is_exact(self):
        archive, receipt = self.zip_fixture()
        (self.root / 'crc_spoof.py').write_text('TARGETS = {}')
        with self.assertRaisesRegex(ValueError, 'CRC differs'):
            build.verify_zip(self.root, archive, receipt)

    def test_packaged_payload_must_remain_exact(self):
        archive, receipt = self.zip_fixture(changed_at=0)
        with self.assertRaisesRegex(ValueError, 'payload differs'):
            build.verify_zip(self.root, archive, receipt)

    def test_invalid_chip_mapping_refused(self):
        archive, receipt = self.zip_fixture(lane_override=-1)
        with self.assertRaisesRegex(ValueError, 'chip mapping'):
            build.verify_zip(self.root, archive, receipt)

    def test_truncated_receipt_refused(self):
        archive, receipt = self.zip_fixture()
        receipt.write_text('BDDROM 3\n"fixture" 0\n2\n')
        with self.assertRaisesRegex(ValueError, 'payload count'):
            build.verify_zip(self.root, archive, receipt)

    def job_fixture(self):
        plan = {'version': 1, 'verifier': 'fixture', 'tools': {'fixture': 'hash'},
                'allow_stock_bg_drift': False, 'git_dir': None, 'review': {'edited_tables': []}}
        for name in ('baseline', 'candidate', 'reference'):
            folder = self.root / name
            folder.mkdir()
            (folder / 'build.py').write_text('raise RuntimeError("failed build")')
            plan[name] = str(folder)
            plan[name + '_files'] = build.inventory(folder)
        for name, key in (('baseline.romreceipt', 'baseline_receipt_sha256'),
                          ('baseline-sprites.json', 'baseline_sprites_sha256')):
            (self.root / name).write_text('evidence')
            plan[key] = build.sha(self.root / name)
        path = self.root / 'job.json'
        build.write_json(path, plan)
        return path, self.root / 'output'

    def test_stale_input_fails_before_creating_scratch(self):
        job, output = self.job_fixture()
        (self.root / 'candidate/build.py').write_text('changed')
        with patch.object(build, 'tool_files', return_value={'fixture': 'hash'}):
            with self.assertRaisesRegex(ValueError, 'input changed'):
                build.run(job, output)
        self.assertFalse(output.exists())

    def test_wrong_candidate_or_adapter_refused(self):
        job, output = self.job_fixture()
        with self.assertRaisesRegex(ValueError, 'candidate differs'):
            build.run(job, output, self.root / 'wrong')
        with patch.object(build, 'tool_files', return_value={'fixture': 'changed'}):
            with self.assertRaisesRegex(ValueError, 'adapter changed'):
                build.run(job, output)
        self.assertFalse(output.exists())

    def test_failed_build_keeps_evidence_without_success_or_source_edits(self):
        job, output = self.job_fixture()
        before = build.inventory(self.root / 'candidate')
        with patch.object(build, 'tool_files', return_value={'fixture': 'hash'}):
            with self.assertRaisesRegex(ValueError, 'Step failed'):
                build.run(job, output)
        self.assertTrue((output / 'FAILED.txt').exists())
        self.assertTrue((output / 'build.log').exists())
        self.assertFalse((output / 'SUCCESS.json').exists())
        self.assertEqual(build.inventory(self.root / 'candidate'), before)


if __name__ == '__main__':
    unittest.main()
