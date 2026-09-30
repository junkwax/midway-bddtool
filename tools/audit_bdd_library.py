"""Read-only BDD library survey. Runs bddtool's analyzer on frozen local copies."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def summarize(report, out):
    """Summarize independent alternatives; never add masters to generated packs."""
    results = report['results']
    rows = []
    for r in results:
        scans = [s for s in r.get('scans', []) if s['verified']]
        preserve = max((s for s in scans if s['mode'].startswith('preserve')),
                       key=lambda s: s['saved_bytes'], default=None)
        best = max(scans, key=lambda s: s['saved_bytes'], default=None)
        status = r['status']
        name = Path(r['path']).stem.upper()
        if r['path'].startswith('data/') and '/' not in r['path'][5:]:
            if re.fullmatch(r'MK1PT[1-4]', name):
                status = 'generated Pit pack; overlaps KUNGFU5 master'
            elif re.fullmatch(r'MK1WS[1-3]', name):
                status = 'generated Shrine pack; overlaps KUNGFU1 master'
            elif name == 'KUNGFU5':
                status = 'Pit master; also supplies Shrine shared art'
            elif name == 'KUNGFU1':
                status = 'Shrine master'
        rows.append({'file': r['path'], 'scope': status,
                     'baseline_bytes': r.get('inventory', {}).get('baseline_bytes', ''),
                     'preserve_saved': preserve['saved_bytes'] if preserve else '',
                     'best_saved': best['saved_bytes'] if best else '',
                     'best_mode': best['mode'] if best else '',
                     'extra_objects': best['objects_after'] - best['objects_before'] if best else '',
                     'extra_table_bytes': best['table_delta'] if best else '',
                     'extra_palette_bytes': best['palette_delta'] if best else '',
                     'palettes_after': best['palettes_after'] if best else '',
                     'unverified_modes': '; '.join(s['mode'] for s in r.get('scans', []) if not s['verified']),
                     'detail': f"reports/{r['id']:03d}/"})
    with (out / 'summary.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    active_names = {r['path'] for r in results if r['active_lods'] == ['MK7MIL']}
    primary = sorted((r for r in rows if r['file'] in active_names),
                     key=lambda r: r['best_saved'] or 0, reverse=True)
    preserve_total = sum(r['preserve_saved'] or 0 for r in primary)
    best_total = sum(r['best_saved'] or 0 for r in primary)
    lines = ['# BDD library savings audit', '',
             f"Snapshot: {report['captured_utc']}. Source: `{report['root']}`.", '',
             f"Scanned **{len(rows)} BDD files** in `data/` and `stage_packs/`, including nested imports. "
             f"All **{report['checked_files']:,}** checked source/build file hashes "
             + ('are unchanged.' if not report['changed_source_files'] else '**require review: files changed during the scan**.'), '',
             'Analysis ran on frozen copies. No document apply/save, game generator, LOAD2, assembly, ROM packing or emulator deployment ran.', '',
             '## Active primary build inputs', '',
             f"For the {len(primary)} BDD inputs listed directly in active `MK7MIL.LOD`, verified proposals sum to "
             f"**{preserve_total:,} bytes ({preserve_total/1024:.1f} KiB)** preserving palettes, or "
             f"**{best_total:,} bytes ({best_total/1024:.1f} KiB)** using the best verified alternative including palette copies. "
             'These are alternative totals, not additive. Unverified modes contribute nothing; this is a per-file model, not a combined LOAD2 result.', '',
             '| BDD | Preserve palettes | Best static-pixel saving | Extra placements (best) | Palettes after (best) |',
             '| --- | ---: | ---: | ---: | ---: |']
    fmt = lambda x: f'{x:,}' if isinstance(x, int) else '—'
    for r in primary:
        lines.append(f"| {Path(r['file']).name} | {fmt(r['preserve_saved'])} | {fmt(r['best_saved'])} | {fmt(r['extra_objects'])} | {fmt(r['palettes_after'])} |")
    lines += ['', '## Interpretation and limits', '',
              '- Deep search, balanced policy, at most eight pieces per image, 24 additional placements per file and 45 palettes. A zero result does not prove no opportunity exists.',
              '- Four independent scans: subdivision/reuse and shared-base/detail decomposition, each with and without palette compaction. Pick the best verified result; do not sum the scans.',
              '- Exact reconstruction checks static RGB555 pixels and transparency for current palette uses. Palette cycling, swapping, direct image indices, generated tables, runtime effects and peak object/DMA use still need review.',
              '- Larger palette results can reach the 45-background-palette limit. MOUNTAIN and FOREST2 reach 45; BATTLE reaches 44. These are research candidates, not immediate deployment recommendations.',
              '- Model: auto BPP, zero compression and 16-bit image alignment. Exact payload duplicates are already counted once. Actual LOAD2 policy and checksum sharing can change the result. Table/palette growth is a separate program-ROM cost shown in the CSV.',
              '- MK3CAVE keeps its cavern/water layer locked for this scan. Palette-changing cave proposals cannot use the current protected export profile.',
              '- Generated MK3CV, MK1PT and MK1WS packs overlap their source masters and are not additional stage savings. Pit and Shrine already share some payloads by runtime address, so their pack/master estimates need generator-aware reconciliation.',
              '- Absence from active BBB directives is not proof that artwork is unused. KUNGFU1/KUNGFU5 feed active generators. Other files can be references, prototypes or imported variants. No deletion saving is counted.',
              '- Animated IMG artwork (including Forest faces), deliberate repeat/mirror redesigns and visibility/camera trims are outside these static BDD totals.', '',
              '## Incomplete checks', '',
              'Only successful modes are eligible for the totals. Files with no verified alternative show a dash. An unverified scan is not evidence of zero savings.', '']
    for r in rows:
        if r['unverified_modes']:
            errors = set()
            for mode in r['unverified_modes'].split('; '):
                detail = out / r['detail'] / (mode + '.txt')
                if detail.exists():
                    errors.update(re.findall(r'^Error: (.+)$', detail.read_text(errors='replace'), re.M))
            lines.append(f"- `{r['file']}`: {r['unverified_modes']}. {'; '.join(sorted(errors))} See [{r['detail']}]({r['detail']}).")
    lines += ['', '## All files', '',
              'Bytes below are modeled savings. **—** means no verified alternative, not zero opportunity. Best includes preserve-palette alternatives. An asterisk marks incomplete modes.', '',
              '| File | Preserve palettes | Best verified | Build relationship |',
              '| --- | ---: | ---: | --- |']
    for r in rows:
        label = r['file'] + (' *' if r['unverified_modes'] else '')
        lines.append(f"| [{label}]({r['detail']}) | {fmt(r['preserve_saved'])} | {fmt(r['best_saved'])} | {r['scope']} |")
    lines += ['', '## Next work', '',
              '1. Validate the strongest palette-preserving candidates in isolated builds. Check custom runtime consumers and peak object use before applying.',
              '2. Investigate the largest palette-copy candidates with runtime palette-budget and cycling checks. Reduce palette growth before treating these as export-ready.',
              '3. Extend the existing animation analysis into verified IMG/multipart export, then add reference-proven unused-art removal. Both remain separate from this BDD scan.',
              '4. Add a library audit view to the editor using these reports, including active build membership, generated-source relationships and measured-versus-estimated savings.', '',
              'Reproduce with `python tools/audit_bdd_library.py GAME ANALYZER NEW_OUTPUT` after building the `studio_savings_audit` CMake target. Full machine-readable evidence: `audit.json`, `summary.csv`, `source-before.json`, `source-after.json` and per-file reports.', '']
    (out / 'REPORT.md').write_text('\n'.join(lines), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('analyzer', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root, exe, out = args.game.resolve(), args.analyzer.resolve(), args.output.resolve()
    if out.is_relative_to(root) or root.is_relative_to(out):
        parser.error('Reports must be outside the game checkout')
    out.mkdir(parents=True, exist_ok=False)
    inputs = sorted(p for directory in ('data', 'stage_packs')
                    for p in (root / directory).rglob('*') if p.suffix.lower() == '.bdd')
    # Hash inputs, source/configuration, existing packed payloads and ROM output.
    protected = set(inputs)
    for directory in ('data', 'stage_packs', 'src', 'rom', 'tools'):
        for p in (root / directory).rglob('*'):
            if p.is_file() and (directory in ('src', 'rom') or
                               p.suffix.lower() in ('.bdd', '.bdb', '.meta', '.bddstudio',
                                                    '.lod', '.irw', '.py', '.bat', '.json')):
                protected.add(p)
    protected.update(p for p in root.iterdir() if p.is_file())
    before = {p.relative_to(root).as_posix(): digest(p) for p in sorted(protected)}
    (out / 'source-before.json').write_text(json.dumps(before, indent=2) + '\n')
    live_lods = set()
    for line in (root / 'data/IMGBUILD.BAT').read_text(errors='replace').splitlines():
        if line.lstrip().lower().startswith(('rem ', '::')):
            continue
        match = re.search(r'(?:^|[\\\s])load2(?:\.exe)?\s+(\w+)', line, re.I)
        if match:
            live_lods.add(match[1].upper())
    references = {}
    for lod in sorted(live_lods):
        path = root / 'data' / (lod + '.LOD')
        if path.exists():
            for name in re.findall(r'^\s*BBB>\s+([\w]+)', path.read_text(errors='replace'), re.M | re.I):
                references.setdefault(name.upper(), []).append(lod)
    records = []
    for i, source in enumerate(inputs):
        relative = source.relative_to(root)
        copied = out / 'inputs' / relative
        copied.parent.mkdir(parents=True, exist_ok=True)
        # Preserve the paired document and metadata exactly; never normalize or save it.
        companions = [p for p in source.parent.iterdir() if p.is_file() and
                      (p.stem.lower() == source.stem.lower() and
                       p.suffix.lower() in ('.bdd', '.bdb', '.bddstudio') or
                       p.name.lower() == source.name.lower() + '.meta')]
        for p in companions:
            shutil.copyfile(p, copied.parent / p.name)
            if digest(p) != before[p.relative_to(root).as_posix()] or digest(copied.parent / p.name) != digest(p):
                raise RuntimeError('Source changed during capture: ' + str(p))
        direct = source.parent == root / 'data'
        active = references.get(source.stem.upper(), []) if direct else []
        status = 'active BBB input' if active else 'not in active BBB list'
        if direct and source.stem.upper() == 'MK3CAVE' and all('MK3CV'+str(k) in references for k in range(1, 5)):
            status = 'master of active MK3CV packs'
        elif active and re.fullmatch(r'MK3CV[1-4]', source.stem, re.I):
            status = 'generated active pack; overlaps MK3CAVE master'
        records.append({'id': i, 'path': relative.as_posix(), 'status': status,
                        'active_lods': active, 'sha256': before[relative.as_posix()],
                        'copied': str(copied)})
    print(f'Captured {len(inputs)} BDD files; {len(before)} source/build hashes; {len(live_lods)} active LOAD2 calls', flush=True)

    def analyze(record):
        folder = out / 'reports' / f"{record['id']:03d}"
        try:
            process = subprocess.run([str(exe), record['copied'], str(folder)],
                                     capture_output=True, text=True, timeout=240)
            record['exit_code'] = process.returncode
            record['log'] = process.stdout + process.stderr
        except subprocess.TimeoutExpired:
            record['exit_code'] = -1
            record['log'] = 'Analysis timeout after 240 seconds; no complete result claimed.'
        if (folder / 'summary.csv').exists():
            with (folder / 'summary.csv').open(newline='') as stream:
                record['scans'] = [{k: v if k == 'mode' else int(v) for k, v in row.items()}
                                   for row in csv.DictReader(stream)]
        if (folder / 'inventory.txt').exists():
            record['inventory'] = dict(line.split('=', 1) for line in
                                       (folder / 'inventory.txt').read_text().splitlines() if '=' in line)
        print(f"[{record['id'] + 1}/{len(inputs)}] {record['path']} exit={record['exit_code']}", flush=True)
        return record

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(analyze, records))
    after = {relative: digest(root / relative) if (root / relative).is_file() else None for relative in before}
    changed = [name for name in before if before[name] != after[name]]
    report = {'captured_utc': datetime.now(timezone.utc).isoformat(), 'root': str(root),
              'settings': {'deep': True, 'max_added_objects': 24, 'max_pieces': 8, 'max_palettes': 45},
              'active_load2': sorted(live_lods), 'checked_files': len(before),
              'changed_source_files': changed, 'results': results}
    (out / 'source-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    summarize(report, out)
    print(f"Complete: {len(results)} files, {sum(r['exit_code'] != 0 for r in results)} failures, {len(changed)} changed source/build files", flush=True)
    if changed:
        raise SystemExit('Source checkout changed during audit; review changed_source_files before using the results.')


if __name__ == '__main__':
    main()
