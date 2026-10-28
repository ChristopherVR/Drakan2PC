"""Read-only seven native category/page-state assertions."""

import argparse
import json
import pathlib
import struct

p = argparse.ArgumentParser()
p.add_argument("directory", type=pathlib.Path)
a = p.parse_args()
checks = []
states = {}
errors = []


def raw(name):
    b = (a.directory / f"ram_cat_{name}.ram").read_bytes()
    if len(b) != 0x2000000:
        raise ValueError("RAM size must be32MiB")
    return b


def rd(b, p):
    if not 0x100000 <= p <= len(b) - 4:
        raise ValueError(f"bad pointer {p:x}")
    return struct.unpack_from("<I", b, p)[0]


try:
    baseline = raw("baseline")
    screen = rd(baseline, 0x4FB60C)
    if rd(baseline, screen + 0x18) != 21:
        raise ValueError("baseline is not native inventory")
except Exception as e:
    errors.append(str(e))
    screen = 0
keys = [0x437CD0, 0x437CD0, 0x445898, 0x43A588, 0x439090]
for name in ["equipment", "spells", "skills", "map", "journal", "outside", "return"]:
    try:
        b = raw(name)
        active = rd(b, 0x4FB60C)
        section = rd(b, screen + 0x154)
        table = rd(b, screen + 0x1A8)
        if section >= 5:
            raise ValueError(f"invalid section {section}")
        page = rd(b, table + section * 4)
        vt = rd(b, page + 0x18)
        player = rd(b, screen + 0x14C)
        c = player + 0x584
        states[name] = {
            "active_screen": active,
            "active_id": rd(b, active + 0x18),
            "inventory_screen": screen,
            "section": section,
            "page_object": page,
            "key_handler": rd(b, vt + 0x3C),
            "equipment_mode": rd(b, c + 0x24),
            "equipment_selection": rd(b, c + 0x20),
            "equipment_preview": rd(b, c + 0x1B8),
            "equipped_references": [struct.unpack_from("<H", b, player + x)[0] for x in [0xEAC, 0xED0]],
            "journal_state": [rd(b, rd(b, table + 16) + x) for x in [0x58, 0x5C, 0x64]],
            "opening_velocity": struct.unpack_from("<f", b, screen + 0x15C)[0],
        }
    except Exception as e:
        errors.append(f"{name}: {e}")
for expected, name in enumerate(["equipment", "spells", "skills", "map", "journal"]):
    s = states.get(name, {})
    ok = (
        s.get("section") == expected
        and s.get("active_screen") == screen
        and s.get("active_id") == 21
        and s.get("key_handler") == keys[expected]
    )
    checks.append({"check": f"absolute icon {expected} selects native {name} page", "pass": ok})
old, new = states.get("journal", {}), states.get("outside", {})
fields = [
    "active_screen",
    "active_id",
    "section",
    "page_object",
    "journal_state",
    "equipment_selection",
    "equipment_preview",
    "equipped_references",
]
log = (a.directory / "input.log").read_text(errors="replace")
start = log.find("ram_cat_journal.ram")
end = log.find("ram_cat_outside.ram", start)
no_adapter = start >= 0 and end > start and "inventory mouse input" not in log[start:end]
checks.append(
    {
        "check": "outside icon gap preserves native page/selection/equipment and emits no adapter tap",
        "pass": bool(old and new and all(old[x] == new[x] for x in fields) and no_adapter),
    }
)
s = states.get("return", {})
checks.append(
    {
        "check": "absolute Equipment return observes native Equipment controller active",
        "pass": s.get("section") == 0
        and s.get("active_id") == 21
        and s.get("active_screen") == screen
        and s.get("key_handler") == keys[0]
        and s.get("equipment_mode") == 0,
    }
)
report = {
    "states": states,
    "checks": checks,
    "errors": errors,
    "limits": [
        "Top categories only; other section content absolute selection not tested",
        "Help is confirmed with ordinary Enter; prehelp RAM captures retained",
    ],
}
print(json.dumps(report, indent=2))
raise SystemExit(0 if not errors and all(x["pass"] for x in checks) else 1)
