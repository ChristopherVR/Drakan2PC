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
  progress SAVE                            the state table (kind, key, value) at the end of the text payload
  item   SAVE                              the item text kept in Rynn's record (last inspected item)
  inventory SAVE                           occupied inventory cells with item resource, durability, flags
  validate SAVE                            integrity and plausibility report (exit 1 on errors)

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
AROKH_RESOURCE = 0xA60  # u32 at +0 of Arokh's record (Rynn's is 0xa50)
PROGRESS_ROW_SIZE = 12
PROGRESS_KINDS = (20, 50, 68, 75, 100)
STATS_KEY = 0x739ACD45  # key of the RAM stats block (*(0x4fe86c)); the same key is baked into every save
CELL_COUNT = 24
CELL_SIZE = 16
ITEM_PTR_GAP = 0xE0  # an item record stores its own handle pointer twice, this far apart
QUEST_ROW_SIZE = 0x20
STAT_OFFSETS = {  # offsets from the stats block start S; every value is stored as enc(v)
    "hp": 0xC,
    "maxhp": 0x10,
    "gold": 0x18,
    "mana": 0x1C,
    "maxmana": 0x20,
    "xp": 0x24,
    "level": 0x28,
    "skillpoints": 0x2C,
}


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
    """A 0x102 object record, found by the u32 class id at +8 or the u32 resource id at +0.

    entry_id pins the index id where the layout is verified."""

    class_id: int | None = None
    entry_id: int | None = None
    resource_id: int | None = None


@dataclass(frozen=True)
class SkillTableLocator:
    """Rows of the skill table (type 0x401, id 893). Rows are keyed by their stored id (1 Melee, 2 Archery, 3 Magic)."""


@dataclass(frozen=True)
class ProgressLocator:
    """Rows {u32 kind, u32 key, u32 value} of the state table at the end of the text payload, keyed by `key`."""


@dataclass(frozen=True)
class StatsLocator:
    """The player stats block (RAM 0x4fbac8) dumped raw inside type 0x401 id 893; values are enc()-obfuscated."""


@dataclass(frozen=True)
class CellLocator:
    """Cell N (0..23) of the inventory grid in Rynn's record: {u32 equipped, u32 hot, u32 handle, u32 icon}."""


@dataclass(frozen=True)
class ItemLocator:
    """The item record whose own handle pointer sits in cell N; cur/max follow the pointer (enc()-obfuscated)."""


@dataclass(frozen=True)
class QuestLocator:
    """Row N of the quest / map marker table in type 0x401 id 893 (0x20-byte rows after a u32 count at +0x80)."""


Locator = RecordLocator | SkillTableLocator | ProgressLocator | StatsLocator | CellLocator | ItemLocator | QuestLocator


@dataclass(frozen=True)
class Field:
    locator: Locator
    offset: int  # byte offset inside the record (or skill row)
    kind: str  # "u32" or "vec3" (three float32)
    confidence: str  # "high", "medium" or "low"
    note: str
    minimum: int | None = None
    bound: tuple[str, str] | None = None  # (sibling field, "<=" or ">="), checked within the same skill row
    encoded: bool = False  # u32 stored as rotl32(v, 8) ^ STATS_KEY


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
    "arokh.pos": Field(
        RecordLocator(resource_id=AROKH_RESOURCE),
        12,
        "vec3",
        "low",
        "Record whose resource id (u32 at +0) is 0xa60, float X,Y,Z at +12 like every positioned record. Arokh sits "
        "far from Rynn in Surdana saves. Only matches by symmetry with Rynn; never moved in game.",
    ),
    "progress": Field(
        ProgressLocator(),
        8,
        "u32",
        "low",
        "Row of the state table at the end of type 0x401 id 893: {u32 kind, u32 key, u32 value}, 12 bytes, running to "
        "the end of the payload. kind is one of 20, 50, 68, 75, 100; keys are unique. The table has about 100-370 rows, "
        "fewer after play than at the start. Meaning of kind and value is a hypothesis (per-actor state); only the "
        "layout is verified.",
        minimum=0,
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

_STATS_NOTE = (
    "Stats block S (RAM 0x4fbac8) written raw into type 0x401 id 893; found as the u32 pair (6, 3) = S+4, S+8 "
    "before the skill table. Value at S+0x%x, stored as rotl32(v, 8) ^ 0x739acd45. Proven in game: an edited save "
    "loaded, RAM and the Tab screen show the edit."
)
for _name, _off in STAT_OFFSETS.items():
    FIELDS[f"stat.{_name}"] = Field(
        StatsLocator(),
        _off,
        "u32",
        "medium" if _name in ("mana", "maxmana") else "high",
        _STATS_NOTE % _off,
        minimum=0,
        encoded=True,
    )
FIELDS["inv.equipped"] = Field(
    CellLocator(),
    0,
    "u32",
    "medium",
    "Equipped flag of inventory cell N (inv.N.equipped, 0 or 1) in Rynn's record. The cell table is found by "
    "structure (save-editor.md). Read from 60 saves; edits are checked for load only.",
)
FIELDS["inv.hot"] = Field(
    CellLocator(),
    4,
    "u32",
    "medium",
    "Quick-slot flag of cell N (inv.N.hot, 0 or 1).",
)
FIELDS["inv.durability"] = Field(
    ItemLocator(),
    8,
    "u32",
    "high",
    "Durability (melee), arrows (bow) or quantity (potion) of the item in cell N (inv.N.durability); stored "
    "obfuscated in the item record right after its self pointer. 0xffffffff = indestructible.",
    encoded=True,
)
FIELDS["inv.max"] = Field(
    ItemLocator(),
    12,
    "u32",
    "high",
    "Max durability / arrows / quantity of the item in cell N (inv.N.max).",
    encoded=True,
)
FIELDS["quest"] = Field(
    QuestLocator(),
    0x1C,
    "u32",
    "low",
    "Row N of the quest / map marker table (quest.N, last word of the 0x20-byte row). Row layout "
    "{x, y, float, icon, enabled, region, quest id, state} matches RAM 0x4fbb70; the meaning of state is unverified.",
)

_SKILL_FIELD = re.compile(r"skill\.(\d+)(\.max)?")
_PROGRESS_FIELD = re.compile(r"progress\.(\d+)")
_INV_FIELD = re.compile(r"inv\.(\d+)\.(equipped|hot|durability|max)")
_QUEST_FIELD = re.compile(r"quest\.(\d+)")


def resolve_field(name: str) -> tuple[str, int | None]:
    """Map a user field name to a FIELDS key and, for skill rows, the row id."""
    match = _SKILL_FIELD.fullmatch(name)
    if match:
        return ("skill.max" if match.group(2) else "skill"), int(match.group(1))
    match = _PROGRESS_FIELD.fullmatch(name)
    if match:
        return "progress", int(match.group(1))
    match = _INV_FIELD.fullmatch(name)
    if match:
        return f"inv.{match.group(2)}", int(match.group(1))
    match = _QUEST_FIELD.fullmatch(name)
    if match:
        return "quest", int(match.group(1))
    if name in FIELDS and isinstance(FIELDS[name].locator, (RecordLocator, StatsLocator)):
        return name, None
    raise SaveToolError(
        f"unknown field {name!r}; run `savetool fields` (skill.N, skill.N.max, progress.KEY, stat.NAME, inv.CELL.equipped|hot|durability|max, quest.ROW)"
    )


def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def _find_record(save: Save, loc: RecordLocator, force: bool) -> Entry:
    what = f"class 0x{loc.class_id:08x}" if loc.class_id is not None else f"resource 0x{loc.resource_id:x}"
    candidates = [
        e
        for e in save.entries
        if e.type == TYPE_RECORD
        and len(e.data) >= CLASS_OFFSET + 4
        and (
            _u32(e.data, CLASS_OFFSET) == loc.class_id
            if loc.class_id is not None
            else _u32(e.data, 0) == loc.resource_id
        )
    ]
    if loc.entry_id is not None and not force:
        pinned = [e for e in candidates if e.id == loc.entry_id]
        if candidates and not pinned:
            ids = sorted({e.id for e in candidates})
            raise SaveToolError(
                f"record {what} has entry id {ids}; this field is verified only for id "
                f"{loc.entry_id} (pass --force to use it anyway)"
            )
        candidates = pinned
    if len(candidates) != 1:
        raise SaveToolError(f"expected one record for {what}, found {len(candidates)}")
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


def progress_table(save: Save) -> tuple[Entry, int, list[tuple[int, int, int]]]:
    """Return (entry, offset of the first row, rows). The start is found by walking back from the end of the payload
    while the row kind is known, never into the skill rows."""
    entry, first, count = _skill_table(save)
    data = entry.data
    floor = first + count * SKILL_ROW_SIZE
    start = len(data)
    while start - PROGRESS_ROW_SIZE >= floor and _u32(data, start - PROGRESS_ROW_SIZE) in PROGRESS_KINDS:
        start -= PROGRESS_ROW_SIZE
    rows = [
        struct.unpack_from("<III", data, o) for o in range(start, len(data) - PROGRESS_ROW_SIZE + 1, PROGRESS_ROW_SIZE)
    ]
    return entry, start, rows


def enc(value: int) -> int:
    return (((value << 8) | (value >> 24)) & 0xFFFFFFFF) ^ STATS_KEY


def dec(word: int) -> int:
    word ^= STATS_KEY
    return ((word >> 8) | (word << 24)) & 0xFFFFFFFF


def stats_base(save: Save) -> tuple[Entry, int]:
    """Entry and offset S of the raw stats block: the u32 pair (6, 3) at S+4, S+8 with a plausible block after it."""
    entry, first, _ = _skill_table(save)
    data = entry.data
    for a in range(4, first - 0x30, 4):
        if _u32(data, a) != 6 or _u32(data, a + 4) != 3 or _u32(data, a + 0x10) != 0:
            continue
        hp, mx, gold, _m, _mm, _xp, level, _pts = (dec(_u32(data, a - 4 + o)) for o in STAT_OFFSETS.values())
        if 1 <= mx <= 100_000 and hp <= mx + 100_000 and 1 <= level <= 200 and gold < 100_000_000:
            return entry, a - 4
    raise SaveToolError("stats block not found in the text payload")


def _rynn_record(save: Save) -> Entry:
    found = [
        e
        for e in save.entries
        if e.type == TYPE_RECORD and len(e.data) > 12 and _u32(e.data, CLASS_OFFSET) == RYNN_CLASS
    ]
    if len(found) != 1:
        raise SaveToolError(f"expected one Rynn record, found {len(found)}")
    return found[0]


def _heap_handle(word: int) -> bool:
    return 0x600000 < word < 0x2000000 and word % 16 == 0


def cell_table(save: Save) -> tuple[Entry, int]:
    """Rynn's record and the offset of inventory cell 0. The controller is dumped raw at a layout-dependent offset
    with a 2-byte phase; it is found by structure: -1 at base-0x14 and base-4, selection (-1..0x1c) at base-0x10,
    pane (0..2) at base-0xc, then 24 cells of {equipped 0/1, hot 0/1, handle 0 / <=0x53 / heap pointer, icon}."""
    rec = _rynn_record(save)
    data = rec.data
    for base in range(0x40, len(data) - CELL_COUNT * CELL_SIZE - 8, 2):
        if struct.unpack_from("<i", data, base - 4)[0] != -1 or struct.unpack_from("<i", data, base - 0x14)[0] != -1:
            continue
        if not -1 <= struct.unpack_from("<i", data, base - 0x10)[0] <= 0x1C or _u32(data, base - 0xC) > 2:
            continue
        cells = [struct.unpack_from("<IIII", data, base + CELL_SIZE * k) for k in range(CELL_COUNT)]
        if all(c[0] <= 1 and c[1] <= 1 and (c[2] <= 0x53 or _heap_handle(c[2])) for c in cells):
            return rec, base
    raise SaveToolError("inventory cell table not recognised in Rynn's record")


def item_record(save: Save, handle: int) -> tuple[Entry, int]:
    """The 0x102 record that stores `handle` twice, 0xe0 bytes apart; returns it and the offset of the first copy."""
    pk = struct.pack("<I", handle)
    for e in save.entries:
        if e.type != TYPE_RECORD or len(e.data) < ITEM_PTR_GAP + 16 or _u32(e.data, CLASS_OFFSET) == RYNN_CLASS:
            continue
        i = e.data.find(pk)
        while i >= 0:
            if e.data[i + ITEM_PTR_GAP : i + ITEM_PTR_GAP + 4] == pk:
                return e, i
            i = e.data.find(pk, i + 1)
    raise SaveToolError(f"no item record holds handle 0x{handle:x}")


def inventory(save: Save) -> list[dict]:
    """Occupied cells: cell, equipped, hot, handle and, from the item record, resource id (record u32 +0 plus
    0x20000), entry id, durability and max; or {cell, marker} for the extra cells of a multi-cell item."""
    rec, base = cell_table(save)
    out: list[dict] = []
    for k in range(CELL_COUNT):
        eq, hot, handle, _icon = struct.unpack_from("<IIII", rec.data, base + CELL_SIZE * k)
        if handle == 0:
            continue
        if handle <= 0x53:
            out.append({"cell": k, "marker": handle})
            continue
        row: dict = {"cell": k, "equipped": eq, "hot": hot, "handle": handle}
        try:
            item, i = item_record(save, handle)
            row.update(
                resource=_u32(item.data, 0) + 0x20000,
                entry=item.id,
                durability=dec(_u32(item.data, i + 8)),
                max=dec(_u32(item.data, i + 12)),
            )
        except SaveToolError:
            pass
        out.append(row)
    return out


def quest_rows(save: Save) -> tuple[Entry, int, int]:
    """Entry, offset of the first row and row count of the quest / map marker table."""
    entry, _, _ = _skill_table(save)
    count = _u32(entry.data, 0x80)
    if not 0 <= count <= 64 or 0x84 + count * QUEST_ROW_SIZE > len(entry.data):
        raise SaveToolError("quest table count does not fit the payload")
    return entry, 0x84, count


def locate(save: Save, field: Field, row_id: int | None, force: bool) -> tuple[Entry, int]:
    """Return (entry, base offset) for a field: the record start, or the start of the matching skill row."""
    if isinstance(field.locator, StatsLocator):
        return stats_base(save)
    if isinstance(field.locator, QuestLocator):
        entry, first, count = quest_rows(save)
        if row_id is None or not 0 <= row_id < count:
            raise SaveToolError(f"quest row must be 0..{count - 1}")
        return entry, first + row_id * QUEST_ROW_SIZE
    if isinstance(field.locator, (CellLocator, ItemLocator)):
        if row_id is None or not 0 <= row_id < CELL_COUNT:
            raise SaveToolError("inventory fields need a cell 0..23, e.g. inv.0.durability")
        rec, base = cell_table(save)
        cell = base + CELL_SIZE * row_id
        handle = _u32(rec.data, cell + 8)
        if handle <= 0x53:
            raise SaveToolError(f"cell {row_id} holds no item")
        if isinstance(field.locator, CellLocator):
            return rec, cell
        return item_record(save, handle)
    if isinstance(field.locator, RecordLocator):
        return _find_record(save, field.locator, force), 0
    if isinstance(field.locator, ProgressLocator):
        entry, start, rows = progress_table(save)
        hits = [start + k * PROGRESS_ROW_SIZE for k, row in enumerate(rows) if row[1] == row_id]
        if len(hits) != 1:
            raise SaveToolError(f"no unique progress row with key {row_id}")
        return entry, hits[0]
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
    raw = decode(field.kind, entry.data, base + field.offset)
    return dec(raw) if field.encoded else raw


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
    if field.encoded:
        before = dec(before)
    if isinstance(field.locator, CellLocator) and value > 1:
        raise SaveToolError(f"{name} must be 0 or 1")
    payload = bytearray(entry.data)
    if field.kind == "u32":
        struct.pack_into("<I", payload, off, enc(value) if field.encoded else value)
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
    labels = {
        "skill": "skill.N",
        "skill.max": "skill.N.max",
        "progress": "progress.KEY",
        "inv.equipped": "inv.N.equipped",
        "inv.hot": "inv.N.hot",
        "inv.durability": "inv.N.durability",
        "inv.max": "inv.N.max",
        "quest": "quest.N",
    }
    for name, field in FIELDS.items():
        if isinstance(field.locator, RecordLocator):
            if field.locator.class_id is not None:
                where = f"record class 0x{field.locator.class_id:08x}"
            else:
                where = f"record resource 0x{field.locator.resource_id:x}"
            if field.locator.entry_id is not None:
                where += f" id {field.locator.entry_id}"
        elif isinstance(field.locator, ProgressLocator):
            where = "state table row"
        elif isinstance(field.locator, StatsLocator):
            where = "stats block in type 0x401 id 893"
        elif isinstance(field.locator, CellLocator):
            where = "inventory cell in Rynn's record"
        elif isinstance(field.locator, ItemLocator):
            where = "item record of a cell"
        elif isinstance(field.locator, QuestLocator):
            where = "quest table row"
        else:
            where = "skill table row"
        print(f"{labels.get(name, name)}  [{field.confidence}]  {field.kind} at +0x{field.offset:x}  ({where})")
        print(f"    {field.note}")
    return 0


# ---------------------------------------------------------------- CLI


_LINE_OK = re.compile(r"[A-Za-z' ]+: ?[\w/ ]+|In (left|right) hand|Equips to.*|Indestructible")
_REPAIRS = (
    (r"^ill Needed:", "Skill Needed:"),
    (r"^kill Needed:", "Skill Needed:"),
    (r"^ange:", "Range:"),
    (r"^urability:", "Durability:"),
    (r"^rrows:", "Arrows:"),
    (r"^mage:", "Damage:"),
    (r"^oden Arrows", "Wooden Arrows"),
)


def _clean_line(run: str) -> str:
    text = run
    cut = re.search(r"[=?]", text[:3])
    if cut:
        text = text[cut.end() :]
    text = text.strip()
    for pattern, repl in _REPAIRS:
        if re.match(pattern, text):
            return re.sub(pattern, repl, text)
    return text


def _runs(data: bytes, start: int, end: int) -> list[tuple[int, str]]:
    out = []
    for m in re.finditer(rb"[ -~]+", data[start:end]):
        text = m.group().decode("latin-1")
        if re.search(r"[A-Za-z]{3}", text):
            out.append((start + m.start(), text))
    return out


def item_panel(save: Save) -> dict | None:
    """The item text the inventory screen keeps in Rynn's record: name and detail lines of the last inspected item."""
    rec = next(
        (
            e
            for e in save.entries
            if e.type == TYPE_RECORD and len(e.data) > 12 and _u32(e.data, CLASS_OFFSET) == RYNN_CLASS
        ),
        None,
    )
    if rec is None:
        return None
    data = rec.data
    m = re.search(rb"ed: \d", data)
    if not m:
        return None
    anchor = m.start()
    runs = _runs(data, max(0, anchor - 0x60), anchor + 0x1F0)
    need = next((r for r in runs if "Need" in r[1] and r[0] <= anchor < r[0] + len(r[1])), None)
    need_at = need[0] if need else anchor
    before = [r for r in runs if r[0] + len(r[1]) <= need_at]
    lines: list[str] = []
    for at, text in runs:
        if at < need_at:
            continue
        line = _clean_line(text)
        if (_LINE_OK.fullmatch(line) or "Needed" in line) and line not in lines:
            lines.append(line)
    panel: dict = {"name": _clean_line(before[-1][1]) if before else "", "lines": lines, "offset": anchor}
    for line in lines:
        dm = re.search(r"(\d+)/(\d+)", line)
        if dm and re.search(r"Durability|Arrows", line):
            panel["durability"] = [int(dm.group(1)), int(dm.group(2))]
        if re.search(r"left hand", line, re.I):
            panel["hand"] = "left"
        elif re.search(r"right hand", line, re.I):
            panel["hand"] = "right"
    return panel


def validate_save(save: Save) -> list[tuple[str, str, str]]:
    """Return (severity, code, message) issues: duplicate keys, bad skills, bad player position, state table."""
    out: list[tuple[str, str, str]] = []
    seen: dict[tuple[int, int, int], int] = {}
    for e in save.entries:
        key = (e.type, e.id, e.sub)
        if (e.type, e.id) == (TYPE_SKILLS, 891):  # every save carries two level-name entries
            continue
        seen[key] = seen.get(key, 0) + 1
    dups = [k for k, n in seen.items() if n > 1]
    if dups:
        out.append(("error", "duplicate-entries", f"{len(dups)} entry key(s) occur more than once, e.g. {dups[0]}"))
    path = next((e for e in save.entries if e.type == 0x400), None)
    text = path.data[2:].decode("latin-1").rstrip("\0") if path is not None and len(path.data) > 2 else ""
    if not text:
        out.append(("warn", "level-path", "level path entry (type 0x400) is missing"))
    elif not text.lower().endswith(".lvl"):
        out.append(("warn", "level-path", f"level path {text!r} does not end in .lvl"))
    try:
        entry, first, count = _skill_table(save)
        for k in range(count):
            row = first + k * SKILL_ROW_SIZE
            ident, level, mx = struct.unpack_from("<III", entry.data, row)
            if level > mx:
                out.append(("error", "skill-over-max", f"skill {ident}: level {level} exceeds max {mx}"))
            if mx > 99:
                out.append(("warn", "skill-max", f"skill {ident}: max {mx} is far above the 8 seen in normal saves"))
    except SaveToolError as exc:
        out.append(("warn", "skills", f"skill table not recognised: {exc}"))
    players = [
        e
        for e in save.entries
        if e.type == TYPE_RECORD and len(e.data) > 23 and _u32(e.data, CLASS_OFFSET) == RYNN_CLASS
    ]
    if not players:
        out.append(("info", "player", "no Rynn record: health and position cannot be edited"))
    if len(players) > 1:
        out.append(("error", "player", f"{len(players)} Rynn records found; expected one"))
    for p in players:
        pos = struct.unpack_from("<3f", p.data, 12)
        if not all(math.isfinite(x) for x in pos):
            out.append(("error", "player-pos", "Rynn position holds a NaN or infinity"))
        elif all(x == 0 for x in pos) or any(abs(x) > 1e8 for x in pos):
            out.append(("warn", "player-pos", "Rynn position is zero or out of range"))
    try:
        gold = get_field(save, "gold")
        if isinstance(gold, int) and gold > 10_000_000:
            out.append(("warn", "gold-range", f"gold reads {gold}; this size comes from a second record layout"))
    except SaveToolError:
        pass
    try:
        _, _, rows = progress_table(save)
        if not rows:
            out.append(("warn", "progress", "state table has no rows"))
        keys = [r[1] for r in rows]
        if len(keys) != len(set(keys)):
            out.append(("error", "progress-dup", f"{len(keys) - len(set(keys))} state table key(s) repeat"))
    except SaveToolError:
        pass
    return out


def progress_command(save_path: str) -> int:
    _, start, rows = progress_table(parse(_read(save_path)))
    print(f"state table: {len(rows)} rows from payload offset 0x{start:x}")
    print(f"{'kind':>5} {'key':>6} {'value':>11}")
    for kind, key, value in rows:
        print(f"{kind:>5} {key:>6} {value:>11}")
    return 0


def item_command(save_path: str) -> int:
    panel = item_panel(parse(_read(save_path)))
    if panel is None:
        print("no item text in Rynn's record")
        return 0
    print(json.dumps(panel))
    return 0


def inventory_command(save_path: str) -> int:
    for row in inventory(parse(_read(save_path))):
        print(json.dumps(row))
    return 0


def validate_command(save_path: str) -> int:
    issues = validate_save(parse(_read(save_path)))
    for severity, code, message in issues:
        print(f"{severity:5} {code}: {message}")
    if not issues:
        print("ok")
    return 1 if any(i[0] == "error" for i in issues) else 0


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
    commands.add_parser("progress", help="print the state table").add_argument("save")
    commands.add_parser("item", help="print the item text kept in Rynn's record").add_argument("save")
    commands.add_parser("inventory", help="list the inventory cells and their items").add_argument("save")
    commands.add_parser("validate", help="integrity and plausibility report").add_argument("save")
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
        if args.command == "progress":
            return progress_command(args.save)
        if args.command == "item":
            return item_command(args.save)
        if args.command == "inventory":
            return inventory_command(args.save)
        if args.command == "validate":
            return validate_command(args.save)
        return fields_table()
    except (SaveToolError, OSError) as exc:
        print(f"savetool: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
