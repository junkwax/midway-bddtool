#!/usr/bin/env python3
"""Smoke-test bddview load/save round trips.

The test exercises the app's real BDB/BDD loader and saver through
``bddview --roundtrip-save``. Each generated output is then checked with
``bddtool validate`` and ``bddtool diff``; BDD payloads are compared byte-for-
byte to catch binary image/palette drift.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def exe_candidates(explicit: Path | None, env_name: str, basename: str) -> list[Path]:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    env = os.environ.get(env_name)
    if env:
        candidates.append(Path(env))
    local_appdata = os.environ.get("LOCALAPPDATA")
    if local_appdata:
        candidates.append(Path(local_appdata) / "bddview-build" / "build" / "Release" / basename)
    candidates.extend(
        [
            REPO_ROOT / "build-win" / "Release" / basename,
            REPO_ROOT / "build" / "Release" / basename,
            REPO_ROOT / "build" / basename.removesuffix(".exe"),
        ]
    )
    return candidates


def find_exe(explicit: Path | None, env_name: str, basename: str) -> Path:
    for candidate in exe_candidates(explicit, env_name, basename):
        if candidate.exists():
            return candidate
    raise SystemExit(f"{basename} not found; run build first or pass --{basename.removesuffix('.exe')}")


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(cmd))
    proc = subprocess.run(cmd, cwd=REPO_ROOT, text=True, capture_output=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return proc


def companion(path: Path, suffix: str) -> Path:
    candidates = [
        path.with_suffix(suffix),
        path.with_suffix(suffix.lower()),
        path.with_suffix(suffix.upper()),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit(f"missing companion {suffix} for {path}")


def compare_bytes(label: str, a: Path, b: Path) -> None:
    ab = a.read_bytes()
    bb = b.read_bytes()
    if ab != bb:
        raise SystemExit(
            f"{label}: BDD byte drift after round trip: {a} ({len(ab)} bytes) != {b} ({len(bb)} bytes)"
        )


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, width: int, height: int, pixels: list[tuple[int, int, int, int]]) -> None:
    if len(pixels) != width * height:
        raise ValueError("pixel count does not match PNG dimensions")
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(bytes(pixels[y * width + x]))
    payload = bytearray(b"\x89PNG\r\n\x1a\n")
    payload.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
    payload.extend(png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9)))
    payload.extend(png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def png_import_roundtrip(work: Path, bddview: Path) -> None:
    png = work / "png_src" / "rgb555_probe.png"
    write_rgba_png(
        png,
        4,
        2,
        [
            (132, 0, 0, 255),
            (0, 132, 0, 255),
            (0, 0, 132, 255),
            (255, 255, 255, 255),
            (5, 11, 17, 255),
            (123, 87, 45, 255),
            (240, 128, 8, 255),
            (0, 0, 0, 0),
        ],
    )
    prefix = work / "png_import" / "RGB555_PROBE"
    prefix.parent.mkdir(parents=True, exist_ok=True)
    run([str(bddview), "--import-png-smoke", str(png), str(prefix)])
    print("png import RGB555 roundtrip ok")


def roundtrip_one(label: str, source_bdb: Path, work: Path, bddview: Path, bddtool: Path) -> None:
    source_bdb = source_bdb.resolve()
    source_bdd = companion(source_bdb, ".BDD").resolve()
    out_dir = work / label
    out_dir.mkdir(parents=True, exist_ok=True)
    prefix = out_dir / "ROUND"
    out_bdb = prefix.with_suffix(".BDB")
    out_bdd = prefix.with_suffix(".BDD")

    run([str(bddview), "--roundtrip-save", str(source_bdb), str(prefix)])
    run([str(bddtool), "validate", str(out_bdb), str(out_bdd)])
    run([str(bddtool), "diff", str(source_bdb), str(out_bdb)])
    compare_bytes(label, source_bdd, out_bdd)
    print(f"{label}: roundtrip ok")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bddview", type=Path, help="Path to bddview executable")
    parser.add_argument("--bddtool", type=Path, help="Path to bddtool executable")
    parser.add_argument("--work-dir", type=Path, default=REPO_ROOT / "tmp" / "roundtrip_smoke")
    parser.add_argument(
        "--fixture",
        type=Path,
        action="append",
        help="Additional BDB fixture to round-trip; may be passed more than once",
    )
    args = parser.parse_args()

    bddview = find_exe(args.bddview, "BDDVIEW_EXE", "bddview.exe")
    bddtool = find_exe(args.bddtool, "BDDTOOL_EXE", "bddtool.exe")
    work = args.work_dir
    work.mkdir(parents=True, exist_ok=True)

    checker_prefix = work / "checker_src" / "CHECKER"
    checker_prefix.parent.mkdir(parents=True, exist_ok=True)
    run([str(bddview), "--write-checker-test", str(checker_prefix)])
    png_import_roundtrip(work, bddview)

    fixtures = [checker_prefix.with_suffix(".BDB")]
    stock_dir = REPO_ROOT / "reference" / "stages" / "stock"
    for stock_name in ("ARMORY1", "KUNGFU1", "DEDPOOL"):
        default_stock = stock_dir / f"{stock_name}.BDB"
        if default_stock.exists():
            fixtures.append(default_stock)
    if args.fixture:
        fixtures.extend(args.fixture)

    seen: set[Path] = set()
    for fixture in fixtures:
        resolved = fixture.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        label = resolved.stem.lower()
        if resolved.parent.name.lower() != "stock":
            label = f"{resolved.parent.name.lower()}_{label}"
        roundtrip_one(label, resolved, work, bddview, bddtool)

    print("roundtrip smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
