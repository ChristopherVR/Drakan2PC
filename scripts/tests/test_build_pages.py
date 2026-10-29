"""Protect the public artifact boundary, project URLs and existing browser-player storage routes."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_pages", ROOT / "scripts/buildtool/build_pages.py")
pages = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pages)


@unittest.skipUnless(importlib.util.find_spec("markdown"), "Install docs/site/requirements.txt for site tests")
class PagesTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="pages-test-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "site"
        pages.build(self.output)

    def test_published_artifact_and_player_compatibility(self):
        manifest = json.loads((ROOT / "docs/site/pages.json").read_text(encoding="utf-8"))
        expected = {page["url"] for page in manifest} | {
            "index.html",
            "browser.html",
            "get-started.html",
            "404.html",
            "search-index.json",
            ".nojekyll",
            *pages.PLAYER_FILES,
        }
        expected |= {"assets/" + path.name for path in (ROOT / "docs/site/assets").iterdir()}
        actual = {path.relative_to(self.output).as_posix() for path in self.output.rglob("*") if path.is_file()}
        self.assertEqual(actual, expected)
        for name in pages.PLAYER_FILES:
            self.assertEqual((self.output / name).read_bytes(), (ROOT / "runtime/web/pages" / name).read_bytes())
        player = (self.output / "browser.html").read_text(encoding="utf-8")
        for control in ("pick-build", "pick-iso", "pick-textures", "play", "status"):
            self.assertIn(f'id="{control}"', player)
        self.assertIn('src="loader.js"', player)

    def test_nested_pages_and_search_index(self):
        controls = (self.output / "guides/controls.html").read_text(encoding="utf-8")
        self.assertIn('href="../assets/site.css"', controls)
        self.assertIn('href="saves.html"', controls)
        self.assertIn('aria-current="page">Controls &amp; combat', controls)
        index = json.loads((self.output / "search-index.json").read_text(encoding="utf-8"))
        enhancements = next(page for page in index if page["url"] == "guides/enhancements.html")
        self.assertIn("Dual wielding", enhancements["text"])
        for page in index:
            self.assertTrue((self.output / page["url"]).is_file())
        not_found = (self.output / "404.html").read_text(encoding="utf-8")
        self.assertIn('href="/Drakan2PC/assets/site.css"', not_found)
        pages.validate_site(self.output, "/Drakan2PC/")

    def test_validation_rejects_missing_links_fragments_and_private_files(self):
        bad = self.output / "broken.html"
        for link in ("missing.html", "guides/install.html#not-a-heading"):
            bad.write_text(f'<a href="{link}">Broken</a>', encoding="utf-8")
            with self.assertRaises(ValueError):
                pages.validate_site(self.output, "/Drakan2PC/")
        bad.unlink()
        (self.output / "game.wasm").write_bytes(b"not real game data")
        with self.assertRaisesRegex(ValueError, "Unexpected public artifact"):
            pages.validate_site(self.output, "/Drakan2PC/")

    def test_root_hosting_and_output_guard(self):
        pages.build(self.output, "/")
        not_found = (self.output / "404.html").read_text(encoding="utf-8")
        self.assertIn('href="/assets/site.css"', not_found)
        self.assertNotIn('href="//', not_found)
        for unsafe in (ROOT, ROOT / "build", ROOT / "docs", ROOT / "dist/DrakanPC"):
            with self.assertRaisesRegex(ValueError, "dedicated directory"):
                pages.build(unsafe)

    def test_source_link_mapping(self):
        source = ROOT / "docs/guides/install.md"
        published = {(ROOT / "BUILDING.md").resolve(): "development/building.html"}
        self.assertEqual(
            pages.rewrite_link("../../BUILDING.md#prerequisites", source, "guides/install.html", published),
            "../development/building.html#prerequisites",
        )
        with self.assertRaisesRegex(ValueError, "Missing source link"):
            pages.rewrite_link("missing.md", source, "guides/install.html", published)

    def test_media_and_linux_guide(self):
        content = (self.output / "guides/enhancements.html").read_text(encoding="utf-8")
        parser = pages.PageParser()
        parser.feed(content)
        self.assertIn('class="media-figure"', content)
        self.assertIn('data-animation="https://github.com/', content)
        self.assertIn('aria-pressed="false" hidden', content)
        self.assertNotRegex(content, r'<img[^>]+src="[^"]+\.gif"')
        self.assertNotIn("What's new", content)
        linux = (self.output / "guides/linux.html").read_text(encoding="utf-8")
        self.assertIn("Build-Drakan.sh", linux)
        self.assertIn("Play-Drakan.sh", linux)
        with self.assertRaisesRegex(ValueError, "Unknown media entry"):
            pages.render_media("<!-- media:missing -->", {})

    def test_comparison_and_clips(self):
        home = (self.output / "index.html").read_text(encoding="utf-8")
        self.assertEqual(home.count('<figure class="compare"'), 3)
        self.assertEqual(home.count('class="compare-tab"'), 9)
        self.assertNotIn("animation-toggle", home)
        self.assertIn('data-before-label="Ray tracing off"', home)
        self.assertIn('compare-label-before" aria-hidden="true">Original<', home)
        self.assertIn('class="compare-range" type="range"', home)
        self.assertRegex(
            home, r'<video src="https://github\.com/[^"]+\.mp4"[^>]* muted loop playsinline preload="none"'
        )
        self.assertIn('class="backdrop"', home)
        self.assertNotIn("<!-- media:", home)
        controls = (self.output / "guides/controls.html").read_text(encoding="utf-8")
        self.assertIn('class="media-figure media-video"', controls)
        media = json.loads((ROOT / "docs/site/media.json").read_text(encoding="utf-8"))
        scene = {**media["hd-compare"]["scenes"][0], "after_url": "https://x/a.jpg"}
        self.assertIn("https://x/a.jpg", pages.media_urls({**media["hd-compare"], "scenes": [scene]}))

    def test_diagrams_pending_media_and_compare_shape(self):
        media = json.loads((ROOT / "docs/site/media.json").read_text(encoding="utf-8"))
        # Every diagram is a committed SVG that the site publishes and the guides reference.
        diagrams = {key: item for key, item in media.items() if item.get("kind") == "diagram"}
        self.assertTrue(diagrams)
        for key, item in diagrams.items():
            self.assertTrue((self.output / "assets" / item["file"]).is_file())
        overview = (self.output / "development/index.html").read_text(encoding="utf-8")
        self.assertIn('<figure class="media-figure media-diagram"><a class="media-enlarge" href="../assets/', overview)
        self.assertIn('src="../assets/diagram-architecture.svg"', overview)
        # Player guides show real captures, not generated diagrams.
        for page in (self.output / "guides").glob("*.html"):
            self.assertNotIn("media-diagram", page.read_text(encoding="utf-8"), page.name)
        building = (self.output / "development/building.html").read_text(encoding="utf-8")
        self.assertIn('src="../assets/diagram-build-pipeline.svg"', building)
        # A pending capture renders nothing: no figure, no URL and no leftover marker.
        pending = {"url": f"{pages.REPOSITORY}/releases/download/site-media-1/x.jpg", "pending": True}
        for source in ("<p><!-- media:p --></p>", f'<p><img alt="x" src="{pending["url"]}"></p>'):
            self.assertEqual(pages.render_media(source, {"p": {**pending, "alt": "x", "caption": "x"}}), "")
        for key, item in media.items():
            if item.get("pending"):
                for page in self.output.rglob("*.html"):
                    text = page.read_text(encoding="utf-8")
                    for url in pages.media_urls(item):
                        self.assertNotIn(url, text, f"{key} is pending but rendered in {page.name}")
        # Non-16:9 comparisons carry their own aspect ratio.
        scene = {"label": "A", "before_url": "b.jpg", "after_url": "a.jpg"}
        square = pages.compare_figure({"width": 1280, "height": 960, "caption": "c", "scenes": [scene]})
        self.assertIn('style="aspect-ratio: 1280 / 960"', square)
        wide = pages.compare_figure({"width": 1920, "height": 1080, "caption": "c", "scenes": [scene]})
        self.assertNotIn("aspect-ratio", wide)

    def test_feature_clips_and_loops(self):
        clip = {"kind": "clip", "label": "F5 & F8", "url": "p.jpg", "video_url": "v.mp4", "alt": "a", "caption": "c"}
        link = pages.render_media("<li><!-- media:q --></li>", {"q": clip})
        self.assertEqual(
            link,
            '<li><a class="feature-clip" href="v.mp4" data-poster="p.jpg" data-alt="a" data-caption="c">F5 &amp; F8</a></li>',
        )
        # A pending chip keeps its label but no URL.
        self.assertEqual(
            pages.render_media("<li><!-- media:q --></li>", {"q": {**clip, "pending": True}}),
            "<li><span>F5 &amp; F8</span></li>",
        )
        loop = pages.media_figure({**clip, "kind": None, "loop": True, "width": 1280, "height": 720})
        self.assertIn("autoplay muted loop playsinline", loop)
        self.assertNotIn("toggle", loop)


if __name__ == "__main__":
    unittest.main()
