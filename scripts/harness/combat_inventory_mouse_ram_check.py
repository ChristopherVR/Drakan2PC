"""Read-only assertions for combat_inventory_mouse_recipe.py dumps (32 MiB each).
Unavailable or malformed snapshots fail individually; never infer an equip from
callbacks. Requires fixture dagger(resource1000e) in cell0, bow(1004f) in cell1,
and empty slot7. Native dagger equip uses ED0; bow uses EAC. Never accept the
expected handle merely appearing in either arbitrary channel. Originals untouched.
"""

import argparse
import json
import pathlib
import struct

p = argparse.ArgumentParser()
p.add_argument("directory", type=pathlib.Path)
a = p.parse_args()
results = []
snap = {}
for name in ["before", "item0", "empty", "item1"]:
    try:
        path = a.directory / f"ram_mouse_{name}.ram"
        b = path.read_bytes()
        if len(b) != 0x2000000:
            raise ValueError("expected 32 MiB")

        def u(x):
            if not 0x100000 <= x <= len(b) - 4:
                raise ValueError(f"invalid address {x:x}")
            return struct.unpack_from("<I", b, x)[0]

        s = u(0x4FB60C)
        if u(s + 0x18) != 21 or u(s + 0x154) != 0:
            raise ValueError("Equipment screen21 not active")
        r = u(s + 0x14C)
        c = r + 0x584
        ctx = u(0x5565D4)
        if u(c + 0x24) != 0:
            raise ValueError("Equipment controller not active")
        cells = [u(c + 0x38 + i * 16) for i in range(24)]
        n = u(ctx + 0x84)
        table = u(ctx + 0x7C)
        channels = {}
        for channel, offset in [("EAC", 0xEAC), ("ED0", 0xED0)]:
            ref = struct.unpack_from("<H", b, r + offset)[0]
            if ref > n:
                raise ValueError(f"{channel} actor reference {ref} exceeds registry count {n}")
            handle = u(table + (ref - 1) * 8) if ref else None
            channels[channel] = {"ref": ref, "handle": handle}
        resources = u(ctx + 0x320)

        def resource_class(handle):
            if handle <= 0x53:
                return None
            index = struct.unpack_from("<H", b, handle + 0x10)[0]
            if index == 0xFFFF or index > u(resources + 0x10):
                return None
            return u(u(resources + 8) + index * 0x24 + 8)

        classes = [resource_class(handle) for handle in cells[:2]]
        v = {
            "snapshot": name,
            "available": True,
            "selection": u(c + 0x20),
            "preview": u(c + 0x1B8),
            "cells": cells,
            "cell_resource_classes": classes,
            "actor_count": n,
            "equipped_channels": channels,
        }
        snap[name] = v
        results.append(v)
    except Exception as e:
        results.append({"snapshot": name, "available": False, "error": str(e)})
checks = []


def check(name, value):
    checks.append({"check": name, "pass": bool(value)})


check("all four snapshots available", len(snap) == 4)
if len(snap) == 4:
    before, first, empty, second = [snap[x] for x in ["before", "item0", "empty", "item1"]]
    check(
        "fixture has dagger1000e cell0, bow1004f cell1 and empty7",
        before["cell_resource_classes"] == [0x1000E, 0x1004F]
        and before["cells"][0] > 0x53
        and before["cells"][1] > 0x53
        and before["cells"][0] != before["cells"][1]
        and before["cells"][7] == 0,
    )
    check(
        "cell0 selected with matching preview",
        first["selection"] == 0 and first["preview"] == before["cells"][0] and first["cells"][0] == before["cells"][0],
    )
    check(
        "cell0 dagger actually equipped in native ED0 channel",
        first["cell_resource_classes"][0] == 0x1000E
        and first["equipped_channels"]["ED0"]["handle"] == before["cells"][0],
    )
    check(
        "empty click preserves selection, preview and both equip channels",
        empty["selection"] == first["selection"]
        and empty["preview"] == first["preview"]
        and empty["equipped_channels"] == first["equipped_channels"],
    )
    check(
        "cell1 selected with matching preview",
        second["selection"] == 1
        and second["preview"] == before["cells"][1]
        and second["cells"][1] == before["cells"][1],
    )
    check(
        "cell1 bow actually equipped in native EAC channel",
        second["cell_resource_classes"][1] == 0x1004F
        and second["equipped_channels"]["EAC"]["handle"] == before["cells"][1],
    )
print(json.dumps({"snapshots": results, "checks": checks}, indent=2))
raise SystemExit(0 if all(c["pass"] for c in checks) else 1)
