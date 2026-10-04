"""Prepare and repeat an exact reviewed MK7 preservation build in new scratch copies.

No source checkout or installed ROM is modified. This adapter uses trusted local
MK2 build scripts; the hashes detect drift, not maliciously rewritten job plans.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import datetime
import ast
import zipfile
import shlex
import zlib
import re

sys.dont_write_bytecode = True
import preserve_stage_pixels as preservation


def need(ok, why):
    if not ok:
        raise ValueError(why)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    with path.open('x', encoding='utf-8') as f:
        json.dump(value, f, indent=2)
        f.write('\n')


def contained(root, relative):
    p = Path(relative)
    need(not p.is_absolute() and '..' not in p.parts, 'Invalid job-relative path')
    target = (root / p).resolve()
    need(target.is_relative_to(root) and target != root, 'Job input escapes its root')
    return target


def inventory(root):
    # Nested data/doc assets are build inputs. Nested tools/screens, ROM test
    # runs and emulator state are deliberately not part of a rebuild snapshot.
    paths = [p for p in root.iterdir() if p.is_file()]
    for directory in ('data', 'src', 'doc'):
        paths.extend(p for p in (root / directory).rglob('*')
                     if p.is_file() and '__pycache__' not in p.parts)
    for directory in ('tools', 'sound', 'rom', 'tmp/load2'):
        if (root / directory).is_dir():
            paths.extend(p for p in (root / directory).iterdir() if p.is_file())
    return {p.relative_to(root).as_posix(): sha(contained(root, p.relative_to(root)))
            for p in sorted(set(paths))}


def verify(root, expected, exact=False):
    need(isinstance(expected, dict) and expected, 'Empty input inventory')
    for rel, digest in expected.items():
        path = contained(root, rel)
        need(path.is_file() and sha(path) == digest, 'Reviewed input changed: ' + str(path))
    if exact:
        need(inventory(root) == expected, 'Reviewed input set changed: ' + str(root))


def copy_verified(root, expected, dest):
    need(not dest.exists(), 'Scratch checkout already exists')
    dest.mkdir(parents=True)
    for rel, digest in expected.items():
        source, output = contained(root, rel), contained(dest, rel)
        need(source.is_file() and sha(source) == digest, 'Input changed during copy: ' + rel)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
        need(sha(output) == digest, 'Input changed while copying: ' + rel)


def separate(output, roots):
    need(not output.exists(), 'Use a new output folder')
    for root in roots:
        need(not output.is_relative_to(root) and not root.is_relative_to(output),
             'Output must be separate from source checkouts and reference files')


def command(args, cwd, log, env=None):
    print('Running: ' + ' '.join(map(str, args)), flush=True)
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    with log.open('x', encoding='utf-8') as f:
        result = subprocess.run(list(map(str, args)), cwd=cwd, env=env,
                                stdout=f, stderr=subprocess.STDOUT, startupinfo=startup)
    need(result.returncode == 0, f'Step failed ({result.returncode}); see {log}')


def tool_files(verifier):
    scripts = Path(__file__).resolve().parent
    paths = [verifier, scripts / 'reviewed_stage_build.py',
             scripts / 'preserve_stage_pixels.py', scripts / 'verify_static_stage_packing.py']
    return {str(p): sha(p) for p in paths}


def check_module_order(modules, stage):
    for module in modules:
        rows = module['blocks']
        for left, right in zip(rows, rows[1:]):
            need(left['x'] <= right['x'],
                 f'Unsorted runtime blocks in {stage}/{module["name"]}: '
                 f'X={left["x"]} before X={right["x"]}. Re-export sorted game sources.')


def verify_runtime_block_order(root, edited_tables):
    """LOAD2 retains BDB row order; the game's binary lookup requires sorted X."""
    pending = set(edited_tables)
    if not pending:
        return []
    reader = preservation.packing.module(root / 'tools/stage_preview.py', 'runtime_order_reader')
    stages = []
    for line in (root / 'data/MK7MIL.LOD').read_text().splitlines():
        line = line.split(';', 1)[0].strip()
        if not line.startswith('BBB>'):
            continue
        stage = line[4:].strip()
        need(re.fullmatch(r'[A-Za-z0-9_]{1,64}', stage), 'Unsupported MK7 background name')
        name, _, _, modules, blocks = reader.load_bdb(root / 'data' / (stage + '.BDB'))
        table = name[4:] + 'HDRS'
        if table not in pending:
            continue
        images, order, _ = reader.load_bdd(root / 'data' / (stage + '.BDD'))
        reader.assign_modules(modules, blocks, images, order)
        check_module_order(modules, stage)
        pending.remove(table)
        stages.append(stage)
    need(not pending, 'Unmapped edited background tables: ' + ', '.join(sorted(pending)))
    return stages


def video_layout(root, receipt):
    tree = ast.parse((root / 'makevrom.py').read_text(encoding='utf-8'))
    values = [ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
              and any(isinstance(t, ast.Name) and t.id == 'MAME_ROMS' for t in n.targets)]
    need(len(values) == 1 and len(values[0]) == 12, 'Unsupported packaged chip list')
    chips = values[0]
    need(len({c[0] for c in chips}) == 12, 'Duplicate video chip mapping')
    lines = receipt.read_text(encoding='utf-8').splitlines()
    need(len(lines) >= 3 and lines[0] == 'BDDROM 3', 'Unsupported native receipt for ZIP verification')
    count = int(lines[2])
    need(0 < count <= len(lines) - 3, 'Invalid verified payload count')
    occupied = bytearray(0xc00000)
    for line in lines[3:3 + count]:
        _, _, start, count, _ = shlex.split(line)
        start, count = int(start), int(count)
        need(0 <= start <= len(occupied) and 0 < count <= len(occupied) - start,
             'Invalid verified payload range')
        occupied[start:start + count] = b'\x01' * count
    for name, offset, size, lane in chips:
        need(0 <= lane < 4 and offset >= 0 and size > 0 and
             offset + (size - 1) * 4 + lane < len(occupied), 'Invalid packaged chip mapping')
    return chips, occupied


def video_patch_offsets(root, receipt):
    chips, occupied = video_layout(root, receipt)
    offsets = {}
    for name, offset, size, lane in chips:
        raw = contained(root, 'rom/' + name).read_bytes()
        need(len(raw) == size, 'Wrong raw chip size: ' + name)
        for pos in range(size - 4, -1, -1):
            if raw[pos:pos + 4] in (b'\x00' * 4, b'\xff' * 4) and all(
                    not occupied[offset + (pos + i) * 4 + lane] for i in range(4)):
                offsets[name] = pos
                break
        need(name in offsets, 'No verified video padding for CRC patch: ' + name)
    return offsets


def verify_zip(root, archive, receipt):
    chips, occupied = video_layout(root, receipt)
    tree = ast.parse((root / 'crc_spoof.py').read_text(encoding='utf-8'))
    targets = [ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'TARGETS' for t in n.targets)]
    need(len(targets) == 1, 'Unsupported video CRC targets')
    padding_changes = 0
    with zipfile.ZipFile(archive) as zipped:
        for name, offset, size, lane in chips:
            need(zipped.namelist().count(name) == 1, 'Missing/duplicate packaged chip: ' + name)
            packed, raw = zipped.read(name), contained(root, 'rom/' + name).read_bytes()
            need(len(packed) == len(raw) == size, 'Wrong packaged chip size: ' + name)
            for i, (a, b) in enumerate(zip(packed, raw)):
                if a != b:
                    need(not occupied[offset + i * 4 + lane],
                         'Packaged video payload differs from verified output: ' + name)
                    padding_changes += 1
            need(name in targets[0] and zlib.crc32(packed) == targets[0][name],
                 'Packaged video CRC differs from emulator target: ' + name)
    # mamerom deliberately CRC-spoofs padding. Every occupied video byte must
    # remain exact; no offset is exempt merely because it is near a chip's end.
    return padding_changes


def prepare(baseline, candidate, review, verifier, folder, git_dir=None, allow_drift=False):
    need(baseline != candidate and not baseline.is_relative_to(candidate) and
         not candidate.is_relative_to(baseline), 'Baseline and candidate must be separate')
    reference = candidate.parent / 'mk2-readonly' / 'mk2-main'
    need(reference.is_dir(), 'The candidate needs its sibling mk2-readonly reference tree')
    separate(folder, [baseline, candidate, reference])
    reviewed = json.loads(review.read_text(encoding='utf-8'))
    for root, field in [(baseline, 'baseline_irw_sha256'), (candidate, 'candidate_irw_sha256')]:
        need(sha(root / 'data/MK7MIL.IRW') == reviewed[field], 'Reviewed MK7 output changed')
    before, after, retail = inventory(baseline), inventory(candidate), inventory(reference)
    pinned_tools = tool_files(verifier)
    verify_runtime_block_order(candidate, reviewed['edited_tables'])
    folder.mkdir(parents=True)
    try:
        regenerated = preservation.preserve(baseline, candidate, folder / 'review-check',
                                           reviewed['approved'], reviewed['edited_tables'])
        need(regenerated == reviewed, 'The preservation receipt no longer reproduces exactly')
        command([verifier, baseline, folder / 'baseline.romreceipt'], folder,
                folder / 'baseline-check.log')
        command([verifier, candidate, folder / 'candidate.romreceipt'], folder,
                folder / 'candidate-check.log')
        command([sys.executable, '-B', baseline / 'tools/art_identity_snapshot.py',
                 '--write', folder / 'baseline-sprites.json'], baseline, folder / 'sprites.log')
        verify(baseline, before, exact=True)
        verify(candidate, after, exact=True)
        verify(reference, retail, exact=True)
        if git_dir:
            need(git_dir.is_dir(), 'Missing read-only Git history reference')
        plan = {'version': 1, 'baseline': str(baseline), 'candidate': str(candidate),
                'reference': str(reference), 'baseline_files': before, 'candidate_files': after,
                'reference_files': retail, 'tools': pinned_tools, 'verifier': str(verifier),
                'review': reviewed, 'git_dir': str(git_dir) if git_dir else None,
                'allow_stock_bg_drift': bool(allow_drift),
                'baseline_receipt_sha256': sha(folder / 'baseline.romreceipt'),
                'baseline_sprites_sha256': sha(folder / 'baseline-sprites.json')}
        write_json(folder / 'job.json', plan)
        (folder / 'REVIEW.txt').write_text(
            f'Reviewed MK7 rebuild\nBaseline: {baseline}\nCandidate: {candidate}\n'
            f'Expected saving: {reviewed["saved_bytes"]} bytes\n'
            f'Approved physical payload changes: {reviewed["changed_payload_bits"]} bit(s)\n'
            'Every run creates separate scratch checkouts, runs the full build, repeats only '
            'this exact preservation and verifies background/sprite output before packaging.\n'
            'The ZIP is local; nothing is installed. Emulator and runtime stress review remain separate.\n',
            encoding='utf-8')
        print('Reviewed job prepared: ' + str(folder / 'job.json'), flush=True)
        return plan
    except Exception as error:
        (folder / 'FAILED.txt').write_text(str(error), encoding='utf-8')
        raise


def run(plan_path, output, expected_candidate=None):
    plan_bytes = plan_path.read_bytes()
    plan = json.loads(plan_bytes)
    need(plan['version'] == 1, 'Unsupported reviewed build job')
    baseline, candidate, reference = [Path(plan[k]).resolve() for k in
                                       ('baseline', 'candidate', 'reference')]
    if expected_candidate:
        need(candidate == expected_candidate, 'Job candidate differs from the selected game checkout')
    separate(output, [baseline, candidate, reference])
    need(not plan_path.is_relative_to(output), 'Output contains the reviewed job')
    need(tool_files(Path(plan['verifier'])) == plan['tools'],
         'Build adapter changed; prepare a new job')
    for name, field in [('baseline.romreceipt', 'baseline_receipt_sha256'),
                        ('baseline-sprites.json', 'baseline_sprites_sha256')]:
        need(sha(plan_path.parent / name) == plan[field], 'Reviewed baseline evidence changed')
    for root, key in [(baseline, 'baseline_files'), (candidate, 'candidate_files'),
                      (reference, 'reference_files')]:
        verify(root, plan[key], exact=True)
    verify_runtime_block_order(candidate, plan['review']['edited_tables'])
    output.mkdir(parents=True)
    try:
        for name, field in [('baseline.romreceipt', 'baseline_receipt_sha256'),
                            ('baseline-sprites.json', 'baseline_sprites_sha256')]:
            shutil.copy2(plan_path.parent / name, output / name)
            need(sha(output / name) == plan[field], 'Baseline evidence changed during copy')
        scratch, frozen = output / 'checkout', output / 'baseline'
        copy_verified(candidate, plan['candidate_files'], scratch)
        copy_verified(baseline, plan['baseline_files'], frozen)
        copy_verified(reference, plan['reference_files'], output / 'mk2-readonly/mk2-main')
        env = os.environ.copy()
        env['PYTHONDONTWRITEBYTECODE'] = '1'
        env['PYTHONUNBUFFERED'] = '1'
        env.pop('ALLOW_STOCK_BG_DRIFT', None)
        env.pop('MK2_GIT_DIR', None)
        if plan['allow_stock_bg_drift']:
            env['ALLOW_STOCK_BG_DRIFT'] = '1'
        if plan['git_dir']:
            env['MK2_GIT_DIR'] = plan['git_dir']
            # Trust only this explicitly selected history checkout, in this child
            # process. Do not change the user's global Git configuration.
            index = int(env.get('GIT_CONFIG_COUNT', '0'))
            env['GIT_CONFIG_COUNT'] = str(index + 1)
            env[f'GIT_CONFIG_KEY_{index}'] = 'safe.directory'
            env[f'GIT_CONFIG_VALUE_{index}'] = Path(plan['git_dir']).parent.as_posix()
        # Wrapper hides DOSBox children too; the reviewed build.py remains byte-exact.
        launcher = output / 'build_launcher.py'
        launcher.write_text(
            'import os,runpy,subprocess,sys\n'
            'original=subprocess.Popen\n'
            'class Hidden(original):\n'
            ' def __init__(self,*a,**k):\n'
            '  if os.name=="nt" and "startupinfo" not in k:\n'
            '   s=subprocess.STARTUPINFO();s.dwFlags|=subprocess.STARTF_USESHOWWINDOW;'
            's.wShowWindow=0;k["startupinfo"]=s\n'
            '  super().__init__(*a,**k)\n'
            'subprocess.Popen=Hidden\n'
            'sys.argv=["build.py"];runpy.run_path("build.py",run_name="__main__")\n', encoding='utf-8')
        command([sys.executable, '-B', launcher], scratch, output / 'build.log', env)
        # These artifacts must exactly match the reviewed raw candidate. A different
        # packing result requires another review, even if the difference seems small.
        need(sha(scratch / 'data/MK7MIL.IRW') == plan['review']['candidate_irw_sha256'],
             'Fresh MK7 packing differs from the reviewed candidate')
        need(sha(scratch / 'tmp/load2/BGNDTBL.MK7') == plan['review']['candidate_headers_sha256'],
             'Fresh background headers differ from the reviewed candidate')
        # Authored background inputs must survive generators unchanged.
        protected = {p: h for p, h in plan['candidate_files'].items()
                     if p.startswith('data/') and Path(p).suffix.upper() in ('.BDB', '.BDD', '.LOD')}
        verify(scratch, protected)
        sorted_stages = verify_runtime_block_order(scratch, plan['review']['edited_tables'])
        restored = preservation.preserve(frozen, scratch, output / 'preservation',
                                        plan['review']['approved'], plan['review']['edited_tables'])
        need(restored == plan['review'], 'Preservation differs from the reviewed correction')
        shutil.copy2(scratch / 'data/MK7MIL.IRW', output / 'raw-MK7MIL.IRW')
        shutil.copy2(output / 'preservation/MK7MIL.IRW', scratch / 'data/MK7MIL.IRW')
        command([sys.executable, '-B', 'makerom.py'], scratch, output / 'program.log', env)
        command([sys.executable, '-B', 'makevrom.py'], scratch, output / 'video.log', env)
        verifier = Path(plan['verifier'])
        command([verifier, scratch, output / 'verified.romreceipt',
                 output / 'baseline.romreceipt'], scratch, output / 'artwork.log', env)
        command([sys.executable, '-B', 'tools/art_identity_snapshot.py', '--strict',
                 '--compare', output / 'baseline-sprites.json'], scratch,
                output / 'sprite-check.log', env)
        archive = output / 'rom/mk2.zip'
        # The stock CRC fallback scans for zero/FF runs, which may be actual
        # artwork. Give it only windows outside every verified IRW payload.
        patch_offsets = video_patch_offsets(scratch, output / 'verified.romreceipt')
        write_json(output / 'video-crc-offsets.json', patch_offsets)
        packager = output / 'package_launcher.py'
        packager.write_text(
            'import os,runpy,sys\nsys.path.insert(0,os.getcwd())\nimport crc_spoof\n'
            f'crc_spoof.PATCH_OFFSETS.update({patch_offsets!r})\n'
            f'sys.argv=["mamerom.py","--no-install","--output",{str(archive)!r}]\n'
            'runpy.run_path("mamerom.py",run_name="__main__")\n', encoding='utf-8')
        command([sys.executable, '-B', packager], scratch, output / 'package.log', env)
        need(archive.is_file(), 'Packaging did not produce the diagnostic ZIP')
        padding_changes = verify_zip(scratch, archive, output / 'verified.romreceipt')
        # Packaging itself must not silently change the verified chips/IRWs.
        command([verifier, scratch, output / 'final.romreceipt',
                 output / 'baseline.romreceipt'], scratch, output / 'final-check.log', env)
        need(sha(scratch / 'data/MK7MIL.IRW') == plan['review']['output_irw_sha256'],
             'Final MK7 output changed after preservation')
        for root, key in [(baseline, 'baseline_files'), (candidate, 'candidate_files'),
                          (reference, 'reference_files')]:
            verify(root, plan[key], exact=True)
        need(plan_path.read_bytes() == plan_bytes, 'Reviewed job changed during the run')
        need(tool_files(verifier) == plan['tools'], 'Build adapter changed during the run')
        result = {'status': 'verified', 'job_sha256': hashlib.sha256(plan_bytes).hexdigest(),
                  'completed_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  'zip': str(archive), 'zip_sha256': sha(archive), 'installed': False,
                  'saved_bytes': restored['saved_bytes'],
                  'changed_payload_bits': restored['changed_payload_bits'],
                  'runtime_sorted_stages': sorted_stages,
                  'zip_video_padding_bytes_changed': padding_changes,
                  'video_crc_offsets': patch_offsets,
                  'runtime_review': 'Emulator/combat/fatality validation remains separate'}
        write_json(output / 'SUCCESS.json', result)
        print(json.dumps(result, indent=2), flush=True)
        return result
    except Exception as error:
        (output / 'FAILED.txt').write_text(str(error), encoding='utf-8')
        print('Build refused. Inputs remain intact; scratch files and logs are in ' + str(output), flush=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    prep = sub.add_parser('prepare')
    for name in ('baseline', 'candidate', 'review', 'verifier', 'folder'):
        prep.add_argument(name, type=Path)
    prep.add_argument('--git-dir', type=Path)
    prep.add_argument('--allow-stock-bg-drift', action='store_true')
    execute = sub.add_parser('run')
    execute.add_argument('job', type=Path)
    execute.add_argument('folder', type=Path)
    execute.add_argument('--candidate', type=Path)
    args = parser.parse_args()
    try:
        if args.mode == 'prepare':
            prepare(*[getattr(args, k).resolve() for k in
                      ('baseline', 'candidate', 'review', 'verifier', 'folder')],
                    args.git_dir.resolve() if args.git_dir else None, args.allow_stock_bg_drift)
        else:
            run(args.job.resolve(), args.folder.resolve(),
                args.candidate.resolve() if args.candidate else None)
    except (ValueError, OSError, KeyError, TypeError, AssertionError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
