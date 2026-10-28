"""Read-only SCES_500.06 combat RAM evidence. No inferred pass verdicts.
Player stats: 3d56b0/3d56e0 and 3d57c8/3d5838.
NPC health: 4738e8 uses +24c and invokes health subtraction via its vtable.
"""

import json
import math
import pathlib
import struct
import sys


def report(path):
    b = path.read_bytes()
    if len(b) != 0x2000000:
        raise ValueError("Expected 32 MiB guest RAM")

    def valid(a, n=4):
        return 0x100000 <= (a & 0x1FFFFFFF) <= len(b) - n

    def u(a):
        return struct.unpack_from("<I", b, a & 0x1FFFFFF)[0]

    def h(a):
        return struct.unpack_from("<H", b, a & 0x1FFFFFF)[0]

    def si(a):
        return struct.unpack_from("<i", b, a & 0x1FFFFFF)[0]

    def xyz(a):
        return list(struct.unpack_from("<3f", b, a & 0x1FFFFFF))

    def obj(handle):
        if not valid(handle, 0x100):
            return 0
        ins = u(handle + 0xFC)
        return u(ins + 0x10) if valid(ins, 0x18) else 0

    ct = u(0x5565D4)

    def unavailable(reason, **details):
        return {"ram": path.name, "rynn_available": False, "reason": reason, **details}

    if not valid(ct, 0x4B8):
        return unavailable("game_context_unavailable")
    controlled = u(ct + 0x4B0)
    key = u(0x4FE86C)
    tb = u(ct + 0x7C)
    n = min(u(ct + 0x84), 10000)
    resources = u(ct + 0x320)
    if not valid(resources, 0x14) or not valid(tb, max(8, n * 8)):
        return unavailable("actor_registry_unavailable")

    def resource_class(handle):
        if not valid(handle, 0x12):
            return None
        ix = h(handle + 0x10)
        if ix == 0xFFFF or ix > u(resources + 0x10):
            return None
        record = u(resources + 8) + ix * 0x24
        return u(record + 8) if valid(record, 0x24) else None

    # Controlled actor can change on mounting: never interpret dragon memory as Rynn.
    ph = controlled
    if resource_class(ph) != 0x10009:
        candidates = [u(tb + i * 8) for i in range(n) if resource_class(u(tb + i * 8)) == 0x10009]
        if len(candidates) != 1:
            return unavailable(
                "cannot_uniquely_identify_rynn", candidate_count=len(candidates), controlled_handle=hex(controlled)
            )
        ph = candidates[0]
    p = obj(ph)
    if not valid(p, 0x1050):
        return unavailable("rynn_data_unavailable", handle=hex(ph))

    def dec(off):
        x = u(0x4FBAC8 + off) ^ key
        x = (x >> 8 | x << 24) & 0xFFFFFFFF
        return x if x < 0x80000000 else x - 0x100000000

    out = {
        "ram": path.name,
        "player": hex(p),
        "position": xyz(ph + 0x14),
        "hp": dec(0xC),
        "hp_max": dec(0x10),
        "mana": dec(0x1C),
        "mana_max": dec(0x20),
        "flags90": hex(u(p + 0x90)),
        "flags94": hex(u(p + 0x94)),
        "item_eac": hex(h(p + 0xEAC)),
        "item_ed0": hex(h(p + 0xED0)),
        "attack_state1040": u(p + 0x1040),
        "animation_aec": u(p + 0xAEC),
        "attack_b1c": u(p + 0xB1C),
        "attack_b28": u(p + 0xB28),
        "mode_e90": u(p + 0xE90),
        "mode_eb4": u(p + 0xEB4),
        "camera_object": hex(obj(u(ct + 0x4B4))),
        "target_a78": hex(u(p + 0xA78)),
        "dragon_ref4d8": h(p + 0x4D8),
        "target_slot4da": h(p + 0x4DA),
        "heading_matrix": list(struct.unpack_from("<9f", b, ph + 0x20)),
        "npcs": [],
        "projectiles": [],
    }
    out["rynn_available"] = True
    out["controlled_handle"] = hex(controlled)
    out["controlled_class"] = resource_class(controlled)
    cam = obj(u(ct + 0x4B4))
    out["camera_pitch_rad"] = struct.unpack_from("<f", b, cam + 0x84)[0] if valid(cam, 0x88) else None
    # Angle of the first native matrix basis vector in its X/Z plane. Keep the
    # matrix too: this is an observable heading delta, not a camera yaw guarantee.
    matrix = out["heading_matrix"]
    out["heading_basis_xz_rad"] = math.atan2(matrix[2], matrix[0])
    out["equipped_channels"] = {}
    for channel, offset in [("EAC", 0xEAC), ("ED0", 0xED0)]:
        ref = h(p + offset)
        handle = u(tb + (ref - 1) * 8) if 0 < ref <= n else 0
        out["equipped_channels"][channel] = {
            "ref": ref,
            "handle": hex(handle),
            "resource_class": resource_class(handle),
        }
    out["dragon_actors"] = []
    out["spell_projectiles"] = []
    for i in range(n):
        handle = u(tb + i * 8)
        if resource_class(handle) != 0x1001C:
            continue
        data = obj(handle)
        actor = {"slot": i + 1, "handle": hex(handle), "data": hex(data), "position": xyz(handle + 0x14)}
        if valid(data, 0xA0):
            actor.update({"flags90": hex(u(data + 0x90)), "flags94": hex(u(data + 0x94)), "state9c": u(data + 0x9C)})
        out["dragon_actors"].append(actor)
    out["flags_ab0"] = hex(u(p + 0xAB0))
    out["damage_reduction_window4000"] = bool(u(p + 0xAB0) & 0x4000)
    # Inventory-close observations, not a verdict about which path executed.
    screen = u(0x4FB60C)
    controller = p + 0x584
    out["inventory"] = {
        "screen": hex(screen),
        "global_active": u(0x4FBAFC),
        "global_mode": u(0x4FBAC8 + 0x70),
        "player104c": si(p + 0x104C),
        "controller0c": u(controller + 0xC),
    }
    if valid(screen, 0x1AC):
        out["inventory"].update({"screen_id": u(screen + 0x18), "section": u(screen + 0x154)})
    child = u(controller + 0x18)
    if valid(child, 0x28):
        out["inventory"]["controller_child24"] = u(child + 0x24)
    sv = u(p + 0x1028)
    sn = min(u(p + 0x1030), 32)
    if valid(sv, sn * 20):
        out["spells"] = [
            {"level": u(sv + i * 20), "type": u(sv + i * 20 + 4), "state": u(sv + i * 20 + 16)} for i in range(sn)
        ]
    for i in range(n):
        handle = u(tb + i * 8)
        if not valid(handle, 0x100):
            continue
        q = obj(handle)
        ins = u(handle + 0xFC)
        if u(handle + 0xC) == 9 and resource_class(handle) == 0x10008 and valid(ins, 0x18):
            out["spell_projectiles"].append(
                {
                    "slot": i + 1,
                    "handle": hex(handle),
                    "factory": hex(u(ins + 0x14)),
                    "data": hex(q),
                    "position": xyz(handle + 0x14),
                }
            )
        if u(handle + 0xC) == 13 and valid(ins, 0x18):
            pos = xyz(handle + 0x14)
            if all(math.isfinite(v) for v in pos):
                out["projectiles"].append(
                    {
                        "slot": i + 1,
                        "handle": hex(handle),
                        "factory": hex(u(ins + 0x14)),
                        "data": hex(q),
                        "position": pos,
                    }
                )
        if not valid(q, 0x3B4) or u(q + 0x23C) != handle:
            continue
        vt = u(q + 0x268)
        if not 0x500000 <= vt < 0x556000:
            continue
        pos = xyz(handle + 0x14)
        if not all(math.isfinite(v) for v in pos):
            continue
        dist = math.dist(pos, out["position"])
        if dist > 30000:
            continue
        out["npcs"].append(
            {
                "slot": i + 1,
                "handle": hex(handle),
                "data": hex(q),
                "position": pos,
                "distance": dist,
                "hp": si(q + 0x24C),
                "hp_max": si(q + 0x250),
                "ai_state": u(q + 0x3B0),
                "target_slot": h(q + 0x244),
                "flags": hex(u(q + 0x3A8)),
            }
        )
    ref = h(p + 0xEAC)
    if 0 < ref <= n:
        item = u(tb + (ref - 1) * 8)
        data = obj(item)
        if valid(data, 0x1C):

            def quantity(off):
                x = u(data + off) ^ key
                return (x >> 8 | x << 24) & 0xFFFFFFFF

            out["selected_item"] = {
                "slot": ref,
                "handle": hex(item),
                "data": hex(data),
                "quantity_candidate": quantity(0x14),
                "maximum_candidate": quantity(0x18),
            }
    return out


paths = []
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    paths.extend(sorted(p.glob("ram*.ram")) if p.is_dir() else [p])
results = []
for p in paths:
    try:
        result = report(p)
        # Validate each result before serializing the batch (including finite floats).
        json.dumps(result, allow_nan=False)
    except Exception as error:
        result = {
            "ram": p.name,
            "rynn_available": False,
            "reason": "parse_error",
            "error_type": type(error).__name__,
            "error": str(error),
        }
    results.append(result)
print(json.dumps(results, indent=2, allow_nan=False))
