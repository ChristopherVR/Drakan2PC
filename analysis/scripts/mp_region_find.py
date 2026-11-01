#!/usr/bin/env python3
"""Find the multiplayer / mod hook addresses of another disc region by instruction signatures.

The multiplayer, co-op, level-change and lifecycle hooks use hard-coded SCES-500.06 (PAL) guest addresses. A new
disc needs a verified PAL -> region pair for each of them (configs/regions/<id>.json "addresses"). This tool
produces *candidates* for a human to audit; it never writes the config itself.

Two steps, both read the player's own ELFs at analysis time (extract with scripts/buildtool/prepare_iso.py
--output-directory build/<dir>); nothing derived from code bytes is written except addresses and counts:

  inventory   scan the runtime sources for every PAL guest address (0x100000..0x5fffff) with the constant's name,
              classify it against analysis/functions.csv (function start / inside a function / data) and print JSON.
  find        for each inventory address build a relocation-masked signature from the PAL ELF and search the
              target ELF for it:
                * function start     the first 6..32 normalised instructions of the function
                * inside a function  a window around the address, clipped to the function
                * data address       windows around PAL instructions that reference it (lui + lo16 pairs); the
                                     target address is decoded from the matching instruction in the target ELF and
                                     the references vote
              A candidate is reported only when its signature occurs exactly once in the target .text. Status:
                unique     one signature, one hit, (data: all voting references agree)
                voted      data only: several references agree, some disagree (majority listed)
                ambiguous  the signature occurs more than once
                missing    nothing matched (the code differs in this build, or the address is not referenced)
              Normalisation matches analysis/scripts/region_map.py (jump targets, lui halves and non-$sp
              16-bit immediates masked).

Typical use with a USA disc:
  python -I analysis/scripts/mp_region_find.py find --pal build/final-game/SCES_500.06 \
      --target build/usa-game/SCUS_971.28 --out build/mp_usa_candidates.json
  python -I analysis/scripts/mp_region_find.py table --candidates build/mp_usa_candidates.json
Then audit each row (disassemble both sides), add it to configs/regions/<id>.json and run scripts/tests/test_regions.py.
`--check-map analysis/regions/usa` compares the finder with the whole-function matcher (region_map.py) as a
cross-check.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import region_map as rm

ROOT = HERE.parent.parent
LIB = ROOT / "ps2recomp" / "ps2xRuntime" / "src" / "lib"
# Sources whose hard-coded addresses belong to the multiplayer / mod feature set.
MP_SOURCES = [
    LIB / "drakan_arena.cpp",
    LIB / "drakan_coop.cpp",
    LIB / "drakan_coop_character.cpp",
    LIB / "drakan_pvp.cpp",
    LIB / "drakan_ghost.cpp",
    LIB / "drakan_netplay.cpp",
    LIB / "drakan_world.cpp",
    LIB / "drakan_levelchange.cpp",
    LIB / "drakan_lifecycle.cpp",
    LIB / "drakan_foliage.cpp",
    LIB / "drakan_input_glue.cpp",
    ROOT / "runtime" / "world" / "include" / "world" / "Addresses.h",
]
ADDR_RE = re.compile(r"\b0x([1-5][0-9a-fA-F]{5})[uUlL]*\b")
NAME_RE = re.compile(r"\b(k[A-Z]\w*|g\.\w+|\w+)\s*=\s*0x([1-5][0-9a-fA-F]{5})\b")
BUILD_ASM = ".text"


def inventory(functions_csv: Path) -> list[dict]:
    funcs = []
    with functions_csv.open(newline="") as f:
        for row in csv.DictReader(f):
            funcs.append((int(row["start_addr_hex"], 16), int(row["end_addr_hex"], 16)))
    funcs.sort()
    starts = [s for s, _ in funcs]
    found: dict[int, dict] = {}
    for src in MP_SOURCES:
        if not src.exists():
            continue
        rel = src.relative_to(ROOT).as_posix()
        for no, line in enumerate(src.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("//")[0]
            names = {int(m.group(2), 16): m.group(1) for m in NAME_RE.finditer(code)}
            for m in ADDR_RE.finditer(code):
                a = int(m.group(1), 16)
                e = found.setdefault(a, {"pal": "%08x" % a, "name": "", "uses": []})
                if a in names and not e["name"]:
                    e["name"] = names[a]
                e["uses"].append("%s:%d" % (rel, no))
    out = []
    for a in sorted(found):
        e = found[a]
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and funcs[i][0] <= a < funcs[i][1]:
            e["kind"] = "code"
            e["function"] = "%08x" % funcs[i][0]
            e["offset"] = a - funcs[i][0]
        else:
            e["kind"] = "data"
        e["uses"] = e["uses"][:4]
        out.append(e)
    return out


class Elf:
    def __init__(self, path: Path, functions: Path | None):
        raw = path.read_bytes()
        addr, off, size, _ = rm.sections(raw)[BUILD_ASM]
        self.base, self.size = addr, size
        self.words = struct.unpack_from("<%dI" % (size // 4), raw, off)
        self.norm = [rm.norm(w) for w in self.words]
        self.blob = struct.pack("<%dI" % len(self.norm), *self.norm)
        self.funcs: dict[int, int] = {}
        if functions:
            with functions.open(newline="") as f:
                for row in csv.DictReader(f):
                    s, e = int(row["start_addr_hex"], 16), int(row["end_addr_hex"], 16)
                    if addr <= s < addr + size:
                        self.funcs[s] = e
        self.starts = sorted(self.funcs)

    def index(self, a: int) -> int:
        return (a - self.base) // 4

    def in_text(self, a: int) -> bool:
        return self.base <= a < self.base + self.size

    def function_of(self, a: int):
        i = bisect.bisect_right(self.starts, a) - 1
        if i >= 0 and self.starts[i] <= a < self.funcs[self.starts[i]]:
            return self.starts[i], self.funcs[self.starts[i]]
        return None

    def search(self, seq: list[int]) -> list[int]:
        """All word indices where the normalised sequence occurs."""
        pat = struct.pack("<%dI" % len(seq), *seq)
        hits, pos = [], 0
        while True:
            p = self.blob.find(pat, pos)
            if p < 0:
                return hits
            if p % 4 == 0:
                hits.append(p // 4)
            pos = p + 1
            if len(hits) > 8:
                return hits


def window_search(pal: Elf, tgt: Elf, lo: int, hi: int, centre: int, widths) -> tuple[str, list[int], int, int]:
    """Search growing then shrinking windows around word index `centre` inside [lo, hi). Returns
    (status, hits, window_start_index, width)."""
    for w in widths:
        a, b = max(lo, centre - w), min(hi, centre + w + 1)
        if b - a < 6:
            continue
        hits = tgt.search(pal.norm[a:b])
        if len(hits) == 1:
            return "unique", hits, a, b - a
        if hits:
            last = ("ambiguous", hits, a, b - a)
            # a wider window may disambiguate; keep trying
            continue
    try:
        return last  # type: ignore[name-defined]
    except NameError:
        return "missing", [], 0, 0


def lo16(w: int, signed: bool) -> int:
    v = w & 0xFFFF
    return v - 0x10000 if signed and v & 0x8000 else v


def decode_refs(elf: Elf, i_use: int):
    """Absolute address formed by the use instruction at word index i_use (needs a lui of its base register within
    the previous 12 instructions in the same straight line), plus the lui's index."""
    w = elf.words[i_use]
    op, rs = w >> 26, (w >> 21) & 31
    if rs == 29 or not (op in rm.IMM_OPS or op in rm.MEM_OPS) or op == 0x0F:
        return None
    for k in range(i_use - 1, max(-1, i_use - 13), -1):
        p = elf.words[k]
        pop, prt = p >> 26, (p >> 16) & 31
        if pop == 0x0F and prt == rs:
            hi = (p & 0xFFFF) << 16
            lo = lo16(w, op != 0x0D)
            return (hi + lo) & 0xFFFFFFFF, k
        # the base register is overwritten, or control flow leaves the straight line: no pair
        if pop in (2, 3) or (pop == 0 and p & 0x3F in (8, 9)) or pop in (4, 5, 6, 7, 1):
            return None
        wrote = None
        if pop in (0x00, 0x1C):
            wrote = (p >> 11) & 31
        elif pop in rm.IMM_OPS or 0x20 <= pop <= 0x27 or pop == 0x37:
            wrote = prt
        if wrote == rs:
            return None
    return None


def data_references(pal: Elf, wanted: set[int]) -> dict[int, list[int]]:
    refs: dict[int, list[int]] = defaultdict(list)
    for j in range(len(pal.words)):
        r = decode_refs(pal, j)
        if r and r[0] in wanted:
            refs[r[0]].append(j)
    return refs


def find_code(pal: Elf, tgt: Elf, item: dict) -> dict:
    a = int(item["pal"], 16)
    fn = pal.function_of(a)
    out = {"status": "missing"}
    if not fn:
        return out
    start, end = fn
    n = (end - start) // 4
    if a == start:
        for w in (32, 16, 10, 6):
            w = min(w, n)
            if w < 6:
                out["note"] = "function shorter than 6 instructions"
                return out
            hits = tgt.search(pal.norm[pal.index(start) : pal.index(start) + w])
            if len(hits) == 1:
                out.update(status="unique", target="%08x" % (tgt.base + hits[0] * 4), window=w)
                return out
            if hits:
                out.update(status="ambiguous", candidates=len(hits), window=w)
                if w >= n:
                    break
        return out
    centre = pal.index(a)
    status, hits, a0, width = window_search(pal, tgt, pal.index(start), pal.index(end), centre, (12, 8, 6, 4))
    if status == "unique":
        t = tgt.base + (hits[0] + (centre - a0)) * 4
        out.update(status="unique", target="%08x" % t, window=width, inside_function=True)
    elif status == "ambiguous":
        out.update(status="ambiguous", candidates=len(hits), window=width)
    return out


def find_data(pal: Elf, tgt: Elf, sites: list[int]) -> dict:
    votes: Counter = Counter()
    for j in sites[:40]:
        fn = pal.function_of(pal.base + j * 4)
        if not fn:
            continue
        status, hits, a0, width = window_search(pal, tgt, pal.index(fn[0]), pal.index(fn[1]), j, (10, 6))
        if status != "unique":
            continue
        t = decode_refs(tgt, hits[0] + (j - a0))
        if t:
            votes[t[0]] += 1
    out = {"status": "missing", "references": len(sites)}
    if not votes:
        return out
    (best, n), total = votes.most_common(1)[0], sum(votes.values())
    out.update(target="%08x" % best, agree=n, matched_references=total)
    out["status"] = "unique" if n == total else "voted"
    if n == total and n == 1:
        out["note"] = "single reference"
    return out


def run_find(args) -> dict:
    pal = Elf(args.pal, args.pal_functions)
    tgt = Elf(args.target, args.target_functions)
    items = inventory(args.pal_functions)
    data_addrs = {int(i["pal"], 16) for i in items if i["kind"] == "data"}
    refs = data_references(pal, data_addrs)
    rows = []
    for it in items:
        a = int(it["pal"], 16)
        res = find_code(pal, tgt, it) if it["kind"] == "code" else find_data(pal, tgt, refs.get(a, []))
        rows.append({**it, **res})
    return {
        "pal_text": [pal.base, pal.size],
        "target_text": [tgt.base, tgt.size],
        "summary": dict(Counter(r["status"] for r in rows)),
        "rows": rows,
    }


def check_map(rows: list[dict], map_dir: Path) -> dict:
    fm = {}
    with (map_dir / "function_map.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            fm[int(r["pal_start_hex"], 16)] = (int(r["region_start_hex"], 16), r["kind"])
    dm = {}
    with (map_dir / "data_map.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            dm[int(r["pal_addr_hex"], 16)] = int(r["region_addr_hex"], 16)
    agree = disagree = nomap = 0
    for r in rows:
        r.pop("map", None)
        if r["status"] not in ("unique", "voted"):
            continue
        a = int(r["pal"], 16)
        if r["kind"] == "data":
            expect = dm.get(a)
        elif r.get("inside_function"):
            f = fm.get(int(r["function"], 16))
            expect = f[0] + r["offset"] if f else None
        else:
            f = fm.get(a)
            expect = f[0] if f else None
        if expect is None:
            nomap += 1
            r["map"] = "none"
        elif expect == int(r["target"], 16):
            agree += 1
            r["map"] = "agrees"
        else:
            disagree += 1
            r["map"] = "differs %08x" % expect
    return {"agree": agree, "disagree": disagree, "no_map_entry": nomap}


def print_table(doc: dict) -> None:
    print("| PAL | kind | name | function+off | status | candidate | notes |")
    print("| --- | --- | --- | --- | --- | --- | --- |")
    for r in doc["rows"]:
        where = "%s+0x%x" % (r["function"], r["offset"]) if r["kind"] == "code" else ""
        notes = r.get("map", "")
        if r.get("agree"):
            notes += " %d/%d refs" % (r["agree"], r["matched_references"])
        print(
            "| %s | %s | %s | %s | %s | %s | %s |"
            % (r["pal"], r["kind"], r["name"], where, r["status"], r.get("target", ""), notes.strip())
        )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("inventory")
    p.add_argument("--pal-functions", type=Path, default=ROOT / "analysis" / "functions.csv")
    p = sub.add_parser("find")
    p.add_argument("--pal", type=Path, required=True)
    p.add_argument("--target", type=Path, required=True)
    p.add_argument("--pal-functions", type=Path, default=ROOT / "analysis" / "functions.csv")
    p.add_argument(
        "--target-functions", type=Path, help="optional (Ghidra) function list of the target; unused for matching"
    )
    p.add_argument("--check-map", type=Path, help="analysis/regions/<id> directory to cross-check against")
    p.add_argument("--out", type=Path)
    p = sub.add_parser("table")
    p.add_argument("--candidates", type=Path, required=True)
    args = ap.parse_args()
    if args.cmd == "inventory":
        print(json.dumps(inventory(args.pal_functions), indent=1))
    elif args.cmd == "find":
        doc = run_find(args)
        if args.check_map:
            doc["map_check"] = check_map(doc["rows"], args.check_map)
        text = json.dumps(doc, indent=1)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(text + "\n")
        print(json.dumps({k: v for k, v in doc.items() if k != "rows"}))
    else:
        print_table(json.loads(args.candidates.read_text()))


if __name__ == "__main__":
    main()
