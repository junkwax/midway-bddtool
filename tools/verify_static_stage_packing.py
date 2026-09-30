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


def inspect(root, stage):
    reader = module(root / 'tools/stage_preview.py', 'preview')
    video = module(root / 'makevrom.py', 'video')
    images, order, palettes = reader.load_bdd(root / 'data' / (stage + '.BDD'))
    label = {'BATTLE': 'leHDRS', 'NUPOOL': 'OLHDRS'}[stage]
    table = (root / 'tmp/load2/BGNDTBL.MK7').read_text()
    start = re.search(r'^' + label + r':\s*$', table, re.M).end()
    values = []
    for line in table[start:].splitlines():
        line = line.split(';')[0].strip()
        if not line:
            continue
        if not line.lower().startswith(('.word', '.long')):
            break
        values += [reader._num(t) for t in line.split(None, 1)[1].split(',')]
    assert len(values) == len(order) * 4, (stage, 'header count')
    base, bank, payload = video.parse_irw(root / 'data/MK7MIL.IRW')
    assert bank == 1
    rows = 0
    intervals = []
    mismatches = []
    for i, index in enumerate(order):
        width, height, address, control = values[i*4:i*4+4]
        im = images[index]
        assert (width, height) == (im['w'], im['h'])
        bpp = (control >> 12) & 7 or 8
        assert max(im['px']) < 1 << bpp, (index, 'palette index truncated')
        offset = address - 0x2000000 - base
        begin = offset
        different = 0
        decoded_image = bytearray()

        def bits(count):
            nonlocal offset
            assert 0 <= offset and offset + count <= len(payload) * 8
            at, shift = divmod(offset, 8)
            value = (int.from_bytes(payload[at:at+(shift+count+7)//8], 'little') >> shift) & ((1 << count)-1)
            offset += count
            return value

        for y in range(height):
            lead = trail = 0
            if control & 0x80:
                header = bits(8)
                lead = (header & 15) << ((control >> 8) & 3)
                trail = (header >> 4) << ((control >> 10) & 3)
            assert lead + trail <= width
            decoded = bytes([0] * lead + [bits(bpp) for _ in range(width-lead-trail)] + [0] * trail)
            different += sum(a != b for a, b in zip(decoded, im['px'][y*width:(y+1)*width]))
            decoded_image.extend(decoded)
            rows += 1
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
            'source_mismatches': mismatches}


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('Usage: verify_static_stage_packing.py GAME STAGE NEW_REPORT.json')
    report = inspect(Path(sys.argv[1]).resolve(), sys.argv[2].upper())
    with Path(sys.argv[3]).open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps(report))
    if not report['packed_pixels_identical']:
        raise SystemExit(2)
