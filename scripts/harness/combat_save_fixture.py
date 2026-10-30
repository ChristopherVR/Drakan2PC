"""Private native-disc fixture: relocate Rynn/camera; never edit the source card.
Usage: python combat_save_fixture.py SOURCE_ELF_DIR NEW_DIR X Y Z [--region=pal|usa] [--dragon-near-player] [--dragon-landed] [--dragon-ground-state]
Positions change for Rynn/cameras and optionally the observed dragon. The explicit
landed option also clears the dragon's saved airborne bit (not quest progression).
Ground-state additionally sets native +9c to 1; neither option is quest progress.
Health, inventory, enemy records and quest flags remain byte-identical.
"""

import hashlib
import json
import pathlib
import shutil
import struct
import sys
import zlib

source = pathlib.Path(sys.argv[1]).resolve()
target = pathlib.Path(sys.argv[2]).resolve()
if target.exists():
    raise SystemExit("Target must not already exist")
if source == target or source in target.parents:
    raise SystemExit("Use a separate fixture directory")
xyz = tuple(map(float, sys.argv[3:6]))
if len(xyz) != 3:
    raise SystemExit("X Y Z required")
options = set(sys.argv[6:])
region_options = [option for option in options if option.startswith("--region=")]
if len(region_options) > 1:
    raise SystemExit("Specify one region")
region = region_options[0].partition("=")[2] if region_options else "pal"
options.difference_update(region_options)
registry = json.loads(
    (pathlib.Path(__file__).resolve().parents[1] / "buildtool/regions.json").read_text(encoding="utf-8")
)
if region not in registry["regions"]:
    raise SystemExit("Unknown disc region")
if options - {"--dragon-near-player", "--dragon-landed", "--dragon-ground-state"}:
    raise SystemExit("Unknown fixture option")
if "--dragon-landed" in options and "--dragon-near-player" not in options:
    raise SystemExit("Landed fixture requires --dragon-near-player")
if "--dragon-ground-state" in options and "--dragon-landed" not in options:
    raise SystemExit("Ground state requires --dragon-landed")
profile_path = pathlib.Path(__file__).resolve().parents[2] / registry["regions"][region]["runtime_profile"]
profile = json.loads(profile_path.read_text(encoding="utf-8"))["save_fixture"]
folder = registry["regions"][region]["save_id"] + "Slot1"
name = "Level 1A - Surdana.rsg"
src = source / "mc0" / folder / name
d = src.read_bytes()
idx, count = struct.unpack_from("<II", d, 6)
entries = [struct.unpack_from("<HHHII", d, idx + i * 14) for i in range(count)]
raw = [d[o : o + n] for t, i, s, o, n in entries]
players = []
for n, e in enumerate(entries):
    if e[0] != 0x102:
        continue
    v = zlib.decompress(raw[n])
    if len(v) > 24 and struct.unpack_from("<I", v, 8)[0] == profile["player_class"]:
        players.append((n, v))
if len(players) != 1:
    raise SystemExit(f"Expected one Rynn, found {len(players)}")
old = struct.unpack_from("<3f", players[0][1], 12)
delta = tuple(a - b for a, b in zip(xyz, old))
changed = []
dragon = None
if "--dragon-near-player" in options:
    # This selector is deliberately limited to the inspected original save, whose
    # common actor class also occurs in unrelated small serialized records.
    if hashlib.sha256(d).hexdigest() != "b1fec620193ff5c04bbd00b4328ea9d69b1b3683a28a91b7a0ec40d0d7eeba95":
        raise SystemExit("Dragon fixture requires the inspected original save")
    candidates = [
        n for n, e in enumerate(entries) if e[0] == 0x102 and e[1] == 55 and len(zlib.decompress(raw[n])) == 3162
    ]
    if len(candidates) != 1:
        raise SystemExit("Cannot uniquely identify the observed dragon record")
    dragon = candidates[0]
for n, e in enumerate(entries):
    if e[0] != 0x102:
        continue
    v = bytearray(zlib.decompress(raw[n]))
    if len(v) < 24:
        continue
    typ = struct.unpack_from("<I", v, 8)[0]
    pos = struct.unpack_from("<3f", v, 12)
    # Two camera classes in the observed starting save, and the player itself.
    if n == dragon:
        if typ != 0x1B001041:
            raise SystemExit("Unexpected dragon record class")
        new = (xyz[0] - 500, xyz[1], xyz[2])
        if "--dragon-landed" in options:
            if struct.unpack_from("<II", v, 0x2D6) != (0x40200, 9):
                raise SystemExit("Unexpected observed dragon flags")
            struct.pack_into("<I", v, 0x2DA, 1)
            if "--dragon-ground-state" in options:
                if struct.unpack_from("<I", v, 0x2E2)[0] != 0:
                    raise SystemExit("Unexpected observed dragon state")
                struct.pack_into("<I", v, 0x2E2, 1)
    else:
        if n != players[0][0] and not (
            typ in profile["camera_classes"] and sum((a - b) ** 2 for a, b in zip(pos, old)) < 1500**2
        ):
            continue
        new = tuple(a + b for a, b in zip(pos, delta))
    if new == pos:
        continue
    struct.pack_into("<3f", v, 12, *new)
    raw[n] = zlib.compress(v)
    change = {"entry": e[:3], "class": hex(typ), "before": pos, "after": new}
    if n == dragon and "--dragon-landed" in options:
        change["saved_flags"] = {
            "offset": "0x2da",
            "before": 9,
            "after": 1,
            "meaning": "clear airborne bit 8; forced landed fixture, not quest progress",
        }
    if n == dragon and "--dragon-ground-state" in options:
        change["saved_state"] = {
            "offset": "0x2e2",
            "before": 0,
            "after": 1,
            "meaning": "native landing state +9c; forced fixture, not quest progress",
        }
    changed.append(change)
body = bytearray(d[:16])
newentries = []
for e, v in zip(entries, raw):
    body.extend(b"\0" * ((-len(body)) % 16))
    o = len(body)
    body.extend(v)
    newentries.append((*e[:3], o, len(v)))
body.extend(b"\0" * ((-len(body)) % 16))
ix = len(body)
for e in newentries:
    body.extend(struct.pack("<HHHII", *e))
struct.pack_into("<II", body, 6, ix, count)
target.mkdir(parents=True)
for card in ("mc0", "mc1"):
    if (source / card).is_dir():
        shutil.copytree(source / card, target / card)
(target / "mc0" / folder / name).write_bytes(body)
receipt = {
    "source": str(src),
    "region": region,
    "source_sha256": hashlib.sha256(d).hexdigest(),
    "fixture_sha256": hashlib.sha256(body).hexdigest(),
    "changes": changed,
    "unmodified_resources": count - len(changed),
}
(target / "fixture.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps(receipt, indent=2))
