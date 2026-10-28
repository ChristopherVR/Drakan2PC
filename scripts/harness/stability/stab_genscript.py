#!/usr/bin/env python3
"""Generate a scripted-input file (DRAKAN_INPUT_SCRIPT, see docs/reference/input.md section 11) for the stability matrix.

  stab_genscript.py <scenario> <seed> > script.txt

Scenarios (all start with the logo + New Game taps; frame numbers = PadMgr_Update calls, ~60/s in menus):
  a  intro plays fully, then Level 0 gameplay for ~600 frames, quit
  b  START-skip the intro (3 START taps, then more START like a player mashing), short gameplay, quit
  c  START-skip, Level 0 gameplay ~4300 frames (3 min) with walking / strafing / jumping / attacking / mouse look
  d  START-skip, then open/close pause menu and inventory ~25 times
  e  START-skip, then save (pause menu -> Save Game -> slot) and load (pause menu -> Load Game -> slot)
"""

import random
import sys

sc = sys.argv[1]
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
r = random.Random(seed * 7919 + ord(sc[0]))
L = ["1 log on 600"]


def add(f, s):
    L.append("%d %s" % (f, s))


# logo (~13 s) -> title menu; Cross: New Game, confirm, slot  (jittered so the guest timing differs between boots)
f = 860 + r.randrange(0, 40)
for k in range(3):
    add(f, "pad tap CROSS")
    f += 110 + r.randrange(0, 30)
if sc == "a":
    # 'gameplay' is already reported while the intro movie (gui screen 0x1d) plays, so the sync is satisfied early: wait explicitly until the movie has
    # ended (about 1200 frames) and the level has loaded before walking around
    L.append("0 sync play 30")
    f = 3200 + r.randrange(0, 200)
    for k in range(10):
        add(f, "key down W")
        f += 40 + r.randrange(40)
        add(f, "key up W")
        f += 20
    add(f, "quit")
else:
    # intro movie starts ~ 1180; START skip needs 3 START messages while the movie runs
    f = 1230 + r.randrange(0, 160)
    for k in range(30 + r.randrange(10)):
        add(f, "pad tap START")
        f += 25 + r.randrange(0, 30)
    L.append("0 sync play 20")
    f = 30
    if sc == "b":
        for k in range(6):
            add(f, "key down W")
            f += 30 + r.randrange(40)
            add(f, "key up W")
            f += 10
        add(f, "quit")
    elif sc == "c":
        end = 4300 + r.randrange(0, 200)
        held = set()
        while f < end:
            a = r.randrange(10)
            if a == 0:
                add(f, "key down W")
                held.add("W")
            elif a == 1 and "W" in held:
                add(f, "key up W")
                held.discard("W")
            elif a == 2:
                add(f, "key tap SPACE")
            elif a == 3:
                add(f, "button tap 1")
            elif a == 4:
                add(f, "button tap 2")
            elif a == 5:
                add(f, "mouse %d %d %d" % (r.randrange(-40, 40), r.randrange(-15, 15), 1 + r.randrange(25)))
            elif a == 6:
                add(f, "key down A" if "A" not in held else "key up A")
                (held.discard("A") if "A" in held else held.add("A"))
            elif a == 7:
                add(f, "key down D" if "D" not in held else "key up D")
                (held.discard("D") if "D" in held else held.add("D"))
            elif a == 8:
                add(f, "key tap LCONTROL")
            else:
                add(f, "button tap 1")
                add(f + 4, "key tap SPACE")
            f += 8 + r.randrange(0, 60)
        for h in sorted(held):
            add(f, "key up %s" % h)
        add(f + 10, "quit")
    elif sc == "d":
        for k in range(25):
            add(f, "pad tap START")
            f += 40 + r.randrange(40)  # pause menu opens
            add(f, "pad tap START")
            f += 40 + r.randrange(40)  # and closes (Start = resume)
            add(f, "pad tap SELECT")
            f += 45 + r.randrange(40)  # inventory
            if r.randrange(2):
                add(f, "pad tap L1")
                f += 20
            if r.randrange(2):
                add(f, "pad tap R1")
                f += 20
            add(f, "pad tap SELECT")
            f += 45 + r.randrange(40)
            if r.randrange(3) == 0:
                add(f, "pad tap CIRCLE")
                f += 20
        add(f, "quit")
    elif sc == "e":
        for rep in range(2):
            add(f, "pad tap START")
            f += 60 + r.randrange(30)  # pause menu, first entry = Save Game
            add(f, "pad tap CROSS")
            f += 120 + r.randrange(30)  # save dialog -> slot list
            add(f, "pad tap CROSS")
            f += 120 + r.randrange(30)  # slot 1
            add(f, "pad tap CROSS")
            f += 200 + r.randrange(60)  # overwrite confirm / saved ok
            add(f, "pad tap CROSS")
            f += 120
            add(f, "pad tap CIRCLE")
            f += 60
            add(f, "pad tap START")
            f += 60 + r.randrange(30)
            add(f, "pad tap DPAD_DOWN")
            f += 30
            add(f, "pad tap CROSS")
            f += 120 + r.randrange(30)  # Load Game
            add(f, "pad tap CROSS")
            f += 200 + r.randrange(60)  # slot 1
            add(f, "pad tap CROSS")
            f += 300 + r.randrange(60)  # confirm; level reloads
        L.append("0 sync play 20")
        add(60, "key down W")
        add(160, "key up W")
        add(400, "quit")
print("\n".join(L))
