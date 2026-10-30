#!/usr/bin/env python3
"""Translate the validated PAL recompiler config (configs/drakan.toml) to another region.

Uses the function map written by region_map.py. Every `name@0xADDR` in `stubs` / `entry_points`,
every `[mmio]` instruction address and every `sub_XXXXXXXX` in `[performance]` is translated:

* the PAL function is mapped "exact" / "neighbour" (identical normalised body) -> same offset inside
  the region's function;
* mapped "callsite" / "fuzzy" (body differs, only the entry is known) -> only the entry address;
* config_overrides.json beside the maps -> hand-checked region address;
* otherwise the entry must be listed in allowed_missing, or generation fails.

No ELF is read; the output contains public address metadata only.

Run with:  python -I analysis/scripts/region_config.py --map analysis/regions/usa
           --pal-functions analysis/functions.csv --template configs/drakan.toml
           --boot SCUS_971.28 --out configs/drakan-usa.toml
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import os
import re
from pathlib import Path


def load_map(directory: Path):
    exact, entry = {}, {}
    with (directory / "function_map.csv").open(newline="") as f:
        for row in csv.DictReader(f):
            pal, reg = int(row["pal_start_hex"], 16), int(row["region_start_hex"], 16)
            entry[pal] = reg
            if row["kind"] in ("exact", "neighbour"):
                exact[pal] = (reg, int(row["size"]))
    return exact, entry


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", type=Path, required=True, help="directory with region_map.py output")
    ap.add_argument("--pal-functions", type=Path, required=True)
    ap.add_argument("--template", type=Path, required=True)
    ap.add_argument("--region", help="profile id (defaults to the map directory name)")
    ap.add_argument("--boot", required=True)
    ap.add_argument("--functions-csv", help="path in generated config (defaults to the map's functions.csv)")
    ap.add_argument("--jump-tables-csv", help="path in generated config (defaults to the map's jumptables.csv)")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    args.region = args.region or args.map.name
    args.functions_csv = (
        args.functions_csv or Path(os.path.relpath(args.map / "functions.csv", args.out.parent)).as_posix()
    )
    args.jump_tables_csv = (
        args.jump_tables_csv or Path(os.path.relpath(args.map / "jumptables.csv", args.out.parent)).as_posix()
    )
    exact, entry = load_map(args.map)
    overrides_path = args.map / "config_overrides.json"
    overrides = json.loads(overrides_path.read_text(encoding="utf-8")) if overrides_path.exists() else {}
    manual = {int(a, 0): int(b, 0) for a, b in overrides.get("manual_addresses", {}).items()}
    pal_funcs = []
    with args.pal_functions.open(newline="") as f:
        for row in csv.DictReader(f):
            pal_funcs.append((int(row["start_addr_hex"], 16), int(row["end_addr_hex"], 16)))
    pal_funcs.sort()
    starts = [s for s, _ in pal_funcs]

    def translate(addr: int, need_entry: bool) -> int | None:
        if addr in manual:
            return manual[addr]
        if addr in entry and (need_entry or addr in exact):
            return entry[addr]
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and pal_funcs[i][0] <= addr < pal_funcs[i][1] and pal_funcs[i][0] in exact:
            return exact[pal_funcs[i][0]][0] + addr - pal_funcs[i][0]
        return None

    text = args.template.read_text(encoding="utf-8")
    dropped = []

    def named(m: re.Match) -> str:
        name, addr = m[1], int(m[2], 16)
        new = translate(addr, True)
        if new is None:
            dropped.append(f"{name}@0x{addr:08X}")
            return "\x00"
        return f'"{name}@0x{new:08X}"'

    text = re.sub(r'"([^"@\n]+)@0x([0-9A-Fa-f]{8})"', named, text)
    text = re.sub(r"(?m)^[ \t]*\x00,?[ \t]*\n", "", text)

    def mmio(m: re.Match) -> str:
        addr = int(m[1], 16)
        new = translate(addr, False)
        if new is None:
            dropped.append(f"mmio 0x{addr:x}")
            return ""
        return f'"0x{new:x}" = "{m[2]}"\n'

    text = re.sub(r'(?m)^"0x([0-9a-f]+)" = "(0x[0-9a-f]+)"\n', mmio, text)

    def perf(m: re.Match) -> str:
        addr = int(m[1], 16)
        new = translate(addr, True)
        if new is None:
            dropped.append(f"performance sub_{addr:08X}")
            return ""
        return f"{m[0][: m.start(1) - m.start(0)]}{new:08X}{m[0][m.end(1) - m.start(0) :]}"

    text = re.sub(r'(?m)^  "sub_([0-9A-F]{8})",[^\n]*\n', perf, text)
    unexpected = set(dropped) - set(overrides.get("allowed_missing", []))
    if unexpected:
        ap.exit(1, "Unverified missing mappings (output not written): " + ", ".join(sorted(unexpected)) + "\n")
    header = (
        f"# PS2Recomp configuration for: {args.boot} ({args.region.upper()})\n"
        f"# Generated from {args.template.as_posix()} by analysis/scripts/region_config.py; do not edit by hand.\n"
        "# Public address/stub metadata only; scripts/buildtool/prepare_iso.py resolves the private paths.\n"
    )
    body = text.split("\n[general]", 1)[1] if "\n[general]" in text else text
    body = "\n[general]" + body
    body = re.sub(r'(?m)^input = "[^"]*"', f'input = "../build/local-game/{args.boot}"', body)
    body = re.sub(r'(?m)^functions_csv = "[^"]*"', f'functions_csv = "{args.functions_csv}"', body)
    body = re.sub(r'(?m)^jump_tables_csv = "[^"]*"', f'jump_tables_csv = "{args.jump_tables_csv}"', body)
    args.out.write_text(header + body, encoding="utf-8", newline="\n")
    print(f"wrote {args.out}; dropped {len(dropped)}: {', '.join(dropped)}")


if __name__ == "__main__":
    main()
