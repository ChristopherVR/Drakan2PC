"""Record the editor screenshots and clips of the documentation site, with no disc and no game data.

The editor's browser build (``editor/packages/app``) runs from a local static server in a private Playwright context.
Nothing from a disc is opened: the start page, the help guides, the command palette and Project Settings work without
one, a made-up project is put into the editor's local storage, and the Save Editor opens a synthetic ``.rsg`` built by
the editor's own test fixture (``editor/packages/formats/test/livefixture.ts``) with a drawn gradient as its picture.

Output goes to ``build/site-media/editor/out`` (ignored): ``<id>.jpg`` stills and posters, ``<id>.mp4`` clips (H.264,
muted, small), ``manifest.json`` (size, SHA-256, dimensions per file) and ``UPLOAD.md`` (the release upload commands).
Nothing is uploaded or committed. Review every file, upload them to the ``site-media-*`` release named in
``docs/site/media.json``, then remove the ``"pending"`` flags (``docs/reference/github-pages.md``).

Run it again after the editor UI changes; the scenes find controls by their visible text, so a renamed button fails
loudly instead of recording the wrong thing.

    python scripts/buildtool/capture_editor_media.py            # build the editor, record every scene
    python scripts/buildtool/capture_editor_media.py --scene save-editor-tour --skip-build

Needs Node 22+ with ``npm ci`` done in ``editor/``, the Python ``playwright`` package with Chrome (``channel=chrome``) or
``playwright install chromium``, and FFmpeg on PATH (or ``--ffmpeg``).
"""

from __future__ import annotations

import argparse
import contextlib
import functools
import hashlib
import http.server
import json
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "editor"
WORK = ROOT / "build" / "site-media" / "editor"
APP = WORK / "app"
SYNTH = WORK / "synthetic"
OUT = WORK / "out"
VIEWPORT = {"width": 1280, "height": 800}
RELEASE = "site-media-20261102"

# A made-up mod project: level titles are invented, nothing comes from a disc.
DEMO_PROJECT = {
    "format": 1,
    "id": "the-lost-road",
    "name": "The Lost Road",
    "author": "",
    "version": "1.0.0",
    "description": "",
    "levels": [
        {"title": "Level A - Old Village"},
        {"title": "Level B - Mistwood"},
        {"title": "Level C - Sunken Caves"},
        {"title": "Level D - Watchtower"},
    ],
    "start": "Level A - Old Village",
    "task": "campaign",
    "ticks": {},
    "createdAt": 0,
    "mapLayout": {
        "Level A - Old Village": {"x": 40, "y": 140},
        "Level B - Mistwood": {"x": 360, "y": 60},
        "Level C - Sunken Caves": {"x": 680, "y": 140},
        "Level D - Watchtower": {"x": 680, "y": 300},
    },
}

# Synthetic save: the test fixture's records, a drawn picture and an invented level name (Node strips the TS types).
SAVE_SCRIPT = r"""
import { writeFileSync } from 'node:fs';
import { liveEntries } from './test/livefixture.ts';
import { buildSave } from './test/savefixture.ts';

const [out] = process.argv.slice(2);
const w = 160, h = 120;
const pic = new Uint8Array(16 + w * h * 2);
const dv = new DataView(pic.buffer);
dv.setUint32(0, w, true); dv.setUint32(4, h, true); dv.setUint32(8, 16, true); dv.setUint32(12, w * h * 2, true);
for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
        const ridge = 70 + 14 * Math.sin(x / 13) + 8 * Math.sin(x / 5.3);
        const t = y / h;
        let r = 6 + 20 * t, g = 5 + 9 * t, b = 12 + 6 * (1 - t);
        if (y > ridge) { r = 3; g = 4; b = 6; }
        const px = 0x8000 | (Math.round(b) << 10) | (Math.round(g) << 5) | Math.round(r);
        dv.setUint16(16 + (y * w + x) * 2, px, true);
    }
}
const name = 'Mistwood Vale';
const label = new Uint8Array(2 + name.length + 3);
new DataView(label.buffer).setUint16(0, name.length + 3, true);
for (let i = 0; i < name.length; i++) label[2 + i] = name.charCodeAt(i);
const entries = liveEntries({}).map((e) =>
    e.type === 0x23 ? { ...e, data: pic } : e.type === 0x401 && e.id === 891 ? { ...e, data: label } : e,
);
writeFileSync(out, buildSave(entries));
"""

# Drawn pointer, so clips show where the clicks go (headless captures have no cursor).
CURSOR_SCRIPT = r"""
window.addEventListener('DOMContentLoaded', () => {
    const c = document.createElement('div');
    c.id = '__doc_cursor';
    c.innerHTML = '<svg width="22" height="22" viewBox="0 0 22 22"><path d="M3 2l14 9-6 1.5L8 19z" fill="#fff" ' +
        'stroke="#111" stroke-width="1.5" stroke-linejoin="round"/></svg>';
    Object.assign(c.style, { position: 'fixed', left: '-40px', top: '-40px', zIndex: 2147483647,
        pointerEvents: 'none', transition: 'transform 80ms', transformOrigin: '3px 2px' });
    document.documentElement.appendChild(c);
    addEventListener('mousemove', (e) => { c.style.left = e.clientX - 3 + 'px'; c.style.top = e.clientY - 2 + 'px'; },
        true);
    addEventListener('mousedown', () => { c.style.transform = 'scale(0.82)'; }, true);
    addEventListener('mouseup', () => { c.style.transform = ''; }, true);
});
"""

NO_FS_ACCESS = "delete window.showOpenFilePicker; delete window.showDirectoryPicker; delete window.showSaveFilePicker;"


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True, shell=sys.platform == "win32" and cmd[0] in ("npx", "npm"))


def build_editor() -> None:
    run(["npx", "vite", "build", "--outDir", str(APP), "--emptyOutDir"], EDITOR / "packages" / "app")


def build_save() -> Path:
    SYNTH.mkdir(parents=True, exist_ok=True)
    out = SYNTH / "mistwood.rsg"
    with tempfile.NamedTemporaryFile("w", suffix=".mts", dir=EDITOR / "packages" / "formats", delete=False) as f:
        f.write(SAVE_SCRIPT)
    try:
        run(
            ["node", "--experimental-transform-types", "--no-warnings", f.name, str(out)],
            EDITOR / "packages" / "formats",
        )
    finally:
        Path(f.name).unlink()
    return out


@contextlib.contextmanager
def serve(directory: Path):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass

    handler = functools.partial(Quiet, directory=str(directory))
    with socketserver.ThreadingTCPServer(("127.0.0.1", 0), handler) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            yield f"http://127.0.0.1:{server.server_address[1]}"
        finally:
            server.shutdown()


class Scene:
    """One page in a fresh private context; ``mark()`` starts the clip, ``still()`` saves a JPEG."""

    def __init__(self, browser, base: str, name: str, video: bool, init: tuple[str, ...] = ()):
        self.name = name
        self.video_dir = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=WORK)) if video else None
        options = {"viewport": VIEWPORT, "device_scale_factor": 1, "color_scheme": "dark", "locale": "en-GB"}
        if self.video_dir:
            options |= {"record_video_dir": str(self.video_dir), "record_video_size": VIEWPORT}
        self.context = browser.new_context(**options)
        self.page = self.context.new_page()
        self.opened = time.monotonic()
        self.start = 0.0
        self.base = base
        for script in (CURSOR_SCRIPT, *init):
            self.page.add_init_script(script)
        self.page.on("pageerror", lambda e: print(f"[{name}] page error: {e}", file=sys.stderr))

    def goto(self, path: str) -> None:
        self.page.goto(f"{self.base}/{path}")
        self.page.wait_for_load_state("networkidle")
        self.page.wait_for_timeout(800)
        self.page.mouse.move(VIEWPORT["width"] * 0.55, VIEWPORT["height"] * 0.45)

    def mark(self) -> None:
        self.start = time.monotonic() - self.opened

    def wait(self, ms: int) -> None:
        self.page.wait_for_timeout(ms)

    def point(self, locator, click: bool = True, steps: int = 18) -> None:
        locator.wait_for(state="attached", timeout=10000)
        with contextlib.suppress(Exception):  # zero-height rows count as hidden; their box is still usable
            locator.scroll_into_view_if_needed(timeout=2000)
        box = locator.bounding_box()
        if not box:
            raise RuntimeError(f"[{self.name}] not visible: {locator}")
        self.page.mouse.move(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2, steps=steps)
        self.wait(220)
        if click:
            self.page.mouse.down()
            self.wait(90)
            self.page.mouse.up()
            self.wait(380)

    def type(self, text: str, delay: int = 70) -> None:
        self.page.keyboard.type(text, delay=delay)

    def still(self, media_id: str, clip: dict | None = None) -> None:
        self.page.screenshot(path=str(OUT / f"{media_id}.jpg"), type="jpeg", quality=86, clip=clip)

    def finish(self, media_id: str | None, ffmpeg: str | None) -> None:
        video = self.page.video
        end = time.monotonic() - self.opened
        self.context.close()
        if media_id and video and ffmpeg:
            encode(ffmpeg, Path(video.path()), OUT / f"{media_id}.mp4", self.start, end - self.start)
        if self.video_dir:
            shutil.rmtree(self.video_dir, ignore_errors=True)


def encode(ffmpeg: str, source: Path, target: Path, start: float, length: float) -> None:
    cmd = [ffmpeg, "-y", "-loglevel", "error", "-ss", f"{max(start, 0):.2f}", "-i", str(source)]
    cmd += ["-t", f"{length:.2f}", "-an", "-vf", "fps=20,scale=1280:-2:flags=lanczos", "-c:v", "libx264"]
    cmd += ["-preset", "slow", "-crf", "30", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(target)]
    subprocess.run(cmd, check=True)


def project_init() -> str:
    raw = json.dumps(json.dumps(DEMO_PROJECT))
    return f"if (!sessionStorage.getItem('doc-seeded')) {{ localStorage.clear(); localStorage.setItem('drakan-editor:project', {raw}); sessionStorage.setItem('doc-seeded', '1'); }}"


def text(page, value: str, exact: bool = True):
    return page.get_by_text(value, exact=exact).locator("visible=true").first


# ---- scenes ----------------------------------------------------------------------------------------------------------


def scene_editor_start(s: Scene) -> None:
    """Start page without a disc: pick a few templates to read what they involve, then the Add menu."""
    s.goto("index.html")
    s.mark()
    s.wait(900)
    s.still("editor-start")
    for title in ("Multiplayer arena", "Cut scene", "Shop and prices", "Quest"):
        s.point(s.page.locator(".sp-tile-t", has_text=title).first, steps=14)
        s.wait(1100)
    s.page.mouse.wheel(0, -2000)
    s.wait(600)
    s.point(s.page.get_by_role("button", name="Add").first)
    s.wait(1300)
    s.still("editor-add-menu")
    s.page.keyboard.press("Escape")
    s.wait(400)


def scene_editor_help(s: Scene) -> None:
    """The help guides from the top bar: open a task guide."""
    s.goto("index.html")
    s.mark()
    s.point(s.page.locator("button:visible", has_text="Help").last)  # the Help button of the top bar
    s.wait(900)
    s.point(s.page.locator('[data-guide="campaign"]').first)
    s.wait(1400)
    s.page.mouse.wheel(0, 300)
    s.wait(900)
    s.still("editor-help")
    s.point(s.page.locator(".hp-back:visible").first)
    s.wait(700)
    s.point(s.page.locator('[data-guide="multiplayer"]').first)
    s.wait(1400)


def scene_editor_palette(s: Scene) -> None:
    """Ctrl+P finds any command."""
    s.goto("index.html")
    s.mark()
    s.page.keyboard.press("Control+p")
    s.wait(500)
    s.type("export")
    s.wait(900)
    s.still("editor-palette")


def scene_editor_project(s: Scene) -> None:
    """Project Settings: name the mod, order the levels, pick the start, switch to the Campaign map."""
    s.goto("index.html")
    s.page.keyboard.press("Control+Comma")
    s.wait(700)
    s.mark()
    author = s.page.get_by_label("Author", exact=True).locator("visible=true").first
    s.point(author)
    s.type("Your name", delay=60)
    s.wait(300)
    description = s.page.get_by_label("Description", exact=True).locator("visible=true").first
    s.point(description)
    s.type("Rynn takes the old road home.", delay=40)
    s.wait(500)
    s.still("editor-project")
    s.point(text(s.page, "Map"))
    s.wait(1500)
    s.still("editor-campaign-map")
    s.point(text(s.page, "Auto arrange", exact=False))
    s.wait(1500)
    s.point(text(s.page, "List"))
    s.wait(900)


def scene_save_editor(s: Scene, save: Path) -> None:
    """Save Editor: open the synthetic save, change gold and health, look at the pages and the pending list."""
    s.goto("save.html")
    s.mark()
    with s.page.expect_file_chooser() as chooser:
        s.point(s.page.get_by_role("button", name="Open save").first)
    chooser.value.set_files(str(save))
    s.wait(1500)
    gold = text(s.page, "Gold").locator("xpath=ancestor::*[.//input][1]").locator("input").first
    s.point(gold)
    gold.press("Control+a")
    s.type("5000")
    gold.press("Tab")
    s.wait(700)
    s.point(text(s.page, "Heal to full", exact=False))
    s.wait(1000)
    s.still("save-editor-tour")
    for page in ("Inventory", "Story", "World"):
        s.point(text(s.page, page))
        s.wait(1200)
        if page == "Inventory":
            s.still("save-editor-inventory")
    s.point(text(s.page, "Character"))
    s.wait(900)


SCENES = {
    "editor-start": (scene_editor_start, True, ()),
    "editor-help": (scene_editor_help, True, ()),
    "editor-palette": (scene_editor_palette, False, ()),
    "editor-project": (scene_editor_project, True, ("project",)),
    "save-editor-tour": (scene_save_editor, True, ("nofs",)),
}


def manifest() -> None:
    from PIL import Image

    rows = []
    for path in sorted(OUT.iterdir()):
        if path.suffix not in (".jpg", ".mp4"):
            continue
        row = {"file": path.name, "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.suffix == ".jpg":
            with Image.open(path) as image:
                row["width"], row["height"] = image.size
        rows.append(row)
    (OUT / "manifest.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8", newline="\n")
    files = " ".join(f"build/site-media/editor/out/{r['file']}" for r in rows)
    table = "\n".join(f"| `{r['file']}` | {r['bytes'] // 1024} KB | `{r['sha256']}` |" for r in rows)
    (OUT / "UPLOAD.md").write_text(
        f"# Editor documentation media ({RELEASE})\n\n"
        "UI only, recorded without a disc by `scripts/buildtool/capture_editor_media.py`. Review each file first.\n\n"
        f"| File | Size | SHA-256 |\n| --- | --- | --- |\n{table}\n\n"
        "```powershell\n"
        f'gh release create {RELEASE} --repo ChristopherVR/Drakan2PC --title "Site media 2026-11-02" '
        '--notes "Editor documentation captures (UI only, no game content)." --prerelease --latest=false\n'
        f"gh release upload {RELEASE} --repo ChristopherVR/Drakan2PC --clobber {files}\n"
        "```\n\n"
        "Then check that `docs/site/media.json` has the same SHA-256 values, delete the `pending` flags of the\n"
        "`editor-*` and `save-editor-*` entries, and rebuild the site.\n",
        encoding="utf-8",
        newline="\n",
    )
    for r in rows:
        print(f"{r['file']:32} {r['bytes'] / 1024:8.0f} KB  {r.get('width', '')}x{r.get('height', '')}  {r['sha256']}")


def update_media_json() -> None:
    """Copy the new hashes and sizes into the docs/site/media.json entries that point at this release's files."""
    from PIL import Image

    path = ROOT / "docs" / "site" / "media.json"
    media = json.loads(path.read_text(encoding="utf-8"))
    prefix = f"/releases/download/{RELEASE}/"
    changed = []
    for key, item in media.items():
        for url_key, hash_key in (("url", "sha256"), ("video_url", "video_sha256")):
            url = item.get(url_key, "")
            if prefix not in url:
                continue
            file = OUT / url.rsplit("/", 1)[1]
            if not file.is_file():
                print(f"missing capture for {key}: {file.name}", file=sys.stderr)
                continue
            item[hash_key] = hashlib.sha256(file.read_bytes()).hexdigest()
            if url_key == "url":
                with Image.open(file) as image:
                    item["width"], item["height"] = image.size
            changed.append(key)
    path.write_text(json.dumps(media, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"media.json: updated {len(set(changed))} entries; they stay pending until the release upload")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--scene", action="append", choices=sorted(SCENES), help="record only this scene (repeatable)")
    parser.add_argument("--skip-build", action="store_true", help="reuse build/site-media/editor/app")
    parser.add_argument(
        "--ffmpeg", default=shutil.which("ffmpeg"), help="FFmpeg executable (clips are skipped without)"
    )
    parser.add_argument("--update-media-json", action="store_true", help="write the new SHA-256 values into media.json")
    parser.add_argument("--headed", action="store_true", help="show the browser while recording")
    args = parser.parse_args()

    from playwright.sync_api import sync_playwright

    if not args.ffmpeg:
        print("FFmpeg not found: recording stills only.", file=sys.stderr)
    if not args.skip_build or not (APP / "index.html").is_file():
        build_editor()
    save = build_save()
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.scene:  # a full run replaces the whole set, so no stale capture is listed for upload
        for old in [*OUT.glob("*.jpg"), *OUT.glob("*.mp4")]:
            old.unlink()
    init_scripts = {"project": project_init(), "nofs": NO_FS_ACCESS}
    with serve(APP) as base, sync_playwright() as pw:
        try:
            browser = pw.chromium.launch(channel="chrome", headless=not args.headed)
        except Exception:  # no installed Chrome: use Playwright's own Chromium
            browser = pw.chromium.launch(headless=not args.headed)
        for name in args.scene or SCENES:
            fn, video, needs = SCENES[name]
            scene = Scene(browser, base, name, video, tuple(init_scripts[n] for n in needs))
            print(f"scene {name}", flush=True)
            try:
                fn(scene, save) if name.startswith("save-") else fn(scene)
            except Exception:
                scene.page.screenshot(path=str(WORK / f"failed-{name}.png"))
                print(f"scene {name} failed; last frame: {WORK / f'failed-{name}.png'}", file=sys.stderr)
                scene.finish(None, None)
                raise
            scene.finish(name if video else None, args.ffmpeg)
        browser.close()
    manifest()
    if args.update_media_json:
        update_media_json()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
