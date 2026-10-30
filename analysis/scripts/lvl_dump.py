"""Dump the SRSC entries of level folders (.LVL .ODB .MOD .ADB .TXD .SSD) and run cheap structure heuristics.

Input is either an extracted directory or the level folders of the disc image (ISO9660 read in place, no mount):

  python -I analysis/scripts/lvl_dump.py --dir iso/sample/L0r
  python -I analysis/scripts/lvl_dump.py --iso "<game>.iso" --level LEVEL0RO --analyse
  python -I analysis/scripts/lvl_dump.py --iso "<game>.iso" --all --analyse

Output per level, under build/lvl-dump/<LEVEL>/ (gitignored, never commit it):
  index.json                   every SRSC entry: file, type, id, sub, offset, size, zlib, inflated size, bin paths
  <FILE>/NNNN_tTTTT_iI_sS.raw.bin   raw payload of each entry
  <FILE>/NNNN_tTTTT_iI_sS.inf.bin   inflated payload, only when the payload is a complete zlib stream
  analysis.json                (--analyse) float runs, u32 lag-equality (record stride), strings, repeated u32

The analysis is heuristic: every field it reports is a guess to be checked against the game, not a decoded format.
A type histogram is printed for each level.
"""

import argparse
import collections
import json
import math
import operator
import os
import re
import shutil
import struct
import sys
import zlib
from array import array
from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
DECOMP_DIR = ANALYSIS_DIR.parents[1]
sys.path.insert(0, str(ANALYSIS_DIR))
import iop_srsc
import ui_layout_dump

SRSC_EXTS = (".lvl", ".odb", ".mod", ".adb", ".txd", ".ssd")
LEVEL_FOLDERS = ("LEVELS", "WORLDCON")
DEFAULT_OUT = DECOMP_DIR / "build" / "lvl-dump"
DEFAULT_MAX_BYTES = 1 << 20

# Class ids quoted in docs/journal/2026-10-31/save-editor.md (save fixture). Reported when they appear.
KNOWN_CLASS_IDS = {
    0x17081069: "player class (Rynn, save fixture)",
    0x19001255: "gold record class (save fixture)",
}

Entry = collections.namedtuple("Entry", "index type id sub offset size")

ASCII_RUN = re.compile(rb"[\x20-\x7e]{5,}")


def parse_srsc(data):
    """Return the index entries of an SRSC blob. Raises ValueError when the container does not check out."""
    if len(data) < 16 or data[:4] != b"SRSC":
        raise ValueError("not an SRSC file")
    _magic, _ver, idx, cnt = struct.unpack_from("<4sHII", data, 0)
    if idx + 14 * cnt != len(data):
        raise ValueError("index size does not match file size")
    out = []
    for i, (typ, rid, sub, off, size) in enumerate(iop_srsc.parse(data)[4]):
        if off + size > idx:
            raise ValueError("entry %d runs into the index" % i)
        out.append(Entry(i, typ, rid, sub, off, size))
    return out


def try_inflate(raw):
    """Inflate a payload that is one complete zlib stream; None otherwise."""
    if len(raw) < 6 or (raw[0] & 0x0F) != 8 or ((raw[0] << 8) | raw[1]) % 31:
        return None
    dec = zlib.decompressobj()
    try:
        out = dec.decompress(raw) + dec.flush()
    except zlib.error:
        return None
    return out if dec.eof else None


def _u32_array(b, typecode):
    n = len(b) // 4
    arr = array(typecode)
    arr.frombytes(b[: n * 4])
    if sys.byteorder == "big":
        arr.byteswap()
    return arr


def plausible_float(x):
    """Finite, |x| < 1e5, and either exactly zero or not a denormal-looking tiny value."""
    return math.isfinite(x) and abs(x) < 1e5 and (x == 0.0 or abs(x) >= 1e-3)


def float_runs(b, min_len=3, top=20):
    """Runs of consecutive plausible float32 (4-byte aligned) containing at least one non-zero value."""
    vals = _u32_array(b, "f")
    runs = []
    start = None
    for i in range(len(vals) + 1):
        ok = i < len(vals) and plausible_float(vals[i])
        if ok and start is None:
            start = i
        elif not ok and start is not None:
            seg = vals[start:i]
            if len(seg) >= min_len and any(x != 0.0 for x in seg):
                runs.append(
                    {
                        "offset": start * 4,
                        "count": len(seg),
                        "head": [round(x, 4) for x in seg[:4]],
                        "max_abs": round(max(abs(x) for x in seg), 3),
                    }
                )
            start = None
    nonzero = sum(1 for x in vals if x != 0.0 and plausible_float(x))
    runs.sort(key=lambda r: (-r["count"], r["offset"]))
    return {
        "floats": len(vals),
        "plausible_nonzero_fraction": round(nonzero / len(vals), 4) if len(vals) else 0.0,
        "run_count": len(runs),
        "top_runs": runs[:top],
    }


def stride_candidates(b, max_words=16384, max_lag_words=256, top=5):
    """Lag-equality scan over u32 words: a record stride shows as a lag whose match rate stands out.

    Zero words never count as a match (they are padding). The score is the match rate divided by the median rate
    over all lags (floored at one match in the array), so a value well above 1 stands out. Multiples of the true stride also score, so read the
    smallest lag among near-equal candidates.
    """
    words = _u32_array(b[: 4 * max_words], "I")
    n = len(words)
    nonzero = sum(1 for w in words if w)
    if n < 16 or nonzero < 16:
        return {"nonzero_words": nonzero, "candidates": []}
    # Unique sentinels for zero words on each side, so a zero never equals anything.
    keys_a = [w if w else (1 << 40) + i for i, w in enumerate(words)]
    keys_b = [w if w else (1 << 41) + i for i, w in enumerate(words)]
    rates = {}
    for lag in range(2, min(max_lag_words, n // 4) + 1):
        rates[lag] = sum(map(operator.eq, keys_a[: n - lag], keys_b[lag:])) / (n - lag)
    ordered = sorted(rates.values())
    median = ordered[len(ordered) // 2]
    base = max(median, 1.0 / n)  # a lag with no matches at all still needs a finite baseline
    ranked = sorted(rates.items(), key=lambda kv: (-kv[1], kv[0]))[:top]
    cands = [
        {
            "lag_words": lag,
            "stride_bytes": 4 * lag,
            "match_rate": round(rate, 4),
            "score": round(rate / base, 2),
        }
        for lag, rate in ranked
        if rate > 0
    ]
    return {"nonzero_words": nonzero, "median_rate": round(median, 5), "candidates": cands}


def ascii_strings(b, top=20):
    """Printable runs of 5+ bytes. Flags runs preceded by a u16 length equal to their size (the ODB name style)."""
    hits = []
    prefixed = 0
    total = 0
    for m in ASCII_RUN.finditer(b):
        total += 1
        text = m.group().decode("ascii")
        off = m.start()
        lp = off >= 2 and struct.unpack_from("<H", b, off - 2)[0] in (len(text), len(text) + 1, len(text) + 2)
        prefixed += lp
        if len(hits) < top:
            hits.append({"offset": off, "len_prefixed": lp, "text": text[:60]})
    return {"count": total, "len_prefixed_count": prefixed, "first": hits}


def repeated_u32(b, min_count=3, top=20):
    """Most common aligned u32 values >= 0x10000 (candidate class ids, flags or pointers), plus known class ids."""
    words = _u32_array(b, "I")
    counts = collections.Counter(words)
    rows = []
    for value, count in counts.most_common():
        if len(rows) >= top:
            break
        if count < min_count or value < 0x10000:
            continue
        rows.append(
            {
                "value": "0x%08x" % value,
                "count": count,
                "first_offset": 4 * words.index(value),
                "known": KNOWN_CLASS_IDS.get(value),
            }
        )
    known = [
        {"value": "0x%08x" % v, "count": counts[v], "known": name} for v, name in KNOWN_CLASS_IDS.items() if counts[v]
    ]
    return {"top": rows, "known_present": known}


def analyse_payload(payload, max_bytes=DEFAULT_MAX_BYTES):
    body = payload[:max_bytes]
    return {
        "bytes_analysed": len(body),
        "floats": float_runs(body),
        "stride": stride_candidates(body),
        "strings": ascii_strings(body),
        "u32": repeated_u32(body),
    }


def dump_level(level, files, out_root, analyse=False, max_bytes=DEFAULT_MAX_BYTES):
    """Write one level. `files` is a list of (file name, bytes). Returns the index dict."""
    level_dir = Path(out_root) / level
    if level_dir.exists():
        shutil.rmtree(level_dir)  # drop stale bins from an earlier run of this level
    level_dir.mkdir(parents=True)
    entries = []
    analysis = []
    skipped = []
    for name, data in files:
        try:
            ents = parse_srsc(data)
        except ValueError as exc:
            skipped.append({"file": name, "reason": str(exc)})
            continue
        fdir = level_dir / name
        fdir.mkdir()
        for e in ents:
            payload = data[e.offset : e.offset + e.size]
            stem = "%04d_t%04x_i%d_s%d" % (e.index, e.type, e.id, e.sub)
            (fdir / (stem + ".raw.bin")).write_bytes(payload)
            inflated = try_inflate(payload)
            if inflated is not None:
                (fdir / (stem + ".inf.bin")).write_bytes(inflated)
            rec = {
                "file": name,
                "entry": e.index,
                "type": "0x%04x" % e.type,
                "id": e.id,
                "sub": e.sub,
                "offset": e.offset,
                "size": e.size,
                "zlib": inflated is not None,
                "inflated_size": len(inflated) if inflated is not None else None,
                "raw": "%s/%s.raw.bin" % (name, stem),
                "inflated": ("%s/%s.inf.bin" % (name, stem)) if inflated is not None else None,
            }
            entries.append(rec)
            if analyse:
                body = inflated if inflated is not None else payload
                analysis.append(
                    {
                        "file": name,
                        "entry": e.index,
                        "type": rec["type"],
                        "id": e.id,
                        "sub": e.sub,
                        "source": "inflated" if inflated is not None else "raw",
                        **analyse_payload(body, max_bytes),
                    }
                )
    hist = collections.OrderedDict()
    for rec in sorted(entries, key=lambda r: r["type"]):
        h = hist.setdefault(rec["type"], {"count": 0, "bytes": 0, "zlib": 0})
        h["count"] += 1
        h["bytes"] += rec["size"]
        h["zlib"] += rec["zlib"]
    index = {
        "level": level,
        "files": [n for n, _ in files],
        "skipped": skipped,
        "entry_count": len(entries),
        "histogram": hist,
        "entries": entries,
    }
    (level_dir / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    if analyse:
        doc = {"level": level, "max_bytes": max_bytes, "entries": analysis}
        (level_dir / "analysis.json").write_text(json.dumps(doc, indent=1), encoding="utf-8")
    return index


def dir_level_files(directory):
    """SRSC-capable files directly inside a directory (not recursive)."""
    out = []
    for p in sorted(Path(directory).iterdir()):
        if p.is_file() and p.suffix.lower() in SRSC_EXTS:
            out.append((p.name, p.read_bytes()))
    return out


def iso_level_files(iso_path, lang="ENGLISH", wanted=None):
    """Group the disc's level folders by level name: {LEVEL: [(file name, bytes), ...]}.

    `wanted` is an iterable of level names (upper case) or None for every level.
    """
    f, files = ui_layout_dump.iso_files(str(iso_path))
    groups = collections.defaultdict(list)
    try:
        for path in sorted(files):
            parts = path.strip("/").split("/")
            if len(parts) < 4 or parts[0].upper() != lang.upper() or parts[1].upper() not in LEVEL_FOLDERS:
                continue
            name = parts[-1]
            if os.path.splitext(name)[1].lower() not in SRSC_EXTS:
                continue
            level = parts[-2].upper()
            if wanted is not None and level not in wanted:
                continue
            groups[level].append((name, ui_layout_dump.read(f, files[path])))
    finally:
        f.close()
    return dict(groups)


def print_histogram(levels_hist):
    total = collections.Counter()
    total_bytes = collections.Counter()
    for level, hist in levels_hist.items():
        print("%s: %d entries" % (level, sum(h["count"] for h in hist.values())))
        for typ, h in hist.items():
            total[typ] += h["count"]
            total_bytes[typ] += h["bytes"]
            print("  %s  count %5d  bytes %10d  zlib %4d" % (typ, h["count"], h["bytes"], h["zlib"]))
    if len(levels_hist) > 1:
        print("all levels:")
        for typ in sorted(total):
            print("  %s  count %5d  bytes %10d" % (typ, total[typ], total_bytes[typ]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--dir", type=Path, help="extracted level folder (files directly inside it)")
    src.add_argument("--iso", type=Path, help="disc image (.iso)")
    ap.add_argument("--level-name", help="level name for --dir (default: the directory name)")
    ap.add_argument("--level", action="append", default=[], help="ISO level folder to dump (repeatable)")
    ap.add_argument("--all", action="store_true", help="every level folder on the disc")
    ap.add_argument("--lang", default="ENGLISH", help="language folder on the disc (default ENGLISH)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output root (default build/lvl-dump)")
    ap.add_argument("--analyse", action="store_true", help="write analysis.json (float runs, strides, strings, u32)")
    ap.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="bytes per entry the analysis reads")
    args = ap.parse_args(argv)

    levels = {}
    if args.dir:
        files = dir_level_files(args.dir)
        stems = {os.path.splitext(n)[0].upper() for n, _ in files if n.lower().endswith(".lvl")}
        # a level folder holds one <LEVEL>.LVL; fall back to the directory name otherwise
        default = stems.pop() if len(stems) == 1 else args.dir.resolve().name
        levels[(args.level_name or default).upper()] = files
    else:
        if not (args.level or args.all):
            ap.error("--iso needs --level NAME (repeatable) or --all")
        wanted = None if args.all else {n.upper() for n in args.level}
        levels = iso_level_files(args.iso, lang=args.lang, wanted=wanted)
        missing = (wanted or set()) - set(levels)
        if missing:
            ap.error("no such level folder on the disc: " + ", ".join(sorted(missing)))

    levels_hist = {}
    for level in sorted(levels):
        index = dump_level(level, levels[level], args.out, analyse=args.analyse, max_bytes=args.max_bytes)
        levels_hist[level] = index["histogram"]
        print(
            "%s: %d files, %d SRSC entries%s -> %s"
            % (
                level,
                len(index["files"]) - len(index["skipped"]),
                index["entry_count"],
                ("; skipped " + ", ".join(s["file"] for s in index["skipped"])) if index["skipped"] else "",
                Path(args.out) / level,
            )
        )
    print_histogram(levels_hist)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
