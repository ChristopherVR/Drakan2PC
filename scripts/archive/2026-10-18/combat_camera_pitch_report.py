"""Read-only extra camera fields for combat_camera_pitch_followup.txt.

No launches or inferred passes. Native actor/HP availability comes from the
existing combat reporter. Preserve its unavailable results, never invent zeros.
"""

import json
import math
import pathlib
import struct
import subprocess
import sys


def report(directory):
    snapshots = json.loads(
        subprocess.check_output(
            [
                sys.executable,
                str(pathlib.Path(__file__).resolve().parents[2] / "harness" / "combat_ram_report.py"),
                str(directory),
            ],
            text=True,
        )
    )
    for snapshot in snapshots:
        if not snapshot.get("rynn_available"):
            continue
        try:
            ram = (directory / snapshot["ram"]).read_bytes()
            player = int(snapshot["player"], 16)
            cam = int(snapshot["camera_object"], 16)
            if len(ram) != 0x2000000 or not 0x100000 <= cam <= len(ram) - 0x150:
                raise ValueError("camera unavailable")

            def word(address):
                return struct.unpack_from("<I", ram, address)[0]

            def real(address):
                value = struct.unpack_from("<f", ram, address)[0]
                if not math.isfinite(value):
                    raise ValueError(f"nonfinite camera value at {address:#x}")
                return value

            ctx = word(0x5565D4)
            flags = word(cam + 0x48)
            vt = word(player + 0x57C)
            if not 0x100000 <= vt <= len(ram) - 0x180:
                raise ValueError("pitch vtable unavailable")
            predicate = word(vt + 0x17C)
            adjustment = struct.unpack_from("<h", ram, vt + 0x178)[0]
            adjusted = player + adjustment
            if not 0x100000 <= adjusted <= len(ram) - 0xAF4:
                raise ValueError("pitch predicate object unavailable")
            state94, stateab0, stateaf0 = word(adjusted + 0x94), word(adjusted + 0xAB0), word(adjusted + 0xAF0)
            veto = None
            if predicate == 0x32CDA8:
                veto = bool(
                    state94 & 0xC000
                    or ((state94 & 0x80) and not (stateab0 & 0x200800))
                    or ((state94 & 0x10) and stateaf0 in (3, 4))
                )
            elif predicate == 0x276E40:
                veto = False
            ref = snapshot["target_slot4da"]
            handle = None
            if ref:
                base, count = word(ctx + 0x7C), word(ctx + 0x84)
                address = base + (ref - 1) * 8
                if count > 10000 or ref > count + 1 or not 0x100000 <= address <= len(ram) - 8:
                    raise ValueError("target registry unavailable")
                handle = hex(word(address))
            snapshot["pitch_evidence"] = {
                "available": True,
                "camera_flags48": hex(flags),
                "camera_mode14c": word(cam + 0x14C),
                "native_alternate_channel": not bool(flags & 0x1000) and bool(flags & 0x800),
                "native_control": word(0x4FB730),
                "scripted_flow": word(0x4FBAFC),
                "aim_flag4000": bool(word(player + 0x90) & 0x4000),
                "pitch_axis_suppressed": bool(word(player + 0x94) & 4),
                "anchor114": real(player + 0x114),
                "target_handle": handle,
                "pitch_predicate": hex(predicate),
                "pitch_this_adjustment": adjustment,
                "native_pitch_veto": veto,
                "predicate_state94": hex(state94),
                "predicate_stateab0": hex(stateab0),
                "predicate_stateaf0": stateaf0,
                "pitch84": real(cam + 0x84),
                "pitch_rate9c": real(cam + 0x9C),
                "turn_ratea0": real(cam + 0xA0),
                "orbit_ratea4": real(cam + 0xA4),
            }
        except (OSError, ValueError, KeyError, struct.error) as error:
            snapshot["pitch_evidence"] = {"available": False, "reason": str(error)}
    return snapshots


if __name__ == "__main__":
    print(json.dumps(report(pathlib.Path(sys.argv[1])), indent=2, allow_nan=False))
