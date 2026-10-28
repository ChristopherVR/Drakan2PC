"""Offline Map probe preparation/checking. Never launches or modifies the game.

Geometry certificates are reviewed evidence, not an automatic identity proof:
the frozen hook logs identity but omits corners. See docs/journal/2026-10-18/map-mouse-live-probe.md.
"""

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path


def snapshot(path):
    data = Path(path).read_bytes()
    if len(data) != 0x2000000:
        raise ValueError("expected 32 MiB RAM dump")

    def u(address):
        if not 0x100000 <= address <= len(data) - 4:
            raise ValueError(f"invalid guest address {address:x}")
        return struct.unpack_from("<I", data, address)[0]

    screen = u(0x4FB60C)
    if u(screen + 0x18) != 21 or u(screen + 0x154) != 3:
        raise ValueError("native inventory Map section is not active")
    if u(screen + 0x3C) or u(screen + 0x158) != 0x3F800000 or u(screen + 0x15C):
        raise ValueError("Map is modal or transitioning")
    if u(screen + 0x1B0) != 5:
        raise ValueError("unexpected section table")
    section = u(u(screen + 0x1A8) + 12)
    if u(section + 0x10) != screen:
        raise ValueError("section owner mismatch")
    table, count = u(0x4FBB70), u(0x4FBB78)
    if not 0 < count <= 256:
        raise ValueError("marker count outside native mouse bound")
    records = [[u(table + i * 32 + j) for j in (0, 4, 0x10, 0x14, 0x18, 0x1C)] for i in range(count)]
    selected = u(section + 0x6C)
    if selected >= count:
        raise ValueError("selected index outside marker table")
    return dict(
        screen=screen,
        section=section,
        table=table,
        count=count,
        region=u(section + 0x70),
        selected=selected,
        records=records,
        sha256=hashlib.sha256(data).hexdigest(),
    )


def certificate(path):
    c = json.loads(Path(path).read_text())
    if c.get("reviewed_identity_and_complete_visible_geometry") is not True:
        raise ValueError("manual identity/complete visible geometry review required")
    if not c.get("evidence") or not c.get("exe_sha256") or not c.get("ini_sha256"):
        raise ValueError("evidence paths and executable/INI hashes required")
    if c["aspect"] not in ("21:9", "32:9"):
        raise ValueError("unsupported probe aspect")
    for marker in c["markers"]:
        if not marker.get("raw_draw") or not marker.get("identity_log"):
            raise ValueError("each marker needs raw draw and hook identity references")
        l, t, r, b = marker["rect"]
        if not all(math.isfinite(x) for x in (l, t, r, b)) or not (0 <= l < r <= 512 and 0 <= t < b <= 512):
            raise ValueError("invalid native framebuffer marker rectangle")
    return c


def recipe(c):
    markers = c["markers"]
    targets = [m for m in markers if m["index"] == c["target"]]
    if len(targets) != 1:
        raise ValueError("target needs exactly one reviewed rectangle")
    l, t, r, b = targets[0]["rect"]
    target = ((l + r) / 2, (t + b) / 2)
    empty = c["empty"]
    for point, expected in ((target, 1), (empty, 0)):
        hits = sum(a <= point[0] <= d and e <= point[1] <= f for a, e, d, f in (m["rect"] for m in markers))
        if hits != expected:
            raise ValueError("overlapping target or nonempty empty-space point")
    vx, vy, vw, vh = c["view"]
    scale = c["page_scale_x"]
    if not all(math.isfinite(x) for x in (*c["view"], scale, *empty)) or not (vw > 0 and vh > 0 and 0 < scale <= 1):
        raise ValueError("invalid presenter viewport/page scale")

    def cursor(point):
        x, y = round(vx + point[0] / 512 * scale * vw), round(vy + point[1] / 512 * vh)
        if not (0 <= x < 640 and 0 <= y < 512):
            raise ValueError("cursor outside virtual client")
        return f"{x} {y}"

    # Existing early-save route. Times are milliseconds after sync; calibrate first.
    lines = [
        "clock milliseconds",
        "0 sync play 90",
        "1000 key tap TAB",
        "3000 key tap ENTER",
        "4500 key tap ENTER",
        "5500 key tap PGDN",
        "7500 key tap ENTER",
        "8500 key tap PGDN",
        "10500 key tap ENTER",
        "11500 key tap PGDN",
        "13500 key tap ENTER",
        "14500 shot map_before",
        "14700 dump ram_map_before",
        f"15000 cursor {cursor(target)}",
        "15200 button down 1",
        "15250 button up 1",
    ]
    # More than 771 updates at 60 Hz, retaining a stationary pointer throughout.
    lines += [
        "30000 shot map_marker",
        "30200 dump ram_map_marker",
        f"30500 cursor {cursor(empty)}",
        "30700 button down 1",
        "30750 button up 1",
        "32500 shot map_empty",
        "32700 dump ram_map_empty",
        "33500 key tap TAB",
        "35000 quit",
    ]
    return "\n".join(lines) + "\n"


def check(directory, c):
    s = [snapshot(Path(directory) / f"ram_map_{name}.ram") for name in ("before", "marker", "empty")]
    before, marker, empty = s
    identity = lambda x: [x[k] for k in ("screen", "section", "table", "count", "region", "records")]
    checks = {
        "stable_native_map_identity": identity(before) == identity(marker) == identity(empty),
        "target_in_table": 0 <= c["target"] < before["count"],
        "native_selected_target": marker["selected"] == c["target"],
        "empty_space_preserves_selection": empty["selected"] == marker["selected"],
    }
    changed = before["selected"] != c["target"]
    return dict(
        checks=checks,
        snapshots=s,
        changed_selection_exercised=changed,
        status="PASS" if all(checks.values()) and changed else "LIMITED" if all(checks.values()) else "FAIL",
        limits=[
            "RAM proves selection only; native draw freshness/dynamic eligibility require live review.",
            "No full-map or image-only branch coverage claim; no Cross activation exists for Map.",
        ],
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="mode", required=True)
    inspect = sub.add_parser("inspect")
    inspect.add_argument("ram")
    make = sub.add_parser("recipe")
    make.add_argument("certificate")
    make.add_argument("output")
    test = sub.add_parser("check")
    test.add_argument("certificate")
    test.add_argument("directory")
    a = p.parse_args()
    if a.mode == "inspect":
        print(json.dumps(snapshot(a.ram), indent=2))
        return
    c = certificate(a.certificate)
    if a.mode == "recipe":
        Path(a.output).write_text(recipe(c))
        return
    result = check(a.directory, c)
    print(json.dumps(result, indent=2))
    raise SystemExit(1 if result["status"] == "FAIL" else 0)


if __name__ == "__main__":
    main()
