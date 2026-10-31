"""Inspect and edit native Drakan save files (.rsg, SRSC containers).

An .rsg is an SRSC container: a 16-byte header ('SRSC', u16 version, u32 index offset, u32 count, u16 pad),
zlib-compressed payloads laid out in file order with optional gap bytes between them, and a 14-byte-per-entry
index that runs to EOF. There is no checksum; each payload's zlib Adler-32 is the only integrity check.
Layout and field evidence: docs/journal/2026-10-31/save-editor.md.

Commands:
  info   SAVE                              entries table
  get    SAVE FIELD [--force]              print one field
  set    SAVE FIELD VALUE (-o OUT | --in-place) [--force]
  dump   SAVE DIR                          inflated entries, raw entries and manifest.json
  pack   DIR -o SAVE                       rebuild a save from a dump
  fields                                   list the editable fields with confidence notes

Writes never replace a file silently: an existing output (or the input for --in-place) is first kept as
NAME.bak, or NAME.bak.1, NAME.bak.2, ... when those exist. Stdlib only; Python 3.11+.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"SRSC"
HEADER = struct.Struct("<4sHIIH")
INDEX = struct.Struct("<HHHII")
HEADER_SIZE = 16
INDEX_SIZE = 14
MANIFEST = "manifest.json"
DUMP_FORMAT = "savetool-dump"
DUMP_FORMAT_VERSION = 1
ENTRY_FILE = re.compile(r"e\d{4}\.(bin|z)")

TYPE_RECORD = 0x102
TYPE_SKILLS = 0x401
SKILL_TABLE_ID = 893
SKILL_ROW_SIZE = 0x4C
SKILL_NAME_OFFSET = 12
RYNN_CLASS = 0x17081069
GOLD_CLASS = 0x19001255
CLASS_OFFSET = 8


class SaveToolError(Exception):
    """A user-facing error: bad input, unknown field, or a layout the tool does not recognise."""


class SaveFormatError(SaveToolError):
    """The file is not a well-formed SRSC container with zlib payloads."""


@dataclass
class Entry:
    index: int  # position in the index table
    type: int
    id: int
    sub: int
    position: int  # rank by file offset; payloads are written in this order
    gap: bytes  # bytes between the previous payload end (or header) and this payload
    raw: bytes  # the zlib stream as stored in the file
    data: bytes  # inflated payload


@dataclass
class Save:
    version: int
    pad: int
    entries: list[Entry]
    tail: bytes  # bytes between the last payload and the index table


# ---------------------------------------------------------------- container


def inflate(raw: bytes, label: str) -> bytes:
    stream = zlib.decompressobj()
    try:
        data = stream.decompress(raw) + stream.flush()
    except zlib.error as exc:
        raise SaveFormatError(f"{label}: payload is not a zlib stream ({exc})") from exc
    if not stream.eof or stream.unused_data:
        raise SaveFormatError(f"{label}: zlib stream is incomplete or has trailing bytes")
    return data


def parse(blob: bytes) -> Save:
    if len(blob) < HEADER_SIZE:
        raise SaveFormatError("file is shorter than the 16-byte SRSC header")
    magic, version, index_off, count, pad = HEADER.unpack_from(blob, 0)
    if magic != MAGIC:
        raise SaveFormatError(f"bad magic {magic!r}, expected b'SRSC'")
    if index_off < HEADER_SIZE or index_off + count * INDEX_SIZE != len(blob):
        raise SaveFormatError("index table does not run to EOF (index offset or count mismatch)")
    rows = [INDEX.unpack_from(blob, index_off + INDEX_SIZE * i) for i in range(count)]
    built: dict[int, Entry] = {}
    cursor = HEADER_SIZE
    for position, i in enumerate(sorted(range(count), key=lambda k: rows[k][3])):
        typ, entry_id, sub, off, size = rows[i]
        if size == 0 or off < cursor or off + size > index_off:
            raise SaveFormatError(f"entry {i} (type 0x{typ:x} id {entry_id}) has a bad range {off:#x}+{size}")
        raw = blob[off : off + size]
        built[i] = Entry(i, typ, entry_id, sub, position, blob[cursor:off], raw, inflate(raw, f"entry {i}"))
        cursor = off + size
    return Save(version, pad, [built[i] for i in range(count)], blob[cursor:index_off])


def assemble(save: Save) -> tuple[bytes, list[int]]:
    """Serialise a Save. Returns the file bytes and each entry's payload offset, in index order."""
    body = bytearray(HEADER_SIZE)
    offsets = [0] * len(save.entries)
    for entry in sorted(save.entries, key=lambda e: e.position):
        body += entry.gap
        offsets[entry.index] = len(body)
        body += entry.raw
    body += save.tail
    index_off = len(body)
    for entry, off in zip(save.entries, offsets, strict=True):
        body += INDEX.pack(entry.type, entry.id, entry.sub, off, len(entry.raw))
    HEADER.pack_into(body, 0, MAGIC, save.version, index_off, len(save.entries), save.pad)
    return bytes(body), offsets


def set_payload(entry: Entry, data: bytes) -> None:
    entry.data = data
    entry.raw = zlib.compress(data)


# ---------------------------------------------------------------- fields


@dataclass(frozen=True)
class RecordLocator:
    """A 0x102 object record, found by the u32 class id at +8. entry_id pins the index id where the layout is verified."""

    class_id: int
    entry_id: int | None = None


@dataclass(frozen=True)
class SkillTableLocator:
    """Rows of the skill table (type 0x401, id 893). Rows are keyed by their stored id (1 Melee, 2 Archery, 3 Magic)."""


@dataclass(frozen=True)
class Field:
    locator: RecordLocator | SkillTableLocator
    offset: int  # byte offset inside the record (or skill row)
    kind: str  # "u32" or "vec3" (three float32)
    confidence: str  # "high", "medium" or "low"
    note: str
    minimum: int | None = None
    bound: tuple[str, str] | None = None  # (sibling field, "<=" or ">="), checked within the same skill row


# One table for every editable field. Confidence is the evidence level from save-editor.md, re-checked
# against the local saves: high = verified by an in-game fixture or by matching structure in every sample;
# medium = one or two distinct values seen, no controlled edit yet; low = a guess.
FIELDS: dict[str, Field] = {
    "hp": Field(
        RecordLocator(RYNN_CLASS, entry_id=56),
        0x708,
        "u32",
        "medium",
        "Rynn record, u32 at +0x708. Pinned to entry id 56: every such save holds 84 (the RAM HP of the same "
        "session), while entry id 53 saves hold other data here, so --force is needed for them. Max HP not located. "
        "Only one distinct value seen; not yet confirmed by a controlled edit.",
    ),
    "gold": Field(
        RecordLocator(GOLD_CLASS, entry_id=63),
        0x448,
        "u32",
        "medium",
        "Record class 0x19001255 entry id 63, u32 at +0x448. Holds 376 in 305 of the 403 local saves that carry it; "
        "the other 98 hold other values (e.g. 3600193170), so this record may have a second layout. "
        "Confirm with a controlled edit before trusting.",
        minimum=0,
    ),
    "pos": Field(
        RecordLocator(RYNN_CLASS),
        12,
        "vec3",
        "high",
        "Rynn record, float X,Y,Z at +12. Matched by class alone because entry ids vary between saves (53, 56, ...). "
        "The same offsets combat_save_fixture.py moves, verified in game.",
    ),
    "skill": Field(
        SkillTableLocator(),
        4,
        "u32",
        "high",
        "Level of skill row N (skill.N). Row = {u32 id, u32 level, u32 max, char name[64]}, stride 0x4c. The table "
        "starts where the name 'Melee' sits minus 12; the u32 row count is 4 bytes before the first row, not at a fixed "
        "offset. Level must not exceed max.",
        bound=("skill.max", "<="),
    ),
    "skill.max": Field(
        SkillTableLocator(),
        8,
        "u32",
        "high",
        "Max of skill row N (skill.N.max). Level must not be below the current level.",
        bound=("skill", ">="),
    ),
}

_SKILL_FIELD = re.compile(r"skill\.(\d+)(\.max)?")


def resolve_field(name: str) -> tuple[str, int | None]:
    """Map a user field name to a FIELDS key and, for skill rows, the row id."""
    match = _SKILL_FIELD.fullmatch(name)
    if match:
        return ("skill.max" if match.group(2) else "skill"), int(match.group(1))
    if name in FIELDS and isinstance(FIELDS[name].locator, RecordLocator):
        return name, None
    raise SaveToolError(f"unknown field {name!r}; run `savetool fields` (skills are skill.N or skill.N.max)")


def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def _find_record(save: Save, loc: RecordLocator, force: bool) -> Entry:
    candidates = [
        e
        for e in save.entries
        if e.type == TYPE_RECORD and len(e.data) >= CLASS_OFFSET + 4 and _u32(e.data, CLASS_OFFSET) == loc.class_id
    ]
    if loc.entry_id is not None and not force:
        pinned = [e for e in candidates if e.id == loc.entry_id]
        if candidates and not pinned:
            ids = sorted({e.id for e in candidates})
            raise SaveToolError(
                f"record class 0x{loc.class_id:08x} has entry id {ids}; this field is verified only for id "
                f"{loc.entry_id} (pass --force to use it anyway)"
            )
        candidates = pinned
    if len(candidates) != 1:
        raise SaveToolError(f"expected one record for class 0x{loc.class_id:08x}, found {len(candidates)}")
    return candidates[0]


def _skill_table(save: Save) -> tuple[Entry, int, int]:
    """Return (entry, offset of the first row, row count)."""
    tables = [e for e in save.entries if e.type == TYPE_SKILLS and e.id == SKILL_TABLE_ID]
    if len(tables) != 1:
        raise SaveToolError(f"expected one skill table (type 0x401 id {SKILL_TABLE_ID}), found {len(tables)}")
    data = tables[0].data
    anchor = data.find(b"Melee\x00")
    if anchor < SKILL_NAME_OFFSET + 4 or data.find(b"Melee\x00", anchor + 1) >= 0:
        raise SaveToolError("skill table layout not recognised (no unique 'Melee' row)")
    first = anchor - SKILL_NAME_OFFSET
    count = _u32(data, first - 4)
    if not 1 <= count <= 64 or first + count * SKILL_ROW_SIZE > len(data):
        raise SaveToolError(f"skill table row count {count} does not fit the payload")
    return tables[0], first, count


def locate(save: Save, field: Field, row_id: int | None, force: bool) -> tuple[Entry, int]:
    """Return (entry, base offset) for a field: the record start, or the start of the matching skill row."""
    if isinstance(field.locator, RecordLocator):
        return _find_record(save, field.locator, force), 0
    if row_id is None:
        raise SaveToolError("skill fields need a row number, e.g. skill.1")
    entry, first, count = _skill_table(save)
    rows = [first + k * SKILL_ROW_SIZE for k in range(count) if _u32(entry.data, first + k * SKILL_ROW_SIZE) == row_id]
    if len(rows) != 1:
        raise SaveToolError(f"no unique skill row with id {row_id} (table ids are 1..{count})")
    return entry, rows[0]


def _check_span(entry: Entry, off: int, width: int, name: str) -> None:
    if off + width > len(entry.data):
        raise SaveToolError(f"{name} is outside its payload ({len(entry.data)} bytes)")


def decode(kind: str, data: bytes, off: int) -> int | tuple[float, ...]:
    if kind == "u32":
        return _u32(data, off)
    return struct.unpack_from("<3f", data, off)


def format_value(kind: str, value: int | tuple[float, ...]) -> str:
    if kind == "u32":
        return str(value)
    return ",".join(f"{x:.9g}" for x in value)


def parse_value(kind: str, text: str) -> int | tuple[float, ...]:
    if kind == "u32":
        try:
            value = int(text, 0)
        except ValueError as exc:
            raise SaveToolError(f"expected an integer, got {text!r}") from exc
        if not 0 <= value <= 0xFFFFFFFF:
            raise SaveToolError(f"{value} does not fit in u32")
        return value
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 3:
        raise SaveToolError("expected X,Y,Z")
    try:
        values = tuple(float(p) for p in parts)
    except ValueError as exc:
        raise SaveToolError(f"expected three numbers, got {text!r}") from exc
    if not all(math.isfinite(v) for v in values):
        raise SaveToolError("coordinates must be finite")
    try:
        struct.pack("<3f", *values)
    except OverflowError as exc:
        raise SaveToolError("coordinate does not fit in float32") from exc
    return values


def get_field(save: Save, name: str, force: bool = False) -> int | tuple[float, ...]:
    base_name, row_id = resolve_field(name)
    field = FIELDS[base_name]
    entry, base = locate(save, field, row_id, force)
    width = 4 if field.kind == "u32" else 12
    _check_span(entry, base + field.offset, width, name)
    return decode(field.kind, entry.data, base + field.offset)


def set_field(save: Save, name: str, text: str, force: bool = False) -> tuple[object, object]:
    """Edit one field in memory. Returns (before, after). Only that field's bytes change in the payload."""
    base_name, row_id = resolve_field(name)
    field = FIELDS[base_name]
    entry, base = locate(save, field, row_id, force)
    width = 4 if field.kind == "u32" else 12
    off = base + field.offset
    _check_span(entry, off, width, name)
    value = parse_value(field.kind, text)
    if field.minimum is not None and value < field.minimum:
        raise SaveToolError(f"{name} must be at least {field.minimum}")
    if field.bound is not None:
        sibling_name, op = field.bound
        sibling = FIELDS[sibling_name]
        limit = _u32(entry.data, base + sibling.offset)
        if (op == "<=" and value > limit) or (op == ">=" and value < limit):
            raise SaveToolError(f"{name}={value} must be {op} {sibling_name} ({limit})")
    before = decode(field.kind, entry.data, off)
    payload = bytearray(entry.data)
    if field.kind == "u32":
        struct.pack_into("<I", payload, off, value)
    else:
        struct.pack_into("<3f", payload, off, *value)
    set_payload(entry, bytes(payload))
    return before, value


# ---------------------------------------------------------------- files


def _backup_path(path: Path) -> Path:
    candidate = path.with_name(path.name + ".bak")
    n = 0
    while candidate.exists():
        n += 1
        candidate = path.with_name(f"{path.name}.bak.{n}")
    return candidate


def write_output(path: Path, data: bytes) -> Path | None:
    """Write atomically. An existing file is first copied to a fresh .bak name; returns that backup."""
    backup = None
    if path.exists():
        backup = _backup_path(path)
        shutil.copy2(path, backup)
    temp = path.with_name(path.name + ".savetool-tmp")
    temp.write_bytes(data)
    os.replace(temp, path)
    return backup


def _read(path: str) -> bytes:
    return Path(path).read_bytes()


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(save_path: str, out_dir: str) -> int:
    save = parse(_read(save_path))
    out = Path(out_dir)
    if out.exists() and any(out.iterdir()):
        raise SaveToolError(f"{out} exists and is not empty")
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for entry in save.entries:
        stem = f"e{entry.index:04d}"
        (out / f"{stem}.bin").write_bytes(entry.data)
        (out / f"{stem}.z").write_bytes(entry.raw)
        rows.append(
            {
                "index": entry.index,
                "type": entry.type,
                "id": entry.id,
                "sub": entry.sub,
                "position": entry.position,
                "gap_hex": entry.gap.hex(),
                "inflated_file": f"{stem}.bin",
                "inflated_sha256": _sha256(entry.data),
                "raw_file": f"{stem}.z",
                "raw_sha256": _sha256(entry.raw),
            }
        )
    manifest = {
        "format": DUMP_FORMAT,
        "format_version": DUMP_FORMAT_VERSION,
        "version": save.version,
        "pad": save.pad,
        "tail_hex": save.tail.hex(),
        "entries": rows,
    }
    (out / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"dumped {len(rows)} entries to {out}")
    return 0


def pack(dump_dir: str, out_path: str) -> int:
    base = Path(dump_dir)
    try:
        manifest = json.loads((base / MANIFEST).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SaveToolError(f"{base} has no readable {MANIFEST}") from exc
    if manifest.get("format") != DUMP_FORMAT or manifest.get("format_version") != DUMP_FORMAT_VERSION:
        raise SaveToolError("not a savetool dump of a supported version")
    entries = []
    for i, row in enumerate(manifest["entries"]):
        if (
            row["index"] != i
            or not ENTRY_FILE.fullmatch(row["inflated_file"])
            or not ENTRY_FILE.fullmatch(row["raw_file"])
        ):
            raise SaveToolError(f"manifest entry {i} is malformed")
        data = (base / row["inflated_file"]).read_bytes()
        if _sha256(data) == row["inflated_sha256"]:
            # Untouched: reuse the exact stored compressed bytes, so the output is byte-identical.
            raw = (base / row["raw_file"]).read_bytes()
            if _sha256(raw) != row["raw_sha256"]:
                raise SaveToolError(f"{row['raw_file']} does not match the manifest")
        else:
            # Edited by hand in the dump: recompress with this zlib.
            raw = zlib.compress(data)
        entries.append(
            Entry(
                i,
                row["type"],
                row["id"],
                row["sub"],
                row["position"],
                bytes.fromhex(row["gap_hex"]),
                raw,
                data,
            )
        )
    save = Save(manifest["version"], manifest["pad"], entries, bytes.fromhex(manifest["tail_hex"]))
    blob, _ = assemble(save)
    parse(blob)  # refuse to write something the reader would reject
    backup = write_output(Path(out_path), blob)
    _report_write(out_path, backup)
    return 0


def _report_write(path: str, backup: Path | None) -> None:
    print(f"wrote {path}")
    if backup is not None:
        print(f"previous file kept as {backup}")


def info(save_path: str) -> int:
    blob = _read(save_path)
    save = parse(blob)
    _, offsets = assemble(save)
    print(
        f"SRSC version 0x{save.version:04x}, {len(save.entries)} entries, {len(blob)} bytes, "
        f"index at 0x{len(blob) - INDEX_SIZE * len(save.entries):x}, tail {len(save.tail)} bytes"
    )
    print(f"{'idx':>4} {'type':>7} {'id':>5} {'sub':>4} {'offset':>9} {'stored':>7} {'inflated':>9}  note")
    for entry in save.entries:
        note = ""
        if entry.type == TYPE_RECORD and len(entry.data) >= CLASS_OFFSET + 4:
            note = f"class 0x{_u32(entry.data, CLASS_OFFSET):08x}"
        elif entry.type == TYPE_SKILLS and entry.id == SKILL_TABLE_ID:
            note = "skill table"
        print(
            f"{entry.index:>4} 0x{entry.type:05x} {entry.id:>5} {entry.sub:>4} {offsets[entry.index]:>#9x} "
            f"{len(entry.raw):>7} {len(entry.data):>9}  {note}"
        )
    return 0


def get(save_path: str, name: str, force: bool) -> int:
    save = parse(_read(save_path))
    base_name, _ = resolve_field(name)
    value = get_field(save, name, force)
    print(format_value(FIELDS[base_name].kind, value))
    return 0


def set_command(save_path: str, name: str, text: str, out: str | None, in_place: bool, force: bool) -> int:
    src = Path(save_path)
    target = src if in_place else Path(out)
    if not in_place and target.resolve() == src.resolve():
        raise SaveToolError("-o names the input file; use --in-place to overwrite it")
    save = parse(src.read_bytes())
    kind = FIELDS[resolve_field(name)[0]].kind
    before, after = set_field(save, name, text, force)
    blob, _ = assemble(save)
    parse(blob)
    backup = write_output(target, blob)
    print(f"{name}: {format_value(kind, before)} -> {format_value(kind, after)}")
    _report_write(str(target), backup)
    return 0


def fields_table() -> int:
    labels = {"skill": "skill.N", "skill.max": "skill.N.max"}
    for name, field in FIELDS.items():
        if isinstance(field.locator, RecordLocator):
            where = f"record class 0x{field.locator.class_id:08x}"
            if field.locator.entry_id is not None:
                where += f" id {field.locator.entry_id}"
        else:
            where = "skill table row"
        print(f"{labels.get(name, name)}  [{field.confidence}]  {field.kind} at +0x{field.offset:x}  ({where})")
        print(f"    {field.note}")
    return 0


# ---------------------------------------------------------------- CLI


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="savetool", description="Inspect and edit Drakan .rsg save files.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("info", help="list the container entries").add_argument("save")
    get_cmd = commands.add_parser("get", help="print one field")
    get_cmd.add_argument("save")
    get_cmd.add_argument("field")
    get_cmd.add_argument("--force", action="store_true", help="ignore the verified entry-id pin")
    set_cmd = commands.add_parser("set", help="edit one field into a new file")
    set_cmd.add_argument("save")
    set_cmd.add_argument("field")
    set_cmd.add_argument("value")
    destination = set_cmd.add_mutually_exclusive_group(required=True)
    destination.add_argument("-o", "--out", help="output file (the input is never modified)")
    destination.add_argument("--in-place", action="store_true", help="overwrite the input (keeps NAME.bak)")
    set_cmd.add_argument("--force", action="store_true", help="ignore the verified entry-id pin")
    dump_cmd = commands.add_parser("dump", help="write inflated entries and a manifest to a directory")
    dump_cmd.add_argument("save")
    dump_cmd.add_argument("dir")
    pack_cmd = commands.add_parser("pack", help="rebuild a save from a dump directory")
    pack_cmd.add_argument("dir")
    pack_cmd.add_argument("-o", "--out", required=True)
    commands.add_parser("fields", help="list editable fields with confidence notes")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "info":
            return info(args.save)
        if args.command == "get":
            return get(args.save, args.field, args.force)
        if args.command == "set":
            return set_command(args.save, args.field, args.value, args.out, args.in_place, args.force)
        if args.command == "dump":
            return dump(args.save, args.dir)
        if args.command == "pack":
            return pack(args.dir, args.out)
        return fields_table()
    except (SaveToolError, OSError) as exc:
        print(f"savetool: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
