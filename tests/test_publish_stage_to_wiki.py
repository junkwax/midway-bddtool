import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("publisher", ROOT / "tools/publish_stage_to_wiki.py")
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PublisherCompanions(unittest.TestCase):
    def test_layout_metadata_links_and_binary_attributes(self):
        page = "[Layout](TEST.bddstudio) [Metadata](TEST.BDD.meta) [BDD](TEST.BDD)"
        output = publisher.rewrite_asset_links(page, "owner/repo", "TEST")
        for suffix in ("bddstudio", "BDD.meta", "BDD"):
            self.assertIn(f"https://raw.githubusercontent.com/wiki/owner/repo/stages/TEST/TEST.{suffix}", output)
        with tempfile.TemporaryDirectory() as tmp:
            wiki = Path(tmp)
            (wiki / ".gitattributes").write_text("*.BDB -text\n*.BDD -text\n", encoding="utf-8")
            publisher.ensure_binary_attrs(wiki)
            once = (wiki / ".gitattributes").read_text(encoding="utf-8")
            self.assertIn("*.bddstudio -text", once)
            self.assertIn("*.meta -text", once)
            publisher.ensure_binary_attrs(wiki)
            self.assertEqual(once, (wiki / ".gitattributes").read_text(encoding="utf-8"))

    def test_local_dry_run_preserves_companion_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle, wiki = root / "bundle", root / "wiki"
            bundle.mkdir(); wiki.mkdir()
            subprocess.run(["git", "init", "--quiet", str(wiki)], check=True, capture_output=True)
            page = "# TEST\n\n[Layout](TEST.bddstudio)\n[Metadata](TEST.BDD.meta)\n| Modules (planes) | 2 |\n| Blocks | 3 |\n"
            (bundle / "TEST.md").write_text(page, encoding="utf-8")
            files = {"TEST.BDB": b"binary\r\n\x00\xff", "TEST.BDD": b"pixels\x00\r\n",
                     "TEST.bddstudio": b"BDDSTUDIO 2\r\n", "TEST.BDD.meta": b"opaque\xff\r\n"}
            for name, data in files.items():
                (bundle / name).write_bytes(data)
            result = subprocess.run(["python", str(ROOT / "tools/publish_stage_to_wiki.py"), str(bundle),
                                     "--wiki", str(wiki), "--dry-run"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name, data in files.items():
                self.assertEqual((wiki / "stages/TEST" / name).read_bytes(), data)
            self.assertIn("stages/TEST/TEST.bddstudio", (wiki / "TEST.md").read_text(encoding="utf-8"))
            self.assertIn("| 2 | 3 |", (wiki / "Stage-Catalog.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
