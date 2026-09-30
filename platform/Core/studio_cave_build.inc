R"BDD(#!/usr/bin/env python3
"""bddtool MK3CAVE build: selected checkout only; packages ROMs without installing."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True

def run(*args):
    print('\n> ' + ' '.join(args), flush=True)
    subprocess.run([sys.executable, '-u', *args], cwd=ROOT, check=True)

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def verify():
    game = load(ROOT / 'tools/make_mk3cave.py', 'bddtool_cave')
    video = load(ROOT / 'makevrom.py', 'bddtool_video')
    images, order, palettes, modules = game.runtime_modules()
    headers = game.merged_headers(images, order)
    payloads = game._irw_payloads()
    report = {'images': len(order), 'rows': 0, 'packs': {}}
    for name, start, end in game.PACKS:
        expected = video.CUSTOM_VIDEO_SLOTS[name + '.IRW']
        if (start, end) != tuple(expected[:2]):
            raise RuntimeError(name + ': packing limits changed')
        base, bank, data = video.parse_irw(ROOT / 'data' / (name + '.IRW'))
        if bank != 1 or game.sag_phys(base + 0x2000000) != start or start + len(data) > end:
            raise RuntimeError(name + ': packed data exceeds its current bank/base/slot')
        report['packs'][name] = {'bytes': len(data), 'capacity': end - start}
    for index, (_, _, address, control) in zip(order, headers):
        image = images[index]
        bpp = (control >> 12) & 7 or 8
        if max(image['px']) >= 1 << bpp:
            raise RuntimeError('Packed palette indices were truncated')
        offsets, bits, bad = game.walk_rows(image, address, control, payloads)
        lod = address - 0x2000000
        if bad or not any(base <= lod and lod + bits <= base + len(data) * 8 for base, data in payloads):
            raise RuntimeError('Packed pixels differ or read beyond the payload: ' + str(index))
        report['rows'] += len(offsets)
    report['water_spans'] = len(game.water_spans(images, order, palettes, modules, headers))
    report['video_bytes'] = sum(pack['bytes'] for pack in report['packs'].values())
    return report

def main():
    report_path = ROOT / 'tmp/bddtool-cave/verification.json'
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.unlink(missing_ok=True)
    run('tools/make_mk3cave.py', '--parts')
    run('build.py')
    report = verify()
    run('tools/make_mk3cave.py', '--check')
    run('makerom.py')
    run('makevrom.py')
    run('mamerom.py', '--no-install', '--output', str(ROOT / 'rom/bddtool/mk2.zip'))
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('\nCAVE VERIFIED: ' + str(report['video_bytes']) + ' packed video bytes; ' +
          str(report['rows']) + ' decoded rows match; water spans rebuilt.', flush=True)
    print('ROMs: rom/bddtool/mk2.zip (not installed). Emulator/combat checks remain separate.', flush=True)

if __name__ == '__main__':
    main()
)BDD"
