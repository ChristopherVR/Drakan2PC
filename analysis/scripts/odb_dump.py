"""Dump an ODB object database: class records (0x101), instance records (0x102) and the zlib blob (0x103).

  python -I analysis/scripts/odb_dump.py --file iso/sample/L0r/LEVEL0RO.ODB
  python -I analysis/scripts/odb_dump.py --iso "<game>.iso" --level LEVEL0RO --limit 80 --json build/odb/LEVEL0RO.json

Layout observed in LEVEL0RO.ODB (confirmed by the checks this script prints, not hard-coded assumptions):
  0x101 class record   u16 name_len, name (NUL padded to name_len)
  0x102 instance       u16 name_len, name (NUL padded), then a u32 body. The body's word 0 low 16 bits equal the
                       SRSC `sub` of the record, which names its class (0x101 record id). Body length varies per
                       record, so there is no single fixed stride; the script reports per-class prefixes and
                       per-word field roles instead.
  0x103 schema         zlib. Parsed here as u32 count, then count x {u32 kind, u16 len, name}.

Field roles are guesses from the values alone: "const" (one value for all records), "class-ref-low16" (word equals the
record's class in its low 16 bits), "float" (mostly plausible non-zero floats, varying), "varying-u32" (anything else).
Use with docs/reference when a role is confirmed against the game.
"""

import argparse
import collections
import json
import struct
import sys
from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(ANALYSIS_DIR))
import lvl_dump

MIN_CLASS_RECORDS = 4


def parse_name(payload):
    """Return (name, offset of the byte after the name field) for a 0x101/0x102 payload."""
    nl = struct.unpack_from("<H", payload, 0)[0]
    name = payload[2 : 2 + nl].split(b"\0", 1)[0].decode("latin-1")
    return name, 2 + nl


def parse_schema(blob):
    """Parse the 0x103 blob as u32 count + count x {u32 kind, u16 len, name}. Reports how far it gets."""
    if len(blob) < 4:
        return {"blob_size": len(blob), "error": "short blob"}
    declared = struct.unpack_from("<I", blob, 0)[0]
    pos = 4
    parsed = 0
    kinds = collections.Counter()
    names = []
    while parsed < declared and pos + 6 <= len(blob):
        kind, nl = struct.unpack_from("<IH", blob, pos)
        raw = blob[pos + 6 : pos + 6 + nl]
        text = raw.split(b"\0", 1)[0]  # the length field counts NUL padding, as for the 0x101/0x102 names
        if len(raw) != nl or not text or not all(0x20 <= c < 0x7F for c in text) or raw[len(text) :].strip(b"\0"):
            break
        kinds[kind] += 1
        names.append(text.decode("ascii"))
        pos += 6 + nl
        parsed += 1
    return {
        "blob_size": len(blob),
        "declared": declared,
        "parsed": parsed,
        "consumed": pos,
        "complete": parsed == declared and pos == len(blob),
        "kinds": {"0x%x" % k: v for k, v in kinds.most_common()},
        "first_names": names[:12],
    }


def load_odb(data):
    ents = lvl_dump.parse_srsc(data)
    classes = {}
    instances = []
    schema_blob = None
    types = collections.Counter()
    for e in ents:
        types["0x%04x" % e.type] += 1
        payload = data[e.offset : e.offset + e.size]
        if e.type == 0x101:
            classes[e.id] = parse_name(payload)[0]
        elif e.type == 0x102:
            name, body_at = parse_name(payload)
            instances.append(
                {
                    "entry": e.index,
                    "id": e.id,
                    "sub": e.sub,
                    "name": name,
                    "size": e.size,
                    "body": payload[body_at:],
                }
            )
        elif e.type == 0x103 and schema_blob is None:
            schema_blob = lvl_dump.try_inflate(payload)
    return {
        "entry_count": len(ents),
        "types": dict(types),
        "classes": classes,
        "instances": instances,
        "schema": parse_schema(schema_blob) if schema_blob is not None else None,
    }


def plausible_float_bits(x):
    f = struct.unpack("<f", struct.pack("<I", x))[0]
    return lvl_dump.plausible_float(f) and f != 0.0, f


def common_prefix(bodies, cap=256):
    n = min(*(len(b) for b in bodies), cap)
    first = bodies[0]
    for i in range(n):
        c = first[i]
        if any(b[i] != c for b in bodies):
            return i
    return n


def size_report(instances):
    by_class = collections.defaultdict(list)
    for r in instances:
        by_class[r["sub"]].append(r)
    rows = []
    for sub, recs in sorted(by_class.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        rows.append(
            {
                "sub": sub,
                "count": len(recs),
                "body_sizes": collections.Counter(len(r["body"]) for r in recs).most_common(4),
                "common_body_prefix": common_prefix([r["body"] for r in recs]) if len(recs) > 1 else None,
            }
        )
    return {
        "record_sizes": collections.Counter(r["size"] for r in instances).most_common(8),
        "by_class": rows,
    }


def field_report(instances, words=64, min_records=4):
    fields = []
    for k in range(words):
        vals = [(struct.unpack_from("<I", r["body"], 4 * k)[0], r) for r in instances if len(r["body"]) >= 4 * k + 4]
        if len(vals) < min_records:
            break
        raw = [v for v, _ in vals]
        n = len(raw)
        distinct = len(set(raw))
        modal, modal_n = collections.Counter(raw).most_common(1)[0]
        low_ref = sum(1 for v, r in vals if (v & 0xFFFF) == r["sub"]) / n
        float_hits = []
        for v in raw:
            ok, f = plausible_float_bits(v)
            if ok:
                float_hits.append(abs(f))
        float_frac = len(float_hits) / n
        if distinct == 1:
            role = "const"
        elif low_ref >= 0.9:
            role = "class-ref-low16"
        elif float_frac >= 0.8:
            role = "float"
        else:
            role = "varying-u32"
        row = {
            "word": k,
            "offset": 4 * k,
            "n": n,
            "distinct": distinct,
            "modal": "0x%08x" % modal,
            "modal_frac": round(modal_n / n, 3),
            "float_frac": round(float_frac, 3),
            "role": role,
        }
        if float_hits:
            float_hits.sort()
            row["float_median_abs"] = round(float_hits[len(float_hits) // 2], 4)
        fields.append(row)
    return fields


def float_triples(fields):
    """Offsets where three consecutive varying float fields start: candidate 3-vectors (positions or scales)."""
    by_word = {f["word"]: f for f in fields}
    out = []
    for k in by_word:
        trio = [by_word.get(k + j) for j in range(3)]
        if all(t and t["role"] == "float" for t in trio):
            out.append({"offset": 4 * k, "median_abs": [t.get("float_median_abs") for t in trio]})
    return out


def analyse(data, words=64):
    odb = load_odb(data)
    inst = odb["instances"]
    cls = odb["classes"]
    result = {
        "entry_count": odb["entry_count"],
        "types": odb["types"],
        "classes": {"0x%x" % k: v for k, v in sorted(cls.items())},
        "instance_count": len(inst),
        "schema": odb["schema"],
        "sizes": size_report(inst),
    }
    result["class_ref_match"] = sum(
        1 for r in inst if len(r["body"]) >= 4 and (struct.unpack_from("<I", r["body"], 0)[0] & 0xFFFF) == r["sub"]
    )
    # Body layouts differ per class, so roles are classified per class (classes with enough records only).
    by_class = collections.defaultdict(list)
    for r in inst:
        by_class[r["sub"]].append(r)
    result["fields_by_class"] = {}
    for sub, recs in sorted(by_class.items()):
        if len(recs) < MIN_CLASS_RECORDS:
            continue
        fields = field_report(recs, words=words)
        result["fields_by_class"]["0x%x" % sub] = {
            "class": cls.get(sub, "?"),
            "n": len(recs),
            "fields": fields,
            "float_triples": float_triples(fields),
        }
    result["records"] = [
        {
            "entry": r["entry"],
            "id": r["id"],
            "sub": r["sub"],
            "class": cls.get(r["sub"], "?"),
            "name": r["name"],
            "size": r["size"],
            "head": ["0x%08x" % v for v in struct.unpack_from("<%dI" % min(6, len(r["body"]) // 4), r["body"])],
        }
        for r in inst
    ]
    return result


def print_report(label, res, limit):
    print(
        "%s: %d entries, %d instances, %d classes, %d class-ref matches (word0 low16 == sub)"
        % (label, res["entry_count"], res["instance_count"], len(res["classes"]), res["class_ref_match"])
    )
    print("types:", ", ".join("%s x%d" % kv for kv in sorted(res["types"].items())))
    print("classes (0x101):")
    for k, v in res["classes"].items():
        print("  %s  %s" % (k, v))
    sch = res["schema"]
    if sch and "error" not in sch:
        print(
            "schema (0x103): %d declared, %d parsed, %d of %d bytes consumed, kinds %s"
            % (sch["declared"], sch["parsed"], sch["consumed"], sch["blob_size"], sch["kinds"])
        )
        print("  first names:", ", ".join(sch["first_names"]))
    print("record sizes (size: count):", res["sizes"]["record_sizes"])
    print("per class (sub: count, body sizes, common body prefix bytes):")
    for row in res["sizes"]["by_class"]:
        print(
            "  sub %3d  n=%4d  sizes=%s  prefix=%s"
            % (row["sub"], row["count"], row["body_sizes"], row["common_body_prefix"])
        )
    print("records (first %d):" % min(limit, len(res["records"])))
    for r in res["records"][:limit]:
        print(
            "  e%-4d id=%-4d sub=%-3d %-18s %-18s size=%-5d head=%s"
            % (r["entry"], r["id"], r["sub"], r["class"][:18], r["name"][:18], r["size"], " ".join(r["head"]))
        )
    for sub, block in res["fields_by_class"].items():
        print(
            "body fields, class %s (%s), %d records: word offset n distinct modal (frac) float role"
            % (sub, block["class"], block["n"])
        )
        for f in block["fields"]:
            extra = " median|x|=%s" % f["float_median_abs"] if "float_median_abs" in f else ""
            print(
                "  w%02d @%03x n=%4d distinct=%4d modal=%s (%.2f) float=%.2f %s%s"
                % (
                    f["word"],
                    f["offset"],
                    f["n"],
                    f["distinct"],
                    f["modal"],
                    f["modal_frac"],
                    f["float_frac"],
                    f["role"],
                    extra,
                )
            )
        offs = [t["offset"] for t in block["float_triples"]]
        print("  float 3-vector candidates (body offsets):", offs or "none")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--file", type=Path, help="an .ODB file")
    src.add_argument("--iso", type=Path, help="disc image; use with --level")
    ap.add_argument("--level", help="level folder on the disc, e.g. LEVEL0RO (ENGLISH)")
    ap.add_argument("--lang", default="ENGLISH")
    ap.add_argument("--limit", type=int, default=40, help="records to print")
    ap.add_argument("--words", type=int, default=64, help="u32 body words to classify")
    ap.add_argument("--json", type=Path, help="also write the full result here")
    args = ap.parse_args(argv)

    if args.file:
        label = args.file.name
        data = args.file.read_bytes()
    else:
        if not args.level:
            ap.error("--iso needs --level")
        groups = lvl_dump.iso_level_files(args.iso, lang=args.lang, wanted={args.level.upper()})
        odbs = [(n, d) for n, d in groups.get(args.level.upper(), []) if n.lower().endswith(".odb")]
        if len(odbs) != 1:
            ap.error("expected one .ODB in %s, found %d" % (args.level, len(odbs)))
        label, data = odbs[0]

    res = analyse(data, words=args.words)
    print_report(label, res, args.limit)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(res, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
