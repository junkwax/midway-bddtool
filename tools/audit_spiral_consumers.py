"""Read-only SPIRAL audit of known Portal/Outer Haven static consumers.

Reads a trusted frozen game checkout and an optional palette-preserving BDD/BDB
proposal. Writes one new JSON report, never game assets or generated tables.
This is a bounded static proof, not a build or runtime approval.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
from verify_static_stage_packing import module, inspect, header_tables


def record(text, label):
    matches = list(re.finditer(r'^' + re.escape(label) + r':\s*(?:;[^\n]*)?$', text, re.M))
    if len(matches) != 1:
        raise ValueError('Expected one record: ' + label)
    values = []
    for raw in text[matches[0].end():].splitlines():
        line = raw.split(';')[0].strip()
        if not line:
            continue
        if re.match(r'\w+:', line) or line.lower().startswith(('.include', '.text', '.end')):
            break
        match = re.fullmatch(r'\.(?:word|long)\s+(.+)', line, re.I)
        if not match:
            raise ValueError('Unsupported record syntax: ' + raw)
        values.extend(v.strip() for v in match[1].split(','))
    return values


def blocks(values, number):
    words = [number(v) for v in values]
    if not words or words[-1] != 65535 or (len(words) - 1) % 4:
        raise ValueError('Incomplete block table')
    result = []
    for i in range(0, len(words) - 1, 4):
        flags, x, y, header = words[i:i + 4]
        result.append(dict(z=flags, x=x, y=y, hi=header & 0x3fff,
                           pal=(flags & 15) | ((header >> 14) << 4)))
    return result


def equivalence(indices, palettes):
    """Only merge opaque indices equal in EVERY supplied palette variant."""
    groups = {}
    for index in sorted(set(indices) - {0}):
        if any(index >= len(p) for p in palettes):
            raise ValueError('Palette index out of range')
        signature = tuple(p[index] & 0x7fff for p in palettes)
        groups.setdefault(signature, []).append(index)
    return list(groups.values())


def render(images, order, palettes, entries, width, height, reverse=False):
    # Encode RGB555 and coverage exactly, without RGB expansion or screenshots.
    out = [0] * (width * height)
    placed = sorted(enumerate(entries), key=lambda t: (
        t[1]['z'] >> 8, t[1]['y'],
        -t[1]['x'] if reverse else t[1]['x'], -t[0] if reverse else t[0]))
    for _, b in placed:
        im = images[order[b['hi']]]
        pal = palettes[b['pal']]
        for y in range(im['h']):
            for x in range(im['w']):
                sx = im['w'] - 1 - x if b['z'] & 16 else x
                sy = im['h'] - 1 - y if b['z'] & 32 else y
                pixel = im['px'][sy * im['w'] + sx]
                if pixel:
                    dx, dy = b['x'] + x, b['y'] + y
                    if not (0 <= dx < width and 0 <= dy < height):
                        raise ValueError('Opaque pixel outside module bounds')
                    out[dy * width + dx] = 0x10000 | (pal[pixel] & 0x7fff)
    return out


def transfer_palette(before, after, old_colors, assignments, flip=0):
    """Infer an alternate palette from corresponding pixels, rejecting collisions."""
    if (before['w'], before['h']) != (after['w'], after['h']):
        raise ValueError('Palette transfer requires unchanged dimensions')
    pairs = ((before['px'][y * before['w'] + x], after['px'][
        (after['h'] - 1 - y if flip & 32 else y) * after['w'] +
        (after['w'] - 1 - x if flip & 16 else x)])
        for y in range(before['h']) for x in range(before['w']))
    for old, new in pairs:
        if (old == 0) != (new == 0):
            raise ValueError('Transparency changed')
        if not old:
            continue
        color = old_colors[old] & 0x7fff
        if new in assignments and assignments[new] != color:
            raise ValueError('Alternate palette color collision')
        assignments[new] = color


def audit(root, proposal=None):
    reader = module(root / 'tools/stage_preview.py', 'spiral_preview')
    number = reader._num
    images, order, bdd_palettes = reader.load_bdd(root / 'data/SPIRAL.BDD')
    table = (root / 'src/BGNDTBL.ASM').read_text()
    generated = (root / 'tmp/load2/BGNDTBL.MK7').read_text()
    if header_tables(table, number)['alHDRS'] != header_tables(generated, number)['alHDRS']:
        raise ValueError('Runtime headers differ from packed snapshot')
    palette_text = (root / 'src/BGNDPAL.ASM').read_text()
    palettes, palette_labels = {}, {}
    for consumer, label in [('portal', 'alPALS'), ('outer_haven', 'ohPALS')]:
        names = record(palette_text, label)
        colors = []
        for name in names:
            words = [number(v) for v in record(palette_text, name)]
            if words[0] != len(words) - 1:
                raise ValueError('Palette size mismatch: ' + name)
            colors.append(words[1:])
        palette_labels[consumer], palettes[consumer] = names, colors
    if palettes['portal'] != [p for _, p in bdd_palettes]:
        raise ValueError('Portal source palettes differ from BDD')
    consumers = {}
    usages = {index: {} for index in order}
    for consumer, prefix in [('portal', ''), ('outer_haven', 'oh')]:
        rows = []
        for i in range(1, 5):
            name = prefix + 'spiral' + str(i) + 'BMOD'
            values = record(table, name)
            if len(values) != 6 or values[4:] != ['alHDRS', 'alPALS' if not prefix else 'ohPALS']:
                raise ValueError('Unexpected consumer binding: ' + name)
            w, h, count = map(number, values[:3])
            entries = blocks(record(table, values[3]), number)
            if count != len(entries):
                raise ValueError('Consumer block count mismatch: ' + name)
            rows.append(dict(module=name, width=w, height=h, count=count,
                             block_table=values[3], entries=entries))
            for entry in entries:
                if entry['hi'] >= len(order) or entry['pal'] >= len(palettes[consumer]):
                    raise ValueError('Consumer reference out of range')
                slots = usages[order[entry['hi']]].setdefault(consumer, set())
                slots.add(entry['pal'])
        consumers[consumer] = rows
    packed = inspect(root, 'SPIRAL')
    if not packed['packed_pixels_identical']:
        raise ValueError('Packed source pixels differ')
    palette_analysis = []
    for index in order:
        used = usages[index]
        if set(used) != set(palettes):
            raise ValueError('Unreviewed consumer coverage for image ' + str(index))
        portal = [palettes['portal'][s] for s in sorted(used['portal'])]
        joint = portal + [palettes['outer_haven'][s] for s in sorted(used['outer_haven'])]
        local_groups = equivalence(images[index]['px'], portal)
        joint_groups = equivalence(images[index]['px'], joint)
        conflicts = [group for group in local_groups if len(equivalence(group, joint)) > 1]
        palette_analysis.append(dict(image=index, slots={k: sorted(v) for k, v in used.items()},
            portal_opaque_classes=len(local_groups), joint_opaque_classes=len(joint_groups),
            portal_min_bpp=max(1, len(local_groups).bit_length()),
            joint_min_bpp=max(1, len(joint_groups).bit_length()),
            unsafe_portal_only_merges=conflicts))
    result = dict(stage='SPIRAL', packed=packed, palette_labels=palette_labels,
                  consumers=consumers, palette_analysis=palette_analysis,
                  scope='Known static modules and palettes; runtime effects are separate',
                  built=False, installed=False)
    if proposal:
        new_images, new_order, new_palettes = reader.load_bdd(proposal / 'SPIRAL.BDD')
        for shared in packed['shared_images']:
            if images[shared['image']] != new_images.get(shared['image']):
                raise ValueError('Proposal changes cross-stage shared image ' + str(shared['image']))
        palette_changed = new_palettes != bdd_palettes
        _, _, _, mods, entries = reader.load_bdb(proposal / 'SPIRAL.BDB')
        if reader.assign_modules(mods, entries, new_images, new_order):
            raise ValueError('Unassigned proposal placements')
        _, _, _, old_mods, old_entries = reader.load_bdb(root / 'data/SPIRAL.BDB')
        if reader.assign_modules(old_mods, old_entries, images, order):
            raise ValueError('Unassigned original placements')
        old_modules = {m['name']: m for m in old_mods}
        candidate_modules = {m['name']: m for m in mods}
        expected_modules = {'spiral' + str(i) for i in range(1, 5)}
        if (set(old_modules) != expected_modules or set(candidate_modules) != expected_modules
                or len(mods) != 4 or len(old_mods) != 4):
            raise ValueError('Unexpected module set')
        proposal_palettes = dict(portal=[p for _, p in new_palettes], outer_haven=palettes['outer_haven'])
        remap = {}
        if palette_changed:
            # This path handles zero-added-placement palette proposals only.
            key = lambda b: (b['x'], b['y'], b['z'] & ~48)
            old_by_key = {key(b): b for b in old_entries}
            new_by_key = {key(b): b for b in entries}
            if (len(old_by_key) != len(old_entries) or len(new_by_key) != len(entries)
                    or old_by_key.keys() != new_by_key.keys()):
                raise ValueError('Palette proposal changes placement geometry')
            alternate = [dict(enumerate(palettes['outer_haven'][i]))
                         if i < len(bdd_palettes) and p == bdd_palettes[i] else {}
                         for i, p in enumerate(new_palettes)]
            for position, before in old_by_key.items():
                after = new_by_key[position]
                pair = (before['hi'], before['pal'])
                flip = (before['z'] ^ after['z']) & 48
                replacement = (after['hi'], after['pal'], flip)
                if pair in remap and remap[pair] != replacement:
                    raise ValueError('Ambiguous custom placement remap')
                remap[pair] = replacement
                transfer_palette(images[before['hdr']], new_images[after['hdr']],
                                 palettes['outer_haven'][before['pal']], alternate[after['pal']], flip)
            proposal_palettes['outer_haven'] = [
                [mapping.get(i, 0) for i in range(len(p))]
                for mapping, (_, p) in zip(alternate, new_palettes)]
            if any(mapping and max(mapping) >= len(new_palettes[i][1]) for i, mapping in enumerate(alternate)):
                raise ValueError('Transferred palette index out of range')
        comparisons, repairs = [], []
        if palette_changed:
            repairs.append(dict(reason='Install paired Portal/Outer Haven palette tables and remap custom hut palette slots',
                                old_slots=len(bdd_palettes), new_slots=len(new_palettes)))
        for consumer, bindings in consumers.items():
            for i, binding in enumerate(bindings, 1):
                m = candidate_modules['spiral' + str(i)]
                old_m = old_modules['spiral' + str(i)]
                if any(m[k] != old_m[k] for k in ('x1', 'x2', 'y1', 'y2')):
                    raise ValueError('Module source origin or bounds changed')
                if (m['x2'] - m['x1'] + 1, m['y2'] - m['y1'] + 1) != (binding['width'], binding['height']):
                    raise ValueError('Module dimensions changed')
                if consumer == 'outer_haven' and i == 2:
                    after = []
                    for b in binding['entries']:
                        image_id = order[b['hi']]
                        if palette_changed:
                            hi, pal, flip = remap[(b['hi'], b['pal'])]
                            after.append(dict(b, hi=hi, pal=pal, z=b['z'] ^ flip))
                        else:
                            if images[image_id] != new_images.get(image_id):
                                raise ValueError('Custom hut image changed; explicit subdivision remap required')
                            after.append(dict(b, hi=new_order.index(image_id)))
                    if after != binding['entries']:
                        repairs.append(dict(module=binding['module'], reason='Remap custom hut header/palette indices', entries=after))
                else:
                    after = [dict(b, x=b['x'] - m['x1'], y=b['y'] - m['y1']) for b in m['blocks']]
                if len(after) != binding['count']:
                    repairs.append(dict(module=binding['module'], old_count=binding['count'], new_count=len(after),
                                        generated=consumer == 'portal'))
                for reverse in (False, True):
                    before_pixels = render(images, order, palettes[consumer], binding['entries'],
                                           binding['width'], binding['height'], reverse)
                    after_pixels = render(new_images, new_order, proposal_palettes[consumer], after,
                                          binding['width'], binding['height'], reverse)
                    comparisons.append(dict(consumer=consumer, module=binding['module'], reverse=reverse,
                                            different_pixels=sum(a != b for a, b in zip(before_pixels, after_pixels))))
        result['proposal'] = dict(path=str(proposal), comparisons=comparisons, required_binding_updates=repairs,
                                  alternate_palette_recipe=proposal_palettes['outer_haven'] if palette_changed else None,
                                  static_pixels_identical=all(c['different_pixels'] == 0 for c in comparisons))
    paths = [root / p for p in ('data/SPIRAL.BDD', 'data/SPIRAL.BDB', 'data/MK7MIL.IRW',
             'src/BGNDTBL.ASM', 'src/BGNDPAL.ASM', 'src/BGND.ASM', 'tmp/load2/BGNDTBL.MK7')]
    if proposal:
        paths += [proposal / ('SPIRAL.' + ext) for ext in ('BDD', 'BDB')]
    result['input_hashes'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('--proposal', type=Path)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('Use a new report path')
    result = audit(args.root.resolve(), args.proposal.resolve() if args.proposal else None)
    with args.report.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Report:', args.report)
    print('Shared image IDs:', [r['image'] for r in result['packed']['shared_images']])
    print('Images with unsafe Portal-only color merges:',
          [r['image'] for r in result['palette_analysis'] if r['unsafe_portal_only_merges']])
    if 'proposal' in result:
        print('Static pixels identical:', result['proposal']['static_pixels_identical'])
        print('Required binding updates:', [{k: v for k, v in row.items() if k != 'entries'}
                                            for row in result['proposal']['required_binding_updates']])
        if not result['proposal']['static_pixels_identical']:
            raise SystemExit(2)
