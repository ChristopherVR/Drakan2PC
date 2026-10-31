"""Co-op character roster: which NPC classes can stand in for a player (docs/journal/2026-10-31/coop-characters.md).

  python -I analysis/scripts/coop_character_roster.py --iso "<game>.iso" --csv analysis/coop_characters.csv \
      --inc ps2recomp/ps2xRuntime/src/lib/drakan_coop_character_table.inc

Findings it encodes (all from the disc, no game data is written, only ids, names and durations in ms):

* No NPC model shares Rynn's skeleton (0x0208 node-name lists: Rynn 578 names, the best NPC overlap is 25 percent), so Rynn's
  clips cannot be played on another model: a partner has to use its own class's clips.
* The humanoid NPC class is rfl class 9 + 0x10090 = 0x1009d ("NPC"); its resource record id is `0x50000 | class id` (NPCS.ODB is
  database 5 of every level) and its animation fields (Run, Walk, Base Idle, Attack Animations, Death ...) hold the clip ids
  (`0x50000 | adb id`, NPCS.ADB; Rynn's are `0x20000 | id`, SYSTEM.ADB).
* The table maps every clip Rynn can play to a coarse activity (idle, walk, run, strafe, attack ...) by the class field it
  sits in, and every roster character to one clip per activity (fallbacks for activities the NPC does not have).
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(ANALYSIS_DIR))
import lvl_dump
import odb_fields

RYNN_CLASS = 2640  # SYSTEM.ODB "Rynn" (resource 0x20a50)
NPC_RFL = 0x9D  # runtime class 0x1009d
ACTS = [
    "Idle",
    "Walk",
    "Run",
    "RunBack",
    "StrafeL",
    "StrafeR",
    "TurnL",
    "TurnR",
    "Jump",
    "InAir",
    "Land",
    "Crouch",
    "Attack",
    "Hit",
    "Block",
    "Draw",
    "Death",
    "Spell",
    "Roll",
    "Other",
]

# Rynn field name -> activity (first match wins)
RYNN_RULES = [
    (r"Death|Drown", "Death"),
    (r"HitRxn", "Hit"),
    (r"Parry|Block", "Block"),
    (r"Draw|Drawn|Reload|Shoot", "Draw"),
    (r"Spell|Magic|Scroll|Crystal|Summon", "Spell"),
    (r"Roll Left|Roll Right|Roll Forward|Roll Back|Cartwheel|Flip", "Roll"),
    (r"Run Back|Backwards|Walk Backward", "RunBack"),
    (r"Strafe L|StrafeL|Strafe Left", "StrafeL"),
    (r"Strafe R|StrafeR|Strafe Right", "StrafeR"),
    (r"Turn L", "TurnL"),
    (r"Turn R", "TurnR"),
    (r"In Air", "InAir"),
    (r"Landing|Archer Land", "Land"),
    (r"Jump", "Jump"),
    (r"Crouch", "Crouch"),
    (r"Attack|RH$|RM\d|RL$|HMR|HML|HMO|HMJ|UB\d|Lunge|Thrust|Kick", "Attack"),
    (r"^Walk|Archer Walk", "Walk"),
    (r"^Run|Archer Run|Swim To Run", "Run"),
    (r"Idle|Ready|Huh", "Idle"),
]


def adb_table(blob):
    """ADB 0x0501 headers: {id: (name, duration in milliseconds)}; the header is `name, f32 seconds, u32 1, u32 65 key frames`."""
    out = {}
    for e in lvl_dump.parse_srsc(blob):
        if e.type == 0x501:
            b = blob[e.offset : e.offset + e.size]
            (n,) = struct.unpack_from("<H", b, 0)
            name = b[2 : 2 + n].rstrip(b"\0").decode("latin1")
            secs = struct.unpack_from("<f", b, 2 + n)[0] if len(b) >= 2 + n + 4 else 0.0
            out[e.id] = (name, round(secs * 1000) if 0 < secs < 600 else 0)
    return out


def anims(cls, name):
    """Clip ids of the animation field `name` (scalar or array), without zeros."""
    for f in cls["fields"]:
        if f["name"] == name and "animation" in f["type_name"]:
            v = f["default"]
            vs = v if isinstance(v, list) else [v]
            return [x for x in vs if isinstance(x, int) and x]
    return []


def rynn_clip_activities(rynn):
    out = {}
    for f in rynn["fields"]:
        if "animation" not in f["type_name"]:
            continue
        act = next((a for pat, a in RYNN_RULES if re.search(pat, f["name"])), "Other")
        v = f["default"]
        for x in v if isinstance(v, list) else [v]:
            if isinstance(x, int) and x and x not in out:
                out[x] = act
    return out


def npc_clips(cls):
    """One clip per activity for an NPC class (None = the class has none; the C++ falls back)."""

    def first(*names):
        for n in names:
            a = anims(cls, n)
            if a:
                return a[0]
        return 0

    c = {a: 0 for a in ACTS}
    c["Idle"] = first("Base Idle", "Spontaneous Idle Anims")
    c["Walk"] = first("Walk")
    c["Run"] = first("Run")
    c["RunBack"] = first("Walk Backwards")
    c["StrafeL"] = first("Strafe Left")
    c["StrafeR"] = first("Strafe Right")
    c["TurnL"] = first("Turn Left")
    c["TurnR"] = first("Turn Right")
    c["Attack"] = first("Attack Animations", "Upper Body Attacks")
    c["Hit"] = first("Hit Rxn Med damage anim", "Hit Rxn Low damage anim", "Hit Rxn High damage anim")
    c["Death"] = first("Death")
    c["Spell"] = first("Summoning Animation", "Taunt Animation")
    c["Crouch"] = first("Cower", "Crouch Ready")
    c["Block"] = first("Block Mid Animations", "Combat Ready")
    c["Draw"] = first("Aim Arrow anim", "Load Arrow anim")
    return c


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--csv", type=Path)
    ap.add_argument("--inc", type=Path)
    args = ap.parse_args(argv)
    from ui_layout_dump import iso_files, read

    f, files = iso_files(str(args.iso))
    try:
        get = lambda p: read(f, files[p])
        sys_odb = odb_fields.load_classes(get("/ENGLISH/COMMON/SYSTEM/SYSTEM.ODB"))
        npc_odb = odb_fields.load_classes(get("/ENGLISH/COMMON/NPC/NPCS.ODB"))
        sys_adb = adb_table(get("/ENGLISH/COMMON/SYSTEM/SYSTEM.ADB"))
        npc_adb = adb_table(get("/ENGLISH/COMMON/NPC/NPCS.ADB"))
    finally:
        f.close()

    rynn_acts = rynn_clip_activities(sys_odb[RYNN_CLASS])
    rows = []
    for cid, c in sorted(npc_odb.items()):
        if c["rfl_class"] != NPC_RFL or not c.get("model"):
            continue
        clips = npc_clips(c)
        ok = all(clips[a] for a in ("Idle", "Walk", "Run", "Attack", "Death"))
        rows.append((cid, c["name"].strip(), c["model"][0], ok, clips))

    if args.csv:
        with args.csv.open("w", newline="\n") as fh:
            fh.write("class_id,record_id,name,model,usable," + ",".join(ACTS) + "\n")
            for cid, name, model, ok, clips in rows:
                fh.write(
                    "0x%x,0x%x,%s,%d,%d,%s\n"
                    % (
                        cid,
                        0x50000 | cid,
                        name.replace(",", ";"),
                        model,
                        int(ok),
                        ",".join(str(clips[a]) for a in ACTS),
                    )
                )
    if args.inc:
        with args.inc.open("w", newline="\n") as fh:
            fh.write(
                "// Generated by analysis/scripts/coop_character_roster.py: ids, names and durations in ms only.\n"
            )
            fh.write("// Rynn clips: {clip resource id (0x20000 | SYSTEM.ADB id), activity, duration ms}\n")
            fh.write("constexpr RynnClip kRynnClips[] = {\n")
            for x, act in sorted(rynn_acts.items()):
                fh.write(
                    "    {0x%x, Act::%s, %d}, // %s\n"
                    % (0x20000 | x, act, sys_adb.get(x, ("?", 0))[1], sys_adb.get(x, ("?",))[0])
                )
            fh.write("};\n")
            fh.write(
                "// Characters: {class id, name, model id, usable, clips by Act {clip resource id 0x50000 | NPCS.ADB id, duration ms}}\n"
            )
            fh.write("constexpr CharacterRow kCharacterRows[] = {\n")
            for cid, name, model, ok, clips in rows:
                cells = ", ".join(
                    "{0x%x, %d}" % ((0x50000 | clips[a]) if clips[a] else 0, npc_adb.get(clips[a], ("", 0))[1])
                    for a in ACTS
                )
                fh.write('    {0x%x, "%s", %d, %s, {%s}},\n' % (cid, name, model, "true" if ok else "false", cells))
            fh.write("};\n")
    print("rynn clips %d, npc classes %d (usable %d)" % (len(rynn_acts), len(rows), sum(1 for r in rows if r[3])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
