"""Real loopback HTTP tests with a synthetic disc; no game data required."""

import importlib.util
import json
import tempfile
import threading
import unittest
from functools import partial
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("serve_web", ROOT / "scripts/buildtool/serve_web.py")
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)


class QuietHandler(web.Handler):
    def log_message(self, *args):
        pass


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        public = root / "player"
        public.mkdir()
        self.public = public
        (public / "index.html").write_text("synthetic player", encoding="utf-8")
        self.iso = root / "private.iso"
        self.data = bytes(range(256)) * 8193
        self.iso.write_bytes(self.data)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(public)))
        self.server.iso_path = self.iso
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.close)

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, path="/disc.iso", headers=None, method="GET"):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.request(method, path, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_ranges_return_exact_bytes_and_isolation_headers(self):
        for start, end in [
            (0, 2047),
            (1024 * 1024 - 100, 1024 * 1024 + 100),
            (len(self.data) - 100, len(self.data) + 100),
        ]:
            status, headers, body = self.request(headers={"Range": f"bytes={start}-{end}"})
            self.assertEqual(status, 206)
            self.assertEqual(body, self.data[start : end + 1])
            self.assertEqual(headers["Content-Length"], str(len(body)))
            self.assertEqual(headers["Cross-Origin-Opener-Policy"], "same-origin")
            self.assertEqual(headers["Cross-Origin-Embedder-Policy"], "require-corp")

    def test_persistent_connection_can_read_many_ranges(self):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            for index in range(40):
                start = index * 100
                connection.request("GET", "/disc.iso", headers={"Range": f"bytes={start}-{start + 2047}"})
                response = connection.getresponse()
                self.assertEqual(response.version, 11)
                self.assertEqual(response.status, 206)
                self.assertEqual(response.read(), self.data[start : start + 2048])
                self.assertIsNotNone(connection.sock)
        finally:
            connection.close()

    def test_full_disc_invalid_and_oversized_requests_are_rejected(self):
        for value in [None, "bytes=-100", "bytes=0-1048576", "bytes=1-0", "bytes=99999999-", "bytes=0-1,4-5"]:
            headers = {"Range": value} if value else {}
            status, _, body = self.request(headers=headers)
            self.assertEqual(status, 416)
            self.assertEqual(body, b"")

    def test_foreign_origins_and_dns_rebinding_are_rejected(self):
        for headers in [{"Host": "attacker.example"}, {"Origin": "https://attacker.example"}]:
            self.assertEqual(self.request("/", headers)[0], 403)

    def test_static_files_and_disc_head(self):
        self.assertEqual(self.request("/")[2], b"synthetic player")
        self.assertEqual(self.request("/../private.iso")[0], 404)
        status, headers, body = self.request(method="HEAD")
        self.assertEqual(status, 200)
        self.assertEqual(int(headers["Content-Length"]), len(self.data))
        self.assertEqual(body, b"")

    def test_texture_index_only_names_pack_images_and_metadata(self):
        textures = self.public / "textures"
        textures.mkdir()
        image = "0123456789abcdef.png"
        (textures / image).write_bytes(b"synthetic image")
        (textures / "unrelated.txt").write_text("private notes")
        (textures / "alias.txt").write_text("0000000000000001 0123456789abcdef\n")
        status, _, body = self.request("/texture-pack.json")
        self.assertEqual(status, 200)
        index = json.loads(body)
        self.assertEqual(index["files"], [image])
        self.assertEqual(index["metadata"], {"alias.txt": "0000000000000001 0123456789abcdef\n"})
        self.assertEqual(self.request("/textures/" + image)[2], b"synthetic image")
        self.assertEqual(self.request("/texture-pack.json", {"Origin": "https://attacker.example"})[0], 403)

    def test_missing_texture_pack_is_an_empty_index(self):
        self.assertEqual(json.loads(self.request("/texture-pack.json")[2]), {"files": [], "metadata": {}})

    def test_existing_external_pack_streams_without_copying(self):
        pack = self.public.parent / "shared-pack"
        pack.mkdir()
        name = "0123456789abcdef.dds"
        (pack / name).write_bytes(b"shared image bytes")
        self.server.texture_path = pack
        self.assertEqual(json.loads(self.request("/texture-pack.json")[2])["files"], [name])
        self.assertEqual(self.request("/textures/" + name)[2], b"shared image bytes")
        self.assertEqual(self.request("/textures/../private.iso")[0], 404)


if __name__ == "__main__":
    unittest.main()
