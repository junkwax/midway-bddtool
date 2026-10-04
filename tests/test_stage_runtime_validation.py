from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import stage_runtime_validation as runtime


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, label, boot=0, pixel=0, drops=0, peak=2, failure=False):
        root = self.root / label
        (root / 'screens').mkdir(parents=True)
        lines = ['XInput device detection failed. Error: 0x80041003', '[bhp] PASS fixture']
        for i in range(12):
            frame = 1000 + boot + i * 30
            lines.append(f'[validation] SNAP frame={frame} sample={30*(i+1)} stage=0 death=0')
            Image.new('RGB', (2, 2), (pixel, 0, 0)).save(root / 'screens' / f'{i:04}.png')
        lines.append(f'[validation] METRICS samples=360 min_free=220 peak={peak} drops={drops} '
                     'wrong_stage=0 broken_list=0 captures=12 queue_words=600 queue_overflows=0 '
                     'late_frames=0 lost_entries=0 palette_drops=0 palette_failures=0')
        if failure:
            lines.append('[bhp] ERROR simulation failed')
        (root / 'process.log').write_text('\n'.join(lines))
        return root

    def test_boot_offset_does_not_hide_relative_timing(self):
        a, b = self.fixture('a'), self.fixture('b', boot=1)
        self.assertTrue(runtime.compare(a, b, '[bhp] PASS')['passed'])
        path = b / 'process.log'
        path.write_text(path.read_text().replace('frame=1031', 'frame=1032'))
        with self.assertRaisesRegex(ValueError, 'timing/state diverged'):
            runtime.compare(a, b, '[bhp] PASS')

    def test_changed_pixels_or_runtime_pressure_fail(self):
        a = self.fixture('a')
        for name, settings in [('pixels', {'pixel': 1}), ('drops', {'drops': 1}), ('peak', {'peak': 3})]:
            b = self.fixture(name, **settings)
            self.assertFalse(runtime.compare(a, b, '[bhp] PASS')['passed'])

    def test_failure_or_missing_capture_refused(self):
        a, b = self.fixture('a'), self.fixture('b', failure=True)
        with self.assertRaisesRegex(ValueError, 'reported a failure'):
            runtime.compare(a, b, '[bhp] PASS')
        c = self.fixture('c')
        (c / 'screens/0000.png').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or unmatched'):
            runtime.compare(a, c, '[bhp] PASS')

    def test_object_queue_pressure_cannot_hide_behind_zero_misc_drops(self):
        a, b = self.fixture('a'), self.fixture('b')
        path = b / 'process.log'
        good = path.read_text()
        for old, new in [('queue_overflows=0', 'queue_overflows=1'),
                         ('queue_words=600', 'queue_words=3006'),
                         ('late_frames=0', 'late_frames=1'), ('lost_entries=0', 'lost_entries=1'),
                         ('palette_drops=0', 'palette_drops=1'), ('palette_failures=0', 'palette_failures=1')]:
            path.write_text(good.replace(old, new))
            self.assertFalse(runtime.compare(a, b, '[bhp] PASS')['passed'])
        path.write_text(good.replace('late_frames=0', ''))
        with self.assertRaisesRegex(ValueError, 'Incomplete runtime pressure'):
            runtime.compare(a, b, '[bhp] PASS')

    def test_inherited_late_drawing_allowed_only_without_increase(self):
        a, b = self.fixture('a'), self.fixture('b')
        for root, lost in ((a, 111), (b, 108)):
            path = root / 'process.log'
            path.write_text(path.read_text().replace('late_frames=0', 'late_frames=1')
                            .replace('lost_entries=0', f'lost_entries={lost}'))
        self.assertTrue(runtime.compare(a, b, '[bhp] PASS')['passed'])
        path = b / 'process.log'
        path.write_text(path.read_text().replace('late_frames=1', 'late_frames=2'))
        result = runtime.compare(a, b, '[bhp] PASS')
        self.assertFalse(result['passed'])
        self.assertEqual(result['failures'], ['late_frames increased: 1 -> 2'])

    def test_negative_camera_coordinates_are_part_of_the_state_trace(self):
        a, b = self.fixture('a'), self.fixture('b')
        for root, camera in ((a, -2), (b, -3)):
            path = root / 'process.log'
            path.write_text(path.read_text().replace('death=0', f'death=0 camera={camera}'))
        with self.assertRaisesRegex(ValueError, 'timing/state diverged'):
            runtime.compare(a, b, '[bhp] PASS')

    def test_probe_drift_refused(self):
        with self.assertRaisesRegex(ValueError, 'Probe version changed'):
            runtime.adapt('unrecognized source', 'lightning-0', 0, self.root)

    def test_play_requires_real_travel_damage_and_expected_matchup(self):
        root = self.fixture('play')
        path = root / 'process.log'
        original = path.read_text()
        good = ('[validation] PLAY samples=2400 frames=2401 fight_exits=1 camera_min=-10 camera_max=400 '
                'p1_min_hp=0 p2_min_hp=161 wrong_matchup=0 transition_captures=16')
        path.write_text(original + '\n' + good)
        self.assertEqual(runtime.inspect_play(root)['camera_min'], -10)
        for old, new in [('samples=2400', 'samples=300'), ('frames=2401', 'frames=600'),
                         ('camera_max=400', 'camera_max=20'),
                         ('p1_min_hp=0', 'p1_min_hp=161'), ('wrong_matchup=0', 'wrong_matchup=1'),
                         ('transition_captures=16', 'transition_captures=0')]:
            path.write_text(original + '\n' + good.replace(old, new))
            with self.assertRaises(ValueError):
                runtime.inspect_play(root)

    def test_play_rejects_game_probe_failures_and_state_divergence(self):
        a, b = self.fixture('a'), self.fixture('b')
        coverage = ('\n[validation] PLAY samples=2400 frames=2401 fight_exits=1 camera_min=0 camera_max=400 '
                    'p1_min_hp=0 p2_min_hp=161 wrong_matchup=0 transition_captures=16')
        for root in (a, b):
            path = root / 'process.log'
            path.write_text(path.read_text().replace('[bhp] PASS fixture', '[sweep] PASS fixture') + coverage)
        self.assertTrue(runtime.compare(a, b, '[sweep] PASS ')['passed'])
        path = b / 'process.log'
        good = path.read_text()
        path.write_text(good.replace('camera_max=400', 'camera_max=401'))
        with self.assertRaisesRegex(ValueError, 'state diverged'):
            runtime.compare(a, b, '[sweep] PASS ')
        path.write_text(good + '\n[sweep] FAIL crash')
        with self.assertRaisesRegex(ValueError, 'reported a failure'):
            runtime.compare(a, b, '[sweep] PASS ')

    def test_play_disables_optional_local_addon(self):
        source = ('local probe = io.open(addon, "r")\n'
                  'if wrel < OBSERVE*0.45 then walk_dir=1 else walk_dir=-1 end')
        self.assertIn('local probe = nil', runtime.adapt(source, 'play-kang-jax-0', 0, self.root))
        with self.assertRaisesRegex(ValueError, 'Probe version changed'):
            runtime.adapt('changed addon loader', 'play-kang-jax-0', 0, self.root)

    def test_other_stage_cases_do_not_retarget_acid_probes(self):
        cases = runtime.scenarios()
        for stage in (1, 7, 0x16, 0x18, 0x19):
            selected = {name: case for name, case in cases.items() if case[0] == stage}
            self.assertEqual(set(selected), {f'lightning-{stage}', f'decap-{stage}',
                                            f'play-kang-jax-{stage}', f'play-baraka-scorpion-{stage}'})
            for name, (_, _, settings, _) in selected.items():
                if name.startswith('play-'):
                    self.assertEqual(settings['SWEEP_STAGE'], str(stage))
                    self.assertEqual(settings['SWEEP_NO_HP_PIN'], '1')
        self.assertEqual(cases['slime-left'][0], 0)
        self.assertEqual(cases['slime-right'][0], 0)


if __name__ == '__main__':
    unittest.main()
