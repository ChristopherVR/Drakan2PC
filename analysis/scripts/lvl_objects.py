"""Decode the placed-object list of a level (.LVL SRSC type 0x0020) into build/lvl-objects/<LEVEL>.json.

  python -I analysis/scripts/lvl_objects.py --iso "<game>.iso" --level LEVEL1NA
  python -I analysis/scripts/lvl_objects.py --iso ... --level LEVEL1NA --world world_Surdana_*.json   # validate
  python -I analysis/scripts/lvl_objects.py --lvl LEVEL1NA.LVL --odb-dir <folder with *.ODB>

Layout: docs/reference/level-format.md (confidence tags there). Short form of the 0x0020 payload:

  u16 count; count x {u32 id, u16 class_id, u16 db_index}; then `count` bodies back to back:
    u32 lighting_layer, u32 unk, f32 pos[3], u32 flags (0x100 scaled, 0x10 has radius), u16 event_count,
    [f32 radius if flags & 0x10], u16 link_count, link_count x u32, i16 rot[3] (degrees),
    [f32 scale[3] if flags & 0x100], u32 0, u32 0xffffffff,
    u32 data_dwords, u32 param_count, u32 data[data_dwords], u16 field_index[param_count],
    param_count x {u32 type, u16 len, name[len]}

The (class_id, db_index) pair names a 0x0102 class record: db_index selects an ODB through the LVL 0x0000 dependency
table. The skeleton (table, rotation, scale, links, field_def list) follows OpenDrakan (GPL-3.0, Zalasus), commit
6c0d499, doc/lvl_format.txt and doc/riot_database_format.txt; the PS2 differences (8-byte prefix, 0x10 radius word,
the 0xffffffff sentinel, u32 link ids) were found against the PAL disc.
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
DECOMP_DIR = ANALYSIS_DIR.parents[1]
sys.path.insert(0, str(ANALYSIS_DIR))
import iop_mangle
import lvl_dump

DEFAULT_OUT = DECOMP_DIR / "build" / "lvl-objects"
TYPE_HEADER = 0x0000
TYPE_OBJECTS = 0x0020
FLAG_RADIUS = 0x10
FLAG_SCALED = 0x100
SENTINEL = 0xFFFFFFFF

FIELD_TYPES = {
    1: "int",
    2: "float",
    3: "class",
    4: "model",
    5: "sound",
    6: "bool",
    7: "enum",
    8: "channel",
    9: "animation",
    0x0A: "string",
    0x0B: "sequence",
    0x0C: "layer",
    0x0D: "unknown",
    0x0E: "texture",
    0x0F: "color",
}


class LevelFormatError(ValueError):
    pass


class Reader:
    def __init__(self, data, off=0):
        self.b = data
        self.o = off

    def take(self, fmt):
        size = struct.calcsize(fmt)
        if self.o + size > len(self.b):
            raise LevelFormatError("read past end at 0x%x" % self.o)
        v = struct.unpack_from(fmt, self.b, self.o)
        self.o += size
        return v

    def raw(self, n):
        if self.o + n > len(self.b):
            raise LevelFormatError("read past end at 0x%x" % self.o)
        v = self.b[self.o : self.o + n]
        self.o += n
        return v

    def string(self):
        (n,) = self.take("<H")
        return self.raw(n).split(b"\0", 1)[0].decode("latin-1")


def parse_level_header(data):
    """LVL 0x0000: level name, max size and the dependency table {db index: path}."""
    r = Reader(data)
    name = r.string()
    width, height, count = r.take("<III")
    deps = {}
    for _ in range(count):
        index, _zero = r.take("<HH")
        deps[index] = r.string()
    return {"name": name, "max_width": width, "max_height": height, "dependencies": deps}


def odb_name_for(path):
    """ODB file stem for a dependency path ('..\\..\\Common\\NPC\\NPCs' -> NPCS), via the disc name mangler."""
    return iop_mangle.mangle_component(path.replace("/", "\\").split("\\")[-1])


def field_value(ftype, dword, data):
    """Typed view of one parameter dword. Strings and arrays are (u16 offset, u16 length) into the data area."""
    base = ftype & 0xFF
    is_array = (ftype >> 8) & 0x10 != 0
    if is_array or base == 0x0A:
        off, length = dword & 0xFFFF, dword >> 16
        if off + length > len(data):
            return {"raw": dword, "error": "array out of range"}
        words = data[off : off + length]
        if base == 0x0A and not is_array:
            text = b"".join(struct.pack("<I", w) for w in words).split(b"\0", 1)[0]
            return text.decode("latin-1")
        return list(words)
    if base == 2:
        return struct.unpack("<f", struct.pack("<I", dword))[0]
    if base == 1:
        return struct.unpack("<i", struct.pack("<I", dword))[0]
    return dword


def parse_body(r):
    """Parse one object body at the reader position (after the table entry)."""
    start = r.o
    lighting, unk = r.take("<II")
    pos = r.take("<3f")
    (flags,) = r.take("<I")
    (events,) = r.take("<H")
    radius = r.take("<f")[0] if flags & FLAG_RADIUS else None
    (n_links,) = r.take("<H")
    links = list(r.take("<%dI" % n_links)) if n_links else []
    rot = r.take("<3h")
    scale = r.take("<3f") if flags & FLAG_SCALED else None
    zero, sentinel = r.take("<II")
    if sentinel != SENTINEL or not all(math.isfinite(v) for v in pos):
        raise LevelFormatError("body at 0x%x: bad sentinel or position" % start)
    data_dwords, n_params = r.take("<II")
    data = list(r.take("<%dI" % data_dwords)) if data_dwords else []
    indices = list(r.take("<%dH" % n_params)) if n_params else []
    params = []
    for i in range(n_params):
        (ftype,) = r.take("<I")
        name = r.string()
        dword = data[i] if i < len(data) else 0
        params.append(
            {
                "field": indices[i],
                "name": name,
                "type": ftype,
                "type_name": FIELD_TYPES.get(ftype & 0xFF, "0x%x" % (ftype & 0xFF)),
                "value": field_value(ftype, dword, data),
            }
        )
    return {
        "offset": start,
        "lighting_layer": lighting,
        "unk": unk,
        "pos": list(pos),
        "flags": flags,
        "initial_events": events,
        "radius": radius,
        "links": links,
        "rot_deg": list(rot),
        "scale": list(scale) if scale else None,
        "pre_sentinel": zero,
        "data_dwords": data_dwords,
        "params": params,
        "data": data,
        "end": r.o,
    }


def parse_object_stream(b):
    """Decode a 0x0020 payload. Raises LevelFormatError unless the bodies consume the payload exactly."""
    r = Reader(b)
    (count,) = r.take("<H")
    table = [r.take("<IHH") for _ in range(count)]
    objects = []
    for i, (oid, class_id, db_index) in enumerate(table):
        try:
            body = parse_body(r)
        except (LevelFormatError, struct.error) as e:
            raise LevelFormatError("object %d (id %d): %s" % (i, oid, e)) from e
        body.update({"index": i, "id": oid, "class_id": class_id, "db_index": db_index})
        objects.append(body)
    if r.o != len(b):
        raise LevelFormatError("%d bytes left after the last object" % (len(b) - r.o))
    return objects


def euler_to_yaw_deg(rot):
    """Yaw as the runtime world reader reports it (atan2(m[0][2], m[0][0])) for a yaw-only rotation: ry - 90."""
    return (rot[1] - 90 + 180) % 360 - 180


def odb_class_id(body):
    """Runtime class id from a 0x0102 body: u32 at +16 when the u16 at +14 is 0, else u32 at +80 (601/601, Surdana)."""
    if len(body) < 20:
        return None
    at = 16 if struct.unpack_from("<H", body, 14)[0] == 0 else 80
    return struct.unpack_from("<I", body, at)[0] if at + 4 <= len(body) else None


def load_odbs(odb_files):
    """{STEM: bytes} -> {STEM: {srsc id: {name, group, runtime_class}}}."""
    import odb_dump

    out = {}
    for stem, data in odb_files.items():
        odb = odb_dump.load_odb(data)
        out[stem.upper()] = {
            i["id"]: {"name": i["name"], "group": i["sub"], "runtime_class": odb_class_id(i["body"])}
            for i in odb["instances"]
        }
    return out


def build_records(objects, header=None, odbs=None):
    deps = (header or {}).get("dependencies", {})
    recs = []
    for o in objects:
        r = {
            "index": o["index"],
            "id": o["id"],
            "class_id": o["class_id"],
            "db_index": o["db_index"],
            "res_id": "0x%x" % ((o["db_index"] << 16) | o["class_id"]),
            "pos": o["pos"],
            "rot_deg": o["rot_deg"],
            "yaw_deg": euler_to_yaw_deg(o["rot_deg"]),
            "scale": o["scale"],
            "radius": o["radius"],
            "flags": o["flags"],
            "lighting_layer": o["lighting_layer"],
            "links": o["links"],
            "params": o["params"],
        }
        path = deps.get(o["db_index"])
        if path:
            r["db"] = odb_name_for(path)
            cls = (odbs or {}).get(r["db"], {}).get(o["class_id"])
            if cls:
                r["name"] = cls["name"]
                r["group"] = cls["group"]
                rc = cls["runtime_class"]
                r["runtime_class"] = "0x%x" % rc if rc is not None else None
        recs.append(r)
    return recs


def validate(recs, world, eps=1.0):
    """Compare with a world JSON dump (drakan::world). Match key: resId + position within eps."""
    ents = world["entities"]
    by = {}
    for e in ents:
        by.setdefault(e["resId"], []).append(e)
    matched = yaw_ok = cls_checked = cls_ok = 0
    for r in recs:
        best = None
        for e in by.get(r["res_id"], ()):
            if all(abs(a - b) <= eps for a, b in zip(e["pos"], r["pos"])):
                best = e
                break
        if not best:
            continue
        matched += 1
        d = abs((math.degrees(best["yaw"]) - r["yaw_deg"] + 180) % 360 - 180)
        yaw_ok += d < 6
        if r.get("runtime_class"):
            cls_checked += 1
            cls_ok += r["runtime_class"] == best["class"]
    return {
        "records": len(recs),
        "live_actors": len(ents),
        "matched_res_id_and_pos": matched,
        "yaw_within_6deg": yaw_ok,
        "runtime_class_checked": cls_checked,
        "runtime_class_equal": cls_ok,
        "eps": eps,
    }


def extract_entry(lvl_bytes, etype):
    for e in lvl_dump.parse_srsc(lvl_bytes):
        if e.type == etype:
            return lvl_bytes[e.offset : e.offset + e.size]
    raise LevelFormatError("no 0x%04x entry in the LVL" % etype)


def load_from_iso(iso, level, lang="ENGLISH"):
    """The level's LVL plus every ODB on the disc, keyed by file stem."""
    from ui_layout_dump import iso_files, read

    f, files = iso_files(str(iso))
    lvl = None
    odbs = {}
    try:
        for path in sorted(files):
            parts = path.strip("/").split("/")
            if parts[0].upper() != lang.upper():
                continue
            fname = parts[-1].upper()
            if fname == level.upper() + ".LVL":
                lvl = read(f, files[path])
            elif fname.endswith(".ODB"):
                odbs[Path(fname).stem] = read(f, files[path])
    finally:
        f.close()
    if lvl is None:
        raise LevelFormatError("no %s.LVL on the disc" % level)
    return lvl, odbs


def decode_level(lvl_bytes, odb_bytes=None):
    header = parse_level_header(extract_entry(lvl_bytes, TYPE_HEADER))
    objects = parse_object_stream(extract_entry(lvl_bytes, TYPE_OBJECTS))
    odbs = load_odbs(odb_bytes) if odb_bytes else None
    return header, build_records(objects, header, odbs)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--iso", type=Path)
    src.add_argument("--lvl", type=Path)
    ap.add_argument("--level", help="LVL name with --iso, e.g. LEVEL1NA")
    ap.add_argument("--odb-dir", type=Path, help="with --lvl: folder holding *.ODB files")
    ap.add_argument("--world", type=Path, help="world JSON dump to validate against")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args(argv)

    if args.iso:
        if not args.level:
            ap.error("--iso needs --level")
        lvl_bytes, odb_bytes = load_from_iso(args.iso, args.level)
        name = args.level.upper()
    else:
        lvl_bytes = args.lvl.read_bytes()
        odb_bytes = {p.stem.upper(): p.read_bytes() for p in args.odb_dir.glob("*.ODB")} if args.odb_dir else {}
        name = args.lvl.stem.upper()
    header, recs = decode_level(lvl_bytes, odb_bytes)
    doc = {
        "format": 1,
        "level": name,
        "level_name": header["name"],
        "dependencies": {str(k): v for k, v in sorted(header["dependencies"].items())},
        "count": len(recs),
        "objects": recs,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out / (name + ".json")
    out.write_text(json.dumps(doc, indent=1))
    print("%s: %d objects -> %s" % (name, len(recs), out))
    if args.world:
        print(json.dumps(validate(recs, json.loads(args.world.read_text())), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
