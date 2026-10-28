"""Resolve render owner -> resource manager -> exact mesh, read-only RAM evidence."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("ram", type=Path)
p.add_argument("objects", nargs="+", type=lambda s: int(s, 16))
a = p.parse_args()
r = a.ram.read_bytes()
u = lambda x: struct.unpack_from("<I", r, x)[0]
valid = lambda x, n: 0x100000 <= x <= len(r) - n
result = []
for obj in a.objects:
    assert valid(obj, 0x120) and u(obj) == 0x51B8B0
    handle = u(obj + 8)
    # 12b6b8..12b700 hash buckets and next pointer; no guest execution.
    node = u(0x4DAE20 + 0x20 + (((handle * 0x59) & 0x7F) * 4))
    seen = set()
    while valid(node, 0xAC) and u(node) != handle and node not in seen:
        seen.add(node)
        node = u(node + 0x14)
    item = dict(owner=hex(obj), handle=hex(handle), controller=hex(u(obj + 0x50)))
    if valid(node, 0xAC) and u(node) == handle:
        table = u(node + 0xA8)
        controller = u(obj + 0x50)
        slot = u(controller + 0x10) if valid(controller, 0x124) else 0
        if valid(table, 4) and slot < 4096:
            mesh = u(table) + slot * 0x28
            item.update(resource=hex(node), mesh=hex(mesh), slot=slot)
            if valid(mesh, 0x28):
                count, base = u(mesh + 0xC), u(mesh + 0x10)
                item["parts"] = []
                if count <= 4096 and valid(base, count * 0x30):
                    for i in range(count):
                        part = base + i * 0x30
                        groups, group_base = u(part + 0x28), u(part + 0x20)
                        entry = dict(
                            part=hex(part),
                            original_vertices=u(part + 0x10),
                            original_indices=hex(u(part + 0x2C)),
                            groups=groups,
                        )
                        item["parts"].append(entry)
                        entry["sources"] = []
                        if groups <= 65536 and valid(group_base, groups * 16):
                            for g in range(groups):
                                desc = group_base + g * 16
                                n, uv, pos = u(desc), u(desc + 4), u(desc + 8)
                                if 0 < n <= 256 and valid(pos, n * 12) and valid(uv, n * 8):
                                    entry["sources"].append(
                                        dict(
                                            group=hex(desc),
                                            count=n,
                                            positions=hex(pos),
                                            positions_sha256=hashlib.sha256(r[pos : pos + n * 12]).hexdigest(),
                                            uv_sha256=hashlib.sha256(r[uv : uv + n * 8]).hexdigest(),
                                        )
                                    )
    result.append(item)
print(json.dumps(result, indent=2))
