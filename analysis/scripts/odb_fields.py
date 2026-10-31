"""Decode the class field model of the ODB files (0x0102 class records + the 0x0103 field-definition blob) and check it
against the per-object parameter overrides of every level LVL.

  python -I analysis/scripts/odb_fields.py --iso "<game>.iso"                 # all ODBs, all levels, summary
  python -I analysis/scripts/odb_fields.py --iso "<game>.iso" --json build/odb-fields   # also dump a catalog per ODB
  python -I analysis/scripts/odb_fields.py --odb NPCS.ODB --class Ballista   # one class, from a file

Layout (docs/reference/level-format.md section 12). Short form, `body` = the 0x0102 payload after the name string:

  u16 group_id; u16 model_id; u16 model_db;
  model_id != 0 (PS2 "model class"):  74 more bytes (u16 +80 = rfl class id, u16 +82 = 1, u16 +84 = icon), fc at +86
  model_id == 0:                     10 zero bytes, u16 rfl class id, u16 1, u16 icon, fc at +22
  u32 field_count; u32 dword_count; u32 data[dword_count]                 # data[i] = default of field i
The 0x0103 entry (zlib) holds one field-definition list per class record, in record order:
  per class { u32 count; count x { u32 type; u16 len; name[len] } }          # count == field_count of that class
Strings and arrays: data[i] = (u16 offset, u16 length) into data (dwords), like the object parameters in the LVL.
Credit: the field model (types, data area, offset/length encoding) follows OpenDrakan (Zalasus, GPL-3.0,
commit 6c0d499, doc/riot_database_format.txt "Class format"); the PS2 record offsets and the 0x0103 list were found
against the PAL disc.
"""

import argparse
import collections
import json
import struct
import sys
from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
DECOMP_DIR = ANALYSIS_DIR.parents[1]
sys.path.insert(0, str(ANALYSIS_DIR))
import lvl_dump
import lvl_objects as lo

MODEL_FC_OFFSET = 86
PLAIN_FC_OFFSET = 22
ARRAY_BIT = 0x1000
TYPE_NAMES = lo.FIELD_TYPES


class OdbFieldError(ValueError):
    pass


def parse_field_lists(blob):
    """The 0x0103 blob -> list (one per class record, in order) of lists of (type, name). Must consume the blob."""
    pos = 0
    lists = []
    while pos < len(blob):
        if pos + 4 > len(blob):
            raise OdbFieldError("field blob: truncated count at 0x%x" % pos)
        (n,) = struct.unpack_from("<I", blob, pos)
        pos += 4
        defs = []
        for _ in range(n):
            if pos + 6 > len(blob):
                raise OdbFieldError("field blob: truncated def at 0x%x" % pos)
            t, ln = struct.unpack_from("<IH", blob, pos)
            raw = blob[pos + 6 : pos + 6 + ln]
            if len(raw) != ln:
                raise OdbFieldError("field blob: truncated name at 0x%x" % pos)
            defs.append((t, raw.split(b"\0", 1)[0].decode("latin-1")))
            pos += 6 + ln
        lists.append(defs)
    return lists


def parse_class_body(payload):
    """A 0x0102 payload -> dict(name, group, model, rfl_class, icon, field_count, data). Raises OdbFieldError."""
    (nl,) = struct.unpack_from("<H", payload, 0)
    name = payload[2 : 2 + nl].split(b"\0", 1)[0].decode("latin-1")
    b = payload[2 + nl :]
    group, model_id, model_db = struct.unpack_from("<HHH", b, 0)
    if model_id:
        fc_at = MODEL_FC_OFFSET
        rfl, one, icon = struct.unpack_from("<HHH", b, 80)
    else:
        fc_at = PLAIN_FC_OFFSET
        rfl, one, icon = struct.unpack_from("<HHH", b, 16)
    if fc_at + 8 > len(b):
        raise OdbFieldError("%s: body too short" % name)
    fc, dc = struct.unpack_from("<II", b, fc_at)
    if fc_at + 8 + 4 * dc != len(b):
        raise OdbFieldError("%s: data area does not end the record" % name)
    data = list(struct.unpack_from("<%dI" % dc, b, fc_at + 8))
    return {
        "name": name,
        "group": group,
        "model": (model_id, model_db) if model_id else None,
        "rfl_class": rfl,
        "icon": icon,
        "field_count": fc,
        "data": data,
        "one": one,
    }


def load_classes(odb_bytes):
    """ODB bytes -> {class id: class dict with `fields` [{index, type, type_name, name, default}]}; raises on mismatch."""
    ents = lvl_dump.parse_srsc(odb_bytes)
    blob = None
    recs = []
    for e in ents:
        raw = odb_bytes[e.offset : e.offset + e.size]
        if e.type == 0x103 and blob is None:
            blob = lvl_dump.try_inflate(raw)
        elif e.type == 0x102:
            recs.append((e, raw))
    lists = parse_field_lists(blob) if blob is not None else []
    if recs and len(lists) != len(recs):
        raise OdbFieldError("%d class records but %d field lists" % (len(recs), len(lists)))
    out = {}
    for (e, raw), defs in zip(recs, lists):
        c = parse_class_body(raw)
        if c["field_count"] != len(defs):
            raise OdbFieldError("%s: field_count %d != %d definitions" % (c["name"], c["field_count"], len(defs)))
        c["id"] = e.id
        c["fields"] = [
            {
                "index": i,
                "type": t,
                "type_name": TYPE_NAMES.get(t & 0xFF, "0x%x" % (t & 0xFF)) + (" array" if t & ARRAY_BIT else ""),
                "name": n,
                "default": lo.field_value(t, c["data"][i], c["data"]) if i < len(c["data"]) else None,
            }
            for i, (t, n) in enumerate(defs)
        ]
        out[e.id] = c
    return out


def disc_files(iso, lang="ENGLISH"):
    from ui_layout_dump import iso_files, read

    f, files = iso_files(str(iso))
    lvls, odbs = {}, {}
    try:
        for path in sorted(files):
            parts = path.strip("/").split("/")
            if parts[0].upper() != lang.upper():
                continue
            n = parts[-1].upper()
            if n.endswith(".ODB"):
                odbs[Path(n).stem] = read(f, files[path])
            elif n.endswith(".LVL") and n != "OUT.LVL":
                lvls[Path(n).stem] = read(f, files[path])
    finally:
        f.close()
    return lvls, odbs


def audit(lvls, odbs):
    """Decode every ODB, then check every LVL object parameter against the class field definitions."""
    classes = {}
    summary = {"odbs": {}, "levels": {}}
    for stem, data in sorted(odbs.items()):
        classes[stem] = load_classes(data)
        cl = classes[stem]
        summary["odbs"][stem] = {"classes": len(cl), "fields": sum(len(c["fields"]) for c in cl.values())}
    totals = collections.Counter()
    kinds = collections.Counter()
    problems = []
    for lname, lvl_bytes in sorted(lvls.items()):
        header = lo.parse_level_header(lo.extract_entry(lvl_bytes, lo.TYPE_HEADER))
        objs = lo.parse_object_stream(lo.extract_entry(lvl_bytes, lo.TYPE_OBJECTS))
        c = collections.Counter()
        for o in objs:
            path = header["dependencies"].get(o["db_index"])
            cls = classes.get(lo.odb_name_for(path), {}).get(o["class_id"]) if path else None
            if cls is None:
                c["unresolved_objects"] += 1
                continue
            c["objects"] += 1
            for p in o["params"]:
                c["params"] += 1
                idx = p["field"]
                fd = cls["fields"][idx] if idx < len(cls["fields"]) else None
                if fd and fd["type"] == p["type"] and fd["name"] == p["name"]:
                    c["params_match"] += 1
                else:
                    c["params_mismatch"] += 1
                    problems.append((lname, o["id"], cls["name"], idx, p["name"]))
        summary["levels"][lname] = dict(c)
        totals.update(c)
    for cl in classes.values():
        for c in cl.values():
            for f in c["fields"]:
                kinds["0x%x" % f["type"]] += 1
    summary["totals"] = dict(totals)
    summary["field_types"] = dict(kinds.most_common())
    summary["mismatches"] = problems[:20]
    return classes, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", type=Path)
    ap.add_argument("--odb", type=Path)
    ap.add_argument("--class", dest="cls")
    ap.add_argument("--json", type=Path, help="write <ODB>.json class catalogs into this folder")
    args = ap.parse_args(argv)
    if args.odb:
        cl = load_classes(args.odb.read_bytes())
        for c in cl.values():
            if args.cls is None or c["name"] == args.cls:
                print(json.dumps({k: v for k, v in c.items() if k != "data"}, indent=1))
        return 0
    if not args.iso:
        ap.error("need --iso or --odb")
    lvls, odbs = disc_files(args.iso)
    classes, summary = audit(lvls, odbs)
    print(json.dumps(summary, indent=1))
    if args.json:
        args.json.mkdir(parents=True, exist_ok=True)
        for stem, cl in classes.items():
            slim = {str(i): {k: v for k, v in c.items() if k not in ("data", "one")} for i, c in cl.items()}
            (args.json / (stem + ".json")).write_text(json.dumps(slim))
    return 0 if not summary["mismatches"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
