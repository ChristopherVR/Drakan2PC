"""Build the public, source-only GitHub Pages site. No ISO or game build is needed."""

from __future__ import annotations

import argparse
import html
import json
import posixpath
import re
import shutil
from html.parser import HTMLParser
from pathlib import Path
from string import Template
from urllib.parse import quote, unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "docs" / "site"
REPOSITORY = "https://github.com/ChristopherVR/Drakan2PC"
PLAYER_FILES = ("loader.js", "handles.js", "sw.js", "loader.css")


MEDIA_URL_KEYS = ("url", "animation_url", "video_url", "before_url", "after_url")


def media_urls(item):
    for entry in (item, *item.get("scenes", ())):
        yield from (entry[key] for key in MEDIA_URL_KEYS if key in entry)


def compare_figure(item):
    """Before/after slider. Without JavaScript the first scene shows half original, half HD."""
    size = f'width="{int(item["width"])}" height="{int(item["height"])}"'
    scenes = item["scenes"]
    first = scenes[0]
    tabs = "".join(
        f'<button type="button" class="compare-tab" data-before="{html.escape(s["before_url"], quote=True)}" '
        f'data-after="{html.escape(s["after_url"], quote=True)}" aria-pressed="{str(i == 0).lower()}">'
        f"{html.escape(s['label'])}</button>"
        for i, s in enumerate(scenes)
    )
    label = html.escape(first["label"], quote=True)
    return (
        f'<figure class="compare"><div class="compare-tabs" role="group" aria-label="Scene">{tabs}</div>'
        f'<div class="compare-stage">'
        f'<img class="compare-after" src="{html.escape(first["after_url"], quote=True)}" alt="{label} with HD textures" '
        f'{size} loading="lazy" decoding="async">'
        f'<img class="compare-before" src="{html.escape(first["before_url"], quote=True)}" '
        f'alt="{label} with the original textures" {size} loading="lazy" decoding="async">'
        f'<span class="compare-label compare-label-before" aria-hidden="true">Original</span>'
        f'<span class="compare-label compare-label-after" aria-hidden="true">HD</span>'
        f'<span class="compare-handle" aria-hidden="true"></span>'
        f'<input class="compare-range" type="range" min="0" max="100" value="50" '
        f'aria-label="Original / HD split position"></div>'
        f"<figcaption>{html.escape(item['caption'])}</figcaption></figure>"
    )


def video_figure(item):
    """Short muted clip with its poster; site.js plays it while visible unless reduced motion is requested."""
    poster = html.escape(item["url"], quote=True)
    alt = html.escape(item["alt"], quote=True)
    return (
        f'<figure class="media-figure media-video"><video src="{html.escape(item["video_url"], quote=True)}" '
        f'poster="{poster}" width="{int(item["width"])}" height="{int(item["height"])}" aria-label="{alt}" '
        f'muted loop playsinline preload="none" controls></video>'
        f'<button type="button" class="video-toggle" aria-pressed="false" hidden>Play</button>'
        f"<figcaption>{html.escape(item['caption'])}</figcaption></figure>"
    )


def media_figure(item):
    """Render externally hosted, reviewed captures; never copy game media into Git or Pages artifacts."""
    kind = item.get("kind")
    if kind == "compare":
        return compare_figure(item)
    if kind == "backdrop":
        return (
            f'<img class="backdrop" src="{html.escape(item["url"], quote=True)}" alt="" '
            f'width="{int(item["width"])}" height="{int(item["height"])}" decoding="async">'
        )
    if item.get("video_url"):
        return video_figure(item)
    url = html.escape(item["url"], quote=True)
    alt = html.escape(item["alt"], quote=True)
    caption = html.escape(item["caption"])
    controls = ""
    if item.get("animation_url"):
        animation = html.escape(item["animation_url"], quote=True)
        controls = (
            f'<button type="button" class="animation-toggle" data-animation="{animation}" '
            f'aria-pressed="false" hidden>Play animation</button>'
            f'<noscript><a href="{animation}">View animation</a></noscript>'
        )
    return (
        f'<figure class="media-figure"><a class="media-enlarge" href="{url}" aria-label="Enlarge: {alt}">'
        f'<img src="{url}" alt="{alt}" width="{int(item["width"])}" height="{int(item["height"])}" '
        f'loading="lazy" decoding="async"></a>{controls}<figcaption>{caption}</figcaption></figure>'
    )


def render_media(content, media):
    for key, item in media.items():
        figure = media_figure(item)
        # Markdown renders a lone marker as its own paragraph-free block; drop any wrapper paragraph.
        content = content.replace(f"<p><!-- media:{key} --></p>", figure)
        content = content.replace(f"<!-- media:{key} -->", figure)
        if "url" not in item:
            continue
        pattern = r'<p><img\b[^>]*\bsrc="' + re.escape(html.escape(item["url"], quote=True)) + r'"[^>]*>\s*</p>'
        content = re.sub(pattern, lambda _, replacement=figure: replacement, content)
    if re.search(r"<!--\s*media:", content):
        raise ValueError("Unknown media entry in site content")
    return content


def relative_url(target: str, current: str) -> str:
    return posixpath.relpath(target, posixpath.dirname(current) or ".")


class PageParser(HTMLParser):
    """Rewrite source links, and gather text, IDs and links for search/validation."""

    def __init__(self, rewrite=None):
        super().__init__(convert_charrefs=False)
        self.rewrite = rewrite
        self.output = []
        self.text = []
        self.ids = set()
        self.links = []

    def handle_starttag(self, tag, attrs):
        updated = []
        for name, value in attrs:
            if value is not None and name in ("href", "src"):
                value = self.rewrite(value) if self.rewrite else value
                self.links.append(value)
            if name == "id":
                if value in self.ids:
                    raise ValueError(f"Duplicate HTML id: {value}")
                self.ids.add(value)
            updated.append(name if value is None else f'{name}="{html.escape(value, quote=True)}"')
        suffix = " " + " ".join(updated) if updated else ""
        self.output.append(f"<{tag}{suffix}>")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        self.output.append(f"</{tag}>")

    def handle_data(self, data):
        self.output.append(data)
        self.text.append(data)

    def handle_entityref(self, name):
        self.output.append(f"&{name};")
        self.text.append(html.unescape(f"&{name};"))

    def handle_charref(self, name):
        self.output.append(f"&#{name};")
        self.text.append(html.unescape(f"&#{name};"))

    def handle_comment(self, data):
        self.output.append(f"<!--{data}-->")


def rewrite_link(link: str, source: Path, current: str, published: dict) -> str:
    parts = urlsplit(link)
    if parts.scheme or parts.netloc or not parts.path or parts.path.startswith("/"):
        return link
    target = (source.parent / unquote(parts.path)).resolve()
    if not target.is_relative_to(ROOT):
        raise ValueError(f"Link leaves the repository in {source}: {link}")
    suffix = ("?" + parts.query if parts.query else "") + ("#" + parts.fragment if parts.fragment else "")
    if target in published:
        return relative_url(published[target], current) + suffix
    if not target.exists():
        raise ValueError(f"Missing source link in {source}: {link}")
    kind = "tree" if target.is_dir() else "blob"
    return f"{REPOSITORY}/{kind}/main/{quote(target.relative_to(ROOT).as_posix())}{suffix}"


def navigation(pages, current):
    output = []
    section = None
    for page in pages:
        if page["section"] != section:
            section = page["section"]
            output.append(f"<h2>{html.escape(section)}</h2>")
        active = ' aria-current="page"' if page["url"] == current else ""
        output.append(f'<a href="{relative_url(page["url"], current)}"{active}>{html.escape(page["title"])}</a>')
    return "\n".join(output)


def validate_site(output: Path, base_path: str):
    """Fail before deployment on broken local URLs, fragments or accidental payloads."""
    documents = {}
    for path in output.rglob("*.html"):
        parser = PageParser()
        parser.feed(path.read_text(encoding="utf-8"))
        documents[path.resolve()] = parser
    allowed = {".html", ".css", ".js", ".json", ".svg"}
    for path in output.rglob("*"):
        if path.is_file() and path.name != ".nojekyll" and path.suffix not in allowed:
            raise ValueError(f"Unexpected public artifact: {path}")
    for path, parser in documents.items():
        for link in parser.links:
            parts = urlsplit(link)
            if parts.scheme or parts.netloc:
                continue
            url_path = unquote(parts.path)
            if url_path.startswith("/"):
                if not url_path.startswith(base_path):
                    raise ValueError(f"URL escapes Pages base path: {link}")
                target = output / url_path.removeprefix(base_path)
            else:
                target = path.parent / url_path if url_path else path
            if target.is_dir():
                target /= "index.html"
            target = target.resolve()
            if not target.is_relative_to(output.resolve()) or not target.is_file():
                raise ValueError(f"Broken local URL in {path.name}: {link}")
            if parts.fragment and target in documents and unquote(parts.fragment) not in documents[target].ids:
                raise ValueError(f"Broken fragment in {path.name}: {link}")


def build(output: Path, base_path: str = "/Drakan2PC/"):
    import markdown

    output = output.resolve()
    build_root = (ROOT / "build").resolve()
    if not output.is_relative_to(build_root) or output == build_root:
        raise ValueError("Pages output must be a dedicated directory inside build/")
    if not re.fullmatch(r"/(?:[A-Za-z0-9_-]+/)*", base_path):
        raise ValueError("Base path must be / or a slash-delimited repository path, e.g. /Drakan2PC/")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    pages = json.loads((SITE / "pages.json").read_text(encoding="utf-8"))
    media = json.loads((SITE / "media.json").read_text(encoding="utf-8"))
    for item in media.values():
        for url in media_urls(item):
            if not url.startswith(f"{REPOSITORY}/releases/download/site-media-"):
                raise ValueError(f"Media must use the reviewed GitHub release host: {url}")
    published = {(ROOT / page["source"]).resolve(): page["url"] for page in pages}
    published[(ROOT / "runtime/web/pages/index.html").resolve()] = "browser.html"
    template = Template((SITE / "template.html").read_text(encoding="utf-8"))
    search = []

    def write(url, title, description, layout, scripts="", page_class="", base=None):
        destination = output / url
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(
            template.substitute(
                title=html.escape(title),
                description=html.escape(description, quote=True),
                layout=render_media(layout, media),
                scripts=scripts,
                page_class=page_class,
                base=relative_url(".", url) if base is None else base,
            ),
            encoding="utf-8",
            newline="\n",
        )

    for index, page in enumerate(pages):
        source = ROOT / page["source"]
        markdown_text = source.read_text(encoding="utf-8")
        # The page shell supplies the single h1; source Markdown stays readable on GitHub.
        markdown_text = re.sub(r"\A# [^\n]+\n", "", markdown_text, count=1)
        renderer = markdown.Markdown(
            extensions=["tables", "fenced_code", "toc", "sane_lists"], extension_configs={"toc": {"toc_depth": "2-3"}}
        )
        content = render_media(renderer.convert(markdown_text), media)
        parser = PageParser(lambda link, s=source, u=page["url"]: rewrite_link(link, s, u, published))
        parser.feed(content)
        turns = []
        if index:
            previous = pages[index - 1]
            turns.append(
                f'<a href="{relative_url(previous["url"], page["url"])}">← {html.escape(previous["title"])}</a>'
            )
        if index + 1 < len(pages):
            following = pages[index + 1]
            turns.append(
                f'<a href="{relative_url(following["url"], page["url"])}">{html.escape(following["title"])} →</a>'
            )
        layout = f'''<div class="docs-layout">
<button class="doc-menu" type="button" aria-expanded="false" aria-controls="guide-nav">Browse documentation +</button>
<nav class="sidebar" id="guide-nav" aria-label="Documentation">{navigation(pages, page["url"])}</nav>
<main class="doc-main" id="main" tabindex="-1"><p class="doc-section">{html.escape(page["section"])}</p>
<h1>{html.escape(page["title"])}</h1><p class="doc-description">{html.escape(page["description"])}</p>
<article class="prose">{"".join(parser.output)}</article>
<p class="source-link"><a href="{REPOSITORY}/blob/main/{page["source"]}">View this page's source on GitHub ↗</a></p>
<nav class="page-turn" aria-label="Previous and next pages">{"".join(turns)}</nav></main>
<aside class="toc" aria-label="On this page"><h2>On this page</h2>{renderer.toc.replace('class="toc"', 'class="toc-tree"')}</aside></div>'''
        write(page["url"], page["title"], page["description"], layout, page_class="documentation")
        search.append(
            {
                **{key: page[key] for key in ("url", "title", "description")},
                "text": " ".join(" ".join(parser.text).split()),
            }
        )

    write(
        "index.html",
        "The Ancients' Gates, recompiled for PC",
        "User guides, PC enhancements and developer documentation for Drakan2PC. Build from your own PS2 disc.",
        (SITE / "home.html").read_text(encoding="utf-8"),
        page_class="home",
    )
    player = (ROOT / "runtime/web/pages/index.html").read_text(encoding="utf-8")
    player_body = re.search(r"<main>(.*?)</main>", player, re.DOTALL).group(1)
    write(
        "browser.html",
        "Browser player",
        "Play your own local Drakan browser build and disc image.",
        '<main id="main" tabindex="-1" class="browser-layout">'
        + player_body
        + '<p><a href="guides/browser.html">Browser setup guide</a> · '
        '<a href="guides/saves.html">How browser saves work</a></p></main>',
        scripts='<script src="handles.js"></script><script src="loader.js"></script>',
    )
    search.append(
        {
            "url": "browser.html",
            "title": "Browser player",
            "description": "Choose your local build and ISO.",
            "text": "Play browser launcher local folder ISO Chrome Edge",
        }
    )
    # Keep the worker, virtual play/ URL and IndexedDB names at their original locations.
    for name in PLAYER_FILES:
        shutil.copyfile(ROOT / "runtime/web/pages" / name, output / name)
    shutil.copytree(SITE / "assets", output / "assets")
    write(
        "404.html",
        "Page not found",
        "Find your way back to the Drakan2PC guides.",
        f'<main id="main" class="browser-layout" tabindex="-1"><p class="doc-section">Error 404</p>'
        f"<h1>Page not found</h1><p>This page may have moved. Browse the guides or use search above.</p>"
        f'<a class="button primary" href="{base_path}index.html">Return to the guide</a></main>',
        base=base_path.rstrip("/"),
    )
    (output / "search-index.json").write_text(json.dumps(search, ensure_ascii=False), encoding="utf-8", newline="\n")
    (output / ".nojekyll").touch()
    validate_site(output, base_path)
    print(f"Built {len(pages) + 3} pages; local links and fragments verified: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-path", default="/Drakan2PC/", help="GitHub Pages path, used by the 404 page")
    args = parser.parse_args()
    build(ROOT / "build/github-pages", args.base_path)


if __name__ == "__main__":
    main()
