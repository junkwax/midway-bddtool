"""Create a new diagnostic IRW preserving explicitly selected baseline pixels.

This is a compatibility pass, not a LOAD2 repair or an authored-art correction.
It never modifies either checkout. Only reviewed, uncompressed BDD records are
supported. Shared references must all be approved and agree on the same pixels.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import verify_static_stage_packing as packing


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def restore_pixels(before, after, base_before, base_after, tables_before, tables_after,
                   approved, edited=(), other_spans=()):
    """Return new payload bytes; refuse all unapproved or ambiguous changes."""
    require(tables_before.keys() == tables_after.keys(), 'Header table set changed')
    require(set(edited) <= tables_before.keys(), 'Unknown edited table')
    approved = set(approved)
    require(approved, 'Select at least one reviewed record')
    require(all(t in tables_before and t not in edited and
                0 <= i < len(tables_before[t]) and i < len(tables_after[t])
                for t, i in approved), 'Invalid or edited approved record')
    cached = {}
    for table, headers in tables_after.items():
        for index, header in enumerate(headers):
            cached[table, index] = packing.decode_header(after, base_after, header)
    output = bytearray(after)
    writes = {}
    repairs = []
    for table, index in sorted(approved):
        a, b = tables_before[table][index], tables_after[table][index]
        require(a[:2] == b[:2] and a[3] == b[3], 'Record geometry or control changed')
        require(not b[3] & 0x80, 'Compressed records require a separate review')
        want = packing.decode_header(before, base_before, a)[0]
        got, start, end = cached[table, index]
        # A partial overlap can reinterpret pixel bits as another record's header.
        for key, (_, lo, hi) in cached.items():
            if lo < end and start < hi:
                require(key in approved and (lo, hi) == (start, end) and
                        tables_after[key[0]][key[1]][:2] == b[:2] and
                        tables_after[key[0]][key[1]][3] == b[3],
                        'Record has an unapproved or partial background alias')
        require(not any(lo < end and start < hi for lo, hi in other_spans),
                'Record overlaps a sprite-table reference')
        changes = [(i, x, y) for i, (x, y) in enumerate(zip(got, want)) if x != y]
        require(len(changes) <= 16, 'More than 16 changed pixels; not a narrow preservation')
        bpp = (b[3] >> 12) & 7 or 8
        for pixel, old, new in changes:
            offset = start + pixel * bpp
            for bit in range(bpp):
                at, value = offset + bit, (new >> bit) & 1
                require(at not in writes or writes[at] == value, 'Conflicting baseline aliases')
                writes[at] = value
                output[at // 8] = (output[at // 8] & ~(1 << (at % 8))) | (value << (at % 8))
            repairs.append(dict(table=table, header_index=index, x=pixel % b[0],
                                y=pixel // b[0], before=old, baseline=new, bit_offset=offset))
    # An approved alias with no changes must still agree after all writes.
    for table, headers in tables_after.items():
        if table not in edited:
            require(len(headers) == len(tables_before[table]), 'Unedited header count changed')
        for index, header in enumerate(headers):
            decoded = packing.decode_header(output, base_after, header)[0]
            expected = (packing.decode_header(before, base_before, tables_before[table][index])[0]
                        if table not in edited else cached[table, index][0])
            require(decoded == expected, f'Pixel verification failed: {table}:{index}')
            if table not in edited:
                require(header[:2] == tables_before[table][index][:2], 'Unedited size changed')
    return bytes(output), repairs


def preserve(baseline, candidate, output_folder, selections, edited):
    require(not output_folder.exists(), 'Output folder already exists')
    for root in (baseline, candidate):
        require(not output_folder.is_relative_to(root), 'Output must be outside both checkouts')
    reader = packing.module(baseline / 'tools/stage_preview.py', 'preview')
    video = packing.module(baseline / 'makevrom.py', 'video')
    paths = [root / 'data/MK7MIL.IRW' for root in (baseline, candidate)]
    raw_a, raw_b = [p.read_bytes() for p in paths]
    base_a, bank_a, pixels_a = video.parse_irw(paths[0])
    base_b, bank_b, pixels_b = video.parse_irw(paths[1])
    require(bank_a == bank_b == 1, 'Expected bank 1')
    # Refuse continuation records, trailers and guessed offsets.
    require(len(raw_b) == 68 + len(pixels_b) and raw_b[68:] == pixels_b and
            struct.unpack_from('<I', raw_b, 48)[0] == len(pixels_b),
            'Only a single explicit IRW payload is supported')
    header_a, header_b = [(root / 'tmp/load2/BGNDTBL.MK7').read_bytes()
                          for root in (baseline, candidate)]
    tables_a = packing.header_tables(header_a.decode(), reader._num)
    tables_b = packing.header_tables(header_b.decode(), reader._num)
    approved, sources = [], {}
    for selection in selections:
        stage, table, token = selection.split(':')
        require(stage.isalnum(), 'Stage must be a simple data-file stem')
        index = int(token)
        for ext in ('BDD', 'BDB'):
            rel = f'data/{stage}.{ext}'
            a, b = [(root / rel).read_bytes() for root in (baseline, candidate)]
            require(a == b, f'Authored source changed: {rel}')
            sources[rel] = sha(a)
        images, order, _ = reader.load_bdd(baseline / 'data' / f'{stage}.BDD')
        require(table in tables_a and len(order) == len(tables_a[table]) and
                0 <= index < len(order), 'Source/header count or index mismatch')
        image = images[order[index]]
        require([image['w'], image['h']] == tables_a[table][index][:2],
                'Source/header dimensions differ')
        approved.append((table, index))
    # Refuse aliases in the candidate's parsed sprite tables, including unused
    # legacy headers. Sprite SAGs use flat-ROM bit coordinates; IRW bases
    # require makevrom's bank mapping before these intervals can be compared.
    sprite_reader = packing.module(candidate / 'tools/audit_sprite_bpp.py', 'sprite_reader')
    other_spans = []
    flat_base = (video.BANK_OFFSETS[bank_b] + video.image_byte_offset(base_b, bank_b)) * 8
    for _, _, w, h, sag, ctrl in sprite_reader.parse_tbls(str(candidate / 'src')):
        if flat_base <= sag < flat_base + len(pixels_b) * 8:
            require(w > 0 and h > 0, 'Invalid sprite-table geometry in this payload')
            start = sag - flat_base
            # Upper bound also covers stale/undecodable records. Never dismiss
            # a possible alias merely because its existing pixels do not decode.
            end = start + w * h * ((ctrl >> 12) & 7 or 8) + (8 * h if ctrl & 0x80 else 0)
            other_spans.append((start, end))
    result, repairs = restore_pixels(pixels_a, pixels_b, base_a, base_b, tables_a, tables_b,
                                     approved, edited, other_spans)
    irw = bytearray(raw_b)
    irw[68:] = result
    checksum = sum(int.from_bytes(result[i:i+2], 'little') for i in range(0, len(result), 2))
    struct.pack_into('<I', irw, 52, checksum & 0xffffffff)
    report = dict(mode='preserve baseline decoded pixels; does not repair LOAD2',
                  baseline_irw_sha256=sha(raw_a), candidate_irw_sha256=sha(raw_b),
                  output_irw_sha256=sha(irw), baseline_headers_sha256=sha(header_a),
                  candidate_headers_sha256=sha(header_b), source_sha256=sources,
                  approved=selections, edited_tables=sorted(edited), repairs=repairs,
                  parsed_sprite_spans_checked=len(other_spans),
                  changed_payload_bits=sum((a ^ b).bit_count() for a, b in zip(pixels_b, result)),
                  payload_bytes=len(result), saved_bytes=len(pixels_a)-len(result))
    output_folder.mkdir(parents=True)
    (output_folder / 'MK7MIL.IRW').write_bytes(irw)
    (output_folder / 'preservation.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('new_folder', type=Path)
    parser.add_argument('--keep', action='append', required=True, metavar='STAGE:TABLE:INDEX')
    parser.add_argument('--edited', action='append', default=[], metavar='TABLE')
    args = parser.parse_args()
    try:
        report = preserve(args.baseline.resolve(), args.candidate.resolve(), args.new_folder.resolve(),
                          args.keep, args.edited)
    except (ValueError, AssertionError, IndexError, KeyError) as error:
        parser.exit(2, 'Preservation refused: ' + (str(error) or type(error).__name__) + '\n')
    print(json.dumps(report, indent=2))
