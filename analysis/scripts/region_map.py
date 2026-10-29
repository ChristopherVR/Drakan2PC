#!/usr/bin/env python3
"""Map the PAL SCES_500.06 executable onto another region's executable (USA SCUS_971.28).

Reads two owned ELFs plus their Ghidra function lists and writes public address metadata only
(no code bytes):

  <out>/functions.csv      the region's function list: Ghidra's list, split so that every function
                           matched to a PAL function starts where the PAL one does
  <out>/function_map.csv   pal_start,region_start,size,kind (exact | neighbour | fuzzy)
  <out>/data_map.csv       pal_addr,region_addr,refs   (absolute data references paired inside
                           exactly matched functions; one row per PAL address, majority target)

Matching normalises every instruction by masking what moves between builds (jump targets, lui
halves and the low 16 bits of immediates / memory offsets that are not $sp relative), then looks
for the PAL function's whole normalised body in the other image. Short or repeated bodies are
resolved by the neighbouring functions' delta; the rest are paired by opcode similarity inside the
gap their mapped neighbours leave. See docs/reference/regions.md.

Run with:  python -I analysis/scripts/region_map.py --pal <SCES_500.06> --region <SCUS_971.28>
           --pal-functions analysis/functions.csv --region-functions <ghidra functions.csv>
           --out analysis/regions/usa
"""

from __future__ import annotations

import argparse
import bisect
import csv
import difflib
import struct
from collections import defaultdict
from pathlib import Path

IMM_OPS = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x14, 0x15, 0x16, 0x17}
MEM_OPS = set(range(0x18, 0x40)) - {0x1C}
K = 6


def sections(data: bytes) -> dict[str, tuple[int, int, int, int]]:
    shoff = struct.unpack_from("<I", data, 0x20)[0]
    shnum, shstr = struct.unpack_from("<HH", data, 0x30)
    raw = [struct.unpack_from("<10I", data, shoff + 40 * i) for i in range(shnum)]
    names = raw[shstr][4]
    out = {}
    for s in raw:
        name = data[names + s[0] : data.index(b"\0", names + s[0])].decode("latin-1")
        out[name] = (s[3], s[4], s[5], s[1])  # addr, offset, size, type
    return out


def norm(word: int) -> int:
    op = word >> 26
    if op in (2, 3):
        return op << 26
    if op == 0x0F:
        return word & 0xFFFF0000
    if op in IMM_OPS or op in MEM_OPS:
        if (word >> 21) & 31 == 29:
            return word
        return word & 0xFFFF0000
    return word


class Image:
    def __init__(self, path: Path, functions: Path):
        data = path.read_bytes()
        addr, off, size, _ = sections(data)[".text"]
        self.base = addr
        self.words = struct.unpack_from("<%dI" % (size // 4), data, off)
        self.norm = [norm(w) for w in self.words]
        self.funcs: dict[int, int] = {}
        with functions.open(newline="") as f:
            for row in csv.DictReader(f):
                start, end = int(row["start_addr_hex"], 16), int(row["end_addr_hex"], 16)
                if addr <= start < addr + size:
                    self.funcs[start] = end
        self.rows = functions

    def body(self, start: int, end: int) -> list[int]:
        i = (start - self.base) // 4
        return self.norm[i : i + max(1, (end - start) // 4)]

    def word(self, addr: int) -> int:
        return self.words[(addr - self.base) // 4]


def match(pal: Image, reg: Image) -> dict[int, tuple[int, str]]:
    index: dict[tuple, list[int]] = defaultdict(list)
    for i in range(len(reg.norm) - K):
        index[tuple(reg.norm[i : i + K])].append(i)
    mapping: dict[int, tuple[int, str]] = {}
    ambiguous: dict[int, set[int]] = {}
    for start, end in pal.funcs.items():
        seq = pal.body(start, end)
        if len(seq) < K:
            continue
        hits = [j for j in index.get(tuple(seq[:K]), []) if reg.norm[j : j + len(seq)] == seq]
        if len(hits) == 1:
            mapping[start] = (reg.base + hits[0] * 4, "exact")
        elif hits:
            ambiguous[start] = {reg.base + j * 4 for j in hits}
    order = sorted(pal.funcs)
    changed = True
    while changed:
        changed = False
        for idx, start in enumerate(order):
            if start in mapping:
                continue
            seq = pal.body(start, pal.funcs[start])
            deltas = []
            for step in (-1, 1):
                j = idx + step
                while 0 <= j < len(order) and order[j] not in mapping:
                    j += step
                if 0 <= j < len(order):
                    deltas.append(mapping[order[j]][0] - order[j])
            for delta in dict.fromkeys(deltas):
                target = start + delta
                if start in ambiguous and target not in ambiguous[start]:
                    continue
                i = (target - reg.base) // 4
                if 0 <= i and reg.norm[i : i + len(seq)] == seq:
                    mapping[start] = (target, "neighbour")
                    changed = True
                    break
    # call sites: a jal in a matched body names the callee in both images
    calls: dict[int, dict[int, int]] = defaultdict(lambda: defaultdict(int))
    for start, (target, _) in list(mapping.items()):
        for k in range(0, pal.funcs[start] - start, 4):
            a = pal.word(start + k)
            if a >> 26 == 3:
                calls[(a & 0x3FFFFFF) << 2][(reg.word(target + k) & 0x3FFFFFF) << 2] += 1
    for callee, targets in calls.items():
        if callee in mapping or callee not in pal.funcs or len(targets) != 1:
            continue
        mapping[callee] = (next(iter(targets)), "callsite")
    # fuzzy: pair the remaining functions with unclaimed region functions inside the gap
    claimed = {v[0] for v in mapping.values()}
    rstarts = sorted(reg.funcs)
    for idx, start in enumerate(order):
        if start in mapping:
            continue
        lo = hi = None
        for j in range(idx - 1, -1, -1):
            if order[j] in mapping:
                lo = mapping[order[j]][0]
                break
        for j in range(idx + 1, len(order)):
            if order[j] in mapping:
                hi = mapping[order[j]][0]
                break
        if lo is None or hi is None:
            continue
        a, b = bisect.bisect_right(rstarts, lo), bisect.bisect_left(rstarts, hi)
        seq = [w >> 26 for w in pal.body(start, pal.funcs[start])]
        scored = []
        for r in rstarts[a:b]:
            if r in claimed:
                continue
            other = [w >> 26 for w in reg.body(r, reg.funcs[r])]
            ratio = difflib.SequenceMatcher(None, seq, other, autojunk=False).ratio()
            scored.append((ratio, r))
        scored.sort(reverse=True)
        if scored and scored[0][0] >= 0.75 and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.1):
            mapping[start] = (scored[0][1], "fuzzy")
            claimed.add(scored[0][1])
    return mapping


def data_pairs(pal: Image, reg: Image, mapping) -> dict[int, dict[int, int]]:
    pairs: dict[int, dict[int, int]] = defaultdict(lambda: defaultdict(int))
    for start, (target, kind) in mapping.items():
        if kind not in ("exact", "neighbour"):
            continue
        hip, hir = {}, {}
        for k in range(0, pal.funcs[start] - start, 4):
            a, b = pal.word(start + k), reg.word(target + k)
            op, rs, rt = a >> 26, (a >> 21) & 31, (a >> 16) & 31
            if op == 0x0F:
                hip[rt], hir[rt] = (a & 0xFFFF) << 16, (b & 0xFFFF) << 16
            elif (op in IMM_OPS or op in MEM_OPS) and rs in hip and rs != 29:
                if op == 0x0D:
                    lo_a, lo_b = a & 0xFFFF, b & 0xFFFF
                else:
                    lo_a = (a & 0xFFFF) - (0x10000 if a & 0x8000 else 0)
                    lo_b = (b & 0xFFFF) - (0x10000 if b & 0x8000 else 0)
                pa, pr = (hip[rs] + lo_a) & 0xFFFFFFFF, (hir[rs] + lo_b) & 0xFFFFFFFF
                if 0x100000 <= pa < 0x2000000:
                    pairs[pa][pr] += 1
            # forget a register once anything but lui writes it (or a call may have clobbered it)
            if op == 0x0F:
                continue
            written = None
            if op in (0x00, 0x1C):
                written = (a >> 11) & 31
            elif op in IMM_OPS or op in (0x1A, 0x1B, 0x1E) or 0x20 <= op <= 0x27 or op == 0x37:
                written = rt
            elif op == 3 or (op == 0 and a & 0x3F == 9):
                for r in [r for r in hip if not 16 <= r <= 23]:
                    hip.pop(r, None)
                    hir.pop(r, None)
            if written is not None:
                hip.pop(written, None)
                hir.pop(written, None)
    return pairs


def split_functions(reg: Image, starts: set[int]) -> list[tuple[int, int]]:
    funcs = dict(reg.funcs)
    ordered = sorted(funcs)
    for s in sorted(starts):
        if s in funcs:
            continue
        i = bisect.bisect_right(ordered, s) - 1
        if i >= 0 and ordered[i] < s < funcs[ordered[i]]:
            parent = ordered[i]
            funcs[s] = funcs[parent]
            funcs[parent] = s
            ordered.insert(i + 1, s)
    return sorted(funcs.items())


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pal", type=Path, required=True)
    ap.add_argument("--region", type=Path, required=True)
    ap.add_argument("--pal-functions", type=Path, required=True)
    ap.add_argument("--region-functions", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    pal, reg = Image(args.pal, args.pal_functions), Image(args.region, args.region_functions)
    mapping = match(pal, reg)
    args.out.mkdir(parents=True, exist_ok=True)

    names = {}
    with args.region_functions.open(newline="") as f:
        for row in csv.DictReader(f):
            names[int(row["start_addr_hex"], 16)] = row
    with (args.out / "functions.csv").open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["start_addr_hex", "end_addr_hex", "size", "name", "num_callers", "is_thunk"])
        for start, end in split_functions(reg, {v[0] for v in mapping.values()}):
            row = names.get(start, {})
            w.writerow(
                [
                    "%08x" % start,
                    "%08x" % end,
                    end - start,
                    "FUN_%08x" % start,
                    row.get("num_callers", "0"),
                    row.get("is_thunk", "0"),
                ]
            )
    with (args.out / "function_map.csv").open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["pal_start_hex", "region_start_hex", "size", "kind"])
        for start in sorted(mapping):
            target, kind = mapping[start]
            w.writerow(["%08x" % start, "%08x" % target, pal.funcs[start] - start, kind])
    pairs = data_pairs(pal, reg, mapping)
    with (args.out / "data_map.csv").open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["pal_addr_hex", "region_addr_hex", "refs", "conflicting_refs"])
        for pa in sorted(pairs):
            best = max(pairs[pa], key=pairs[pa].get)
            total = sum(pairs[pa].values())
            w.writerow(["%08x" % pa, "%08x" % best, pairs[pa][best], total - pairs[pa][best]])
    kinds = defaultdict(int)
    for _, kind in mapping.values():
        kinds[kind] += 1
    print(f"PAL functions {len(pal.funcs)}, mapped {len(mapping)} {dict(kinds)}; data addresses {len(pairs)}")


if __name__ == "__main__":
    main()
