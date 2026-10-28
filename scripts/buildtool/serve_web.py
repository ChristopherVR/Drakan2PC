"""Serve a private browser build and bounded reads of its user-supplied ISO on loopback."""

import argparse
import json
import os
import re
import webbrowser
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import ClassVar
from urllib.parse import urlsplit

MAX_RANGE = 1024 * 1024


def disc_range(value, size):
    match = re.fullmatch(r"bytes=(\d+)-(\d*)", value or "")
    if not match:
        raise ValueError("A single explicit byte range is required")
    start = int(match[1])
    end = min(int(match[2]) if match[2] else start + MAX_RANGE - 1, size - 1)
    if start >= size or end < start or end - start + 1 > MAX_RANGE:
        raise ValueError("Invalid or oversized disc range")
    return start, end


class Handler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    extensions_map: ClassVar = {**SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def allowed_host(self):
        port = self.server.server_port
        allowed = {f"127.0.0.1:{port}", f"localhost:{port}"}
        origin = self.headers.get("Origin")
        return self.headers.get("Host") in allowed and (not origin or origin in {f"http://{h}" for h in allowed})

    def do_HEAD(self):
        self.get(head=True)

    def do_GET(self):
        self.get(head=False)

    def get(self, head):
        if not self.allowed_host():
            self.send_error(403, "Loopback requests only")
            return
        if urlsplit(self.path).path == "/texture-pack.json":
            directory = getattr(self.server, "texture_path", Path(self.directory) / "textures")
            files = sorted(
                path.name
                for path in directory.glob("*")
                if path.is_file() and re.fullmatch(r"[0-9a-fA-F]{16}\.(?:png|dds)", path.name, re.IGNORECASE)
            )
            metadata = {}
            for name in ("alias.txt", "exclude.txt"):
                path = directory / name
                if path.is_file() and path.stat().st_size <= 8 * 1024 * 1024:
                    metadata[name] = path.read_text(encoding="utf-8")
            data = json.dumps({"files": files, "metadata": metadata}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if not head:
                self.wfile.write(data)
            return
        image = re.fullmatch(r"/textures/([0-9a-fA-F]{16}\.(?:png|dds))", urlsplit(self.path).path, re.IGNORECASE)
        if image:
            directory = getattr(self.server, "texture_path", Path(self.directory) / "textures")
            path = directory / image[1]
            if not path.is_file():
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(path.stat().st_size))
            self.end_headers()
            if not head:
                with path.open("rb") as source:
                    self.copyfile(source, self.wfile)
            return
        if urlsplit(self.path).path != "/disc.iso":
            if head:
                super().do_HEAD()
            else:
                super().do_GET()
            return
        size = self.server.iso_path.stat().st_size
        if head:
            self.send_response(200)
            self.send_header("Content-Length", str(size))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            return
        try:
            start, end = disc_range(self.headers.get("Range"), size)
        except ValueError:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        with self.server.iso_path.open("rb") as source:
            source.seek(start)
            data = source.read(end - start + 1)
        self.send_response(206)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def list_directory(self, path):
        self.send_error(404)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--textures", type=Path, help="Optional existing shared HD pack")
    parser.add_argument("--port", type=int, default=8715)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    directory = args.directory.resolve(strict=True)
    config = {}
    if args.iso:
        iso = args.iso
    else:
        config = json.loads((directory / "web-build.json").read_text(encoding="utf-8-sig"))
        iso = Path(config.get("iso_windows", config["iso"]) if os.name == "nt" else config["iso"])
    iso = iso.resolve(strict=True)
    with ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(directory))) as server:
        server.iso_path = iso
        texture_path = args.textures or config.get("textures_windows" if os.name == "nt" else "textures")
        if texture_path:
            server.texture_path = Path(texture_path).resolve()
        url = f"http://127.0.0.1:{server.server_port}/"
        print(f"Private browser build: {url}\nThe ISO stays on this computer. Press Ctrl+C to stop.", flush=True)
        if args.open:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
