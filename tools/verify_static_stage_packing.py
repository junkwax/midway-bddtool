"""Read-only packed-pixel check of a trusted isolated MK7 background build."""
import importlib.util
import hashlib
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def header_tables(text, number):
    """Read LOAD2's four-value image headers, retaining their table labels."""
    tables = {}
    for match in re.finditer(r'^(\w*HDRS):\s*$', text, re.M):
        assert match[1] not in tables, (match[1], 'duplicate header table')
        values = []
        for line in text[match.end():].splitlines():
            line = line.split(';')[0].strip()
            if not line:
                continue
            if not line.lower().startswith(('.word', '.long')):
                break
            values += [number(t) for t in line.split(None, 1)[1].split(',')]
        assert len(values) % 4 == 0, (match[1], 'incomplete image header')
        tables[match[1]] = [values[i:i+4] for i in range(0, len(values), 4)]
    return tables


def shared_images(tables, label, order):
    """Same-address references elsewhere in this bank can erase local savings."""
    owners = {}
    for name, headers in tables.items():
        for _, _, address, _ in headers:
            owners.setdefault(address, set()).add(name)
    return [dict(image=index, address=header[2],
                 other_header_tables=sorted(owners[header[2]] - {label}))
            for index, header in zip(order, tables[label])
            if owners[header[2]] - {label}]


def decode_header(payload, base, header):
    """Decode DMA pixels from one packed header without loading authored artwork."""
    width, height, address, control = header
    assert width > 0 and height > 0
    bpp = (control >> 12) & 7 or 8
    offset = address - 0x2000000 - base
    begin = offset
    decoded_image = bytearray()

    def bits(count):
        nonlocal offset
        assert 0 <= offset and offset + count <= len(payload) * 8
        at, shift = divmod(offset, 8)
        value = (int.from_bytes(payload[at:at+(shift+count+7)//8], 'little') >> shift) & ((1 << count)-1)
        offset += count
        return value

    for _ in range(height):
        lead = trail = 0
        if control & 0x80:
            row = bits(8)
            lead = (row & 15) << ((control >> 8) & 3)
            trail = (row >> 4) << ((control >> 10) & 3)
        assert lead + trail <= width
        decoded_image.extend([0] * lead + [bits(bpp) for _ in range(width-lead-trail)] + [0] * trail)
    return bytes(decoded_image), begin, offset


def inspect(root, stage):
    reader = module(root / 'tools/stage_preview.py', 'preview')
    video = module(root / 'makevrom.py', 'video')
    images, order, palettes = reader.load_bdd(root / 'data' / (stage + '.BDD'))
    label = {'BATTLE': 'leHDRS', 'NUPOOL': 'OLHDRS', 'TOMB': 'HDRS',
             'MOUNTAIN': 'tainHDRS', 'SPIRAL': 'alHDRS'}[stage]
    table = (root / 'tmp/load2/BGNDTBL.MK7').read_text()
    tables = header_tables(table, reader._num)
    headers = tables[label]
    assert len(headers) == len(order), (stage, 'header count')
    base, bank, payload = video.parse_irw(root / 'data/MK7MIL.IRW')
    assert bank == 1
    rows = 0
    intervals = []
    mismatches = []
    for i, index in enumerate(order):
        width, height, address, control = headers[i]
        im = images[index]
        assert (width, height) == (im['w'], im['h'])
        bpp = (control >> 12) & 7 or 8
        assert max(im['px']) < 1 << bpp, (index, 'palette index truncated')
        decoded_image, begin, offset = decode_header(payload, base, headers[i])
        different = sum(a != b for a, b in zip(decoded_image, im['px']))
        rows += height
        if different:
            mismatches.append({'image': index, 'width': width, 'height': height,
                               'different_pixels': different,
                               'decoded_sha256': hashlib.sha256(decoded_image).hexdigest(),
                               'source_sha256': hashlib.sha256(im['px']).hexdigest()})
        intervals.append((begin, offset))
    # Count distinct referenced bits; multiple headers may share a payload.
    merged = []
    for a, b in sorted(intervals):
        if merged and a <= merged[-1][1]:
            merged[-1][1] = max(b, merged[-1][1])
        else:
            merged.append([a, b])
    return {'stage': stage, 'images': len(order), 'rows_decoded': rows,
            'referenced_bits': sum(b-a for a, b in merged), 'mk7_payload_bytes': len(payload),
            'palettes': len(palettes), 'packed_pixels_identical': not mismatches,
            'shared_images': shared_images(tables, label, order),
            'source_mismatches': mismatches}


def compare_stage_bank(baseline, candidate, changed_tables=()):
    """Compare every unedited BDD header table, even when ROM addresses move.

    This covers BGNDTBL's static backgrounds, not the bank's separate IMG tables.
    """
    reader = module(baseline / 'tools/stage_preview.py', 'preview')
    video = module(baseline / 'makevrom.py', 'video')
    before = header_tables((baseline / 'tmp/load2/BGNDTBL.MK7').read_text(), reader._num)
    after = header_tables((candidate / 'tmp/load2/BGNDTBL.MK7').read_text(), reader._num)
    assert before.keys() == after.keys(), 'background header tables changed'
    assert set(changed_tables) <= before.keys(), 'unknown excluded header table'
    base_a, bank_a, payload_a = video.parse_irw(baseline / 'data/MK7MIL.IRW')
    base_b, bank_b, payload_b = video.parse_irw(candidate / 'data/MK7MIL.IRW')
    assert bank_a == bank_b == 1
    comparisons = []
    for label, headers in before.items():
        if label in changed_tables:
            continue
        changed = []
        other = after[label]
        for i in range(max(len(headers), len(other))):
            if i >= len(headers) or i >= len(other):
                changed.append(dict(header_index=i, reason='header added or removed'))
                continue
            a, b = headers[i], other[i]
            pixels_a = decode_header(payload_a, base_a, a)[0]
            pixels_b = decode_header(payload_b, base_b, b)[0]
            if a[:2] != b[:2] or pixels_a != pixels_b:
                changed.append(dict(header_index=i, before_dimensions=a[:2],
                                    after_dimensions=b[:2],
                                    before_sha256=hashlib.sha256(pixels_a).hexdigest(),
                                    after_sha256=hashlib.sha256(pixels_b).hexdigest()))
        comparisons.append(dict(table=label, images_before=len(headers),
                                images_after=len(other), changes=changed))
    return dict(baseline_mk7_payload_bytes=len(payload_a),
                candidate_mk7_payload_bytes=len(payload_b),
                measured_saving_bytes=len(payload_a)-len(payload_b),
                excluded_tables=sorted(changed_tables), tables=comparisons,
                unchanged_stage_pixels_identical=not any(t['changes'] for t in comparisons))


if __name__ == '__main__':
    if len(sys.argv) >= 5 and sys.argv[1] == '--compare-bank':
        report = compare_stage_bank(Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(),
                                    sys.argv[5:])
        output = Path(sys.argv[4])
        ok = report['unchanged_stage_pixels_identical']
    elif len(sys.argv) == 4:
        report = inspect(Path(sys.argv[1]).resolve(), sys.argv[2].upper())
        output = Path(sys.argv[3])
        ok = report['packed_pixels_identical']
    else:
        raise SystemExit('Usage: verify_static_stage_packing.py GAME STAGE NEW_REPORT.json\n'
                         '   or: verify_static_stage_packing.py --compare-bank BASELINE CANDIDATE '
                         'NEW_REPORT.json [EDITED_HEADER_TABLE ...]')
    with output.open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps(report))
    if not ok:
        raise SystemExit(2)
