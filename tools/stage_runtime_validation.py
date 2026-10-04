"""Compare MK2 runtime probes without changing checkouts or installed ROMs.

Requires the local game's existing Lua probes, MAME and Pillow. Health/position
setup occurs in emulator RAM for the controlled cases. Play cases select a
matchup before fighting, then use ordinary inputs with natural damage/timing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from PIL import Image


def need(ok, why):
    if not ok:
        raise ValueError(why)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(source, old, new):
    need(source.count(old) == 1, 'Probe version changed: ' + old)
    return source.replace(old, new, 1)


def scenarios():
    cases = {}
    combat_stages = (0, 1, 2, 7, 0x16, 0x18, 0x19)
    for stage in combat_stages:
        cases[f'lightning-{stage}'] = (stage, 'backhp_standing_test.lua',
                                     {'BHP_CHAR': '7', 'BHP_ACT': '1', 'BHP_ACT_HI': '22',
                                      'BHP_LABEL': 'raiden_shortbolt'}, '[bhp] PASS ')
        cases[f'decap-{stage}'] = (stage, 'baraka_decap_frames_test.lua', {}, '[bdf] PASS ')
    for side, x in [('right', 310), ('left', 830)]:
        cases['slime-' + side] = (0, 'deadpool_slime_fall_test.lua',
                                {'DP_SIDE': side, 'DP_VX': str(x), 'DP_OBSERVE': '420'},
                                '[dpslime] RESULT PASS ')
    for stage in combat_stages:
        for matchup, p1, p2 in [('kang-jax', 1, 11), ('baraka-scorpion', 3, 10)]:
            cases[f'play-{matchup}-{stage}'] = (stage, 'battle_stability_sweep.lua',
                {'SWEEP_P1': str(p1), 'SWEEP_P2': str(p2), 'SWEEP_STAGE': str(stage),
                 'SWEEP_OBSERVE': '2400', 'SWEEP_NO_HP_PIN': '1', 'SWEEP_P2_HUMAN': '0',
                 'VALIDATION_PLAY': '1'}, '[sweep] PASS ')
    return cases


def adapt(source, kind, stage, root):
    source = source.replace('C:/Users/xbx/Workplace/mk2-main', root.as_posix())
    if kind.startswith('lightning'):
        source = replace_once(source, 'local PHASE_FRAMES = 1800', 'local PHASE_FRAMES = 3600')
        source = replace_once(source, 'w16(A.curback, 0)', f'w16(A.curback, {stage})')
        source = replace_once(source, 'if x1 then pin(A.p2_obj, x1 + 0x50) end',
            'if x1 then\n'
            '        local positions = {100, 550, 950}\n'
            '        local x = positions[1 + ((frame - fighting_frame) // 1200) % 3]\n'
            '        pin(A.p1_obj, x); pin(A.p2_obj, x + 0x50)\n'
            '    end')
    elif kind.startswith('decap'):
        source = replace_once(source, 'local STAGE_ID = 5', f'local STAGE_ID = {stage}')
    elif kind.startswith('play-'):
        # The game's optional addon is unreviewed, can write gameplay state,
        # and is deliberately excluded from these input-only fight comparisons.
        source = replace_once(source, 'local probe = io.open(addon, "r")', 'local probe = nil')
        # Start by retreating, and reverse periodically while the CPU attacks.
        # The original single late reversal can occur after a natural KO.
        source = replace_once(source,
            'if wrel < OBSERVE*0.45 then walk_dir=1 else walk_dir=-1 end',
            'walk_dir = ((wrel // 600) % 2 == 0) and -1 or 1')
    return 'local validation_print = print\n' + source + '\n' + Path(__file__).with_name(
        'stage_runtime_metrics.lua').read_text(encoding='utf-8')


def inspect(folder, marker):
    log = (folder / 'process.log').read_text(encoding='utf-8', errors='replace')
    need(marker in log, 'Probe did not report success: ' + str(folder))
    probe_lines = '\n'.join(s for s in log.splitlines()
                            if re.match(r'\[(?:bhp|bdf|dpslime|sweep|validation)\]', s))
    need(not re.search(r'\b(?:FAIL|ERROR|TIMEOUT|MISSING)\b|missing symbol', probe_lines,
                       re.IGNORECASE) and 'Probe load ERROR:' not in log,
         'Probe reported a failure: ' + str(folder))
    rows = re.findall(r'\[validation\] METRICS (.+)', log)
    need(len(rows) == 1, 'Missing or duplicate runtime measurements')
    metrics = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', rows[0])}
    need({'samples', 'captures', 'wrong_stage', 'broken_list', 'min_free', 'drops', 'peak',
          'queue_words', 'queue_overflows', 'late_frames', 'lost_entries',
          'palette_drops', 'palette_failures'} <= metrics.keys(),
         'Incomplete runtime pressure measurements')
    need(metrics['samples'] > 100 and metrics['captures'] > 0, 'Insufficient runtime samples')
    need(not metrics['wrong_stage'] and not metrics['broken_list'] and metrics['min_free'] > 0,
         'Wrong stage or invalid/exhausted object pool')
    snaps = [{k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', row)}
             for row in re.findall(r'\[validation\] SNAP (.+)', log)]
    need(len(snaps) == metrics['captures'], 'Incomplete capture trace')
    # Boot can vary by a frame. Preserve capture timing relative to the first
    # fighting sample, as well as the measured sample index and death state.
    if snaps:
        origin = snaps[0]['frame']
        for snap in snaps:
            snap['frame'] -= origin
    return metrics, snaps


def inspect_play(folder):
    log = (folder / 'process.log').read_text(encoding='utf-8', errors='replace')
    rows = re.findall(r'\[validation\] PLAY (.+)', log)
    need(len(rows) == 1, 'Missing or duplicate natural-play coverage')
    coverage = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', rows[0])}
    # A natural KO can end fighting well before the observation window ends.
    # Require both actual combat and a full window including result transitions.
    need(coverage['samples'] >= 600 and coverage['frames'] >= 2400,
         'Insufficient natural-play duration')
    need(coverage['camera_max'] - coverage['camera_min'] >= 100, 'Insufficient camera travel')
    need(min(coverage['p1_min_hp'], coverage['p2_min_hp']) < 161, 'No natural damage observed')
    need(coverage['wrong_matchup'] == 0, 'Natural-play matchup changed')
    need(coverage.get('transition_captures', 0) >= 8, 'Missing dense transition coverage')
    return coverage


def compare(before, after, marker):
    a, trace_a = inspect(before, marker)
    b, trace_b = inspect(after, marker)
    need(trace_a == trace_b, 'Runtime capture timing/state diverged')
    need(a['samples'] == b['samples'], 'Measured runtime duration diverged')
    files = [sorted((p / 'screens').rglob('*.png')) for p in (before, after)]
    need(len(files[0]) == len(files[1]) and len(files[0]) >= len(trace_a) > 10,
         'Missing or unmatched screenshots')
    checks = []
    for x, y in zip(*files):
        with Image.open(x) as im:
            sa, ba = im.size, im.convert('RGB').tobytes()
        with Image.open(y) as im:
            sb, bb = im.size, im.convert('RGB').tobytes()
        checks.append({'capture': x.relative_to(before / 'screens').as_posix(),
                       'identical': sa == sb and ba == bb,
                       'baseline_rgb_sha256': hashlib.sha256(ba).hexdigest(),
                       'candidate_rgb_sha256': hashlib.sha256(bb).hexdigest()})
    result = {'baseline': a, 'candidate': b, 'captures': checks,
              'identical': sum(c['identical'] for c in checks), 'total': len(checks)}
    failures = []
    if result['identical'] != result['total']:
        failures.append(f'Changed RGB captures: {result["total"] - result["identical"]}/{result["total"]}')
    for field in ('drops', 'queue_overflows', 'palette_drops', 'palette_failures'):
        if b[field]:
            failures.append(f'{field}: {b[field]} (required zero)')
    if b['queue_words'] > 3000:
        failures.append(f'queue_words: {b["queue_words"]} exceeds capacity 3000')
    for field in ('peak', 'late_frames', 'lost_entries'):
        if b[field] > a[field]:
            failures.append(f'{field} increased: {a[field]} -> {b[field]}')
    result['failures'] = failures
    result['passed'] = not failures
    if marker == '[sweep] PASS ':
        result['play_baseline'], result['play_candidate'] = inspect_play(before), inspect_play(after)
        need(result['play_baseline'] == result['play_candidate'], 'Natural-play state diverged')
    return result


def run(args):
    roots = [args.baseline.resolve(), args.candidate.resolve()]
    zips = [args.baseline_zip.resolve(), args.candidate_zip.resolve()]
    output, emulator = args.output.resolve(), args.emulator.resolve()
    need(not output.exists(), 'Use a new output folder')
    need(all(not output.is_relative_to(r) and not r.is_relative_to(output) for r in roots),
         'Keep runtime output separate from checkouts')
    cases = scenarios()
    selected = args.scenario or list(cases)
    inputs = [emulator, Path(__file__).resolve(), Path(__file__).with_name('stage_runtime_metrics.lua'), *zips]
    for root in roots:
        inputs.append(root / 'src/MK2.MAP')
        inputs.append(root / 'src/BGND.ASM')
        inputs.append(root / 'src/MKBGANI.TBL')
        inputs.extend(root / 'tools' / cases[k][1] for k in selected)
    manifest = {str(p.resolve()): sha(p) for p in inputs}
    output.mkdir(parents=True)
    (output / 'inputs.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    results = {}
    try:
        for case in selected:
            stage, script, settings, marker = cases[case]
            for label, root, archive in zip(('baseline', 'candidate'), roots, zips):
                folder = output / case / label
                folder.mkdir(parents=True)
                source = (root / 'tools' / script).read_text(encoding='utf-8')
                lua = folder / 'probe.lua'
                lua.write_text(adapt(source, case, stage, root), encoding='utf-8')
                bootstrap = folder / 'bootstrap.lua'
                bootstrap.write_text(
                    f'local f=assert(io.open({json.dumps((folder / "startup.log").as_posix())},"w"))\n'
                    'f:write("Loading probe\\n");f:flush()\n'
                    f'local ok,err=pcall(dofile,{json.dumps(lua.as_posix())})\n'
                    'f:write(ok and "Loaded\\n" or tostring(err));f:close()\n'
                    'if not ok then print("Probe load ERROR: "..tostring(err));manager.machine:exit() end\n',
                    encoding='utf-8')
                env = {k: v for k, v in os.environ.items()
                       if not k.upper().startswith(('BHP_', 'BDF_', 'DP_', 'SWEEP_', 'VALIDATION_'))}
                # Set every knob used by the selected probes; do not inherit an
                # unrelated interactive probe's settings.
                env.update(MK2_ROOT=root.as_posix(), VALIDATION_STAGE=str(stage),
                           BHP_LOG=str(folder / 'probe.log'), BDF_LOG=str(folder / 'probe.log'),
                           DP_TEST_LOG=str(folder / 'probe.log'), BDF_SNAP_X='60', DP_GAP='55',
                           SWEEP_LOG=str(folder / 'probe.log'), SWEEP_LABEL=case)
                env.update(settings)
                need(archive.name.lower() == 'mk2.zip', 'MAME archive must be named mk2.zip')
                command = [str(emulator), '-rompath', str(archive.parent), 'mk2', '-window',
                           '-nomaximize', '-resolution', '640x480', '-video', 'gdi',
                           '-keyboardprovider', 'win32', '-skip_gameinfo', '-sound', 'none',
                           '-nothrottle', '-autoboot_script', str(bootstrap), '-seconds_to_run', '240']
                for flag, name in [('nvram_directory', 'nvram'), ('snapshot_directory', 'screens'),
                                   ('cfg_directory', 'cfg'), ('state_directory', 'sta'),
                                   ('diff_directory', 'diff')]:
                    (folder / name).mkdir()
                    command.extend(['-' + flag, str(folder / name)])
                startup = None
                if os.name == 'nt':
                    startup = subprocess.STARTUPINFO()
                    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    startup.wShowWindow = 0
                print(f'Running {case}: {label}', flush=True)
                with (folder / 'process.log').open('x', encoding='utf-8') as log:
                    proc = subprocess.Popen(command, cwd=folder, env=env, startupinfo=startup,
                                            stdout=log, stderr=subprocess.STDOUT)
                    print(f'Emulator PID {proc.pid}', flush=True)
                    try:
                        proc.wait(timeout=90)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
                        raise
                need(proc.returncode == 0, 'Emulator failed: ' + str(folder))
                inspect(folder, marker)
            result = compare(output / case / 'baseline', output / case / 'candidate', marker)
            results[case] = result
            (output / case / 'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
            print(f'{case}: {"PASS" if result["passed"] else "FAIL"}; '
                  f'{result["identical"]}/{result["total"]} matching captures; '
                  f'candidate {result["candidate"]}', flush=True)
            for failure in result['failures']:
                print('  ' + failure, flush=True)
        need(all(sha(Path(p)) == h for p, h in manifest.items()), 'Runtime input changed')
        summary = {'cases': {k: {a: b for a, b in v.items() if a != 'captures'} for k, v in results.items()},
                   'passed': all(v['passed'] for v in results.values()), 'installed': False,
                   'coverage': {k: ('Pre-fight matchup setup; input-only walking versus CPU, natural damage/timer'
                                    if k.startswith('play-') else 'Controlled health/position setup')
                                for k in selected}}
        (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        need(summary['passed'], 'Runtime comparison failed; see summary.json')
        print('All selected runtime comparisons passed: ' + str(output), flush=True)
    except Exception as error:
        (output / 'FAILED.txt').write_text(str(error), encoding='utf-8')
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'baseline_zip', 'candidate_zip', 'emulator', 'output'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--scenario', action='append', choices=scenarios())
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
