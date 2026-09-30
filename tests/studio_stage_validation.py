#!/usr/bin/env python3
"""Compare two trusted MK2 build snapshots after packing MK3CAVE with LOAD2.

Reads each snapshot's game generator and makevrom.py. Checks runtime placement,
both flip axes, palette identity, every packed image row and current ROM slots.
This does not build, install ROMs, or substitute for an emulator check.
Requires Pillow. Output is a new JSON file; game snapshots are not modified.
"""

import argparse
import importlib.util
import json
import sys
from pathlib import Path

from PIL import Image

sys.dont_write_bytecode = True


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(root, name):
    game = module(root / "tools/make_mk3cave.py", name)
    video = module(root / "makevrom.py", name + "_video")
    images, order, palettes, layers = game.runtime_modules()
    headers = game.merged_headers(images, order)
    payloads = game._irw_payloads()
    rows = 0
    packs = {}
    for pack_name, _, _ in game.PACKS:
        start, end, _ = video.CUSTOM_VIDEO_SLOTS[pack_name + ".IRW"]
        base, bank, payload = video.parse_irw(root / "data" / (pack_name + ".IRW"))
        actual_start = game.sag_phys(base + 0x2000000)
        require(bank == 1 and actual_start == start, f"{name}/{pack_name}: wrong bank/base")
        require(start + len(payload) <= end, f"{name}/{pack_name}: current slot overflow")
        packs[pack_name] = {"bytes": len(payload), "slot_bytes": end - start,
                            "free_bytes": end - start - len(payload)}
    for index, (width, height, address, control) in zip(order, headers):
        image = images[index]
        bpp = (control >> 12) & 7 or 8
        require(max(image["px"]) < 1 << bpp, f"{name}/{index}: palette indices truncated")
        offsets, bits, bad_rows = game.walk_rows(image, address, control, payloads)
        require(not bad_rows, f"{name}/{index}: packed pixels differ on rows {bad_rows}")
        lod_address = address - 0x2000000
        require(any(base <= lod_address and lod_address + bits <= base + len(data) * 8
                    for base, data in payloads), f"{name}/{index}: image reads beyond payload")
        rows += len(offsets)
    # Rebuild the actual water descriptions too; this validates their row offsets.
    spans = game.water_spans(images, order, palettes, layers, headers)
    rendered = {}
    for layer in layers:
        canvas = Image.new("RGBA", (layer["w"], layer["h"]))
        for block in layer["blocks"]:
            image = images[block["hdr"]]
            palette = palettes[block["pal"]][1]
            tile = Image.new("RGBA", (image["w"], image["h"]))
            tile.putdata([(0, 0, 0, 0) if p == 0 else game.rgb(palette[p]) + (255,)
                          for p in image["px"]])
            if block["z"] & 0x10:
                tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if block["z"] & 0x20:
                tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            canvas.paste(tile, (block["rx"], block["ry"]), tile)
        rendered[layer["name"]] = (canvas, game.PLACE[layer["name"]])
    return game, palettes, rendered, {
        "images": len(order), "placements": sum(len(layer["blocks"]) for layer in layers),
        "rows_verified": rows, "water_spans": len(spans), "packs": packs,
        "video_bytes": sum(pack["bytes"] for pack in packs.values()),
    }


def compare(before_root, after_root):
    before, before_palettes, before_layers, before_report = inspect(before_root, "before")
    after, after_palettes, after_layers, after_report = inspect(after_root, "after")
    require(before_palettes == after_palettes, "Runtime palette names/indices/colors changed")
    for constant in ("CAMERA", "GROUND_Y", "WORLD_Y", "AUTOERASE", "FLOOR", "RATES", "WATER"):
        require(getattr(before, constant) == getattr(after, constant), constant + " changed")
    require(before.water_wave() == after.water_wave(), "Water timing/wave changed")
    require((before_root / "data/FL_CAVE.BIN").read_bytes() ==
            (after_root / "data/FL_CAVE.BIN").read_bytes(), "Floor pixels changed")
    require(before.CAVEFL_P == after.CAVEFL_P, "Floor palette changed")
    require(before_layers.keys() == after_layers.keys(), "Runtime module set changed")
    layers = {}
    for name, (original, (plane, x, y)) in before_layers.items():
        candidate, (new_plane, new_x, new_y) = after_layers[name]
        require(plane == new_plane, name + ": plane changed")
        left, top = min(x, new_x), min(y, new_y)
        width = max(x + original.width, new_x + candidate.width) - left
        height = max(y + original.height, new_y + candidate.height) - top
        a, b = Image.new("RGBA", (width, height)), Image.new("RGBA", (width, height))
        a.paste(original, (x - left, y - top))
        b.paste(candidate, (new_x - left, new_y - top))
        require(a.tobytes() == b.tobytes(), name + ": runtime world pixels differ")
        layers[name] = {"pixels_equal": True, "before_size": original.size,
                        "after_size": candidate.size, "origin_delta": [new_x - x, new_y - y]}
    return {"baseline": before_report, "candidate": after_report, "layers": layers,
            "video_bytes_saved": before_report["video_bytes"] - after_report["video_bytes"],
            "note": "Packed pixels and runtime layer geometry verified; MAME validation is separate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path, help="new JSON report path")
    args = parser.parse_args()
    require(not args.output.exists(), "Output already exists")
    report = compare(args.before.resolve(), args.after.resolve())
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {report['video_bytes_saved']:,} packed video bytes saved; "
          "all runtime layers match and every packed image row matches.")


if __name__ == "__main__":
    main()
