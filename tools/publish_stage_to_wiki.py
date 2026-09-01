#!/usr/bin/env python3
"""Publish a stage share bundle to the project wiki.

Takes a bundle produced by bddtool's **File > Share Stage...** (or
`bddview --share-bundle-smoke`) and turns it into a wiki entry:

  <NAME>.md                     the page, at /wiki/<NAME>
  stages/<NAME>/...             its images and video
  Stage-Catalog.md              gains a row linking to it

Relative image paths in the generated page are rewritten to raw wiki URLs,
because relative asset links inside GitHub wiki pages do not resolve reliably.

Usage:
  publish_stage_to_wiki.py BUNDLE_DIR [--repo OWNER/REPO] [--author NAME]
                                      [--wiki DIR] [--push] [--dry-run]

Without --push it stages the commit locally and stops, so you can inspect it.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_REPO = "junkwax/midway-bddtool"
IMAGE_SUFFIXES = {".png", ".gif", ".mp4", ".webm", ".jpg"}
# The stage itself. A page that shows a stage off but withholds the two files
# it is made of is only half an entry, so these get published beside the
# renders and linked for download like any other asset.
STAGE_SUFFIXES = {".bdd", ".bdb"}
ASSET_SUFFIXES = IMAGE_SUFFIXES | STAGE_SUFFIXES


def run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> str:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise SystemExit(f"$ {' '.join(cmd)}\n{proc.stdout}{proc.stderr}")
    return proc.stdout


def raw_wiki_url(repo: str, path: str) -> str:
    return f"https://raw.githubusercontent.com/wiki/{repo}/{path}"


def rewrite_asset_links(page: str, repo: str, name: str) -> str:
    """Point every relative asset reference at its raw wiki URL."""
    prefix = f"stages/{name}"

    def repl(m: re.Match) -> str:
        bang, alt, target = m.group(1), m.group(2), m.group(3)
        if target.startswith(("http://", "https://", "#")):
            return m.group(0)
        if Path(target).suffix.lower() not in ASSET_SUFFIXES:
            return m.group(0)
        return f"{bang}[{alt}]({raw_wiki_url(repo, f'{prefix}/{target}')})"

    return re.sub(r"(!?)\[([^\]]*)\]\(([^)]+)\)", repl, page)


def catalog_row(name: str, author: str, planes: str, blocks: str) -> str:
    who = f"@{author}" if author and not author.startswith("@") else (author or "_unattributed_")
    return f"| [{name}]({name}) | {who} | {planes} | {blocks} |\n"


def read_stat(page: str, label: str) -> str:
    m = re.search(rf"\|\s*{re.escape(label)}\s*\|\s*(\d+)\s*\|", page)
    return m.group(1) if m else "?"


# A BDB is mostly ASCII module names with no NUL near the start, so git's text
# heuristic guesses "text" and a clone with core.autocrlf=true strips CR bytes
# out of the block table on commit -- publishing a stage that no longer loads.
# The BDD escapes this only by accident, because its pixel data trips the NUL
# check. Pin both so the outcome does not depend on the publisher's git config.
GITATTRIBUTES = (
    "# Stage files are binary -- never let autocrlf rewrite them.\n"
    "*.BDB -text\n*.BDD -text\n*.bdb -text\n*.bdd -text\n"
)


def ensure_binary_attrs(wiki: Path) -> None:
    path = wiki / ".gitattributes"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    if "*.BDB -text" in text:
        return
    path.write_text((text.rstrip() + "\n\n" if text.strip() else "") + GITATTRIBUTES,
                    encoding="utf-8")


BEGIN = "<!-- stages:begin -->"
END = "<!-- stages:end -->"


def update_catalog(wiki: Path, row: str, name: str) -> None:
    """Rows live between explicit markers so the surrounding prose is never
    guessed at, and re-publishing a stage replaces its row instead of adding
    a second one."""
    path = wiki / "Stage-Catalog.md"
    seed = ("# Stage Catalog\n\n## Stages\n\n"
            "| Stage | Author | Planes | Blocks |\n|---|---|---:|---:|\n"
            f"{BEGIN}\n{END}\n")
    text = path.read_text(encoding="utf-8") if path.exists() else seed
    if BEGIN not in text or END not in text:
        text = text.rstrip() + "\n\n" + f"{BEGIN}\n{END}\n"

    head, rest = text.split(BEGIN, 1)
    body, tail = rest.split(END, 1)

    rows = [ln for ln in body.splitlines() if ln.strip()]
    rows = [ln for ln in rows if not re.match(rf"\|\s*\[{re.escape(name)}\]", ln.strip())]
    rows.append(row.rstrip("\n"))
    rows.sort(key=str.lower)

    path.write_text(head + BEGIN + "\n" + "\n".join(rows) + "\n" + END + tail,
                    encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bundle", type=Path, help="bundle directory from Share Stage")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--author", default="", help="override the catalog author column")
    ap.add_argument("--wiki", type=Path, help="existing wiki clone to reuse")
    ap.add_argument("--push", action="store_true", help="push after committing")
    ap.add_argument("--dry-run", action="store_true", help="show what would change")
    args = ap.parse_args()

    bundle: Path = args.bundle
    if not bundle.is_dir():
        raise SystemExit(f"not a directory: {bundle}")
    pages = [p for p in bundle.glob("*.md")]
    if len(pages) != 1:
        raise SystemExit(f"expected exactly one .md in {bundle}, found {len(pages)}")
    page_path = pages[0]
    name = page_path.stem
    page = page_path.read_text(encoding="utf-8")

    wiki = args.wiki
    tmp_clone = False
    if wiki is None:
        wiki = bundle.parent / f"_wiki_{name}"
        if wiki.exists():
            shutil.rmtree(wiki)
        run(["git", "clone", "--quiet", f"https://github.com/{args.repo}.wiki.git", str(wiki)])
        tmp_clone = True

    ensure_binary_attrs(wiki)

    dest = wiki / "stages" / name
    dest.mkdir(parents=True, exist_ok=True)

    copied = []
    for src in sorted(bundle.rglob("*")):
        if src.is_dir() or src.suffix.lower() not in ASSET_SUFFIXES:
            continue
        rel = src.relative_to(bundle)
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)
        copied.append(str(rel).replace("\\", "/"))

    published = rewrite_asset_links(page, args.repo, name)
    (wiki / f"{name}.md").write_text(published, encoding="utf-8")

    author = args.author
    if not author:
        m = re.search(r"\|\s*\*\*Author\*\*\s*\|\s*([^|]+?)\s*\|", page)
        if m and "unattributed" not in m.group(1):
            author = m.group(1).strip()
    update_catalog(wiki,
                   catalog_row(name, author,
                               read_stat(page, "Modules (planes)"),
                               read_stat(page, "Blocks")),
                   name)

    total = sum(f.stat().st_size for f in dest.rglob("*") if f.is_file())
    print(f"stage:   {name}")
    print(f"assets:  {len(copied)} files, {total / 1024:.0f} KB -> stages/{name}/")
    print(f"page:    {name}.md")
    print(f"catalog: Stage-Catalog.md updated")

    if args.dry_run:
        print("\n(dry run — nothing committed)")
        print(run(["git", "status", "--short"], cwd=wiki))
        return 0

    run(["git", "add", "-A"], cwd=wiki)
    status = run(["git", "status", "--porcelain"], cwd=wiki)
    if not status.strip():
        print("\nnothing to commit — wiki already up to date")
        return 0
    run(["git", "commit", "--quiet", "-m", f"Publish stage: {name}"], cwd=wiki)
    print(f"\ncommitted to {wiki}")

    if args.push:
        run(["git", "push", "--quiet", "origin", "HEAD"], cwd=wiki)
        print(f"pushed -> https://github.com/{args.repo}/wiki/{name}")
    else:
        print("not pushed (pass --push)")
        if tmp_clone:
            print(f"clone kept at {wiki}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
