"""Extract the Drakan: Order of the Flame (PC) game data from its retail disc image into build/ootf/cab_disc.

    python analysis/scripts/ootf_cab.py "C:/.../Drakan.zip"          # the zip holding DRAKAN.mdx
    python analysis/scripts/ootf_cab.py DRAKAN.mdx --out build/ootf/cab_disc

The disc image (`.mdx`) is a raw 2048-byte-sector ISO9660 volume that starts at file offset 0x40. `SETUP/DATA1.CAB` on it is an
InstallShield 5 cabinet (signature `ISc(`) that 7-Zip cannot open; this reads it directly. Layout, in our own words
(docs/journal/2026-10-31/ootf-data-study.md section 1.1): the common header holds the descriptor offset (u32 at +0x0c) and
size; in the descriptor the file table offset is at +0x0c, the table size at +0x14, the directory count at +0x1c and the file
count at +0x28. The file table is `dirs + files` u32 offsets (relative to the table) followed by NUL-terminated directory names
and 42-byte file descriptors: name offset u32, directory index u32, flags u16 (0x04 compressed, 0x08 unused), expanded size u32
at +0x0a, compressed size u32 at +0x0e, absolute data offset u32 at +0x26. Compressed data is `{u16 length, raw deflate}` blocks.
Everything in the cabinet is extracted (the converter reads the sound and animation databases and the wave files too).
"""

from __future__ import annotations

import argparse
import struct
import sys
import zipfile
import zlib
from pathlib import Path, PureWindowsPath

ROOT = Path(__file__).resolve().parents[2]
MAX_FILE_SIZE = 512 * 1024 * 1024


def checked_range(data: bytes, off: int, size: int) -> None:
    if off < 0 or size < 0 or off + size > len(data):
        raise ValueError(f"range outside input: offset {off}, size {size}")


def iso_find(img: bytes, base: int, path: str) -> tuple[int, int]:
    """(offset, size) of a file on the ISO9660 volume that starts at `base`."""
    pvd = base + 16 * 2048
    checked_range(img, pvd, 2048)
    if img[pvd : pvd + 7] != b"\x01CD001\x01":
        raise ValueError("no supported ISO9660 primary volume")
    root_lba = struct.unpack_from("<I", img, pvd + 156 + 2)[0]
    root_len = struct.unpack_from("<I", img, pvd + 156 + 10)[0]
    lba, length = root_lba, root_len
    parts = [p for p in path.upper().split("/") if p]
    for part in parts:
        off = base + lba * 2048
        checked_range(img, off, length)
        end = off + length
        found = None
        while off < end:
            rec_len = img[off]
            if rec_len == 0:
                off = base + ((off - base) // 2048 + 1) * 2048
                continue
            if rec_len < 34 or off + rec_len > end or (off - base) % 2048 + rec_len > 2048:
                raise ValueError("invalid ISO directory record")
            name_len = img[off + 32]
            if 33 + name_len > rec_len:
                raise ValueError("ISO name outside directory record")
            name = img[off + 33 : off + 33 + name_len].decode("latin-1").upper().split(";")[0]
            if name == part:
                found = (struct.unpack_from("<I", img, off + 2)[0], struct.unpack_from("<I", img, off + 10)[0])
                break
            off += rec_len
        if not found:
            raise FileNotFoundError(path)
        lba, length = found
    checked_range(img, base + lba * 2048, length)
    return base + lba * 2048, length


def read_cab(data: bytes) -> list[tuple[str, str, int, int, int, int]]:
    checked_range(data, 0, 16)
    if data[:4] != b"ISc(":
        raise ValueError("not an InstallShield cabinet")
    desc_off = struct.unpack_from("<I", data, 0x0C)[0]
    d = desc_off
    checked_range(data, d, 0x2C)
    table_off = struct.unpack_from("<I", data, d + 0x0C)[0]
    table_size = struct.unpack_from("<I", data, d + 0x14)[0]
    ndirs = struct.unpack_from("<I", data, d + 0x1C)[0]
    nfiles = struct.unpack_from("<I", data, d + 0x28)[0]
    table = d + table_off
    checked_range(data, table, table_size)
    if (ndirs + nfiles) * 4 > table_size:
        raise ValueError("cabinet offset table exceeds file table")
    table_end = table + table_size
    offs = struct.unpack_from(f"<{ndirs + nfiles}I", data, table)

    def cstr(o: int) -> str:
        if not table <= o < table_end:
            raise ValueError("cabinet name outside file table")
        e = data.find(b"\0", o, table_end)
        if e < 0:
            raise ValueError("unterminated cabinet name")
        return data[o:e].decode("latin-1")

    dirs = [cstr(table + offs[i]) for i in range(ndirs)]
    out = []
    for i in range(nfiles):
        fd = table + offs[ndirs + i]
        if fd < table or fd + 42 > table_end:
            raise ValueError("cabinet descriptor outside file table")
        name_off, dir_idx, flags = struct.unpack_from("<IIH", data, fd)
        if flags & 0x08:  # unused placeholder
            continue
        expanded, compressed = struct.unpack_from("<II", data, fd + 0x0A)
        if expanded > MAX_FILE_SIZE:
            raise ValueError("cabinet file exceeds extraction size limit")
        if dir_idx >= ndirs:
            raise ValueError("invalid cabinet directory index")
        data_off = struct.unpack_from("<I", data, fd + 0x26)[0]
        checked_range(data, data_off, compressed)
        out.append((dirs[dir_idx], cstr(table + name_off), flags, expanded, compressed, data_off))
    return out


def inflate_blocks(data: bytes, off: int, comp: int, expanded: int) -> bytes:
    checked_range(data, off, comp)
    if not 0 <= expanded <= MAX_FILE_SIZE:
        raise ValueError("invalid expanded size")
    out = bytearray()
    p, end = off, off + comp
    while p < end:
        if p + 2 > end:
            raise ValueError("truncated compressed block header")
        n = struct.unpack_from("<H", data, p)[0]
        p += 2
        if n == 0 or p + n > end:
            raise ValueError("compressed block outside file data")
        decoder = zlib.decompressobj(-15)
        out += decoder.decompress(data[p : p + n], expanded - len(out) + 1)
        if len(out) > expanded or not decoder.eof or decoder.unused_data:
            raise ValueError("invalid compressed block or expanded size")
        p += n
    if len(out) != expanded:
        raise ValueError("expanded size does not match cabinet descriptor")
    return bytes(out)


def output_path(root: Path, dirname: str, name: str) -> Path:
    """Validate Windows cabinet names on every host, including existing symlink escapes."""
    path = PureWindowsPath(dirname) / PureWindowsPath(name)
    if not name or path.drive or path.root or any(p in (".", "..") or ":" in p for p in path.parts):
        raise ValueError("unsafe cabinet output path")
    if any(p.endswith((".", " ")) or PureWindowsPath(p).is_reserved() for p in path.parts):
        raise ValueError("unsafe Windows output name")
    dest = root.joinpath(*path.parts)
    if not dest.resolve().is_relative_to(root.resolve()):
        raise ValueError("cabinet output escapes destination")
    return dest


def extract_cab(cab: bytes, root: Path) -> int:
    entries = read_cab(cab)
    paths = [output_path(root, dirname, name) for dirname, name, *_ in entries]
    if len({str(p).casefold() for p in paths}) != len(paths):
        raise ValueError("duplicate cabinet output paths")
    for dest, (_, _, flags, expanded, compressed, doff) in zip(paths, entries, strict=True):
        raw = cab[doff : doff + compressed]
        body = inflate_blocks(cab, doff, compressed, expanded) if flags & 0x04 else raw
        if len(body) != expanded:
            raise ValueError(f"expanded size mismatch for {dest.name}")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(body)
    return len(entries)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path, help="DRAKAN.mdx, or the zip that holds it")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "ootf" / "cab_disc")
    a = ap.parse_args()
    if a.image.suffix.lower() == ".zip":
        with zipfile.ZipFile(a.image) as z:
            names = [n for n in z.namelist() if n.lower().endswith((".mdx", ".iso"))]
            if len(names) != 1:
                raise ValueError("zip must contain exactly one MDX or ISO image")
            name = names[0]
            img = z.read(name)
    else:
        img = a.image.read_bytes()
    base = 0 if img[16 * 2048 + 1 : 16 * 2048 + 6] == b"CD001" else 0x40
    off, size = iso_find(img, base, "SETUP/DATA1.CAB")
    cab = img[off : off + size]
    n = extract_cab(cab, a.out)
    print(f"extracted {n} files to {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
